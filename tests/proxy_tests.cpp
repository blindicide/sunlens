#include "proxy.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <iostream>

using namespace Qt::StringLiterals;

namespace {

bool checkSettings() {
    const auto http = QJsonDocument::fromJson(sunlens::tunConfig(u"http"_s, u"192.0.2.1"_s, 8080));
    const auto socks =
        QJsonDocument::fromJson(sunlens::tunConfig(u"socks5"_s, u"[2001:db8::1]"_s, 1080));
    const auto httpOut = http[u"outbounds"_s].toArray().first().toObject();
    const auto socksOut = socks[u"outbounds"_s].toArray().first().toObject();
    const auto rules = http[u"route"_s].toObject().value(u"rules"_s).toArray();
    return httpOut.value(u"type"_s).toString() == u"http" &&
           httpOut.value(u"server_port"_s).toInt() == 8080 &&
           http[u"outbounds"_s].toArray().size() == 1 &&
           http[u"route"_s].toObject().value(u"final"_s).toString() == u"upstream" &&
           rules.last().toObject().value(u"network"_s).toString() == u"udp" &&
           rules.last().toObject().value(u"action"_s).toString() == u"reject" &&
           socksOut.value(u"type"_s).toString() == u"socks" &&
           socksOut.value(u"version"_s).toString() == u"5" &&
           socksOut.value(u"server"_s).toString() == u"2001:db8::1" &&
           !socksOut.contains(u"password"_s) && !socksOut.contains(u"username"_s) &&
           sunlens::tunConfig(u"http"_s, u"example.test"_s, 80).isEmpty() &&
           sunlens::tunConfig(u"http"_s, u"192.0.2.1:8080"_s, 80).isEmpty() &&
           sunlens::tunConfig(u"http"_s, u"192.0.2.1"_s, 65536).isEmpty() &&
           sunlens::tunConfig(u"socks4"_s, u"192.0.2.1"_s, 80).isEmpty();
}

// Local protocol stubs only: nothing is forwarded and no Internet is needed.
bool probeUsesProxy(bool socks5) {
    QEventLoop loop;
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost))
        return false;

    QByteArray received;
    bool greeted = false;
    bool sawExpectedRequest = false;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            received += socket->readAll();
            if (!socks5) {
                if (received.contains("\r\n\r\n")) {
                    sawExpectedRequest |= received.startsWith("CONNECT www.google.com:443 HTTP/");
                    socket->write("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
                    socket->disconnectFromHost();
                }
            } else {
                if (!greeted && received.size() >= 2) {
                    const auto nmethods = static_cast<unsigned char>(received[1]);
                    if (received[0] != '\x05' || received.size() < 2 + nmethods)
                        return;
                    received.remove(0, 2 + nmethods);
                    greeted = true;
                    socket->write(QByteArray::fromHex("0500"));
                }
                if (greeted && received.size() >= 21) {
                    sawExpectedRequest |=
                        received.startsWith(QByteArray::fromHex("050100030e") + "www.google.com" +
                                            QByteArray::fromHex("01bb"));
                    // Connection refused by the proxy.
                    socket->write(QByteArray::fromHex("05050001000000000000"));
                    socket->disconnectFromHost();
                }
            }
        });
    });

    sunlens::ProxyController proxy;
    bool finished = false;
    QObject::connect(&proxy, &sunlens::ProxyController::changed, &loop, [&] {
        if (!proxy.busy()) {
            finished = true;
            loop.quit();
        }
    });
    QTimer::singleShot(4000, &loop, &QEventLoop::quit);
    proxy.test(socks5 ? u"socks5"_s : u"http"_s, u"127.0.0.1"_s, server.serverPort(), {}, {});
    loop.exec();
    return finished && sawExpectedRequest && !proxy.status().startsWith(QStringLiteral("HTTPS "));
}

} // namespace

int main(int argc, char* argv[]) {
    const QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 3 && args[1] == u"--write-config") {
        const auto config = sunlens::tunConfig(args[2], u"192.0.2.1"_s, 1080);
        return config.isEmpty() || std::cout.write(config.constData(), config.size()).fail();
    }
    const bool settings = checkSettings();
    const bool http = probeUsesProxy(false);
    const bool socks = probeUsesProxy(true);
    std::cout << "settings=" << settings << " HTTP CONNECT=" << http << " SOCKS5=" << socks << '\n';
    return settings && http && socks ? 0 : 1;
}
