#include "sunlens/profile-repository.hpp"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QSet>
#include <QSqlQuery>
#include <QUuid>

namespace sunlens {
namespace {

void execute(QSqlQuery& query) {
    if (!query.exec())
        throw Error(
            QStringLiteral("Операция SQLite не выполнена. Проверьте доступ и свободное место."));
}

void statement(QSqlDatabase& db, const QString& sql) {
    QSqlQuery query(db);
    if (!query.exec(sql))
        throw Error(QStringLiteral("Не удалось подготовить базу SQLite."));
}

} // namespace

ProfileRepository::ProfileRepository(const QString& path)
    : connectionName_(QUuid::createUuid().toString()),
      db_(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_)) {
    try {
        if (QFileInfo(path).isSymLink())
            throw Error(QStringLiteral("База данных не должна быть символической ссылкой."));
        QFile file(path);
        if (!file.exists()) {
            if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                throw Error(QStringLiteral("Не удалось создать файл базы данных."));
            file.close();
        }
        if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
            throw Error(QStringLiteral("Не удалось ограничить доступ к базе данных."));
        db_.setDatabaseName(path);
        db_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
        if (!db_.open())
            throw Error(
                QStringLiteral("Не удалось открыть SQLite. Проверьте наличие QSQLITE driver."));
        statement(db_, QStringLiteral("PRAGMA foreign_keys=ON"));
        QSqlQuery version(db_);
        if (!version.exec(QStringLiteral("PRAGMA user_version")) || !version.next())
            throw Error(QStringLiteral("Не удалось прочитать версию схемы SQLite."));
        const int schema = version.value(0).toInt();
        version.finish();
        if (schema > 1)
            throw Error(
                QStringLiteral("База создана более новой версией SUNLENS. Обновите приложение."));
        if (!db_.transaction())
            throw Error(QStringLiteral("Не удалось начать транзакцию SQLite."));
        statement(db_,
                  QStringLiteral(
                      "CREATE TABLE IF NOT EXISTS subscriptions("
                      "id TEXT PRIMARY KEY,url TEXT UNIQUE,name TEXT NOT NULL,"
                      "status TEXT NOT NULL DEFAULT 'new',updated_at TEXT NOT NULL DEFAULT '')"));
        statement(db_,
                  QStringLiteral("CREATE TABLE IF NOT EXISTS nodes("
                                 "id TEXT PRIMARY KEY,subscription_id TEXT NOT NULL REFERENCES "
                                 "subscriptions(id) ON DELETE CASCADE,fingerprint TEXT NOT NULL,"
                                 "config TEXT NOT NULL,label_override TEXT NOT NULL DEFAULT '',"
                                 "UNIQUE(subscription_id,fingerprint))"));
        statement(db_,
                  QStringLiteral(
                      "CREATE TABLE IF NOT EXISTS preferences(key TEXT PRIMARY KEY,value TEXT)"));
        statement(db_, QStringLiteral("PRAGMA user_version=1"));
        if (!db_.commit())
            throw Error(QStringLiteral("Не удалось сохранить схему SQLite."));
        QSqlQuery manual(db_);
        manual.prepare(
            QStringLiteral("INSERT OR IGNORE INTO subscriptions(id,url,name) VALUES(?,NULL,?)"));
        manual.addBindValue(manualGroupId());
        manual.addBindValue(QStringLiteral("Ручной импорт"));
        execute(manual);
    } catch (...) {
        db_.rollback();
        db_.close();
        db_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName_);
        throw;
    }
}

ProfileRepository::~ProfileRepository() {
    db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName_);
}

QString ProfileRepository::manualGroupId() {
    return QStringLiteral("7400b3e7-12de-49d5-9d50-3ae54cba261e");
}

QString ProfileRepository::ensureSubscription(const QString& url, const QString& name) {
    QSqlQuery existing(db_);
    existing.prepare(QStringLiteral("SELECT id FROM subscriptions WHERE url=?"));
    existing.addBindValue(url);
    execute(existing);
    if (existing.next())
        return existing.value(0).toString();
    if (url.isEmpty() || name.trimmed().isEmpty() || name.size() > 256)
        throw Error(QStringLiteral("Укажите URL и название подписки длиной до 256 символов."));
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery insert(db_);
    insert.prepare(QStringLiteral("INSERT INTO subscriptions(id,url,name) VALUES(?,?,?)"));
    insert.addBindValue(id);
    insert.addBindValue(url);
    insert.addBindValue(name.trimmed());
    execute(insert);
    return id;
}

void ProfileRepository::replaceNodes(const QString& subscriptionId, const QVector<Node>& nodes) {
    if (nodes.isEmpty() || nodes.size() > ProfileParser::MaxNodes)
        throw Error(QStringLiteral("Обновление должно содержать от 1 до 10000 узлов."));
    for (const auto& node : nodes)
        node.validate();
    if (!subscription(subscriptionId))
        throw Error(QStringLiteral("Подписка не найдена."));
    if (!db_.transaction())
        throw Error(QStringLiteral("Не удалось начать обновление SQLite."));
    try {
        QSet<QString> retained;
        for (const auto& node : nodes) {
            const auto fingerprint = node.fingerprint();
            const auto id = QUuid::createUuidV5(QUuid(subscriptionId), fingerprint.toUtf8())
                                .toString(QUuid::WithoutBraces);
            retained.insert(id);
            QSqlQuery upsert(db_);
            upsert.prepare(QStringLiteral(
                "INSERT INTO nodes(id,subscription_id,fingerprint,config) VALUES(?,?,?,?) "
                "ON CONFLICT(subscription_id,fingerprint) DO UPDATE SET config=excluded.config"));
            upsert.addBindValue(id);
            upsert.addBindValue(subscriptionId);
            upsert.addBindValue(fingerprint);
            upsert.addBindValue(
                QString::fromUtf8(QJsonDocument(node.toJson()).toJson(QJsonDocument::Compact)));
            execute(upsert);
        }
        QStringList removed;
        QSqlQuery previous(db_);
        previous.prepare(QStringLiteral("SELECT id FROM nodes WHERE subscription_id=?"));
        previous.addBindValue(subscriptionId);
        execute(previous);
        while (previous.next()) {
            if (!retained.contains(previous.value(0).toString()))
                removed << previous.value(0).toString();
        }
        previous.finish();
        for (const auto& id : removed) {
            QSqlQuery remove(db_);
            remove.prepare(QStringLiteral("DELETE FROM nodes WHERE id=?"));
            remove.addBindValue(id);
            execute(remove);
        }
        QSqlQuery update(db_);
        update.prepare(
            QStringLiteral("UPDATE subscriptions SET status='ok',updated_at=? WHERE id=?"));
        update.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        update.addBindValue(subscriptionId);
        execute(update);
        if (!db_.commit())
            throw Error(QStringLiteral("Не удалось сохранить обновление SQLite."));
    } catch (...) {
        db_.rollback();
        throw;
    }
}

void ProfileRepository::markFetchError(const QString& subscriptionId) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE subscriptions SET status='error' WHERE id=?"));
    query.addBindValue(subscriptionId);
    execute(query);
}

void ProfileRepository::setLabel(const QString& nodeId, const QString& label) {
    if (label.size() > 256 || label.contains('\n') || label.contains(QChar::Null))
        throw Error(QStringLiteral("Некорректная пользовательская метка."));
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE nodes SET label_override=? WHERE id=?"));
    query.addBindValue(label);
    query.addBindValue(nodeId);
    execute(query);
}

QVector<Server> ProfileRepository::servers() const {
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral(
            "SELECT id,subscription_id,config,label_override FROM nodes ORDER BY id")))
        throw Error(QStringLiteral("Не удалось прочитать профили."));
    QVector<Server> result;
    while (query.next()) {
        auto node =
            Node::fromJson(QJsonDocument::fromJson(query.value(2).toString().toUtf8()).object());
        const auto label = query.value(3).toString();
        if (!label.isEmpty())
            node.label = label;
        result.append({query.value(0).toString(), query.value(1).toString(), node});
    }
    return result;
}

QVector<Subscription> ProfileRepository::subscriptions() const {
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("SELECT id,name,url,status,updated_at FROM subscriptions "
                                   "ORDER BY name,id")))
        throw Error(QStringLiteral("Не удалось прочитать подписки."));
    QVector<Subscription> result;
    while (query.next())
        result.append({query.value(0).toString(), query.value(1).toString(),
                       query.value(2).toString(), query.value(3).toString(),
                       query.value(4).toString()});
    return result;
}

std::optional<Subscription> ProfileRepository::subscription(const QString& id) const {
    for (const auto& entry : subscriptions()) {
        if (entry.id == id)
            return entry;
    }
    return std::nullopt;
}

void ProfileRepository::setPreference(const QString& key, const QString& value) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "INSERT INTO preferences(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=?"));
    query.addBindValue(key);
    query.addBindValue(value);
    query.addBindValue(value);
    execute(query);
}

QString ProfileRepository::preference(const QString& key) const {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("SELECT value FROM preferences WHERE key=?"));
    query.addBindValue(key);
    execute(query);
    return query.next() ? query.value(0).toString() : QString();
}

} // namespace sunlens
