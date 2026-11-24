#include "sunlens/core-adapter.hpp"

#include <QHostAddress>
#include <QRegularExpression>

namespace sunlens {

void CoreOptions::validate() const {
    static const QRegularExpression name(QStringLiteral("^[a-zA-Z][a-zA-Z0-9_-]{0,14}$"));
    const auto ipv4 = QHostAddress::parseSubnet(tunnel.ipv4Prefix);
    const auto ipv6 = QHostAddress::parseSubnet(tunnel.ipv6Prefix);
    if (!name.match(tunnel.interfaceName).hasMatch() || tunnel.routeTable < 256 ||
        tunnel.rulePriority < 1 || tunnel.rulePriority > 32750 || ipv4.second != 30 ||
        ipv4.first.protocol() != QAbstractSocket::IPv4Protocol || ipv6.second != 126 ||
        ipv6.first.protocol() != QAbstractSocket::IPv6Protocol)
        throw Error(QStringLiteral("Неверные внутренние параметры TUN."));
    QHostAddress resolver;
    if (!resolver.setAddress(dns.address) || resolver.isNull() || resolver.isMulticast() ||
        resolver.isLoopback() || resolver == QHostAddress::AnyIPv4 ||
        resolver == QHostAddress::AnyIPv6 || !resolver.scopeId().isEmpty() || dns.port < 1 ||
        dns.port > 65535 || dns.tlsName.size() > 253 ||
        dns.tlsName.contains(QRegularExpression(QStringLiteral("[\\s\\x00-\\x1f]"))))
        throw Error(QStringLiteral("DNS требует IP-адрес, порт и корректное имя TLS."));
    if (routing != RoutingMode::Proxy && routing != RoutingMode::Direct &&
        routing != RoutingMode::Rules)
        throw Error(QStringLiteral("Неизвестный режим маршрутизации."));
    if (routing != RoutingMode::Rules && !rules.isEmpty())
        throw Error(
            QStringLiteral("Пользовательские правила требуют режима маршрутизации по правилам."));
    if (rules.size() > 1000)
        throw Error(QStringLiteral("Слишком много правил: максимум 1000."));
    static const QRegularExpression domain(
        QStringLiteral("^[a-zA-Z0-9](?:[a-zA-Z0-9.-]*[a-zA-Z0-9])?$"));
    for (const auto& rule : rules) {
        if (rule.kind == RuleKind::IpCidr) {
            if (!rule.value.contains('/') || QHostAddress::parseSubnet(rule.value).second < 0)
                throw Error(QStringLiteral("Правило IP требует корректный CIDR."));
        } else if (rule.kind == RuleKind::Domain || rule.kind == RuleKind::DomainSuffix) {
            if (rule.value.size() > 253 || !domain.match(rule.value).hasMatch() ||
                rule.value.contains(".."))
                throw Error(QStringLiteral("Правило домена содержит некорректное имя."));
        } else {
            throw Error(QStringLiteral("Неподдержанный тип правила."));
        }
    }
}

} // namespace sunlens
