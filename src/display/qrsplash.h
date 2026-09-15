// SPDX-License-Identifier: GPL-2.0-only

#ifndef HUB75_QRSPLASH_H
#define HUB75_QRSPLASH_H

#include <QString>

namespace rgb_matrix {
class Font;
}

namespace hub75 {

class PanelView;

// A QR code encoding a URL, meant to fill one 64x32 panel for the first few
// seconds after startup so a phone can scan its way straight to the web
// config form instead of the IP:port having to be typed in by hand.
//
// Wraps libqrencode rather than hand-rolling QR encoding - Reed-Solomon ECC
// and module placement are exactly the kind of format-correctness code
// where a subtle bug just silently produces something that fails to scan,
// not worth reinventing for a nice-to-have splash screen.
class QrSplash
{
public:
    // Encodes "url" at the smallest QR version that fits, then checks the
    // result (plus a small quiet-zone margin on all sides) still fits
    // within a panelSize x panelSize square. Returns an invalid
    // (isValid() == false) instance otherwise.
    static QrSplash forUrl(const QString &url, int panelSize);

    QrSplash();
    QrSplash(QrSplash &&other) noexcept;
    QrSplash &operator=(QrSplash &&other) noexcept;
    ~QrSplash();
    QrSplash(const QrSplash &) = delete;
    QrSplash &operator=(const QrSplash &) = delete;

    bool isValid() const { return code_ != nullptr; }
    // Side length in modules/pixels (one QR module per pixel); 0 if invalid.
    int size() const;

private:
    friend void renderStartupSplash(PanelView *panel, const QrSplash &qr,
                                    const rgb_matrix::Font &font);

    // Opaque rather than "QRcode *": libqrencode's QRcode is a typedef'd
    // anonymous struct (`typedef struct { ... } QRcode;`), which - unlike a
    // tagged "struct QRcode { ... };" - cannot be forward-declared, so a
    // real pointer to it can't appear in this header without pulling in
    // <qrencode.h> here too. void* keeps that include confined to the .cpp.
    explicit QrSplash(void *code);
    void render(PanelView *panel, int originX, int originY) const;

    void *code_ = nullptr;
};

// Draws "qr" flush-left (vertically centred) on "panel", with the app's
// "hub75" / "stock" wordmark stacked to its right - the URL is already
// spelled out by the QR code itself, so repeating it as text next to it
// would just be redundant. A no-op if qr.isValid() is false.
void renderStartupSplash(PanelView *panel, const QrSplash &qr, const rgb_matrix::Font &font);

} // namespace hub75

#endif // HUB75_QRSPLASH_H
