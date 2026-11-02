/**
 * @file MainWindowDropImport.cpp
 * @brief 导入整合包与"拖一个文件进来"：窗口拖放、实例设置页的拖放、以及导入那一串转交。
 *
 * 【为什么单独一个翻译单元】
 *
 * 这一片的主语是"用户扔进来一个文件"：`dragEnterEvent()` / `dropEvent()` / `handleWindowDrop()`
 * 是窗口那一层，`handleInstanceSettingsDrop()` 是实例设置页那一层（同一个文件落在不同页上
 * 意思不同），`onImportModpackClicked()` / `importModpackFromFile()` / `applyModpackImport()` /
 * `importDroppedModpack()` 是整合包那一路，`installLocalResourceFile()` 是单个资源文件。
 *
 * 【判据不在这一层】
 *
 * 拖进来的是什么（`.jar` 只看后缀、`.zip` 才读条目表）在 `ModpackImport` 那一层判；
 * 落在哪一页由 `InstanceSettingsDialog` 的页键决定；安装/导入的落盘在 `ImportController`。
 */

#include "MainWindow.hpp"

#include "..\ui\InstanceSettingsDialog.hpp"
#include "controllers/ImportController.hpp"
#include "download/ResourceCatalog.hpp"
#include "manage/ModpackImport.hpp"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

void MainWindow::onImportModpackClicked() {
    m_importController->onImportModpackClicked();
}

void MainWindow::importModpackFromFile(const QString &zipPath, const QString &namePrefill) {
    m_importController->importModpackFromFile(zipPath, namePrefill);
}

bool MainWindow::applyModpackImport(const QString &zipPath, const QString &instanceName,
                                    const QByteArray &manifestJson,
                                    ModpackImport::Format format) {
    return m_importController->applyModpackImport(zipPath, instanceName, manifestJson, format);
}

bool MainWindow::installLocalResourceFile(const QString &versionId,
                                          ResourceCatalog::Kind kind, const QString &path,
                                          QString *problemOut) {
    return m_importController->installLocalResourceFile(versionId, kind, path, problemOut);
}

void MainWindow::handleInstanceSettingsDrop(QWidget *page, const QString &versionId,
                                            const QStringList &paths) {
    m_importController->handleInstanceSettingsDrop(page, versionId, paths);
}

void MainWindow::importDroppedModpack(const QString &zipPath, const QString &namePrefill) {
    m_importController->importDroppedModpack(zipPath, namePrefill);
}

void MainWindow::handleWindowDrop(const QStringList &paths) {
    m_importController->handleWindowDrop(paths);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    // 准入判据**就是**实例设置那一页用的那一个（同一个函数，见 .hpp 里那段 NOTE）：
    // 拖的是一串本地文件才接。
    if (InstanceSettingsDialog::droppedFilePaths(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    const QStringList paths = InstanceSettingsDialog::droppedFilePaths(event->mimeData());
    if (paths.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    handleWindowDrop(paths);
}
