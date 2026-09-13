#ifndef HUB75_SYMBOL_H
#define HUB75_SYMBOL_H

#include <QString>

namespace hub75 {

// A stock identified in the provider-agnostic "EXCHANGE:TICKER" notation used
// in the configuration file, e.g. "ASX:BRN", "XETR:SAP" or plain "IONQ" for
// the default (US) market. An optional third segment, "EXCHANGE:TICKER:ALIAS",
// gives a friendlier on-panel label than the ticker - some exchanges (see
// README "Live data") list a foreign stock under their own internal code
// rather than its home-market ticker, e.g. IonQ trades on Xetra as "0YB0",
// so "XETR:0YB0:IONQ" fetches the right instrument but still displays "IONQ".
//
// Keeping the config notation independent of the data provider means the
// config file survives a provider swap; each provider gets its own translation
// (see toYahooSymbol()).
class Symbol {
public:
    Symbol() = default;

    // Parses "EXCHANGE:TICKER:ALIAS", "EXCHANGE:TICKER" or plain "TICKER". A
    // leading '$' on the ticker is tolerated so that the "$IONQ" spelling
    // common in tickers/news can be pasted in directly. Returns false and
    // fills *error on malformed input.
    static bool parse(const QString &text, Symbol *out, QString *error);

    bool isValid() const { return !ticker_.isEmpty(); }

    // Empty for the default market (US).
    const QString &exchange() const { return exchange_; }
    const QString &ticker() const { return ticker_; }

    // Round-trips back to the configuration notation, alias included.
    QString toConfigString() const;

    // Yahoo Finance notation: "BRN.AX", "SAP.DE", "IONQ". Returns an empty
    // string if the exchange is not in the mapping table.
    QString toYahooSymbol() const;

    // Short label to render on a 64px wide panel - the alias if one was
    // given, otherwise the bare ticker.
    QString displayLabel() const { return alias_.isEmpty() ? ticker_ : alias_; }

    // True if exchange() is known to the exchange mapping table.
    bool hasKnownExchange() const;

    // Alias is deliberately not part of identity - two entries differing
    // only in display label are still "the same symbol" (e.g. for the list
    // in DisplayConfig::symbols rejecting duplicates).
    bool operator==(const Symbol &other) const {
        return exchange_ == other.exchange_ && ticker_ == other.ticker_;
    }

private:
    QString exchange_;
    QString ticker_;
    QString alias_;
};

} // namespace hub75

#endif // HUB75_SYMBOL_H
