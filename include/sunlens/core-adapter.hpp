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

struct CoreOptions {
    RoutingMode routing = RoutingMode::Proxy;
    bool bypassLan = true;
    bool ipv6 = true;
    QVector<RouteRule> rules;
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
};

} // namespace sunlens
