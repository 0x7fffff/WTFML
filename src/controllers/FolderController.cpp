#include "FolderController.hpp"

#include "InlineMessage.hpp"
#include "MainWindowUI.hpp"
#include "OverlayHost.hpp"
#include "launcher/LauncherManager.hpp"
#include "manage/FolderManager.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QUrl>

#include <utility>

FolderController::FolderController(FolderManager *folders, LauncherManager *launcher,
                                   MainWindowUI *ui, QWidget *dialogParent, Host host,
                                   QObject *parent)
    : QObject(parent), m_folders(folders), m_launcher(launcher), m_ui(ui),
      m_dialogParent(dialogParent), m_host(std::move(host)) {}

void FolderController::log(const QString &text) const {
    if (m_host.log) m_host.log(text);
}

void FolderController::add() {
    const QString folder = QFileDialog::getExistingDirectory(
        m_dialogParent, tr("选择 Minecraft 游戏文件夹"), QDir::homePath());
    if (folder.isEmpty()) return;
    if (!m_folders->addFolder(folder)) {
        InlineMessage::warning(m_ui->overlay(), tr("添加文件夹失败"),
                               tr("「%1」不是一个存在的目录。").arg(folder));
        return;
    }
    log(tr("已添加文件夹：%1").arg(folder));
    m_folders->setCurrentFolder(folder);
}

void FolderController::removeFromList(const QString &folder) {
    if (m_folders->isDefaultFolder(folder)) {
        InlineMessage::information(
            m_ui->overlay(), tr("移除文件夹"),
            tr("「%1」是启动器自带的游戏文件夹，移不掉。\n它是别的目录都被移走时唯一的退路。")
                .arg(QDir::toNativeSeparators(folder)));
        return;
    }
    QString question = tr("「%1」会从上面这个列表里移除。\n\n"
                          "文件夹本身和里面的文件都不会被删掉 —— 这一步只动启动器的列表，"
                          "盘上一个字节都不动。")
                           .arg(QDir::toNativeSeparators(folder));
    if (m_folders->currentFolder() == folder) {
        question += tr("\n\n它是启动器当前在用的那一份，移除之后改用「%1」。")
                        .arg(QDir::toNativeSeparators(FolderManager::defaultFolder()));
    }
    if (!InlineMessage::question(m_ui->overlay(), tr("从列表里移除文件夹"), question,
                                 tr("是"), tr("否"), true)) return;
    if (!m_folders->removeFolder(folder)) {
        InlineMessage::warning(m_ui->overlay(), tr("移除文件夹"),
                               tr("没能把「%1」从列表里移除。")
                                   .arg(QDir::toNativeSeparators(folder)));
        return;
    }
    log(tr("已从列表里移除文件夹：%1").arg(folder));
}

void FolderController::action(const QString &path, int action) {
    switch (action) {
    case MainWindowUI::FolderActionRefresh: refresh(path); return;
    case MainWindowUI::FolderActionRename: rename(path); return;
    case MainWindowUI::FolderActionOpen: open(path); return;
    case MainWindowUI::FolderActionRemove: removeFromList(path); return;
    case MainWindowUI::FolderActionDelete: removeFromDisk(path); return;
    }
    log(tr("不认识的文件夹动作：%1").arg(action));
}

void FolderController::refresh(const QString &path) {
    const bool onDisk = QFileInfo(path).isDir();
    if (m_host.refreshList) m_host.refreshList();
    if (path == m_folders->currentFolder() && m_host.refreshInstances) m_host.refreshInstances();
    log(onDisk ? tr("已刷新游戏文件夹：%1").arg(QDir::toNativeSeparators(path))
               : tr("游戏文件夹不在盘上：%1").arg(QDir::toNativeSeparators(path)));
}

void FolderController::rename(const QString &path) {
    const QString custom = m_folders->displayName(path);
    bool ok = false;
    const QString shown = m_host.title ? m_host.title(path) : QFileInfo(path).fileName();
    const QString input = InlineMessage::getText(
        m_ui->overlay(), tr("重命名游戏文件夹"),
        tr("新名字（现在显示的是「%1」）。\n"
           "它只用在启动器的这个列表上，磁盘上的目录名一个字都不动；\n"
           "清空这一行就恢复默认的名字。")
            .arg(shown), custom, &ok);
    if (!ok) return;
    const QString name = input.trimmed();
    if (name == custom) return;
    const QString problem = FolderManager::validateDisplayName(name);
    if (!problem.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("重命名游戏文件夹"), problem);
        return;
    }
    if (!m_folders->setDisplayName(path, name)) {
        InlineMessage::warning(m_ui->overlay(), tr("重命名游戏文件夹"),
                               tr("「%1」已经不在列表里了。")
                                   .arg(QDir::toNativeSeparators(path)));
        return;
    }
    log(name.isEmpty() ? tr("游戏文件夹恢复默认名字：%1").arg(QDir::toNativeSeparators(path))
                       : tr("游戏文件夹改名为「%1」：%2").arg(name, QDir::toNativeSeparators(path)));
}

void FolderController::open(const QString &path) {
    if (!QFileInfo(path).isDir()) {
        InlineMessage::warning(m_ui->overlay(), tr("打开游戏文件夹"),
                               tr("「%1」现在不在盘上（U 盘没插、或者目录被挪走了）。")
                                   .arg(QDir::toNativeSeparators(path)));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        InlineMessage::warning(m_ui->overlay(), tr("打开游戏文件夹"),
                               tr("没能打开「%1」。").arg(QDir::toNativeSeparators(path)));
    }
}

void FolderController::removeFromDisk(const QString &path) {
    if (m_folders->isDefaultFolder(path)) {
        InlineMessage::information(
            m_ui->overlay(), tr("删除游戏文件夹"),
            tr("「%1」是启动器自带的游戏文件夹，删不掉。\n它是别的目录都被移走时唯一的退路。")
                .arg(QDir::toNativeSeparators(path)));
        return;
    }
    if (m_launcher->hasRunningGame()) {
        InlineMessage::warning(m_ui->overlay(), tr("删除游戏文件夹"),
                               tr("现在有游戏正在运行，先关掉游戏再删。\n"
                                  "游戏正开着的时候，它占着的文件移不进回收站。"));
        return;
    }
    QString question = tr("「%1」这个文件夹和**里面的全部文件**都会被删掉"
                          "（会进回收站，可以从那里还原）。\n\n"
                          "存档、截图、自己放的模组都在这份目录里。\n"
                          "只想把它从列表里拿掉、盘上的东西留着的话，用「移除」。")
                           .arg(QDir::toNativeSeparators(path));
    if (m_folders->currentFolder() == path) {
        question += tr("\n\n它是启动器当前在用的那一份，删掉之后改用「%1」。")
                        .arg(QDir::toNativeSeparators(FolderManager::defaultFolder()));
    }
    if (!InlineMessage::question(m_ui->overlay(), tr("删除游戏文件夹"), question,
                                 tr("是"), tr("否"), false, true)) return;
    QString error;
    if (!m_folders->deleteFolder(path, &error)) {
        InlineMessage::critical(m_ui->overlay(), tr("删除游戏文件夹失败"), error);
        return;
    }
    log(tr("已删除游戏文件夹：%1").arg(QDir::toNativeSeparators(path)));
}
