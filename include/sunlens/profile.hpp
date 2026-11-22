#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <stdexcept>

namespace sunlens {

class Error : public std::runtime_error {
  public:
    explicit Error(const QString& message) : std::runtime_error(message.toStdString()) {}
};

struct Node {
    QString protocol;
    QString label;
    QString host;
    int port = 0;
    QString credential;
    QString method;
    QString transport = QStringLiteral("tcp");
    bool tls = false;
    QString serverName;
    QString wsHost;
    QString wsPath;
    QString flow;

    void validate() const;
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static Node fromJson(const QJsonObject& object);
    [[nodiscard]] QString fingerprint() const;
};

struct Server {
    QString id;
    QString subscriptionId;
    Node node;
};

struct Subscription {
    QString id;
    QString name;
    QString url;
    QString status;
    QString updatedAt;
};

class ProfileParser {
  public:
    static constexpr qsizetype MaxBytes = 4 * 1024 * 1024;
    static constexpr qsizetype MaxNodes = 10000;
    [[nodiscard]] static QVector<Node> parse(const QByteArray& input);
};

} // namespace sunlens
