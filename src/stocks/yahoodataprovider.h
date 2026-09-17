// SPDX-License-Identifier: GPL-2.0-only

#ifndef HUB75_YAHOODATAPROVIDER_H
#define HUB75_YAHOODATAPROVIDER_H

#include "config/symbol.h"
#include "stockdataprovider.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVector>

class QNetworkReply;

namespace hub75 {

// Fetches real intraday OHLC data from Yahoo Finance's chart endpoint -
// there is no official public Yahoo Finance API; this is the same
// unofficial/reverse-engineered one many hobby projects use. No API key,
// but also no guarantee it keeps working - Yahoo could change the response
// shape or start blocking unrecognised clients without notice.
//
// Each update() re-fetches the *entire* current session's 1-minute bars per
// symbol and re-aggregates them into pricedata::kBucketMinutes buckets from
// scratch, rather than incrementally patching the previous snapshot. That's
// simpler than incremental patching, and it means a lost connection (or one
// transient fetch failure) self-heals on the very next successful poll -
// there's no accumulated local state that can drift from reality.
class YahooDataProvider : public QObject, public StockDataProvider
{
    Q_OBJECT
public:
    explicit YahooDataProvider(QObject *parent = nullptr);

    void ensureSymbols(const QVector<Symbol> &symbols) override;
    void update() override;
    const StockSnapshot *snapshot(const Symbol &symbol) const override;
    DataHealth dataHealth(const Symbol &symbol) const override;

private:
    void fetchNext();
    void handleReply(const Symbol &symbol, QNetworkReply *reply);

    QNetworkAccessManager network_;
    QVector<Symbol> symbols_;
    QVector<Symbol> pending_; // queued for this update() round, not yet fetched
    QHash<QString, StockSnapshot> byKey_; // keyed by Symbol::toConfigString()
    // Outcome of the most recent fetch attempt per symbol - only ever set by
    // handleReply(), never cleared elsewhere, so a symbol with no entry yet
    // (nothing has completed since startup) correctly reads as "no known
    // issue" rather than a false-positive error at boot.
    QHash<QString, DataHealth> lastFetchHealth_;
    int inFlight_ = 0;
};

} // namespace hub75

#endif // HUB75_YAHOODATAPROVIDER_H
