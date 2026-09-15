// SPDX-License-Identifier: GPL-2.0-only

#ifndef HUB75_MEMORYCANVAS_H
#define HUB75_MEMORYCANVAS_H

#include "canvas.h"

#include <QByteArray>
#include <QString>

namespace hub75 {

// A plain RGB buffer implementing the library's Canvas interface.
//
// Used by --dry-run so that layout, panel addressing and the test patterns can
// be exercised on the development host, where there is no Pi to talk to.
class MemoryCanvas : public rgb_matrix::Canvas
{
public:
    MemoryCanvas(int width, int height);

    int width() const override { return width_; }
    int height() const override { return height_; }
    void SetPixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue) override;
    void Clear() override;
    void Fill(uint8_t red, uint8_t green, uint8_t blue) override;

    // Renders the buffer as ANSI-coloured half-blocks (two pixel rows per text
    // line). Panel boundaries are marked so the arrangement is easy to check.
    QString toAnsi(int panelWidth, int panelHeight) const;

    // RGB triplet at (x, y); out of bounds is undefined (callers already
    // know the canvas's own width()/height()). Used by panelexport.cpp to
    // read back text rendered into a throwaway MemoryCanvas via the
    // library's own DrawText(), rather than pulling in Qt's font/painter
    // stack (which needs a QGuiApplication - this project only runs a
    // QCoreApplication - and crashes without one, confirmed directly).
    const uint8_t *pixel(int x, int y) const;

private:
    int width_ = 0;
    int height_ = 0;
    QByteArray data_;
};

} // namespace hub75

#endif // HUB75_MEMORYCANVAS_H
