// SPDX-License-Identifier: GPL-2.0-only

#ifndef HUB75_PNGWRITER_H
#define HUB75_PNGWRITER_H

#include <QByteArray>
#include <QString>

namespace hub75 {

// Writes a minimal, 8-bit truecolor PNG - just enough of the format to save
// the flat, hard-edged pixel art this project ever produces (LED panel
// exports), without pulling in Qt6::Gui purely to call QImage::save(). That
// matters on the actual target: Qt6::Gui's *hard* dependencies (not just
// Recommends) drag in the entire X11/EGL/OpenGL/input library stack -
// confirmed for real to be too much for a Raspberry Pi's SD card. The only
// dependency here is zlib, for the DEFLATE compression PNG's IDAT chunk
// requires - a near-universal, tiny library already linked transitively by
// half of Qt itself.
//
// "rgb" is width*height*3 bytes, row-major, top row first, no padding
// between rows and no per-row filter byte (added internally - every row
// uses PNG filter type 0/None, the simplest choice and a fine one for flat
// pixel art with no photographic gradients that would actually benefit from
// a smarter filter).
bool writePng(const QString &path, int width, int height, const QByteArray &rgb, QString *error);

} // namespace hub75

#endif // HUB75_PNGWRITER_H
