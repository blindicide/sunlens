#include "sunlens/linux-tunnel-platform.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <csignal>
#include <unistd.h>

namespace {

QByteArray contents(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    // Этот driver не устанавливается и работает только в user namespace непривилегированного UID.
    const auto mapping = contents(QStringLiteral("/proc/self/uid_map")).simplified().split(' ');
    if (::geteuid() != 0 || mapping.size() != 3 || mapping[0] != "0" || mapping[1] == "0" ||
        mapping[2] != "1" || (app.arguments().size() != 5 && app.arguments().size() != 6)) {
        qCritical("Driver разрешён только внутри изолированного user/network namespace.");
        return 1;
    }
    const auto arguments = app.arguments();
    try {
        sunlens::LinuxTunnelPaths paths{arguments[1],
                                        arguments[2],
                                        QStringLiteral("/usr/sbin/ip"),
                                        QStringLiteral("/usr/bin/resolvectl"),
                                        false,
                                        false};
        if (arguments.size() == 6)
            paths.xray = arguments[5];
        sunlens::TunnelManager manager(std::make_unique<sunlens::LinuxTunnelPlatform>(paths),
                                       arguments[3]);
        const auto respond = [&] {
            const auto bytes =
                QJsonDocument(manager.status()).toJson(QJsonDocument::Compact) + '\n';
            (void)::write(STDOUT_FILENO, bytes.constData(), bytes.size());
        };
        QObject::connect(&manager, &sunlens::TunnelManager::statusChanged, &app, respond);
        if (!manager.recover())
            return 1;
        QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
        QObject::connect(&input, &QSocketNotifier::activated, &app, [&] {
            char buffer[256];
            const auto count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
            if (count <= 0) {
                (void)manager.stop();
                app.quit();
                return;
            }
            const auto operation = QByteArray(buffer, static_cast<qsizetype>(count)).trimmed();
            if (operation == "start") {
                const auto object = QJsonDocument::fromJson(contents(arguments[4])).object();
                (void)manager.start(sunlens::TunnelRequest::fromJson(object));
            } else if (operation == "stop") {
                (void)manager.stop();
            } else if (operation == "kill-core") {
                const auto lease = sunlens::RecoveryJournal(arguments[3]).load();
                if (lease)
                    (void)::kill(static_cast<pid_t>(lease->processId), SIGKILL);
            } else {
                (void)manager.stop();
                app.quit();
            }
        });
        return app.exec();
    } catch (const sunlens::Error& error) {
        qCritical("%s", error.what());
        return 1;
    }
}
