#include "sunlens/external-core.hpp"

#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

namespace sunlens {
namespace {

struct ProcessResult {
    bool ok = false;
    QByteArray output;
};

ProcessResult runChecked(const QString& executable, const QStringList& arguments) {
    QProcess process;
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("PATH"), QStringLiteral("/usr/sbin:/usr/bin:/sbin:/bin"));
    environment.insert(QStringLiteral("LANG"), QStringLiteral("C.UTF-8"));
    process.setProcessEnvironment(environment);
    process.setProgram(executable);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);
    QByteArray output;
    bool overflow = false;
    const auto drain = [&] {
        output += process.read(65537 - output.size());
        if (output.size() > 65536) {
            overflow = true;
            process.kill();
        }
    };
    QObject::connect(&process, &QProcess::readyRead, &process, drain);
    process.start();
    if (!process.waitForStarted(2000))
        return {};
    QDeadlineTimer deadline(5000);
    while (process.state() != QProcess::NotRunning && !deadline.hasExpired() && !overflow)
        process.waitForReadyRead(100);
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    drain();
    return {!overflow && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            output};
}

} // namespace

ValidationResult validateExternalCore(const QString& executable, const QJsonDocument& config,
                                      const CoreBinarySpec& spec) {
#ifndef Q_OS_LINUX
    Q_UNUSED(executable)
    Q_UNUSED(config)
    Q_UNUSED(spec)
    return {false, QStringLiteral("Проверка ядра пока реализована только для Linux x86_64."), {}};
#else
    const QFileInfo info(executable);
    if (!info.isAbsolute() || !info.isFile() || !info.isExecutable() || info.isSymLink())
        return {false,
                QStringLiteral("Укажите абсолютный путь к обычному исполняемому файлу ядра."),
                {}};
    QFile binary(executable);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!binary.open(QIODevice::ReadOnly) || !hash.addData(&binary) ||
        hash.result().toHex() != spec.hash)
        return {
            false,
            QStringLiteral("SHA-256 ядра не совпадает с закреплённым manifest. Запуск отменён."),
            {}};
    binary.close();
    const auto version = runChecked(executable, {QStringLiteral("version")});
    if (!version.ok || !version.output.startsWith(spec.versionPrefix))
        return {false, QStringLiteral("Не удалось подтвердить закреплённую версию ядра."), {}};
    try {
        if (!config.isObject())
            throw Error(QStringLiteral("Конфигурация должна быть JSON-объектом."));
        QTemporaryDir directory;
        if (!directory.isValid())
            throw Error(QStringLiteral("Не удалось создать закрытый временный каталог."));
        QFile file(directory.filePath(QStringLiteral("config.json")));
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
            !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
            throw Error(QStringLiteral("Не удалось создать закрытый файл конфигурации."));
        const auto data = config.toJson(QJsonDocument::Compact);
        if (file.write(data) != data.size() || !file.flush())
            throw Error(QStringLiteral("Не удалось записать конфигурацию ядра."));
        file.close();
        auto arguments = spec.validationArguments;
        arguments.append(file.fileName());
        const auto check = runChecked(executable, arguments);
        if (!check.ok)
            return {false,
                    QStringLiteral("%1 отклонил конфигурацию или не ответил за 5 секунд. "
                                   "Сырой вывод скрыт из-за возможных секретов.")
                        .arg(spec.name),
                    spec.version};
        return {true,
                QStringLiteral("%1 %2: конфигурация принята. TUN не запускался.")
                    .arg(spec.name, spec.version),
                spec.version};
    } catch (const Error& error) {
        return {false, QString::fromUtf8(error.what()), spec.version};
    }
#endif
}

} // namespace sunlens
