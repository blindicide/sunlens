#include "sunlens/tunnel.hpp"

#include <QHostAddress>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUuid>
#include <limits>

namespace sunlens {
namespace {

void onlyFields(const QJsonObject& object, const QStringList& fields) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!fields.contains(it.key()))
            throw Error(QStringLiteral("Неизвестное поле запроса помощника."));
    }
}

QString text(const QJsonObject& object, const QString& key) {
    if (!object.value(key).isString())
        throw Error(QStringLiteral("Ожидается строковое поле запроса помощника."));
    return object.value(key).toString();
}

bool boolean(const QJsonObject& object, const QString& key) {
    if (!object.value(key).isBool())
        throw Error(QStringLiteral("Ожидается логическое поле запроса помощника."));
    return object.value(key).toBool();
}

qint64 integer(const QJsonObject& object, const QString& key, qint64 minimum, qint64 maximum) {
    const auto value = object.value(key);
    const auto number = value.toDouble(-1);
    if (!value.isDouble() || number < minimum || number > maximum ||
        number != static_cast<double>(static_cast<qint64>(number)))
        throw Error(QStringLiteral("Некорректное числовое поле запроса помощника."));
    return static_cast<qint64>(number);
}

bool uuid(const QString& value) {
    return !QUuid(value).isNull() && QUuid(value).toString(QUuid::WithoutBraces) == value;
}

} // namespace

QJsonObject TunnelRequest::toJson() const {
    QJsonArray rules;
    for (const auto& rule : options.rules)
        rules.append(QJsonObject{
            {"kind", static_cast<int>(rule.kind)}, {"value", rule.value}, {"direct", rule.direct}});
    return {{"schema", 1},
            {"core", core},
            {"serverId", serverId},
            {"subscriptionId", subscriptionId},
            {"node", node.toJson()},
            {"options", QJsonObject{{"routing", static_cast<int>(options.routing)},
                                    {"bypassLan", options.bypassLan},
                                    {"ipv6", options.ipv6},
                                    {"dns", QJsonObject{{"address", options.dns.address},
                                                        {"port", options.dns.port},
                                                        {"https", options.dns.https},
                                                        {"tlsName", options.dns.tlsName}}},
                                    {"rules", rules}}}};
}

TunnelRequest TunnelRequest::fromJson(const QJsonObject& object) {
    onlyFields(object, {"schema", "core", "serverId", "subscriptionId", "node", "options"});
    if (integer(object, "schema", 1, 1) != 1 || !object.value("node").isObject() ||
        !object.value("options").isObject())
        throw Error(QStringLiteral("Неверная схема запроса помощника."));
    TunnelRequest result;
    result.core = text(object, "core");
    if (result.core != "sing-box")
        throw Error(QStringLiteral("Это ядро ещё не поддерживается сетевым помощником."));
    result.serverId = text(object, "serverId");
    result.subscriptionId = text(object, "subscriptionId");
    if (!uuid(result.serverId) || !uuid(result.subscriptionId))
        throw Error(QStringLiteral("Требуются стабильные UUID сервера и подписки."));
    result.node = Node::fromJson(object["node"].toObject());
    const auto options = object["options"].toObject();
    onlyFields(options, {"routing", "bypassLan", "ipv6", "dns", "rules"});
    result.options.routing = static_cast<RoutingMode>(integer(options, "routing", 0, 2));
    result.options.bypassLan = boolean(options, "bypassLan");
    result.options.ipv6 = boolean(options, "ipv6");
    if (!options["dns"].isObject())
        throw Error(QStringLiteral("Отсутствуют параметры DNS."));
    const auto dns = options["dns"].toObject();
    onlyFields(dns, {"address", "port", "https", "tlsName"});
    result.options.dns.address = text(dns, "address");
    result.options.dns.port = static_cast<int>(integer(dns, "port", 1, 65535));
    result.options.dns.https = boolean(dns, "https");
    result.options.dns.tlsName = text(dns, "tlsName");
    if (!options["rules"].isArray() || options["rules"].toArray().size() > 1000)
        throw Error(QStringLiteral("Некорректный список правил помощника."));
    for (const auto& value : options["rules"].toArray()) {
        if (!value.isObject())
            throw Error(QStringLiteral("Правило должно быть объектом."));
        const auto rule = value.toObject();
        onlyFields(rule, {"kind", "value", "direct"});
        result.options.rules.append({static_cast<RuleKind>(integer(rule, "kind", 0, 2)),
                                     text(rule, "value"), boolean(rule, "direct")});
    }
    // Генератор валидирует применимость правил и все параметры до изменения сети.
    (void)SingBoxAdapter().configuration(result.node, result.options);
    return result;
}

QJsonObject TunnelLease::toJson() const {
    return {{"id", id},
            {"bootId", bootId},
            {"interfaceName", interfaceName},
            {"ipv4Prefix", ipv4Prefix},
            {"ipv6Prefix", ipv6Prefix},
            {"routeTable", routeTable},
            {"rulePriority", rulePriority},
            {"interfaceIndex", interfaceIndex},
            {"processId", processId},
            {"processIdentity", processIdentity},
            {"before", before}};
}

TunnelLease TunnelLease::fromJson(const QJsonObject& object) {
    onlyFields(object,
               {"id", "bootId", "interfaceName", "ipv4Prefix", "ipv6Prefix", "routeTable",
                "rulePriority", "interfaceIndex", "processId", "processIdentity", "before"});
    TunnelLease lease;
    lease.id = text(object, "id");
    lease.bootId = text(object, "bootId");
    lease.interfaceName = text(object, "interfaceName");
    lease.ipv4Prefix = text(object, "ipv4Prefix");
    lease.ipv6Prefix = text(object, "ipv6Prefix");
    lease.routeTable = static_cast<int>(integer(object, "routeTable", 40000, 60000));
    lease.rulePriority = static_cast<int>(integer(object, "rulePriority", 12000, 30000));
    lease.interfaceIndex =
        static_cast<int>(integer(object, "interfaceIndex", 0, std::numeric_limits<int>::max()));
    lease.processId = integer(object, "processId", 0, std::numeric_limits<int>::max());
    lease.processIdentity = text(object, "processIdentity");
    static const QRegularExpression interface(QStringLiteral("^sln[0-9a-f]{8}$"));
    static const QRegularExpression ticks(QStringLiteral("^[0-9]{1,24}$"));
    const auto ipv4 = QHostAddress::parseSubnet(lease.ipv4Prefix);
    const auto ipv6 = QHostAddress::parseSubnet(lease.ipv6Prefix);
    if (!uuid(lease.id) || !uuid(lease.bootId) ||
        !interface.match(lease.interfaceName).hasMatch() || ipv4.second != 30 ||
        ipv4.first.protocol() != QAbstractSocket::IPv4Protocol || ipv6.second != 126 ||
        ipv6.first.protocol() != QAbstractSocket::IPv6Protocol ||
        (lease.processId > 0 && !ticks.match(lease.processIdentity).hasMatch()) ||
        !object["before"].isObject())
        throw Error(
            QStringLiteral("Повреждён журнал восстановления; автоматическая очистка запрещена."));
    lease.before = object["before"].toObject();
    return lease;
}

} // namespace sunlens
