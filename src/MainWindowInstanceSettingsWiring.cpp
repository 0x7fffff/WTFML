/**
 * @file MainWindowInstanceSettingsWiring.cpp
 * @brief 「实例设置」那一扇窗背后的页面数据与动作：配置文件编辑器、世界设置、模组可用性、服务器列表。
 *
 * 【为什么单独一个翻译单元】
 *
 * 这一片的主语是**对话框要什么、按下之后交给谁**：`instanceIdOfPath()`（从盘上路径反查
 * 是哪一页、哪个实例）、配置文件的打开 / 保存 / 恢复与「世代备份」保留几份、世界设置的
 * 进出与落盘、`instanceHasMod()` / `instanceShaderUsable()` 那几条可用性判据、服务器列表的
 * 读与测速，以及文件页那两个开关。主窗口其余部分一律经 `openInstanceSettings()` 进这一扇窗，
 * 窗里的细节全在这里 —— 分出来之后界面改文案 / 加一页只在一个文件里翻。
 *
 * 【判据不在这一层】
 *
 * 盘上的读写归 `InstanceEditorController`（配置 / 世界 / 服务器列表）与
 * `ServerPingController`（测速）；存档那一份 NBT 的人话投影在 `NbtUtil`，
 * 服务器列表文件的形状在 `ServerListFile`。这一层只做"哪一个键 → 哪一件事"的分派。
 */

#include "MainWindow.hpp"

#include "..\ui\InstanceSettingsDialog.hpp"
#include "..\ui\WorldSettingsPage.hpp"
#include "controllers/InstanceEditorController.hpp"
#include "controllers/InstanceFileController.hpp"
#include "controllers/InstancePagesController.hpp"
#include "controllers/MainWindowViewData.hpp"
#include "controllers/ServerPingController.hpp"
#include "manage/ServerListFile.hpp"
#include "manage/VersionManager.hpp"
#include "settings/LauncherSettings.hpp"
#include "util/AppPaths.hpp"
#include "util/NbtUtil.hpp"

#include <QDir>
#include <QFileInfo>
#include <QTimer>

using namespace MainWindowViewData;

// ---------- 「世代备份」那三件事（见 .hpp 那一段）----------

namespace {

/// 每个实例的备份留最近多少份。**不按文件算、按"存了几次"算**：备份是"某一刻整个 config/
/// 的一批"，用不着按单个文件各留二十份（那会长出二十个文件夹、每个里面一个文件）。
///
/// NOTE: 20 这个数是拍的，不是量出来的 —— 它只影响"最多能回退几步"，而每一步都比上一步
/// 更不像用户想要的。真要按量出的数来定，得先知道用户平均多久存一次（今天没有这个
/// 数）。这里宁可留够：20 份 × 一份配置那几十 KB 是几个 MB。
[[maybe_unused]] constexpr int kConfigBackupGenerations = 20;

}  // namespace

QString MainWindow::instanceIdOfPath(const QString &path) const {
    // cleanPath 顺手把 `\` 换成 `/`（Windows 上真有可能递进来一份原生分隔符的）—— 后面
    // 每一步都只认一种写法，省得两个方向的字符串各比一遍。
    const QString clean = QDir::cleanPath(path);
    const QString prefix = AppPaths::versionsDir() + QLatin1Char('/');
    if (!clean.startsWith(prefix)) {
        return QString();  // 不在任何实例底下（理论上到不了这儿：这一格只列 config/ 里的文件）
    }
    const QString rest = clean.mid(prefix.size());
    const int slash = rest.indexOf(QLatin1Char('/'));
    // `slash <= 0` 一并把"versions/ 底下直接一个文件"那种挡掉了（那不是实例）。
    return slash <= 0 ? QString() : rest.left(slash);
}

QString MainWindow::latestConfigBackup(const QString &instanceId, const QString &path) const {
    return m_instanceEditorController->latestConfigBackup(instanceId, path);
}

void MainWindow::pruneConfigBackups(const QString &instanceId, int keep) {
    m_instanceEditorController->pruneConfigBackups(instanceId, keep);
}

void MainWindow::openConfigFile(InstanceSettingsDialog *dialog, const QString &path) {
    m_instanceEditorController->openConfigFile(dialog, path);
}

void MainWindow::saveConfigFile(InstanceSettingsDialog *dialog, const QString &path,
                                const QByteArray &bytes) {
    m_instanceEditorController->saveConfigFile(dialog, path, bytes);
}

void MainWindow::restoreConfigFile(InstanceSettingsDialog *dialog, const QString &path) {
    m_instanceEditorController->restoreConfigFile(dialog, path);
}

bool MainWindow::backupWorldLevelData(const QString &worldDir, QString *problemOut) {
    return m_instanceEditorController->backupWorldLevelData(worldDir, problemOut);
}

QString MainWindow::latestWorldBackup(const QString &worldDir) const {
    return m_instanceEditorController->latestWorldBackup(worldDir);
}

void MainWindow::showWorldSettingsPage(InstanceSettingsDialog *dialog, const QString &worldDir) {
    m_instanceEditorController->showWorldSettingsPage(dialog, worldDir);
}

void MainWindow::leaveWorldSettings(InstanceSettingsDialog *dialog) {
    m_instanceEditorController->leaveWorldSettings(dialog);
}

void MainWindow::saveWorldSettings(InstanceSettingsDialog *dialog) {
    m_instanceEditorController->saveWorldSettings(dialog);
}

void MainWindow::leaveConfigEditor(InstanceSettingsDialog *dialog) {
    m_instanceEditorController->leaveConfigEditor(dialog);
}

bool MainWindow::confirmDiscardConfigEdits() {
    return m_instanceEditorController->confirmDiscardConfigEdits();
}

bool MainWindow::confirmDiscardWorldEdits() {
    return m_instanceEditorController->confirmDiscardWorldEdits();
}

bool MainWindow::instanceHasMod(const QString &versionId, const QString &nameToken) const {
    // 判断（文件名切词、命中判据）在 `InstancePagesController::hasMod()` /
    // `jarNameHasToken()`。这个成员留着是因为它也有别的读者（常用目录那一列认 ReplayMod、
    // 自动归类那一带），它们拿的是主窗口。
    return InstancePagesController::hasMod(versionId, nameToken);
}

bool MainWindow::instanceShaderUsable(const QString &versionId) const {
    return InstancePagesController::shaderUsable(m_versionManager, versionId);
}

bool MainWindow::instanceModsUsable(const QString &versionId) const {
    return InstancePagesController::modsUsable(m_versionManager, versionId);
}

void MainWindow::loadServerList(InstanceSettingsDialog *dialog, const QString &versionId) const {
    if (dialog == nullptr) {
        return;
    }
    // 「文件不在盘上」（Missing）不算 problem：那一页会说「这个实例还没有加过服务器。」
    // —— 那正是实话。只有"文件在但是读不动"才把原因递过去。
    dialog->setServerList(serverListOf(versionId), serverListProblem(versionId));
    QStringList favorites;
    for (const auto &address : dialog->serverAddresses())
        if (m_settings->isServerFavorite(address)) favorites.append(address);
    dialog->setServerFavorites(favorites);
}

QList<QPair<QString, QString>> MainWindow::serverListOf(const QString &versionId) const {
    QList<NbtUtil::ServerEntry> entries;
    QString problem;
    // 路径从 `ServerListFile::pathFor()` 来 —— 和写侧（改 / 加 / 删）**同一份实现**。
    NbtUtil::readServerList(ServerListFile::pathFor(versionId), &entries, &problem);
    QList<QPair<QString, QString>> servers;
    servers.reserve(entries.size());
    for (const NbtUtil::ServerEntry &entry : entries) {
        servers.append(qMakePair(entry.name, entry.address));
    }
    return servers;
}

QString MainWindow::serverListProblem(const QString &versionId) const {
    QList<NbtUtil::ServerEntry> entries;
    QString problem;
    NbtUtil::readServerList(ServerListFile::pathFor(versionId), &entries, &problem);
    return problem;
}

void MainWindow::pingServers(InstanceSettingsDialog *dialog, const QString &versionId) {
    m_serverPingController->ping(dialog, versionId);
}

bool MainWindow::toggleInstanceFile(const QString &path, QString *problem) {
    return m_instanceFileController->toggle(path, problem);
}

bool MainWindow::toggleInstanceFileForTest(const QString &path, QString *problem) {
    return toggleInstanceFile(path, problem);
}
