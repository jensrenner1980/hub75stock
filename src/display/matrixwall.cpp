#include "matrixwall.h"

#include "memorycanvas.h"

namespace hub75 {

MatrixWall::Options::Options(const MatrixConfig &config)
    : hardwareMapping_(config.hardwareMapping.toUtf8())
    , ledRgbSequence_(config.ledRgbSequence.toUtf8())
{
    // RGBMatrix::Options borrows these pointers and the matrix keeps a copy of
    // the options, so the backing QByteArrays live in this object.
    led.hardware_mapping = hardwareMapping_.constData();
    led.led_rgb_sequence = ledRgbSequence_.constData();

    led.rows = limits::kPanelHeight;
    led.cols = limits::kPanelWidth;
    // A rectangular canvas is unavoidable: size it to the longest chain.
    led.chain_length = config.maxChainLength();
    led.parallel = config.chainCount();
    led.brightness = config.brightness;
    led.pwm_bits = config.pwmBits;
    led.pwm_lsb_nanoseconds = config.pwmLsbNanoseconds;
    led.limit_refresh_rate_hz = config.limitRefreshHz;
    led.show_refresh_rate = config.showRefreshRate;

    runtime.gpio_slowdown = config.gpioSlowdown;
}

bool MatrixWall::Options::applyCommandLineFlags(int *argc, char ***argv, QString *error)
{
    if (!rgb_matrix::ParseOptionsFromFlags(argc, argv, &led, &runtime,
                                           /*remove_consumed_flags=*/true)) {
        *error = QStringLiteral("could not parse the --led-* options "
                                "(run with --led-help for the list)");
        return false;
    }
    return true;
}

bool MatrixWall::Options::validate(QString *error) const
{
    std::string message;
    if (!led.Validate(&message)) {
        *error = QStringLiteral("invalid matrix options: %1")
                     .arg(QString::fromStdString(message));
        return false;
    }
    return true;
}

bool MatrixWall::Options::writeBack(MatrixConfig *config, QString *error) const
{
    // Geometry is derived from matrix.chains and must not be contradicted.
    struct GeometryCheck {
        const char *flag;
        int fromFlags;
        int fromConfig;
    };
    const GeometryCheck checks[] = {
        { "--led-rows",     led.rows,         limits::kPanelHeight },
        { "--led-cols",     led.cols,         limits::kPanelWidth },
        { "--led-chain",    led.chain_length, config->maxChainLength() },
        { "--led-parallel", led.parallel,     config->chainCount() },
    };
    for (const GeometryCheck &check : checks) {
        if (check.fromFlags != check.fromConfig) {
            *error = QStringLiteral("%1=%2 contradicts the wiring in matrix.chains, "
                                    "which implies %3. Change matrix.chains in the "
                                    "configuration file instead - panel addressing is "
                                    "derived from it.")
                         .arg(QLatin1String(check.flag))
                         .arg(check.fromFlags)
                         .arg(check.fromConfig);
            return false;
        }
    }

    if (led.hardware_mapping)
        config->hardwareMapping = QString::fromUtf8(led.hardware_mapping);
    if (led.led_rgb_sequence)
        config->ledRgbSequence = QString::fromUtf8(led.led_rgb_sequence);
    config->brightness = led.brightness;
    config->pwmBits = led.pwm_bits;
    config->pwmLsbNanoseconds = led.pwm_lsb_nanoseconds;
    config->limitRefreshHz = led.limit_refresh_rate_hz;
    config->showRefreshRate = led.show_refresh_rate;
    config->gpioSlowdown = runtime.gpio_slowdown;
    return true;
}

MatrixWall::MatrixWall(const MatrixConfig &config)
    : config_(config)
{
}

MatrixWall::~MatrixWall()
{
    if (matrix_) {
        matrix_->Clear();
        delete matrix_; // also releases the frame canvases it handed out
        matrix_ = nullptr;
        backBuffer_ = nullptr;
    }
}

std::unique_ptr<MatrixWall> MatrixWall::createHardware(const MatrixConfig &config,
                                                       const Options &options,
                                                       QString *error)
{
    if (!options.validate(error))
        return nullptr;

    rgb_matrix::RGBMatrix *matrix =
        rgb_matrix::RGBMatrix::CreateFromOptions(options.led, options.runtime);
    if (!matrix) {
        *error = QStringLiteral("could not initialise the LED matrix - this needs to run "
                                "on a Raspberry Pi with root privileges "
                                "(the library's diagnostics are on stderr above)");
        return nullptr;
    }

    std::unique_ptr<MatrixWall> wall(new MatrixWall(config));
    wall->matrix_ = matrix;
    wall->backBuffer_ = matrix->CreateFrameCanvas();
    if (!wall->backBuffer_) {
        *error = QStringLiteral("could not allocate an offscreen frame canvas");
        return nullptr;
    }
    wall->buildPanels(wall->backBuffer_);
    return wall;
}

std::unique_ptr<MatrixWall> MatrixWall::createOffscreen(const MatrixConfig &config)
{
    std::unique_ptr<MatrixWall> wall(new MatrixWall(config));
    wall->offscreen_ = std::make_unique<MemoryCanvas>(config.pixelWidth(),
                                                      config.pixelHeight());
    wall->buildPanels(wall->offscreen_.get());
    return wall;
}

int MatrixWall::panelIndex(int row, int column) const
{
    return (row - 1) * config_.maxChainLength() + (column - 1);
}

void MatrixWall::buildPanels(rgb_matrix::Canvas *target)
{
    // Sized once and never resized again, so the pointers in panelOrder_ stay
    // valid for the lifetime of the wall.
    panelStorage_.resize(config_.chainCount() * config_.maxChainLength());
    panelOrder_.clear();

    for (int row = 1; row <= config_.chainCount(); ++row) {
        const int panelsInChain = config_.chains.at(row - 1);
        for (int column = 1; column <= panelsInChain; ++column) {
            PanelView &view = panelStorage_[panelIndex(row, column)];
            view = PanelView(target,
                             (column - 1) * limits::kPanelWidth,
                             (row - 1) * limits::kPanelHeight,
                             limits::kPanelWidth, limits::kPanelHeight);
            view.setPosition(row, column);
            panelOrder_.append(&view);
        }
    }
}

void MatrixWall::retargetPanels(rgb_matrix::Canvas *target)
{
    for (PanelView *view : panelOrder_)
        view->setTarget(target);
}

PanelView *MatrixWall::panel(int row, int column)
{
    if (row < 1 || row > config_.chainCount())
        return nullptr;
    if (column < 1 || column > config_.chains.at(row - 1))
        return nullptr;
    return &panelStorage_[panelIndex(row, column)];
}

void MatrixWall::clear()
{
    if (backBuffer_)
        backBuffer_->Clear();
    else if (offscreen_)
        offscreen_->Clear();
}

void MatrixWall::present()
{
    if (!matrix_ || !backBuffer_)
        return;
    backBuffer_ = matrix_->SwapOnVSync(backBuffer_);
    retargetPanels(backBuffer_);
}

QString MatrixWall::toAnsiPreview() const
{
    if (!offscreen_)
        return QString();
    return offscreen_->toAnsi(limits::kPanelWidth, limits::kPanelHeight);
}

} // namespace hub75
