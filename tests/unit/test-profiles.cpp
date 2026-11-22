#include "sunlens/application-controller.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace sunlens;

namespace {
const QByteArray Vless =
    "vless://11223344-5566-4788-99aa-bbccddeeff00@example.invalid:443"
    "?encryption=none&security=tls&type=ws&sni=example.invalid&path=%2Fvpn#Fixture";
}

class ProfileTests : public QObject {
    Q_OBJECT
  private slots:
    void valid_data() {
        QTest::addColumn<QByteArray>("input");
        QTest::addColumn<QString>("protocol");
        QTest::newRow("vless-ws") << Vless << QStringLiteral("vless");
        QTest::newRow("base64-list") << Vless.toBase64() << QStringLiteral("vless");
        QTest::newRow("url-base64")
            << Vless.toBase64(QByteArray::Base64UrlEncoding) << QStringLiteral("vless");
        QTest::newRow("trojan") << QByteArray("trojan://fixture%40password@[2001:db8::1]:443#TLS")
                                << QStringLiteral("trojan");
        QTest::newRow("ss-sip002")
            << QByteArray("ss://") + QByteArray("aes-128-gcm:fixture:password").toBase64() +
                   "@example.invalid:8388#SS"
            << QStringLiteral("shadowsocks");
        QTest::newRow("ss-legacy")
            << QByteArray("ss://") +
                   QByteArray("aes-256-gcm:fixture@example.invalid:8388").toBase64() + "#SS"
            << QStringLiteral("shadowsocks");
        QJsonObject vmess{{"v", "2"},
                          {"ps", "Fixture"},
                          {"add", "example.invalid"},
                          {"port", 443},
                          {"id", "11223344-5566-4788-99aa-bbccddeeff00"},
                          {"aid", 0},
                          {"net", "tcp"},
                          {"tls", "tls"},
                          {"scy", "auto"}};
        QTest::newRow("vmess") << QByteArray("vmess://") +
                                      QJsonDocument(vmess).toJson(QJsonDocument::Compact).toBase64()
                               << QStringLiteral("vmess");
    }
    void valid() {
        QFETCH(QByteArray, input);
        QFETCH(QString, protocol);
        const auto nodes = ProfileParser::parse(input);
        QCOMPARE(nodes.size(), 1);
        QCOMPARE(nodes[0].protocol, protocol);
        QCOMPARE(Node::fromJson(nodes[0].toJson()).fingerprint(), nodes[0].fingerprint());
    }
    void invalid_data() {
        QTest::addColumn<QByteArray>("input");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("bad-base64") << QByteArray("%%%");
        QTest::newRow("bad-utf8") << QByteArray("\xff", 1);
        QTest::newRow("yaml") << QByteArray("proxies:\n - name: unsupported");
        QTest::newRow("json") << QByteArray("[{\"server\":\"example.invalid\"}]");
        QTest::newRow("unknown-scheme") << QByteArray("https://example.invalid/list");
        QTest::newRow("unknown-query") << QByteArray(Vless).replace("#Fixture", "&allowInsecure=1");
        QTest::newRow("bad-percent") << QByteArray(Vless).replace("%2Fvpn", "%ZZ");
        QTest::newRow("bad-port") << QByteArray(Vless).replace(":443", ":70000");
        QTest::newRow("bad-uuid") << QByteArray(Vless).replace(
            "11223344-5566-4788-99aa-bbccddeeff00", "invalid");
        QTest::newRow("reality") << QByteArray(Vless).replace("security=tls", "security=reality");
        QTest::newRow("duplicate") << QByteArray(Vless).replace("#Fixture", "&security=none");
        QTest::newRow("grpc") << QByteArray(Vless).replace("type=ws", "type=grpc");
        QTest::newRow("mixed") << Vless + "\nunsupported";
        QTest::newRow("trojan-no-tls")
            << QByteArray("trojan://fixture@example.invalid:443?security=none");
        QTest::newRow("ss-plugin")
            << QByteArray("ss://YWVzLTEyOC1nY206Zml4dHVyZQ==@example.invalid:8388?plugin=obfs");
        QTest::newRow("oversize") << QByteArray(ProfileParser::MaxBytes + 1, 'a');
        QTest::newRow("control-label") << QByteArray(Vless).replace("#Fixture", "#bad%0Alabel");
    }
    void invalid() {
        QFETCH(QByteArray, input);
        QVERIFY_EXCEPTION_THROWN(ProfileParser::parse(input), Error);
    }
    void identityIgnoresLabel() {
        auto a = ProfileParser::parse(Vless).first();
        auto b = a;
        b.label = QStringLiteral("Другая метка");
        QCOMPARE(a.fingerprint(), b.fingerprint());
        b.port = 8443;
        QVERIFY(a.fingerprint() != b.fingerprint());
    }
    void transactionalRefreshAndPersistence() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("profiles.sqlite");
        QString id;
        QString serverId;
        auto nodes = ProfileParser::parse(Vless);
        {
            ProfileRepository repository(path);
            id = repository.ensureSubscription("https://example.invalid/private?token=fixture",
                                               QStringLiteral("Тест"));
            QCOMPARE(repository.ensureSubscription("https://example.invalid/private?token=fixture",
                                                   QStringLiteral("Новое имя")),
                     id);
            repository.replaceNodes(id, nodes);
            serverId = repository.servers().first().id;
            repository.setLabel(serverId, QStringLiteral("Моя метка"));
            nodes[0].label = QStringLiteral("Имя провайдера изменилось");
            repository.replaceNodes(id, nodes);
            QCOMPARE(repository.servers().first().id, serverId);
            QCOMPARE(repository.servers().first().node.label, QStringLiteral("Моя метка"));
            auto invalid = nodes;
            invalid[0].port = 0;
            QVERIFY_EXCEPTION_THROWN(repository.replaceNodes(id, invalid), Error);
            QVERIFY_EXCEPTION_THROWN(repository.replaceNodes(id, {}), Error);
            QCOMPARE(repository.servers().size(), 1);
            repository.setPreference("selected-server", serverId);
        }
        {
            ProfileRepository reopened(path);
            QCOMPARE(reopened.servers().first().id, serverId);
            QCOMPARE(reopened.preference("selected-server"), serverId);
            QCOMPARE(reopened.servers().first().node.label, QStringLiteral("Моя метка"));
            QVERIFY(!reopened.subscription(id)->updatedAt.isEmpty());
        }
#ifdef Q_OS_UNIX
        const auto permissions = QFile::permissions(path);
        QVERIFY(!(permissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                                 QFileDevice::ReadOther | QFileDevice::WriteOther)));
#endif
    }
    void sqlRollbackAfterPartialWrite() {
        QTemporaryDir directory;
        const auto path = directory.filePath("profiles.sqlite");
        ProfileRepository repository(path);
        const auto original = ProfileParser::parse(Vless);
        repository.replaceNodes(ProfileRepository::manualGroupId(), original);
        const auto server = repository.servers().first();
        const auto connection = QStringLiteral("rollback-fixture");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TRIGGER reject_fixture BEFORE INSERT ON nodes "
                               "WHEN json_extract(NEW.config,'$.port')=8443 "
                               "BEGIN SELECT RAISE(ABORT,'fixture'); END"));
        }
        QSqlDatabase::removeDatabase(connection);
        auto update = original;
        update[0].label = QStringLiteral("Не должно сохраниться");
        auto another = original.first();
        another.port = 8443;
        update.append(another);
        QVERIFY_EXCEPTION_THROWN(
            repository.replaceNodes(ProfileRepository::manualGroupId(), update), Error);
        QCOMPARE(repository.servers().size(), 1);
        QCOMPARE(repository.servers().first().node.label, server.node.label);
    }
    void duplicatesAndGrouping() {
        QTemporaryDir directory;
        ProfileRepository repository(directory.filePath("profiles.sqlite"));
        const auto nodes = ProfileParser::parse(Vless + '\n' + Vless);
        repository.replaceNodes(ProfileRepository::manualGroupId(), nodes);
        QCOMPARE(repository.servers().size(), 1);
        const auto remote = repository.ensureSubscription("https://example.invalid/other", "Other");
        repository.replaceNodes(remote, nodes);
        QCOMPARE(repository.servers().size(), 2);
        QVERIFY(repository.servers()[0].id != repository.servers()[1].id);
    }
    void controllerKeepsSecretsOutOfViewModel() {
        QTemporaryDir directory;
        ProfileRepository repository(directory.filePath("profiles.sqlite"));
        ApplicationController controller(repository);
        QVERIFY(controller.importText(QString::fromUtf8(Vless)));
        QCOMPARE(controller.servers().size(), 1);
        const auto map = controller.servers()[0].toMap();
        QVERIFY(!map.contains("credential"));
        QVERIFY(!map.contains("url"));
        controller.selectServer(map["id"].toString());
        QCOMPARE(controller.selectedId(), map["id"].toString());
        QVERIFY(!controller.importText(QString::fromUtf8(Vless) + "\ninvalid"));
        QCOMPARE(controller.servers().size(), 1);
        QVERIFY(controller.addManual("trojan", "manual.invalid", 443, "fixture-password", "", true,
                                     ""));
        QCOMPARE(controller.servers().size(), 2);
    }
};

QTEST_GUILESS_MAIN(ProfileTests)
#include "test-profiles.moc"
