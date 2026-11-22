#include "sunlens/tunnel-client.hpp"

#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>

#ifdef Q_OS_LINUX
#include "sunlens/helper-paths.hpp"
#include "sunlens/linux-tunnel-platform.hpp"
#endif

namespace sunlens {

HelperClient::HelperClient(QObject* parent) : ITunnelClient(parent) {
    process_.setStandardErrorFile(QProcess::nullDevice());
    deadline_.setSingleShot(true);
    connect(&process_, &QProcess::readyReadStandardOutput, this, &HelperClient::readOutput);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        pending_.reset();
        closing_ = true;
        process_.closeWriteChannel();
        localStatus(startSent_ ? QStringLiteral("recovery-required") : QStringLiteral("error"),
                    QStringLiteral("Помощник не ответил вовремя. Проверьте polkit и диагностику."));
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            deadline_.stop();
            pending_.reset();
            localStatus(
                QStringLiteral("error"),
                QStringLiteral("Не удалось запустить pkexec/помощник. Переустановите SUNLENS."));
        }
    });
    connect(
        &process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus) {
            deadline_.stop();
            pending_.reset();
            if (lastState_ == "recovery-required" || lastState_ == "error")
                return;
            if (!handshake_) {
                localStatus(QStringLiteral("error"),
                            code == 126
                                ? QStringLiteral("Авторизация отменена. Запрос TUN не отправлялся.")
                                : QStringLiteral(
                                      "Авторизация недоступна. Нужен установленный polkit agent."));
            } else if (lastState_ != "idle") {
                localStatus(
                    QStringLiteral("recovery-required"),
                    QStringLiteral("Связь с помощником потеряна. Проверьте восстановление сети."));
            }
        });
}

HelperClient::~HelperClient() {
    deadline_.stop();
    disconnect(&process_, nullptr, this, nullptr);
    pending_.reset();
    process_.closeWriteChannel();
    if (process_.state() != QProcess::NotRunning)
        (void)process_.waitForFinished(30000);
}

bool HelperClient::available() const {
#ifdef Q_OS_LINUX
    const QFileInfo pkexec(QStringLiteral("/usr/bin/pkexec"));
    return LinuxTunnelPlatform::trustedPath(InstalledHelper, true) && pkexec.isFile() &&
           pkexec.isExecutable() && pkexec.ownerId() == 0 &&
           !(pkexec.permissions() & (QFileDevice::WriteGroup | QFileDevice::WriteOther));
#else
    return false;
#endif
}

void HelperClient::localStatus(const QString& state, const QString& message) {
    lastState_ = state;
    emit statusReceived(QJsonObject{{"schema", 1},
                                    {"state", state},
                                    {"message", message},
                                    {"reachability", "unverified"},
                                    {"counterSource", "tun-l3"},
                                    {"countersAvailable", false},
                                    {"upload", "0"},
                                    {"download", "0"}});
}

void HelperClient::launch() {
#ifdef Q_OS_LINUX
    handshake_ = false;
    startSent_ = false;
    closing_ = false;
    buffer_.clear();
    process_.setProgram(QStringLiteral("/usr/bin/pkexec"));
    process_.setArguments({QStringLiteral("--disable-internal-agent"), InstalledHelper});
    deadline_.start(120000);
    localStatus(QStringLiteral("authorizing"),
                QStringLiteral("Подтвердите настройку сети в системном окне polkit."));
    process_.start();
#else
    localStatus(QStringLiteral("error"), QStringLiteral("Для Windows помощник ещё не установлен."));
#endif
}

void HelperClient::start(const TunnelRequest& request) {
    if (!available()) {
        localStatus(
            QStringLiteral("error"),
            QStringLiteral(
                "Установленный root-owned помощник недоступен. Developer-путь не используется."));
        return;
    }
    if (process_.state() != QProcess::NotRunning) {
        localStatus(QStringLiteral("error"),
                    QStringLiteral("Дождитесь завершения предыдущего помощника."));
        return;
    }
    try {
        pending_ = TunnelRequest::fromJson(request.toJson());
        const auto bytes =
            QJsonDocument(
                QJsonObject{{"schema", 1}, {"operation", "start"}, {"request", pending_->toJson()}})
                .toJson(QJsonDocument::Compact);
        if (bytes.size() >= 128 * 1024)
            throw Error(QStringLiteral("Запрос помощника превышает 128 КиБ."));
        launch();
    } catch (const Error& error) {
        pending_.reset();
        localStatus(QStringLiteral("error"), QString::fromUtf8(error.what()));
    }
}

void HelperClient::recover() {
    if (!available() || process_.state() != QProcess::NotRunning) {
        localStatus(QStringLiteral("recovery-required"),
                    QStringLiteral("Восстановление требует доступного установленного помощника."));
        return;
    }
    pending_.reset();
    launch();
}

void HelperClient::writeOperation(const QString& operation) {
    const auto frame = QJsonDocument(QJsonObject{{"schema", 1}, {"operation", operation}})
                           .toJson(QJsonDocument::Compact) +
                       '\n';
    if (process_.write(frame) != frame.size()) {
        closing_ = true;
        process_.closeWriteChannel();
        localStatus(QStringLiteral("recovery-required"),
                    QStringLiteral("Не удалось отправить запрос помощнику."));
    }
}

void HelperClient::stop() {
    pending_.reset();
    closing_ = true;
    if (process_.state() == QProcess::NotRunning) {
        recover();
        return;
    }
    if (!handshake_) {
        process_.closeWriteChannel();
        process_.terminate();
        return;
    }
    localStatus(QStringLiteral("stopping"), QStringLiteral("Остановка TUN и восстановление сети."));
    writeOperation(QStringLiteral("stop"));
    deadline_.start(30000);
}

void HelperClient::acceptStatus(const QJsonObject& status) {
    static const QRegularExpression counter(QStringLiteral("^[0-9]{1,20}$"));
    const auto state = status["state"].toString();
    if (status["schema"] != QJsonValue(1) ||
        !QStringList{"idle", "starting", "ready", "error", "recovery-required"}.contains(state) ||
        !status["message"].isString() || status["message"].toString().size() > 2048 ||
        !status["countersAvailable"].isBool() || status["counterSource"] != QJsonValue("tun-l3") ||
        !counter.match(status["upload"].toString()).hasMatch() ||
        !counter.match(status["download"].toString()).hasMatch() ||
        ((state == "starting" || state == "ready") &&
         QUuid(status["sessionId"].toString()).isNull())) {
        pending_.reset();
        closing_ = true;
        process_.closeWriteChannel();
        localStatus(
            QStringLiteral("recovery-required"),
            QStringLiteral("Неверный ответ помощника. Соединение закрыто для безопасной очистки."));
        return;
    }
    if (!handshake_) {
        handshake_ = true;
        if (state == "idle" && pending_ && !closing_) {
            const auto frame = QJsonDocument(QJsonObject{{"schema", 1},
                                                         {"operation", "start"},
                                                         {"request", pending_->toJson()}})
                                   .toJson(QJsonDocument::Compact) +
                               '\n';
            pending_.reset();
            if (process_.write(frame) != frame.size()) {
                process_.closeWriteChannel();
                localStatus(QStringLiteral("recovery-required"),
                            QStringLiteral("Не удалось передать профиль помощнику."));
            } else {
                startSent_ = true;
                deadline_.start(25000);
            }
            return;
        }
    }
    if (closing_ && (lastState_ == "error" || lastState_ == "recovery-required"))
        return;
    lastState_ = state;
    emit statusReceived(status);
    if (state == "ready")
        deadline_.stop();
    if (state == "idle" || state == "error" || state == "recovery-required") {
        deadline_.stop();
        closing_ = true;
        pending_.reset();
        process_.closeWriteChannel();
    }
}

void HelperClient::readOutput() {
    buffer_ += process_.readAllStandardOutput();
    if (buffer_.size() > 128 * 1024) {
        closing_ = true;
        pending_.reset();
        process_.closeWriteChannel();
        localStatus(QStringLiteral("recovery-required"),
                    QStringLiteral("Превышен лимит ответа помощника."));
        return;
    }
    while (buffer_.contains('\n')) {
        const auto end = buffer_.indexOf('\n');
        const auto frame = buffer_.left(end);
        buffer_.remove(0, end + 1);
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(frame, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            acceptStatus({});
        else
            acceptStatus(document.object());
    }
}

} // namespace sunlens
