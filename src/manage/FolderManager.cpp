/**
 * @file FolderManager.cpp
 * @brief 实现「游戏文件夹」列表的读取、添加、移除与切换。
 * @author LoserKugua
 * @date 2026-04-10
 * @note 见 FolderManager.hpp 的说明。
 */

#include "FolderManager.hpp"

#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include "../settings/LauncherSettings.hpp"
#include "../util/AppPaths.hpp"

FolderManager::FolderManager(LauncherSettings *settings, QObject *parent)
    : QObject(parent), m_settings(settings) {
    // 盘上那串路径是上一次运行写下的：用户可能已经把目录删了、挪了、或者换了盘符。
    // 一律重新归一化一遍，免得界面里同一个目录以两种写法各占一行。
    const QStringList stored = m_settings != nullptr ? m_settings->folders() : QStringList();
    for (const QString &entry : stored) {
        const QString path = normalized(entry);
        // 等于自带那份的要丢掉：启动器被拷到 D:\MC\ 之后，原先"用户添加的
        // D:\MC\.minecraft"就和自带那份撞上了，留着就是列表里两条一模一样的路径。
        if (path.isEmpty() || path == defaultFolder() || m_userFolders.contains(path)) {
            continue;
        }
        m_userFolders.append(path);
    }

    m_current = normalized(m_settings != nullptr ? m_settings->currentFolder() : QString());
    if (m_current.isEmpty() || !QFileInfo(m_current).isDir()) {
        // 当前那一份不在盘上（U 盘今天没插、目录被删或被挪走）：这次先用自带那份。
        // **刻意不把盘上那个值改掉** —— 它可能只是今天没挂上，插回去还该接着用它。
        // 代价是这次会话里它不再生效，右边那句"当前文件夹"会照实说是哪一份。
        if (!m_current.isEmpty()) {
            qWarning() << "FolderManager: 记着的游戏文件夹不在盘上，这次先用自带那份："
                       << m_current;
        }
        m_current = defaultFolder();
    }
}

QString FolderManager::normalized(const QString &path) {
    if (path.isEmpty()) {
        return QString();
    }
    const QFileInfo info(path);
    if (info.exists()) {
        // 目录真在的时候用 canonicalFilePath：它顺带统一盘符大小写、解掉符号链接和
        // `..`，否则 `D:/mc/.minecraft` 和 `d:\mc\.minecraft` 会被当成两个文件夹。
        const QString canonical = info.canonicalFilePath();
        if (!canonical.isEmpty()) {
            return QDir::cleanPath(canonical);
        }
    }
    // 目录还不存在（新装的启动器、还没下过东西）：只能按字面归一到绝对路径。
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString FolderManager::defaultFolder() { return normalized(AppPaths::defaultMinecraftDir()); }

bool FolderManager::isDefaultFolder(const QString &path) {
    const QString target = normalized(path);
    return !target.isEmpty() && target == defaultFolder();
}

QStringList FolderManager::folders() const {
    QStringList all;
    all.append(defaultFolder());
    all.append(m_userFolders);
    return all;
}

QString FolderManager::currentFolder() const { return m_current; }

bool FolderManager::addFolder(const QString &path) {
    const QString target = normalized(path);
    if (target.isEmpty() || !QFileInfo(target).isDir()) {
        return false;
    }
    // 自带那份、以及已经在列表里的：不重复追加，但它确实"现在在列表里"，所以算成功。
    if (target == defaultFolder() || m_userFolders.contains(target)) {
        return true;
    }
    m_userFolders.append(target);
    if (m_settings != nullptr) {
        m_settings->setFolders(m_userFolders);
    }
    emit foldersChanged();
    return true;
}

bool FolderManager::removeFolder(const QString &path) {
    const QString target = normalized(path);
    // 自带那份移不掉：它是 AppPaths 的默认值，也是"别的目录都不在盘上"时的唯一退路。
    if (target.isEmpty() || target == defaultFolder()) {
        return false;
    }
    if (!m_userFolders.removeOne(target)) {
        return false;
    }
    if (m_settings != nullptr) {
        m_settings->setFolders(m_userFolders);
    }
    emit foldersChanged();

    // 移除掉的正好是当前在用的那一份 → 退回自带那份。不退回的话启动器会指着一个
    // 已经不在列表里的目录（界面上没有它，用户想再选回来都没地方点）。
    if (target == m_current) {
        m_current = defaultFolder();
        if (m_settings != nullptr) {
            m_settings->setCurrentFolder(QString());
        }
        emit currentFolderChanged(m_current);
    }
    return true;
}

bool FolderManager::setCurrentFolder(const QString &path) {
    const QString target = normalized(path);
    if (target.isEmpty() || !QFileInfo(target).isDir() || !folders().contains(target)) {
        return false;
    }
    if (target == m_current) {
        // 值没变，但盘上记的可能是那个"不在盘上、这次先不用"的旧目录（见构造函数）。
        // 顺手把盘上写回当前这一份，免得每次开窗都警告一遍。不发信号：界面没必要重灌。
        if (m_settings != nullptr) {
            m_settings->setCurrentFolder(m_current == defaultFolder() ? QString() : m_current);
        }
        return true;
    }
    m_current = target;
    if (m_settings != nullptr) {
        m_settings->setCurrentFolder(target);
    }
    emit currentFolderChanged(m_current);
    return true;
}
