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

// The global connectivity indicator (row 1/column 1's corner only) is
// purely a network-layer signal - red/blue/green for no-WiFi/WiFi-only/
// online, deliberately reusing kRed/kGreen (a conscious choice, not an
// oversight - see renderConnectivityIndicator()'s own comment for why data
// health doesn't belong here at all any more). Blue is the one colour that
// needs its own constant, since neither kRed nor kGreen fits "WiFi up, no
// internet".
const Color kIndicatorWifiOnly(0, 140, 255);

// Per-symbol data-health tint for that symbol's own price/change (applied
// on top of the normal market-state colour - see applyDataHealth()).
// Deliberately amber, not red: red already means "price down" right next
// to these same numbers, and a stale figure sitting in red would read as a
// price move that didn't happen. Bright for "fetch succeeded, no data yet"
// (a mild, usually-brief situation - confirmed for real: the first ~20-30
// min after a session opens, before Yahoo's backend has published a bar);
// dim for a genuine fetch error, reusing the same "dim = less certain"
// language already used for gap-bridged chart lines elsewhere in this
// file, so the more serious of the two situations reads as the *more*
// uncertain one, not the brighter/more attention-grabbing one.
const Color kAmber(255, 140, 0);

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
// colour choice - the chart body and the day's % change (and, since the
// price now always matches the change colour - see the price/change call
// sites below - the price figure too) stay at full, undimmed colour the
// whole time the session being shown is merely "after hours", not actually
// stale. They're still exactly correct regardless of whether the market
// happens to be open right now: the chart is a complete record of the whole
// day so far, and the day's change (and the last price, which used to get
// its own separate dimmed treatment here - deliberately retired in favour
// of always matching the change colour, for a more uniform list view) is
// the real, final number once the session's over. Grey still applies once
// the session itself is stale (see isStaleSession()) - a previous day's
// frozen close is a fundamentally different situation, not just "closed for
// now". Normal always passes normal through unchanged regardless of any of
// this; Blank is handled by the caller (it skips drawing instead).
Color applyMarketState(const Color &normal, bool marketOpen, bool staleSession,
                       ClosedMarketStyle style)
{
    if (marketOpen || style != ClosedMarketStyle::Grey || !staleSession)
        return normal;
    return kGrey;
}

// Overrides an otherwise-normal price/change colour with the amber
// data-health tint (see kAmber's own comment for the bright/dim
// reasoning), when the most recent fetch attempt for this specific symbol
// had a problem. Takes priority over the market-state colour it's given -
// an active fetch problem happening right now is a more urgent thing to
// surface than plain after-hours/stale-session styling.
Color applyDataHealth(const Color &normal, StockDataProvider::DataHealth health)
{
    switch (health) {
    case StockDataProvider::DataHealth::Error:
        return dim(kAmber);
    case StockDataProvider::DataHealth::NoData:
        return kAmber;
    case StockDataProvider::DataHealth::Ok:
        return normal;
    }
    return normal;
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

// Maps an ISO 4217 code (StockSnapshot::currency) to the single-glyph
// currency symbol embedded in the project's own fonts - $/€/£/¥ for the
// four this project actually deals with, and the generic ISO "currency
// sign" (¤) for anything else/unrecognised/empty (MockDataProvider before
// it started setting a plausible one, or a real exchange in a currency
// this table doesn't know) - more informative than showing nothing at all,
// and every font glyph used here was confirmed to actually exist in
// resources/fonts/4x6.bdf before relying on it (a missing glyph falls back
// to the Unicode replacement character instead, which would look like a
// rendering bug rather than "unknown currency").
QChar currencySymbol(const QString &isoCode)
{
    if (isoCode == QStringLiteral("USD"))
        return QLatin1Char('$');
    if (isoCode == QStringLiteral("EUR"))
        return QChar(0x20AC); // Euro sign
    if (isoCode == QStringLiteral("GBP"))
        return QChar(0x00A3); // pound sterling
    if (isoCode == QStringLiteral("JPY"))
        return QChar(0x00A5); // yen
    return QChar(0x00A4); // generic ISO 4217 currency sign
}

// "+3.2" / "-12.5" - signed percentage, 1 decimal, no "%" (added by the
// caller - a literal "%" in chart mode's header, folded into renderList's
// own compressed-width drawing for list mode - see drawCompressed()). The
// sign is always shown.
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

// Formats a price into a fixed digit-character budget - 4 total under 100
// (whole or fractional currency units don't need as much precision spelled
// out as a genuinely volatile/cheap price does), 5 total at or above it -
// distributing them between the integer and fractional parts by magnitude
// rather than a fixed decimal count: more digits before the point leaves
// fewer after it, down to none at all once the integer part alone reaches
// the budget. So a cheap price shows more fractional precision (e.g.
// "5.500", 1+3) and an expensive one shows less or none (e.g. "155.50",
// 3+2; "99999", 5+0, also this function's cap - a real equity price that
// high isn't expected, but the fixed-width layout needs a hard ceiling
// regardless). This isn't just a formatting choice: since the price block
// is right-aligned to a fixed end column (see README "Compact list mode"),
// a shorter digit budget makes the *whole* block narrower, which shifts
// the currency symbol before it further right automatically - no separate
// positioning logic needed, that's just what right-alignment already does
// with less content to fit.
QString formatPriceFixedWidth(float price)
{
    const float capped = std::min(price, 99999.0f);
    const int intDigits = QString::number(static_cast<qint64>(capped)).size();
    const int digitBudget = intDigits <= 2 ? 4 : 5; // under 100 -> one fewer digit total
    const int fracDigits = std::max(0, digitBudget - intDigits);
    return QString::number(capped, 'f', fracDigits);
}

// Formats a daily change percentage into a sign plus exactly 2 digit
// characters total - the fixed budget list mode's right-aligned change
// block assumes: one decimal place while the magnitude is still a single
// integer digit (e.g. "+6.7"), none once it reaches two (e.g. "+67") -
// capped at +/-99%, a move that large not being expected for a real stock,
// but, same as the price cap above, the fixed-width layout needs a hard
// ceiling regardless. Checked against 9.95, not 10.0: a value just under 10
// can round *up* to "10.0" at one decimal place, which would overflow the
// two-digit budget by a character - checking against the rounded boundary
// avoids that.
QString formatChangePercentFixedWidth(float changePercent)
{
    const QString sign = changePercent >= 0.0f ? QStringLiteral("+") : QStringLiteral("-");
    const float capped = std::min(std::abs(changePercent), 99.0f);
    const int decimals = capped < 9.95f ? 1 : 0;
    return sign + QString::number(capped, 'f', decimals);
}

// Advance for one glyph under the compressed-width rules drawCompressed()
// and compressedWidth() both follow - kept as one shared table so the two
// can never drift out of sync with each other. ' ' and '.' compress from
// the font's normal 4px advance down to 2px (see drawCompressed()'s own
// comment for why - the short version: a space has no ink to protect
// anywhere, and a period's ink sits in column 1 of its cell, not 0, so
// there's already a free column to reclaim). '%' advances only 3px - its
// own true ink width, with no reserved trailing blank column - since it's
// always the last glyph on its line in this layout, with nothing after it
// that would need the usual 1px gap.
int compressedAdvance(QChar ch)
{
    if (ch == QLatin1Char(' ') || ch == QLatin1Char('.'))
        return 2;
    if (ch == QLatin1Char('%'))
        return 3;
    return 4;
}

// Total width text would occupy under drawCompressed()'s own rules,
// without drawing anything - used to right-align a block whose rendered
// width depends on its actual content (e.g. how many digits a price needs).
int compressedWidth(const QString &text)
{
    int width = 0;
    for (const QChar &ch : text)
        width += compressedAdvance(ch);
    return width;
}

// Draws text one glyph at a time rather than as a single fixed-pitch
// DrawText() call, applying compressedAdvance()'s per-glyph rules instead
// of the font's normal uniform 4px pitch - frees exactly the width list
// mode needs to fit a currency symbol before the price and a "%" after the
// change, within the panel's fixed 64px line (see README "Compact list
// mode" for the exact budget).
//
// A '.' is drawn one column *before* its "natural" slot, not just advanced
// less: its own ink sits in column 1 of its cell, not column 0, so drawing
// it shifted left lands the dot flush against whatever precedes it
// (already blank there, from that glyph's own trailing column) while still
// leaving a full 1px gap before whatever follows. Simply truncating the
// advance without shifting the draw position would instead leave the dot
// flush against the *next* character instead of the previous one - worse,
// since every other glyph pairing on the line keeps its usual 1px gap.
int drawCompressed(PanelView *panel, const rgb_matrix::Font &font, int x, int baseline,
                   const Color &color, const QString &text)
{
    for (const QChar &ch : text) {
        if (ch == QLatin1Char(' ')) {
            x += 2;
            continue;
        }
        const bool isPeriod = ch == QLatin1Char('.');
        const QString single(ch);
        rgb_matrix::DrawText(panel, font, isPeriod ? x - 1 : x, baseline, color, nullptr,
                             single.toUtf8().constData(), 0);
        x += compressedAdvance(ch);
    }
    return x;
}

// ---------------------------------------------------------------------------
// List mode: up to 4 lines, 4x6 font, three fixed-column blocks per line -
// ticker, currency+price, change - each right- or left-aligned to its own
// fixed anchor so the same content lines up column-for-column across every
// row, not just left-aligned as one variable-width string. See README
// "Compact list mode" for the full column-by-column derivation; verified
// pixel-by-pixel against a hand-drawn reference mockup before implementing.
// ---------------------------------------------------------------------------
// Physical row capacity - 32px panel height / 8px line pitch (see README
// "Compact list mode"). Distinct from limits::kMaxSymbolsPerDisplay, which
// is how many symbols can be *assigned* to one display in total: once
// there are more symbols than fit on screen at once, the visible window
// scrolls through the rest instead of just truncating silently.
constexpr int kListVisibleRows = 4;

// A ticker longer than this is truncated (matching chart mode's own
// `.left(5)`) - 4 characters get a 16px cell (4x4px, left-aligned, padded);
// exactly 5 get a 20px cell instead of truncating a real 5-letter name down
// to 4. Anything already <=4 characters is padded out to exactly 4, not 5,
// so short tickers don't waste the extra column.
constexpr int kListTickerMaxChars = 5;

// Where the currency+price block's own last (always-blank) column lands -
// 0-indexed, so this is "one past" that column, matching drawCompressed()'s
// own return-value convention. The block is right-aligned here regardless
// of how many digits the price actually needs, which is what guarantees at
// least one blank column before it even in the worst case (a 5-character
// ticker's own 20px cell ends at column 20, one column short of this
// anchor minus the block's own max 26px width starting at column 20 too -
// they'd collide at exactly one shared column without the ticker's own
// built-in trailing blank column filling that gap; verified against a
// hand-drawn reference image before relying on it).
constexpr int kListPriceBlockEndX = 46;

// Where the change block ends - the panel's own right edge (64), since
// it's always the last content on the line and its own trailing glyph
// ('%', via compressedAdvance()) already omits the usual reserved blank
// column that would otherwise overshoot it.
constexpr int kListChangeBlockEndX = limits::kPanelWidth;

void renderList(PanelView *panel, const DisplayConfig &display, const GlobalConfig &global,
                const StockDataProvider &data, const rgb_matrix::Font &font, qint64 elapsedMs)
{
    const int totalSymbols = display.symbols.size();
    const int visibleRows = std::min(totalSymbols, kListVisibleRows);

    // A one-row-per-tick scrolling window, on the same cadence chart mode
    // already rotates symbols on - "the same delay as in chart view", per
    // the feature request this implements. offset advances by exactly one
    // symbol per rotationSeconds tick and wraps modulo totalSymbols, so
    // row r always shows symbols[(offset + r) % totalSymbols]: row 0 shows
    // the "oldest" visible symbol, row (visibleRows-1) the newest, and the
    // symbol that just scrolled off the top reappears at the bottom
    // exactly one tick later - a circular sliding window, not smooth pixel
    // scrolling (deliberately: a discrete once-per-tick row swap fits this
    // project's LED-matrix aesthetic and its existing rotation mechanic far
    // better than continuous motion would). No rotation at all - offset
    // always 0 - when everything already fits on screen at once, exactly
    // matching the previous static behaviour for the common <=4-symbol
    // case.
    const qint64 dwellMs = std::max(1, global.rotationSeconds) * 1000LL;
    const int offset = totalSymbols > kListVisibleRows
                           ? static_cast<int>((elapsedMs / dwellMs) % totalSymbols)
                           : 0;

    for (int line = 0; line < visibleRows; ++line) {
        const Symbol &symbol = display.symbols.at((offset + line) % totalSymbols);
        const int baseline = line * 8 + font.baseline() + 1;
        const QString rawTicker = formatTicker(symbol).left(kListTickerMaxChars);
        const int tickerCellChars = rawTicker.size() <= 4 ? 4 : 5;
        const QString ticker = QStringLiteral("%1").arg(rawTicker, -tickerCellChars);

        const StockSnapshot *snapshot = data.snapshot(symbol);
        if (!snapshot || !snapshot->isValid()) {
            // No data yet (e.g. just added to the config) - show the
            // ticker in its normal position, dashes at the price/change
            // anchors rather than nothing at all. Plain DrawText, not
            // drawCompressed() - see the real-data path below for why.
            rgb_matrix::DrawText(panel, font, 0, baseline, kGrey, nullptr,
                                 ticker.toUtf8().constData(), 0);
            const QString priceDashes = QStringLiteral("-----");
            drawCompressed(panel, font, kListPriceBlockEndX - compressedWidth(priceDashes),
                           baseline, kGrey, priceDashes);
            const QString changeDashes = QStringLiteral("--%");
            drawCompressed(panel, font, kListChangeBlockEndX - compressedWidth(changeDashes),
                           baseline, kGrey, changeDashes);
            continue;
        }

        if (!snapshot->marketOpen && global.closedMarketStyle == ClosedMarketStyle::Blank)
            continue;

        const bool stale = isStaleSession(snapshot->sessionAnchorEpoch);
        const StockDataProvider::DataHealth health = data.dataHealth(symbol);
        const Color changeColor = applyDataHealth(
            applyMarketState(trendColor(*snapshot), snapshot->marketOpen, stale,
                            global.closedMarketStyle),
            health);
        // The price always matches the change colour - see applyMarketState()'s
        // own comment for why the separate after-hours price dimming was
        // retired in favour of this.
        const Color priceColor = changeColor;

        // Ticker left-aligned at column 0, plain DrawText rather than
        // drawCompressed() - the ticker never contains a period, and its
        // padding is meant to fill its cell at the font's normal 4px
        // pitch, not the 2px drawCompressed() would give a literal space
        // (that compression is specifically for the deliberate separator/
        // decimal spaces elsewhere, not structural padding here - using it
        // would still render correctly, since padding is blank either way
        // and nothing depends on exactly where it ends, but would silently
        // stop matching the stated 16px/20px cell width). Price and change
        // are each right-aligned to their own fixed anchor regardless of
        // how wide their actual content is, which is what makes every row
        // line up the same way rather than drifting with each symbol's own
        // price magnitude.
        rgb_matrix::DrawText(panel, font, 0, baseline, kWhite, nullptr,
                             ticker.toUtf8().constData(), 0);

        const QString priceText = currencySymbol(snapshot->currency)
            + formatPriceFixedWidth(snapshot->lastPrice);
        drawCompressed(panel, font, kListPriceBlockEndX - compressedWidth(priceText), baseline,
                       priceColor, priceText);

        const QString changeText =
            formatChangePercentFixedWidth(snapshot->dailyChangePercent()) + QStringLiteral("%");
        drawCompressed(panel, font, kListChangeBlockEndX - compressedWidth(changeText), baseline,
                       changeColor, changeText);
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
                const GlobalConfig &global, const StockDataProvider &data,
                const rgb_matrix::Font &big, const rgb_matrix::Font &small)
{
    rgb_matrix::DrawText(panel, big, 0, big.baseline(), kWhite, nullptr,
                         formatTicker(symbol).left(5).toUtf8().constData(), 1);

    if (!snapshot || !snapshot->isValid())
        return;

    const bool blank = !snapshot->marketOpen && global.closedMarketStyle == ClosedMarketStyle::Blank;
    if (blank)
        return;

    const bool stale = isStaleSession(snapshot->sessionAnchorEpoch);
    const StockDataProvider::DataHealth health = data.dataHealth(symbol);
    const Color changeColor = applyDataHealth(
        applyMarketState(trendColor(*snapshot), snapshot->marketOpen, stale,
                        global.closedMarketStyle),
        health);
    // The price always matches the change colour - a deliberate
    // simplification over the price having its own separate after-hours
    // dimming, in favour of the two numbers always reading as one
    // consistent unit at a glance.
    const Color priceColor = changeColor;
    drawRightAlignedFixedWidth(panel, small, panel->width(), small.baseline(), priceColor,
                              currencySymbol(snapshot->currency) + formatPrice(snapshot->lastPrice));
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
    Color prevColor = kWhite; // unused until prevX >= 0
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
        // information about.
        const bool bridgesGap = prevX >= 0 && (i - prevIndex) > 1;

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

        // A segment whose two endpoints land on opposite sides of the
        // baseline is split exactly where it crosses it, rather than
        // painting the whole segment in just the destination point's
        // colour - green above, red below, precisely at the baseline
        // rather than one point late. y != prevY guards the degenerate
        // case where both ends round to the same row despite differing
        // colours (only possible right at the boundary) - nothing
        // meaningful to interpolate there, so it falls back to the
        // single-colour path like a non-crossing segment.
        const bool crosses = prevX >= 0
                             && (prevColor.r != pointColor.r || prevColor.g != pointColor.g
                                 || prevColor.b != pointColor.b)
                             && y != prevY;
        if (crosses) {
            const float t = (referenceY - prevY) / static_cast<float>(y - prevY);
            const int crossX = prevX + static_cast<int>(std::lround(t * (x - prevX)));
            const Color firstColor = bridgesGap ? dim(prevColor) : prevColor;
            const Color secondColor = bridgesGap ? dim(pointColor) : pointColor;
            rgb_matrix::DrawLine(panel, prevX, prevY, crossX, referenceY, firstColor);
            rgb_matrix::DrawLine(panel, crossX, referenceY, x, y, secondColor);
        } else if (prevX >= 0) {
            const Color segmentColor = bridgesGap ? dim(pointColor) : pointColor;
            rgb_matrix::DrawLine(panel, prevX, prevY, x, y, segmentColor);
        } else {
            panel->SetPixel(x, y, pointColor.r, pointColor.g, pointColor.b);
        }
        prevX = x;
        prevY = y;
        prevColor = pointColor;
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

    // Left-aligned from a small fixed offset: column startColumn is the
    // session's opening bucket, growing rightward as the day progresses,
    // same mental model as "the chart builds up from the left at market
    // open and finishes on the right at close" - not *proportionally*
    // centred (i.e. not `(panel->width() - visibleCount) / 2`), which would
    // put blank "no data yet" margin on both sides of a partial/sparse
    // session instead of just on the right, where unelapsed/not-yet-happened
    // time actually belongs. Was proportionally centred previously (a
    // leftover from when MockDataProvider always populated a full
    // kBucketCount buckets, so this only ever had a few px of cosmetic slack
    // to place); with real, possibly sparse data - either early in a
    // session, or a thinly-traded listing - that produced an oddly
    // disconnected-looking handful of columns floating mid-panel instead of
    // starting near the left edge like a real chart.
    //
    // The fixed 2px offset below is a different thing, not a reintroduction
    // of that bug: it's constant regardless of how much data is actually
    // present, so a sparse session still starts building at column 2 (just
    // doesn't reach as far right yet), never floating. It exists purely so
    // the chart has a uniform 2px border on both sides once a session is
    // fully populated: kBucketCount (60) + 2 + 2 == panel->width() (64)
    // exactly.
    const int startColumn = 2;
    const int visibleCount =
        std::min(static_cast<int>(buckets.size()), panel->width() - startColumn);
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

    // Widen (never shrink) the range with the session's authoritative day
    // high/low (see StockSnapshot::dayHigh/dayLow) - our own bucket data
    // can undershoot the true extremes early in a session, or during
    // Yahoo's reporting lag, if the tick that set the real high/low hasn't
    // landed in a completed bucket yet. Guarded against 0 (unset - never a
    // legitimate price) rather than assuming it's always populated.
    if (snapshot.dayHigh > 0.0f)
        maxPrice = std::max(maxPrice, snapshot.dayHigh);
    if (snapshot.dayLow > 0.0f)
        minPrice = std::min(minPrice, snapshot.dayLow);

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

    drawHeader(panel, symbol, snapshot, global, data, big, small);

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
        renderList(panel, display, config.global(), data, *small, elapsedMs);
        break;
    case DisplayMode::Line:
    case DisplayMode::Area:
    case DisplayMode::Candles:
        renderChart(panel, display, config.global(), data, *big, *small, elapsedMs);
        break;
    }
}

void renderConnectivityIndicator(PanelView *panel, ConnectivityMonitor::State state)
{
    // Purely a network-layer signal now - data health used to be folded in
    // here too (a fourth/fifth colour for "fetch is failing"), but that
    // meant this one global indicator stayed stuck on the worst symbol's
    // state even once other symbols had already recovered - confirmed
    // confusing for real on live hardware. Data health now lives directly
    // on each symbol's own price/change instead (see applyDataHealth()),
    // which is both more precise (names *which* symbol) and doesn't lag
    // behind individual recoveries the way one aggregate did.
    Color color = kIndicatorWifiOnly;
    switch (state) {
    case ConnectivityMonitor::State::NoConnection:
        color = kRed;
        break;
    case ConnectivityMonitor::State::WifiOnly:
        color = kIndicatorWifiOnly;
        break;
    case ConnectivityMonitor::State::Online:
        color = kGreen;
        break;
    }

    const int x = panel->width() - 2;
    const int y = panel->height() - 2;
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx)
            panel->SetPixel(x + dx, y + dy, color.r, color.g, color.b);
}

} // namespace hub75
