#include "sunlens/core-adapter.hpp"
#include "sunlens/core-dependencies.hpp"
#include "sunlens/external-core.hpp"

#include <QJsonArray>

namespace sunlens {
namespace {

QJsonObject tlsOptions(const QString& name) {
    QJsonObject object{{"enabled", true}};
    if (!name.isEmpty())
        object.insert("server_name", name);
    return object;
}

QJsonObject doh(const QString& tag, const QString& detour) {
    QJsonObject object{{"type", "https"},
                       {"tag", tag},
                       {"server", "1.1.1.1"},
                       {"path", "/dns-query"},
                       {"tls", tlsOptions(QStringLiteral("cloudflare-dns.com"))}};
    if (detour != "direct")
        object.insert("detour", detour);
    return object;
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
        field = QStringLiteral("ip_cidr");
    } else {
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
    options.validate();
    const auto& parameters = options.tunnel;
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
                    {"interface_name", parameters.interfaceName},
                    {"address", QJsonArray{parameters.ipv4Prefix, parameters.ipv6Prefix}},
                    {"mtu", 1500},
                    {"auto_route", true},
                    {"auto_redirect", parameters.autoRedirect},
                    {"iproute2_table_index", parameters.routeTable},
                    {"iproute2_rule_index", parameters.rulePriority},
                    {"strict_route", true},
                    {"dns_mode", parameters.nativeDns ? "hijack" : "disabled"}};
    if (options.bypassLan)
        tun.insert("route_exclude_address",
                   QJsonArray{"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "169.254.0.0/16",
                              "fc00::/7", "fe80::/10"});
    const bool direct = options.routing == RoutingMode::Direct;
    QJsonObject remote{{"tag", "remote"},
                       {"type", options.dns.https ? "https" : "udp"},
                       {"server", options.dns.address},
                       {"server_port", options.dns.port}};
    if (!direct)
        remote.insert("detour", "proxy");
    if (options.dns.https)
        remote.insert("tls", tlsOptions(options.dns.tlsName));
    return QJsonDocument(
        QJsonObject{{"log", QJsonObject{{"disabled", true}}},
                    {"dns", QJsonObject{{"servers", QJsonArray{doh("bootstrap", "direct"), remote}},
                                        {"final", "remote"},
                                        {"strategy", options.ipv6 ? "prefer_ipv4" : "ipv4_only"},
                                        {"reverse_mapping", true}}},
                    {"inbounds", QJsonArray{tun}},
                    {"outbounds", QJsonArray{outbound(node),
                                             QJsonObject{{"type", "direct"}, {"tag", "direct"}}}},
                    {"route", QJsonObject{{"auto_detect_interface", true},
                                          {"default_domain_resolver", "bootstrap"},
                                          {"final", direct ? "direct" : "proxy"},
                                          {"rules", rules}}}});
}

QJsonDocument SingBoxAdapter::profileCheckConfig(const Node& node, quint16 port,
                                                 const QString& user, const QString& password) {
    node.validate();
    const QJsonObject inbound{
        {"type", "socks"},
        {"tag", "profile-check"},
        {"listen", "127.0.0.1"},
        {"listen_port", port},
        {"users", QJsonArray{QJsonObject{{"username", user}, {"password", password}}}}};
    return QJsonDocument(QJsonObject{
        {"log", QJsonObject{{"level", "info"}}},
        {"dns", QJsonObject{{"servers", QJsonArray{doh("bootstrap", "direct")}}}},
        {"inbounds", QJsonArray{inbound}},
        {"outbounds", QJsonArray{outbound(node)}},
        {"route", QJsonObject{{"default_domain_resolver", "bootstrap"}, {"final", "proxy"}}}});
}

ValidationResult SingBoxAdapter::validate(const QString& executable,
                                          const QJsonDocument& config) const {
    return validateExternalCore(executable, config,
                                {QStringLiteral("sing-box"),
                                 SingBoxVersion,
                                 SingBoxLinuxHash,
                                 QByteArray("sing-box version ") + SingBoxVersion.toUtf8() + '\n',
                                 {"check", "-c"}});
}

} // namespace sunlens
