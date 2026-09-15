// SPDX-License-Identifier: GPL-2.0-only

#include "pngwriter.h"

#include <QFile>
#include <QtEndian>

#include <zlib.h>

namespace hub75 {
namespace {

void appendChunk(QByteArray *out, const char *type, const QByteArray &data)
{
    QByteArray lengthField(4, Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(data.size()),
                          reinterpret_cast<uchar *>(lengthField.data()));
    out->append(lengthField);

    QByteArray typeAndData(type, 4);
    typeAndData.append(data);
    out->append(typeAndData);

    const uLong crc = crc32(0L, reinterpret_cast<const Bytef *>(typeAndData.constData()),
                            static_cast<uInt>(typeAndData.size()));
    QByteArray crcField(4, Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(crc), reinterpret_cast<uchar *>(crcField.data()));
    out->append(crcField);
}

} // namespace

bool writePng(const QString &path, int width, int height, const QByteArray &rgb, QString *error)
{
    if (rgb.size() != qsizetype(width) * height * 3) {
        *error = QStringLiteral("pixel buffer size does not match width*height*3");
        return false;
    }

    QByteArray raw;
    raw.reserve((width * 3 + 1) * height);
    for (int y = 0; y < height; ++y) {
        raw.append('\0'); // filter type 0 (None) for every row
        raw.append(rgb.constData() + qsizetype(y) * width * 3, width * 3);
    }

    uLongf compressedBound = compressBound(static_cast<uLong>(raw.size()));
    QByteArray compressed(static_cast<qsizetype>(compressedBound), Qt::Uninitialized);
    if (compress2(reinterpret_cast<Bytef *>(compressed.data()), &compressedBound,
                  reinterpret_cast<const Bytef *>(raw.constData()), static_cast<uLong>(raw.size()),
                  Z_BEST_COMPRESSION) != Z_OK) {
        *error = QStringLiteral("zlib compression failed");
        return false;
    }
    compressed.resize(static_cast<qsizetype>(compressedBound));

    QByteArray png;
    png.append("\x89PNG\r\n\x1a\n", 8);

    QByteArray ihdr(13, char(0));
    qToBigEndian<quint32>(static_cast<quint32>(width), reinterpret_cast<uchar *>(ihdr.data()));
    qToBigEndian<quint32>(static_cast<quint32>(height), reinterpret_cast<uchar *>(ihdr.data() + 4));
    ihdr[8] = char(8);  // bit depth
    ihdr[9] = char(2);  // color type: truecolor RGB
    ihdr[10] = char(0); // compression method (the only one PNG defines)
    ihdr[11] = char(0); // filter method (the only one PNG defines)
    ihdr[12] = char(0); // interlace method: none

    appendChunk(&png, "IHDR", ihdr);
    appendChunk(&png, "IDAT", compressed);
    appendChunk(&png, "IEND", QByteArray());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("could not open \"%1\" for writing").arg(path);
        return false;
    }
    if (file.write(png) != png.size()) {
        *error = QStringLiteral("short write to \"%1\"").arg(path);
        return false;
    }
    return true;
}

} // namespace hub75
