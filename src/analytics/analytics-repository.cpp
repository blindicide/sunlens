#include "sunlens/analytics.hpp"

#include <QSqlQuery>
#include <QUuid>
#include <limits>

namespace sunlens {
namespace {

void execute(QSqlQuery& query) {
    if (!query.exec())
        throw Error(QStringLiteral("Не удалось выполнить операцию аналитики SQLite."));
}

void validateIdentity(const TrafficIdentity& identity, bool requireSession) {
    if ((requireSession && QUuid(identity.session).isNull()) || QUuid(identity.server).isNull() ||
        QUuid(identity.subscription).isNull() ||
        (identity.core != "sing-box" && identity.core != "xray"))
        throw Error(QStringLiteral("Некорректный идентификатор аналитики."));
}

void insertHistory(const QSqlDatabase& db, const TrafficIdentity& identity, const QString& kind,
                   qint64 utcMs, QVariant upload, QVariant download, QVariant latency) {
    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO analytics_history("
                                 "at_ms,server,subscription,core,kind,upload,download,latency)"
                                 " VALUES(?,?,?,?,?,?,?,?)"));
    for (const auto& value :
         {QVariant(utcMs), QVariant(identity.server), QVariant(identity.subscription),
          QVariant(identity.core), QVariant(kind), upload, download, latency})
        query.addBindValue(value);
    execute(query);
}

} // namespace

AnalyticsRepository::AnalyticsRepository(ProfileRepository& profiles) : db_(profiles.db_) {}

TrafficDelta AnalyticsRepository::sample(const TrafficIdentity& identity, qint64 upload,
                                         qint64 download, qint64 utcMs) {
    validateIdentity(identity, true);
    if (upload < 0 || download < 0 || utcMs < 1)
        throw Error(QStringLiteral("Недопустимое значение счётчиков TUN."));
    if (!db_.transaction())
        throw Error(QStringLiteral("Не удалось начать транзакцию аналитики."));
    try {
        TrafficDelta delta{upload, download, 0, false};
        qint64 cumulativeUp = 0;
        qint64 cumulativeDown = 0;
        QSqlQuery previous(db_);
        previous.prepare(
            QStringLiteral("SELECT server,subscription,core,raw_up,raw_down,updated_ms,"
                           "total_up,total_down FROM traffic_sessions WHERE session=?"));
        previous.addBindValue(identity.session);
        execute(previous);
        if (previous.next()) {
            if (previous.value(0).toString() != identity.server ||
                previous.value(1).toString() != identity.subscription ||
                previous.value(2).toString() != identity.core)
                throw Error(QStringLiteral("Сессия уже принадлежит другому профилю."));
            const auto last = previous.value(5).toLongLong();
            if (utcMs <= last) {
                previous.finish();
                db_.rollback();
                return {};
            }
            const auto oldUp = previous.value(3).toLongLong();
            const auto oldDown = previous.value(4).toLongLong();
            cumulativeUp = previous.value(6).toLongLong();
            cumulativeDown = previous.value(7).toLongLong();
            if (upload >= oldUp && download >= oldDown) {
                delta = {upload - oldUp, download - oldDown, utcMs - last, true};
            } else {
                delta = {};
                insertHistory(db_, identity, QStringLiteral("counter-reset"), utcMs, {}, {}, {});
            }
        }
        previous.finish();
        constexpr auto maximum = std::numeric_limits<qint64>::max();
        if (delta.upload > maximum - cumulativeUp || delta.download > maximum - cumulativeDown)
            throw Error(QStringLiteral("Переполнение счётчиков сессии."));
        QSqlQuery update(db_);
        update.prepare(QStringLiteral(
            "INSERT INTO traffic_sessions(session,server,subscription,core,raw_up,raw_down,"
            "updated_ms,total_up,total_down) VALUES(?,?,?,?,?,?,?,?,?) ON CONFLICT(session) "
            "DO UPDATE SET raw_up=excluded.raw_up,raw_down=excluded.raw_down,"
            "updated_ms=excluded.updated_ms,total_up=excluded.total_up,"
            "total_down=excluded.total_down"));
        for (const auto& value :
             {QVariant(identity.session), QVariant(identity.server),
              QVariant(identity.subscription), QVariant(identity.core), QVariant(upload),
              QVariant(download), QVariant(utcMs), QVariant(cumulativeUp + delta.upload),
              QVariant(cumulativeDown + delta.download)})
            update.addBindValue(value);
        execute(update);
        if (delta.upload || delta.download)
            insertHistory(db_, identity, QStringLiteral("traffic"), utcMs, delta.upload,
                          delta.download, {});
        if (!db_.commit())
            throw Error(QStringLiteral("Не удалось сохранить счётчики аналитики."));
        return delta;
    } catch (...) {
        db_.rollback();
        throw;
    }
}

void AnalyticsRepository::event(const TrafficIdentity& identity, const QString& state,
                                qint64 utcMs) {
    validateIdentity(identity, false);
    if (!QStringList{"idle", "authorizing", "starting", "ready", "stopping", "error",
                     "recovery-required"}
             .contains(state))
        throw Error(QStringLiteral("Неизвестное событие аналитики."));
    insertHistory(db_, identity, state, utcMs, {}, {}, {});
}

void AnalyticsRepository::probe(const TrafficIdentity& identity, qint64 latencyMs, qint64 utcMs) {
    validateIdentity(identity, false);
    if (latencyMs < -1 || latencyMs > 5000)
        throw Error(QStringLiteral("Некорректный результат TCP-пробы."));
    insertHistory(db_, identity, QStringLiteral("tcp-probe"), utcMs, {}, {}, latencyMs);
}

void AnalyticsRepository::prune(qint64 utcMs, int days) {
    if (days < 1 || days > 3650)
        throw Error(QStringLiteral("Срок хранения истории: от 1 до 3650 дней."));
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("DELETE FROM analytics_history WHERE at_ms<?"));
    query.addBindValue(utcMs - qint64(days) * 86400000);
    execute(query);
}

QVariantList AnalyticsRepository::totals(const QString& field) const {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT %1,SUM(total_up),SUM(total_down) "
                                 "FROM traffic_sessions GROUP BY %1 ORDER BY %1")
                      .arg(field));
    execute(query);
    QVariantList results;
    while (query.next())
        results.append(QVariantMap{{"id", query.value(0).toString()},
                                   {"upload", query.value(1).toString()},
                                   {"download", query.value(2).toString()}});
    return results;
}

QVariantList AnalyticsRepository::serverTotals() const { return totals(QStringLiteral("server")); }
QVariantList AnalyticsRepository::subscriptionTotals() const {
    return totals(QStringLiteral("subscription"));
}

QVariantMap AnalyticsRepository::total() const {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT COUNT(*),COALESCE(SUM(total_up),0),"
                                 "COALESCE(SUM(total_down),0) FROM traffic_sessions"));
    execute(query);
    if (!query.next())
        return {};
    return {{"sessions", query.value(0).toLongLong()},
            {"upload", query.value(1).toString()},
            {"download", query.value(2).toString()}};
}

QVariantList AnalyticsRepository::history() const {
    QSqlQuery query(db_);
    query.prepare(
        QStringLiteral("SELECT at_ms,server,subscription,core,kind,upload,download,latency "
                       "FROM analytics_history ORDER BY at_ms DESC,row_id DESC LIMIT 100"));
    execute(query);
    QVariantList results;
    while (query.next()) {
        const auto at = QDateTime::fromMSecsSinceEpoch(query.value(0).toLongLong());
        results.append(QVariantMap{{"at", at.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))},
                                   {"server", query.value(1).toString()},
                                   {"subscription", query.value(2).toString()},
                                   {"core", query.value(3).toString()},
                                   {"kind", query.value(4).toString()},
                                   {"upload", query.value(5).toString()},
                                   {"download", query.value(6).toString()},
                                   {"latency", query.value(7).toString()}});
    }
    return results;
}

} // namespace sunlens
