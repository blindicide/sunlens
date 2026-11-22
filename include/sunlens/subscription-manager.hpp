#pragma once

#include "sunlens/profile-repository.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

namespace sunlens {

struct FetchPolicy {
    int timeoutMs = 15000;
    int retries = 2;
    qsizetype maxBytes = ProfileParser::MaxBytes;
    bool allowLoopbackHttp = false;
};

class SubscriptionManager : public QObject {
    Q_OBJECT
  public:
    explicit SubscriptionManager(ProfileRepository& repository, FetchPolicy policy = {},
                                 QObject* parent = nullptr);
    ~SubscriptionManager() override;
    [[nodiscard]] bool busy() const;
    void fetch(const QString& url, const QString& name);
    void refresh(const QString& id);
    void cancel();

  signals:
    void busyChanged();
    void completed(const QString& subscriptionId);
    void failed(const QString& message);

  private:
    void begin(const QString& id, const QUrl& url);
    void attempt();
    void finishReply();
    void fail(const QString& message);
    void releaseReply();
    [[nodiscard]] QUrl checkedUrl(const QString& source) const;

    ProfileRepository& repository_;
    FetchPolicy policy_;
    QNetworkAccessManager network_;
    QNetworkReply* reply_ = nullptr;
    QTimer deadline_;
    QTimer retry_;
    QString id_;
    QUrl url_;
    QByteArray body_;
    QString failureReason_;
    int attempts_ = 0;
    bool active_ = false;
};

} // namespace sunlens
