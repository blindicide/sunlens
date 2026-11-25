#include "sunlens/profile-check.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QFutureWatcher>
#include <QSslCertificate>
#include <QtConcurrent>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 8)
        return 2;

    sunlens::Node node;
    node.protocol = QStringLiteral("shadowsocks");
    node.label = QStringLiteral("Стенд");
    node.host = QStringLiteral("127.0.0.1");
    node.port = args[3].toInt();
    node.method = QStringLiteral("aes-128-gcm");
    node.credential = args[6] == "valid" ? QStringLiteral("fixture-password")
                                         : QStringLiteral("fixture-incorrect");
    auto tls = QSslConfiguration::defaultConfiguration();
    tls.setCaCertificates(QSslCertificate::fromPath(args[5]));
    QFutureWatcher<sunlens::ProfileCheckResult> watcher;
    QObject::connect(&watcher, &QFutureWatcher<sunlens::ProfileCheckResult>::finished, &app,
                     &QCoreApplication::quit);
    watcher.setFuture(QtConcurrent::run(
        [=] { return sunlens::checkProfile(args[1], args[2], node, QUrl(args[4]), tls); }));
    app.exec();
    const auto result = watcher.result();
    qInfo().noquote() << result.message;
    return (result.elapsedMs >= 0) == (args[7] == "ok") ? 0 : 1;
}
