#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkReply;

namespace sunlens {

[[nodiscard]] QByteArray tunConfig(const QString& protocol, const QString& host, int port);

class ProxyController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

  public:
    explicit ProxyController(QObject* parent = nullptr);

    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString status() const;

    Q_INVOKABLE void test(const QString& protocol, const QString& host, int port,
                          const QString& username, const QString& password);
    Q_INVOKABLE void exportTun(const QUrl& file, const QString& protocol, const QString& host,
                               int port);

  signals:
    void changed();

  private:
    void setStatus(const QString& status);

    QPointer<QNetworkReply> reply_;
    QElapsedTimer elapsed_;
    QString status_;
};

} // namespace sunlens
