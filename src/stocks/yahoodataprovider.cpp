#include "yahoodataprovider.h"

#include <QDateTime>
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
    if (reply->error() != QNetworkReply::NoError)
        return;

    const QJsonObject chart = QJsonDocument::fromJson(reply->readAll())
                                   .object().value(QStringLiteral("chart")).toObject();
    if (!chart.value(QStringLiteral("error")).isNull())
        return;
    const QJsonArray results = chart.value(QStringLiteral("result")).toArray();
    if (results.isEmpty())
        return;

    const QJsonObject result = results.first().toObject();
    const QJsonObject meta = result.value(QStringLiteral("meta")).toObject();
    const QJsonArray timestamps = result.value(QStringLiteral("timestamp")).toArray();
    const QJsonArray quoteArr = result.value(QStringLiteral("indicators")).toObject()
                                      .value(QStringLiteral("quote")).toArray();
    if (timestamps.isEmpty() || quoteArr.isEmpty())
        return;

    const QJsonObject quote = quoteArr.first().toObject();
    const QJsonArray opens = quote.value(QStringLiteral("open")).toArray();
    const QJsonArray highs = quote.value(QStringLiteral("high")).toArray();
    const QJsonArray lows = quote.value(QStringLiteral("low")).toArray();
    const QJsonArray closes = quote.value(QStringLiteral("close")).toArray();

    const QJsonObject regularPeriod = meta.value(QStringLiteral("currentTradingPeriod")).toObject()
                                           .value(QStringLiteral("regular")).toObject();
    const qint64 sessionStart = regularPeriod.value(QStringLiteral("start")).toInteger();
    const qint64 sessionEnd = regularPeriod.value(QStringLiteral("end")).toInteger();
    const qint64 bucketSeconds = qint64(pricedata::kBucketMinutes) * 60;

    // Aggregate 1-minute bars into kBucketMinutes-wide buckets, keyed by
    // elapsed time since the first returned bar - deliberately not
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

    if (aggregated.isEmpty())
        return; // nothing usable in this response - keep the old snapshot

    // The true session open/last-traded price, from the real data only -
    // unaffected by the gap-filling or the truncation below, so a sparse or
    // long session still gets a correct "daily change"/up-down colour
    // rather than one computed relative to a placeholder or to wherever the
    // visible window happens to start.
    const float sessionOpenPrice = aggregated.first().open;
    const float lastPrice = aggregated.last().close;

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
    // affects what's drawn, not sessionOpenPrice/lastPrice above.
    if (buckets.size() > pricedata::kBucketCount)
        buckets = buckets.mid(buckets.size() - pricedata::kBucketCount);

    StockSnapshot snapshot;
    snapshot.symbol = symbol;
    snapshot.sessionOpenPrice = sessionOpenPrice;
    snapshot.lastPrice = lastPrice;
    snapshot.buckets = buckets;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    snapshot.marketOpen = sessionStart > 0 && now >= sessionStart && now <= sessionEnd;

    byKey_.insert(symbol.toConfigString(), snapshot);
}

const StockSnapshot *YahooDataProvider::snapshot(const Symbol &symbol) const
{
    const auto it = byKey_.constFind(symbol.toConfigString());
    return it == byKey_.constEnd() ? nullptr : &it.value();
}

} // namespace hub75
