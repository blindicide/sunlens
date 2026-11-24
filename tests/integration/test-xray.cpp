#include "sunlens/core-adapter.hpp"

#include <QJsonArray>
#include <QtTest>

using namespace sunlens;

class XrayTests : public QObject {
    Q_OBJECT
  private slots:
    void coreAcceptsIndependentSchema() {
        const auto core = qEnvironmentVariable("SUNLENS_XRAY_EXECUTABLE");
        auto nodes = ProfileParser::parse(
            "vless://11223344-5566-4788-99aa-bbccddeeff00@192.0.2.1:443?security=tls"
            "&flow=xtls-rprx-vision&sni=example.invalid\n"
            "trojan://fixture@192.0.2.1:443?type=ws&path=%2Fvpn\n"
            "ss://YWVzLTEyOC1nY206Zml4dHVyZQ==@192.0.2.1:8388");
        auto vmess = nodes.first();
        vmess.protocol = QStringLiteral("vmess");
        vmess.method = QStringLiteral("auto");
        vmess.flow.clear();
        nodes.append(vmess);
        XrayAdapter adapter;
        CoreOptions options;
        options.dns = {"192.0.2.53", 53, false, {}};
        options.routing = RoutingMode::Rules;
        options.rules = {{RuleKind::DomainSuffix, "example.invalid", true},
                         {RuleKind::IpCidr, "192.0.2.0/24", false}};
        for (const auto& node : nodes) {
            const auto result = adapter.validate(core, adapter.configuration(node, options));
            QVERIFY2(result.valid, qPrintable(result.message));
            QCOMPARE(result.coreVersion, adapter.capabilities().pinnedVersion);
        }
        QVERIFY(!adapter.validate(core, QJsonDocument(QJsonObject{{"inbounds", QJsonArray{false}}}))
                     .valid);
    }
    void unsupportedDnsAndBootstrapRejected() {
        XrayAdapter adapter;
        auto node = ProfileParser::parse("trojan://fixture@example.invalid:443").first();
        CoreOptions options;
        QVERIFY_EXCEPTION_THROWN(adapter.configuration(node, options), Error);
        options.dns = {"192.0.2.53", 53, false, {}};
        QVERIFY_EXCEPTION_THROWN(adapter.configuration(node, options), Error);
    }
};

QTEST_GUILESS_MAIN(XrayTests)
#include "test-xray.moc"
