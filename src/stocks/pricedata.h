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
// 60 buckets * 12 minutes = a 12 hour session, comfortably covering a
// regular trading day (including the longer European floor-trading
// sessions - see README "Live data"), one bucket per LED column (matches
// limits::kPanelWidth with a few columns of margin).
constexpr int kBucketCount = 60;
constexpr int kBucketMinutes = 12;
} // namespace pricedata

// Everything one panel needs to render a single stock: current price, the
// day's reference (opening) price for the overall trend colour, and the
// intraday chart history. Buckets are stored oldest-first; buckets_.last()
// is the currently-forming (still live) bucket.
struct StockSnapshot {
    Symbol symbol;
    float sessionOpenPrice = 0.0f;
    float lastPrice = 0.0f;
    bool marketOpen = true;
    QVector<PriceBucket> buckets; // 0..pricedata::kBucketCount, oldest first

    float dailyChange() const { return lastPrice - sessionOpenPrice; }
    float dailyChangePercent() const {
        return sessionOpenPrice != 0.0f ? (dailyChange() / sessionOpenPrice) * 100.0f : 0.0f;
    }
    bool isUp() const { return dailyChange() >= 0.0f; }
    bool isValid() const { return symbol.isValid() && !buckets.isEmpty(); }
};

} // namespace hub75

#endif // HUB75_PRICEDATA_H
