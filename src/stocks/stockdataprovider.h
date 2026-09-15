// SPDX-License-Identifier: GPL-2.0-only

#ifndef HUB75_STOCKDATAPROVIDER_H
#define HUB75_STOCKDATAPROVIDER_H

#include "pricedata.h"

#include <QVector>

namespace hub75 {

class Symbol;

// Common interface for anything that can supply StockSnapshot data to the
// renderer. MockDataProvider (synthetic, for development without a network)
// and YahooDataProvider (the real fetcher) both implement this, so main.cpp
// and stockrenderer.cpp don't need to know or care which one is in use.
class StockDataProvider
{
public:
    virtual ~StockDataProvider() = default;

    // Registers symbols this provider should track. Safe to call repeatedly
    // (e.g. on every config reload) with symbols already known.
    virtual void ensureSymbols(const QVector<Symbol> &symbols) = 0;

    // Advances/refreshes data. Call roughly on updateIntervalSeconds.
    virtual void update() = 0;

    // nullptr if the symbol hasn't produced any data yet.
    virtual const StockSnapshot *snapshot(const Symbol &symbol) const = 0;

    // True if the most recent fetch attempt for any tracked symbol failed
    // (network error, or Yahoo itself returning an API-level error) - drives
    // the on-screen connectivity indicator's fourth state, distinguishing
    // "network is fine but the data source is failing" from an ordinary
    // quiet chart (market closed, or between minute bars). Defaults to false
    // so MockDataProvider - which never fails - doesn't need to override
    // this at all.
    virtual bool hasDataIssue() const { return false; }
};

} // namespace hub75

#endif // HUB75_STOCKDATAPROVIDER_H
