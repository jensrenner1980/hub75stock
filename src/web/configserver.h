#ifndef HUB75_CONFIGSERVER_H
#define HUB75_CONFIGSERVER_H

#include <QHttpServer>
#include <QObject>
#include <QString>
#include <QTcpServer>

#include <memory>

namespace hub75 {

// A tiny, unauthenticated web form for editing the day-to-day settings in
// the JSON config file - deliberately not the matrix/GPIO/hardware-wiring
// settings, which stay hand-edit-only since they're riskier to fat-finger
// remotely and rarely need changing at runtime.
//
// No authentication by design: reachable only from the same WiFi network is
// treated as a sufficient trust boundary for a personal desk device. Saved
// changes take effect on the next restart of the main application, not
// live - this reads and writes the config file directly and has no
// connection to a possibly-running hub75stock process.
class ConfigWebServer
{
public:
    explicit ConfigWebServer(QString configPath);

    // Starts listening on the given port (all interfaces). Returns false and
    // fills *error if the port can't be bound.
    bool start(quint16 port, QString *error);

private:
    QString renderForm(const QString *statusMessage) const;
    QHttpServerResponse handleSave(const QHttpServerRequest &request);

    QString configPath_;
    QHttpServer server_;
    std::unique_ptr<QTcpServer> tcpServer_;
};

} // namespace hub75

#endif // HUB75_CONFIGSERVER_H
