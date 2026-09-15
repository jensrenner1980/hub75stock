// SPDX-License-Identifier: GPL-2.0-only

#include "panelexport.h"

#include "fontstore.h"
#include "memorycanvas.h"
#include "panelview.h"
#include "pngwriter.h"
#include "textutil.h"

#include "graphics.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>

namespace hub75 {
namespace {

constexpr int kCellSize = 20;                          // total px per real LED, border included
constexpr int kBorderSize = 2;                          // px of black border on each side
constexpr int kInnerSize = kCellSize - 2 * kBorderSize; // 16px actual fill

struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};
constexpr Rgb kOffColor{0x0f, 0x0f, 0x0f};
constexpr Rgb kWatermarkColor{70, 130, 240};

constexpr int kWatermarkScale = 6; // 4x6 font's 6px height * 6 = 36px, exactly
constexpr int kWatermarkMargin = 12;

// image is a plain width*height*3 RGB buffer (see pngwriter.h) - no QImage,
// so no Qt6::Gui dependency for this whole file.
void setPixel(QByteArray *image, int width, int x, int y, Rgb color)
{
    uint8_t *p = reinterpret_cast<uint8_t *>(image->data()) + (qsizetype(y) * width + x) * 3;
    p[0] = color.r;
    p[1] = color.g;
    p[2] = color.b;
}

} // namespace

bool exportPanelPng(const PanelView &panel, FontStore &fonts, const QString &directory,
                    QString *error)
{
    const int panelWidth = panel.width();
    const int panelHeight = panel.height();
    const int imageWidth = panelWidth * kCellSize;
    const int imageHeight = panelHeight * kCellSize;

    // Zero-initialised, so the border pixels between/around cells (never
    // explicitly written below) come out black for free.
    QByteArray image(qsizetype(imageWidth) * imageHeight * 3, char(0));

    for (int y = 0; y < panelHeight; ++y) {
        for (int x = 0; x < panelWidth; ++x) {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            panel.shadowPixel(x, y, &r, &g, &b);
            const bool lit = r != 0 || g != 0 || b != 0;
            const Rgb fill = lit ? Rgb{r, g, b} : kOffColor;
            const int left = x * kCellSize + kBorderSize;
            const int top = y * kCellSize + kBorderSize;
            for (int dy = 0; dy < kInnerSize; ++dy) {
                for (int dx = 0; dx < kInnerSize; ++dx)
                    setPixel(&image, imageWidth, left + dx, top + dy, fill);
            }
        }
    }

    // Watermark: rendered with the project's own embedded BDF font via the
    // library's own DrawText(), onto a throwaway MemoryCanvas at native
    // resolution, then blitted into the image scaled up - not Qt's
    // QPainter/QFont, which need a QGuiApplication this QCoreApplication-only
    // app doesn't have (confirmed directly: it aborts without one, even with
    // QT_QPA_PLATFORM=offscreen set). Not fatal if the font can't load -
    // the panel image itself is still valid without the watermark.
    const rgb_matrix::Font *font = fonts.font(QStringLiteral("4x6"), error);
    if (font) {
        const QString watermark = QStringLiteral("hub75stock");
        const int nativeWidth = textutil::textWidth(*font, watermark, /*kerning=*/1);
        const int nativeHeight = font->height();
        MemoryCanvas watermarkCanvas(nativeWidth, nativeHeight);
        rgb_matrix::DrawText(&watermarkCanvas, *font, 0, font->baseline(),
                             rgb_matrix::Color(kWatermarkColor.r, kWatermarkColor.g,
                                               kWatermarkColor.b),
                             nullptr, watermark.toUtf8().constData(), 1);

        const int scaledHeight = nativeHeight * kWatermarkScale;
        // Bottom-left, not bottom-right - the connectivity indicator (see
        // stockrenderer.cpp) always lives in the bottom-right corner of
        // row 1/column 1's panel, and the two would otherwise compete for
        // the same spot on that panel's export.
        const int originX = kWatermarkMargin;
        const int originY = imageHeight - scaledHeight - kWatermarkMargin;
        for (int ny = 0; ny < nativeHeight; ++ny) {
            for (int nx = 0; nx < nativeWidth; ++nx) {
                const uint8_t *px = watermarkCanvas.pixel(nx, ny);
                if (px[0] == 0 && px[1] == 0 && px[2] == 0)
                    continue; // unlit - leave the panel image showing through
                const Rgb glyphColor{px[0], px[1], px[2]};
                for (int dy = 0; dy < kWatermarkScale; ++dy) {
                    for (int dx = 0; dx < kWatermarkScale; ++dx) {
                        setPixel(&image, imageWidth, originX + nx * kWatermarkScale + dx,
                                originY + ny * kWatermarkScale + dy, glyphColor);
                    }
                }
            }
        }
    }

    QDir dir(directory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        *error = QStringLiteral("could not create export directory \"%1\"").arg(directory);
        return false;
    }

    const QString filename =
        QStringLiteral("R%1C%2_%3.png")
            .arg(panel.row())
            .arg(panel.column())
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = dir.filePath(filename);
    return writePng(path, imageWidth, imageHeight, image, error);
}

} // namespace hub75
