#pragma once

#include "sunlens/tunnel.hpp"

#include <QProcess>

namespace sunlens {

struct LinuxTunnelPaths {
    QString core;
    QString runtimeDirectory;
    QString ip = QStringLiteral("/usr/bin/ip");
    QString resolvectl = QStringLiteral("/usr/bin/resolvectl");
    bool strictOwnership = true;
    bool systemDns = true;
    QString xray;
};

class LinuxTunnelPlatform final : public ITunnelPlatform {
  public:
    explicit LinuxTunnelPlatform(LinuxTunnelPaths paths);
    [[nodiscard]] QJsonObject snapshot() override;
    [[nodiscard]] TunnelLease reserve(const QJsonObject& before) override;
    void launch(const TunnelRequest& request, TunnelLease& lease) override;
    [[nodiscard]] bool ready(TunnelLease& lease) override;
    [[nodiscard]] bool running() const override;
    void shutdown(const TunnelLease& lease) override;
    void restore(const TunnelLease& lease) override;
    [[nodiscard]] TrafficSample counters(const TunnelLease& lease) override;
    [[nodiscard]] static bool trustedPath(const QString& path, bool executable);
    [[nodiscard]] static QString bootId();
    [[nodiscard]] static QString processIdentity(qint64 pid);

  private:
    [[nodiscard]] QJsonArray ipJson(const QStringList& arguments) const;
    void ipChange(const QStringList& arguments) const;
    void removeConfig(const TunnelLease& lease) const;
    LinuxTunnelPaths paths_;
    QProcess core_;
};

} // namespace sunlens
