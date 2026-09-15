// SPDX-License-Identifier: GPL-2.0-only

#include "appconfig.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <QStringList>

namespace hub75 {
namespace {

// Reads an integer key, leaving *value untouched when the key is absent.
bool readInt(const QJsonObject &obj, const QString &key, const QString &context,
             int min, int max, int *value, QString *error)
{
    if (!obj.contains(key))
        return true;
    const QJsonValue v = obj.value(key);
    if (!v.isDouble()) {
        *error = QStringLiteral("%1.%2: expected a number").arg(context, key);
        return false;
    }
    const int i = v.toInt();
    if (i < min || i > max) {
        *error = QStringLiteral("%1.%2: %3 is out of range (%4..%5)")
                     .arg(context, key).arg(i).arg(min).arg(max);
        return false;
    }
    *value = i;
    return true;
}

bool readBool(const QJsonObject &obj, const QString &key, const QString &context,
              bool *value, QString *error)
{
    if (!obj.contains(key))
        return true;
    const QJsonValue v = obj.value(key);
    if (!v.isBool()) {
        *error = QStringLiteral("%1.%2: expected true or false").arg(context, key);
        return false;
    }
    *value = v.toBool();
    return true;
}

bool readString(const QJsonObject &obj, const QString &key, const QString &context,
                QString *value, QString *error)
{
    if (!obj.contains(key))
        return true;
    const QJsonValue v = obj.value(key);
    if (!v.isString()) {
        *error = QStringLiteral("%1.%2: expected a string").arg(context, key);
        return false;
    }
    *value = v.toString();
    return true;
}

bool parseMatrix(const QJsonObject &obj, MatrixConfig *cfg, QString *error)
{
    const QString ctx = QStringLiteral("matrix");

    if (obj.contains(QStringLiteral("chains"))) {
        const QJsonValue v = obj.value(QStringLiteral("chains"));
        if (!v.isArray()) {
            *error = QStringLiteral("matrix.chains: expected an array of panel counts, e.g. [3, 2, 1]");
            return false;
        }
        const QJsonArray arr = v.toArray();
        if (arr.isEmpty() || arr.size() > limits::kMaxChains) {
            *error = QStringLiteral("matrix.chains: %1 chains configured, must be 1..%2")
                         .arg(arr.size()).arg(limits::kMaxChains);
            return false;
        }
        QVector<int> chains;
        chains.reserve(arr.size());
        for (int i = 0; i < arr.size(); ++i) {
            if (!arr.at(i).isDouble()) {
                *error = QStringLiteral("matrix.chains[%1]: expected a number").arg(i);
                return false;
            }
            const int count = arr.at(i).toInt();
            if (count < 1 || count > limits::kMaxDisplaysPerChain) {
                *error = QStringLiteral("matrix.chains[%1]: %2 panels, must be 1..%3")
                             .arg(i).arg(count).arg(limits::kMaxDisplaysPerChain);
                return false;
            }
            chains.append(count);
        }
        cfg->chains = chains;
    }

    if (!readString(obj, QStringLiteral("hardwareMapping"), ctx, &cfg->hardwareMapping, error))
        return false;
    if (!readString(obj, QStringLiteral("ledRgbSequence"), ctx, &cfg->ledRgbSequence, error))
        return false;
    {
        const QString sequence = cfg->ledRgbSequence.toUpper();
        const bool wellFormed = sequence.size() == 3
                                && sequence.contains(QLatin1Char('R'))
                                && sequence.contains(QLatin1Char('G'))
                                && sequence.contains(QLatin1Char('B'));
        if (!wellFormed) {
            *error = QStringLiteral("matrix.ledRgbSequence: \"%1\" must be a 3-letter "
                                    "permutation of R, G and B (e.g. \"RBG\" if the panel's "
                                    "green and blue channels are swapped)")
                         .arg(cfg->ledRgbSequence);
            return false;
        }
        cfg->ledRgbSequence = sequence;
    }
    if (!readInt(obj, QStringLiteral("brightness"), ctx, 1, 100, &cfg->brightness, error))
        return false;
    if (!readBool(obj, QStringLiteral("autoDim"), ctx, &cfg->autoDim, error))
        return false;
    if (!readInt(obj, QStringLiteral("gpioSlowdown"), ctx, 0, 4, &cfg->gpioSlowdown, error))
        return false;
    if (!readInt(obj, QStringLiteral("pwmBits"), ctx, 1, 11, &cfg->pwmBits, error))
        return false;
    if (!readInt(obj, QStringLiteral("pwmLsbNanoseconds"), ctx, 50, 3000,
                 &cfg->pwmLsbNanoseconds, error))
        return false;
    if (!readInt(obj, QStringLiteral("limitRefreshHz"), ctx, 0, 1000,
                 &cfg->limitRefreshHz, error))
        return false;
    if (!readBool(obj, QStringLiteral("showRefreshRate"), ctx, &cfg->showRefreshRate, error))
        return false;

    return true;
}

bool parseGlobal(const QJsonObject &obj, GlobalConfig *cfg, QString *error)
{
    const QString ctx = QStringLiteral("global");

    if (!readInt(obj, QStringLiteral("updateIntervalSeconds"), ctx, 60, 900,
                 &cfg->updateIntervalSeconds, error))
        return false;
    if (!readInt(obj, QStringLiteral("rotationSeconds"), ctx, 1, 3600,
                 &cfg->rotationSeconds, error))
        return false;
    if (!readInt(obj, QStringLiteral("renderFps"), ctx, 1, 120, &cfg->renderFps, error))
        return false;
    if (!readString(obj, QStringLiteral("colorScheme"), ctx, &cfg->colorScheme, error))
        return false;
    if (!readBool(obj, QStringLiteral("showConnectivityIndicator"), ctx,
                 &cfg->showConnectivityIndicator, error))
        return false;

    QString text;
    if (!readString(obj, QStringLiteral("marketClosed"), ctx, &text, error))
        return false;
    if (!text.isEmpty() && !closedMarketStyleFromString(text, &cfg->closedMarketStyle)) {
        *error = QStringLiteral("global.marketClosed: \"%1\" is not one of grey, normal, blank")
                     .arg(text);
        return false;
    }

    return true;
}

bool parseDisplay(const QJsonObject &obj, int index, DisplayConfig *cfg, QString *error)
{
    const QString ctx = QStringLiteral("displays[%1]").arg(index);

    if (!obj.contains(QStringLiteral("row")) || !obj.contains(QStringLiteral("column"))) {
        *error = QStringLiteral("%1: both \"row\" and \"column\" are required").arg(ctx);
        return false;
    }
    if (!readInt(obj, QStringLiteral("row"), ctx, 1, limits::kMaxChains, &cfg->row, error))
        return false;
    if (!readInt(obj, QStringLiteral("column"), ctx, 1, limits::kMaxDisplaysPerChain,
                 &cfg->column, error))
        return false;

    QString modeText;
    if (!readString(obj, QStringLiteral("mode"), ctx, &modeText, error))
        return false;
    if (!modeText.isEmpty() && !displayModeFromString(modeText, &cfg->mode)) {
        *error = QStringLiteral("%1.mode: \"%2\" is not one of list, line, area, candles")
                     .arg(ctx, modeText);
        return false;
    }

    const QJsonValue symbolsValue = obj.value(QStringLiteral("symbols"));
    if (!symbolsValue.isArray()) {
        *error = QStringLiteral("%1.symbols: expected an array of 1..%2 symbols")
                     .arg(ctx).arg(limits::kMaxSymbolsPerDisplay);
        return false;
    }
    const QJsonArray arr = symbolsValue.toArray();
    if (arr.isEmpty() || arr.size() > limits::kMaxSymbolsPerDisplay) {
        *error = QStringLiteral("%1.symbols: %2 symbols, must be 1..%3")
                     .arg(ctx).arg(arr.size()).arg(limits::kMaxSymbolsPerDisplay);
        return false;
    }
    for (int i = 0; i < arr.size(); ++i) {
        if (!arr.at(i).isString()) {
            *error = QStringLiteral("%1.symbols[%2]: expected a string").arg(ctx).arg(i);
            return false;
        }
        Symbol symbol;
        QString parseError;
        if (!Symbol::parse(arr.at(i).toString(), &symbol, &parseError)) {
            *error = QStringLiteral("%1.symbols[%2]: %3").arg(ctx).arg(i).arg(parseError);
            return false;
        }
        if (!symbol.hasKnownExchange()) {
            *error = QStringLiteral("%1.symbols[%2]: unknown exchange \"%3\" "
                                    "(see the table in src/config/symbol.cpp)")
                         .arg(ctx).arg(i).arg(symbol.exchange());
            return false;
        }
        if (cfg->symbols.contains(symbol)) {
            *error = QStringLiteral("%1.symbols[%2]: \"%3\" is listed twice on this panel")
                         .arg(ctx).arg(i).arg(symbol.toConfigString());
            return false;
        }
        cfg->symbols.append(symbol);
    }

    return true;
}

} // namespace

int MatrixConfig::maxChainLength() const
{
    int longest = 0;
    for (int count : chains)
        longest = qMax(longest, count);
    return qMax(longest, 1);
}

int MatrixConfig::panelCount() const
{
    int total = 0;
    for (int count : chains)
        total += count;
    return total;
}

bool AppConfig::load(const QString &path, AppConfig *out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = QStringLiteral("cannot open \"%1\": %2").arg(path, file.errorString());
        return false;
    }
    return loadFromJsonBytes(file.readAll(), path, out, error);
}

// Split out of load() so the web config server can validate a posted,
// in-memory edit (see src/web/configserver.cpp) before ever writing it to
// disk, instead of writing-then-validating-then-maybe-reverting a file.
bool AppConfig::loadFromJsonBytes(const QByteArray &jsonBytes, const QString &sourceLabel,
                                  AppConfig *out, QString *error)
{
    QJsonParseError parseError {};
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &parseError);
    if (doc.isNull()) {
        *error = QStringLiteral("%1: JSON error at offset %2: %3")
                     .arg(sourceLabel).arg(parseError.offset).arg(parseError.errorString());
        return false;
    }
    if (!doc.isObject()) {
        *error = QStringLiteral("%1: top level must be a JSON object").arg(sourceLabel);
        return false;
    }

    const QJsonObject root = doc.object();
    AppConfig config;

    if (root.contains(QStringLiteral("matrix"))) {
        if (!root.value(QStringLiteral("matrix")).isObject()) {
            *error = QStringLiteral("matrix: expected an object");
            return false;
        }
        if (!parseMatrix(root.value(QStringLiteral("matrix")).toObject(),
                         &config.matrix_, error))
            return false;
    }

    if (root.contains(QStringLiteral("global"))) {
        if (!root.value(QStringLiteral("global")).isObject()) {
            *error = QStringLiteral("global: expected an object");
            return false;
        }
        if (!parseGlobal(root.value(QStringLiteral("global")).toObject(),
                         &config.global_, error))
            return false;
    }

    if (root.contains(QStringLiteral("displays"))) {
        const QJsonValue v = root.value(QStringLiteral("displays"));
        if (!v.isArray()) {
            *error = QStringLiteral("displays: expected an array");
            return false;
        }
        const QJsonArray arr = v.toArray();
        for (int i = 0; i < arr.size(); ++i) {
            if (!arr.at(i).isObject()) {
                *error = QStringLiteral("displays[%1]: expected an object").arg(i);
                return false;
            }
            DisplayConfig display;
            if (!parseDisplay(arr.at(i).toObject(), i, &display, error))
                return false;
            config.displays_.append(display);
        }
    }

    if (!config.validate(error))
        return false;

    *out = config;
    return true;
}

bool AppConfig::validate(QString *error) const
{
    if (matrix_.chains.isEmpty() || matrix_.chains.size() > limits::kMaxChains) {
        *error = QStringLiteral("matrix.chains: %1 chains, must be 1..%2")
                     .arg(matrix_.chains.size()).arg(limits::kMaxChains);
        return false;
    }
    for (int i = 0; i < matrix_.chains.size(); ++i) {
        const int count = matrix_.chains.at(i);
        if (count < 1 || count > limits::kMaxDisplaysPerChain) {
            *error = QStringLiteral("matrix.chains[%1]: %2 panels, must be 1..%3")
                         .arg(i).arg(count).arg(limits::kMaxDisplaysPerChain);
            return false;
        }
    }

    // Every configured panel must exist in the wiring, and be claimed once.
    QSet<int> occupied;
    for (const DisplayConfig &display : displays_) {
        if (display.row > matrix_.chainCount()) {
            *error = QStringLiteral("displays: row %1 is configured but matrix.chains only "
                                    "declares %2 chain(s)")
                         .arg(display.row).arg(matrix_.chainCount());
            return false;
        }
        const int available = matrix_.chains.at(display.row - 1);
        if (display.column > available) {
            *error = QStringLiteral("displays: row %1 column %2 is configured but chain %1 "
                                    "only has %3 panel(s)")
                         .arg(display.row).arg(display.column).arg(available);
            return false;
        }
        const int key = display.row * (limits::kMaxDisplaysPerChain + 1) + display.column;
        if (occupied.contains(key)) {
            *error = QStringLiteral("displays: row %1 column %2 is configured more than once")
                         .arg(display.row).arg(display.column);
            return false;
        }
        occupied.insert(key);
    }

    if (global_.updateIntervalSeconds < 60 || global_.updateIntervalSeconds > 900) {
        *error = QStringLiteral("global.updateIntervalSeconds: %1 is out of range (60..900)")
                     .arg(global_.updateIntervalSeconds);
        return false;
    }

    return true;
}

const DisplayConfig *AppConfig::displayAt(int row, int column) const
{
    for (const DisplayConfig &display : displays_) {
        if (display.row == row && display.column == column)
            return &display;
    }
    return nullptr;
}

bool AppConfig::panelExists(int row, int column) const
{
    if (row < 1 || row > matrix_.chainCount())
        return false;
    return column >= 1 && column <= matrix_.chains.at(row - 1);
}

QString AppConfig::describe() const
{
    QStringList lines;

    lines << QStringLiteral("Matrix");
    lines << QStringLiteral("  hardware mapping : %1").arg(matrix_.hardwareMapping);
    lines << QStringLiteral("  RGB sequence     : %1").arg(matrix_.ledRgbSequence);
    lines << QStringLiteral("  panel geometry   : %1x%2 per panel")
                 .arg(limits::kPanelWidth).arg(limits::kPanelHeight);
    QStringList chainDesc;
    for (int i = 0; i < matrix_.chains.size(); ++i)
        chainDesc << QStringLiteral("chain %1: %2").arg(i + 1).arg(matrix_.chains.at(i));
    lines << QStringLiteral("  chains           : %1 (%2 panels total)")
                 .arg(chainDesc.join(QStringLiteral(", "))).arg(matrix_.panelCount());
    lines << QStringLiteral("  library canvas   : %1x%2 (chain=%3, parallel=%4)")
                 .arg(matrix_.pixelWidth()).arg(matrix_.pixelHeight())
                 .arg(matrix_.maxChainLength()).arg(matrix_.chainCount());
    lines << QStringLiteral("  brightness       : %1%").arg(matrix_.brightness);
    lines << QStringLiteral("  auto dim         : %1")
                 .arg(matrix_.autoDim ? QStringLiteral("on (no ALS wired up yet)")
                                     : QStringLiteral("off"));
    lines << QStringLiteral("  gpio slowdown    : %1").arg(matrix_.gpioSlowdown);
    lines << QStringLiteral("  pwm              : %1 bits, %2 ns lsb")
                 .arg(matrix_.pwmBits).arg(matrix_.pwmLsbNanoseconds);
    lines << QStringLiteral("  refresh limit    : %1")
                 .arg(matrix_.limitRefreshHz > 0
                          ? QStringLiteral("%1 Hz").arg(matrix_.limitRefreshHz)
                          : QStringLiteral("unlimited"));

    lines << QString();
    lines << QStringLiteral("Global");
    lines << QStringLiteral("  data update      : every %1 s").arg(global_.updateIntervalSeconds);
    lines << QStringLiteral("  rotation         : every %1 s").arg(global_.rotationSeconds);
    lines << QStringLiteral("  render rate      : %1 fps").arg(global_.renderFps);
    lines << QStringLiteral("  market closed    : %1").arg(toString(global_.closedMarketStyle));
    lines << QStringLiteral("  color scheme     : %1").arg(global_.colorScheme);
    lines << QStringLiteral("  status indicator : %1")
                 .arg(global_.showConnectivityIndicator ? QStringLiteral("on") : QStringLiteral("off"));

    lines << QString();
    lines << QStringLiteral("Panels (row = chain, column = position in chain)");
    for (int row = 1; row <= matrix_.chainCount(); ++row) {
        for (int column = 1; column <= matrix_.chains.at(row - 1); ++column) {
            const DisplayConfig *display = displayAt(row, column);
            const QString position = QStringLiteral("  R%1C%2").arg(row).arg(column);
            if (!display) {
                lines << QStringLiteral("%1 : wired, no content assigned (stays dark)")
                             .arg(position);
                continue;
            }
            QStringList symbols;
            for (const Symbol &symbol : display->symbols)
                symbols << symbol.toConfigString();
            lines << QStringLiteral("%1 : %2 [%3]")
                         .arg(position, toString(display->mode),
                              symbols.join(QStringLiteral(", ")));
        }
    }

    return lines.join(QLatin1Char('\n'));
}

QString toString(DisplayMode mode)
{
    switch (mode) {
    case DisplayMode::StockList: return QStringLiteral("list");
    case DisplayMode::Line:      return QStringLiteral("line");
    case DisplayMode::Area:      return QStringLiteral("area");
    case DisplayMode::Candles:   return QStringLiteral("candles");
    }
    return QStringLiteral("area");
}

QString toString(ClosedMarketStyle style)
{
    switch (style) {
    case ClosedMarketStyle::Grey:   return QStringLiteral("grey");
    case ClosedMarketStyle::Normal: return QStringLiteral("normal");
    case ClosedMarketStyle::Blank:  return QStringLiteral("blank");
    }
    return QStringLiteral("grey");
}

bool displayModeFromString(const QString &text, DisplayMode *out)
{
    const QString key = text.trimmed().toLower();
    if (key == QLatin1String("list"))    { *out = DisplayMode::StockList; return true; }
    if (key == QLatin1String("line"))    { *out = DisplayMode::Line;      return true; }
    if (key == QLatin1String("area"))    { *out = DisplayMode::Area;      return true; }
    if (key == QLatin1String("candles") || key == QLatin1String("candlesticks")) {
        *out = DisplayMode::Candles;
        return true;
    }
    return false;
}

bool closedMarketStyleFromString(const QString &text, ClosedMarketStyle *out)
{
    const QString key = text.trimmed().toLower();
    if (key == QLatin1String("grey") || key == QLatin1String("gray")) {
        *out = ClosedMarketStyle::Grey;
        return true;
    }
    if (key == QLatin1String("normal")) { *out = ClosedMarketStyle::Normal; return true; }
    if (key == QLatin1String("blank") || key == QLatin1String("hide")) {
        *out = ClosedMarketStyle::Blank;
        return true;
    }
    return false;
}

} // namespace hub75
