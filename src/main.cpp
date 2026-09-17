// SPDX-License-Identifier: GPL-2.0-only

#include "config/appconfig.h"
#include "display/fontstore.h"
#include "display/matrixwall.h"
#include "display/qrsplash.h"
#include "display/stockrenderer.h"
#include "display/testpatterns.h"
#include "net/connectivitymonitor.h"
#include "web/configserver.h"
#include "stocks/mockprovider.h"
#include "stocks/stockdataprovider.h"
#include "stocks/yahoodataprovider.h"

#include "graphics.h"
#include "led-matrix.h"

#include <QAbstractSocket>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QSocketNotifier>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include <csignal>
#include <cstdlib>
#include <vector>

#include <signal.h>
#include <unistd.h>

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

// wlan0's IPv4 address specifically - not just "any non-loopback interface"
// (which on a Pi with the USB gadget/RNDIS port wired up can easily resolve
// to that instead), since that's the interface ConnectivityMonitor itself
// reports on and the one a phone on the same WiFi actually needs. Prefers a
// routable address over a link-local (169.254.0.0/16) one; empty if wlan0
// doesn't exist, is down, or has no IPv4 address yet.
QString wlan0IPv4Address()
{
    const QNetworkInterface iface = QNetworkInterface::interfaceFromName(QStringLiteral("wlan0"));
    if (!iface.isValid() || !iface.flags().testFlag(QNetworkInterface::IsUp))
        return QString();

    QString linkLocalFallback;
    for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
        const QHostAddress address = entry.ip();
        if (address.protocol() != QAbstractSocket::IPv4Protocol)
            continue;
        if (address.isLinkLocal()) {
            if (linkLocalFallback.isEmpty())
                linkLocalFallback = address.toString();
            continue;
        }
        return address.toString();
    }
    return linkLocalFallback;
}

// QTextStream buffers, and an error path usually returns straight away, so
// every diagnostic is flushed as it is written.
void printError(const QString &message)
{
    err() << message << '\n';
    err().flush();
}

// --- Clean shutdown ------------------------------------------------------
//
// The matrix must be torn down properly: its refresh thread keeps the panels
// lit, so being killed mid-frame leaves rows stuck on at full current. A
// self-pipe turns SIGINT/SIGTERM into a normal Qt event, which lets the
// destructors run.
int g_signalPipe[2] = { -1, -1 };

void handleSignal(int)
{
    const char byte = 1;
    // Async-signal-safe; a failed write just means a shutdown is already queued.
    const ssize_t written = ::write(g_signalPipe[1], &byte, 1);
    Q_UNUSED(written);
}

bool installSignalHandler(QString *error)
{
    if (::pipe(g_signalPipe) != 0) {
        *error = QStringLiteral("cannot create the signal pipe");
        return false;
    }

    struct sigaction action {};
    action.sa_handler = handleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    if (::sigaction(SIGINT, &action, nullptr) != 0
        || ::sigaction(SIGTERM, &action, nullptr) != 0) {
        *error = QStringLiteral("cannot install the signal handlers");
        return false;
    }
    return true;
}

// --- Command line --------------------------------------------------------

struct CommandLine {
    QString configPath;
    QString patternName;
    QString fontName;
    int durationSeconds = 0; // 0 = run until interrupted
    bool listPatterns = false;
    bool dryRun = false;
    bool showConfig = false;
    int webConfigPort = 0; // 0 = disabled
    bool useMock = false;
    QString exportDir;
};

QString defaultConfigPath()
{
    const QStringList candidates = {
        // Highest priority: the boot partition, mounted at this fixed path on
        // a running Pi (post-Bookworm; same partition config.txt/cmdline.txt
        // and provisioning/wificonfig.json live on). Lets a config be dropped
        // onto the SD card from any OS via a card reader, no SSH/root shell
        // on the Pi itself needed - the same rationale as the WiFi
        // provisioning file.
        QStringLiteral("/boot/firmware/hub75stock.json"),
        QStringLiteral("config/hub75stock.json"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/hub75stock.json"),
        QStringLiteral("/etc/hub75stock/hub75stock.json"),
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return candidates.first();
}

// A mutable copy of argv. ParseOptionsFromFlags() rewrites the pointer array
// in place and shortens argc, so it must not be handed the array that
// QCoreApplication holds on to.
class ArgvBuffer
{
public:
    ArgvBuffer(int argc, char **argv)
        : storage_(argv, argv + argc)
        , count_(argc)
    {
        storage_.push_back(nullptr);
        data_ = storage_.data();
    }

    int *countPtr() { return &count_; }
    char ***dataPtr() { return &data_; }

    QStringList toStringList() const
    {
        QStringList list;
        list.reserve(count_);
        for (int i = 0; i < count_; ++i)
            list << QString::fromLocal8Bit(data_[i]);
        return list;
    }

private:
    std::vector<char *> storage_;
    int count_ = 0;
    char **data_ = nullptr;
};

void printPatternList()
{
    out() << "Available test patterns:\n";
    for (const hub75::TestPatternInfo &info : hub75::availableTestPatterns())
        out() << QStringLiteral("  %1  %2\n").arg(info.name, -12).arg(info.description);
    out().flush();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("hub75stock"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Stock information on a wall of HUB75 LED matrix panels.\n"
                       "\n"
                       "Matrix settings come from the JSON configuration file; any\n"
                       "--led-* flag of rpi-rgb-led-matrix overrides them (--led-help\n"
                       "lists them all)."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption configOption(
        { QStringLiteral("c"), QStringLiteral("config") },
        QStringLiteral("Configuration file (default: %1).").arg(defaultConfigPath()),
        QStringLiteral("path"));
    const QCommandLineOption patternOption(
        { QStringLiteral("p"), QStringLiteral("pattern") },
        QStringLiteral("Render a hardware test pattern instead of the real stock display. "
                       "If omitted, runs the actual stock display for the configured "
                       "panels, backed by live Yahoo Finance data unless --mock is given."),
        QStringLiteral("name"), QStringLiteral("identify"));
    const QCommandLineOption listPatternsOption(
        QStringLiteral("list-patterns"),
        QStringLiteral("List the available test patterns and exit."));
    const QCommandLineOption durationOption(
        QStringLiteral("duration"),
        QStringLiteral("Stop after this many seconds (default: run until interrupted)."),
        QStringLiteral("seconds"), QStringLiteral("0"));
    const QCommandLineOption fontOption(
        QStringLiteral("font"),
        QStringLiteral("Embedded font to use (5x8, 4x6, tom-thumb)."),
        QStringLiteral("name"), QStringLiteral("5x8"));
    const QCommandLineOption dryRunOption(
        QStringLiteral("dry-run"),
        QStringLiteral("Do not touch the hardware: validate the configuration and draw "
                       "into an in-memory canvas, previewed on the terminal. Works on "
                       "the development host."));
    const QCommandLineOption showConfigOption(
        QStringLiteral("show-config"),
        QStringLiteral("Print the resolved configuration and exit."));
    const QCommandLineOption webConfigPortOption(
        QStringLiteral("web-config-port"),
        QStringLiteral("Serve an unauthenticated web form for editing the config file's "
                       "day-to-day settings, on this TCP port (all interfaces). Disabled "
                       "unless given. Saved changes need an app restart to take effect."),
        QStringLiteral("port"));
    const QCommandLineOption mockOption(
        QStringLiteral("mock"),
        QStringLiteral("Use synthetic data instead of fetching real prices from Yahoo "
                       "Finance - for development/testing without a network, or just "
                       "exercising the display. Ignored with --pattern."));
    const QCommandLineOption exportDirOption(
        QStringLiteral("export-dir"),
        QStringLiteral("Directory the web config form's \"Export current view as PNG\" "
                       "button saves into. Created if it doesn't exist."),
        QStringLiteral("path"), QStringLiteral("/tmp/hub75stock-exports"));

    parser.addOption(configOption);
    parser.addOption(patternOption);
    parser.addOption(listPatternsOption);
    parser.addOption(durationOption);
    parser.addOption(fontOption);
    parser.addOption(dryRunOption);
    parser.addOption(showConfigOption);
    parser.addOption(webConfigPortOption);
    parser.addOption(exportDirOption);
    parser.addOption(mockOption);

    // The command line carries two families of options: ours, and the --led-*
    // flags of rpi-rgb-led-matrix. The library accepts both "--led-rows=32"
    // and "--led-rows 32", so a value may live in a separate argv entry and
    // only the library itself can split the two groups reliably.
    //
    // First pass: strip the --led-* flags into throwaway option structs, only
    // to find out which arguments are ours. They are parsed again further
    // down, on top of the values from the configuration file, so that a flag
    // always wins over the file.
    ArgvBuffer probeArgs(argc, argv);
    if (!rgb_matrix::ParseOptionsFromFlags(probeArgs.countPtr(), probeArgs.dataPtr(),
                                           nullptr, nullptr,
                                           /*remove_consumed_flags=*/true)) {
        // This is also the path taken by --led-help, which asks for exactly this.
        rgb_matrix::PrintMatrixFlags(stderr);
        return EXIT_FAILURE;
    }

    // Whatever is left belongs to us, so it can be parsed strictly. process()
    // handles --help/--version and exits on an unknown option.
    parser.process(probeArgs.toStringList());

    CommandLine cli;
    cli.configPath = parser.isSet(configOption) ? parser.value(configOption)
                                                : defaultConfigPath();
    cli.patternName = parser.value(patternOption);
    const bool useTestPattern = parser.isSet(patternOption);
    cli.fontName = parser.value(fontOption);
    cli.listPatterns = parser.isSet(listPatternsOption);
    cli.dryRun = parser.isSet(dryRunOption);
    cli.showConfig = parser.isSet(showConfigOption);
    cli.useMock = parser.isSet(mockOption);
    cli.exportDir = parser.value(exportDirOption);

    bool durationOk = false;
    cli.durationSeconds = parser.value(durationOption).toInt(&durationOk);
    if (!durationOk || cli.durationSeconds < 0) {
        printError(QStringLiteral("--duration expects a non-negative number of seconds"));
        return EXIT_FAILURE;
    }

    if (parser.isSet(webConfigPortOption)) {
        bool portOk = false;
        cli.webConfigPort = parser.value(webConfigPortOption).toInt(&portOk);
        if (!portOk || cli.webConfigPort <= 0 || cli.webConfigPort > 65535) {
            printError(QStringLiteral("--web-config-port expects a port number (1-65535)"));
            return EXIT_FAILURE;
        }
    }

    if (cli.listPatterns) {
        printPatternList();
        return EXIT_SUCCESS;
    }

    hub75::TestPattern pattern = hub75::TestPattern::Identify;
    if (!hub75::testPatternFromName(cli.patternName, &pattern)) {
        printError(QStringLiteral("Unknown test pattern \"%1\".").arg(cli.patternName));
        printPatternList();
        return EXIT_FAILURE;
    }

    // --- Configuration ---------------------------------------------------
    hub75::AppConfig config;
    QString error;
    if (!hub75::AppConfig::load(cli.configPath, &config, &error)) {
        printError(QStringLiteral("Configuration error: %1").arg(error));
        return EXIT_FAILURE;
    }

    // Second pass: the config values are the base, --led-* flags override them.
    hub75::MatrixWall::Options matrixOptions(config.matrix());
    ArgvBuffer ledArgs(argc, argv);
    if (!matrixOptions.applyCommandLineFlags(ledArgs.countPtr(), ledArgs.dataPtr(),
                                             &error)) {
        printError(error);
        return EXIT_FAILURE;
    }
    // Fold the overrides back in, so everything downstream sees one truth.
    if (!matrixOptions.writeBack(&config.matrixRef(), &error)) {
        printError(error);
        return EXIT_FAILURE;
    }

    if (cli.showConfig) {
        out() << QStringLiteral("Configuration file: %1\n\n").arg(cli.configPath);
        out() << config.describe() << "\n";
        out().flush();
        return EXIT_SUCCESS;
    }

    // --- Hardware --------------------------------------------------------
    std::unique_ptr<hub75::MatrixWall> wall;
    if (cli.dryRun) {
        wall = hub75::MatrixWall::createOffscreen(config.matrix());
    } else {
        wall = hub75::MatrixWall::createHardware(config.matrix(), matrixOptions, &error);
        if (!wall) {
            printError(QStringLiteral("%1\n"
                                      "Use --dry-run to exercise the layout without hardware.")
                           .arg(error));
            return EXIT_FAILURE;
        }
    }

    hub75::FontStore fonts;
    const rgb_matrix::Font *font = fonts.font(cli.fontName, &error);
    if (!font) {
        printError(QStringLiteral("Font error: %1").arg(error));
        return EXIT_FAILURE;
    }

    // Stock data - only touched in real-display mode. Seeded with every
    // symbol across every configured panel, regardless of mode, so a panel
    // whose data hasn't updated in a while still has something to show.
    // Real (Yahoo Finance) data by default; --mock swaps in the synthetic
    // provider instead, e.g. for development without a network.
    std::unique_ptr<hub75::StockDataProvider> stockData;
    if (cli.useMock)
        stockData = std::make_unique<hub75::MockDataProvider>();
    else
        stockData = std::make_unique<hub75::YahooDataProvider>();
    if (!useTestPattern) {
        QVector<hub75::Symbol> allSymbols;
        for (const hub75::DisplayConfig &display : config.displays())
            allSymbols += display.symbols;
        stockData->ensureSymbols(allSymbols);
        // Kicks off the first fetch immediately (YahooDataProvider) rather
        // than leaving every panel showing "no data yet" until the first
        // dataTimer tick, which could be up to updateIntervalSeconds away.
        // A harmless extra nudge for MockDataProvider, whose data is
        // already fully seeded by ensureSymbols() above.
        stockData->update();
    }

    const QString modeLabel = useTestPattern
                                   ? QStringLiteral("test pattern \"%1\"").arg(hub75::toString(pattern))
                                   : QStringLiteral("stock display (%1)")
                                         .arg(cli.useMock ? QStringLiteral("mock data")
                                                          : QStringLiteral("live Yahoo Finance data"));

    // Web config server - declared here (rather than down by the "Web
    // config form" block below) so it can be checked below; actually
    // constructed/started later, unchanged from before.
    std::unique_ptr<hub75::ConfigWebServer> webConfig;

    // WiFi/internet status corner indicator (separate from the startup
    // splash below - the indicator wants the fuller Online/WifiOnly/
    // NoConnection distinction, checked periodically for as long as the app
    // runs; the splash below only cares about "is wlan0 up right now").
    // Declared here (so renderFrame can capture it) but only actually
    // started further down, once we know we're past the single-frame
    // --dry-run preview - no point spawning nmcli subprocesses for a print
    // that exits before the event loop ever runs.
    hub75::ConnectivityMonitor connectivityMonitor;
    hub75::ConnectivityMonitor::State connectivityState = hub75::ConnectivityMonitor::State::NoConnection;
    const bool showConnectivityIndicator =
        !useTestPattern && config.global().showConnectivityIndicator;
    const bool needsConnectivityMonitor = !useTestPattern;

    if (needsConnectivityMonitor) {
        QObject::connect(&connectivityMonitor, &hub75::ConnectivityMonitor::stateChanged, &app,
                         [&connectivityState](hub75::ConnectivityMonitor::State state) {
            connectivityState = state;
        });
    }

    // Startup QR splash for the web config form - a fixed "hub75"/"stock"
    // label rather than the URL (which is right there in the QR code
    // itself - showing it twice was redundant). Decided synchronously,
    // right here at startup (see the "Web config form" block below, which
    // runs before the render loop's own clock starts) rather than waiting
    // on the async ConnectivityMonitor - that briefly showed normal stock
    // content before the splash popped in once connectivity resolved, which
    // looked broken. A plain, instant QNetworkInterface read of wlan0's
    // current IP has no such delay: either it already has one at this exact
    // moment, or the splash is simply skipped for this run - "static" and
    // immediate, not something that can pop up mid-runtime.
    hub75::QrSplash qrSplash;
    constexpr qint64 kSplashDurationMs = 5000;
    const rgb_matrix::Font *splashFont = fonts.font(QStringLiteral("4x6"), &error);

    // One frame, either the startup splash, a test pattern, or the real
    // per-panel stock content.
    const auto renderFrame = [&](qint64 elapsedMs) {
        if (qrSplash.isValid() && splashFont && elapsedMs < kSplashDurationMs) {
            for (hub75::PanelView *panel : wall->panels()) {
                if (panel->row() == 1 && panel->column() == 1)
                    hub75::renderStartupSplash(panel, qrSplash, *splashFont);
            }
            return;
        }
        if (useTestPattern) {
            hub75::renderTestPattern(pattern, wall.get(), config, *font, fonts, elapsedMs);
            return;
        }
        for (hub75::PanelView *panel : wall->panels()) {
            const hub75::DisplayConfig *display =
                config.displayAt(panel->row(), panel->column());
            if (display)
                hub75::renderStockPanel(panel, *display, config, *stockData, fonts, elapsedMs);
            if (showConnectivityIndicator && panel->row() == 1 && panel->column() == 1)
                hub75::renderConnectivityIndicator(panel, connectivityState);
        }
    };

    // Prints the offscreen buffer, for --dry-run.
    const auto printPreview = [&]() {
        out() << QStringLiteral("%1, %2x%3 canvas "
                               "(| and - mark the panel seams):\n\n")
                     .arg(modeLabel)
                     .arg(config.matrix().pixelWidth())
                     .arg(config.matrix().pixelHeight());
        out() << wall->toAnsiPreview() << "\n";
        out().flush();
    };

    // --- Dry run without a duration: a single frame, then out -------------
    if (cli.dryRun && cli.durationSeconds == 0) {
        wall->clear();
        renderFrame(0);
        out() << QStringLiteral("Configuration file: %1\n\n").arg(cli.configPath);
        out() << config.describe() << "\n\n";
        printPreview();
        return EXIT_SUCCESS;
    }

    if (needsConnectivityMonitor)
        connectivityMonitor.start();

    // --- Web config form (optional) ---------------------------------------
    // Not fatal if it can't bind - the display is the app's actual job, the
    // web form is a convenience on top of it.
    if (cli.webConfigPort != 0) {
        webConfig = std::make_unique<hub75::ConfigWebServer>(cli.configPath, wall->panels(),
                                                             &fonts, cli.exportDir);
        if (!webConfig->start(static_cast<quint16>(cli.webConfigPort), &error)) {
            printError(QStringLiteral("Web config server: %1").arg(error));
            webConfig.reset();
        } else {
            const QString address = wlan0IPv4Address();
            if (address.isEmpty()) {
                out() << QStringLiteral("Web config form: http://<this-device>:%1/\n")
                             .arg(cli.webConfigPort);
            } else {
                const QString url =
                    QStringLiteral("http://%1:%2/").arg(address).arg(cli.webConfigPort);
                out() << QStringLiteral("Web config form: %1\n").arg(url);
                qrSplash = hub75::QrSplash::forUrl(url, hub75::limits::kPanelHeight);
            }
        }
    }

    // --- Render loop -----------------------------------------------------
    if (!installSignalHandler(&error)) {
        printError(error);
        return EXIT_FAILURE;
    }
    QSocketNotifier signalNotifier(g_signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, &app, [&]() {
        signalNotifier.setEnabled(false);
        out() << "\nShutting down, blanking the panels...\n";
        out().flush();
        QCoreApplication::quit();
    });

    out() << QStringLiteral("hub75stock: %1 panel(s) on %2 chain(s), %3 "
                            "at %4 fps%5.\n")
                 .arg(config.matrix().panelCount())
                 .arg(config.matrix().chainCount())
                 .arg(modeLabel)
                 .arg(config.global().renderFps)
                 .arg(wall->isHardware() ? QString()
                                         : QStringLiteral(" (dry run, no hardware)"));
    out() << "Press Ctrl+C to stop.\n";
    out().flush();

    QElapsedTimer clock;
    clock.start();

    qint64 frameCount = 0;
    QTimer renderTimer;
    renderTimer.setTimerType(Qt::PreciseTimer);
    renderTimer.setInterval(1000 / qBound(1, config.global().renderFps, 120));
    QObject::connect(&renderTimer, &QTimer::timeout, &app, [&]() {
        // The buffer handed back by SwapOnVSync() holds an older frame, so
        // every frame is drawn from scratch.
        wall->clear();
        renderFrame(clock.elapsed());
        wall->present();
        ++frameCount;
    });
    renderTimer.start();

    // Refreshes stock data on the configured update cadence (nudges the
    // mock's random walk, or kicks off a new round of Yahoo Finance
    // fetches). Not needed in test-pattern mode, and harmless to skip since
    // QTimer only fires while started.
    QTimer dataTimer;
    if (!useTestPattern) {
        dataTimer.setInterval(qBound(60, config.global().updateIntervalSeconds, 900) * 1000);
        QObject::connect(&dataTimer, &QTimer::timeout, &app, [&]() { stockData->update(); });
        dataTimer.start();
    }

    if (cli.durationSeconds > 0)
        QTimer::singleShot(cli.durationSeconds * 1000, &app, &QCoreApplication::quit);

    const int result = app.exec();
    renderTimer.stop();

    if (cli.dryRun) {
        out() << QStringLiteral("\nRendered %1 frame(s) in %2 ms.\n")
                     .arg(frameCount).arg(clock.elapsed());
        printPreview();
    }

    // The MatrixWall destructor blanks the panels and stops the refresh thread.
    wall.reset();
    return result;
}
