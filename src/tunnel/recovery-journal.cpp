#include "sunlens/tunnel.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sunlens {
namespace {

void syncDirectory(const QString& directory) {
#ifdef Q_OS_UNIX
    const int descriptor = ::open(QFile::encodeName(directory).constData(),
                                  O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0)
        throw Error(QStringLiteral("Не удалось открыть каталог журнала для синхронизации."));
    const int result = ::fsync(descriptor);
    ::close(descriptor);
    if (result != 0)
        throw Error(QStringLiteral("Не удалось синхронизировать каталог журнала."));
#else
    Q_UNUSED(directory)
#endif
}

} // namespace

RecoveryJournal::RecoveryJournal(QString directory) : directory_(std::move(directory)) {
    QFileInfo info(directory_);
    if (info.isSymLink() || (info.exists() && !info.isDir()))
        throw Error(QStringLiteral("Небезопасный каталог журнала восстановления."));
    if (!QDir().mkpath(directory_))
        throw Error(QStringLiteral("Не удалось создать каталог журнала."));
#ifdef Q_OS_UNIX
    info.refresh();
    if (info.ownerId() != ::geteuid() ||
        !QFile::setPermissions(directory_, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                               QFileDevice::ExeOwner))
        throw Error(QStringLiteral("Неверный владелец или права каталога журнала."));
#endif
}

QString RecoveryJournal::directory() const { return directory_; }

void RecoveryJournal::save(const TunnelLease& lease, const QString& stage) {
    (void)TunnelLease::fromJson(lease.toJson());
    if (!QStringList{"prepared", "starting", "ready", "stopping"}.contains(stage))
        throw Error(QStringLiteral("Неизвестная стадия сетевой транзакции."));
    const auto path = QDir(directory_).filePath(QStringLiteral("recovery.json"));
    if (QFileInfo(path).isSymLink())
        throw Error(QStringLiteral("Журнал не должен быть символической ссылкой."));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        throw Error(QStringLiteral("Не удалось открыть закрытый журнал восстановления."));
    const auto bytes =
        QJsonDocument(QJsonObject{{"schema", 1}, {"stage", stage}, {"lease", lease.toJson()}})
            .toJson(QJsonDocument::Compact);
    if (bytes.size() > 1024 * 1024 || file.write(bytes) != bytes.size() || !file.flush())
        throw Error(QStringLiteral("Не удалось записать журнал восстановления."));
#ifdef Q_OS_UNIX
    if (::fsync(file.handle()) != 0)
        throw Error(QStringLiteral("Не удалось синхронизировать журнал восстановления."));
#endif
    if (!file.commit())
        throw Error(QStringLiteral("Не удалось атомарно сохранить журнал восстановления."));
    syncDirectory(directory_);
}

std::optional<TunnelLease> RecoveryJournal::load() const {
    const auto path = QDir(directory_).filePath(QStringLiteral("recovery.json"));
    const QFileInfo info(path);
    if (info.isSymLink())
        throw Error(QStringLiteral("Небезопасная ссылка вместо журнала."));
    if (!info.exists())
        return {};
#ifdef Q_OS_UNIX
    if (info.ownerId() != ::geteuid() ||
        (info.permissions() & (QFileDevice::WriteGroup | QFileDevice::WriteOther |
                               QFileDevice::ReadGroup | QFileDevice::ReadOther)))
        throw Error(QStringLiteral("Неверные права или владелец журнала."));
#endif
    QFile file(path);
    if (file.size() > 1024 * 1024 || !file.open(QIODevice::ReadOnly))
        throw Error(QStringLiteral("Не удалось прочитать журнал восстановления."));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    const auto object = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        object["schema"] != QJsonValue(1) ||
        !QStringList{"prepared", "starting", "ready", "stopping"}.contains(
            object["stage"].toString()) ||
        !object["lease"].isObject() || object.size() != 3)
        throw Error(
            QStringLiteral("Журнал повреждён; требуется диагностика, а не слепая очистка."));
    return TunnelLease::fromJson(object["lease"].toObject());
}

void RecoveryJournal::clear() {
    const auto path = QDir(directory_).filePath(QStringLiteral("recovery.json"));
    if (QFileInfo(path).isSymLink() || (QFile::exists(path) && !QFile::remove(path)))
        throw Error(QStringLiteral("Не удалось удалить завершённый журнал."));
    syncDirectory(directory_);
}

} // namespace sunlens
