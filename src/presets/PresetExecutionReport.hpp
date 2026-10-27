#pragma once
#include "PresetApply.hpp"
#include <QJsonObject>

namespace Presets {
QString presetExecutionReportPath(const QString &instanceRoot);
QJsonObject readPresetExecutionReport(const QString &root, const PresetManifest &manifest, QString *problem);
bool writePresetExecutionReport(const QString &root, const PresetManifest &manifest,
    const PresetApplyResult &result, bool finished, const QJsonObject &previous, QString *problem);
}
