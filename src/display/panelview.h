// SPDX-License-Identifier: GPL-2.0-only

#ifndef HUB75_PANELVIEW_H
#define HUB75_PANELVIEW_H

#include "canvas.h"

#include <QByteArray>
#include <cstdint>

namespace hub75 {

// A single 64x32 panel, addressed in its own local coordinate system.
//
// The library hands out one large canvas for the whole wall (chain length x 64
// wide, chain count x 32 high). This view maps local (0,0) to the panel's
// top-left corner on that canvas, so every drawing routine - including the
// library's own DrawText()/DrawLine() helpers, which take a Canvas* - can be
// written as if it owned a standalone 64x32 display. Writes outside the panel
// are clipped, so one panel can never bleed into its neighbour.
class PanelView : public rgb_matrix::Canvas
{
public:
    PanelView() = default;
    PanelView(rgb_matrix::Canvas *target, int offsetX, int offsetY, int width, int height);

    // The wall's back buffer changes on every SwapOnVSync(), so MatrixWall
    // re-points its views after each swap.
    void setTarget(rgb_matrix::Canvas *target) { target_ = target; }

    // 1-based position on the wall, for labels and log messages.
    void setPosition(int row, int column) { row_ = row; column_ = column; }
    int row() const { return row_; }
    int column() const { return column_; }

    int offsetX() const { return offsetX_; }
    int offsetY() const { return offsetY_; }

    // Canvas interface, in panel-local coordinates.
    int width() const override { return width_; }
    int height() const override { return height_; }
    void SetPixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue) override;
    void Clear() override;
    void Fill(uint8_t red, uint8_t green, uint8_t blue) override;

    // A plain in-memory mirror of whatever was last drawn to this panel,
    // updated alongside every SetPixel()/Fill() - separate from target_,
    // which on real hardware is a write-only, one-way GPIO output that
    // can't itself be queried for "what's currently displayed" (see
    // src/display/panelexport.h). Cleared by MatrixWall::clear() in step
    // with the real frame clear, so it never lags a frame behind.
    void clearShadow();
    void shadowPixel(int x, int y, uint8_t *red, uint8_t *green, uint8_t *blue) const;

private:
    rgb_matrix::Canvas *target_ = nullptr;
    int offsetX_ = 0;
    int offsetY_ = 0;
    int width_ = 0;
    int height_ = 0;
    int row_ = 0;
    int column_ = 0;
    QByteArray shadow_; // width_*height_*3 bytes, RGB per pixel
};

} // namespace hub75

#endif // HUB75_PANELVIEW_H
