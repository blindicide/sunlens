#include "sunlens/application-controller.hpp"

#include <QUrl>

namespace sunlens {

ApplicationController::ApplicationController(ProfileRepository& repository, QObject* parent)
    : QObject(parent), repository_(repository), manager_(repository) {
    connect(&manager_, &SubscriptionManager::busyChanged, this,
            &ApplicationController::busyChanged);
    connect(&manager_, &SubscriptionManager::failed, this, [this](const QString& error) {
        setMessage(error);
        reload();
    });
    connect(&manager_, &SubscriptionManager::completed, this, [this] {
        setMessage(QStringLiteral("Подписка обновлена. Сеть не изменена."));
        reload();
    });
    reload();
}

QVariantList ApplicationController::servers() const { return servers_; }
QVariantList ApplicationController::subscriptions() const { return subscriptions_; }
QString ApplicationController::selectedId() const { return selectedId_; }
QString ApplicationController::message() const { return message_; }
bool ApplicationController::busy() const { return manager_.busy(); }

void ApplicationController::setMessage(const QString& text) {
    message_ = text;
    emit messageChanged();
}

void ApplicationController::reload() {
    servers_.clear();
    subscriptions_.clear();
    selectedId_ = repository_.preference(QStringLiteral("selected-server"));
    bool selectedExists = false;
    for (const auto& server : repository_.servers()) {
        const auto& node = server.node;
        selectedExists |= server.id == selectedId_;
        servers_.append(QVariantMap{{"id", server.id},
                                    {"subscriptionId", server.subscriptionId},
                                    {"label", node.label},
                                    {"protocol", node.protocol},
                                    {"host", node.host},
                                    {"port", node.port}});
    }
    if (!selectedExists) {
        selectedId_.clear();
        repository_.setPreference(QStringLiteral("selected-server"), QStringLiteral(""));
    }
    for (const auto& entry : repository_.subscriptions()) {
        subscriptions_.append(QVariantMap{{"id", entry.id},
                                          {"name", entry.name},
                                          {"status", entry.status},
                                          {"updatedAt", entry.updatedAt},
                                          {"remote", !entry.url.isEmpty()},
                                          {"host", QUrl(entry.url).host()}});
    }
    emit profilesChanged();
}

void ApplicationController::appendManual(const QVector<Node>& nodes) {
    QVector<Node> combined;
    for (const auto& server : repository_.servers()) {
        if (server.subscriptionId == ProfileRepository::manualGroupId())
            combined.append(server.node);
    }
    combined += nodes;
    repository_.replaceNodes(ProfileRepository::manualGroupId(), combined);
    reload();
}

bool ApplicationController::importText(const QString& text) {
    try {
        const auto nodes = ProfileParser::parse(text.toUtf8());
        appendManual(nodes);
        setMessage(QStringLiteral("Импортировано узлов: %1. Сеть не изменена.").arg(nodes.size()));
        return true;
    } catch (const Error& error) {
        setMessage(QString::fromUtf8(error.what()));
        return false;
    }
}

bool ApplicationController::addManual(const QString& protocol, const QString& host, int port,
                                      const QString& credential, const QString& method, bool tls,
                                      const QString& serverName) {
    try {
        Node node;
        node.protocol = protocol;
        node.host = host.trimmed().toLower();
        node.label = node.host + ':' + QString::number(port);
        node.port = port;
        node.credential = credential;
        node.method = method;
        node.tls = tls;
        node.serverName = serverName.trimmed();
        node.validate();
        appendManual({node});
        setMessage(QStringLiteral("Сервер сохранён. Сеть не изменена."));
        return true;
    } catch (const Error& error) {
        setMessage(QString::fromUtf8(error.what()));
        return false;
    }
}

void ApplicationController::fetchSubscription(const QString& url, const QString& name) {
    manager_.fetch(url, name);
}
void ApplicationController::refreshSubscription(const QString& id) { manager_.refresh(id); }
void ApplicationController::cancelFetch() { manager_.cancel(); }
void ApplicationController::selectServer(const QString& id) {
    try {
        for (const auto& server : repository_.servers()) {
            if (server.id == id) {
                repository_.setPreference(QStringLiteral("selected-server"), id);
                reload();
                return;
            }
        }
        setMessage(QStringLiteral("Выбранный сервер больше не существует."));
    } catch (const Error& error) {
        setMessage(QString::fromUtf8(error.what()));
    }
}
void ApplicationController::renameServer(const QString& id, const QString& label) {
    try {
        repository_.setLabel(id, label);
        reload();
    } catch (const Error& error) {
        setMessage(QString::fromUtf8(error.what()));
    }
}

} // namespace sunlens
