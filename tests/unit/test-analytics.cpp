#include "sunlens/analytics.hpp"
#include "sunlens/application-controller.hpp"

#include <QSqlQuery>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>

using namespace sunlens;

namespace {
const TrafficIdentity first{QStringLiteral("023588d5-660e-406d-9e01-8ee327f37283"),
                            QStringLiteral("1fe2aee2-32d2-43d9-b94b-82179f51930d"),
                            QStringLiteral("c2a4b3ff-32d2-43d9-b94b-82179f51930d"),
                            QStringLiteral("sing-box")};
const qint64 at = 1750000000000;
} // namespace

class AnalyticsTests : public QObject {
    Q_OBJECT
  private slots:
    void deltaAndDuplicates() {
        QTemporaryDir directory;
        ProfileRepository profiles(directory.filePath("data.sqlite"));
        AnalyticsRepository analytics(profiles);
        const auto initial = analytics.sample(first, 100, 150, at);
        QCOMPARE(initial.upload, 100);
        QVERIFY(!initial.continuous);
        const auto next = analytics.sample(first, 125, 180, at + 1400);
        QCOMPARE(next.upload, 25);
        QCOMPARE(next.download, 30);
        QCOMPARE(next.intervalMs, 1400);
        QVERIFY(next.continuous);
        QCOMPARE(analytics.sample(first, 125, 180, at + 1500).upload, 0);
        QCOMPARE(analytics.sample(first, 140, 195, at + 1400).upload, 0);
        QCOMPARE(analytics.total().value("upload").toString(), QStringLiteral("125"));
        QCOMPARE(analytics.history().size(), 2);
    }
    void resetsAndSwitchesDoNotDoubleCount() {
        QTemporaryDir directory;
        const auto path = directory.filePath("data.sqlite");
        auto second = first;
        second.session = QStringLiteral("683ca2bc-eb64-4284-b39b-23b937b798e2");
        second.server = QStringLiteral("21a62487-192f-478f-9aaa-3c518ef6b658");
        second.core = QStringLiteral("xray");
        {
            ProfileRepository profiles(path);
            AnalyticsRepository analytics(profiles);
            (void)analytics.sample(first, 1000, 1500, at);
            QVERIFY(!analytics.sample(first, 10, 15, at + 1000).continuous);
            QVERIFY(analytics.sample(first, 50, 60, at + 2000).continuous);
            (void)analytics.sample(second, 12, 18, at + 3000);
            auto wrong = second;
            wrong.session = first.session;
            QVERIFY_EXCEPTION_THROWN((void)analytics.sample(wrong, 1100, 1700, at + 4000), Error);
        }
        ProfileRepository profiles(path);
        AnalyticsRepository analytics(profiles);
        QCOMPARE(analytics.total()["upload"].toString(), QStringLiteral("1052"));
        QCOMPARE(analytics.total()["download"].toString(), QStringLiteral("1563"));
        QCOMPARE(analytics.serverTotals().size(), 2);
        QCOMPARE(analytics.subscriptionTotals().size(), 1);
        QCOMPARE(analytics.subscriptionTotals().first().toMap()["upload"].toString(),
                 QStringLiteral("1052"));
    }
    void retentionKeepsLifetimeCounters() {
        QTemporaryDir directory;
        ProfileRepository profiles(directory.filePath("data.sqlite"));
        AnalyticsRepository analytics(profiles);
        (void)analytics.sample(first, 40, 25, at);
        analytics.event(first, "ready", at + 2);
        analytics.probe(first, 128, at + 86400000 * 2);
        analytics.prune(at + 86400000 * 2, 1);
        QCOMPARE(analytics.history().size(), 1);
        QCOMPARE(analytics.history().first().toMap()["latency"].toString(), QStringLiteral("128"));
        QCOMPARE(analytics.total()["upload"].toString(), QStringLiteral("40"));
        QVERIFY_EXCEPTION_THROWN(analytics.prune(at, 0), Error);
    }
    void tcpProbeUsesLoopbackAndStoresIndependentMeasurement() {
        QTemporaryDir directory;
        ProfileRepository profiles(directory.filePath("data.sqlite"));
        ApplicationController controller(profiles);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const auto url = QStringLiteral("trojan://fixture@127.0.0.1:%1").arg(server.serverPort());
        QVERIFY(controller.importText(url));
        controller.selectServer(controller.servers().first().toMap()["id"].toString());
        controller.probeSelectedServer();
        QTRY_COMPARE(controller.analyticsHistory().size(), 1);
        const auto sample = controller.analyticsHistory().first().toMap();
        QCOMPARE(sample.value("kind").toString(), QStringLiteral("tcp-probe"));
        QVERIFY(sample.value("latency").toString().toInt() >= 0);
        QCOMPARE(controller.analyticsSummary()["upload"].toString(), QStringLiteral("0"));
    }
    void migratesProfileDataFromSchemaOne() {
        QTemporaryDir directory;
        const auto path = directory.filePath("data.sqlite");
        {
            ProfileRepository profiles(path);
            profiles.setPreference("fixture", "preserved");
        }
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "migration-fixture");
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("DROP TABLE analytics_history"));
            QVERIFY(query.exec("DROP TABLE traffic_sessions"));
            QVERIFY(query.exec("PRAGMA user_version=1"));
        }
        QSqlDatabase::removeDatabase("migration-fixture");
        ProfileRepository profiles(path);
        QCOMPARE(profiles.preference("fixture"), QStringLiteral("preserved"));
        AnalyticsRepository analytics(profiles);
        QCOMPARE(analytics.sample(first, 15, 17, at).upload, 15);
    }
};

QTEST_GUILESS_MAIN(AnalyticsTests)
#include "test-analytics.moc"
