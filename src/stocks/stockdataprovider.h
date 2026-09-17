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

    // Outcome of the most recent fetch attempt for this specific symbol -
    // drives a per-symbol tint on that symbol's own price/change (see
    // stockrenderer.cpp's applyDataHealth()), not a wall-wide aggregate:
    // an earlier version of this rolled every symbol's health into one
    // global indicator, but that meant the single indicator stayed stuck
    // on the worst symbol's state even once other symbols had already
    // recovered - confirmed confusing for real on live hardware. Two
    // genuinely different situations, both confirmed for real:
    //   - Error: the fetch itself failed (a network error, or Yahoo
    //     returning an API-level error object) - something is actually
    //     wrong.
    //   - NoData: the fetch succeeded (200 OK, no error object) but came
    //     back with no usable bars - confirmed for real right at a Xetra
    //     session's 09:00 open, before Yahoo's own backend has published
    //     the new session's first minute bar yet. Not an error - the data
    //     just doesn't exist upstream yet - so it shouldn't read as one.
    // Defaults to Ok so MockDataProvider - which never fails - doesn't need
    // to override this at all.
    enum class DataHealth { Ok, NoData, Error };
    virtual DataHealth dataHealth(const Symbol &symbol) const
    {
        Q_UNUSED(symbol);
        return DataHealth::Ok;
    }
};

} // namespace hub75

#endif // HUB75_STOCKDATAPROVIDER_H
