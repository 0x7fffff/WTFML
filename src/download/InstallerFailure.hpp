#pragma once
#include <QString>

namespace InstallerFailure {
inline bool certificateFailure(const QString &output) {
    return output.contains(QStringLiteral("PKIX path building failed"), Qt::CaseInsensitive) ||
           output.contains(QStringLiteral("unable to find valid certification path"), Qt::CaseInsensitive) ||
           output.contains(QStringLiteral("Failed to validate certificates"), Qt::CaseInsensitive);
}
} // namespace InstallerFailure
