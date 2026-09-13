#include "textutil.h"

#include <QtGlobal>

namespace hub75::textutil {

int glyphAdvance(const rgb_matrix::Font &font, uint32_t codepoint)
{
    const int width = font.CharacterWidth(codepoint);
    if (width >= 0)
        return width;
    return qMax(0, font.CharacterWidth(0xFFFD));
}

int textWidth(const rgb_matrix::Font &font, const QString &text, int kerning)
{
    if (text.isEmpty())
        return 0;
    int width = 0;
    for (const QChar ch : text)
        width += glyphAdvance(font, ch.unicode()) + kerning;
    return width - kerning; // no trailing kerning after the last glyph
}

void drawCenteredText(rgb_matrix::Canvas *canvas, const rgb_matrix::Font &font,
                      const QString &text, int centerY, const rgb_matrix::Color &color,
                      int kerning)
{
    const int x = (canvas->width() - textWidth(font, text, kerning)) / 2;
    const int baseline = centerY - font.height() / 2 + font.baseline();
    rgb_matrix::DrawText(canvas, font, x, baseline, color, nullptr,
                         text.toUtf8().constData(), kerning);
}

int drawRightAlignedFixedWidth(rgb_matrix::Canvas *canvas, const rgb_matrix::Font &font,
                               int rightEdge, int baseline, const rgb_matrix::Color &color,
                               const QString &text)
{
    const int width = text.size() * font.CharacterWidth('0');
    const int x = rightEdge - width;
    rgb_matrix::DrawText(canvas, font, x, baseline, color, nullptr,
                         text.toUtf8().constData(), 0);
    return x;
}

} // namespace hub75::textutil
