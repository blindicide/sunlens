#pragma once

#include "sunlens/core-adapter.hpp"

namespace sunlens {

struct CoreBinarySpec {
    QString name;
    QString version;
    QByteArray hash;
    QByteArray versionPrefix;
    QStringList validationArguments;
};

[[nodiscard]] ValidationResult validateExternalCore(const QString& executable,
                                                    const QJsonDocument& config,
                                                    const CoreBinarySpec& spec);

} // namespace sunlens
