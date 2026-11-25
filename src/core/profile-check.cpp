#include "sunlens/profile-check.hpp"

#include "sunlens/core-dependencies.hpp"
#include "sunlens/external-core.hpp"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QProcess>
#include <QSslSocket>
#include <QTcpServer>
#include <QTimer>
#include <QUuid>

namespace sunlens {
namespace {

#ifdef Q_OS_LINUX
class CheckProcess : public QProcess {
  public:
    ~CheckProcess() override {
        if (state() != QProcess::NotRunning) {
            kill();
            waitForFinished(1000);
        }
    }
};

bool waitForListener(QProcess& process, const QByteArray& marker) {
    const QDeadlineTimer deadline(2500);
    QByteArray output;
    do {
        output += process.read(16385 - output.size());
        if (output.size() > 16384 || process.state() != QProcess::Running)
            return false;
        if (output.contains(marker))
            return true;
    } while (!deadline.hasExpired() && process.waitForReadyRead(int(deadline.remainingTime())));
    return false;
}

ProfileCheckResult requestThroughCore(QProcess& core, const QUrl& endpoint, QSslConfiguration tls,
                                      quint16 port, const QString& user, const QString& password) {
    QNetworkAccessManager manager;
    QNetworkProxy proxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"), port, user,
                        password);
    proxy.setCapabilities(QNetworkProxy::TunnelingCapability |
                          QNetworkProxy::HostNameLookupCapability);
    manager.setProxy(proxy);

    QNetworkRequest request(endpoint);
    tls.setPeerVerifyMode(QSslSocket::VerifyPeer);
    request.setSslConfiguration(tls);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);

    QElapsedTimer elapsed;
    elapsed.start();
    auto* reply = manager.get(request);
    reply->setReadBufferSize(4096);
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    deadline.setTimerType(Qt::PreciseTimer);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&core, &QProcess::finished, &loop, &QEventLoop::quit);
    deadline.start(8000);
    if (!reply->isFinished() && core.state() == QProcess::Running)
        loop.exec();
    if (core.state() == QProcess::Running && deadline.isActive() && reply->isFinished() &&
        reply->error() == QNetworkReply::NoError &&
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 204) {
        const auto ms = elapsed.elapsed();
        return {QStringLiteral("HTTPS через профиль: %1 мс.").arg(ms), ms};
    }
    const bool timedOut = !deadline.isActive();
    if (!reply->isFinished())
        reply->abort();
    return {timedOut
                ? QStringLiteral("Профиль не подтверждён: нет ответа за 8 секунд.")
                : QStringLiteral("Профиль не подтверждён: не получен ответ HTTPS 204 через VPN.")};
}
#endif

} // namespace

ProfileCheckResult checkProfile(const QString& core, const QString& executable, const Node& node,
                                const QUrl& endpoint, const QSslConfiguration& tls) {
#ifndef Q_OS_LINUX
    Q_UNUSED(core)
    Q_UNUSED(executable)
    Q_UNUSED(node)
    Q_UNUSED(endpoint)
    Q_UNUSED(tls)
    return {QStringLiteral("Проверка профиля пока доступна только в Linux.")};
#else
    try {
        if (core != "sing-box" && core != "xray")
            throw Error(QStringLiteral("Неизвестное ядро для проверки профиля."));
        if (endpoint.scheme() != "https" || endpoint.host().isEmpty() ||
            !endpoint.userInfo().isEmpty() || !QSslSocket::supportsSsl())
            throw Error(QStringLiteral("Клиент HTTPS для проверки профиля недоступен."));
        const bool xray = core == "xray";
        requirePinnedCoreBinary(executable, xray ? XrayLinuxHash : SingBoxLinuxHash);

        // Временный SOCKS доступен только на loopback и требует случайные учётные данные.
        QTcpServer reservation;
        reservation.setProxy(QNetworkProxy::NoProxy);
        if (!reservation.listen(QHostAddress::LocalHost))
            throw Error(QStringLiteral("Не удалось выделить локальный порт для проверки профиля."));
        const auto port = reservation.serverPort();
        const auto user = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto password = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto config = xray ? XrayAdapter::profileCheckConfig(node, port, user, password)
                                 : SingBoxAdapter::profileCheckConfig(node, port, user, password);
        const auto input = config.toJson(QJsonDocument::Compact);
        CheckProcess process;
        QProcessEnvironment environment;
        environment.insert(QStringLiteral("LANG"), QStringLiteral("C.UTF-8"));
        process.setProcessEnvironment(environment);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.setProgram(executable);
        process.setArguments(xray ? QStringList{"run", "-config", "stdin:"}
                                  : QStringList{"run", "--disable-color", "-c", "/dev/stdin"});
        reservation.close();
        process.start();
        if (!process.waitForStarted(1500) || process.write(input) != input.size())
            throw Error(QStringLiteral("Не удалось запустить ядро для проверки профиля."));
        process.closeWriteChannel();
        // Убедимся, что именно дочернее ядро заняло порт. Сырой вывод не записывается.
        const auto marker =
            xray ? QByteArray("[Warning] core: Xray ") + XrayVersion.toUtf8() + " started\n"
                 : QByteArray("inbound/socks[profile-check]: tcp server started at 127.0.0.1:") +
                       QByteArray::number(port) + '\n';
        if (!waitForListener(process, marker))
            throw Error(QStringLiteral("Ядро не открыло локальный порт для проверки профиля."));
        process.closeReadChannel(QProcess::StandardOutput);
        return requestThroughCore(process, endpoint, tls, port, user, password);
    } catch (const Error& error) {
        return {QString::fromUtf8(error.what())};
    }
#endif
}

} // namespace sunlens
