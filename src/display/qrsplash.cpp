#include "qrsplash.h"

#include "panelview.h"

#include "graphics.h"
#include <qrencode.h>

#include <QVector>

#include <utility>

namespace hub75 {

namespace {
// Modules of black margin on each side, between the QR code and the panel
// edge/side text - real-world scanners are noticeably more tolerant of a
// thin quiet zone on a self-lit screen than on printed paper (no ambient
// reflections to compete with), so this is smaller than the 4-module
// minimum the spec recommends for print.
constexpr int kQuietZoneModules = 2;

const rgb_matrix::Color kWhite(255, 255, 255);

QRcode *asQRcode(void *code)
{
    return static_cast<QRcode *>(code);
}
} // namespace

QrSplash::QrSplash() = default;

QrSplash::QrSplash(void *code) : code_(code) {}

QrSplash::QrSplash(QrSplash &&other) noexcept : code_(std::exchange(other.code_, nullptr)) {}

QrSplash &QrSplash::operator=(QrSplash &&other) noexcept
{
    if (this != &other) {
        if (code_)
            QRcode_free(asQRcode(code_));
        code_ = std::exchange(other.code_, nullptr);
    }
    return *this;
}

QrSplash::~QrSplash()
{
    if (code_)
        QRcode_free(asQRcode(code_));
}

int QrSplash::size() const
{
    return code_ ? asQRcode(code_)->width : 0;
}

QrSplash QrSplash::forUrl(const QString &url, int panelSize)
{
    // version=0: let libqrencode pick the smallest version that fits the
    // data at this error-correction level, rather than us guessing/hard-
    // coding one - this stays correct regardless of exactly how long the
    // URL ends up being (a longer IP or port shouldn't need a code change
    // here, just a slightly bigger - or, if it doesn't fit, skipped - code).
    QRcode *code = QRcode_encodeString8bit(url.toUtf8().constData(), /*version=*/0, QR_ECLEVEL_L);
    if (!code)
        return QrSplash();

    if (code->width + 2 * kQuietZoneModules > panelSize) {
        QRcode_free(code);
        return QrSplash();
    }
    return QrSplash(code);
}

void QrSplash::render(PanelView *panel, int originX, int originY) const
{
    if (!code_)
        return;

    const QRcode *code = asQRcode(code_);
    const int size = code->width;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool dark = code->data[y * size + x] & 1;
            if (!dark) // light module -> white; dark module -> leave the
                       // already-cleared black background as-is
                panel->SetPixel(originX + x, originY + y, 255, 255, 255);
        }
    }
}

void renderStartupSplash(PanelView *panel, const QrSplash &qr, const rgb_matrix::Font &font)
{
    if (!qr.isValid())
        return;

    const int qrOriginX = kQuietZoneModules;
    const int qrOriginY = (panel->height() - qr.size()) / 2;
    qr.render(panel, qrOriginX, qrOriginY);

    // Fixed two-line wordmark to the right, vertically centred - the URL is
    // already spelled out by the QR code itself, so repeating it as text
    // would just be redundant.
    static const QVector<QString> kLines = {QStringLiteral("hub75"), QStringLiteral("stock")};
    const int textX = qrOriginX + qr.size() + kQuietZoneModules;
    constexpr int kLineHeight = 8;
    const int blockHeight = kLines.size() * kLineHeight;
    const int topY = (panel->height() - blockHeight) / 2;

    for (int line = 0; line < kLines.size(); ++line) {
        const int baseline = topY + line * kLineHeight + font.baseline();
        rgb_matrix::DrawText(panel, font, textX, baseline, kWhite, nullptr,
                             kLines.at(line).toUtf8().constData(), 0);
    }
}

} // namespace hub75
