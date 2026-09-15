// SPDX-License-Identifier: GPL-2.0-only

#include "yahoodataprovider.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace hub75 {
namespace {

// Be a reasonably polite client of an unofficial, unrated API - fetch a
// handful of symbols at a time rather than firing off dozens of requests
// the moment update() is called.
constexpr int kMaxConcurrent = 4;

// A generic desktop browser UA - the endpoint has been observed to reject
// requests carrying no/an unusual User-Agent.
const char *kUserAgent = "Mozilla/5.0 (X11; Linux x86_64) hub75stock/1.0";

// Without an explicit timeout, a request that hangs rather than cleanly
// failing (confirmed for real: a burst of "Connection closed" errors on
// every in-flight symbol at once, then total silence from every later poll
// - no new success or failure logged for any symbol, ever) never emits
// QNetworkReply::finished() at all, so handleReply() never runs, inFlight_
// never decrements, and that request's queue slot is gone for the rest of
// the process's life. 20s is generous next to how small this response is,
// while still comfortably clear of even the shortest allowed
// updateIntervalSeconds (60s).
constexpr int kRequestTimeoutMs = 20000;

} // namespace

YahooDataProvider::YahooDataProvider(QObject *parent) : QObject(parent) {}

void YahooDataProvider::ensureSymbols(const QVector<Symbol> &symbols)
{
    for (const Symbol &symbol : symbols) {
        if (!symbols_.contains(symbol))
            symbols_.append(symbol);
    }
}

void YahooDataProvider::update()
{
    // If the previous round left any symbol failed, force fresh TCP/TLS
    // connections for this retry rather than risk reusing ones that may
    // have gone stale in the meantime - confirmed for real on live
    // hardware: a burst of near-simultaneous "Connection closed" errors
    // (Qt's own qt.network.http2 log category) across every symbol that
    // happened to be in flight at once, the signature of a shared HTTP/2
    // connection the far end had already closed by the time this app tried
    // to reuse it for the next poll. Only done when recovering from a
    // failure, not on every single poll - a longer, healthy-running
    // interval (see updateIntervalSeconds) actually makes a connection
    // going stale between polls *more* likely, not less, so this matters
    // more now than it would have at a fixed 60s cadence, not less.
    if (hasDataIssue())
        network_.clearConnectionCache();

    // Refills the queue with every known symbol; fetchNext() drains it
    // kMaxConcurrent at a time. If the previous round is somehow still
    // running (a slow connection), a symbol already pending just doesn't
    // get queued twice rather than piling up duplicate in-flight requests.
    for (const Symbol &symbol : symbols_) {
        if (!pending_.contains(symbol))
            pending_.append(symbol);
    }
    fetchNext();
}

void YahooDataProvider::fetchNext()
{
    while (inFlight_ < kMaxConcurrent && !pending_.isEmpty()) {
        const Symbol symbol = pending_.takeFirst();
        const QString yahooSymbol = symbol.toYahooSymbol();
        if (yahooSymbol.isEmpty())
            continue; // exchange not in the mapping table - nothing to fetch

        QUrl url(QStringLiteral("https://query1.finance.yahoo.com/v8/finance/chart/%1")
                     .arg(yahooSymbol));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("range"), QStringLiteral("1d"));
        query.addQueryItem(QStringLiteral("interval"), QStringLiteral("1m"));
        query.addQueryItem(QStringLiteral("includePrePost"), QStringLiteral("false"));
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, QByteArray(kUserAgent));
        request.setTransferTimeout(kRequestTimeoutMs);

        ++inFlight_;
        QNetworkReply *reply = network_.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, symbol, reply]() {
            --inFlight_;
            handleReply(symbol, reply);
            reply->deleteLater();
            fetchNext(); // pick up whatever's left in the queue, if anything
        });
    }
}

void YahooDataProvider::handleReply(const Symbol &symbol, QNetworkReply *reply)
{
    // On any failure, simply leave byKey_'s existing entry (if any) alone -
    // the next update() cycle tries again; there's no partial/corrupt state
    // to clean up since a snapshot is only ever replaced wholesale, below.
    // Every early-return path below logs why, via qWarning - previously none
    // of this was visible anywhere, so a fetch that silently started failing
    // partway through a session (e.g. Yahoo rate-limiting a long-polling
    // client) left the chart frozen with no trace of the cause. Under the
    // systemd service these land in the journal (`journalctl -u hub75stock`).
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "YahooDataProvider: fetch failed for" << symbol.toConfigString()
                   << "-" << reply->errorString();
        lastFetchFailed_.insert(symbol.toConfigString(), true);
        return;
    }

    const QJsonObject chart = QJsonDocument::fromJson(reply->readAll())
                                   .object().value(QStringLiteral("chart")).toObject();
    const QJsonValue chartError = chart.value(QStringLiteral("error"));
    if (!chartError.isNull()) {
        qWarning() << "YahooDataProvider: Yahoo returned an error for" << symbol.toConfigString()
                   << "-" << chartError;
        lastFetchFailed_.insert(symbol.toConfigString(), true);
        return;
    }
    const QJsonArray results = chart.value(QStringLiteral("result")).toArray();
    if (results.isEmpty()) {
        qWarning() << "YahooDataProvider: empty result array for" << symbol.toConfigString();
        lastFetchFailed_.insert(symbol.toConfigString(), true);
        return;
    }

    const QJsonObject result = results.first().toObject();
    const QJsonObject meta = result.value(QStringLiteral("meta")).toObject();
    const QJsonArray timestamps = result.value(QStringLiteral("timestamp")).toArray();
    const QJsonArray quoteArr = result.value(QStringLiteral("indicators")).toObject()
                                      .value(QStringLiteral("quote")).toArray();
    if (timestamps.isEmpty() || quoteArr.isEmpty()) {
        qWarning() << "YahooDataProvider: no timestamp/quote data for" << symbol.toConfigString();
        lastFetchFailed_.insert(symbol.toConfigString(), true);
        return;
    }

    const QJsonObject quote = quoteArr.first().toObject();
    const QJsonArray opens = quote.value(QStringLiteral("open")).toArray();
    const QJsonArray highs = quote.value(QStringLiteral("high")).toArray();
    const QJsonArray lows = quote.value(QStringLiteral("low")).toArray();
    const QJsonArray closes = quote.value(QStringLiteral("close")).toArray();

    const QJsonObject regularPeriod = meta.value(QStringLiteral("currentTradingPeriod")).toObject()
                                           .value(QStringLiteral("regular")).toObject();
    const qint64 sessionStart = regularPeriod.value(QStringLiteral("start")).toInteger();
    const qint64 sessionEnd = regularPeriod.value(QStringLiteral("end")).toInteger();

    // Aggregate 1-minute bars into buckets, keyed by elapsed time since the
    // first returned bar - deliberately not
    // meta.currentTradingPeriod.regular.start: on a request made outside
    // that period (e.g. over the weekend, or for an exchange far enough
    // from UTC that "today" server-side isn't the same calendar day as the
    // exchange's), that metadata can describe a *different* session than
    // the one the timestamp array actually covers, which silently produced
    // zero buckets for every single data point (seen for real testing
    // ASX:BHP on a Sunday). The data's own first timestamp is always a
    // valid anchor for grouping nearby bars, regardless of what the
    // metadata says about "the current session".
    const qint64 firstTimestamp = timestamps.first().toInteger();

    // Bucket width is sized to the exchange's actual *regular session
    // length* (sessionEnd - sessionStart), not the fixed
    // pricedata::kBucketMinutes MockDataProvider uses - so every exchange's
    // chart fills the full column width by close regardless of how long its
    // day runs (a 6.5h NYSE session and an 8.5h Xetra session should both
    // look like a complete day's chart, not one visibly shorter/emptier
    // just because the exchange trades for fewer hours).
    //
    // Deliberately *not* sized from how much data happens to be available
    // at fetch time (i.e. not "timestamps.last() - timestamps.first()"),
    // which was the bug in an earlier version of this: early in a session,
    // only a few minutes of bars exist yet (plus Yahoo's free/unauthenticated
    // feed runs a real ~15-20 minute reporting lag on top of that - confirmed
    // for real: at 09:51 CEST, 51 minutes into Xetra's 09:00 open, the
    // newest bar returned was timestamped 09:33), so that span is naturally
    // tiny early on. Dividing a tiny span by kBucketCount produced very fine
    // buckets that the (relatively few) available minutes then nearly
    // filled up on their own - a chart reading as "almost half full" after
    // barely a tenth of the session had actually elapsed, not proportional
    // to real progress through the day at all. Using the session's fixed,
    // known duration instead keeps the bucket width constant all day, so
    // the chart fills up in proportion to actual elapsed session time and
    // reaches exactly 100% at close - "left edge is open, right edge is
    // close" (see README "Live data"), not "left edge is open, right edge
    // is however much data Yahoo happened to have a moment ago".
    //
    // The session metadata's exact start/end *timestamps* aren't reliable
    // for anchoring (see above - it can describe a different session
    // entirely), but the session's *duration* is: a given exchange's
    // regular trading hours are essentially constant day to day (bar the
    // rare early-close holiday), so reusing it purely as a length is safe
    // even when the metadata is otherwise describing the wrong day.
    // Falls back to the fixed default if the metadata is missing/malformed,
    // and is never allowed to be shorter than what's already been observed
    // today - the actual data seen so far is a hard lower bound on how long
    // the session has to be, regardless of what the metadata claims.
    qint64 sessionDurationSeconds = sessionEnd > sessionStart ? (sessionEnd - sessionStart) : 0;
    if (sessionDurationSeconds <= 0)
        sessionDurationSeconds = qint64(pricedata::kBucketMinutes) * 60 * pricedata::kBucketCount;
    const qint64 lastTimestamp = timestamps.last().toInteger();
    const qint64 observedSpanSeconds = std::max<qint64>(0, lastTimestamp - firstTimestamp);
    sessionDurationSeconds = std::max(sessionDurationSeconds, observedSpanSeconds);

    const qint64 bucketSeconds =
        std::max<qint64>(60, (sessionDurationSeconds + pricedata::kBucketCount - 1)
                                  / pricedata::kBucketCount);

    // Aggregated real data only, keyed by bucketIndex - a QMap rather than
    // appending sequentially to a plain array, specifically so two real
    // bars that are far apart in time (a thinly-traded listing can easily
    // have long stretches with no trades at all) don't end up adjacent in
    // the buckets array just because they happened to be the next two
    // *non-empty* minutes seen. Sequential appending was exactly this bug:
    // it silently compressed the whole day's sparse trades into a
    // contiguous run at the start, which read as "brief burst of activity
    // at market open" - not the fact of the matter (trades spread out
    // across the day with long gaps in between).
    QMap<int, PriceBucket> aggregated;
    const int n = timestamps.size();
    for (int i = 0; i < n; ++i) {
        if (i >= opens.size() || i >= highs.size() || i >= lows.size() || i >= closes.size())
            break;
        if (opens.at(i).isNull() || highs.at(i).isNull() || lows.at(i).isNull()
            || closes.at(i).isNull())
            continue; // a minute with no trades - skip rather than guess

        const qint64 ts = timestamps.at(i).toInteger();
        const int bucketIndex = static_cast<int>((ts - firstTimestamp) / bucketSeconds);
        if (bucketIndex < 0)
            continue;

        const float o = static_cast<float>(opens.at(i).toDouble());
        const float h = static_cast<float>(highs.at(i).toDouble());
        const float l = static_cast<float>(lows.at(i).toDouble());
        const float c = static_cast<float>(closes.at(i).toDouble());

        const auto it = aggregated.find(bucketIndex);
        if (it == aggregated.end()) {
            PriceBucket fresh;
            fresh.open = o;
            fresh.high = h;
            fresh.low = l;
            fresh.close = c;
            aggregated.insert(bucketIndex, fresh);
        } else {
            it->high = std::max(it->high, h);
            it->low = std::min(it->low, l);
            it->close = c;
        }
    }

    if (aggregated.isEmpty()) {
        qWarning() << "YahooDataProvider: response for" << symbol.toConfigString()
                   << "had no usable (non-null) bars - keeping previous snapshot";
        lastFetchFailed_.insert(symbol.toConfigString(), true);
        return;
    }

    // "Daily change" is measured against the *previous trading day's
    // close* - the same convention every ticker/chart site uses - not
    // today's own opening print. Confirmed for real the two can disagree
    // substantially: NVD.DE gapped down ~2.4% overnight (previousClose
    // 189.50 -> today's own first bar 185.00) and then traded almost flat
    // for the rest of the session so far, so measuring "vs today's own
    // open" showed a barely-there ~-0.2% while Yahoo's own site correctly
    // showed -2.6% (vs previousClose) - completely missing the overnight
    // gap, not just a rounding difference. Falls back to today's own first
    // real open only if previousClose/chartPreviousClose are both missing
    // from the response (better than a zero/undefined reference).
    const double previousCloseValue = meta.value(QStringLiteral("previousClose")).toDouble(
        meta.value(QStringLiteral("chartPreviousClose")).toDouble(0.0));
    const float referencePrice = previousCloseValue > 0.0
                                     ? static_cast<float>(previousCloseValue)
                                     : aggregated.first().open;

    // The displayed "current" price prefers Yahoo's own live
    // regularMarketPrice over our last *completed* bucket's close - see
    // StockSnapshot::lastPrice's own comment for why those two aren't
    // required to match exactly. Falls back to the bucket close if that
    // field is ever missing (0 is not a legitimate quoted price).
    const double regularMarketPriceValue =
        meta.value(QStringLiteral("regularMarketPrice")).toDouble(0.0);
    const float lastPrice = regularMarketPriceValue > 0.0
                                ? static_cast<float>(regularMarketPriceValue)
                                : aggregated.last().close;

    // Today's official day high/low, preferring Yahoo's own authoritative
    // regularMarketDayHigh/Low - see StockSnapshot::dayHigh/dayLow's own
    // comment for why (our own bucket data can undershoot the true
    // extremes). Falls back to the highest/lowest bucket actually
    // aggregated if either field is missing.
    float bucketDayHigh = aggregated.first().high;
    float bucketDayLow = aggregated.first().low;
    for (const PriceBucket &bucket : aggregated) {
        bucketDayHigh = std::max(bucketDayHigh, bucket.high);
        bucketDayLow = std::min(bucketDayLow, bucket.low);
    }
    const double dayHighValue = meta.value(QStringLiteral("regularMarketDayHigh")).toDouble(0.0);
    const double dayLowValue = meta.value(QStringLiteral("regularMarketDayLow")).toDouble(0.0);

    // Expand into a dense, chronologically-indexed array spanning every
    // slot from the session's first bucket up to the last one with real
    // data - filling the gaps with hasData=false placeholders rather than
    // omitting them, so a slot's position in the array (and so its column
    // on screen) always matches its actual elapsed time, never "whichever
    // real data point happened to come next".
    QVector<PriceBucket> buckets;
    buckets.reserve(aggregated.lastKey() + 1);
    for (int index = 0; index <= aggregated.lastKey(); ++index) {
        const auto it = aggregated.constFind(index);
        if (it != aggregated.constEnd()) {
            buckets.append(it.value());
        } else {
            PriceBucket gap;
            gap.hasData = false;
            buckets.append(gap);
        }
    }

    // Keep only the most recent kBucketCount slots, matching the fixed
    // history width the renderer/mock provider both assume - this only
    // affects what's drawn, not referencePrice/lastPrice above.
    if (buckets.size() > pricedata::kBucketCount)
        buckets = buckets.mid(buckets.size() - pricedata::kBucketCount);

    StockSnapshot snapshot;
    snapshot.symbol = symbol;
    snapshot.referencePrice = referencePrice;
    snapshot.lastPrice = lastPrice;
    snapshot.dayHigh = dayHighValue > 0.0 ? static_cast<float>(dayHighValue) : bucketDayHigh;
    snapshot.dayLow = dayLowValue > 0.0 ? static_cast<float>(dayLowValue) : bucketDayLow;
    snapshot.buckets = buckets;
    snapshot.sessionAnchorEpoch = firstTimestamp;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    snapshot.marketOpen = sessionStart > 0 && now >= sessionStart && now <= sessionEnd;
    snapshot.currency = meta.value(QStringLiteral("currency")).toString();

    byKey_.insert(symbol.toConfigString(), snapshot);
    lastFetchFailed_.insert(symbol.toConfigString(), false);
}

const StockSnapshot *YahooDataProvider::snapshot(const Symbol &symbol) const
{
    const auto it = byKey_.constFind(symbol.toConfigString());
    return it == byKey_.constEnd() ? nullptr : &it.value();
}

bool YahooDataProvider::hasDataIssue() const
{
    for (auto it = lastFetchFailed_.constBegin(); it != lastFetchFailed_.constEnd(); ++it) {
        if (it.value())
            return true;
    }
    return false;
}

} // namespace hub75
