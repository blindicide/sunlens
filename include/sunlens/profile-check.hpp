#pragma once

#include "sunlens/profile.hpp"

#include <QSslConfiguration>
#include <QUrl>

namespace sunlens {

inline const QUrl
    ProfileCheckEndpoint(QStringLiteral("https://connectivitycheck.gstatic.com/generate_204"));

struct ProfileCheckResult {
    QString message;
    qint64 elapsedMs = -1;
};

[[nodiscard]] ProfileCheckResult
checkProfile(const QString& core, const QString& executable, const Node& node,
             const QUrl& endpoint = ProfileCheckEndpoint,
             const QSslConfiguration& tls = QSslConfiguration::defaultConfiguration());

} // namespace sunlens
