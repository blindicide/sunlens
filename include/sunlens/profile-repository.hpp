#pragma once

#include "sunlens/profile.hpp"

#include <QSqlDatabase>
#include <optional>

namespace sunlens {

class ProfileRepository {
  public:
    explicit ProfileRepository(const QString& path);
    ~ProfileRepository();
    ProfileRepository(const ProfileRepository&) = delete;
    ProfileRepository& operator=(const ProfileRepository&) = delete;

    [[nodiscard]] QString ensureSubscription(const QString& url, const QString& name);
    void replaceNodes(const QString& subscriptionId, const QVector<Node>& nodes);
    void markFetchError(const QString& subscriptionId);
    void setLabel(const QString& nodeId, const QString& label);
    [[nodiscard]] QVector<Server> servers() const;
    [[nodiscard]] QVector<Subscription> subscriptions() const;
    [[nodiscard]] std::optional<Subscription> subscription(const QString& id) const;
    void setPreference(const QString& key, const QString& value);
    [[nodiscard]] QString preference(const QString& key) const;
    [[nodiscard]] static QString manualGroupId();

  private:
    QString connectionName_;
    QSqlDatabase db_;
};

} // namespace sunlens
