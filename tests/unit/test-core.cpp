#include "sunlens/core-adapter.hpp"

#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

using namespace sunlens;

class CoreTests : public QObject {
    Q_OBJECT
  private:
    static Node fixture() {
        return ProfileParser::parse("trojan://fixture-only@example.invalid:443#Fixture").first();
    }

  private slots:
    void capabilitiesDoNotClaimTunOrStatistics() {
        const auto caps = SingBoxAdapter().capabilities();
        QCOMPARE(caps.name, QStringLiteral("sing-box"));
        QVERIFY(!caps.pinnedVersion.isEmpty());
        QVERIFY(!caps.verifiedTun);
        QVERIFY(!caps.trafficCounters);
        QCOMPARE(caps.configurationTargets, QStringList{"linux"});
    }
    void routingAndProtocol_data() {
        QTest::addColumn<QByteArray>("uri");
        QTest::addColumn<QString>("protocol");
        QTest::newRow("trojan") << QByteArray("trojan://fixture-only@example.invalid:443")
                                << QStringLiteral("trojan");
        QTest::newRow("vless") << QByteArray(
                                      "vless://11223344-5566-4788-99aa-bbccddeeff00@192.0.2.1:443"
                                      "?security=tls&flow=xtls-rprx-vision")
                               << QStringLiteral("vless");
        QTest::newRow("shadowsocks")
            << QByteArray("ss://YWVzLTEyOC1nY206Zml4dHVyZQ==@192.0.2.1:8388")
            << QStringLiteral("shadowsocks");
    }
    void routingAndProtocol() {
        QFETCH(QByteArray, uri);
        QFETCH(QString, protocol);
        const auto node = ProfileParser::parse(uri).first();
        const auto config = SingBoxAdapter().configuration(node, {}).object();
        const auto tun = config["inbounds"].toArray().first().toObject();
        QCOMPARE(tun["type"].toString(), QStringLiteral("tun"));
        QVERIFY(tun["auto_route"].toBool());
        QVERIFY(tun["auto_redirect"].toBool());
        QVERIFY(tun["strict_route"].toBool());
        QCOMPARE(config["outbounds"].toArray().first().toObject()["type"].toString(), protocol);
        const auto route = config["route"].toObject();
        QVERIFY(route["auto_detect_interface"].toBool());
        QCOMPARE(route["default_domain_resolver"].toString(), QStringLiteral("bootstrap"));
        QCOMPARE(route["final"].toString(), QStringLiteral("proxy"));
        QVERIFY(config["log"].toObject()["disabled"].toBool());
        QVERIFY(!config.contains("experimental"));
    }
    void modesLanIpv6AndRules() {
        SingBoxAdapter adapter;
        CoreOptions options;
        options.routing = RoutingMode::Direct;
        options.bypassLan = false;
        options.ipv6 = false;
        auto config = adapter.configuration(fixture(), options).object();
        QCOMPARE(config["route"].toObject()["final"].toString(), QStringLiteral("direct"));
        QVERIFY(!config["inbounds"].toArray().first().toObject().contains("route_exclude_address"));
        QCOMPARE(config["dns"].toObject()["strategy"].toString(), QStringLiteral("ipv4_only"));
        const auto rules = config["route"].toObject()["rules"].toArray();
        QCOMPARE(rules.first().toObject()["ip_version"].toInt(), 6);
        QCOMPARE(rules.first().toObject()["action"].toString(), QStringLiteral("reject"));
        options.routing = RoutingMode::Rules;
        options.rules = {{RuleKind::Domain, "example.invalid", true},
                         {RuleKind::DomainSuffix, "example.org", false},
                         {RuleKind::IpCidr, "192.0.2.0/24", true}};
        config = adapter.configuration(fixture(), options).object();
        const auto translated = config["route"].toObject()["rules"].toArray();
        QVERIFY(translated[4].toObject().contains("domain"));
        QVERIFY(translated[5].toObject().contains("domain_suffix"));
        QVERIFY(translated[6].toObject().contains("ip_cidr"));
        options.routing = RoutingMode::Proxy;
        QVERIFY_EXCEPTION_THROWN(adapter.configuration(fixture(), options), Error);
        options.routing = RoutingMode::Rules;
        options.rules = {{RuleKind::IpCidr, "bad/99", true}};
        QVERIFY_EXCEPTION_THROWN(adapter.configuration(fixture(), options), Error);
    }
    void unknownBinaryIsNotExecuted() {
        QTemporaryDir directory;
        const auto path = directory.filePath("untrusted");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\nexit 0\n");
        file.close();
        QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                    QFileDevice::ExeOwner));
        const auto result =
            SingBoxAdapter().validate(path, SingBoxAdapter().configuration(fixture(), {}));
        QVERIFY(!result.valid);
        QVERIFY(result.coreVersion.isEmpty());
    }
};

QTEST_GUILESS_MAIN(CoreTests)
#include "test-core.moc"
