#pragma once
#include <QObject>
#include "download/ResourceCatalog.hpp"
#include "manage/ModpackImport.hpp"
class MainWindow;
class QWidget;
class ImportController final : public QObject {
    Q_OBJECT
public:
    explicit ImportController(MainWindow *owner);
    void onImportModpackClicked();
    void importModpackFromFile(const QString &zipPath, const QString &namePrefill);
    bool applyModpackImport(const QString &zipPath, const QString &instanceName, const QByteArray &manifestJson, ModpackImport::Format format);
    bool installLocalResourceFile(const QString &versionId, ResourceCatalog::Kind kind, const QString &path, QString *problemOut);
    void handleInstanceSettingsDrop(QWidget *page, const QString &versionId, const QStringList &paths);
    void importDroppedModpack(const QString &zipPath, const QString &namePrefill);
    void handleWindowDrop(const QStringList &paths);
private:
    MainWindow *m_owner = nullptr;
};

