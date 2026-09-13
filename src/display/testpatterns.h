#ifndef HUB75_TESTPATTERNS_H
#define HUB75_TESTPATTERNS_H

#include <QString>
#include <QVector>

namespace rgb_matrix {
class Font;
}

namespace hub75 {

class AppConfig;
class FontStore;
class MatrixWall;

// Patterns for bringing the wall up on real hardware, before any stock data
// is involved: they verify wiring, chain order, panel addressing, orientation,
// colour mapping and the text layout budget.
enum class TestPattern {
    Identify,    // per-panel label + border: check numbering and arrangement
    PanelOrder,  // light one panel at a time, in chain order
    Border,      // frame plus diagonals: check edges and alignment
    ColorCycle,  // solid red/green/blue/white: check RGB mapping
    White,       // solid white, held indefinitely: for measuring true sustained
                 // current draw - color-cycle's 1s-per-colour dwell is too brief
                 // for many PSU ammeters/lagging displays to settle on
    Greyscale,   // horizontal ramp: check PWM bits and gamma
    TextGrid,    // 4 lines x 10 characters: check the 5x8 text budget
    Chase,       // single pixel walking each panel: check for bleed
    List16,      // 4 lines x 16 characters in 4x6: check the compact list layout
    ChartHeaderStacked, // 5x8 ticker + stacked 4x6 price/change, 2/3 chart area
    ChartHeaderInline,  // 5x8 ticker + inline 4x6 price/change, 3/4 chart area
};

struct TestPatternInfo {
    TestPattern pattern;
    QString name;
    QString description;
};

QVector<TestPatternInfo> availableTestPatterns();
bool testPatternFromName(const QString &name, TestPattern *out);
QString toString(TestPattern pattern);

// Draws one frame of "pattern" into the wall's back buffer. The caller is
// responsible for clearing beforehand and presenting afterwards. "elapsedMs"
// drives the animated patterns.
//
// "font" is the face selected on the command line (--font) and is what
// single-font patterns draw with. "fonts" lets the chart-header patterns pull
// in a second, smaller face (always 4x6) regardless of that selection, since
// comparing two sizes at once is the point of those patterns.
void renderTestPattern(TestPattern pattern, MatrixWall *wall, const AppConfig &config,
                       const rgb_matrix::Font &font, FontStore &fonts, qint64 elapsedMs);

} // namespace hub75

#endif // HUB75_TESTPATTERNS_H
