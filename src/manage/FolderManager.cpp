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
