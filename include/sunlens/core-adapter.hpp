#pragma once

#include "sunlens/profile.hpp"

#include <QJsonDocument>

namespace sunlens {

enum class RoutingMode { Proxy, Direct, Rules };
enum class RuleKind { Domain, DomainSuffix, IpCidr };

struct RouteRule {
    RuleKind kind = RuleKind::Domain;
    QString value;
    bool direct = false;
};

struct DnsOptions {
    QString address = QStringLiteral("1.1.1.1");
    int port = 443;
    bool https = true;
    QString tlsName = QStringLiteral("cloudflare-dns.com");
};

struct TunParameters {
    QString interfaceName = QStringLiteral("sunlens0");
    QString ipv4Prefix = QStringLiteral("172.19.0.1/30");
    QString ipv6Prefix = QStringLiteral("fdfe:dcba:9876::1/126");
    int routeTable = 2022;
    int rulePriority = 9000;
    bool autoRedirect = true;
    bool nativeDns = true;
};

struct CoreOptions {
    RoutingMode routing = RoutingMode::Proxy;
    bool bypassLan = true;
    bool ipv6 = true;
    QVector<RouteRule> rules;
    DnsOptions dns;
    TunParameters tunnel;
    void validate() const;
};

struct CoreCapabilities {
    QString name;
    QString pinnedVersion;
    QStringList configurationTargets;
    bool verifiedTun = false;
    bool trafficCounters = false;
};

struct ValidationResult {
    bool valid = false;
    QString message;
    QString coreVersion;
};

class ICoreAdapter {
  public:
    virtual ~ICoreAdapter() = default;
    [[nodiscard]] virtual CoreCapabilities capabilities() const = 0;
    [[nodiscard]] virtual QJsonDocument configuration(const Node& node,
                                                      const CoreOptions& options) const = 0;
    [[nodiscard]] virtual ValidationResult validate(const QString& executable,
                                                    const QJsonDocument& config) const = 0;
};

class SingBoxAdapter final : public ICoreAdapter {
  public:
    [[nodiscard]] CoreCapabilities capabilities() const override;
    [[nodiscard]] QJsonDocument configuration(const Node& node,
                                              const CoreOptions& options) const override;
    [[nodiscard]] ValidationResult validate(const QString& executable,
                                            const QJsonDocument& config) const override;
    [[nodiscard]] static QJsonDocument profileCheckConfig(const Node& node, quint16 port,
                                                          const QString& user,
                                                          const QString& password);
};

class XrayAdapter final : public ICoreAdapter {
  public:
    [[nodiscard]] CoreCapabilities capabilities() const override;
    [[nodiscard]] QJsonDocument configuration(const Node& node,
                                              const CoreOptions& options) const override;
    [[nodiscard]] ValidationResult validate(const QString& executable,
                                            const QJsonDocument& config) const override;
    [[nodiscard]] static QJsonDocument profileCheckConfig(const Node& node, quint16 port,
                                                          const QString& user,
                                                          const QString& password);
};

} // namespace sunlens
