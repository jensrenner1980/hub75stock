// SPDX-License-Identifier: GPL-2.0-only

#include "testpatterns.h"

#include "config/appconfig.h"
#include "fontstore.h"
#include "matrixwall.h"
#include "panelview.h"
#include "textutil.h"

#include "graphics.h"

namespace hub75 {
namespace {

using rgb_matrix::Color;
using textutil::drawCenteredText;
using textutil::drawRightAlignedFixedWidth;

// One hue per chain, so a glance tells you which electrical chain a panel
// hangs on.
Color chainColor(int row)
{
    switch (row) {
    case 1:  return Color(0, 255, 96);    // green
    case 2:  return Color(64, 160, 255);  // blue
    case 3:  return Color(255, 176, 0);   // amber
    default: return Color(255, 255, 255);
    }
}

void drawRect(rgb_matrix::Canvas *canvas, int x0, int y0, int x1, int y1, const Color &color)
{
    rgb_matrix::DrawLine(canvas, x0, y0, x1, y0, color);
    rgb_matrix::DrawLine(canvas, x1, y0, x1, y1, color);
    rgb_matrix::DrawLine(canvas, x1, y1, x0, y1, color);
    rgb_matrix::DrawLine(canvas, x0, y1, x0, y0, color);
}

void renderIdentify(PanelView *panel, const AppConfig &config, const rgb_matrix::Font &font)
{
    const Color color = chainColor(panel->row());
    const Color dim(color.r / 5, color.g / 5, color.b / 5);

    drawRect(panel, 0, 0, panel->width() - 1, panel->height() - 1, dim);

    // A solid 2x2 block in the top-left corner pins down the orientation: if
    // it shows up anywhere else, the panel is rotated or the chain is reversed.
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x)
            panel->SetPixel(x, y, color.r, color.g, color.b);
    }

    const QString label = QStringLiteral("R%1C%2").arg(panel->row()).arg(panel->column());
    drawCenteredText(panel, font, label, 11, color);

    // Second line: what this panel is configured to show, if anything.
    const DisplayConfig *display = config.displayAt(panel->row(), panel->column());
    if (display && !display->symbols.isEmpty()) {
        const QString first = display->symbols.first().displayLabel();
        drawCenteredText(panel, font, first.left(10), 23, Color(120, 120, 120));
    } else {
        drawCenteredText(panel, font, QStringLiteral("-"), 23, Color(80, 80, 80));
    }
}

void renderBorder(PanelView *panel)
{
    const Color white(255, 255, 255);
    const Color dim(0, 40, 70);
    drawRect(panel, 0, 0, panel->width() - 1, panel->height() - 1, white);
    rgb_matrix::DrawLine(panel, 0, 0, panel->width() - 1, panel->height() - 1, dim);
    rgb_matrix::DrawLine(panel, panel->width() - 1, 0, 0, panel->height() - 1, dim);
}

void renderColorCycle(PanelView *panel, qint64 elapsedMs)
{
    static const Color steps[] = {
        Color(255, 0, 0), Color(0, 255, 0), Color(0, 0, 255),
        Color(255, 255, 255), Color(0, 0, 0),
    };
    constexpr int stepCount = static_cast<int>(sizeof(steps) / sizeof(steps[0]));
    const int index = static_cast<int>((elapsedMs / 1000) % stepCount);
    panel->Fill(steps[index].r, steps[index].g, steps[index].b);
}

void renderWhite(PanelView *panel)
{
    panel->Fill(255, 255, 255);
}

void renderGreyscale(PanelView *panel)
{
    // 64 columns map to 0..255 in equal steps; banding here points at pwmBits
    // or pwmLsbNanoseconds being too low.
    for (int x = 0; x < panel->width(); ++x) {
        const uint8_t level = static_cast<uint8_t>(x * 255 / (panel->width() - 1));
        for (int y = 0; y < panel->height(); ++y)
            panel->SetPixel(x, y, level, level, level);
    }
}

void renderTextGrid(PanelView *panel, const rgb_matrix::Font &font)
{
    // The layout this project is designed around: four 8 px lines, ten
    // characters each at 5 px plus 1 px kerning. If the last character or the
    // last line is clipped, the budget is off.
    static const char *const lines[] = { "ABCDEFGHIJ", "0123456789", "+1.23 -4.5", "MNOPQRSTUV" };
    static const Color colors[] = {
        Color(255, 255, 255), Color(0, 255, 96), Color(255, 64, 64), Color(255, 176, 0),
    };
    for (int line = 0; line < 4; ++line) {
        const int baseline = line * 8 + font.baseline();
        rgb_matrix::DrawText(panel, font, 0, baseline, colors[line], nullptr,
                             lines[line], 1);
    }
}

// The candidate list-mode layout: four lines, sixteen 4x6 characters each,
// zero kerning (16 x 4 px = 64 px exactly), with one blank row above and
// below every character line (4 x (1 + 6 + 1) = 32 px exactly). No line
// touches its neighbour.
//
// baseline(line) = line*8 + font.baseline() + 1: the "+1" over the 5x8 grid's
// formula is exactly the one spare row above the glyph that the gap needs;
// it falls out of height=6 leaving 2 spare rows in an 8 px pitch, split
// symmetrically top and bottom.
void renderList16(PanelView *panel, const AppConfig &config, const rgb_matrix::Font &font)
{
    struct Row { const char *ticker; const char *price; const char *change; bool up; };
    static const Row rows[] = {
        { "IONQ", "42.17",  "+3.2", true },
        { "NVDA", "178.42", "-1.9", false },
        { "AAPL", "241.50", "+0.4", true },
        { "AMZN", "228.31", "-2.1", false },
    };

    const ClosedMarketStyle closedStyle = config.global().closedMarketStyle;
    for (int line = 0; line < 4; ++line) {
        const Row &row = rows[line];
        const Color color = closedStyle == ClosedMarketStyle::Grey
                                 ? Color(140, 140, 140)
                                 : (row.up ? Color(0, 255, 96) : Color(255, 64, 64));
        const int baseline = line * 8 + font.baseline() + 1;

        // ticker, right-padded to 4 chars, then price and a sign+1dp change -
        // the "trim what's needed" trick: no thousands separator, at most one
        // decimal on the change, so 4+1+6+1+4 = 16 chars exactly.
        QString text = QStringLiteral("%1 %2 %3")
                            .arg(row.ticker, -4)
                            .arg(row.price, 6)
                            .arg(row.change, 4);
        rgb_matrix::DrawText(panel, font, 0, baseline, color, nullptr,
                             text.toUtf8().constData(), 0);
    }
}

// Option A: ticker top-left in the primary font, price stacked directly
// above change in 4x6 top-right, no gap between the two - a 12 px header
// (0..11), chart area 20 px (12..31) = 62.5% of the panel, the closest exact
// fit to "lower two thirds".
void renderChartHeaderStacked(PanelView *panel, const AppConfig &config,
                              const rgb_matrix::Font &big, const rgb_matrix::Font &small)
{
    const DisplayConfig *display = config.displayAt(panel->row(), panel->column());
    const QString ticker = display && !display->symbols.isEmpty()
                                ? display->symbols.first().displayLabel()
                                : QStringLiteral("----");
    const bool up = true; // placeholder until real data is wired in
    const Color priceColor(255, 255, 255);
    const Color changeColor = up ? Color(0, 255, 96) : Color(255, 64, 64);

    rgb_matrix::DrawText(panel, big, 0, big.baseline(), Color(255, 255, 255), nullptr,
                         ticker.left(5).toUtf8().constData(), 1);
    drawRightAlignedFixedWidth(panel, small, panel->width(), small.baseline(),
                       priceColor, QStringLiteral("241.50"));
    drawRightAlignedFixedWidth(panel, small, panel->width(), small.baseline() + 6,
                       changeColor, QStringLiteral("+0.40"));

    static const int kChartTop = 12;
    rgb_matrix::DrawLine(panel, 0, kChartTop, panel->width() - 1, kChartTop,
                         Color(40, 40, 40));
}

// Option B: ticker top-left in the primary font, price and change on one
// 4x6 line top-right, sharing the ticker's 8 px header band - chart area
// 24 px (8..31) = 75% of the panel, more chart real estate, less separation
// between price and change.
void renderChartHeaderInline(PanelView *panel, const AppConfig &config,
                             const rgb_matrix::Font &big, const rgb_matrix::Font &small)
{
    const DisplayConfig *display = config.displayAt(panel->row(), panel->column());
    const QString ticker = display && !display->symbols.isEmpty()
                                ? display->symbols.first().displayLabel()
                                : QStringLiteral("----");
    const bool up = true; // placeholder until real data is wired in
    const Color changeColor = up ? Color(0, 255, 96) : Color(255, 64, 64);

    rgb_matrix::DrawText(panel, big, 0, big.baseline(), Color(255, 255, 255), nullptr,
                         ticker.left(5).toUtf8().constData(), 1);
    drawRightAlignedFixedWidth(panel, small, panel->width(), small.baseline() + 1,
                       changeColor, QStringLiteral("241.50+0.4"));

    static const int kChartTop = 8;
    rgb_matrix::DrawLine(panel, 0, kChartTop, panel->width() - 1, kChartTop,
                         Color(40, 40, 40));
}

void renderChase(PanelView *panel, qint64 elapsedMs)
{
    const int pixels = panel->width() * panel->height();
    if (pixels <= 0)
        return;
    // ~4 panels-worth per second; the trail makes the path easy to follow.
    const int head = static_cast<int>((elapsedMs * pixels / 8000) % pixels);
    const Color color = chainColor(panel->row());
    for (int trail = 0; trail < 12; ++trail) {
        const int index = ((head - trail) % pixels + pixels) % pixels;
        const int scale = 12 - trail;
        panel->SetPixel(index % panel->width(), index / panel->width(),
                        static_cast<uint8_t>(color.r * scale / 12),
                        static_cast<uint8_t>(color.g * scale / 12),
                        static_cast<uint8_t>(color.b * scale / 12));
    }
}

} // namespace

QVector<TestPatternInfo> availableTestPatterns()
{
    return {
        { TestPattern::Identify, QStringLiteral("identify"),
          QStringLiteral("per-panel R<row>C<col> label, chain-coloured border and a "
                         "top-left orientation marker") },
        { TestPattern::PanelOrder, QStringLiteral("panel-order"),
          QStringLiteral("lights one panel at a time in chain order, to verify the "
                         "daisy-chain sequence") },
        { TestPattern::Border, QStringLiteral("border"),
          QStringLiteral("1 px frame plus diagonals, to check edges and alignment") },
        { TestPattern::ColorCycle, QStringLiteral("color-cycle"),
          QStringLiteral("solid red/green/blue/white/black, to check the RGB mapping") },
        { TestPattern::White, QStringLiteral("white"),
          QStringLiteral("solid white, held indefinitely (use --duration), to measure "
                         "true sustained current draw - color-cycle's 1s dwell is too "
                         "brief for a lagging PSU ammeter to settle on") },
        { TestPattern::Greyscale, QStringLiteral("greyscale"),
          QStringLiteral("horizontal 0..255 ramp, to check PWM bits and gamma") },
        { TestPattern::TextGrid, QStringLiteral("text-grid"),
          QStringLiteral("4 lines x 10 characters in 5x8, to check the text budget") },
        { TestPattern::Chase, QStringLiteral("chase"),
          QStringLiteral("a single pixel walking each panel, to check for bleed "
                         "between panels") },
        { TestPattern::List16, QStringLiteral("list16"),
          QStringLiteral("4 lines x 16 characters in 4x6, to check the compact "
                         "list-mode layout") },
        { TestPattern::ChartHeaderStacked, QStringLiteral("chart-header-stacked"),
          QStringLiteral("5x8 ticker + stacked 4x6 price/change, chart area = 2/3 "
                         "of the panel") },
        { TestPattern::ChartHeaderInline, QStringLiteral("chart-header-inline"),
          QStringLiteral("5x8 ticker + inline 4x6 price/change, chart area = 3/4 "
                         "of the panel") },
    };
}

QString toString(TestPattern pattern)
{
    for (const TestPatternInfo &info : availableTestPatterns()) {
        if (info.pattern == pattern)
            return info.name;
    }
    return QStringLiteral("identify");
}

bool testPatternFromName(const QString &name, TestPattern *out)
{
    const QString key = name.trimmed().toLower();
    for (const TestPatternInfo &info : availableTestPatterns()) {
        if (info.name == key) {
            *out = info.pattern;
            return true;
        }
    }
    return false;
}

void renderTestPattern(TestPattern pattern, MatrixWall *wall, const AppConfig &config,
                       const rgb_matrix::Font &font, FontStore &fonts, qint64 elapsedMs)
{
    const QVector<PanelView *> &panels = wall->panels();

    if (pattern == TestPattern::PanelOrder) {
        if (panels.isEmpty())
            return;
        const int index = static_cast<int>((elapsedMs / 700) % panels.size());
        PanelView *panel = panels.at(index);
        const Color color = chainColor(panel->row());
        panel->Fill(color.r / 3, color.g / 3, color.b / 3);
        drawCenteredText(panel, font,
                         QStringLiteral("R%1C%2").arg(panel->row()).arg(panel->column()),
                         panel->height() / 2, Color(255, 255, 255));
        return;
    }

    // The chart-header patterns need 4x6 specifically (the point is to
    // compare it against the --font selection), independent of --font.
    QString fontError;
    const rgb_matrix::Font *small = fonts.font(QStringLiteral("4x6"), &fontError);

    for (PanelView *panel : panels) {
        switch (pattern) {
        case TestPattern::Identify:   renderIdentify(panel, config, font); break;
        case TestPattern::Border:     renderBorder(panel); break;
        case TestPattern::ColorCycle: renderColorCycle(panel, elapsedMs); break;
        case TestPattern::White:      renderWhite(panel); break;
        case TestPattern::Greyscale:  renderGreyscale(panel); break;
        case TestPattern::TextGrid:   renderTextGrid(panel, font); break;
        case TestPattern::Chase:      renderChase(panel, elapsedMs); break;
        case TestPattern::List16:
            if (small) renderList16(panel, config, *small);
            break;
        case TestPattern::ChartHeaderStacked:
            if (small) renderChartHeaderStacked(panel, config, font, *small);
            break;
        case TestPattern::ChartHeaderInline:
            if (small) renderChartHeaderInline(panel, config, font, *small);
            break;
        case TestPattern::PanelOrder: break; // handled above
        }
    }
}

} // namespace hub75
