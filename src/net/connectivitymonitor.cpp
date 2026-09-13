#include "connectivitymonitor.h"

#include <QProcess>

namespace hub75 {

ConnectivityMonitor::ConnectivityMonitor(QObject *parent) : QObject(parent)
{
    timer_.setInterval(60'000);
    connect(&timer_, &QTimer::timeout, this, &ConnectivityMonitor::checkNow);
}

void ConnectivityMonitor::start()
{
    checkNow();
    timer_.start();
}

void ConnectivityMonitor::checkNow()
{
    auto *wifiState = new QProcess(this);
    // errorOccurred (e.g. nmcli missing) fires instead of finished, never
    // both - each process needs its own fallback to State::NoConnection so
    // a broken nmcli doesn't just leave the indicator stuck on stale data.
    connect(wifiState, &QProcess::errorOccurred, this, [this, wifiState](QProcess::ProcessError) {
        wifiState->deleteLater();
        setState(State::NoConnection);
    });
    connect(wifiState, &QProcess::finished, this,
            [this, wifiState](int, QProcess::ExitStatus) {
        const QString stateOut = QString::fromUtf8(wifiState->readAllStandardOutput()).trimmed();
        wifiState->deleteLater();

        // NM_DEVICE_STATE_ACTIVATED == 100, the leading numeric code in
        // e.g. "100 (connected)" - matching on the number rather than the
        // parenthesised text, which nmcli may localise.
        const bool wifiUp = stateOut.section(QLatin1Char(' '), 0, 0).toInt() == 100;
        if (!wifiUp) {
            setState(State::NoConnection);
            return;
        }

        auto *connectivity = new QProcess(this);
        connect(connectivity, &QProcess::errorOccurred, this,
                [this, connectivity](QProcess::ProcessError) {
            connectivity->deleteLater();
            setState(State::WifiOnly);
        });
        connect(connectivity, &QProcess::finished, this,
                [this, connectivity](int, QProcess::ExitStatus) {
            const QString connOut =
                QString::fromUtf8(connectivity->readAllStandardOutput()).trimmed();
            connectivity->deleteLater();
            setState(connOut == QStringLiteral("full") ? State::Online : State::WifiOnly);
        });
        connectivity->start(QStringLiteral("nmcli"),
                            {QStringLiteral("-t"), QStringLiteral("-g"),
                             QStringLiteral("CONNECTIVITY"), QStringLiteral("general")});
    });
    wifiState->start(QStringLiteral("nmcli"),
                     {QStringLiteral("-t"), QStringLiteral("-g"), QStringLiteral("GENERAL.STATE"),
                      QStringLiteral("device"), QStringLiteral("show"), QStringLiteral("wlan0")});
}

void ConnectivityMonitor::setState(State next)
{
    if (next == state_)
        return;
    state_ = next;
    emit stateChanged(state_);
}

} // namespace hub75
