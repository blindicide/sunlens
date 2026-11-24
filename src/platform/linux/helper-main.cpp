#include "sunlens/helper-paths.hpp"
#include "sunlens/linux-tunnel-platform.hpp"
#include "sunlens/version.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSocketNotifier>
#include <QTimer>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

void privateDirectory(const QString& path) {
    const auto parent = QFileInfo(path).absolutePath();
    if (!sunlens::LinuxTunnelPlatform::trustedPath(parent, false) || QFileInfo(path).isSymLink() ||
        !QDir().mkpath(path) || QFileInfo(path).ownerId() != 0 ||
        !QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                         QFileDevice::ExeOwner))
        throw sunlens::Error(QStringLiteral("Небезопасный системный каталог SUNLENS."));
}

void nonblocking(int descriptor) {
    const int flags = ::fcntl(descriptor, F_GETFL);
    if (flags < 0 || ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) != 0)
        throw sunlens::Error(QStringLiteral("Не удалось настроить закрытый IPC-канал."));
}

} // namespace

int main(int argc, char* argv[]) {
    ::umask(0077);
    ::signal(SIGPIPE, SIG_IGN);
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("sunlens-tunnel-helper"));
    const auto arguments = app.arguments();
    if (arguments.size() == 2 && arguments[1] == "--version") {
        const auto version = QByteArray("SUNLENS ") + sunlens::Version.toUtf8() + '\n';
        return ::write(STDOUT_FILENO, version.constData(), version.size()) == version.size() ? 0
                                                                                             : 1;
    }
    const bool recovery = arguments.size() == 2 && arguments[1] == "--recover";
    if ((arguments.size() != 1 && !recovery) || ::geteuid() != 0) {
        qCritical("Помощник требует авторизации polkit; произвольные аргументы запрещены.");
        return 1;
    }
    const struct rlimit limit {
        0, 0
    };
    if (::prctl(PR_SET_DUMPABLE, 0) != 0 || ::setrlimit(RLIMIT_CORE, &limit) != 0)
        return 1;
    try {
        privateDirectory(sunlens::HelperStateDirectory);
        privateDirectory(sunlens::HelperRuntimeDirectory);
        QLockFile lock(QDir(sunlens::HelperStateDirectory).filePath(QStringLiteral("helper.lock")));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0))
            throw sunlens::Error(
                QStringLiteral("Другой сетевой помощник уже работает. Сначала отключитесь."));
        sunlens::LinuxTunnelPaths paths{sunlens::InstalledSingBox, sunlens::HelperRuntimeDirectory};
        paths.xray = sunlens::InstalledXray;
        auto platform = std::make_unique<sunlens::LinuxTunnelPlatform>(paths);
        sunlens::TunnelManager manager(std::move(platform), sunlens::HelperStateDirectory);
        if (recovery)
            return manager.recover() ? 0 : 1;
        nonblocking(STDIN_FILENO);
        nonblocking(STDOUT_FILENO);
        bool brokenOutput = false;
        const auto respond = [&] {
            if (brokenOutput)
                return;
            const auto bytes =
                QJsonDocument(manager.status()).toJson(QJsonDocument::Compact) + '\n';
            if (bytes.size() > 4096 ||
                ::write(STDOUT_FILENO, bytes.constData(), bytes.size()) != bytes.size()) {
                brokenOutput = true;
                (void)manager.stop();
                app.exit(1);
            }
        };
        QObject::connect(&manager, &sunlens::TunnelManager::statusChanged, &app, respond);
        if (!manager.recover())
            return 1;
        QByteArray input;
        QTimer idle;
        idle.setSingleShot(true);
        idle.setInterval(30000);
        QObject::connect(&idle, &QTimer::timeout, &app, [&] {
            (void)manager.stop();
            app.quit();
        });
        idle.start();
        QSocketNotifier notifier(STDIN_FILENO, QSocketNotifier::Read);
        QObject::connect(&notifier, &QSocketNotifier::activated, &app, [&] {
            char buffer[8192];
            while (true) {
                const auto count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
                if (count == 0) {
                    notifier.setEnabled(false);
                    const bool restored = manager.stop();
                    app.exit(restored ? 0 : 1);
                    return;
                }
                if (count < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    notifier.setEnabled(false);
                    (void)manager.stop();
                    app.exit(1);
                    return;
                }
                input.append(buffer, static_cast<qsizetype>(count));
                if (input.size() > 128 * 1024) {
                    (void)manager.stop();
                    app.exit(1);
                    return;
                }
            }
            while (input.contains('\n')) {
                const auto end = input.indexOf('\n');
                const auto frame = input.left(end);
                input.remove(0, end + 1);
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(frame, &error);
                const auto object = document.object();
                if (error.error != QJsonParseError::NoError || !document.isObject() ||
                    object["schema"] != QJsonValue(1) || !object["operation"].isString()) {
                    (void)manager.stop();
                    app.exit(1);
                    return;
                }
                const auto operation = object["operation"].toString();
                if (operation == "start" && object.size() == 3 && object["request"].isObject()) {
                    try {
                        const auto request =
                            sunlens::TunnelRequest::fromJson(object["request"].toObject());
                        idle.stop();
                        (void)manager.start(request);
                    } catch (const sunlens::Error&) {
                        (void)manager.stop();
                        app.exit(1);
                    }
                } else if (operation == "stop" && object.size() == 2) {
                    (void)manager.stop();
                    idle.start();
                } else if (operation == "status" && object.size() == 2) {
                    respond();
                } else {
                    (void)manager.stop();
                    app.exit(1);
                    return;
                }
            }
        });
        return app.exec();
    } catch (const sunlens::Error& error) {
        qCritical("%s", error.what());
        return 1;
    }
}
