#ifndef HUB75_CONNECTIVITYMONITOR_H
#define HUB75_CONNECTIVITYMONITOR_H

#include <QObject>
#include <QTimer>

namespace hub75 {

// Polls NetworkManager (via nmcli) for wlan0's link state and overall
// internet reachability, entirely asynchronously so a slow or hung nmcli/
// D-Bus round trip never stalls the render loop.
//
// Deliberately nmcli-based rather than a raw connectivity probe of our own:
// NetworkManager already runs its own periodic reachability check (handles
// captive portals etc.), and the rest of this project already leans on
// nmcli for WiFi management (see provisioning/hub75stock-wifi-setup.sh) -
// this reuses that same mechanism instead of adding a second, parallel way
// of answering "are we online".
class ConnectivityMonitor : public QObject
{
    Q_OBJECT
public:
    enum class State { NoConnection, WifiOnly, Online };

    explicit ConnectivityMonitor(QObject *parent = nullptr);

    State state() const { return state_; }

    // Runs an immediate check, then re-checks every 60s.
    void start();

signals:
    void stateChanged(hub75::ConnectivityMonitor::State state);

private:
    void checkNow();
    void setState(State next);

    QTimer timer_;
    State state_ = State::NoConnection;
};

} // namespace hub75

#endif // HUB75_CONNECTIVITYMONITOR_H
