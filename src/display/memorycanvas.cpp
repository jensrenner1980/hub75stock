#include "memorycanvas.h"

#include <QStringList>

namespace hub75 {
namespace {
const QString kAnsiReset = QStringLiteral("\x1b[0m");
} // namespace

MemoryCanvas::MemoryCanvas(int width, int height)
    : width_(width)
    , height_(height)
    , data_(static_cast<qsizetype>(width) * height * 3, '\0')
{
}

void MemoryCanvas::SetPixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue)
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
        return;
    const qsizetype offset = (static_cast<qsizetype>(y) * width_ + x) * 3;
    data_[offset + 0] = static_cast<char>(red);
    data_[offset + 1] = static_cast<char>(green);
    data_[offset + 2] = static_cast<char>(blue);
}

void MemoryCanvas::Clear()
{
    data_.fill('\0');
}

void MemoryCanvas::Fill(uint8_t red, uint8_t green, uint8_t blue)
{
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x)
            SetPixel(x, y, red, green, blue);
    }
}

const uint8_t *MemoryCanvas::pixel(int x, int y) const
{
    const qsizetype offset = (static_cast<qsizetype>(y) * width_ + x) * 3;
    return reinterpret_cast<const uint8_t *>(data_.constData()) + offset;
}

QString MemoryCanvas::toAnsi(int panelWidth, int panelHeight) const
{
    // One text row shows two pixel rows: the upper half-block carries the
    // foreground colour, its background carries the lower pixel row.
    //
    // Escape sequences are only emitted where a colour actually changes.
    // Naively colouring every pixel costs around 40 bytes each, which is
    // megabytes per frame for a full wall.
    QStringList lines;
    for (int y = 0; y < height_; y += 2) {
        QString line;
        line.reserve(width_ * 8);
        int lastTop[3] = { -1, -1, -1 };
        int lastBottom[3] = { -1, -1, -1 };
        for (int x = 0; x < width_; ++x) {
            const uint8_t *top = pixel(x, y);
            if (top[0] != lastTop[0] || top[1] != lastTop[1] || top[2] != lastTop[2]) {
                line += QStringLiteral("\x1b[38;2;%1;%2;%3m")
                            .arg(top[0]).arg(top[1]).arg(top[2]);
                lastTop[0] = top[0]; lastTop[1] = top[1]; lastTop[2] = top[2];
            }
            if (y + 1 < height_) {
                const uint8_t *bottom = pixel(x, y + 1);
                if (bottom[0] != lastBottom[0] || bottom[1] != lastBottom[1]
                    || bottom[2] != lastBottom[2]) {
                    line += QStringLiteral("\x1b[48;2;%1;%2;%3m")
                                .arg(bottom[0]).arg(bottom[1]).arg(bottom[2]);
                    lastBottom[0] = bottom[0]; lastBottom[1] = bottom[1];
                    lastBottom[2] = bottom[2];
                }
            }
            line += QStringLiteral("▀"); // upper half block
        }
        line += kAnsiReset;
        lines << line;

        // Mark the horizontal seam when the next text row starts a new panel.
        const int nextPixelRow = y + 2;
        if (panelHeight > 0 && nextPixelRow < height_ && nextPixelRow % panelHeight == 0)
            lines << QString(width_, QLatin1Char('-'));
    }

    // Column ruler underneath, marking the vertical panel seams.
    if (panelWidth > 0) {
        QString ruler(width_, QLatin1Char(' '));
        for (int x = 0; x < width_; x += panelWidth)
            ruler[x] = QLatin1Char('|');
        lines << ruler;
    }

    return lines.join(QLatin1Char('\n'));
}

} // namespace hub75
