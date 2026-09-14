#include "mockprovider.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace hub75 {
namespace {

// A stable-per-symbol starting price in a plausible range ($10-$500), so the
// same ticker looks roughly the same across runs during development, without
// needing any real data.
float startingPrice(const Symbol &symbol)
{
    const uint hash = qHash(symbol.toConfigString());
    return 10.0f + static_cast<float>(hash % 49000) / 100.0f;
}

// A small, roughly bell-shaped random step (sum of three uniforms, centred on
// zero), as a fraction of price - e.g. 0.006 for a typical per-bucket move.
float randomStepFraction(QRandomGenerator &rng, float scale)
{
    const float u = (rng.generateDouble() + rng.generateDouble() + rng.generateDouble()) / 3.0f;
    return (u - 0.5f) * 2.0f * scale;
}

} // namespace

void MockDataProvider::ensureSymbols(const QVector<Symbol> &symbols)
{
    for (const Symbol &symbol : symbols) {
        const QString key = symbol.toConfigString();
        if (!byKey_.contains(key))
            byKey_.insert(key, generateSession(symbol));
    }
}

StockSnapshot MockDataProvider::generateSession(const Symbol &symbol)
{
    StockSnapshot snapshot;
    snapshot.symbol = symbol;
    snapshot.marketOpen = true;
    snapshot.buckets.reserve(pricedata::kBucketCount);

    float price = startingPrice(symbol);
    snapshot.referencePrice = price;

    for (int i = 0; i < pricedata::kBucketCount; ++i) {
        PriceBucket bucket;
        bucket.open = price;
        // Per-bucket move a bit larger than the live nudge, so 60 buckets add
        // up to a visually interesting session rather than a flat line.
        price = std::max(0.01f, price * (1.0f + randomStepFraction(rng_, 0.012f)));
        bucket.close = price;

        const float wickUp = std::abs(randomStepFraction(rng_, 0.006f));
        const float wickDown = std::abs(randomStepFraction(rng_, 0.006f));
        bucket.high = std::max(bucket.open, bucket.close) * (1.0f + wickUp);
        bucket.low = std::max(0.01f, std::min(bucket.open, bucket.close) * (1.0f - wickDown));

        snapshot.buckets.append(bucket);
    }

    snapshot.lastPrice = price;
    return snapshot;
}

void MockDataProvider::nudge(StockSnapshot *snapshot)
{
    if (snapshot->buckets.isEmpty())
        return;
    PriceBucket &live = snapshot->buckets.last();
    const float moved = std::max(0.01f, live.close * (1.0f + randomStepFraction(rng_, 0.004f)));
    live.close = moved;
    live.high = std::max(live.high, moved);
    live.low = std::min(live.low, moved);
    snapshot->lastPrice = moved;
}

// Retires the live bucket - its close becomes the new bucket's open - and
// appends a fresh one, keeping exactly kBucketCount buckets (drops the
// oldest). Without this, nudge() would extend the same bucket's high/low
// forever.
void MockDataProvider::rollOver(StockSnapshot *snapshot)
{
    if (snapshot->buckets.isEmpty())
        return;
    const float openPrice = snapshot->buckets.last().close;
    snapshot->buckets.removeFirst();
    PriceBucket fresh;
    fresh.open = fresh.close = fresh.high = fresh.low = openPrice;
    snapshot->buckets.append(fresh);
}

void MockDataProvider::update()
{
    if (!clock_.isValid())
        clock_.start();

    const qint64 bucketMs = qint64(pricedata::kBucketMinutes) * 60 * 1000;
    // A loop, not "if": guards against a long pause (e.g. the process was
    // suspended) needing more than one rollover to catch up, rather than
    // silently collapsing several buckets' worth of elapsed time into one.
    while (clock_.elapsed() - lastRolloverMs_ >= bucketMs) {
        lastRolloverMs_ += bucketMs;
        for (StockSnapshot &snapshot : byKey_)
            rollOver(&snapshot);
    }

    for (StockSnapshot &snapshot : byKey_)
        nudge(&snapshot);
}

const StockSnapshot *MockDataProvider::snapshot(const Symbol &symbol) const
{
    const auto it = byKey_.constFind(symbol.toConfigString());
    return it == byKey_.constEnd() ? nullptr : &it.value();
}

} // namespace hub75
