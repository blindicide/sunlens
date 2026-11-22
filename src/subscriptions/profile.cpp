#include "sunlens/profile.hpp"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

namespace sunlens {
namespace {

QString utf8(const QByteArray& bytes) {
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder(bytes);
    if (decoder.hasError() || text.contains(QChar::Null))
        throw Error(QStringLiteral("Ожидается корректный текст UTF-8 без нулевых символов."));
    return text;
}

QByteArray decode64(QByteArray encoded) {
    encoded = encoded.trimmed();
    encoded.replace("\r", "");
    encoded.replace("\n", "");
    if (encoded.isEmpty())
        throw Error(QStringLiteral("Пустые данные base64."));
    const auto flags =
        (encoded.contains('-') || encoded.contains('_') ? QByteArray::Base64UrlEncoding
                                                        : QByteArray::Base64Encoding) |
        QByteArray::AbortOnBase64DecodingErrors;
    const auto result = QByteArray::fromBase64Encoding(encoded, flags);
    if (!result)
        throw Error(QStringLiteral("Повреждённые данные base64."));
    return result.decoded;
}

void checkFields(const QJsonObject& object, const QStringList& allowed) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!allowed.contains(it.key()))
            throw Error(
                QStringLiteral("Неподдержанный параметр профиля. См. совместимость форматов."));
    }
}

QString field(const QJsonObject& object, const QString& name) {
    const auto value = object.value(name);
    if (value.isUndefined())
        return {};
    if (value.isString())
        return value.toString();
    if (value.isDouble() && value.toDouble() == value.toInt())
        return QString::number(value.toInt());
    throw Error(QStringLiteral("Неверный тип поля профиля."));
}

int parsePort(const QString& value) {
    bool ok = false;
    const int port = value.toInt(&ok);
    if (!ok || port < 1 || port > 65535)
        throw Error(QStringLiteral("Порт сервера должен быть числом от 1 до 65535."));
    return port;
}

Node parseVmess(const QString& uri) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(decode64(uri.mid(8).toLatin1()), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        throw Error(QStringLiteral("VMess: ожидается base64 JSON-объект."));
    const auto object = document.object();
    checkFields(object, {"v", "ps", "add", "port", "id", "aid", "net", "type", "host", "path",
                         "tls", "sni", "scy"});
    if ((!field(object, "v").isEmpty() && field(object, "v") != "2") ||
        (!field(object, "aid").isEmpty() && field(object, "aid") != "0") ||
        (!field(object, "type").isEmpty() && field(object, "type") != "none"))
        throw Error(QStringLiteral("VMess: поддерживается v2, alterId=0, без маскировки TCP."));
    Node node;
    node.protocol = QStringLiteral("vmess");
    node.label = field(object, "ps");
    node.host = field(object, "add").toLower();
    node.port = parsePort(field(object, "port"));
    node.credential = field(object, "id");
    node.method = field(object, "scy");
    if (node.method.isEmpty())
        node.method = QStringLiteral("auto");
    node.transport = field(object, "net");
    if (node.transport.isEmpty())
        node.transport = QStringLiteral("tcp");
    const auto tls = field(object, "tls");
    if (!tls.isEmpty() && tls != "tls")
        throw Error(QStringLiteral("VMess: неподдержанный режим TLS."));
    node.tls = tls == "tls";
    node.serverName = field(object, "sni");
    if (node.transport == "ws") {
        node.wsHost = field(object, "host");
        node.wsPath = field(object, "path");
    } else if (!field(object, "host").isEmpty() || !field(object, "path").isEmpty()) {
        throw Error(QStringLiteral("VMess: host/path поддерживаются только для WebSocket."));
    }
    return node;
}

Node parseUri(const QString& uri) {
    if (uri.startsWith("vmess://"))
        return parseVmess(uri);
    if (!uri.startsWith("vless://") && !uri.startsWith("trojan://") && !uri.startsWith("ss://"))
        throw Error(QStringLiteral("Поддерживаются URI VLESS, VMess, Trojan и Shadowsocks."));
    QString normalized = uri;
    if (uri.startsWith("ss://") && !uri.mid(5).section('#', 0, 0).contains('@')) {
        const QString payload = uri.mid(5).section('#', 0, 0);
        if (payload.contains('?'))
            throw Error(QStringLiteral("Shadowsocks: неподдержанный формат параметров."));
        normalized = QStringLiteral("ss://") + utf8(decode64(payload.toLatin1()));
        if (uri.contains('#'))
            normalized += '#' + uri.section('#', 1);
    }
    const QUrl url(normalized, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty())
        throw Error(QStringLiteral("Некорректный URI сервера. Проверьте адрес и кодирование."));
    Node node;
    node.protocol = url.scheme() == "ss" ? QStringLiteral("shadowsocks") : url.scheme();
    node.host = url.host().toLower();
    node.port = url.port(-1);
    node.label = url.fragment(QUrl::FullyDecoded);
    node.tls = node.protocol == "trojan";
    if (!url.path().isEmpty() && url.path() != "/")
        throw Error(QStringLiteral("Путь URI должен задаваться параметром path для WebSocket."));

    QMap<QString, QString> query;
    for (const auto& item : QUrlQuery(url).queryItems(QUrl::FullyDecoded)) {
        if (query.contains(item.first))
            throw Error(QStringLiteral("Повторяющийся параметр URI."));
        query.insert(item.first, item.second);
    }
    QStringList allowed;
    if (node.protocol == "shadowsocks") {
        QString user = QUrl::fromPercentEncoding(url.userInfo(QUrl::FullyEncoded).toUtf8());
        if (!user.contains(':'))
            user = utf8(decode64(user.toLatin1()));
        const auto separator = user.indexOf(':');
        if (separator < 1)
            throw Error(QStringLiteral("Shadowsocks: ожидаются метод и пароль."));
        node.method = user.left(separator);
        node.credential = user.mid(separator + 1);
    } else {
        if (!url.password().isEmpty())
            throw Error(QStringLiteral("Неверный формат credentials в URI."));
        node.credential = url.userName(QUrl::FullyDecoded);
        allowed = {"type", "security", "sni", "path", "host"};
        if (node.protocol == "vless")
            allowed << "encryption" << "flow";
        if (query.contains("encryption") && query.value("encryption") != "none")
            throw Error(QStringLiteral("VLESS: поддерживается только encryption=none."));
        if (query.contains("security")) {
            const auto security = query.value("security");
            if (security != "tls" && security != "none")
                throw Error(
                    QStringLiteral("Поддерживается security=tls/none; REALITY пока недоступен."));
            node.tls = security == "tls";
        }
        if (query.contains("type"))
            node.transport = query.value("type");
        node.serverName = query.value("sni");
        node.wsHost = query.value("host");
        node.wsPath = query.value("path");
        node.flow = query.value("flow");
    }
    for (auto it = query.begin(); it != query.end(); ++it) {
        if (!allowed.contains(it.key()))
            throw Error(QStringLiteral("Неподдержанный параметр URI; профиль не импортирован."));
    }
    return node;
}

} // namespace

void Node::validate() const {
    if (!QStringList{"vless", "vmess", "trojan", "shadowsocks"}.contains(protocol))
        throw Error(QStringLiteral("Неподдержанный протокол."));
    QHostAddress address;
    static const QRegularExpression domain(
        QStringLiteral(R"(^[a-zA-Z0-9](?:[a-zA-Z0-9.-]*[a-zA-Z0-9])?$)"));
    if (host.size() > 253 || host.isEmpty() ||
        (!address.setAddress(host) && (!domain.match(host).hasMatch() || host.contains("..") ||
                                       host.contains(".-") || host.contains("-."))))
        throw Error(QStringLiteral("Некорректное имя или IP-адрес сервера."));
    if (port < 1 || port > 65535)
        throw Error(QStringLiteral("Порт сервера должен быть от 1 до 65535."));
    if (credential.isEmpty() || credential.size() > 4096 || credential.contains(QChar::Null))
        throw Error(QStringLiteral("Пустые или слишком длинные credentials."));
    if (protocol == "vless" || protocol == "vmess") {
        static const QRegularExpression uuid(QStringLiteral(
            "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
        if (!uuid.match(credential).hasMatch() || QUuid(credential).isNull())
            throw Error(QStringLiteral("VLESS/VMess: требуется ненулевой UUID."));
    }
    if (transport != "tcp" && transport != "ws")
        throw Error(QStringLiteral("Поддерживаются только TCP и WebSocket."));
    if (transport != "ws" && (!wsHost.isEmpty() || !wsPath.isEmpty()))
        throw Error(QStringLiteral("Параметры WebSocket указаны для другого транспорта."));
    if (!wsPath.isEmpty() && (!wsPath.startsWith('/') || wsPath.size() > 2048))
        throw Error(
            QStringLiteral("Путь WebSocket должен начинаться с / и быть короче 2049 символов."));
    for (const auto& value : {label, serverName, wsHost, wsPath}) {
        for (QChar c : value) {
            if (c.category() == QChar::Other_Control)
                throw Error(QStringLiteral("Управляющие символы в параметрах профиля запрещены."));
        }
    }
    if (label.size() > 256 || serverName.size() > 253 || wsHost.size() > 253)
        throw Error(QStringLiteral("Слишком длинное имя профиля или TLS/WebSocket host."));
    if (!tls && !serverName.isEmpty())
        throw Error(QStringLiteral("SNI указан без TLS."));
    if (protocol == "trojan" && !tls)
        throw Error(QStringLiteral("Trojan без TLS не поддерживается."));
    if (!flow.isEmpty() &&
        (protocol != "vless" || flow != "xtls-rprx-vision" || !tls || transport != "tcp"))
        throw Error(
            QStringLiteral("VLESS flow поддерживается только как xtls-rprx-vision с TCP/TLS."));
    if (protocol == "shadowsocks" &&
        (!QStringList{"aes-128-gcm", "aes-256-gcm", "chacha20-ietf-poly1305"}.contains(method) ||
         tls || transport != "tcp"))
        throw Error(QStringLiteral("Shadowsocks: поддерживаются AEAD-методы без плагинов/TLS."));
    if (protocol == "vmess" &&
        !QStringList{"auto", "aes-128-gcm", "chacha20-poly1305", "none"}.contains(method))
        throw Error(QStringLiteral("VMess: неподдержанный метод шифрования."));
    if ((protocol == "vless" || protocol == "trojan") && !method.isEmpty())
        throw Error(QStringLiteral("Метод шифрования неприменим к этому протоколу."));
}

QJsonObject Node::toJson() const {
    return {
        {"protocol", protocol},     {"label", label},   {"host", host},           {"port", port},
        {"credential", credential}, {"method", method}, {"transport", transport}, {"tls", tls},
        {"serverName", serverName}, {"wsHost", wsHost}, {"wsPath", wsPath},       {"flow", flow}};
}

Node Node::fromJson(const QJsonObject& object) {
    checkFields(object, {"protocol", "label", "host", "port", "credential", "method", "transport",
                         "tls", "serverName", "wsHost", "wsPath", "flow"});
    if (!object.value("port").isDouble() || !object.value("tls").isBool())
        throw Error(QStringLiteral("Неверные типы нормализованного профиля."));
    Node node;
    node.protocol = field(object, "protocol");
    node.label = field(object, "label");
    node.host = field(object, "host").toLower();
    node.port = parsePort(field(object, "port"));
    node.credential = field(object, "credential");
    node.method = field(object, "method");
    node.transport = field(object, "transport");
    node.tls = object.value("tls").toBool();
    node.serverName = field(object, "serverName");
    node.wsHost = field(object, "wsHost");
    node.wsPath = field(object, "wsPath");
    node.flow = field(object, "flow");
    node.validate();
    return node;
}

QString Node::fingerprint() const {
    auto object = toJson();
    object.remove("label");
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha256)
            .toHex());
}

QVector<Node> ProfileParser::parse(const QByteArray& input) {
    if (input.size() > MaxBytes)
        throw Error(QStringLiteral("Подписка превышает лимит 4 МиБ."));
    QString text = utf8(input).trimmed();
    if (text.startsWith(QChar::ByteOrderMark))
        text.remove(0, 1);
    if (!text.contains("://"))
        text = utf8(decode64(text.toLatin1())).trimmed();
    QVector<Node> nodes;
    const auto lines = text.split('\n');
    for (qsizetype index = 0; index < lines.size(); ++index) {
        const auto line = lines[index].trimmed();
        if (line.isEmpty())
            continue;
        if (nodes.size() >= MaxNodes)
            throw Error(QStringLiteral("Слишком много узлов: максимум 10000."));
        try {
            Node node = parseUri(line);
            if (node.label.isEmpty())
                node.label = node.host + ':' + QString::number(node.port);
            node.validate();
            nodes.append(node);
        } catch (const Error& error) {
            throw Error(QStringLiteral("Строка %1: %2")
                            .arg(index + 1)
                            .arg(QString::fromUtf8(error.what())));
        }
    }
    if (nodes.isEmpty())
        throw Error(QStringLiteral("Подписка не содержит узлов."));
    return nodes;
}

} // namespace sunlens
