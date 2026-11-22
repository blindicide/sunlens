#include "sunlens/tunnel.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace sunlens;

namespace {

TunnelLease fixtureLease() {
    return {"23d9238e-848a-4de7-9cb6-7517dd48cd82",
            "678dbf84-d7cf-40fb-9972-062110728546",
            "sln23d9238e",
            "100.96.0.1/30",
            "fdaa:abcd::1/126",
            50000,
            16000,
            0,
            0,
            {},
            {}};
}

TunnelRequest fixtureRequest() {
    TunnelRequest request;
    request.serverId = QStringLiteral("b6d532a4-82a6-4390-ab6d-17cfe7f468e7");
    request.subscriptionId = QStringLiteral("e5e0cb97-cf16-4d13-9bc9-ad6da8182c61");
    request.node = ProfileParser::parse("trojan://fixture@example.invalid:443").first();
    return request;
}

class FakePlatform final : public ITunnelPlatform {
  public:
    bool alive = false;
    bool canPrepare = true;
    bool canRestore = true;
    bool canLaunch = true;
    int restoreCount = 0;
    QJsonObject snapshot() override { return {}; }
    TunnelLease reserve(const QJsonObject&) override { return fixtureLease(); }
    void launch(const TunnelRequest&, TunnelLease&) override {
        alive = true;
        if (!canLaunch)
            throw Error(QStringLiteral("Ошибка запуска fixture."));
    }
    bool ready(TunnelLease&) override { return canPrepare; }
    bool running() const override { return alive; }
    void shutdown(const TunnelLease&) override { alive = false; }
    void restore(const TunnelLease&) override {
        ++restoreCount;
        if (!canRestore)
            throw Error(QStringLiteral("Ошибка восстановления fixture."));
    }
    TrafficSample counters(const TunnelLease&) override { return {true, 123, 456}; }
};

} // namespace

class TunnelTests : public QObject {
    Q_OBJECT
  private slots:
    void strictRequestBoundary() {
        const auto request = fixtureRequest();
        auto json = request.toJson();
        QCOMPARE(TunnelRequest::fromJson(json).node.fingerprint(), request.node.fingerprint());
        json.insert("command", "arbitrary-root-command");
        QVERIFY_EXCEPTION_THROWN(TunnelRequest::fromJson(json), Error);
        json = request.toJson();
        json["core"] = "unreviewed-core";
        QVERIFY_EXCEPTION_THROWN(TunnelRequest::fromJson(json), Error);
        json = request.toJson();
        auto options = json["options"].toObject();
        options["executable"] = "/tmp/untrusted";
        json["options"] = options;
        QVERIFY_EXCEPTION_THROWN(TunnelRequest::fromJson(json), Error);
    }
    void journalRoundTripAndPermissions() {
        QTemporaryDir directory;
        RecoveryJournal journal(directory.path());
        QVERIFY(!journal.load());
        journal.save(fixtureLease(), "prepared");
        QCOMPARE(journal.load()->id, fixtureLease().id);
        QFile file(directory.filePath("recovery.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("credential"));
        file.close();
#ifdef Q_OS_UNIX
        QVERIFY(!(file.permissions() & (QFileDevice::ReadOther | QFileDevice::WriteOther |
                                        QFileDevice::ReadGroup | QFileDevice::WriteGroup)));
#endif
        journal.clear();
        QVERIFY(!journal.load());
    }
    void corruptJournalNeverAuthorizesCleanup() {
        QTemporaryDir directory;
        RecoveryJournal journal(directory.path());
        QFile file(directory.filePath("recovery.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{invalid");
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        file.close();
        QVERIFY_EXCEPTION_THROWN(journal.load(), Error);
        auto backend = std::make_unique<FakePlatform>();
        auto* observed = backend.get();
        TunnelManager manager(std::move(backend), directory.path());
        QVERIFY(!manager.recover());
        QCOMPARE(observed->restoreCount, 0);
        QCOMPARE(manager.status()["state"].toString(), QStringLiteral("recovery-required"));
    }
    void preparedJournalRejectsForeignResources() {
        auto lease = fixtureLease();
        auto json = lease.toJson();
        json["interfaceName"] = "../../foreign";
        QVERIFY_EXCEPTION_THROWN(TunnelLease::fromJson(json), Error);
        json = lease.toJson();
        json["routeTable"] = 254;
        QVERIFY_EXCEPTION_THROWN(TunnelLease::fromJson(json), Error);
    }
    void lifecycleAndCounterHonesty() {
        QTemporaryDir directory;
        auto backend = std::make_unique<FakePlatform>();
        auto* observed = backend.get();
        TunnelManager manager(std::move(backend), directory.path());
        QVERIFY(manager.recover());
        QVERIFY(manager.start(fixtureRequest()));
        QCOMPARE(manager.status()["state"].toString(), QStringLiteral("starting"));
        QTRY_COMPARE(manager.status()["state"].toString(), QStringLiteral("ready"));
        QCOMPARE(manager.status()["reachability"].toString(), QStringLiteral("unverified"));
        QCOMPARE(manager.status()["upload"].toString(), QStringLiteral("123"));
        QVERIFY(!manager.start(fixtureRequest()));
        QVERIFY(manager.stop());
        QCOMPARE(observed->restoreCount, 1);
        QVERIFY(!RecoveryJournal(directory.path()).load());
        QVERIFY(manager.stop());
        QCOMPARE(observed->restoreCount, 1);
        QVERIFY(manager.start(fixtureRequest()));
        QVERIFY(manager.stop());
    }
    void launchFailureUnwinds() {
        QTemporaryDir directory;
        auto backend = std::make_unique<FakePlatform>();
        auto* observed = backend.get();
        observed->canLaunch = false;
        TunnelManager manager(std::move(backend), directory.path());
        QVERIFY(!manager.start(fixtureRequest()));
        QVERIFY(!observed->alive);
        QCOMPARE(observed->restoreCount, 1);
        QCOMPARE(manager.status()["state"].toString(), QStringLiteral("error"));
        QVERIFY(!RecoveryJournal(directory.path()).load());
    }
    void unexpectedExitAndFailedRestoreKeepsJournal() {
        QTemporaryDir directory;
        auto backend = std::make_unique<FakePlatform>();
        auto* observed = backend.get();
        TunnelManager manager(std::move(backend), directory.path());
        QVERIFY(manager.start(fixtureRequest()));
        observed->canRestore = false;
        observed->alive = false;
        QTRY_COMPARE(manager.status()["state"].toString(), QStringLiteral("recovery-required"));
        QVERIFY(RecoveryJournal(directory.path()).load());
        QVERIFY(!manager.start(fixtureRequest()));
        observed->canRestore = true;
        QVERIFY(manager.recover());
        QVERIFY(!RecoveryJournal(directory.path()).load());
    }
    void restartRecoveryIsIdempotent() {
        QTemporaryDir directory;
        RecoveryJournal(directory.path()).save(fixtureLease(), "starting");
        auto backend = std::make_unique<FakePlatform>();
        auto* observed = backend.get();
        TunnelManager manager(std::move(backend), directory.path());
        QVERIFY(manager.recover());
        QCOMPARE(observed->restoreCount, 1);
        QVERIFY(manager.recover());
        QCOMPARE(observed->restoreCount, 1);
    }
};

QTEST_GUILESS_MAIN(TunnelTests)
#include "test-tunnel.moc"
