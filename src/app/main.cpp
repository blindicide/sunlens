#include "proxy.hpp"
#include "sunlens/version.hpp"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("SUNLENS"));
    QCoreApplication::setOrganizationName(QStringLiteral("SUNLENS"));
    QCoreApplication::setApplicationVersion(sunlens::Version);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("5КР001-1 — настольный VPN-клиент"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("smoke-test"),
                      QStringLiteral("Проверить загрузку интерфейса без изменения сети.")});
    parser.process(app);
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    sunlens::ProxyController proxy;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"), sunlens::Version);
    engine.rootContext()->setContextProperty(QStringLiteral("proxyController"), &proxy);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Sunlens/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    if (parser.isSet(QStringLiteral("smoke-test")))
        QTimer::singleShot(250, &app, &QCoreApplication::quit);
    return app.exec();
}
