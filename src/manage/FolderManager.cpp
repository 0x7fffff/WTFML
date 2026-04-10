/**
 * @file FolderManager.cpp
 * @brief Implements folder validation and list management.
 * @author LoserKugua
 * @date 2026-04-10
 * @note 提供简单的目录增删功能，与 UI 交互分离。
 */

#include "FolderManager.hpp"

#include <QDir>
#include <QFileInfo>

FolderManager::FolderManager(QObject *parent) : QObject(parent) {}

bool FolderManager::addFolder(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        return false;
    }
    if (!m_folders.contains(path)) {
        m_folders.append(path);
    }
    return true;
}

bool FolderManager::removeFolder(const QString &path) { return m_folders.removeOne(path); }

QStringList FolderManager::folders() const { return m_folders; }
