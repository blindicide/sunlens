#include "sunlens/core-adapter.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

using namespace sunlens;

class SingBoxTests : public QObject {
    Q_OBJECT
  private slots:
    void realValidator_data() {
        QTest::addColumn<QByteArray>("uri");
        QTest::newRow("vless-tcp")
            << QByteArray("vless://11223344-5566-4788-99aa-bbccddeeff00@192.0.2.1:443"
                          "?security=tls&sni=example.invalid&flow=xtls-rprx-vision");
        QTest::newRow("vless-ws") << QByteArray(
            "vless://11223344-5566-4788-99aa-bbccddeeff00@example.invalid:443"
            "?security=tls&type=ws&path=%2Fvpn&host=example.invalid");
        QTest::newRow("trojan-ws")
            << QByteArray("trojan://fixture-only@example.invalid:443?type=ws&path=%2Fvpn");
        QTest::newRow("shadowsocks")
            << QByteArray("ss://YWVzLTEyOC1nY206Zml4dHVyZQ==@192.0.2.1:8388");
        const QJsonObject vmess{{"v", "2"},     {"add", "example.invalid"},
                                {"port", 443},  {"id", "11223344-5566-4788-99aa-bbccddeeff00"},
                                {"aid", 0},     {"net", "ws"},
                                {"tls", "tls"}, {"path", "/vpn"}};
        QTest::newRow("vmess-ws") << QByteArray("vmess://") +
                                         QJsonDocument(vmess).toJson().toBase64();
    }
    void realValidator() {
        QFETCH(QByteArray, uri);
        const auto path = qEnvironmentVariable("SUNLENS_SING_BOX_EXECUTABLE");
        QVERIFY2(!path.isEmpty(), "Требуется путь к закреплённому ядру");
        SingBoxAdapter adapter;
        const auto node = ProfileParser::parse(uri).first();
        for (const auto mode : {RoutingMode::Proxy, RoutingMode::Direct, RoutingMode::Rules}) {
            CoreOptions options;
            options.routing = mode;
            options.ipv6 = mode != RoutingMode::Direct;
            options.bypassLan = mode != RoutingMode::Direct;
            if (mode == RoutingMode::Rules)
                options.rules = {{RuleKind::DomainSuffix, "example.org", true},
                                 {RuleKind::IpCidr, "192.0.2.0/24", false}};
            const auto result = adapter.validate(path, adapter.configuration(node, options));
            QVERIFY2(result.valid, qPrintable(result.message));
            QCOMPARE(result.coreVersion, adapter.capabilities().pinnedVersion);
        }
    }
    void invalidConfigDoesNotExposeSecrets() {
        const auto path = qEnvironmentVariable("SUNLENS_SING_BOX_EXECUTABLE");
        const auto result = SingBoxAdapter().validate(
            path, QJsonDocument(QJsonObject{{"unknown", "fixture-sensitive-marker"}}));
        QVERIFY(!result.valid);
        QVERIFY(!result.message.contains("fixture-sensitive-marker"));
        QVERIFY(!result.coreVersion.isEmpty());
    }
};

QTEST_GUILESS_MAIN(SingBoxTests)
#include "test-sing-box.moc"
