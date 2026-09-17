// SPDX-License-Identifier: GPL-2.0-only

#ifndef HUB75_APPCONFIG_H
#define HUB75_APPCONFIG_H

#include "symbol.h"

#include <QString>
#include <QVector>

namespace hub75 {

// Fixed properties of the panels this application is built for.
namespace limits {
constexpr int kPanelWidth = 64;            // LEDs per panel, horizontally
constexpr int kPanelHeight = 32;           // LEDs per panel, vertically
constexpr int kMaxChains = 3;              // parallel electrical chains
constexpr int kMaxDisplaysPerChain = 5;    // panels daisy-chained per chain
// Stocks assignable to one panel/display in total - not the same thing as
// how many rows fit on screen at once (see stockrenderer.cpp's
// kListVisibleRows). Chart mode rotates through all of them one at a time
// regardless; list mode shows kListVisibleRows at a time and scrolls
// through the rest on the same rotationSeconds cadence once there are more
// symbols than rows.
constexpr int kMaxSymbolsPerDisplay = 10;
} // namespace limits

// How a single panel presents its stocks - each display picks exactly one of
// these directly; there is no separate global "chart style" on top, so one
// wall can freely mix a list panel with line/area/candlestick panels.
enum class DisplayMode {
    StockList, // all stocks at once as a compact list, no chart
    Line,      // one stock at a time, full panel, cycling on a timer - line chart
    Area,      // ... line chart, with the area beneath filled
    Candles,   // ... candlestick chart
};

// What to show while the stock's exchange is closed.
enum class ClosedMarketStyle {
    Grey,   // last session's data, drawn in grey instead of green/red
    Normal, // last session's data in the usual colors
    Blank,  // nothing
};

// One 64x32 panel and its content.
struct DisplayConfig {
    int row = 1;                  // 1-based chain index
    int column = 1;               // 1-based position within that chain
    DisplayMode mode = DisplayMode::Area;
    QVector<Symbol> symbols;      // 1..kMaxSymbolsPerDisplay
};

// Panel wiring. Only the number of panels per chain matters here; where they
// physically hang on the wall is a purely human concern.
struct MatrixConfig {
    QString hardwareMapping = QStringLiteral("regular");
    // Some panels wire their HUB75 connector's colour channels to different
    // physical LEDs than the label on the pin suggests (e.g. "G" internally
    // driving the blue LEDs). A permutation of "RGB" corrects for this without
    // rewiring anything - see --led-rgb-sequence in the library docs.
    QString ledRgbSequence = QStringLiteral("RGB");
    QVector<int> chains { 1 };    // panels per chain, one entry per chain
    int brightness = 60;          // percent, 1..100
    // Placeholder for a future ambient-light-sensor integration: no sensor
    // is read yet and this has no behavioural effect on its own - it's just
    // a stored flag, ready for whenever that gets built.
    bool autoDim = false;
    int gpioSlowdown = 1;         // --led-slowdown-gpio; 1 suits a Zero 2 W
    int pwmBits = 11;             // 1..11
    int pwmLsbNanoseconds = 130;
    int limitRefreshHz = 100;     // <= 0 for unlimited
    bool showRefreshRate = false;

    int chainCount() const { return chains.size(); }
    int maxChainLength() const;
    // Size of the canvas the library hands us for the whole wall.
    int pixelWidth() const { return maxChainLength() * limits::kPanelWidth; }
    int pixelHeight() const { return chainCount() * limits::kPanelHeight; }
    // Panels physically present, i.e. the sum over all chains.
    int panelCount() const;
};

// Settings that apply to every panel.
struct GlobalConfig {
    // Data refresh, 60..900. Deliberately not allowed below 60s - Yahoo's
    // own bars are 1-minute granularity and the free feed already lags
    // ~15-20 min behind real time, so polling faster than that buys nothing
    // and only adds load; a real, if unconfirmed, suspicion (see
    // YahooDataProvider) is that sustained fast polling over many hours is
    // what triggered a real fetch stall on live hardware. 180 is a
    // deliberately more conservative default than the polling-every-minute
    // this used to do.
    int updateIntervalSeconds = 180;
    int rotationSeconds = 15;       // dwell time per stock, for any single-stock
                                    // (Line/Area/Candles) display mode
    int renderFps = 20;             // canvas redraw rate
    ClosedMarketStyle closedMarketStyle = ClosedMarketStyle::Grey;
    QString colorScheme = QStringLiteral("default"); // green/red/white
    // 2x2px overlay in the corner of the first panel (row 1, column 1)
    // showing WiFi/internet status - see ConnectivityMonitor. Drawn on top of
    // whatever that panel is already showing; there's no reliably unused
    // corner across every display mode, so this is a deliberate small
    // sacrifice rather than an attempt at pixel-perfect non-interference.
    bool showConnectivityIndicator = true;
};

// Parsed and validated application configuration.
class AppConfig
{
public:
    AppConfig() = default;

    // Reads and validates a JSON configuration file. On failure returns false
    // and fills *error with a message naming the offending key.
    static bool load(const QString &path, AppConfig *out, QString *error);

    // Same parsing/validation as load(), for JSON that's already in memory
    // (e.g. the web config server validating a posted edit before writing
    // anything to disk). "sourceLabel" is only used in error messages.
    static bool loadFromJsonBytes(const QByteArray &jsonBytes, const QString &sourceLabel,
                                  AppConfig *out, QString *error);

    // Validates an already-populated instance. Also called by load().
    bool validate(QString *error) const;

    const MatrixConfig &matrix() const { return matrix_; }
    const GlobalConfig &global() const { return global_; }
    const QVector<DisplayConfig> &displays() const { return displays_; }

    // 1-based lookup; nullptr if no panel is configured at that position.
    const DisplayConfig *displayAt(int row, int column) const;

    // True if the wiring declares a panel at this 1-based position, whether or
    // not content has been assigned to it.
    bool panelExists(int row, int column) const;

    // Human-readable dump of the resolved configuration, used by --dry-run.
    QString describe() const;

    MatrixConfig &matrixRef() { return matrix_; }
    GlobalConfig &globalRef() { return global_; }

private:
    MatrixConfig matrix_;
    GlobalConfig global_;
    QVector<DisplayConfig> displays_;
};

// Enum <-> string helpers, shared by the config parser and --help text.
QString toString(DisplayMode mode);
QString toString(ClosedMarketStyle style);
bool displayModeFromString(const QString &text, DisplayMode *out);
bool closedMarketStyleFromString(const QString &text, ClosedMarketStyle *out);

} // namespace hub75

#endif // HUB75_APPCONFIG_H
