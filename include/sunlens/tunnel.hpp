#pragma once

#include "sunlens/core-adapter.hpp"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <memory>
#include <optional>

namespace sunlens {

struct TunnelRequest {
    QString core = QStringLiteral("sing-box");
    QString serverId;
    QString subscriptionId;
    Node node;
    CoreOptions options;
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static TunnelRequest fromJson(const QJsonObject& object);
};

struct TunnelLease {
    QString id;
    QString bootId;
    QString interfaceName;
    QString ipv4Prefix;
    QString ipv6Prefix;
    int routeTable = 0;
    int rulePriority = 0;
    int interfaceIndex = 0;
    qint64 processId = 0;
    QString processIdentity;
    QJsonObject before;
    QString core = QStringLiteral("sing-box");
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static TunnelLease fromJson(const QJsonObject& object);
};

struct TrafficSample {
    bool available = false;
    quint64 upload = 0;
    quint64 download = 0;
};

class RecoveryJournal {
  public:
    explicit RecoveryJournal(QString directory);
    void save(const TunnelLease& lease, const QString& stage);
    [[nodiscard]] std::optional<TunnelLease> load() const;
    void clear();
    [[nodiscard]] QString directory() const;

  private:
    QString directory_;
};

class ITunnelPlatform {
  public:
    virtual ~ITunnelPlatform() = default;
    [[nodiscard]] virtual QJsonObject snapshot() = 0;
    [[nodiscard]] virtual TunnelLease reserve(const QJsonObject& before) = 0;
    virtual void launch(const TunnelRequest& request, TunnelLease& lease) = 0;
    [[nodiscard]] virtual bool ready(TunnelLease& lease) = 0;
    [[nodiscard]] virtual bool running() const = 0;
    virtual void shutdown(const TunnelLease& lease) = 0;
    virtual void restore(const TunnelLease& lease) = 0;
    [[nodiscard]] virtual TrafficSample counters(const TunnelLease& lease) = 0;
};

class TunnelManager : public QObject {
    Q_OBJECT
  public:
    TunnelManager(std::unique_ptr<ITunnelPlatform> platform, const QString& stateDirectory,
                  QObject* parent = nullptr);
    ~TunnelManager() override;
    bool recover();
    bool start(const TunnelRequest& request);
    bool stop();
    [[nodiscard]] QJsonObject status() const;

  signals:
    void statusChanged();

  private:
    void poll();
    void setState(const QString& state, const QString& message);
    bool unwind(const QString& finalState, const QString& message);
    std::unique_ptr<ITunnelPlatform> platform_;
    RecoveryJournal journal_;
    std::optional<TunnelLease> lease_;
    QTimer timer_;
    QElapsedTimer startup_;
    QString state_ = QStringLiteral("idle");
    QString message_ = QStringLiteral("Сеть не изменена.");
    TrafficSample traffic_;
};

} // namespace sunlens
