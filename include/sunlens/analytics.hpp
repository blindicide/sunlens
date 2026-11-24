#pragma once

#include "sunlens/profile-repository.hpp"

#include <QDateTime>
#include <QVariantList>

namespace sunlens {

struct TrafficDelta {
    qint64 upload = 0;
    qint64 download = 0;
    qint64 intervalMs = 0;
    bool continuous = false;
};

struct TrafficIdentity {
    QString session;
    QString server;
    QString subscription;
    QString core;
};

class AnalyticsRepository {
  public:
    explicit AnalyticsRepository(ProfileRepository& profiles);
    [[nodiscard]] TrafficDelta sample(const TrafficIdentity& identity, qint64 upload,
                                      qint64 download, qint64 utcMs);
    void event(const TrafficIdentity& identity, const QString& state, qint64 utcMs);
    void probe(const TrafficIdentity& identity, qint64 latencyMs, qint64 utcMs);
    void prune(qint64 utcMs, int days);
    [[nodiscard]] QVariantList serverTotals() const;
    [[nodiscard]] QVariantList subscriptionTotals() const;
    [[nodiscard]] QVariantMap total() const;
    [[nodiscard]] QVariantList history() const;

  private:
    QSqlDatabase db_;
    [[nodiscard]] QVariantList totals(const QString& field) const;
};

} // namespace sunlens
