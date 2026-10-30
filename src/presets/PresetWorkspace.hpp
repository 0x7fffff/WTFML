#pragma once
#include "PresetStore.hpp"
namespace Presets {
QString editorSectionPart(const PresetSection &section);
QJsonObject editorDocument(const PresetManifest &manifest);
struct EditorSaveResult {
    bool ok = false;
    QString problem;
    PresetManifest manifest;
    QJsonObject document;
};
EditorSaveResult saveEditorDocument(const QString &root, const QJsonObject &document, bool draft);
} // namespace Presets
