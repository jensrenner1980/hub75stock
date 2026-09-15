// SPDX-License-Identifier: GPL-2.0-only

#include "stockrenderer.h"

#include "config/appconfig.h"
#include "fontstore.h"
#include "panelview.h"
#include "stocks/stockdataprovider.h"
#include "stocks/pricedata.h"
#include "textutil.h"

#include "graphics.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <algorithm>
#include <cmath>

namespace hub75 {
namespace {

using rgb_matrix::Color;
using textutil::drawRightAlignedFixedWidth;

// Not constexpr: rgb_matrix::Color isn't a literal type.
const Color kGreen(0, 255, 96);
const Color kRed(255, 64, 64);
const Color kWhite(255, 255, 255);
const Color kGrey(140, 140, 140);

// Deliberately a different palette from the stock colours above (no green/
// red/white reuse) so the connectivity indicator reads as "system status",
// never mistakeable for a price move at a glance.
const Color kIndicatorNone(255, 0, 128);   // magenta - no WiFi link at all
const Color kIndicatorWifiOnly(255, 140, 0); // amber - WiFi up, no internet
const Color kIndicatorOnline(0, 140, 255);  // blue - online
const Color kIndicatorDataIssue(255, 0, 0); // red - online, but data fetch is failing

Color dim(const Color &c, int divisor = 4)
{
    return Color(c.r / divisor, c.g / divisor, c.b / divisor);
}

// Overall day's-change colour: green/red, or white for (near enough) flat.
// A dedicated threshold rather than comparing to exactly 0 - the mock
// provider's random walk essentially never lands on exactly zero, but real
// data might sit within a cent of the open without being meaningfully "up".
// Deliberately narrow (0.01%, not the original 0.05%): the displayed change
// figure is an absolute currency amount, not a percentage, and for a
// higher-priced stock even a single-tick move can be a meaningfully "real"
// change while still being a tiny percentage - confirmed for real with
// NVD.DE (~185 EUR): a 0.10 EUR tick is 0.054%, which sat inside the wider
// 0.05% band almost every time, so the chart stayed white on essentially
// every real tick rather than just on genuinely-flat ones.
Color trendColor(const StockSnapshot &snapshot)
{
    const float changePercent = snapshot.dailyChangePercent();
    if (std::abs(changePercent) < 0.01f)
        return kWhite;
    return changePercent > 0.0f ? kGreen : kRed;
}

// Per-point colour for Line/Area mode: green above the session's reference
// price, red below - the same convention real intraday stock charts use
// (Yahoo Finance, Robinhood, Google Finance), rather than trendColor()'s one
// verdict for the whole session based only on where it ended up. That
// matters for real: a stock that dipped below its reference mid-session and
// recovered used to render as one solid colour for the entire chart,
// hiding the dip entirely - this shows it. No "near enough to flat" white
// band here (unlike trendColor()) - white is reserved for "no reference
// price known at all" (referencePrice <= 0, meaning even Yahoo's own
// fallback-to-today's-open never resolved), which should be rare in
// practice.
//
// Deliberately not shared with Candles mode: a candle's colour already
// means something different and, for that chart type, more standard - its
// own open vs. its own close within that time slice, not vs. the session's
// reference price. Real candlestick charts everywhere use that convention;
// keeping it means Candles mode isn't touched by this at all.
Color baselinePointColor(float price, float referencePrice)
{
    if (referencePrice <= 0.0f)
        return kWhite;
    return price >= referencePrice ? kGreen : kRed;
}

// True if the displayed session's own first bar (StockSnapshot::
// sessionAnchorEpoch) isn't from today (the viewer's local calendar day) -
// i.e. this isn't just "after hours", it's a stale, previous day's frozen
// session (e.g. Friday's close still showing Monday morning before the next
// open - confirmed for real, see YahooDataProvider's own comments on why
// the response metadata's session start/end can't be used for this: before
// today's own session has started, that metadata already describes today's
// *upcoming* session even while the actual bars shown are still yesterday's
// - so "now < metadata's own session start" alone can't tell "after hours,
// same day" and "before open, stale from yesterday" apart; the anchor's own
// calendar date can). 0 (MockDataProvider's synthetic sessions, which are
// always "today" by construction) is never stale.
bool isStaleSession(qint64 sessionAnchorEpoch)
{
    if (sessionAnchorEpoch <= 0)
        return false;
    return QDateTime::fromSecsSinceEpoch(sessionAnchorEpoch).date() != QDate::currentDate();
}

// Applies the configured closed-market styling on top of an otherwise-normal
// colour choice, for everything *except* the last-price figure (see
// applyPriceMarketState() below for that one) - the chart body and the
// day's % change stay at full, undimmed colour the whole time the session
// being shown is merely "after hours", not actually stale. They're still
// exactly correct regardless of whether the market happens to be open right
// now: the chart is a complete record of the whole day so far, and the
// day's change is the real, final number once the session's over - neither
// one is any less true for the market being closed, so neither dims for
// that reason. Grey still applies once the session itself is stale (see
// isStaleSession()) - a previous day's frozen close is a fundamentally
// different situation, not just "closed for now". Normal always passes
// normal through unchanged regardless of any of this; Blank is handled by
// the caller (it skips drawing instead).
Color applyMarketState(const Color &normal, bool marketOpen, bool staleSession,
                       ClosedMarketStyle style)
{
    if (marketOpen || style != ClosedMarketStyle::Grey || !staleSession)
        return normal;
    return kGrey;
}

// Applies the configured closed-market styling to the last-price figure
// specifically - the one element that dims for "after hours, same day" (see
// applyMarketState() above for why everything else doesn't): unlike the
// chart or the day's change, the last price genuinely stops being live the
// moment the market closes and won't move again until the next session, so
// it's the one number actually made less true by the market being shut.
// Dimming just this, rather than the whole panel, keeps the display at
// whatever brightness was configured for trading hours almost all the time,
// with a small, specific cue instead of a broad visual shift twice a day.
// Grey once the session itself is stale, same as everywhere else.
Color applyPriceMarketState(const Color &normal, bool marketOpen, bool staleSession,
                            ClosedMarketStyle style)
{
    if (marketOpen || style == ClosedMarketStyle::Normal)
        return normal;
    if (!staleSession)
        return dim(normal, 2);
    if (style == ClosedMarketStyle::Grey)
        return kGrey;
    return normal; // Blank is handled before we get here
}

QString formatTicker(const Symbol &symbol)
{
    return symbol.displayLabel();
}

// "241.50" - 2 decimals, sign-free; the field width the caller right-aligns
// into absorbs shorter values, e.g. "10.00" for penny-range symbols. Very
// large prices (>= 1000) will overflow a 6-character field - a known
// limitation, not expected for typical equity prices.
QString formatPrice(float price)
{
    return QString::number(price, 'f', 2);
}

// "+3.2" / "-12.5" - signed percentage, 1 decimal, no "%" (added by the
// caller if there's room - see drawHeader vs renderList). The sign is
// always shown, matching the layout budget worked out for the list view
// (ticker + price + change == 16 characters with no separators to spare).
//
// A percentage, not an absolute price difference - confirmed for real that
// the two can tell noticeably different stories: for a higher-priced
// stock, an absolute currency change stays visibly nonzero on essentially
// every tick regardless of how small the actual move is, while for a
// cheap stock even a meaningful percentage move can round to "+0.0" in
// absolute terms. Percentage is what the colour logic already uses
// (trendColor()) and what every ticker/chart site displays, so this also
// fixes the two disagreeing with each other.
QString formatChangePercent(float changePercent)
{
    return (changePercent >= 0.0f ? QStringLiteral("+") : QStringLiteral("-"))
           + QString::number(std::abs(changePercent), 'f', 1);
}

// ---------------------------------------------------------------------------
// List mode: up to 4 lines, 4x6 font, matching the list16 bring-up pattern's
// layout exactly (see testpatterns.cpp / README "Compact list mode").
// ---------------------------------------------------------------------------
void renderList(PanelView *panel, const DisplayConfig &display, const GlobalConfig &global,
                const StockDataProvider &data, const rgb_matrix::Font &font)
{
    for (int line = 0; line < display.symbols.size() && line < limits::kMaxSymbolsPerDisplay;
         ++line) {
        const Symbol &symbol = display.symbols.at(line);
        const int baseline = line * 8 + font.baseline() + 1;
        const QString ticker = formatTicker(symbol);

        const StockSnapshot *snapshot = data.snapshot(symbol);
        if (!snapshot || !snapshot->isValid()) {
            // No data yet (e.g. just added to the config) - show the ticker,
            // dashes for the rest, rather than nothing at all.
            const QString text = QStringLiteral("%1 ---.-- ----").arg(ticker, -4);
            rgb_matrix::DrawText(panel, font, 0, baseline, kGrey, nullptr,
                                 text.toUtf8().constData(), 0);
            continue;
        }

        if (!snapshot->marketOpen && global.closedMarketStyle == ClosedMarketStyle::Blank)
            continue;

        const bool stale = isStaleSession(snapshot->sessionAnchorEpoch);
        const Color priceColor = applyPriceMarketState(kWhite, snapshot->marketOpen, stale,
                                                       global.closedMarketStyle);
        const Color changeColor = applyMarketState(trendColor(*snapshot), snapshot->marketOpen,
                                                  stale, global.closedMarketStyle);

        // Three separate draws, not one coloured string - the ticker stays
        // plain white regardless of market state (same as chart mode's
        // header always has), only the price dims for "after hours, same
        // day" (see applyPriceMarketState()), and the change keeps its real
        // trend colour throughout, dropping to grey only once the session
        // itself is stale. Each segment starts where the previous one's
        // DrawText() reports it actually ended, rather than hand-computing
        // pixel offsets from the fixed-width font's glyph size.
        int x = rgb_matrix::DrawText(panel, font, 0, baseline, kWhite, nullptr,
                                     QStringLiteral("%1 ").arg(ticker, -4).toUtf8().constData(), 0);
        x = rgb_matrix::DrawText(
            panel, font, x, baseline, priceColor, nullptr,
            QStringLiteral("%1 ").arg(formatPrice(snapshot->lastPrice), 6).toUtf8().constData(), 0);
        rgb_matrix::DrawText(
            panel, font, x, baseline, changeColor, nullptr,
            QStringLiteral("%1").arg(formatChangePercent(snapshot->dailyChangePercent()), 4)
                .toUtf8()
                .constData(),
            0);
    }
}

// ---------------------------------------------------------------------------
// Chart mode: 5x8 ticker top-left, stacked 4x6 price/change top-right (the
// chart-header-stacked bring-up pattern's layout), chart beneath.
// ---------------------------------------------------------------------------
constexpr int kChartTop = 12;    // matches chart-header-stacked
constexpr int kChartBottom = 31;
constexpr int kChartHeight = kChartBottom - kChartTop + 1; // 20 px

void drawHeader(PanelView *panel, const Symbol &symbol, const StockSnapshot *snapshot,
                const GlobalConfig &global, const rgb_matrix::Font &big,
                const rgb_matrix::Font &small)
{
    rgb_matrix::DrawText(panel, big, 0, big.baseline(), kWhite, nullptr,
                         formatTicker(symbol).left(5).toUtf8().constData(), 1);

    if (!snapshot || !snapshot->isValid())
        return;

    const bool blank = !snapshot->marketOpen && global.closedMarketStyle == ClosedMarketStyle::Blank;
    if (blank)
        return;

    const bool stale = isStaleSession(snapshot->sessionAnchorEpoch);
    const Color changeColor = applyMarketState(trendColor(*snapshot), snapshot->marketOpen, stale,
                                              global.closedMarketStyle);
    const Color priceColor = applyPriceMarketState(kWhite, snapshot->marketOpen, stale,
                                                  global.closedMarketStyle);
    drawRightAlignedFixedWidth(panel, small, panel->width(), small.baseline(), priceColor,
                              formatPrice(snapshot->lastPrice));
    drawRightAlignedFixedWidth(panel, small, panel->width(), small.baseline() + 6, changeColor,
                              formatChangePercent(snapshot->dailyChangePercent())
                                  + QStringLiteral("%"));
}

// Maps a price to a chart-area row, given the [minPrice, maxPrice] range
// covered by the buckets being drawn. Higher price -> smaller y (top).
int mapPriceToY(float price, float minPrice, float maxPrice)
{
    if (maxPrice <= minPrice)
        return kChartTop + kChartHeight / 2; // flat/degenerate range: middle
    const float t = (price - minPrice) / (maxPrice - minPrice);
    const int y = kChartBottom - static_cast<int>(std::lround(t * (kChartHeight - 1)));
    return std::clamp(y, kChartTop, kChartBottom);
}

void drawCandles(PanelView *panel, const QVector<PriceBucket> &buckets, int startColumn,
                 float minPrice, float maxPrice)
{
    for (int i = 0; i < buckets.size(); ++i) {
        const PriceBucket &bucket = buckets.at(i);
        if (!bucket.hasData)
            continue; // no trades that slice - leave the column blank, not a fabricated candle
        const int x = startColumn + i;
        if (x < 0 || x >= panel->width())
            continue;
        const Color bright = bucket.isUp() ? kGreen : kRed;

        const int yHigh = mapPriceToY(bucket.high, minPrice, maxPrice);
        const int yLow = mapPriceToY(bucket.low, minPrice, maxPrice);
        rgb_matrix::DrawLine(panel, x, yHigh, x, yLow, dim(bright));

        const int yBodyTop = mapPriceToY(std::max(bucket.open, bucket.close), minPrice, maxPrice);
        const int yBodyBottom = mapPriceToY(std::min(bucket.open, bucket.close), minPrice, maxPrice);
        rgb_matrix::DrawLine(panel, x, yBodyTop, x, yBodyBottom, bright);
    }
}

void drawLineOrArea(PanelView *panel, const QVector<PriceBucket> &buckets, int startColumn,
                    float minPrice, float maxPrice, float referencePrice, bool marketOpen,
                    bool staleSession, ClosedMarketStyle closedStyle, bool filled)
{
    // Where the baseline itself sits on screen, for the area fill below to
    // stop at rather than running all the way to the chart's bottom edge -
    // see baselinePointColor()'s own comment for why. Degrades to the old
    // "fill straight to the bottom" behaviour if there's no reference price
    // to anchor to at all (referencePrice <= 0, expected to be rare).
    const int referenceY =
        referencePrice > 0.0f ? mapPriceToY(referencePrice, minPrice, maxPrice) : kChartBottom;

    int prevX = -1;
    int prevY = 0;
    int prevIndex = -1; // buckets[] index of the last real point drawn
    for (int i = 0; i < buckets.size(); ++i) {
        if (!buckets.at(i).hasData)
            continue; // no trades that slice - no point drawn here at all

        const int x = startColumn + i;
        if (x < 0 || x >= panel->width())
            continue;
        const float price = buckets.at(i).close;
        const int y = mapPriceToY(price, minPrice, maxPrice);
        const Color pointColor = applyMarketState(baselinePointColor(price, referencePrice),
                                                 marketOpen, staleSession, closedStyle);

        // A segment connecting two real points that aren't in adjacent
        // slots bridges one or more no-trades gaps - drawn dimmed (not a
        // separate colour: whatever this point's own green/red/white
        // colour already is, or grey when closed and styled that way)
        // rather than at full brightness. Distinguishing by brightness
        // rather than hue means this needs no special-casing against the
        // closed-market grey styling - dim grey still reads as "different
        // from" full grey - and doesn't reuse a colour already spoken for
        // by the connectivity indicator. Still a plain line for legibility
        // (it's still useful to see the overall day's shape without gaps
        // chopping the chart into disconnected specks), but visually
        // flagged as "no data here", not a claim that the price moved
        // smoothly/gradually through a stretch we simply have no
        // information about. Coloured by the destination (this) point,
        // same simplification every point-to-point chart makes rather than
        // splitting a segment's colour exactly where it crosses the
        // baseline.
        const bool bridgesGap = prevX >= 0 && (i - prevIndex) > 1;
        const Color segmentColor = bridgesGap ? dim(pointColor) : pointColor;

        // Fills from this point to the baseline, not to the chart's bottom
        // edge - green fill sits between the point and the baseline when
        // the point is above it, red fill the same when below, matching
        // the real baseline-chart convention this whole thing is modelled
        // on rather than one solid colour block from top to bottom
        // regardless of which side of the baseline the price actually sits
        // on. Every "x" reaching this point is a real data point's own
        // column - the gap columns in between were already skipped via
        // `continue` above and never get a fill drawn for them at all,
        // bridged or not - so there's no "solid block spanning the gap" to
        // withhold here either.
        //
        // The fill deliberately never reaches referenceY itself (stops one
        // row short on either side) - filling all the way through it
        // painted over drawReferenceLine()'s dashed marker on essentially
        // every column with real data, making it invisible in practice
        // rather than just "occasionally drawn over", which defeated the
        // point of having it. Leaving that one row alone keeps it visible -
        // as an actual dash where the dash pattern lands, and as a thin gap
        // in the fill elsewhere, which reads as a continuous baseline groove
        // across the whole chart width instead of just isolated dashes.
        if (filled) {
            const Color fillColor = dim(pointColor);
            if (y <= referenceY - 2)
                rgb_matrix::DrawLine(panel, x, y + 1, x, referenceY - 1, fillColor);
            else if (y >= referenceY + 2)
                rgb_matrix::DrawLine(panel, x, referenceY + 1, x, y - 1, fillColor);
            // Within one row of the baseline (or exactly on it): nothing to
            // fill without touching referenceY itself.
        }

        if (prevX >= 0)
            rgb_matrix::DrawLine(panel, prevX, prevY, x, y, segmentColor);
        else
            panel->SetPixel(x, y, pointColor.r, pointColor.g, pointColor.b);
        prevX = x;
        prevY = y;
        prevIndex = i;
    }
}

// A dashed horizontal line at the reference price (previous close for real
// data - see StockSnapshot::referencePrice), drawn before the real data so
// it never draws over it. On an auto-scaled chart, a genuinely flat line
// can visually amplify a tiny move into a dramatic-looking slope with
// nothing to compare it against - this gives a fixed anchor to judge
// "above or below where the day started from" at a glance, the same
// mitigation most real trading chart sites use for the same reason.
// Dashed and dimmed specifically so it reads as a background reference
// marker, not a second data series.
void drawReferenceLine(PanelView *panel, float referencePrice, int startColumn, int visibleCount,
                       float minPrice, float maxPrice)
{
    if (referencePrice <= 0.0f)
        return;
    const int y = mapPriceToY(referencePrice, minPrice, maxPrice);
    const Color lineColor = dim(kGrey, 2);
    for (int i = 0; i < visibleCount; ++i) {
        if (i % 2 != 0)
            continue; // dashed, not solid - a background marker, not a data line
        const int x = startColumn + i;
        if (x < 0 || x >= panel->width())
            continue;
        panel->SetPixel(x, y, lineColor.r, lineColor.g, lineColor.b);
    }
}

void drawChart(PanelView *panel, const StockSnapshot &snapshot, DisplayMode mode,
              ClosedMarketStyle closedStyle)
{
    const QVector<PriceBucket> &buckets = snapshot.buckets;
    if (buckets.isEmpty())
        return;

    // Left-aligned: column 0 is the session's opening bucket, growing
    // rightward as the day progresses, same mental model as "the chart
    // builds up from the left at market open and finishes on the right at
    // close" - not centred, which would put blank "no data yet" margin on
    // both sides of a partial/sparse session instead of just on the right,
    // where unelapsed/not-yet-happened time actually belongs. Was centred
    // previously (a leftover from when MockDataProvider always populated a
    // full kBucketCount buckets, so this only ever had a few px of cosmetic
    // slack to place); with real, possibly sparse data - either early in a
    // session, or a thinly-traded listing - that produced an oddly
    // disconnected-looking handful of columns floating mid-panel instead of
    // starting at the left edge like a real chart. Still falls back to
    // showing the most recent history first if there's ever more data than
    // columns, though in practice kBucketCount <= panel->width() always.
    const int visibleCount = std::min(static_cast<int>(buckets.size()), panel->width());
    const int startColumn = 0;
    const QVector<PriceBucket> visible = buckets.mid(buckets.size() - visibleCount);

    // Gap slots (hasData == false, see YahooDataProvider) default-construct
    // to 0/0/0/0 and must be excluded here - including one would drag
    // minPrice down to 0 and badly distort the whole chart's vertical
    // scale. The most recent slot in "visible" is always real data (see
    // YahooDataProvider - the dense array never extends past the last
    // actual trade), so there's always at least one to seed from.
    float minPrice = 0.0f;
    float maxPrice = 0.0f;
    bool havePrice = false;
    for (const PriceBucket &bucket : visible) {
        if (!bucket.hasData)
            continue;
        if (!havePrice) {
            minPrice = bucket.low;
            maxPrice = bucket.high;
            havePrice = true;
        } else {
            minPrice = std::min(minPrice, bucket.low);
            maxPrice = std::max(maxPrice, bucket.high);
        }
    }
    if (!havePrice)
        return;

    drawReferenceLine(panel, snapshot.referencePrice, startColumn, visibleCount, minPrice, maxPrice);

    const bool staleSession = isStaleSession(snapshot.sessionAnchorEpoch);

    switch (mode) {
    case DisplayMode::Candles:
        drawCandles(panel, visible, startColumn, minPrice, maxPrice);
        break;
    case DisplayMode::Area:
        drawLineOrArea(panel, visible, startColumn, minPrice, maxPrice, snapshot.referencePrice,
                      snapshot.marketOpen, staleSession, closedStyle, /*filled=*/true);
        break;
    case DisplayMode::Line:
        drawLineOrArea(panel, visible, startColumn, minPrice, maxPrice, snapshot.referencePrice,
                      snapshot.marketOpen, staleSession, closedStyle, /*filled=*/false);
        break;
    case DisplayMode::StockList:
        break; // unreachable - renderStockPanel() dispatches list mode to renderList()
    }
}

void renderChart(PanelView *panel, const DisplayConfig &display, const GlobalConfig &global,
                 const StockDataProvider &data, const rgb_matrix::Font &big,
                 const rgb_matrix::Font &small, qint64 elapsedMs)
{
    if (display.symbols.isEmpty())
        return;

    // Cycle through the configured symbols, dwelling rotationSeconds on each
    // - "rotating through the up to four stocks after a certain time [...],
    // starting over with the first one" (the cycle length is simply
    // symbolCount * rotationSeconds, so it always starts over cleanly).
    const int symbolCount = display.symbols.size();
    const qint64 dwellMs = std::max(1, global.rotationSeconds) * 1000LL;
    const int index = static_cast<int>((elapsedMs / dwellMs) % symbolCount);
    const Symbol &symbol = display.symbols.at(index);
    const StockSnapshot *snapshot = data.snapshot(symbol);

    drawHeader(panel, symbol, snapshot, global, big, small);

    if (!snapshot || !snapshot->isValid())
        return;
    if (!snapshot->marketOpen && global.closedMarketStyle == ClosedMarketStyle::Blank)
        return;

    drawChart(panel, *snapshot, display.mode, global.closedMarketStyle);
}

} // namespace

void renderStockPanel(PanelView *panel, const DisplayConfig &display, const AppConfig &config,
                      const StockDataProvider &data, FontStore &fonts, qint64 elapsedMs)
{
    QString error;
    const rgb_matrix::Font *big = fonts.font(QStringLiteral("5x8"), &error);
    const rgb_matrix::Font *small = fonts.font(QStringLiteral("4x6"), &error);
    if (!big || !small)
        return;

    switch (display.mode) {
    case DisplayMode::StockList:
        renderList(panel, display, config.global(), data, *small);
        break;
    case DisplayMode::Line:
    case DisplayMode::Area:
    case DisplayMode::Candles:
        renderChart(panel, display, config.global(), data, *big, *small, elapsedMs);
        break;
    }
}

void renderConnectivityIndicator(PanelView *panel, ConnectivityMonitor::State state,
                                 bool dataIssue)
{
    Color color = kIndicatorNone;
    switch (state) {
    case ConnectivityMonitor::State::NoConnection:
        color = kIndicatorNone;
        break;
    case ConnectivityMonitor::State::WifiOnly:
        color = kIndicatorWifiOnly;
        break;
    case ConnectivityMonitor::State::Online:
        color = dataIssue ? kIndicatorDataIssue : kIndicatorOnline;
        break;
    }

    const int x = panel->width() - 2;
    const int y = panel->height() - 2;
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx)
            panel->SetPixel(x + dx, y + dy, color.r, color.g, color.b);
}

} // namespace hub75
