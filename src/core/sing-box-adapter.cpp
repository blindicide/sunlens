#include "sunlens/core-adapter.hpp"
#include "sunlens/core-dependencies.hpp"

#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace sunlens {
namespace {

struct ProcessResult {
    bool ok = false;
    QByteArray output;
};

ProcessResult runChecked(const QString& executable, const QStringList& arguments) {
    QProcess process;
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

QJsonObject tlsOptions(const QString& name) {
    QJsonObject object{{"enabled", true}};
    if (!name.isEmpty())
        object.insert("server_name", name);
    return object;
}

QJsonObject doh(const QString& tag, const QString& detour) {
    return {{"type", "https"},     {"tag", tag},
            {"server", "1.1.1.1"}, {"path", "/dns-query"},
            {"detour", detour},    {"tls", tlsOptions(QStringLiteral("cloudflare-dns.com"))}};
}

QJsonObject outbound(const Node& node) {
    QJsonObject object{{"type", node.protocol},
                       {"tag", "proxy"},
                       {"server", node.host},
                       {"server_port", node.port}};
    if (node.protocol == "vless" || node.protocol == "vmess") {
        object.insert("uuid", node.credential);
        if (node.protocol == "vmess") {
            object.insert("security", node.method);
            object.insert("alter_id", 0);
        }
    } else {
        object.insert("password", node.credential);
        if (node.protocol == "shadowsocks")
            object.insert("method", node.method);
    }
    if (!node.flow.isEmpty())
        object.insert("flow", node.flow);
    if (node.tls)
        object.insert("tls", tlsOptions(node.serverName.isEmpty() ? node.host : node.serverName));
    if (node.transport == "ws") {
        QJsonObject transport{{"type", "ws"}, {"path", node.wsPath.isEmpty() ? "/" : node.wsPath}};
        if (!node.wsHost.isEmpty())
            transport.insert("headers", QJsonObject{{"Host", node.wsHost}});
        object.insert("transport", transport);
    }
    return object;
}

QJsonObject ruleObject(const RouteRule& rule) {
    QString field;
    if (rule.kind == RuleKind::IpCidr) {
        if (!rule.value.contains('/') || QHostAddress::parseSubnet(rule.value).second < 0)
            throw Error(QStringLiteral("Правило IP требует корректный CIDR."));
        field = QStringLiteral("ip_cidr");
    } else {
        static const QRegularExpression domain(
            QStringLiteral("^[a-zA-Z0-9](?:[a-zA-Z0-9.-]*[a-zA-Z0-9])?$"));
        if (rule.value.size() > 253 || !domain.match(rule.value).hasMatch() ||
            rule.value.contains(".."))
            throw Error(QStringLiteral("Правило домена содержит некорректное имя."));
        field = rule.kind == RuleKind::Domain ? QStringLiteral("domain")
                                              : QStringLiteral("domain_suffix");
    }
    return {{field, QJsonArray{rule.value}},
            {"action", "route"},
            {"outbound", rule.direct ? "direct" : "proxy"}};
}

} // namespace

CoreCapabilities SingBoxAdapter::capabilities() const {
    return {QStringLiteral("sing-box"), SingBoxVersion, {QStringLiteral("linux")}, false, false};
}

QJsonDocument SingBoxAdapter::configuration(const Node& node, const CoreOptions& options) const {
    node.validate();
    if (options.routing != RoutingMode::Rules && !options.rules.isEmpty())
        throw Error(
            QStringLiteral("Пользовательские правила требуют режима маршрутизации по правилам."));
    if (options.rules.size() > 1000)
        throw Error(QStringLiteral("Слишком много правил: максимум 1000."));
    QJsonArray rules;
    if (!options.ipv6)
        rules.append(QJsonObject{{"ip_version", 6}, {"action", "reject"}});
    rules.append(QJsonObject{{"action", "sniff"}});
    rules.append(QJsonObject{{"port", 53}, {"action", "hijack-dns"}});
    rules.append(QJsonObject{{"protocol", "dns"}, {"action", "hijack-dns"}});
    if (options.bypassLan)
        rules.append(
            QJsonObject{{"ip_is_private", true}, {"action", "route"}, {"outbound", "direct"}});
    for (const auto& rule : options.rules)
        rules.append(ruleObject(rule));
    QJsonObject tun{{"type", "tun"},
                    {"tag", "tun-in"},
                    {"interface_name", "sunlens0"},
                    {"address", QJsonArray{"172.19.0.1/30", "fdfe:dcba:9876::1/126"}},
                    {"mtu", 1500},
                    {"auto_route", true},
                    {"auto_redirect", true},
                    {"strict_route", true},
                    {"dns_mode", "hijack"}};
    if (options.bypassLan)
        tun.insert("route_exclude_address",
                   QJsonArray{"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "169.254.0.0/16",
                              "fc00::/7", "fe80::/10"});
    const bool direct = options.routing == RoutingMode::Direct;
    return QJsonDocument(QJsonObject{
        {"log", QJsonObject{{"disabled", true}}},
        {"dns",
         QJsonObject{{"servers", QJsonArray{doh("bootstrap", "direct"), doh("remote", "proxy")}},
                     {"final", direct ? "bootstrap" : "remote"},
                     {"strategy", options.ipv6 ? "prefer_ipv4" : "ipv4_only"},
                     {"reverse_mapping", true}}},
        {"inbounds", QJsonArray{tun}},
        {"outbounds",
         QJsonArray{outbound(node), QJsonObject{{"type", "direct"}, {"tag", "direct"}}}},
        {"route", QJsonObject{{"auto_detect_interface", true},
                              {"default_domain_resolver", "bootstrap"},
                              {"final", direct ? "direct" : "proxy"},
                              {"rules", rules}}}});
}

ValidationResult SingBoxAdapter::validate(const QString& executable,
                                          const QJsonDocument& config) const {
#ifndef Q_OS_LINUX
    Q_UNUSED(executable)
    Q_UNUSED(config)
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
        hash.result().toHex() != SingBoxLinuxHash)
        return {
            false,
            QStringLiteral("SHA-256 ядра не совпадает с закреплённым manifest. Запуск отменён."),
            {}};
    binary.close();
    const auto version = runChecked(executable, {QStringLiteral("version")});
    const auto expected = QByteArray("sing-box version ") + SingBoxVersion.toUtf8();
    if (!version.ok || !(version.output.startsWith(expected + '\n') ||
                         version.output.startsWith(expected + "\r\n")))
        return {false, QStringLiteral("Не удалось подтвердить закреплённую версию sing-box."), {}};
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
        const auto check = runChecked(executable, {"check", "-c", file.fileName()});
        if (!check.ok)
            return {false,
                    QStringLiteral(
                        "sing-box отклонил конфигурацию или не ответил за 5 секунд. "
                        "Проверьте поддержанные параметры; сырой вывод скрыт из-за секретов."),
                    SingBoxVersion};
        return {true,
                QStringLiteral("sing-box %1: конфигурация принята. TUN не запускался.")
                    .arg(SingBoxVersion),
                SingBoxVersion};
    } catch (const Error& error) {
        return {false, QString::fromUtf8(error.what()), SingBoxVersion};
    }
#endif
}

} // namespace sunlens
