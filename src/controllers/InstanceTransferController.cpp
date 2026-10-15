#include "InstanceTransferController.hpp"

#include "InlineMessage.hpp"
#include "launcher/LauncherManager.hpp"
#include "manage/InstanceFiles.hpp"
#include "manage/VersionManager.hpp"
#include "manage/WorldFiles.hpp"
#include "util/AppPaths.hpp"

#include <QDir>

#include <utility>

InstanceTransferController::InstanceTransferController(VersionManager *versions,
                                                       LauncherManager *launcher,
                                                       OverlayHost *overlay, Host host,
                                                       QObject *parent)
    : QObject(parent), m_versions(versions), m_launcher(launcher), m_overlay(overlay),
      m_host(std::move(host)) {}

QString InstanceTransferController::displayName(const QString &id) const {
    return m_host.displayName ? m_host.displayName(id) : id;
}
void InstanceTransferController::log(const QString &text) const {
    if (m_host.log) m_host.log(text);
}

bool InstanceTransferController::copyInstance(const QString &sourceId,
                                               const QString &destGameFolder,
                                               const QString &newName) {
    const QString name = newName.trimmed();
    if (name.isEmpty()) {
        InlineMessage::warning(m_overlay, tr("复制实例"), tr("副本得有个名字。"));
        return false;
    }
    const QString nameProblem = InstanceFiles::validateDirName(name);
    if (!nameProblem.isEmpty()) {
        InlineMessage::warning(m_overlay, tr("复制实例"), nameProblem);
        return false;
    }
    if (m_launcher->isVersionRunning(sourceId)) {
        InlineMessage::warning(m_overlay, tr("复制实例"),
                               tr("「%1」正在运行。先关掉游戏再复制 —— 游戏开着的时候拷出来的"
                                  "是一份写到一半的存档。")
                                   .arg(displayName(sourceId)));
        return false;
    }
    if (m_host.isBeingModified && m_host.isBeingModified(sourceId)) {
        InlineMessage::warning(m_overlay, tr("复制实例"),
                               tr("「%1」正在下载，等它装完再复制。")
                                   .arg(displayName(sourceId)));
        return false;
    }
    const QString sourceDir = AppPaths::versionDir(sourceId);
    if (InstanceFiles::pathIsSameOrInside(sourceDir, destGameFolder)) {
        InlineMessage::warning(
            m_overlay, tr("复制实例"),
            tr("「%1」不能复制到它自己里面去。\n\n你挑的是：\n%2\n\n那是这一份实例自己的目录"
               "（或者它底下的一个子目录）。请挑一个游戏文件夹 —— 也就是实例目录上面"
               "那一层，里面直接看得到 versions 文件夹的那一个。")
                .arg(displayName(sourceId), QDir::toNativeSeparators(destGameFolder)));
        return false;
    }
    QString error;
    if (!m_versions->copyInstance(sourceId, destGameFolder, name, &error)) {
        InlineMessage::critical(m_overlay, tr("复制实例失败"), error);
        return false;
    }
    if (m_host.refreshInstances) m_host.refreshInstances();
    const QString landed = QDir::toNativeSeparators(
        QDir(destGameFolder).absoluteFilePath(QStringLiteral("versions/") + name));
    log(tr("已复制实例 %1 → %2").arg(displayName(sourceId), landed));
    const bool sameFolder = QDir::cleanPath(destGameFolder).compare(
                                QDir::cleanPath(AppPaths::minecraftDir()), Qt::CaseInsensitive) == 0;
    if (sameFolder) {
        InlineMessage::information(m_overlay, tr("复制实例"),
                                   tr("「%1」已经复制好了，就在实例列表里。").arg(name));
    } else {
        InlineMessage::information(
            m_overlay, tr("复制实例"),
            tr("「%1」已经复制到另一个游戏文件夹里了：\n%2\n\n它不在当前这个游戏文件夹的实例"
               "列表里 —— 想用它就先把游戏文件夹切过去。")
                .arg(name, landed));
    }
    return true;
}

bool InstanceTransferController::importWorld(const QString &instanceId,
                                             const QString &worldDir,
                                             const QString &name) {
    const QString savesDir = m_host.instanceDirForKey
        ? m_host.instanceDirForKey(instanceId, QStringLiteral("save")) : QString();
    if (savesDir.isEmpty()) {
        InlineMessage::critical(m_overlay, tr("导入世界失败"), tr("认不出这个实例的存档目录。"));
        return false;
    }
    QString error;
    if (!WorldFiles::copyWorldInto(worldDir, savesDir, name, &error)) {
        InlineMessage::critical(m_overlay, tr("导入世界失败"), error);
        return false;
    }
    log(tr("导入世界：%1 → %2").arg(QDir::toNativeSeparators(worldDir),
                                      QDir::toNativeSeparators(savesDir)));
    return true;
}

bool InstanceTransferController::renameWorld(const QString &instanceId,
                                             const QString &worldDir,
                                             const QString &newName) {
    if (m_launcher->isVersionRunning(instanceId)) {
        InlineMessage::warning(m_overlay, tr("重命名世界"),
                               tr("游戏正在运行，先关掉游戏再改存档的名字。"));
        return false;
    }
    QString error;
    if (!WorldFiles::renameWorld(worldDir, newName, &error)) {
        InlineMessage::warning(m_overlay, tr("重命名世界"), error);
        return false;
    }
    log(tr("世界改名：%1 → %2").arg(QDir::toNativeSeparators(worldDir), newName.trimmed()));
    return true;
}
