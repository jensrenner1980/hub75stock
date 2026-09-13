#ifndef HUB75_MOCKPROVIDER_H
#define HUB75_MOCKPROVIDER_H

#include "pricedata.h"
#include "stockdataprovider.h"

#include <QElapsedTimer>
#include <QHash>
#include <QRandomGenerator>
#include <QString>

namespace hub75 {

// Synthetic stock data, for exercising the rendering pipeline before the
// real (Yahoo Finance) fetcher exists. One thing a live provider would never
// do, deliberately: the full 60-bucket history is generated immediately at
// construction (a real provider only has that much history after a real
// trading session has actually elapsed).
//
// The live (last) bucket is nudged on every update() call, and rolled over to
// a fresh bucket every pricedata::kBucketMinutes of real wall-clock time -
// otherwise a long-running process keeps extending the same bucket's
// high/low forever, which after running overnight produces one absurdly
// tall candle holding the entire session's range (seen for real: see
// PXL_20260913_131*.jpg in the project root).
class MockDataProvider : public StockDataProvider
{
public:
    // Seeds a plausible session (a random walk of kBucketCount buckets) for
    // every symbol, the first time that symbol is seen. Safe to call
    // repeatedly with the same symbols (e.g. every config reload).
    void ensureSymbols(const QVector<Symbol> &symbols) override;

    // Nudges every known symbol's live bucket by a small random step,
    // simulating price ticks arriving, and rolls buckets over once enough
    // wall-clock time has passed. Call roughly on updateIntervalSeconds.
    void update() override;

    // nullptr if the symbol hasn't been seen via ensureSymbols() yet.
    const StockSnapshot *snapshot(const Symbol &symbol) const override;

private:
    StockSnapshot generateSession(const Symbol &symbol);
    void nudge(StockSnapshot *snapshot);
    void rollOver(StockSnapshot *snapshot);

    QHash<QString, StockSnapshot> byKey_; // keyed by Symbol::toConfigString()
    QRandomGenerator rng_{QRandomGenerator::securelySeeded()};
    QElapsedTimer clock_;
    qint64 lastRolloverMs_ = 0;
};

} // namespace hub75

#endif // HUB75_MOCKPROVIDER_H
