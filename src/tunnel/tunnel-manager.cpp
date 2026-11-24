#include "sunlens/tunnel.hpp"

#include <QDateTime>

namespace sunlens {

TunnelManager::TunnelManager(std::unique_ptr<ITunnelPlatform> platform,
                             const QString& stateDirectory, QObject* parent)
    : QObject(parent), platform_(std::move(platform)), journal_(stateDirectory) {
    if (!platform_)
        throw Error(QStringLiteral("Не задана реализация сетевой платформы."));
    timer_.setInterval(250);
    connect(&timer_, &QTimer::timeout, this, &TunnelManager::poll);
}

TunnelManager::~TunnelManager() {
    timer_.stop();
    if (lease_)
        (void)stop();
}

void TunnelManager::setState(const QString& state, const QString& message) {
    state_ = state;
    message_ = message;
    emit statusChanged();
}

bool TunnelManager::recover() {
    if (state_ == "starting" || state_ == "ready")
        return stop();
    try {
        lease_ = journal_.load();
        if (lease_)
            return unwind(QStringLiteral("idle"), QStringLiteral("Сеть восстановлена."));
        setState(QStringLiteral("idle"), QStringLiteral("Незавершённых сетевых транзакций нет."));
        return true;
    } catch (const Error& error) {
        setState(QStringLiteral("recovery-required"), QString::fromUtf8(error.what()));
        return false;
    }
}

bool TunnelManager::start(const TunnelRequest& request) {
    if (lease_ || state_ == "recovery-required") {
        message_ = QStringLiteral("Сначала завершите текущую транзакцию или восстановление.");
        emit statusChanged();
        return false;
    }
    try {
        const auto checked = TunnelRequest::fromJson(request.toJson());
        if (journal_.load())
            throw Error(QStringLiteral("Перед подключением требуется восстановление сети."));
        lease_ = platform_->reserve(platform_->snapshot());
        lease_->core = checked.core;
        journal_.save(*lease_, QStringLiteral("prepared"));
        platform_->launch(checked, *lease_);
        journal_.save(*lease_, QStringLiteral("starting"));
        startup_.start();
        traffic_ = {};
        setState(QStringLiteral("starting"),
                 QStringLiteral("Настройка TUN; сервер ещё не проверен."));
        timer_.start();
        return true;
    } catch (const Error& error) {
        if (lease_)
            (void)unwind(QStringLiteral("error"), QString::fromUtf8(error.what()));
        else
            setState(QStringLiteral("error"), QString::fromUtf8(error.what()));
        return false;
    }
}

bool TunnelManager::unwind(const QString& finalState, const QString& message) {
    timer_.stop();
    try {
        if (lease_) {
            try {
                journal_.save(*lease_, QStringLiteral("stopping"));
            } catch (const Error&) {
                // Отказ диска не должен препятствовать остановке и откату сети.
            }
            platform_->shutdown(*lease_);
            platform_->restore(*lease_);
            journal_.clear();
            lease_.reset();
        }
        traffic_ = {};
        setState(finalState, message);
        return true;
    } catch (const Error& error) {
        setState(QStringLiteral("recovery-required"), QString::fromUtf8(error.what()));
        return false;
    }
}

bool TunnelManager::stop() {
    return unwind(QStringLiteral("idle"),
                  QStringLiteral("Отключено. Сетевые ресурсы освобождены."));
}

void TunnelManager::poll() {
    if (!lease_)
        return;
    try {
        if (!platform_->running())
            throw Error(QStringLiteral("Ядро неожиданно завершилось. Выполняется восстановление."));
        if (state_ == "starting") {
            if (platform_->ready(*lease_)) {
                journal_.save(*lease_, QStringLiteral("ready"));
                setState(QStringLiteral("ready"),
                         QStringLiteral("TUN активен. Доступность сервера ещё не подтверждена."));
            } else if (startup_.elapsed() > 15000) {
                throw Error(QStringLiteral("Истекло время подготовки TUN."));
            }
        }
        if (state_ == "ready") {
            traffic_ = platform_->counters(*lease_);
            emit statusChanged();
        }
    } catch (const Error& error) {
        (void)unwind(QStringLiteral("error"), QString::fromUtf8(error.what()));
    }
}

QJsonObject TunnelManager::status() const {
    QJsonObject result{{"schema", 1},
                       {"state", state_},
                       {"message", message_},
                       {"reachability", "unverified"},
                       {"counterSource", "tun-l3"},
                       {"countersAvailable", traffic_.available},
                       {"upload", QString::number(traffic_.upload)},
                       {"download", QString::number(traffic_.download)}};
    if (lease_) {
        result.insert("sessionId", lease_->id);
        result.insert("interfaceName", lease_->interfaceName);
        result.insert("core", lease_->core);
    }
    return result;
}

} // namespace sunlens
