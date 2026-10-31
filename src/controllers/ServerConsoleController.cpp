#include "controllers/ServerConsoleController.hpp"
#include "TextMarks.hpp"
#include "util/SecretRedactor.hpp"

#include "InlineMessage.hpp"
#include "MainWindowUI.hpp"
#include "OverlayHost.hpp"
#include "ServerConsolePage.hpp"
#include "controllers/ServerWorkspaceController.hpp"
#include "server/ServerProcess.hpp"
#include "server/ServerRegistry.hpp"
#include "server/ServerRun.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTabWidget>

#include <utility>

ServerConsoleController::ServerConsoleController(ServerWorkspaceController *workspace,
                                                 MainWindowUI *ui, QWidget *pageParent, Host host,
                                                 QObject *parent)
    : QObject(parent), m_workspace(workspace), m_ui(ui), m_pageParent(pageParent),
      m_host(std::move(host)) {}

void ServerConsoleController::open(const QString &serverId) {
    OverlayHost *overlay = m_ui == nullptr ? nullptr : m_ui->overlay();
    if (overlay == nullptr || serverId.isEmpty()) {
        return;
    }
    if (m_page == nullptr) {
        m_page = new ServerConsolePage(m_pageParent);
        m_page->setMaxLines(ServerRun::ServerProcess::maxLogLines());
        // NOTE: 这三条只接一次（页面复用）。**认人认 `m_serverId`，不认"列表上现在
        // 选中谁"** —— 用户完全可能边看控制台边点别处（同 m_updateCenterInstanceId
        // 那一段的理由）。
        connect(m_page, &ServerConsolePage::commandSubmitted, this,
                [this](const QString &command) {
                    ServerRun::ServerProcess *process = m_workspace->process(m_serverId);
                    if (process == nullptr || !process->sendCommand(command)) {
                        if (m_host.log) m_host.log(tr("这一条没有送到服务端（它已经不在跑了）。"));
                    }
                });
        connect(m_page, &ServerConsolePage::stopRequested, this, [this] {
            if (m_host.stop) m_host.stop(m_serverId);
        });
        connect(m_page, &ServerConsolePage::forceKillRequested, this, [this] {
            if (m_host.forceKill) m_host.forceKill(m_serverId);
        });
        connect(m_page, &ServerConsolePage::exportLogRequested, this,
                &ServerConsoleController::exportLog);
    }
    m_serverId = serverId;
    const ServerRegistry::Entry entry = ServerRegistry::describe(serverId);
    m_page->setServerName(entry.name.isEmpty() ? serverId : entry.name);
    const ServerRun::ServerProcess *process = m_workspace->process(serverId);
    // 开页时把**攒下来的**那些行灌一遍：这一页关着的时候它也一直在跑，重新打开不该
    // 只剩"从打开这一刻起"的日志。
    m_page->setLog(process != nullptr ? process->logLines() : QStringList());
    refresh();  // 状态那几行的唯一出处
    // 开页要说清 `ownerTab`、并且**先切过去**（见 OverlayHost::execPage 那段）：这一页
    // 是从服务端列表上开出来的，那一位就是"实例"这一颗。不切的话，页眉上那颗返回箭头
    // 收页之后用户会落在一个跟刚才没关系的选项卡上。
    const int instancesTab = MainWindowUI::instancesTabIndex();
    if (QTabWidget *tabs = m_ui->getTabWidget()) {
        if (instancesTab >= 0 && instancesTab < tabs->count()) {
            tabs->setCurrentIndex(instancesTab);
        }
    }
    overlay->execPage(m_page, ServerConsolePage::windowTitle(), instancesTab);
    // 收页之后"这一页说的是谁"就没意义了 —— 留着的话，返回键一按、下一行日志还会往一个
    // 已经收起来的页面上写。
    m_serverId.clear();
}

void ServerConsoleController::refresh() {
    if (m_page == nullptr || m_serverId.isEmpty()) {
        return;
    }
    const ServerRun::ServerProcess *process = m_workspace->process(m_serverId);
    const ServerModel::Status status =
        process != nullptr ? process->status() : ServerModel::Status::Stopped;
    const bool running = process != nullptr && process->isRunning();
    const bool stopping = status == ServerModel::Status::Stopping;
    m_page->setStatusText(ServerModel::statusName(status));
    // 「停止」在停止中就不给按了（已经在停了，再点只是催它）；「强制结束」照宽限那一套来。
    m_page->setStopAvailable(running && !stopping);
    m_page->setForceKillAvailable(process != nullptr && process->forceKillAllowed());
    // 没在跑的时候底下那格输入不能用 —— 写进去没人读（那比灰着更容易让人以为坏了）。
    m_page->setCommandAvailable(running);
}

void ServerConsoleController::appendLine(const QString &serverId, const QString &line) {
    if (m_page != nullptr && m_serverId == serverId) {
        m_page->appendLine(line);
    }
}

void ServerConsoleController::announceForceKill(const QString &serverId, int seconds) {
    if (m_page == nullptr || m_serverId != serverId) {
        return;
    }
    // 往控制台里也插一行：用户正盯着这一页看，那颗按钮为什么突然亮了得有个说法。
    // `[wtfml]` 前缀把它和服务端自己吐的那些行分开（它不会吐这个前缀）。
    m_page->appendLine(QStringLiteral("[wtfml] ") + ServerRun::forceKillNote(seconds));
    refresh();
}

void ServerConsoleController::forget(const QString &serverId) {
    if (m_serverId == serverId) {
        m_serverId.clear();
    }
}

void ServerConsoleController::exportLog() {
    if (m_page == nullptr || m_serverId.isEmpty()) {
        return;
    }
    OverlayHost *overlay = m_ui == nullptr ? nullptr : m_ui->overlay();
    const QString text = SecretRedactor::redact(m_page->allLogText());
    if (text.isEmpty()) {
        InlineMessage::warning(overlay, tr("还没有日志"),
                               tr("这个控制台还没有收到任何输出，导出来会是一个空文件。"));
        return;
    }
    const ServerRegistry::Entry entry = ServerRegistry::describe(m_serverId);
    // 文件名里不许出现路径分隔符和 Windows 的那几个非法字符 —— 服务端名是用户自己起的。
    QString safeName = entry.name.isEmpty() ? m_serverId : entry.name;
    safeName.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")),
                     QStringLiteral("_"));
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString suggested =
        QDir(desktop.isEmpty() ? QDir::homePath() : desktop)
            .filePath(QStringLiteral("server-%1-%2.log")
                          .arg(safeName, QDateTime::currentDateTime().toString(
                                             QStringLiteral("yyyyMMdd-HHmmss"))));
    const QString path = QFileDialog::getSaveFileName(
        m_pageParent, tr("导出服务端日志"), suggested,
        tr("文本文件 (*.txt *.log);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }
    const auto bytes = text.toUtf8();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(bytes) != bytes.size() || !file.commit()) {
        const QString error = file.errorString();
        if (m_host.log) {
            m_host.log(tr("导出服务端日志失败：%1").arg(error));
        }
        InlineMessage::warning(overlay, tr("导出失败"),
                               tr("这个文件没能写出来（%1）：\n%2")
                                   .arg(QDir::toNativeSeparators(path), error));
        return;
    }
    if (m_host.log) {
        m_host.log(TextMarks::done(tr("服务端日志已导出：%1").arg(QDir::toNativeSeparators(path))));
    }
}
