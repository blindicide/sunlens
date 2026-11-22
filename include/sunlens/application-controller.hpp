#pragma once

#include "sunlens/subscription-manager.hpp"

#include <QVariantList>

namespace sunlens {

class ApplicationController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList servers READ servers NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList subscriptions READ subscriptions NOTIFY profilesChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY profilesChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  public:
    explicit ApplicationController(ProfileRepository& repository, QObject* parent = nullptr);
    [[nodiscard]] QVariantList servers() const;
    [[nodiscard]] QVariantList subscriptions() const;
    [[nodiscard]] QString selectedId() const;
    [[nodiscard]] QString message() const;
    [[nodiscard]] bool busy() const;

    Q_INVOKABLE bool importText(const QString& text);
    Q_INVOKABLE bool addManual(const QString& protocol, const QString& host, int port,
                               const QString& credential, const QString& method, bool tls,
                               const QString& serverName);
    Q_INVOKABLE void fetchSubscription(const QString& url, const QString& name);
    Q_INVOKABLE void refreshSubscription(const QString& id);
    Q_INVOKABLE void cancelFetch();
    Q_INVOKABLE void selectServer(const QString& id);
    Q_INVOKABLE void renameServer(const QString& id, const QString& label);

  signals:
    void profilesChanged();
    void messageChanged();
    void busyChanged();

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
};

} // namespace sunlens
