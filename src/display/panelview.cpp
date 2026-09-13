#include "panelview.h"

namespace hub75 {

PanelView::PanelView(rgb_matrix::Canvas *target, int offsetX, int offsetY,
                     int width, int height)
    : target_(target)
    , offsetX_(offsetX)
    , offsetY_(offsetY)
    , width_(width)
    , height_(height)
{
}

void PanelView::SetPixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue)
{
    if (!target_ || x < 0 || y < 0 || x >= width_ || y >= height_)
        return;
    target_->SetPixel(offsetX_ + x, offsetY_ + y, red, green, blue);
}

void PanelView::Clear()
{
    Fill(0, 0, 0);
}

void PanelView::Fill(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!target_)
        return;
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x)
            target_->SetPixel(offsetX_ + x, offsetY_ + y, red, green, blue);
    }
}

} // namespace hub75
