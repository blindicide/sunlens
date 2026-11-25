#include "sunlens/application-controller.hpp"

#include <QUrl>
#include <QtConcurrent>

namespace sunlens {

ApplicationController::ApplicationController(ProfileRepository& repository,
                                             std::shared_ptr<const ICoreAdapter> adapter,
                                             QObject* parent, std::unique_ptr<ITunnelClient> tunnel)
    : QObject(parent), repository_(repository), analytics_(repository), manager_(repository),
      adapter_(std::move(adapter)),
      tunnel_(tunnel ? std::move(tunnel) : std::make_unique<HelperClient>()) {
    if (!adapter_)
        throw Error(QStringLiteral("Не задан адаптер ядра."));
    corePath_ = repository_.preference(QStringLiteral("sing-box-path"));
    if (repository_.preference(QStringLiteral("selected-core")) == "xray") {
        selectedCore_ = QStringLiteral("xray");
        corePath_ = repository_.preference(QStringLiteral("xray-path"));
    }
    connect(tunnel_.get(), &ITunnelClient::statusReceived, this, [this](const QJsonObject& status) {
        const auto previous = tunnelState_;
        const auto previousCounters = tunnelCounters_;
        tunnelState_ = status["state"].toString();
        tunnelMessage_ = status["message"].toString();
        tunnelCounters_ = QVariantMap{{"available", status["countersAvailable"].toBool()},
                                      {"upload", status["upload"].toString()},
                                      {"download", status["download"].toString()},
                                      {"source", status["counterSource"].toString()}};
        if (previous == "ready" && tunnelState_ == "ready" &&
            status["countersAvailable"].toBool()) {
            for (const auto& key : {QStringLiteral("uploadRate"), QStringLiteral("downloadRate")}) {
                if (previousCounters.contains(key))
                    tunnelCounters_.insert(key, previousCounters.value(key));
            }
        }
        try {
            recordTunnelStatus(status, previous != tunnelState_);
        } catch (const Error& error) {
            setMessage(QString::fromUtf8(error.what()));
        }
        emit tunnelChanged();
        if (quitRequested_ && tunnelState_ == "idle") {
            quitRequested_ = false;
            emit quitReady();
        }
    });
    const auto retention = repository_.preference(QStringLiteral("analytics-retention")).toInt();
    if (retention >= 1 && retention <= 3650)
        analyticsRetention_ = retention;
    probeDeadline_.setSingleShot(true);
    connect(&probeDeadline_, &QTimer::timeout, this, [this] { finishProbe(false); });
    connect(&probeSocket_, &QAbstractSocket::connected, this, [this] { finishProbe(true); });
    connect(&probeSocket_, &QAbstractSocket::errorOccurred, this, [this] { finishProbe(false); });
    connect(&validation_, &QFutureWatcher<ValidationResult>::finished, this, [this] {
        const auto result = validation_.result();
        coreBusy_ = false;
        coreStatus_ = result.message;
        emit coreInfoChanged();
    });
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
    lastHistoryPrune_ = QDateTime::currentMSecsSinceEpoch();
    analytics_.prune(lastHistoryPrune_, analyticsRetention_);
    reloadAnalytics();
}

QVariantList ApplicationController::servers() const { return servers_; }
QVariantList ApplicationController::subscriptions() const { return subscriptions_; }
QString ApplicationController::selectedId() const { return selectedId_; }
QString ApplicationController::selectedLabel() const { return selectedLabel_; }
QString ApplicationController::message() const { return message_; }
bool ApplicationController::busy() const { return manager_.busy(); }
bool ApplicationController::coreBusy() const { return coreBusy_; }
QString ApplicationController::corePath() const { return corePath_; }
QString ApplicationController::coreStatus() const { return coreStatus_; }
QString ApplicationController::selectedCore() const { return selectedCore_; }
void ApplicationController::setSelectedCore(const QString& core) {
    if (coreBusy_ || tunnelActive() || core == selectedCore_ ||
        (core != "sing-box" && core != "xray"))
        return;
    try {
        const auto path = repository_.preference(core + QStringLiteral("-path"));
        repository_.setPreference(QStringLiteral("selected-core"), core);
        selectedCore_ = core;
        corePath_ = path;
        coreStatus_ = QStringLiteral("Выбранное ядро ещё не проверено.");
        emit coreInfoChanged();
    } catch (const Error& error) {
        setMessage(QString::fromUtf8(error.what()));
    }
}
bool ApplicationController::tunnelAvailable() const { return tunnel_->available(); }
QString ApplicationController::tunnelState() const { return tunnelState_; }
QString ApplicationController::tunnelMessage() const { return tunnelMessage_; }
QVariantMap ApplicationController::tunnelCounters() const { return tunnelCounters_; }
bool ApplicationController::tunnelActive() const {
    return QStringList{"authorizing", "starting", "ready", "stopping", "recovery-required"}
        .contains(tunnelState_);
}

void ApplicationController::setMessage(const QString& text) {
    message_ = text;
    emit messageChanged();
}

void ApplicationController::reload() {
    servers_.clear();
    subscriptions_.clear();
    selectedId_ = repository_.preference(QStringLiteral("selected-server"));
    selectedLabel_.clear();
    bool selectedExists = false;
    for (const auto& server : repository_.servers()) {
        const auto& node = server.node;
        if (server.id == selectedId_) {
            selectedExists = true;
            selectedLabel_ = node.label;
        }
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
    if (tunnelActive() && id != selectedId_) {
        setMessage(QStringLiteral("Сначала отключите TUN, затем выберите другой сервер."));
        return;
    }
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

void ApplicationController::validateSelectedCore(const QString& executable, bool direct,
                                                 bool bypassLan, bool ipv6) {
    if (coreBusy_) {
        setMessage(QStringLiteral("Дождитесь окончания проверки ядра."));
        return;
    }
    try {
        std::optional<Node> selected;
        for (const auto& server : repository_.servers()) {
            if (server.id == selectedId_) {
                selected = server.node;
                break;
            }
        }
        if (!selected)
            throw Error(QStringLiteral("Сначала выберите сервер на вкладке профилей."));
        CoreOptions options;
        options.routing = direct ? RoutingMode::Direct : RoutingMode::Proxy;
        options.bypassLan = bypassLan;
        options.ipv6 = ipv6;
        std::shared_ptr<const ICoreAdapter> adapter = adapter_;
        if (selectedCore_ == "xray") {
            adapter = std::make_shared<XrayAdapter>();
            options.dns = {QStringLiteral("1.1.1.1"), 53, false, {}};
        }
        const auto config = adapter->configuration(*selected, options);
        corePath_ = executable.trimmed();
        repository_.setPreference(selectedCore_ + QStringLiteral("-path"), corePath_);
        coreBusy_ = true;
        coreStatus_ =
            QStringLiteral("Проверка SHA-256, версии и конфигурации. Сеть не изменяется.");
        emit coreInfoChanged();
        const auto path = corePath_;
        validation_.setFuture(QtConcurrent::run([adapter, path, config] {
            try {
                return adapter->validate(path, config);
            } catch (...) {
                return ValidationResult{
                    false, QStringLiteral("Не удалось завершить проверку ядра."), {}};
            }
        }));
    } catch (const Error& error) {
        setMessage(QString::fromUtf8(error.what()));
    }
}

void ApplicationController::startTunnel(int routing, bool bypassLan, bool ipv6,
                                        const QString& dnsAddress, int dnsPort, bool dnsHttps,
                                        const QString& dnsTlsName) {
    if (tunnelActive()) {
        setMessage(QStringLiteral("Текущий TUN нужно отключить или восстановить."));
        return;
    }
    try {
        for (const auto& server : repository_.servers()) {
            if (server.id != selectedId_)
                continue;
            TunnelRequest request;
            request.core = selectedCore_;
            request.serverId = server.id;
            request.subscriptionId = server.subscriptionId;
            request.node = server.node;
            if (routing < 0 || routing > 1)
                throw Error(QStringLiteral("Выберите поддержанный режим маршрутизации."));
            request.options.routing = static_cast<RoutingMode>(routing);
            request.options.bypassLan = bypassLan;
            request.options.ipv6 = ipv6;
            request.options.dns = {dnsAddress.trimmed(), dnsPort, dnsHttps, dnsTlsName.trimmed()};
            (void)TunnelRequest::fromJson(request.toJson());
            if (probeIdentity_)
                finishProbe(false);
            trafficIdentity_ = TrafficIdentity{{}, server.id, server.subscriptionId, request.core};
            measuredPoll_.invalidate();
            tunnel_->start(request);
            return;
        }
        throw Error(QStringLiteral("Выберите существующий сервер перед подключением."));
    } catch (const Error& error) {
        setMessage(QString::fromUtf8(error.what()));
    }
}

void ApplicationController::stopTunnel() { tunnel_->stop(); }
void ApplicationController::recoverTunnel() { tunnel_->recover(); }
void ApplicationController::disconnectAndQuit() {
    if (!tunnelActive()) {
        emit quitReady();
        return;
    }
    quitRequested_ = true;
    tunnel_->stop();
}

} // namespace sunlens
