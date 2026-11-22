#include "sunlens/application-controller.hpp"
#include "sunlens/version.hpp"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

int main(int argc, char* argv[]) {
#ifdef Q_OS_UNIX
    umask(0077);
#endif
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
#ifdef Q_OS_UNIX
    if (geteuid() == 0) {
        qCritical("Не запускайте интерфейс SUNLENS от root.");
        return 1;
    }
#endif
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    try {
        QTemporaryDir temporary;
        const auto directory =
            parser.isSet(QStringLiteral("smoke-test"))
                ? temporary.path()
                : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (directory.isEmpty() || QFileInfo(directory).isSymLink() || !QDir().mkpath(directory) ||
            !QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                  QFileDevice::ExeOwner))
            throw sunlens::Error(QStringLiteral("Не удалось создать закрытый каталог данных."));
        sunlens::ProfileRepository repository(directory + QStringLiteral("/profiles.sqlite"));
        sunlens::ApplicationController controller(repository);
        bool qmlWarnings = false;
        QQmlApplicationEngine engine;
        QObject::connect(&engine, &QQmlEngine::warnings, &app,
                         [&qmlWarnings] { qmlWarnings = true; });
        engine.rootContext()->setContextProperty(QStringLiteral("appVersion"), sunlens::Version);
        engine.rootContext()->setContextProperty(QStringLiteral("qtVersion"),
                                                 QString::fromLatin1(qVersion()));
        engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
        engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Sunlens/qml/Main.qml")));
        if (engine.rootObjects().isEmpty())
            return 1;
        if (parser.isSet(QStringLiteral("smoke-test")))
            QTimer::singleShot(250, &app, [&app, &qmlWarnings] { app.exit(qmlWarnings ? 1 : 0); });
        return app.exec();
    } catch (const sunlens::Error& error) {
        qCritical("%s", error.what());
        return 1;
    }
}
