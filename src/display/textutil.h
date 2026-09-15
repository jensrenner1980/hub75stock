// SPDX-License-Identifier: GPL-2.0-only

#ifndef HUB75_TEXTUTIL_H
#define HUB75_TEXTUTIL_H

#include "graphics.h"

#include <QString>

// Small text-layout helpers shared between the hardware bring-up patterns
// (testpatterns.cpp) and the real stock renderer (stockrenderer.cpp), so the
// two don't drift out of sync on how glyph widths/centring/alignment work.
namespace hub75::textutil {

// Advance of one glyph, mirroring Font::DrawGlyph(): a missing character is
// rendered as the Unicode replacement glyph, and contributes nothing at all
// only when the font lacks that too. CharacterWidth() itself returns -1 for a
// missing glyph, which would otherwise shorten the measurement and throw the
// centring/alignment off.
int glyphAdvance(const rgb_matrix::Font &font, uint32_t codepoint);

// Text advance for a font, including "kerning" pixels of spacing between
// characters (no trailing kerning after the last glyph).
int textWidth(const rgb_matrix::Font &font, const QString &text, int kerning);

void drawCenteredText(rgb_matrix::Canvas *canvas, const rgb_matrix::Font &font,
                      const QString &text, int centerY, const rgb_matrix::Color &color,
                      int kerning = 1);

// Right-aligned text for a fixed-width font (e.g. the embedded 4x6, where
// every glyph is DWIDTH 4) - the advance is just chars * glyph width, no
// per-glyph measurement or kerning needed.
int drawRightAlignedFixedWidth(rgb_matrix::Canvas *canvas, const rgb_matrix::Font &font,
                               int rightEdge, int baseline, const rgb_matrix::Color &color,
                               const QString &text);

} // namespace hub75::textutil

#endif // HUB75_TEXTUTIL_H
