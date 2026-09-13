#ifndef HUB75_MATRIXWALL_H
#define HUB75_MATRIXWALL_H

#include "config/appconfig.h"
#include "panelview.h"

#include "led-matrix.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <memory>

namespace hub75 {

class MemoryCanvas;

// Owns the LED hardware (or an offscreen buffer) and exposes the wall as a set
// of independently addressable 64x32 panels.
//
// Panels are addressed 1-based as (row, column), where row is the electrical
// chain and column the position within that chain - matching how the wiring is
// declared in matrix.chains. Chains may have different lengths; the library
// still needs a rectangular canvas, so it is sized to the longest chain and
// the unused tail of the shorter chains simply stays dark.
class MatrixWall
{
public:
    // The library's option structs plus the storage for the strings they
    // borrow. Built from the JSON config, then optionally overridden by
    // --led-* command line flags.
    class Options
    {
    public:
        explicit Options(const MatrixConfig &config);

        // Lets --led-* flags override the values from the config file. This is
        // the escape hatch for the panel quirks that have no JSON key yet
        // (--led-panel-type, --led-multiplexing, --led-row-addr-type, ...).
        // Consumed flags are removed from argv.
        bool applyCommandLineFlags(int *argc, char ***argv, QString *error);

        bool validate(QString *error) const;

        // Copies the effective values back into the configuration, so that
        // the rest of the program - and --show-config - reports what is
        // actually in use rather than what the file said.
        //
        // The panel geometry is owned by matrix.chains; a flag that
        // contradicts it (--led-chain, --led-parallel, --led-rows,
        // --led-cols) would leave panel addressing pointing at the wrong
        // pixels, so that is reported as an error instead of being applied.
        bool writeBack(MatrixConfig *config, QString *error) const;

        rgb_matrix::RGBMatrix::Options led;
        rgb_matrix::RuntimeOptions runtime;

    private:
        QByteArray hardwareMapping_;
        QByteArray ledRgbSequence_;
    };

    ~MatrixWall();

    MatrixWall(const MatrixWall &) = delete;
    MatrixWall &operator=(const MatrixWall &) = delete;

    // Opens the real panels. Requires root (or the appropriate capabilities)
    // on a Raspberry Pi; returns nullptr and fills *error otherwise.
    static std::unique_ptr<MatrixWall> createHardware(const MatrixConfig &config,
                                                      const Options &options,
                                                      QString *error);

    // Renders into an in-memory buffer instead of real hardware, for host-side
    // development. Never fails.
    static std::unique_ptr<MatrixWall> createOffscreen(const MatrixConfig &config);

    bool isHardware() const { return matrix_ != nullptr; }
    const MatrixConfig &config() const { return config_; }

    // 1-based; nullptr when the wiring has no panel at that position.
    PanelView *panel(int row, int column);

    // All panels declared by the wiring, in chain order.
    const QVector<PanelView *> &panels() const { return panelOrder_; }

    // Blanks the whole back buffer, including the dark tail of short chains.
    void clear();

    // Publishes the back buffer. On hardware this waits for vsync and swaps;
    // the freshly acquired buffer holds a two-frames-old image, so callers are
    // expected to redraw fully each frame (clear() then draw).
    void present();

    // Offscreen only: ANSI preview of the current buffer. Empty on hardware.
    QString toAnsiPreview() const;

private:
    explicit MatrixWall(const MatrixConfig &config);
    void buildPanels(rgb_matrix::Canvas *target);
    void retargetPanels(rgb_matrix::Canvas *target);
    int panelIndex(int row, int column) const;

    MatrixConfig config_;
    rgb_matrix::RGBMatrix *matrix_ = nullptr;       // owning
    rgb_matrix::FrameCanvas *backBuffer_ = nullptr; // owned by matrix_
    std::unique_ptr<MemoryCanvas> offscreen_;
    QVector<PanelView> panelStorage_;
    QVector<PanelView *> panelOrder_;
};

} // namespace hub75

#endif // HUB75_MATRIXWALL_H
