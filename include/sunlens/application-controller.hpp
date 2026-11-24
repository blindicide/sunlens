#pragma once

#include "sunlens/core-adapter.hpp"
#include "sunlens/subscription-manager.hpp"
#include "sunlens/tunnel-client.hpp"

#include <QFutureWatcher>
#include <QVariantList>
#include <memory>

namespace sunlens {

class ApplicationController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList servers READ servers NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList subscriptions READ subscriptions NOTIFY profilesChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY profilesChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool coreBusy READ coreBusy NOTIFY coreInfoChanged)
    Q_PROPERTY(QString corePath READ corePath NOTIFY coreInfoChanged)
    Q_PROPERTY(QString coreStatus READ coreStatus NOTIFY coreInfoChanged)
    Q_PROPERTY(bool tunnelAvailable READ tunnelAvailable CONSTANT)
    Q_PROPERTY(QString tunnelState READ tunnelState NOTIFY tunnelChanged)
    Q_PROPERTY(QString tunnelMessage READ tunnelMessage NOTIFY tunnelChanged)
    Q_PROPERTY(bool tunnelActive READ tunnelActive NOTIFY tunnelChanged)
    Q_PROPERTY(QVariantMap tunnelCounters READ tunnelCounters NOTIFY tunnelChanged)
    Q_PROPERTY(QString selectedCore READ selectedCore WRITE setSelectedCore NOTIFY coreInfoChanged)
  public:
    explicit ApplicationController(
        ProfileRepository& repository,
        std::shared_ptr<const ICoreAdapter> adapter = std::make_shared<SingBoxAdapter>(),
        QObject* parent = nullptr, std::unique_ptr<ITunnelClient> tunnel = nullptr);
    [[nodiscard]] QVariantList servers() const;
    [[nodiscard]] QVariantList subscriptions() const;
    [[nodiscard]] QString selectedId() const;
    [[nodiscard]] QString message() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool coreBusy() const;
    [[nodiscard]] QString corePath() const;
    [[nodiscard]] QString coreStatus() const;
    [[nodiscard]] bool tunnelAvailable() const;
    [[nodiscard]] QString tunnelState() const;
    [[nodiscard]] QString tunnelMessage() const;
    [[nodiscard]] bool tunnelActive() const;
    [[nodiscard]] QVariantMap tunnelCounters() const;
    [[nodiscard]] QString selectedCore() const;
    void setSelectedCore(const QString& core);

    Q_INVOKABLE bool importText(const QString& text);
    Q_INVOKABLE bool addManual(const QString& protocol, const QString& host, int port,
                               const QString& credential, const QString& method, bool tls,
                               const QString& serverName);
    Q_INVOKABLE void fetchSubscription(const QString& url, const QString& name);
    Q_INVOKABLE void refreshSubscription(const QString& id);
    Q_INVOKABLE void cancelFetch();
    Q_INVOKABLE void selectServer(const QString& id);
    Q_INVOKABLE void renameServer(const QString& id, const QString& label);
    Q_INVOKABLE void validateSelectedCore(const QString& executable, bool direct, bool bypassLan,
                                          bool ipv6);
    Q_INVOKABLE void startTunnel(int routing, bool bypassLan, bool ipv6, const QString& dnsAddress,
                                 int dnsPort, bool dnsHttps, const QString& dnsTlsName);
    Q_INVOKABLE void stopTunnel();
    Q_INVOKABLE void recoverTunnel();
    Q_INVOKABLE void disconnectAndQuit();

  signals:
    void profilesChanged();
    void messageChanged();
    void busyChanged();
    void coreInfoChanged();
    void tunnelChanged();
    void quitReady();

  private:
    void reload();
    void setMessage(const QString& text);
    void appendManual(const QVector<Node>& nodes);

    ProfileRepository& repository_;
    SubscriptionManager manager_;
    QVariantList servers_;
    QVariantList subscriptions_;
    QString selectedId_;
    QString message_;
    std::shared_ptr<const ICoreAdapter> adapter_;
    QFutureWatcher<ValidationResult> validation_;
    QString corePath_;
    QString selectedCore_ = QStringLiteral("sing-box");
    QString coreStatus_ = QStringLiteral("Ядро ещё не проверено. TUN не реализован.");
    bool coreBusy_ = false;
    std::unique_ptr<ITunnelClient> tunnel_;
    QString tunnelState_ = QStringLiteral("idle");
    QString tunnelMessage_ = QStringLiteral("Не подключено. Запрос изменения сети не отправлялся.");
    QVariantMap tunnelCounters_;
    bool quitRequested_ = false;
};

} // namespace sunlens
