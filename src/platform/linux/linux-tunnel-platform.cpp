#include "sunlens/linux-tunnel-platform.hpp"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QUuid>
#include <csignal>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace sunlens {
namespace {

struct CommandResult {
    bool success = false;
    QByteArray output;
};

QProcessEnvironment safeEnvironment() {
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("PATH"), QStringLiteral("/usr/sbin:/usr/bin:/sbin:/bin"));
    environment.insert(QStringLiteral("LANG"), QStringLiteral("C.UTF-8"));
    return environment;
}

CommandResult command(const QString& executable, const QStringList& arguments) {
    QProcess process;
    process.setProcessEnvironment(safeEnvironment());
    process.setProgram(executable);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(2000))
        return {};
    QDeadlineTimer deadline(5000);
    QByteArray output;
    while (process.state() != QProcess::NotRunning && !deadline.hasExpired()) {
        process.waitForReadyRead(100);
        output += process.readAll();
        if (output.size() > 4 * 1024 * 1024)
            break;
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    output += process.readAll();
    return {output.size() <= 4 * 1024 * 1024 && process.exitCode() == 0 &&
                process.exitStatus() == QProcess::NormalExit,
            output};
}

QString readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromLatin1(file.read(65536)).trimmed();
}

int tableId(const QJsonValue& value) {
    if (value.isDouble())
        return value.toInt(-1);
    bool valid = false;
    const int result = value.toString().toInt(&valid);
    return valid ? result : -1;
}

bool singBoxEgressSlotsFree(const TunnelLease& lease, const QJsonObject& snapshot) {
    for (const auto& family : {QStringLiteral("rules4"), QStringLiteral("rules6")}) {
        for (const auto& value : snapshot[family].toArray()) {
            const int priority = value.toObject()["priority"].toInt(-1);
            if (priority == lease.rulePriority - 2 || priority == lease.rulePriority - 1)
                return false;
        }
    }
    return true;
}

bool overlaps(const QString& prefix, const QJsonObject& before) {
    const auto candidate = QHostAddress::parseSubnet(prefix);
    for (const auto& family : {QStringLiteral("routes4"), QStringLiteral("routes6")}) {
        for (const auto& route : before[family].toArray()) {
            const auto existing = QHostAddress::parseSubnet(route.toObject()["dst"].toString());
            if (existing.second > 0 && existing.first.protocol() == candidate.first.protocol() &&
                (existing.first.isInSubnet(candidate) || candidate.first.isInSubnet(existing)))
                return true;
        }
    }
    return false;
}

bool freeReservation(const TunnelLease& lease, const QJsonObject& before) {
    for (const auto& family : {QStringLiteral("rules4"), QStringLiteral("rules6")}) {
        for (const auto& value : before[family].toArray()) {
            const auto rule = value.toObject();
            const int priority = rule["priority"].toInt(-1);
            if ((priority >= lease.rulePriority && priority <= lease.rulePriority + 10) ||
                tableId(rule["table"]) == lease.routeTable)
                return false;
        }
    }
    for (const auto& family : {QStringLiteral("routes4"), QStringLiteral("routes6")}) {
        for (const auto& value : before[family].toArray()) {
            if (tableId(value.toObject()["table"]) == lease.routeTable)
                return false;
        }
    }
    for (const auto& value : before["addresses"].toArray()) {
        if (value.toObject()["ifname"].toString() == lease.interfaceName)
            return false;
    }
    return true;
}

bool ownedRule(const QJsonObject& rule, const TunnelLease& lease) {
    if (tableId(rule["table"]) == lease.routeTable)
        return true;
    if (lease.core == "xray") {
        const auto mark = rule["fwmark"].toString();
        if (rule.contains("fwmask") ||
            mark.toUInt(nullptr, 0) != static_cast<unsigned>(lease.routeTable))
            return false;
        const int priority = rule["priority"].toInt();
        return (rule["table"] == QJsonValue("main") && priority == lease.rulePriority) ||
               (rule["action"] == QJsonValue("unreachable") && priority == lease.rulePriority + 1);
    }
    if (rule.contains("nop") && rule["priority"].toInt() == lease.rulePriority + 10)
        return true;
    if (rule["goto"].toInt(-1) == lease.rulePriority + 10) {
        if (rule["iif"].toString() == lease.interfaceName)
            return true;
        return rule["iif"] == QJsonValue("lo") && rule["srclen"].toInt(-1) == 1 &&
               (rule["src"] == QJsonValue("::") || rule["src"] == QJsonValue("8000::"));
    }
    return rule["table"] == QJsonValue("main") && rule.contains("not") &&
           rule["dport"].toInt(-1) == 53 && rule["suppress_prefixlen"].toInt(-1) == 0 &&
           rule["src"] == QJsonValue("all");
}

} // namespace

LinuxTunnelPlatform::LinuxTunnelPlatform(LinuxTunnelPaths paths) : paths_(std::move(paths)) {
    for (QString* executable : {&paths_.ip, &paths_.resolvectl}) {
        const QFileInfo source(*executable);
        if (paths_.strictOwnership &&
            (source.ownerId() != 0 || !trustedPath(source.absolutePath(), false)))
            throw Error(QStringLiteral("Небезопасный путь системного сетевого инструмента."));
        *executable = source.canonicalFilePath();
    }
    if (paths_.strictOwnership && (!trustedPath(paths_.ip, true) ||
                                   (paths_.systemDns && !trustedPath(paths_.resolvectl, true))))
        throw Error(QStringLiteral(
            "Ядро и сетевые инструменты должны принадлежать root без записи для других."));
    core_.setProcessEnvironment(safeEnvironment());
    core_.setStandardOutputFile(QProcess::nullDevice());
    // Вывод fixture доступен только из непоставляемого namespace driver.
    core_.setStandardErrorFile(paths_.strictOwnership ? QProcess::nullDevice()
                                                      : QStringLiteral("/dev/stderr"));
}

bool LinuxTunnelPlatform::trustedPath(const QString& path, bool executable) {
    if (!QFileInfo(path).isAbsolute())
        return false;
    QString current = QDir::cleanPath(path);
    bool first = true;
    while (true) {
        struct stat info {};
        if (::lstat(QFile::encodeName(current).constData(), &info) != 0 || S_ISLNK(info.st_mode) ||
            info.st_uid != 0 || (info.st_mode & (S_IWGRP | S_IWOTH)) != 0)
            return false;
        if (first && executable &&
            (!S_ISREG(info.st_mode) || (info.st_mode & (S_ISUID | S_ISGID)) != 0 ||
             (info.st_mode & S_IXUSR) == 0))
            return false;
        if ((!first || !executable) && !S_ISDIR(info.st_mode))
            return false;
        if (current == "/")
            return true;
        current = QFileInfo(current).absolutePath();
        first = false;
    }
}

QString LinuxTunnelPlatform::bootId() {
    return readFile(QStringLiteral("/proc/sys/kernel/random/boot_id"));
}

QString LinuxTunnelPlatform::processIdentity(qint64 pid) {
    const auto stat = readFile(QStringLiteral("/proc/%1/stat").arg(pid));
    const auto end = stat.lastIndexOf(')');
    if (end < 0)
        return {};
    const auto fields = stat.mid(end + 2).split(' ', Qt::SkipEmptyParts);
    return fields.size() > 19 && fields[0] != "Z" && fields[0] != "X" ? fields[19] : QString();
}

QJsonArray LinuxTunnelPlatform::ipJson(const QStringList& arguments) const {
    QStringList argv{QStringLiteral("-j")};
    argv += arguments;
    const auto result = command(paths_.ip, argv);
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(result.output, &error);
    if (!result.success || error.error != QJsonParseError::NoError || !json.isArray())
        throw Error(QStringLiteral("Не удалось прочитать состояние сети через iproute2."));
    return json.array();
}

void LinuxTunnelPlatform::ipChange(const QStringList& arguments) const {
    if (!command(paths_.ip, arguments).success)
        throw Error(QStringLiteral("Не удалось освободить зарезервированный сетевой ресурс."));
}

QJsonObject LinuxTunnelPlatform::snapshot() {
    QJsonObject result{{"routes4", ipJson({"-4", "route", "show", "table", "all"})},
                       {"routes6", ipJson({"-6", "route", "show", "table", "all"})},
                       {"rules4", ipJson({"-4", "rule", "show"})},
                       {"rules6", ipJson({"-6", "rule", "show"})},
                       {"addresses", ipJson({"address", "show"})}};
    if (paths_.systemDns) {
        const auto resolved = command(paths_.resolvectl, {"status"});
        const auto resolvConf = QFileInfo(QStringLiteral("/etc/resolv.conf")).canonicalFilePath();
        if (!resolved.success ||
            resolvConf != QStringLiteral("/run/systemd/resolve/stub-resolv.conf"))
            throw Error(
                QStringLiteral("Для безопасного DNS нужен systemd-resolved и /etc/resolv.conf, "
                               "ссылающийся на /run/systemd/resolve/stub-resolv.conf."));
        result.insert("resolved", QString::fromUtf8(resolved.output));
    }
    return result;
}

TunnelLease LinuxTunnelPlatform::reserve(const QJsonObject& before) {
    for (int attempt = 0; attempt < 1024; ++attempt) {
        TunnelLease lease;
        lease.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto hex = lease.id;
        hex.remove('-');
        lease.bootId = bootId();
        lease.interfaceName = QStringLiteral("sln") + hex.left(8);
        lease.ipv4Prefix = QStringLiteral("100.96.%1.%2/30")
                               .arg(QRandomGenerator::global()->bounded(256))
                               .arg(QRandomGenerator::global()->bounded(64) * 4 + 1);
        lease.ipv6Prefix = QStringLiteral("fd%1:%2:%3:%4::1/126")
                               .arg(hex.mid(0, 2), hex.mid(2, 4), hex.mid(6, 4), hex.mid(10, 4));
        lease.routeTable = 40000 + QRandomGenerator::global()->bounded(20001);
        lease.rulePriority = 12000 + QRandomGenerator::global()->bounded(562) * 32;
        lease.before = before;
        if (freeReservation(lease, before) && singBoxEgressSlotsFree(lease, before) &&
            !overlaps(lease.ipv4Prefix, before) && !overlaps(lease.ipv6Prefix, before))
            return lease;
    }
    throw Error(QStringLiteral("Нет свободного адресного пространства или диапазона правил TUN."));
}

void LinuxTunnelPlatform::launch(const TunnelRequest& request, TunnelLease& lease) {
    if (::geteuid() != 0)
        throw Error(QStringLiteral("Сетевой помощник должен быть авторизован через polkit."));
    CoreOptions options = request.options;
    options.tunnel.interfaceName = lease.interfaceName;
    options.tunnel.ipv4Prefix = lease.ipv4Prefix;
    options.tunnel.ipv6Prefix = lease.ipv6Prefix;
    options.tunnel.routeTable = lease.routeTable;
    options.tunnel.rulePriority = lease.rulePriority;
    // Первая транзакционная реализация не создаёт глобальные nftables-объекты.
    options.tunnel.autoRedirect = false;
    options.tunnel.nativeDns = paths_.systemDns;
    const bool xray = request.core == "xray";
    const QString executable = xray ? paths_.xray : paths_.core;
    if (paths_.strictOwnership && !trustedPath(executable, true))
        throw Error(QStringLiteral("Установите закреплённое ядро в закрытый root-owned каталог."));
    const XrayAdapter xrayAdapter;
    const SingBoxAdapter singBoxAdapter;
    const ICoreAdapter& adapter = xray ? static_cast<const ICoreAdapter&>(xrayAdapter)
                                       : static_cast<const ICoreAdapter&>(singBoxAdapter);
    const auto config = adapter.configuration(request.node, options);
    const auto validation = adapter.validate(executable, config);
    if (!validation.valid)
        throw Error(validation.message);
    const auto current = snapshot();
    if (!freeReservation(lease, current) || (!xray && !singBoxEgressSlotsFree(lease, current)) ||
        overlaps(lease.ipv4Prefix, current) || overlaps(lease.ipv6Prefix, current))
        throw Error(QStringLiteral("Сеть изменилась во время подготовки. Подключитесь повторно."));
    const auto directory = QDir(paths_.runtimeDirectory).filePath(lease.id);
    if (QFileInfo::exists(directory) || QFileInfo(directory).isSymLink() ||
        !QDir().mkdir(directory) ||
        !QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                              QFileDevice::ExeOwner))
        throw Error(QStringLiteral("Не удалось создать закрытый каталог конфигурации."));
    QFile file(QDir(directory).filePath(QStringLiteral("config.json")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        throw Error(QStringLiteral("Не удалось создать закрытую конфигурацию."));
    const auto bytes = config.toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.flush())
        throw Error(QStringLiteral("Не удалось сохранить конфигурацию."));
    file.close();
    if (!xray) {
        const auto mark = QString::number(lease.routeTable);
        for (const auto& family : {QStringLiteral("-4"), QStringLiteral("-6")}) {
            ipChange({family, "rule", "add", "priority", QString::number(lease.rulePriority - 2),
                      "fwmark", mark, "lookup", "main"});
            ipChange({family, "rule", "add", "priority", QString::number(lease.rulePriority - 1),
                      "fwmark", mark, "unreachable"});
        }
    }
    core_.setProgram(executable);
    core_.setArguments({"run", xray ? "-config" : "-c", file.fileName()});
    const auto parentPid = ::getpid();
    core_.setChildProcessModifier([parentPid] {
        const struct rlimit limit {
            0, 0
        };
        if (::prctl(PR_SET_PDEATHSIG, SIGTERM) != 0 || ::getppid() != parentPid ||
            ::setrlimit(RLIMIT_CORE, &limit) != 0)
            ::_exit(127);
    });
    core_.start();
    if (!core_.waitForStarted(3000))
        throw Error(QStringLiteral("Не удалось запустить закреплённое ядро."));
    lease.processId = core_.processId();
    lease.processIdentity = processIdentity(lease.processId);
    if (lease.processIdentity.isEmpty())
        throw Error(QStringLiteral("Не удалось зафиксировать идентичность процесса ядра."));
}

bool LinuxTunnelPlatform::running() const { return core_.state() != QProcess::NotRunning; }

bool LinuxTunnelPlatform::ready(TunnelLease& lease) {
    const auto addresses = ipJson({"address", "show"});
    QJsonObject link;
    for (const auto& value : addresses) {
        if (value.toObject()["ifname"].toString() == lease.interfaceName)
            link = value.toObject();
    }
    if (link.isEmpty() || !link["flags"].toArray().contains(QJsonValue("UP")))
        return false;
    lease.interfaceIndex = link["ifindex"].toInt();
    if (lease.core == "xray") {
        for (const auto& family : {QStringLiteral("-4"), QStringLiteral("-6")}) {
            const auto rules = ipJson({family, "rule", "show"});
            bool hasMark = false;
            bool hasGuard = false;
            bool hasRoute = false;
            for (const auto& value : rules) {
                const auto rule = value.toObject();
                if (!ownedRule(rule, lease))
                    continue;
                hasMark |= rule["priority"].toInt() == lease.rulePriority;
                hasGuard |= rule["priority"].toInt() == lease.rulePriority + 1;
                hasRoute |= tableId(rule["table"]) == lease.routeTable;
            }
            if (!hasMark)
                ipChange({family, "rule", "add", "priority", QString::number(lease.rulePriority),
                          "fwmark", QString::number(lease.routeTable), "lookup", "main"});
            if (!hasGuard)
                ipChange({family, "rule", "add", "priority",
                          QString::number(lease.rulePriority + 1), "fwmark",
                          QString::number(lease.routeTable), "unreachable"});
            if (!hasRoute) {
                ipChange({family, "route", "add", "default", "dev", lease.interfaceName, "table",
                          QString::number(lease.routeTable)});
                ipChange({family, "rule", "add", "priority",
                          QString::number(lease.rulePriority + 2), "lookup",
                          QString::number(lease.routeTable)});
            }
        }
    }
    bool routed4 = false;
    bool routed6 = false;
    for (const auto& rule : ipJson({"-4", "rule", "show"}))
        routed4 |= tableId(rule.toObject()["table"]) == lease.routeTable;
    for (const auto& rule : ipJson({"-6", "rule", "show"}))
        routed6 |= tableId(rule.toObject()["table"]) == lease.routeTable;
    if (!routed4 || !routed6)
        return false;
    if (paths_.systemDns) {
        const auto dns = command(paths_.resolvectl, {"dns", lease.interfaceName});
        const auto domains = command(paths_.resolvectl, {"domain", lease.interfaceName});
        const auto address = QHostAddress(lease.ipv4Prefix.section('/', 0, 0)).toIPv4Address() + 1;
        if (!dns.success || !domains.success ||
            !dns.output.contains(QHostAddress(address).toString().toUtf8()) ||
            !domains.output.contains("~."))
            return false;
    }
    return true;
}

void LinuxTunnelPlatform::shutdown(const TunnelLease& lease) {
    if (running()) {
        core_.terminate();
        if (!core_.waitForFinished(3000)) {
            core_.kill();
            if (!core_.waitForFinished(1000))
                throw Error(QStringLiteral("Не удалось остановить процесс ядра."));
        }
    }
    if (lease.bootId != bootId() || lease.processId <= 0 ||
        processIdentity(lease.processId).isEmpty())
        return;
    const int pidfd = static_cast<int>(::syscall(SYS_pidfd_open, lease.processId, 0));
    if (pidfd < 0)
        throw Error(QStringLiteral("Не удалось закрепить PID ядра; требуется Linux с pidfd."));
    if (processIdentity(lease.processId) != lease.processIdentity ||
        QFileInfo(QStringLiteral("/proc/%1/exe").arg(lease.processId)).symLinkTarget() !=
            QFileInfo(lease.core == "xray" ? paths_.xray : paths_.core).canonicalFilePath()) {
        ::close(pidfd);
        throw Error(QStringLiteral("PID ядра переиспользован; чужой процесс не будет остановлен."));
    }
    if (::syscall(SYS_pidfd_send_signal, pidfd, SIGTERM, nullptr, 0) != 0) {
        ::close(pidfd);
        throw Error(QStringLiteral("Не удалось остановить оставшееся ядро."));
    }
    struct pollfd waiting {
        pidfd, POLLIN, 0
    };
    if (::poll(&waiting, 1, 3000) <= 0) {
        if (::syscall(SYS_pidfd_send_signal, pidfd, SIGKILL, nullptr, 0) != 0 ||
            ::poll(&waiting, 1, 1000) <= 0) {
            ::close(pidfd);
            throw Error(QStringLiteral("Не удалось завершить оставшееся ядро."));
        }
    }
    ::close(pidfd);
}

void LinuxTunnelPlatform::removeConfig(const TunnelLease& lease) const {
    const auto directory = QDir(paths_.runtimeDirectory).filePath(lease.id);
    if (!QFileInfo::exists(directory))
        return;
    const QFileInfo info(directory);
    if (info.isSymLink() || !info.isDir() || info.ownerId() != ::geteuid())
        throw Error(QStringLiteral("Небезопасный каталог временной конфигурации."));
    const auto path = QDir(directory).filePath(QStringLiteral("config.json"));
    if (QFileInfo(path).isSymLink() || (QFile::exists(path) && !QFile::remove(path)) ||
        !QDir().rmdir(directory))
        throw Error(QStringLiteral("Не удалось безопасно удалить временную конфигурацию."));
}

void LinuxTunnelPlatform::restore(const TunnelLease& lease) {
    (void)TunnelLease::fromJson(lease.toJson());
    if (!freeReservation(lease, lease.before))
        throw Error(QStringLiteral("Журнал пересекается с исходными ресурсами; откат запрещён."));
    if (lease.bootId == bootId()) {
        const auto addresses = ipJson({"address", "show"});
        for (const auto& value : addresses) {
            const auto link = value.toObject();
            if (link["ifname"].toString() != lease.interfaceName)
                continue;
            if (lease.interfaceIndex == 0 || link["ifindex"].toInt() != lease.interfaceIndex)
                throw Error(QStringLiteral(
                    "Идентичность TUN не подтверждена; чужой интерфейс не удаляется."));
            if (paths_.systemDns &&
                !command(paths_.resolvectl, {"revert", lease.interfaceName}).success)
                throw Error(QStringLiteral("Не удалось восстановить DNS TUN."));
            ipChange({"link", "delete", "dev", lease.interfaceName});
        }
        for (const auto& family : {QStringLiteral("-4"), QStringLiteral("-6")}) {
            const auto rules = ipJson({family, "rule", "show"});
            if (lease.core == "sing-box" && singBoxEgressSlotsFree(lease, lease.before)) {
                const auto mark = QString::number(lease.routeTable);
                for (const auto& value : rules) {
                    const auto rule = value.toObject();
                    const int priority = rule["priority"].toInt(-1);
                    const auto ruleMark = rule["fwmark"].toString().toUInt(nullptr, 0);
                    if (rule.contains("fwmask") ||
                        ruleMark != static_cast<unsigned>(lease.routeTable))
                        continue;
                    if (priority == lease.rulePriority - 2 && rule["table"] == QJsonValue("main"))
                        ipChange({family, "rule", "delete", "priority", QString::number(priority),
                                  "fwmark", mark, "lookup", "main"});
                    if (priority == lease.rulePriority - 1 &&
                        rule["action"] == QJsonValue("unreachable"))
                        ipChange({family, "rule", "delete", "priority", QString::number(priority),
                                  "fwmark", mark, "unreachable"});
                }
            }
            for (const auto& value : rules) {
                const auto rule = value.toObject();
                const int priority = rule["priority"].toInt(-1);
                if (priority >= lease.rulePriority && priority <= lease.rulePriority + 10 &&
                    !ownedRule(rule, lease))
                    throw Error(
                        QStringLiteral("В зарезервированном диапазоне появилось чужое правило."));
            }
            for (const auto& value : rules) {
                const int priority = value.toObject()["priority"].toInt(-1);
                if (priority >= lease.rulePriority && priority <= lease.rulePriority + 10)
                    ipChange({family, "rule", "delete", "priority", QString::number(priority)});
            }
            // В таблице разрешались только маршруты нашего эфемерного интерфейса.
            const auto routes = ipJson({family, "route", "show", "table", "all"});
            for (const auto& value : routes) {
                const auto route = value.toObject();
                if (tableId(route["table"]) == lease.routeTable &&
                    route["dev"].toString() != lease.interfaceName)
                    throw Error(
                        QStringLiteral("Таблица содержит чужой маршрут; очистка запрещена."));
            }
            bool tablePresent = false;
            for (const auto& route : routes)
                tablePresent |= tableId(route.toObject()["table"]) == lease.routeTable;
            if (tablePresent)
                ipChange({family, "route", "flush", "table", QString::number(lease.routeTable)});
        }
    }
    removeConfig(lease);
}

TrafficSample LinuxTunnelPlatform::counters(const TunnelLease& lease) {
    const auto links = ipJson({"-s", "link", "show"});
    for (const auto& value : links) {
        const auto link = value.toObject();
        if (link["ifname"].toString() == lease.interfaceName &&
            link["ifindex"].toInt() == lease.interfaceIndex) {
            const auto stats =
                link.contains("stats64") ? link["stats64"].toObject() : link["stats"].toObject();
            const auto upload = stats["tx"].toObject()["bytes"].toInteger(-1);
            const auto download = stats["rx"].toObject()["bytes"].toInteger(-1);
            if (upload < 0 || download < 0)
                return {};
            return {true, static_cast<quint64>(upload), static_cast<quint64>(download)};
        }
    }
    return {};
}

} // namespace sunlens
