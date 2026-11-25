#include "sunlens/core-adapter.hpp"
#include "sunlens/core-dependencies.hpp"
#include "sunlens/external-core.hpp"

#include <QHostAddress>
#include <QJsonArray>

namespace sunlens {
namespace {

QJsonObject stream(const Node& node, int routeMark) {
    QJsonObject result{{"network", node.transport == "ws" ? "ws" : "raw"},
                       {"security", node.tls ? "tls" : "none"}};
    if (routeMark > 0)
        result.insert("sockopt", QJsonObject{{"mark", routeMark}});
    if (node.tls)
        result.insert(
            "tlsSettings",
            QJsonObject{{"serverName", node.serverName.isEmpty() ? node.host : node.serverName},
                        {"allowInsecure", false}});
    if (node.transport == "ws") {
        QJsonObject websocket{{"path", node.wsPath.isEmpty() ? "/" : node.wsPath}};
        if (!node.wsHost.isEmpty())
            websocket.insert("host", node.wsHost);
        result.insert("wsSettings", websocket);
    }
    return result;
}

QJsonObject proxy(const Node& node, int routeMark) {
    QJsonObject settings;
    if (node.protocol == "vless" || node.protocol == "vmess") {
        QJsonObject user{{"id", node.credential}};
        if (node.protocol == "vless") {
            user.insert("encryption", "none");
            if (!node.flow.isEmpty())
                user.insert("flow", node.flow);
        } else {
            user.insert("security", node.method);
            user.insert("alterId", 0);
        }
        settings.insert("vnext", QJsonArray{QJsonObject{{"address", node.host},
                                                        {"port", node.port},
                                                        {"users", QJsonArray{user}}}});
    } else {
        QJsonObject server{
            {"address", node.host}, {"port", node.port}, {"password", node.credential}};
        if (node.protocol == "shadowsocks")
            server.insert("method", node.method);
        settings.insert("servers", QJsonArray{server});
    }
    return {{"tag", "proxy"},
            {"protocol", node.protocol},
            {"settings", settings},
            {"streamSettings", stream(node, routeMark)}};
}

QJsonObject fieldRule(const QString& target) {
    return {{"type", "field"}, {"outboundTag", target}};
}

void requireNumericUpstream(const Node& node) {
    QHostAddress upstream;
    if (!upstream.setAddress(node.host))
        throw Error(
            QStringLiteral("Xray: укажите IP upstream и задайте SNI отдельно, если нужен TLS."));
}

} // namespace

CoreCapabilities XrayAdapter::capabilities() const {
    return {QStringLiteral("xray"), XrayVersion, {QStringLiteral("linux")}, false, false};
}

QJsonDocument XrayAdapter::configuration(const Node& node, const CoreOptions& options) const {
    node.validate();
    options.validate();
    if (options.dns.https)
        throw Error(QStringLiteral("Xray: DoH ещё не проверен. Выберите UDP DNS внутри туннеля."));
    requireNumericUpstream(node);
    QJsonArray rules;
    if (!options.ipv6) {
        auto block = fieldRule("block");
        block.insert("ip", QJsonArray{"::/0"});
        rules.append(block);
    }
    auto dns = fieldRule("dns-out");
    dns.insert("inboundTag", QJsonArray{"tun-in"});
    dns.insert("port", "53");
    rules.append(dns);
    auto dnsQueries = fieldRule(options.routing == RoutingMode::Direct ? "direct" : "proxy");
    dnsQueries.insert("inboundTag", QJsonArray{"dns-query"});
    rules.append(dnsQueries);
    if (options.bypassLan) {
        auto lan = fieldRule("direct");
        lan.insert("ip", QJsonArray{"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16",
                                    "169.254.0.0/16", "fc00::/7", "fe80::/10"});
        rules.append(lan);
    }
    for (const auto& rule : options.rules) {
        auto translated = fieldRule(rule.direct ? "direct" : "proxy");
        if (rule.kind == RuleKind::IpCidr)
            translated.insert("ip", QJsonArray{rule.value});
        else
            translated.insert(
                "domain", QJsonArray{(rule.kind == RuleKind::Domain ? QStringLiteral("full:")
                                                                    : QStringLiteral("domain:")) +
                                     rule.value});
        rules.append(translated);
    }
    auto final = fieldRule(options.routing == RoutingMode::Direct ? "direct" : "proxy");
    final.insert("network", "tcp,udp");
    rules.append(final);
    const auto& tun = options.tunnel;
    QJsonObject direct{
        {"tag", "direct"},
        {"protocol", "freedom"},
        {"streamSettings", QJsonObject{{"sockopt", QJsonObject{{"mark", tun.routeTable}}}}}};
    return QJsonDocument(QJsonObject{
        {"log", QJsonObject{{"loglevel", "none"}}},
        {"dns", QJsonObject{{"tag", "dns-query"},
                            {"queryStrategy", options.ipv6 ? "UseIP" : "UseIPv4"},
                            {"servers", QJsonArray{QJsonObject{{"address", options.dns.address},
                                                               {"port", options.dns.port}}}}}},
        {"inbounds",
         QJsonArray{QJsonObject{
             {"tag", "tun-in"},
             {"protocol", "tun"},
             {"port", 0},
             {"settings", QJsonObject{{"name", tun.interfaceName},
                                      {"mtu", 1500},
                                      {"gateway", QJsonArray{tun.ipv4Prefix, tun.ipv6Prefix}},
                                      {"autoOutboundsInterface", "auto"},
                                      {"autoSystemDnsToGateway", tun.nativeDns}}},
             {"sniffing", QJsonObject{{"enabled", true},
                                      {"routeOnly", true},
                                      {"destOverride", QJsonArray{"http", "tls", "quic"}}}}}}},
        {"outbounds", QJsonArray{proxy(node, tun.routeTable), direct,
                                 QJsonObject{{"tag", "dns-out"}, {"protocol", "dns"}},
                                 QJsonObject{{"tag", "block"}, {"protocol", "blackhole"}}}},
        {"routing", QJsonObject{{"domainStrategy", "AsIs"}, {"rules", rules}}}});
}

QJsonDocument XrayAdapter::profileCheckConfig(const Node& node, quint16 port, const QString& user,
                                              const QString& password) {
    node.validate();
    requireNumericUpstream(node);
    const QJsonObject settings{
        {"auth", "password"},
        {"accounts", QJsonArray{QJsonObject{{"user", user}, {"pass", password}}}},
        {"udp", false}};
    const QJsonObject inbound{{"protocol", "socks"},
                              {"tag", "profile-check"},
                              {"listen", "127.0.0.1"},
                              {"port", port},
                              {"settings", settings}};
    return QJsonDocument(QJsonObject{{"log", QJsonObject{{"loglevel", "warning"}}},
                                     {"inbounds", QJsonArray{inbound}},
                                     {"outbounds", QJsonArray{proxy(node, 0)}}});
}

ValidationResult XrayAdapter::validate(const QString& executable,
                                       const QJsonDocument& config) const {
    return validateExternalCore(executable, config,
                                {QStringLiteral("Xray"),
                                 XrayVersion,
                                 XrayLinuxHash,
                                 QByteArray("Xray ") + XrayVersion.toUtf8() + " (",
                                 {"run", "-test", "-config"}});
}

} // namespace sunlens
