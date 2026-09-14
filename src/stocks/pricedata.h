#ifndef HUB75_PRICEDATA_H
#define HUB75_PRICEDATA_H

#include "config/symbol.h"

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
    float lastPrice = 0.0f;
    bool marketOpen = true;
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
