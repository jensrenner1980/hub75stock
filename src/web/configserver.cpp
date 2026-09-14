#include "configserver.h"

#include "config/appconfig.h"

#include <QFile>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrlQuery>
#include <QVector>

#include <unistd.h>

namespace hub75 {
namespace {

QString htmlEscape(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    escaped.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    escaped.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return escaped;
}

QString optionTag(const QString &value, const QString &label, const QString &current)
{
    return QStringLiteral("<option value=\"%1\"%2>%3</option>")
               .arg(value, value == current ? QStringLiteral(" selected") : QString(), label);
}

const char *kPageStyle =
    "body{font-family:sans-serif;max-width:640px;margin:2em auto;padding:0 1em;"
    "background:#1b1b1b;color:#e8e8e8}"
    "h1{font-size:1.3em}h2{font-size:1.05em;border-bottom:1px solid #444;padding-bottom:.2em}"
    "fieldset{border:1px solid #444;border-radius:6px;margin:1em 0}"
    "label{display:block;margin:.6em 0}"
    "input,select{padding:.3em;background:#2a2a2a;color:#e8e8e8;border:1px solid #555;"
    "border-radius:4px}"
    "input[type=number]{width:6em}input[type=text]{width:100%;box-sizing:border-box}"
    "button{padding:.5em 1.5em;font-size:1em;margin-top:1em;cursor:pointer}"
    ".note{color:#999;font-size:.85em}"
    ".error{color:#ff6b6b;border:1px solid #ff6b6b;border-radius:6px;padding:.8em;margin:1em 0}"
    ".ok{color:#6bff8f;border:1px solid #6bff8f;border-radius:6px;padding:.8em;margin:1em 0}";

} // namespace

ConfigWebServer::ConfigWebServer(QString configPath)
    : configPath_(std::move(configPath))
{
    server_.route(QStringLiteral("/"), QHttpServerRequest::Method::Get,
                  [this]() { return QHttpServerResponse("text/html; charset=utf-8",
                                                        renderForm(nullptr).toUtf8()); });

    server_.route(QStringLiteral("/save"), QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &request) { return handleSave(request); });
}

bool ConfigWebServer::start(quint16 port, QString *error)
{
    tcpServer_ = std::make_unique<QTcpServer>();
    if (!tcpServer_->listen(QHostAddress::Any, port)) {
        *error = QStringLiteral("could not listen on port %1: %2")
                     .arg(port).arg(tcpServer_->errorString());
        tcpServer_.reset();
        return false;
    }
    if (!server_.bind(tcpServer_.get())) {
        *error = QStringLiteral("could not bind the HTTP server to the listening socket");
        tcpServer_.reset();
        return false;
    }
    // QHttpServer::bind() takes ownership.
    tcpServer_.release();
    return true;
}

QString ConfigWebServer::renderForm(const QString *statusMessage) const
{
    AppConfig config;
    QString loadError;
    if (!AppConfig::load(configPath_, &config, &loadError)) {
        return QStringLiteral("<!doctype html><html><body><div class=\"error\">"
                              "Could not load %1: %2</div></body></html>")
                   .arg(htmlEscape(configPath_), htmlEscape(loadError));
    }

    QString html;
    html += QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\">"
                          "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                          "<title>hub75stock config</title><style>%1</style></head><body>")
                .arg(QString::fromLatin1(kPageStyle));
    html += QStringLiteral("<h1>hub75stock configuration</h1>");
    html += QStringLiteral("<p class=\"note\">Editing %1. Changes are saved immediately but only "
                          "take effect after the main application is restarted.</p>")
                .arg(htmlEscape(configPath_));

    if (statusMessage)
        html += *statusMessage;

    html += QStringLiteral("<form method=\"post\" action=\"/save\">");

    html += QStringLiteral("<h2>Display</h2>");
    html += QStringLiteral(
        "<label>Brightness (1-100 %)<br><input type=\"number\" name=\"brightness\" "
        "min=\"1\" max=\"100\" value=\"%1\"></label>")
                .arg(config.matrix().brightness);
    html += QStringLiteral(
        "<label><input type=\"checkbox\" name=\"autoDim\"%1> Auto-dim at night</label>"
        "<p class=\"note\">Placeholder for a future ambient-light sensor - has no effect "
        "until that's wired up.</p>")
                .arg(config.matrix().autoDim ? QStringLiteral(" checked") : QString());

    html += QStringLiteral("<h2>Timing</h2>");
    html += QStringLiteral(
        "<label>Data update interval (1-60 s)<br><input type=\"number\" "
        "name=\"updateIntervalSeconds\" min=\"1\" max=\"60\" value=\"%1\"></label>")
                .arg(config.global().updateIntervalSeconds);
    html += QStringLiteral(
        "<label>Rotation interval (1-3600 s)<br><input type=\"number\" "
        "name=\"rotationSeconds\" min=\"1\" max=\"3600\" value=\"%1\"></label>")
                .arg(config.global().rotationSeconds);
    html += QStringLiteral(
        "<label>Render rate (1-120 fps)<br><input type=\"number\" "
        "name=\"renderFps\" min=\"1\" max=\"120\" value=\"%1\"></label>")
                .arg(config.global().renderFps);
    html += QStringLiteral("<label>When the market is closed<br><select name=\"marketClosed\">%1</select></label>")
                .arg(optionTag(QStringLiteral("grey"), QStringLiteral("grey"),
                              toString(config.global().closedMarketStyle))
                     + optionTag(QStringLiteral("normal"), QStringLiteral("normal"),
                               toString(config.global().closedMarketStyle))
                     + optionTag(QStringLiteral("blank"), QStringLiteral("blank"),
                               toString(config.global().closedMarketStyle)));
    html += QStringLiteral(
        "<label><input type=\"checkbox\" name=\"showConnectivityIndicator\"%1> "
        "Show WiFi/internet status indicator</label>"
        "<p class=\"note\">A small corner overlay on the first panel - see the "
        "README's \"Connectivity status indicator\" section.</p>")
                .arg(config.global().showConnectivityIndicator ? QStringLiteral(" checked")
                                                               : QString());

    html += QStringLiteral("<h2>Panels</h2>");
    html += QStringLiteral(
        "<p class=\"note\">Every physically wired position (from matrix.chains - "
        "hand-edit-only, see the README) is listed, whether or not it currently "
        "shows anything. Leaving Symbols blank removes/skips that panel; typing "
        "one adds it.</p>");
    for (int row = 1; row <= config.matrix().chainCount(); ++row) {
        for (int column = 1; column <= config.matrix().chains.at(row - 1); ++column) {
            const DisplayConfig *display = config.displayAt(row, column);
            const DisplayMode mode = display ? display->mode : DisplayMode::StockList;
            QStringList symbolStrings;
            if (display) {
                for (const Symbol &symbol : display->symbols)
                    symbolStrings << symbol.toConfigString();
            }

            html += QStringLiteral("<fieldset><legend>R%1C%2%3</legend>")
                        .arg(row).arg(column)
                        .arg(display ? QString() : QStringLiteral(" (not configured)"));
            html += QStringLiteral(
                "<label>Mode<br><select name=\"display_%1_%2_mode\">%3</select></label>")
                        .arg(row).arg(column).arg(
                            optionTag(QStringLiteral("list"), QStringLiteral("list"), toString(mode))
                            + optionTag(QStringLiteral("line"), QStringLiteral("line"), toString(mode))
                            + optionTag(QStringLiteral("area"), QStringLiteral("area"), toString(mode))
                            + optionTag(QStringLiteral("candles"), QStringLiteral("candles"), toString(mode)));
            html += QStringLiteral(
                "<label>Symbols, comma-separated, up to %1<br>"
                "<input type=\"text\" name=\"display_%2_%3_symbols\" value=\"%4\"></label>")
                        .arg(limits::kMaxSymbolsPerDisplay).arg(row).arg(column)
                        .arg(htmlEscape(symbolStrings.join(QStringLiteral(", "))));
            html += QStringLiteral("</fieldset>");
        }
    }

    html += QStringLiteral("<button type=\"submit\">Save</button></form></body></html>");
    return html;
}

QHttpServerResponse ConfigWebServer::handleSave(const QHttpServerRequest &request)
{
    // application/x-www-form-urlencoded represents space as '+', a
    // convention specific to form bodies - QUrlQuery follows generic RFC
    // 3986 query-string rules and does not decode '+' as space on its own,
    // so it has to be turned into '%20' before parsing. Safe to do
    // unconditionally: a literal '+' in correctly-encoded form data would
    // itself already be escaped as "%2B".
    QByteArray body = request.body();
    body.replace('+', "%20");
    const QUrlQuery form(QString::fromUtf8(body));

    QFile file(configPath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString message = QStringLiteral("<div class=\"error\">Could not open %1: %2</div>")
                                     .arg(htmlEscape(configPath_), htmlEscape(file.errorString()));
        return QHttpServerResponse("text/html; charset=utf-8", renderForm(&message).toUtf8().constData(),
                                   QHttpServerResponder::StatusCode::InternalServerError);
    }
    QJsonParseError parseError {};
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (doc.isNull() || !doc.isObject()) {
        const QString message = QStringLiteral("<div class=\"error\">%1 is not valid JSON: %2</div>")
                                     .arg(htmlEscape(configPath_), htmlEscape(parseError.errorString()));
        return QHttpServerResponse("text/html; charset=utf-8", renderForm(&message).toUtf8().constData(),
                                   QHttpServerResponder::StatusCode::InternalServerError);
    }

    QJsonObject root = doc.object();
    QJsonObject matrix = root.value(QStringLiteral("matrix")).toObject();
    QJsonObject global = root.value(QStringLiteral("global")).toObject();

    matrix[QStringLiteral("brightness")] = form.queryItemValue(QStringLiteral("brightness")).toInt();
    matrix[QStringLiteral("autoDim")] = form.hasQueryItem(QStringLiteral("autoDim"));

    global[QStringLiteral("updateIntervalSeconds")] =
        form.queryItemValue(QStringLiteral("updateIntervalSeconds")).toInt();
    global[QStringLiteral("rotationSeconds")] =
        form.queryItemValue(QStringLiteral("rotationSeconds")).toInt();
    global[QStringLiteral("renderFps")] = form.queryItemValue(QStringLiteral("renderFps")).toInt();
    global[QStringLiteral("marketClosed")] =
        form.queryItemValue(QStringLiteral("marketClosed"), QUrl::FullyDecoded);
    global[QStringLiteral("showConnectivityIndicator")] =
        form.hasQueryItem(QStringLiteral("showConnectivityIndicator"));

    // Rebuilt from scratch, covering every physically wired (row, column)
    // position - not just the indices already present in the file - so the
    // form can add or remove a panel's display, not only edit ones that
    // already existed. matrix.chains itself is never touched here (hardware
    // wiring stays hand-edit-only); it's only read, to know which positions
    // to offer. A position with no symbols typed is simply left out of the
    // rebuilt array, whether it was configured before or not.
    QVector<int> chains;
    for (const QJsonValue &v : matrix.value(QStringLiteral("chains")).toArray())
        chains.append(v.toInt());
    if (chains.isEmpty())
        chains.append(1); // matches MatrixConfig's own default

    QJsonArray displays;
    for (int row = 1; row <= chains.size(); ++row) {
        for (int column = 1; column <= chains.at(row - 1); ++column) {
            const QString symbolsKey = QStringLiteral("display_%1_%2_symbols").arg(row).arg(column);
            const QString raw = form.queryItemValue(symbolsKey, QUrl::FullyDecoded);
            QJsonArray symbols;
            for (const QString &part : raw.split(QLatin1Char(','), Qt::SkipEmptyParts))
                symbols.append(part.trimmed());
            if (symbols.isEmpty())
                continue;

            const QString modeKey = QStringLiteral("display_%1_%2_mode").arg(row).arg(column);
            QJsonObject display;
            display[QStringLiteral("row")] = row;
            display[QStringLiteral("column")] = column;
            display[QStringLiteral("mode")] = form.queryItemValue(modeKey, QUrl::FullyDecoded);
            display[QStringLiteral("symbols")] = symbols;
            displays.append(display);
        }
    }

    root[QStringLiteral("matrix")] = matrix;
    root[QStringLiteral("global")] = global;
    root[QStringLiteral("displays")] = displays;

    const QByteArray updatedBytes = QJsonDocument(root).toJson(QJsonDocument::Indented);

    AppConfig validated;
    QString validationError;
    if (!AppConfig::loadFromJsonBytes(updatedBytes, QStringLiteral("posted form"),
                                      &validated, &validationError)) {
        const QString message = QStringLiteral("<div class=\"error\">Not saved - %1</div>")
                                     .arg(htmlEscape(validationError));
        return QHttpServerResponse("text/html; charset=utf-8", renderForm(&message).toUtf8().constData(),
                                   QHttpServerResponder::StatusCode::UnprocessableEntity);
    }

    QSaveFile saveFile(configPath_);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text)
        || saveFile.write(updatedBytes) < 0
        || !saveFile.commit()) {
        const QString message = QStringLiteral("<div class=\"error\">Validated OK but could not "
                                              "write %1: %2</div>")
                                     .arg(htmlEscape(configPath_), htmlEscape(saveFile.errorString()));
        return QHttpServerResponse("text/html; charset=utf-8", renderForm(&message).toUtf8().constData(),
                                   QHttpServerResponder::StatusCode::InternalServerError);
    }

    // QSaveFile::commit() protects against a *partial* write (it writes to a
    // temp file and renames over the target, so a crash mid-write never
    // corrupts the existing file) but doesn't force the result to physical
    // storage - the write, and the rename itself, can still be sitting in
    // the page cache. On a device that might lose power without a clean
    // shutdown rather than being cleanly rebooted, that's a real way to
    // lose a change that was already reported back as "Saved". sync()
    // flushes all pending filesystem writes system-wide; blunt (it's not
    // scoped to just this one file) but simple and correct, and this device
    // writes to disk rarely enough that the cost is a non-issue.
    ::sync();

    const QString message = QStringLiteral(
        "<div class=\"ok\">Saved. Restart hub75stock for these changes to take effect.</div>");
    return QHttpServerResponse("text/html; charset=utf-8", renderForm(&message).toUtf8().constData());
}

} // namespace hub75
