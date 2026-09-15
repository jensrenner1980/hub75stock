// SPDX-License-Identifier: GPL-2.0-only

#include "symbol.h"

#include <QHash>
#include <QRegularExpression>

namespace hub75 {
namespace {

// Exchange code (as written in the config) -> Yahoo Finance suffix.
// An empty suffix means "no suffix", which is how Yahoo spells US listings.
// This table is deliberately small and easy to extend; unknown exchanges are
// reported by the config validation rather than silently guessed.
const QHash<QString, QString> &yahooSuffixes()
{
    static const QHash<QString, QString> table = {
        // United States
        { QStringLiteral("NASDAQ"), QString() },
        { QStringLiteral("NYSE"),   QString() },
        { QStringLiteral("AMEX"),   QString() },
        { QStringLiteral("US"),     QString() },
        // Europe
        { QStringLiteral("XETR"),   QStringLiteral(".DE") },
        { QStringLiteral("XETRA"),  QStringLiteral(".DE") },
        { QStringLiteral("FWB"),    QStringLiteral(".F")  },
        { QStringLiteral("FRA"),    QStringLiteral(".F")  },
        { QStringLiteral("LSE"),    QStringLiteral(".L")  },
        { QStringLiteral("EURONEXT"), QStringLiteral(".AS") },
        { QStringLiteral("AMS"),    QStringLiteral(".AS") },
        { QStringLiteral("PAR"),    QStringLiteral(".PA") },
        { QStringLiteral("MIL"),    QStringLiteral(".MI") },
        { QStringLiteral("BME"),    QStringLiteral(".MC") },
        { QStringLiteral("SIX"),    QStringLiteral(".SW") },
        { QStringLiteral("STO"),    QStringLiteral(".ST") },
        { QStringLiteral("HEL"),    QStringLiteral(".HE") },
        { QStringLiteral("CPH"),    QStringLiteral(".CO") },
        { QStringLiteral("OSL"),    QStringLiteral(".OL") },
        { QStringLiteral("WSE"),    QStringLiteral(".WA") },
        { QStringLiteral("VIE"),    QStringLiteral(".VI") },
        // Asia / Pacific
        { QStringLiteral("ASX"),    QStringLiteral(".AX") },
        { QStringLiteral("NZX"),    QStringLiteral(".NZ") },
        { QStringLiteral("TSE"),    QStringLiteral(".T")  },
        { QStringLiteral("TYO"),    QStringLiteral(".T")  },
        { QStringLiteral("HKEX"),   QStringLiteral(".HK") },
        { QStringLiteral("SGX"),    QStringLiteral(".SI") },
        { QStringLiteral("KRX"),    QStringLiteral(".KS") },
        { QStringLiteral("TWSE"),   QStringLiteral(".TW") },
        { QStringLiteral("NSE"),    QStringLiteral(".NS") },
        { QStringLiteral("BSE"),    QStringLiteral(".BO") },
        { QStringLiteral("SSE"),    QStringLiteral(".SS") },
        { QStringLiteral("SZSE"),   QStringLiteral(".SZ") },
        // Americas (non-US) / Africa
        { QStringLiteral("TSX"),    QStringLiteral(".TO") },
        { QStringLiteral("TSXV"),   QStringLiteral(".V")  },
        { QStringLiteral("BMV"),    QStringLiteral(".MX") },
        { QStringLiteral("B3"),     QStringLiteral(".SA") },
        { QStringLiteral("SAO"),    QStringLiteral(".SA") },
        { QStringLiteral("JSE"),    QStringLiteral(".JO") },
        { QStringLiteral("TLV"),    QStringLiteral(".TA") },
    };
    return table;
}

} // namespace

bool Symbol::parse(const QString &text, Symbol *out, QString *error)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (error) *error = QStringLiteral("symbol is empty");
        return false;
    }

    const QStringList parts = trimmed.split(QLatin1Char(':'));
    if (parts.size() > 3) {
        if (error) *error = QStringLiteral(
                        "\"%1\": too many ':'-separated parts (expected "
                        "TICKER, EXCHANGE:TICKER or EXCHANGE:TICKER:ALIAS)")
                                .arg(text);
        return false;
    }

    QString exchange;
    QString ticker = parts.first();
    QString alias;
    if (parts.size() >= 2) {
        exchange = parts.at(0).trimmed().toUpper();
        ticker = parts.at(1).trimmed();
        if (exchange.isEmpty()) {
            if (error) *error = QStringLiteral("\"%1\": exchange part before ':' is empty").arg(text);
            return false;
        }
    }
    if (parts.size() == 3) {
        alias = parts.at(2).trimmed().toUpper();
        if (alias.isEmpty()) {
            if (error) *error = QStringLiteral("\"%1\": alias part after the last ':' is empty")
                                    .arg(text);
            return false;
        }
    }

    // Tolerate the "$IONQ" spelling.
    if (ticker.startsWith(QLatin1Char('$')))
        ticker = ticker.mid(1);
    ticker = ticker.toUpper();

    if (ticker.isEmpty()) {
        if (error) *error = QStringLiteral("\"%1\": ticker part is empty").arg(text);
        return false;
    }

    // Tickers are letters/digits with the occasional '.' or '-' (BRK.B, RDS-A).
    static const QRegularExpression valid(QStringLiteral("^[A-Z0-9][A-Z0-9.\\-]*$"));
    if (!valid.match(ticker).hasMatch()) {
        if (error) *error = QStringLiteral("\"%1\": ticker \"%2\" has unexpected characters")
                                .arg(text, ticker);
        return false;
    }
    if (!alias.isEmpty() && !valid.match(alias).hasMatch()) {
        if (error) *error = QStringLiteral("\"%1\": alias \"%2\" has unexpected characters")
                                .arg(text, alias);
        return false;
    }

    out->exchange_ = exchange;
    out->ticker_ = ticker;
    out->alias_ = alias;
    return true;
}

QString Symbol::toConfigString() const
{
    if (!alias_.isEmpty())
        return QStringLiteral("%1:%2:%3").arg(exchange_, ticker_, alias_);
    return exchange_.isEmpty() ? ticker_
                               : QStringLiteral("%1:%2").arg(exchange_, ticker_);
}

bool Symbol::hasKnownExchange() const
{
    return exchange_.isEmpty() || yahooSuffixes().contains(exchange_);
}

QString Symbol::toYahooSymbol() const
{
    if (!isValid())
        return QString();
    if (exchange_.isEmpty())
        return ticker_;

    const auto it = yahooSuffixes().constFind(exchange_);
    if (it == yahooSuffixes().constEnd())
        return QString();
    return ticker_ + it.value();
}

} // namespace hub75
