#include "proxy.hpp"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace sunlens {
namespace {

QString proxyHost(QString host) {
    host = host.trimmed();
    if (host.startsWith(u'[') && host.endsWith(u']'))
        host = host.sliced(1, host.size() - 2);
    if (!host.contains(u'%')) {
        QHostAddress address;
        if (address.setAddress(host))
            return address.toString();
    }
    static const QRegularExpression domain(
        uR"(\A[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?(?:\.[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)*\.?\z)"_s,
        QRegularExpression::CaseInsensitiveOption);
    const auto ascii = QString::fromLatin1(QUrl::toAce(host));
    return ascii.size() <= 253 && domain.match(ascii).hasMatch() ? ascii : QString{};
}

bool validEndpoint(const QString& protocol, const QString& host, int port) {
    return (protocol == u"http" || protocol == u"socks5") && !host.isEmpty() && port >= 1 &&
           port <= 65535;
}

} // namespace

QByteArray tunConfig(const QString& protocol, const QString& host, int port) {
    const auto normalized = proxyHost(host);
    if (!validEndpoint(protocol, normalized, port) || QHostAddress(normalized).isNull())
        return {};

    const bool http = protocol == u"http";
    QJsonObject outbound{{u"type"_s, http ? u"http"_s : u"socks"_s},
                         {u"tag"_s, u"upstream"_s},
                         {u"server"_s, normalized},
                         {u"server_port"_s, port}};
    if (!http)
        outbound.insert(u"version"_s, u"5"_s);

    const QJsonObject tun{
        {u"type"_s, u"tun"_s},
        {u"address"_s, QJsonArray{u"172.19.0.1/30"_s, u"fdfe:dcba:9876::1/126"_s}},
        {u"auto_route"_s, true},
        {u"strict_route"_s, true},
        {u"dns_mode"_s, u"hijack"_s}};
    const QJsonObject dnsServer{{u"type"_s, u"https"_s},
                                {u"tag"_s, u"doh"_s},
                                {u"server"_s, u"1.1.1.1"_s},
                                {u"detour"_s, u"upstream"_s}};
    QJsonArray rules{QJsonObject{{u"port"_s, 53}, {u"action"_s, u"hijack-dns"_s}}};
    if (http)
        rules.append(QJsonObject{{u"network"_s, u"udp"_s}, {u"action"_s, u"reject"_s}});

    const QJsonObject route{
        {u"auto_detect_interface"_s, true}, {u"rules"_s, rules}, {u"final"_s, u"upstream"_s}};
    return QJsonDocument(QJsonObject{
                             {u"log"_s, QJsonObject{{u"level"_s, u"warn"_s}}},
                             {u"dns"_s, QJsonObject{{u"servers"_s, QJsonArray{dnsServer}}}},
                             {u"inbounds"_s, QJsonArray{tun}},
                             {u"outbounds"_s, QJsonArray{outbound}},
                             {u"route"_s, route},
                         })
        .toJson();
}

ProxyController::ProxyController(QObject* parent) : QObject(parent) {}

bool ProxyController::busy() const { return !reply_.isNull(); }

QString ProxyController::status() const { return status_; }

void ProxyController::setStatus(const QString& status) {
    status_ = status;
    emit changed();
}

void ProxyController::test(const QString& protocol, const QString& host, int port,
                           const QString& username, const QString& password) {
    if (busy())
        return;
    const auto normalized = proxyHost(host);
    if (!validEndpoint(protocol, normalized, port)) {
        setStatus(tr("Введите адрес прокси и порт от 1 до 65535."));
        return;
    }
    if (!password.isEmpty() && username.isEmpty()) {
        setStatus(tr("Для пароля требуется имя пользователя."));
        return;
    }
    if (protocol == u"socks5" &&
        (username.toUtf8().size() > 255 || password.toUtf8().size() > 255)) {
        setStatus(tr("Имя пользователя и пароль SOCKS5 должны быть не длиннее 255 байт."));
        return;
    }

    auto* network = new QNetworkAccessManager(this);
    const auto type = protocol == u"http" ? QNetworkProxy::HttpProxy : QNetworkProxy::Socks5Proxy;
    network->setProxy(
        QNetworkProxy(type, normalized, static_cast<quint16>(port), username, password));
    QNetworkRequest request(QUrl(u"https://www.google.com/generate_204"_s));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    elapsed_.start();
    auto* reply = network->get(request);
    reply_ = reply;
    reply->setReadBufferSize(4096);
    connect(reply, &QNetworkReply::readyRead, reply, [reply] { reply->readAll(); });
    auto* timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply] {
        reply->setProperty("sunlensTimedOut", true);
        reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, network, reply, timeout] {
        timeout->stop();
        const auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString result;
        if (reply->error() == QNetworkReply::NoError && code == 204)
            result = tr("HTTPS через прокси работает (%1 мс).").arg(elapsed_.elapsed());
        else if (reply->property("sunlensTimedOut").toBool())
            result = tr("Время ожидания ответа истекло.");
        else if (reply->error() == QNetworkReply::ProxyAuthenticationRequiredError || code == 407)
            result = tr("Прокси отклонил данные аутентификации или требует входа.");
        else if (reply->error() == QNetworkReply::SslHandshakeFailedError)
            result = tr("Не удалось установить доверенное HTTPS-соединение.");
        else if (code != 0)
            result = tr("Сайт проверки вернул HTTP %1 вместо 204.").arg(code);
        else
            result = tr("Не удалось получить HTTPS-ответ через выбранный прокси.");

        reply_.clear();
        network->deleteLater();
        setStatus(result);
    });
    timeout->start(10000);
    setStatus(tr("Проверяем HTTPS через прокси…"));
}

void ProxyController::exportTun(const QUrl& file, const QString& protocol, const QString& host,
                                int port) {
    if (busy())
        return;
    const auto json = tunConfig(protocol, host, port);
    if (json.isEmpty()) {
        setStatus(tr("Для экспорта нужен IP-адрес прокси и порт от 1 до 65535."));
        return;
    }
    if (!file.isLocalFile()) {
        setStatus(tr("Выберите локальный файл JSON."));
        return;
    }
    QSaveFile output(file.toLocalFile());
    if (!output.open(QIODevice::WriteOnly) || output.write(json) != json.size() ||
        !output.commit()) {
        setStatus(tr("Не удалось сохранить конфигурацию."));
        return;
    }
    setStatus(tr("Конфигурация сохранена без учётных данных. TUN не запущен."));
}

} // namespace sunlens
