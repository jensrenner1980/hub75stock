// SPDX-License-Identifier: GPL-2.0-only

#include "panelview.h"

namespace hub75 {

PanelView::PanelView(rgb_matrix::Canvas *target, int offsetX, int offsetY,
                     int width, int height)
    : target_(target)
    , offsetX_(offsetX)
    , offsetY_(offsetY)
    , width_(width)
    , height_(height)
    , shadow_(width * height * 3, '\0')
{
}

void PanelView::SetPixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue)
{
    if (!target_ || x < 0 || y < 0 || x >= width_ || y >= height_)
        return;
    target_->SetPixel(offsetX_ + x, offsetY_ + y, red, green, blue);
    uint8_t *slot = reinterpret_cast<uint8_t *>(shadow_.data()) + (y * width_ + x) * 3;
    slot[0] = red;
    slot[1] = green;
    slot[2] = blue;
}

void PanelView::Clear()
{
    Fill(0, 0, 0);
}

void PanelView::Fill(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!target_)
        return;
    // Via SetPixel() (not target_->SetPixel() directly, as before) so the
    // shadow buffer stays in sync here too, not just on individual
    // SetPixel() calls.
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x)
            SetPixel(x, y, red, green, blue);
    }
}

void PanelView::clearShadow()
{
    shadow_.fill('\0');
}

void PanelView::shadowPixel(int x, int y, uint8_t *red, uint8_t *green, uint8_t *blue) const
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        *red = *green = *blue = 0;
        return;
    }
    const uint8_t *slot = reinterpret_cast<const uint8_t *>(shadow_.constData()) + (y * width_ + x) * 3;
    *red = slot[0];
    *green = slot[1];
    *blue = slot[2];
}

} // namespace hub75
