// SPDX-License-Identifier: GPL-2.0-only

#ifndef HUB75_PANELEXPORT_H
#define HUB75_PANELEXPORT_H

#include <QString>

namespace hub75 {

class FontStore;
class PanelView;

// Renders a panel's currently-displayed content (PanelView::shadowPixel())
// to a PNG file: one 20x20px block per real LED - a 2px black border
// framing a 16x16 fill, either the LED's actual lit colour or a dark
// "#0f0f0f" grey for anything that's genuinely unlit (pure black) - plus a
// small blue "hub75stock" watermark in the bottom-left corner (deliberately
// not bottom-right, which is where the connectivity indicator - see
// stockrenderer.cpp - always lives, on row 1/column 1's panel).
//
// Filename is "R<row>C<column>_<yyyyMMdd_HHmmss>.png", not the symbol(s)
// currently shown - a list-mode panel can show up to four simultaneously,
// and a chart-mode panel rotates through its own over time, so panel
// position is the only label that's ever unambiguous.
//
// The watermark is drawn using the project's own embedded BDF font (scaled
// up), not Qt's QPainter/QFont text APIs - those need a QGuiApplication,
// which this QCoreApplication-only app doesn't have and crashes without
// (confirmed directly). More generally, this file touches nothing from
// Qt6::Gui at all - the pixel buffer is a plain QByteArray, saved via
// pngwriter.h's own minimal encoder rather than QImage::save(), since
// Qt6::Gui's hard dependencies (the whole X11/EGL/OpenGL/input stack) turned
// out to be too much for a Raspberry Pi's SD card - confirmed for real.
//
// Returns false and fills *error on failure (e.g. the directory doesn't
// exist/isn't writable, or the watermark font failed to load).
bool exportPanelPng(const PanelView &panel, FontStore &fonts, const QString &directory,
                    QString *error);

} // namespace hub75

#endif // HUB75_PANELEXPORT_H
