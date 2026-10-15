#include "controllers/LauncherPreferencesController.hpp"

#include "MainWindowUI.hpp"
#include "download/ResourceStore.hpp"
#include "manage/VersionManager.hpp"
#include "settings/GlobalJavaSettings.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSpinBox>

#include <utility>

LauncherPreferencesController::LauncherPreferencesController(
    MainWindowUI *ui, LauncherSettings *settings, GlobalJavaSettings *javaSettings,
    VersionManager *versions, ResourceStore *resources, Host host, QObject *parent)
    : QObject(parent), m_ui(ui), m_settings(settings), m_javaSettings(javaSettings),
      m_versions(versions), m_resources(resources), m_host(std::move(host)) {}

void LauncherPreferencesController::log(const QString &message) const {
    if (m_host.log) m_host.log(message);
}

void LauncherPreferencesController::pushVersionNameOptions(const VersionNameOptions &options) {
    const QSignalBlocker a(m_ui->getVersionNameCustomCheck());
    const QSignalBlocker b(m_ui->getVersionNameRealCheck());
    const QSignalBlocker c(m_ui->getVersionNameLoaderCheck());
    const QSignalBlocker d(m_ui->getVersionNameLoaderVersionCheck());
    m_ui->getVersionNameCustomCheck()->setChecked(options.showCustomName);
    m_ui->getVersionNameRealCheck()->setChecked(options.showRealVersion);
    m_ui->getVersionNameLoaderCheck()->setChecked(options.showLoaderName);
    m_ui->getVersionNameLoaderVersionCheck()->setChecked(options.showLoaderVersion);
}

void LauncherPreferencesController::launcherBehaviourChanged() {
    m_settings->setLauncherBehaviour(
        LauncherBehaviour(m_ui->getLauncherBehaviourCombo()->currentData().toInt()));
}

void LauncherPreferencesController::downloadSourceChanged() {
    m_settings->setDownloadSource(
        DownloadSource(m_ui->getDownloadSourceCombo()->currentData().toInt()));
    syncDownloadSource();
}

void LauncherPreferencesController::downloadConcurrencyChanged() {
    m_settings->setDownloadConcurrency(
        m_ui->getDownloadConcurrencyCombo()->currentData().toInt());
    syncDownloadConcurrency();
    log(tr("下载：同时最多 %1 个文件在下载（对正在下的这一批也生效，已经在下的那几个不会被叫停）。")
            .arg(m_settings->downloadConcurrency()));
}

void LauncherPreferencesController::downloadAutoIntoInstanceChanged() {
    const bool on = m_ui->getDownloadAutoIntoInstanceCheck()->isChecked();
    m_settings->setDownloadAutoIntoInstance(on);
    log(on ? tr("下载：从实例设置里点「下载新模组」时不再问存哪儿，直接放进该实例。")
           : tr("下载：不管从哪儿点进来的，每次都会问存哪儿。"));
}

void LauncherPreferencesController::warnMissingDependenciesChanged() {
    const bool on = m_ui->getWarnMissingDependenciesCheck()->isChecked();
    m_settings->setWarnMissingDependencies(on);
    log(on ? tr("下载：点下载模组 / 光影包时会先检查那个实例缺不缺前置。")
           : tr("下载：不再检查前置，只下你点的那一版。"));
}

void LauncherPreferencesController::showGameLogChanged() {
    const bool show = m_ui->getShowGameLogCheck()->isChecked();
    m_settings->setShowGameLog(show);
    log(show ? tr("游戏日志：启动游戏时会开一个日志窗。")
             : tr("游戏日志：不再自动开日志窗。"));
}

void LauncherPreferencesController::warnDuplicateLaunchChanged() {
    m_settings->setWarnDuplicateLaunch(m_ui->getWarnDuplicateLaunchCheck()->isChecked());
}

void LauncherPreferencesController::warnWorldLossChanged() {
    m_settings->setWarnWorldLoss(m_ui->getWarnWorldLossCheck()->isChecked());
}

void LauncherPreferencesController::processPriorityChanged() {
    m_settings->setProcessPriority(
        ProcessPriority(m_ui->getProcessPriorityCombo()->currentData().toInt()));
}

void LauncherPreferencesController::gameWindowSizeChanged() {
    m_settings->setGameWindowSize(m_ui->getGameWindowWidthSpin()->value(),
                                  m_ui->getGameWindowHeightSpin()->value());
}

void LauncherPreferencesController::gameWindowModeChanged() {
    m_settings->setGameWindowMode(
        GameWindowMode(m_ui->getGameWindowModeCombo()->currentData().toInt()));
    updateGameWindowSizeEnabled();
}

void LauncherPreferencesController::updateGameWindowSizeEnabled() {
    const bool enabled = m_settings->gameWindowMode() != GameWindowMode::Maximized;
    m_ui->getGameWindowWidthSpin()->setEnabled(enabled);
    m_ui->getGameWindowHeightSpin()->setEnabled(enabled);
    m_ui->getGameWindowSizeTimesLabel()->setEnabled(enabled);
}

void LauncherPreferencesController::syncDownloadSource() {
    m_versions->setUseMirror(m_settings->downloadSource() == DownloadSource::Mirror);
}

void LauncherPreferencesController::syncDownloadConcurrency() {
    m_versions->setDownloadConcurrency(m_settings->downloadConcurrency());
}

void LauncherPreferencesController::syncCatalogCredentials() {
    m_versions->setCurseForgeApiKey(m_settings->curseForgeApiKey());
    m_versions->setCurseForgeApiBaseOverride(m_settings->curseForgeApiBaseOverride());
    m_resources->setCurseForgeApiKey(m_settings->curseForgeApiKey());
    m_resources->setCurseForgeApiBaseOverride(m_settings->curseForgeApiBaseOverride());
    m_versions->setModrinthApiBaseOverride(m_settings->modrinthApiBaseOverride());
    m_resources->setModrinthApiBaseOverride(m_settings->modrinthApiBaseOverride());
}

void LauncherPreferencesController::curseForgeApiKeyEdited() {
    m_settings->setCurseForgeApiKey(m_ui->getCurseForgeApiKeyEdit()->text());
    syncCatalogCredentials();
}

void LauncherPreferencesController::versionNameOptionChanged() {
    VersionNameOptions options;
    options.showCustomName = m_ui->getVersionNameCustomCheck()->isChecked();
    options.showRealVersion = m_ui->getVersionNameRealCheck()->isChecked();
    options.showLoaderName = m_ui->getVersionNameLoaderCheck()->isChecked();
    options.showLoaderVersion = m_ui->getVersionNameLoaderVersionCheck()->isChecked();
    m_settings->setVersionNameOptions(options);
    pushVersionNameOptions(m_settings->versionNameOptions());
    if (m_host.refreshInstalled) m_host.refreshInstalled();
    if (m_host.refreshAvailable) m_host.refreshAvailable();
}

void LauncherPreferencesController::applyExtraJvmArgs() {
    const QString args = m_ui->getExtraJvmArgsEdit()->toPlainText().trimmed();
    m_javaSettings->setExtraJvmArgs(args);
    m_javaSettings->save();
    log(tr("已保存全局 JVM 参数：%1").arg(args.isEmpty() ? tr("（空）") : args));
}
