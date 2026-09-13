#ifndef HUB75_STOCKRENDERER_H
#define HUB75_STOCKRENDERER_H

#include <QtGlobal>

#include "net/connectivitymonitor.h"

namespace rgb_matrix {
class Font;
}

namespace hub75 {

class AppConfig;
class DisplayConfig;
class FontStore;
class PanelView;
class StockDataProvider;
struct StockSnapshot;

// Draws one panel's worth of stock content - either the compact 4-symbol
// list, or a rotating full-panel chart (line/area/candles), per
// display->mode. "elapsedMs" is the render loop's running clock, used to
// pick which symbol is currently shown in a chart mode.
//
// Missing data (a symbol the provider hasn't seen yet) is drawn as a dim
// placeholder rather than skipped, so a panel never goes silently blank.
void renderStockPanel(PanelView *panel, const DisplayConfig &display, const AppConfig &config,
                      const StockDataProvider &data, FontStore &fonts, qint64 elapsedMs);

// Draws a 2x2px WiFi/internet status square in a fixed corner of "panel"
// (the caller picks which one - see main.cpp, which always uses row 1,
// column 1 so it's in one predictable place regardless of wiring). Meant to
// be called last, on top of whatever renderStockPanel() already drew there -
// every display mode uses its panel's full 64x32 area, so there is no corner
// that's guaranteed free in every mode.
void renderConnectivityIndicator(PanelView *panel, ConnectivityMonitor::State state);

} // namespace hub75

#endif // HUB75_STOCKRENDERER_H
