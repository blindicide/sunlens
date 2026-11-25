#include "sunlens/application-controller.hpp"

#include <QHash>

namespace sunlens {

QVariantMap ApplicationController::analyticsSummary() const { return analyticsSummary_; }
QVariantList ApplicationController::analyticsServers() const { return analyticsServers_; }
QVariantList ApplicationController::analyticsSubscriptions() const {
    return analyticsSubscriptions_;
}
QVariantList ApplicationController::analyticsHistory() const { return analyticsHistory_; }
int ApplicationController::analyticsRetention() const { return analyticsRetention_; }
QString ApplicationController::probeStatus() const { return probeStatus_; }

void ApplicationController::reloadAnalytics() {
    const auto names = [](const QVariantList& items, const QString& key) {
        QHash<QString, QString> result;
        for (const auto& item : items) {
            const auto entry = item.toMap();
            result.insert(entry["id"].toString(), entry.value(key).toString());
        }
        return result;
    };
    const auto serverNames = names(servers_, "label");
    const auto subscriptionNames = names(subscriptions_, "name");
    const auto label = [](const QHash<QString, QString>& source, const QString& id) {
        return source.value(id, QStringLiteral("Удалённая запись · %1").arg(id.left(8)));
    };
    const auto titled = [&](const QVariantList& items, const QHash<QString, QString>& source) {
        QVariantList output;
        for (const auto& item : items) {
            auto entry = item.toMap();
            entry.insert("label", label(source, entry["id"].toString()));
            output.append(entry);
        }
        return output;
    };
    analyticsSummary_ = analytics_.total();
    analyticsServers_ = titled(analytics_.serverTotals(), serverNames);
    analyticsSubscriptions_ = titled(analytics_.subscriptionTotals(), subscriptionNames);
    analyticsHistory_.clear();
    for (const auto& item : analytics_.history()) {
        auto entry = item.toMap();
        entry.insert("label", label(serverNames, entry["server"].toString()));
        analyticsHistory_.append(entry);
    }
    emit analyticsChanged();
}

void ApplicationController::recordTunnelStatus(const QJsonObject& status, bool changedState) {
    if (!trafficIdentity_)
        return;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (changedState)
        analytics_.event(*trafficIdentity_, tunnelState_, now);
    if (tunnelState_ == "ready" && status["countersAvailable"].toBool() &&
        (!measuredPoll_.isValid() || measuredPoll_.elapsed() >= 1000 || changedState)) {
        bool upOk = false;
        bool downOk = false;
        const auto upload = status["upload"].toString().toLongLong(&upOk);
        const auto download = status["download"].toString().toLongLong(&downOk);
        const auto session = status["sessionId"].toString();
        if (upOk && downOk && !session.isEmpty()) {
            if (!trafficIdentity_->session.isEmpty() && trafficIdentity_->session != session)
                throw Error(QStringLiteral("Идентификатор активной сессии неожиданно изменился."));
            trafficIdentity_->session = session;
            const auto delta = analytics_.sample(*trafficIdentity_, upload, download, now);
            measuredPoll_.restart();
            tunnelCounters_.remove("uploadRate");
            tunnelCounters_.remove("downloadRate");
            if (delta.continuous && delta.intervalMs > 0) {
                tunnelCounters_.insert(
                    "uploadRate",
                    QString::number(double(delta.upload) * 1000 / delta.intervalMs, 'f', 0));
                tunnelCounters_.insert(
                    "downloadRate",
                    QString::number(double(delta.download) * 1000 / delta.intervalMs, 'f', 0));
            }
            if (now - lastHistoryPrune_ >= 3600000) {
                analytics_.prune(now, analyticsRetention_);
                lastHistoryPrune_ = now;
            }
            reloadAnalytics();
            return;
        }
    }
    if (tunnelState_ == "idle" || tunnelState_ == "error")
        trafficIdentity_.reset();
    if (changedState)
        reloadAnalytics();
}

void ApplicationController::setAnalyticsRetention(int days) {
    try {
        const auto now = QDateTime::currentMSecsSinceEpoch();
        analytics_.prune(now, days);
        repository_.setPreference(QStringLiteral("analytics-retention"), QString::number(days));
        analyticsRetention_ = days;
        lastHistoryPrune_ = now;
        reloadAnalytics();
    } catch (const Error& error) {
        setMessage(QString::fromUtf8(error.what()));
    }
}

void ApplicationController::probeSelectedServer() {
    if (tunnelActive() || profileCheckBusy_ || probeIdentity_) {
        setMessage(
            QStringLiteral("TCP-проба доступна без активного TUN и только по одному запросу."));
        return;
    }
    try {
        for (const auto& server : repository_.servers()) {
            if (server.id != selectedId_)
                continue;
            probeIdentity_ = TrafficIdentity{{}, server.id, server.subscriptionId, selectedCore_};
            probeStatus_ = QStringLiteral("TCP-проба: измерение установки соединения…");
            emit analyticsChanged();
            probeClock_.start();
            probeDeadline_.start(3000);
            probeSocket_.connectToHost(server.node.host, static_cast<quint16>(server.node.port));
            return;
        }
        setMessage(QStringLiteral("Сначала выберите сервер для TCP-пробы."));
    } catch (const Error& error) {
        setMessage(QString::fromUtf8(error.what()));
    }
}

void ApplicationController::finishProbe(bool success) {
    if (!probeIdentity_)
        return;
    const auto identity = *probeIdentity_;
    probeIdentity_.reset();
    const auto latency = success ? qMin<qint64>(probeClock_.elapsed(), 5000) : -1;
    probeDeadline_.stop();
    probeSocket_.abort();
    probeStatus_ =
        success
            ? QStringLiteral("TCP-подключение: %1 мс. VPN-авторизация не проверялась.").arg(latency)
            : QStringLiteral("TCP-проба не установила соединение за отведённое время.");
    try {
        analytics_.probe(identity, latency, QDateTime::currentMSecsSinceEpoch());
        reloadAnalytics();
    } catch (const Error& error) {
        setMessage(QString::fromUtf8(error.what()));
        emit analyticsChanged();
    }
}

} // namespace sunlens
