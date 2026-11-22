#pragma once

#include "sunlens/tunnel.hpp"

#include <QProcess>

namespace sunlens {

class ITunnelClient : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    [[nodiscard]] virtual bool available() const = 0;
    virtual void start(const TunnelRequest& request) = 0;
    virtual void stop() = 0;
    virtual void recover() = 0;
  signals:
    void statusReceived(const QJsonObject& status);
};

class HelperClient final : public ITunnelClient {
    Q_OBJECT
  public:
    explicit HelperClient(QObject* parent = nullptr);
    ~HelperClient() override;
    [[nodiscard]] bool available() const override;
    void start(const TunnelRequest& request) override;
    void stop() override;
    void recover() override;

  private:
    void launch();
    void readOutput();
    void acceptStatus(const QJsonObject& status);
    void writeOperation(const QString& operation);
    void localStatus(const QString& state, const QString& message);
    QProcess process_;
    QTimer deadline_;
    QByteArray buffer_;
    std::optional<TunnelRequest> pending_;
    QString lastState_ = QStringLiteral("idle");
    bool handshake_ = false;
    bool startSent_ = false;
    bool closing_ = false;
};

} // namespace sunlens
