#include "sunlens/subscription-manager.hpp"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

using namespace sunlens;

class SubscriptionTests : public QObject {
    Q_OBJECT
  private:
    static QByteArray fixture() { return "trojan://fixture-only@example.invalid:443#Fixture"; }
    static void respond(QTcpSocket* socket, int status, const QByteArray& body) {
        socket->write("HTTP/1.1 " + QByteArray::number(status) +
                      " Test\r\nConnection: close\r\nContent-Length: " +
                      QByteArray::number(body.size()) + "\r\n\r\n" + body);
        socket->disconnectFromHost();
    }
    static void onRequest(QTcpServer& server, const std::function<void(QTcpSocket*)>& handler) {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, handler] {
            while (server.hasPendingConnections()) {
                auto* socket = server.nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                auto handled = std::make_shared<bool>(false);
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                 [socket, buffer, handled, handler] {
                                     *buffer += socket->readAll();
                                     if (!*handled && buffer->contains("\r\n\r\n")) {
                                         *handled = true;
                                         handler(socket);
                                     }
                                 });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

  private slots:
    void fetchAndMalformedRefreshPreservesWorkingSet() {
        QTemporaryDir directory;
        ProfileRepository repository(directory.filePath("profiles.sqlite"));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray body = fixture();
        onRequest(server, [&body](QTcpSocket* socket) { respond(socket, 200, body); });
        SubscriptionManager manager(repository, {2000, 0, 4096, true});
        QSignalSpy completed(&manager, &SubscriptionManager::completed);
        QSignalSpy failed(&manager, &SubscriptionManager::failed);
        manager.fetch(QString("http://127.0.0.1:%1/list?token=fixture").arg(server.serverPort()),
                      QStringLiteral("Локальный тест"));
        QTRY_COMPARE(completed.size(), 1);
        QVERIFY(!manager.busy());
        QCOMPARE(repository.servers().size(), 1);
        const auto old = repository.servers().first();
        body += "\nunsupported";
        manager.refresh(old.subscriptionId);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(repository.servers().size(), 1);
        QCOMPARE(repository.servers().first().id, old.id);
        QCOMPARE(repository.subscription(old.subscriptionId)->status, QStringLiteral("error"));
        QVERIFY(!failed.first().first().toString().contains("token"));
    }
    void retryOnServerFailure() {
        QTemporaryDir directory;
        ProfileRepository repository(directory.filePath("profiles.sqlite"));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int requests = 0;
        onRequest(server, [&requests](QTcpSocket* socket) {
            ++requests;
            respond(socket, requests == 1 ? 503 : 200, fixture());
        });
        SubscriptionManager manager(repository, {2000, 2, 4096, true});
        QSignalSpy completed(&manager, &SubscriptionManager::completed);
        manager.fetch(QString("http://127.0.0.1:%1/list").arg(server.serverPort()), "Fixture");
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(requests, 2);
    }
    void oversize_data() {
        QTest::addColumn<bool>("announced");
        QTest::newRow("content-length") << true;
        QTest::newRow("streamed") << false;
    }
    void oversize() {
        QFETCH(bool, announced);
        QTemporaryDir directory;
        ProfileRepository repository(directory.filePath("profiles.sqlite"));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        onRequest(server, [announced](QTcpSocket* socket) {
            if (announced)
                respond(socket, 200, QByteArray(256, 'a'));
            else {
                socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n" +
                              QByteArray(256, 'a'));
                socket->disconnectFromHost();
            }
        });
        SubscriptionManager manager(repository, {2000, 0, 64, true});
        QSignalSpy failed(&manager, &SubscriptionManager::failed);
        manager.fetch(QString("http://127.0.0.1:%1/list").arg(server.serverPort()), "Fixture");
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(repository.servers().isEmpty());
    }
    void timeoutAndCancel_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("deadline") << false;
        QTest::newRow("cancel") << true;
    }
    void timeoutAndCancel() {
        QFETCH(bool, cancel);
        QTemporaryDir directory;
        ProfileRepository repository(directory.filePath("profiles.sqlite"));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        onRequest(server, [](QTcpSocket*) {});
        SubscriptionManager manager(repository, {50, 0, 4096, true});
        QSignalSpy failed(&manager, &SubscriptionManager::failed);
        manager.fetch(QString("http://127.0.0.1:%1/list").arg(server.serverPort()), "Fixture");
        if (cancel)
            manager.cancel();
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(!manager.busy());
        QVERIFY(repository.servers().isEmpty());
    }
    void rejectUnsafeUrl_data() {
        QTest::addColumn<QString>("url");
        QTest::newRow("plain-http") << QStringLiteral("http://example.invalid/private");
        QTest::newRow("userinfo") << QStringLiteral("https://user:secret@example.invalid/list");
        QTest::newRow("fragment") << QStringLiteral("https://example.invalid/list#secret");
        QTest::newRow("file") << QStringLiteral("file:///etc/passwd");
        QTest::newRow("bad-url") << QStringLiteral("not a URL");
    }
    void rejectUnsafeUrl() {
        QFETCH(QString, url);
        QTemporaryDir directory;
        ProfileRepository repository(directory.filePath("profiles.sqlite"));
        SubscriptionManager manager(repository);
        QSignalSpy failed(&manager, &SubscriptionManager::failed);
        manager.fetch(url, "Fixture");
        QCOMPARE(failed.size(), 1);
        QVERIFY(!failed.first().first().toString().contains("secret"));
        QVERIFY(!manager.busy());
    }
    void redirectIsNotFollowed() {
        QTemporaryDir directory;
        ProfileRepository repository(directory.filePath("profiles.sqlite"));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int requests = 0;
        onRequest(server, [&requests](QTcpSocket* socket) {
            ++requests;
            socket->write("HTTP/1.1 302 Found\r\nLocation: https://example.invalid/secret\r\n"
                          "Connection: close\r\nContent-Length: 0\r\n\r\n");
            socket->disconnectFromHost();
        });
        SubscriptionManager manager(repository, {2000, 0, 4096, true});
        QSignalSpy failed(&manager, &SubscriptionManager::failed);
        manager.fetch(QString("http://127.0.0.1:%1/list").arg(server.serverPort()), "Fixture");
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(requests, 1);
    }
    void destructionDuringFetch() {
        QTemporaryDir directory;
        ProfileRepository repository(directory.filePath("profiles.sqlite"));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        onRequest(server, [](QTcpSocket*) {});
        auto manager =
            std::make_unique<SubscriptionManager>(repository, FetchPolicy{2000, 0, 4096, true});
        manager->fetch(QString("http://127.0.0.1:%1/list").arg(server.serverPort()), "Fixture");
        QVERIFY(manager->busy());
        manager.reset();
        QCoreApplication::processEvents();
        QVERIFY(repository.servers().isEmpty());
    }
};

QTEST_GUILESS_MAIN(SubscriptionTests)
#include "test-subscriptions.moc"
