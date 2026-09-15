// SPDX-License-Identifier: GPL-2.0-only

#ifndef HUB75_PRICEDATA_H
#define HUB75_PRICEDATA_H

#include "config/symbol.h"

#include <QString>
#include <QVector>

namespace hub75 {

// One bucket of the intraday chart: a 10 (or 12) minute slice of trading,
// one LED column wide. open/close give the bright body of a candle,
// low/high the dim wick - see StockRenderer.
//
// hasData is false for a time slice with no trades at all - a real
// possibility for a thinly-traded listing, not just a hypothetical. Kept in
// the buckets array (rather than omitted) so the array index still lines up
// with elapsed session time; the renderer skips drawing anything for these
// slots and breaks line/area continuity across them, rather than silently
// bridging two real data points that may be hours apart as if they were
// adjacent.
struct PriceBucket {
    float open = 0.0f;
    float close = 0.0f;
    float low = 0.0f;
    float high = 0.0f;
    bool hasData = true;

    bool isUp() const { return close >= open; }
};

namespace pricedata {
// One bucket per LED column (matches limits::kPanelWidth with a few
// columns of margin).
constexpr int kBucketCount = 60;
// MockDataProvider's fixed synthetic-session bucket width (60 * 12 min = a
// 12h session). YahooDataProvider does *not* use this - it sizes its
// buckets dynamically to whatever the real session's actual length is (see
// its own comments), so every real session fills the full kBucketCount
// columns regardless of how many hours that particular exchange trades -
// a fixed width here would leave a 6.5h NYSE session filling barely half
// the chart next to an 8.5h Xetra session filling all of it.
constexpr int kBucketMinutes = 12;
} // namespace pricedata

// Everything one panel needs to render a single stock: current price, the
// reference price the day's change is measured against, and the intraday
// chart history. Buckets are stored oldest-first; buckets_.last() is the
// currently-forming (still live) bucket.
struct StockSnapshot {
    Symbol symbol;
    // The previous trading day's close, for YahooDataProvider - the same
    // convention every ticker/chart site measures "daily change" against,
    // not today's own opening print (those two can disagree substantially
    // on a day the stock gaps at the open). MockDataProvider has no
    // previous-day data to speak of, so uses its synthetic session's own
    // starting price instead - conceptually the same role, just without a
    // real "yesterday" behind it.
    float referencePrice = 0.0f;
    // The currently-displayed price - Yahoo's own meta.regularMarketPrice
    // for YahooDataProvider, a genuinely live tick-level figure rather than
    // this session's last *completed* bucket's close (which can lag it by
    // up to a whole bucket's width - 8.5 min for Xetra - since a bucket
    // only closes once its time slice has fully elapsed). Falls back to the
    // last bucket's close if that field is ever missing. Deliberately not
    // required to exactly match the chart's own rightmost point for this
    // reason - the header shows the most current number available, the
    // chart shows the completed history; a small, momentary difference
    // between the two right at the chart's leading edge is expected, not a
    // bug.
    float lastPrice = 0.0f;
    bool marketOpen = true;
    // Epoch seconds of the displayed session's own first bar (for
    // YahooDataProvider, its bucket-anchoring "firstTimestamp" - see its own
    // comments on why that, not the response metadata's own session start,
    // is the reliable anchor). Lets the renderer tell "after hours, still
    // today's session" apart from "market's closed and this is a stale,
    // previous day's frozen session" (e.g. Friday's close still showing
    // Monday morning before the next open) - two states that otherwise both
    // just look like "!marketOpen" and would draw identically. 0 for
    // MockDataProvider's synthetic sessions, which are always "today" by
    // construction - see StockSnapshot::isValid() callers for how 0 is
    // treated (never stale).
    qint64 sessionAnchorEpoch = 0;
    // ISO 4217 code ("USD", "EUR", ...) from Yahoo's own meta.currency -
    // the actual reported trading currency, not inferred from the
    // exchange (some exchanges list foreign stocks that don't trade in
    // the exchange's own "home" currency). Empty for MockDataProvider's
    // synthetic sessions if never set; the renderer falls back to a
    // generic currency sign for anything empty/unrecognised.
    QString currency;
    // Today's official high/low from Yahoo's own meta.regularMarketDayHigh/
    // Low - the exchange's authoritative day range, not derived from our
    // own bucket array. Used only to *widen* the chart's auto-scaled
    // vertical range (see drawChart() in stockrenderer.cpp), never to
    // shrink it: our own bucket highs/lows can undershoot the true day
    // extremes early in a session, or during Yahoo's own free-feed
    // reporting lag, if the tick that set the real extreme hasn't made it
    // into a completed bucket yet. 0 (unset) for MockDataProvider, which
    // computes its own from its already-generated buckets instead - see
    // MockDataProvider::generateSession().
    float dayHigh = 0.0f;
    float dayLow = 0.0f;
    QVector<PriceBucket> buckets; // 0..pricedata::kBucketCount, oldest first

    float dailyChange() const { return lastPrice - referencePrice; }
    float dailyChangePercent() const {
        return referencePrice != 0.0f ? (dailyChange() / referencePrice) * 100.0f : 0.0f;
    }
    bool isUp() const { return dailyChange() >= 0.0f; }
    bool isValid() const { return symbol.isValid() && !buckets.isEmpty(); }
};

} // namespace hub75

#endif // HUB75_PRICEDATA_H
