/**
 * @file MainWindowSettings.cpp
 * @brief 「设置」选项卡里那一片接线：`applySettingsToUi()` 与各颗 `on…Changed()`。
 *
 * 【为什么单独一个翻译单元】
 *
 * 这一片只认两样东西：`MainWindowUI` 上那些控件，和"改了这个值要落到哪个键上"——
 * 后者逐项归 `LauncherPreferencesController`（启动器行为、下载、版本名格式）或
 * `ThemeSettingsController`（背景图与配色）。它不碰启动、下载批次、实例树，留在 8000 行的
 * `MainWindow.cpp` 里只会让"改一个开关"变成在整扇窗里翻。类没变，还是 `MainWindow`，
 * 只是实现分在两个 .cpp 里。
 *
 * 【落盘时机逐项不同】
 *
 * 这是这一片最容易做错的地方，所以每一项都在它自己那个函数上面写了"什么时候存"
 * （见 `applySettingsToUi()` 上面那段）。这一段里**不许**把几项攒起来一起写 ——
 * 攒起来就要另存一份"改过没有"的状态，而那一份状态迟早和界面对不上。
 */

#include "MainWindow.hpp"

#include "..\ui\Animations.hpp"
#include "..\ui\Theme.hpp"
#include "..\ui\TitleBar.hpp"
#include "auth/SkinApi.hpp"
#include "controllers/JavaSettingsController.hpp"
#include "controllers/LauncherPreferencesController.hpp"
#include "controllers/MainWindowViewData.hpp"
#include "controllers/PresetController.hpp"
#include "controllers/ThemeSettingsController.hpp"
#include "manage/PlayerSkinStore.hpp"
#include "manage/VersionManager.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include "settings/LauncherSettings.hpp"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>
#include <QSignalBlocker>

using namespace MainWindowViewData;

// ======================================================================
// 「设置」选项卡
// ======================================================================
//
// 分工：MainWindowUI 只把控件搭出来（含"选项名称在左、控件在右"的两列布局），值和
// 持久化全在这一层。枚举 → 文案的映射也在这里 —— 界面层不该为了画一个下拉框去
// include src/settings，而 tr() 的文案本来就该和业务逻辑待在一起。
//
// 落盘时机逐项不同，这是这块最容易做错的地方，所以每一项都在自己的函数里说清楚：
// Java / 内存 / 启动后 / 下载源 / 版本名格式 —— 改完立刻存
// JVM 参数 —— 点「应用」才存（那是直接进命令行的东西，不能存半条）

/// 「同时最多下几个」那颗下拉框里每一行的文字。
///
/// 档位表（6/12/24/48、上下限、越界怎么办）在 `DownloadConcurrency.hpp` —— 那里是唯一出处；
/// 这里只有**这一层的文字**（和枚举 → 文案那些一样，界面层不该为了画一个下拉框去 include
/// src/ 的东西）。默认那一档多两个字：「（默认）」标的是**出厂**那一档，不跟着用户当前选谁走
/// —— 它的用处是"我不知道该选哪个"，所以不写的话这一格就没有那个可以照抄的答案。
static QString downloadConcurrencyChoiceText(int value) {
    if (value == DownloadConcurrency::kDefault) {
        return QObject::tr("%1 个（默认）").arg(value);
    }
    return QObject::tr("%1 个").arg(value);
}

/// 「下载速度上限」那颗下拉框里每一行的文字。
///
/// 档位表在 `DownloadSpeedLimit.hpp`（字节/秒，**唯一出处**）；这里只有这一层的文字。
/// 0 那一档写「不限」而不是「0 B/s」—— 用户读的是"限没限速"，不是那个数。
static QString downloadSpeedLimitChoiceText(qint64 bytesPerSecond) {
    if (bytesPerSecond <= 0) {
        return QObject::tr("不限");
    }
    if (bytesPerSecond >= 1024 * 1024) {
        return QObject::tr("%1 MB/s").arg(QLocale().toString(
            double(bytesPerSecond) / (1024.0 * 1024.0), 'g', 3));
    }
    return QObject::tr("%1 KB/s").arg(bytesPerSecond / 1024);
}

/// 「代理」那颗下拉框里每一行的文字。
///
/// 三档的语义在 `DownloadProxy.hpp`（唯一出处）；这里只有这一层的文字。
static QString downloadProxyChoiceText(DownloadProxy::Mode mode) {
    switch (mode) {
    case DownloadProxy::Mode::Direct:
        return QObject::tr("直连（不走代理）");
    case DownloadProxy::Mode::Manual:
        return QObject::tr("手动指定");
    case DownloadProxy::Mode::System:
        break;
    }
    return QObject::tr("跟随系统");
}

void MainWindow::applySettingsToUi() {
    // 测试地址覆盖（**产品永远是空串**）：必须在第一次 fetchAvailableVersions() / 皮肤
    // 刷新之前落到数据层，否则那一发已经打到官方地址上了。
    m_versionManager->setVersionManifestUrlOverride(m_settings->versionManifestUrlOverride());
    PlayerSkinStore::setEndpointOverrides(m_settings->sessionServerBaseOverride(),
                                          m_settings->textureBaseOverride());
    SkinApi::setBaseUrlOverride(m_settings->minecraftServicesBaseOverride());
    // 灌值时把信号全挡上：下面这些控件现在反映的是**盘上的值**，不是用户改的。
    // 不挡的话每条 currentIndexChanged/valueChanged 都会触发一遍"保存"，白写几次盘，
    // 更要紧的是那几条槽里还夹着真动作（改「版本名格式」会重刷实例列表）。
    const QSignalBlocker blockBehaviour(m_ui->getLauncherBehaviourCombo());
    const QSignalBlocker blockSource(m_ui->getDownloadSourceCombo());
    const QSignalBlocker blockConcurrency(m_ui->getDownloadConcurrencyCombo());
    const QSignalBlocker blockSpeedLimit(m_ui->getDownloadSpeedLimitCombo());
    const QSignalBlocker blockProxyMode(m_ui->getDownloadProxyCombo());
    const QSignalBlocker blockProxyManual(m_ui->getDownloadProxyManualEdit());
    const QSignalBlocker blockShowLog(m_ui->getShowGameLogCheck());
    const QSignalBlocker blockWarnDuplicate(m_ui->getWarnDuplicateLaunchCheck());
    const QSignalBlocker blockWarnWorldLoss(m_ui->getWarnWorldLossCheck());
    const QSignalBlocker blockDownloadInto(
        m_ui->getDownloadAutoIntoInstanceCheck());
    const QSignalBlocker blockWarnDeps(m_ui->getWarnMissingDependenciesCheck());
    const QSignalBlocker blockPriority(m_ui->getProcessPriorityCombo());
    const QSignalBlocker blockWindowWidth(m_ui->getGameWindowWidthSpin());
    const QSignalBlocker blockWindowHeight(m_ui->getGameWindowHeightSpin());
    const QSignalBlocker blockTitleText(m_ui->getTitleTextEdit());
    const QSignalBlocker blockCustom(m_ui->getVersionNameCustomCheck());
    const QSignalBlocker blockReal(m_ui->getVersionNameRealCheck());
    const QSignalBlocker blockLoader(m_ui->getVersionNameLoaderCheck());
    const QSignalBlocker blockLoaderVersion(m_ui->getVersionNameLoaderVersionCheck());

    updateJavaPathLabel();
    // 提醒总闸。挡信号：这是"把值画出来"，不挡的话 setChecked 会触发
    // onJavaWarnMismatchChanged() 又回头落一次盘。
    {
        const QSignalBlocker blockWarn(m_ui->getJavaWarnMismatchCheck());
        m_ui->getJavaWarnMismatchCheck()->setChecked(m_javaSettings->warnVersionMismatch());
    }

    refreshMemoryUi();

    // ---------- 启动后 ----------
    // 枚举值存在 itemData 里，文案是这里的 tr()。两边的对应关系只在这四行里。
    QComboBox *behaviourCombo = m_ui->getLauncherBehaviourCombo();
    behaviourCombo->addItem(tr("不变"), int(LauncherBehaviour::Unchanged));
    behaviourCombo->addItem(tr("最小化启动器"), int(LauncherBehaviour::Minimize));
    behaviourCombo->addItem(tr("收起启动器"), int(LauncherBehaviour::Close));
    behaviourCombo->addItem(tr("收起启动器，游戏退出后自动回来"),
                            int(LauncherBehaviour::CloseAndRestore));
    selectByEnumData(behaviourCombo, m_settings->launcherBehaviour());

    // ---------- JVM 参数 ----------
    m_ui->getExtraJvmArgsEdit()->setPlainText(m_javaSettings->extraJvmArgs());

    // ---------- 外观 ----------
    // 条上那行字有**两个**去处：设置页那一格、还有条自己。同一次调用里一起灌 ——
    // 分成两处写的话，以后改一处就会出现"框里是用户改过的名字、条上还是默认那五个字母"。
    // 两边喂的都是盘上那份（空串 = 没改过，条自己会退回 defaultBrandText()）。
    m_ui->getTitleTextEdit()->setText(m_settings->titleText());
    m_ui->titleBar()->setBrandText(m_settings->titleText());

    // ---------- 背景图 ----------
    // 那两行控件画的是 `Theme::current()` 里那两块图的样子（**不是** `m_settings` 里的
    // 东西：主题不归 LauncherSettings 管，它在同一个 QSettings 里另有一组键，见 Theme.hpp）。
    // 信号在 refreshThemeImageRows() 里自己挡（它和上面这一串 blocker 是两回事：
    // 那边连滑块带下拉框一起挡，理由写在那儿）。
    refreshThemeImageRows();

    // 「主题」那一节（配色 / 主题色 / 文字不透明度 / 语义色 / 动效开关）。和上面那一句一样，
    // 画的是 `Theme::current()` 和 `Anim::enabled()` 里的东西 —— 主题和动效都不归
    // LauncherSettings 管，各自另有一组键（见 Theme.hpp / Animations.hpp）。
    // NOTE: 这一句**必须**排在 `Theme::setSpec(Theme::load())` 之后（那一步在 src/main.cpp
    // 里、比 MainWindow 构造还早），不然画出来的是出厂那一份。
    refreshThemeControls();

    // ---------- 启动选项 ----------
    m_ui->getShowGameLogCheck()->setChecked(m_settings->showGameLog());
    // 两条启动提醒（上面那两个 blocker 已经挡上了）。这里**不能**顺手把值抄成
    // "界面 = 盘上"以外的任何默认：默认开这件事只有 LauncherSettings 一处出处。
    m_ui->getWarnDuplicateLaunchCheck()->setChecked(m_settings->warnDuplicateLaunch());
    m_ui->getWarnWorldLossCheck()->setChecked(m_settings->warnWorldLoss());
    // 优先级：枚举值存在 itemData 里，文案是这里的 tr()。五档一一对应 ProcessPriority，
    // 顺序也按枚举来（Low → High）—— 界面从上往下就是"越来越抢 CPU"。
    QComboBox *priorityCombo = m_ui->getProcessPriorityCombo();
    priorityCombo->addItem(tr("低（空闲时才算）"), int(ProcessPriority::Low));
    priorityCombo->addItem(tr("低于正常"), int(ProcessPriority::BelowNormal));
    priorityCombo->addItem(tr("正常（默认）"), int(ProcessPriority::Normal));
    priorityCombo->addItem(tr("高于正常"), int(ProcessPriority::AboveNormal));
    priorityCombo->addItem(tr("高"), int(ProcessPriority::High));
    selectByEnumData(priorityCombo, m_settings->processPriority());
    // 窗口模式：同样把枚举值存在 itemData 里。三档的顺序就是"窗口从花到满" ——
    // 和 ProcessPriority 那条一样，界面从上往下读就是越来越满。
    // NOTE: 文案里点明了各档"下面那个窗口大小还管不管用"，因为「最大化」那一档不吃它
    // （原版没有最大化参数，是照屏幕工作区算的，见 GameWindowSettings.hpp）。
    QComboBox *windowModeCombo = m_ui->getGameWindowModeCombo();
    windowModeCombo->addItem(tr("窗口"),
                             int(GameWindowMode::Windowed));
    windowModeCombo->addItem(tr("最大化"),
                             int(GameWindowMode::Maximized));
    windowModeCombo->addItem(tr("全屏"), int(GameWindowMode::Fullscreen));
    selectByEnumData(windowModeCombo, m_settings->gameWindowMode());
    // 窗口大小：两个框各自 setValue（信号已经被上面那两个 blocker 挡上了）。
    m_ui->getGameWindowWidthSpin()->setValue(m_settings->gameWindowWidth());
    m_ui->getGameWindowHeightSpin()->setValue(m_settings->gameWindowHeight());
    // 灌完模式那一格要把"窗口大小"那一行按档位灰/亮一次 —— 上面那次 selectByEnumData()
    // 是有 blocker 的（走不到槽里），所以这一下不能省，否则盘上存着「最大化」时，
    // 下面两个框会亮着一个"其实不生效"的值。
    updateGameWindowSizeEnabled();

    // ---------- 下载源 ----------
    QComboBox *sourceCombo = m_ui->getDownloadSourceCombo();
    sourceCombo->addItem(tr("优先官方源，速度缓慢时使用国内源"), int(DownloadSource::Official));
    sourceCombo->addItem(tr("优先国内源，速度缓慢时使用官方源"), int(DownloadSource::Mirror));
    selectByEnumData(sourceCombo, m_settings->downloadSource());

    // ---------- 「同时最多下几个」 ----------
    // 四档那个表在 DownloadConcurrency.hpp（**唯一出处**）；这里的文字只能在这一层写
    // （ui/ 不许 include src/，所以档位值由这里灌成 itemData，和下载源、窗口模式同一形状）。
    QComboBox *concurrencyCombo = m_ui->getDownloadConcurrencyCombo();
    const QList<int> presets = DownloadConcurrency::presets();
    for (int value : presets) {
        concurrencyCombo->addItem(downloadConcurrencyChoiceText(value), value);
    }
    {
        const int concurrency = m_settings->downloadConcurrency();
        // 盘上是个**表外的、但落在合法区间里的**数（手改过配置）时补一格 —— 不补的话下拉框
        // 会指着一个"不是盘上那个值"的档，界面就在撒谎（而这一格正好是"用户以为自己调过"）。
        if (!presets.contains(concurrency)) {
            concurrencyCombo->addItem(tr("%1 个（自定义）").arg(concurrency), concurrency);
        }
        selectByEnumData(concurrencyCombo, concurrency);
    }

    // ---------- 「下载速度上限」 ----------
    // 和上面那格同一个形状。★ itemData 存的是 **int**（最大 1 GiB = 1073741824，装得下），
    // 读的时候再转回 qint64 —— 别在这里用 toLongLong 之后又 findData(int)，两边对不上
    // 就会"盘上有值、框里却停在第一项"。
    QComboBox *speedLimitCombo = m_ui->getDownloadSpeedLimitCombo();
    const QList<qint64> speedPresets = DownloadSpeedLimit::presets();
    for (qint64 value : speedPresets) {
        speedLimitCombo->addItem(downloadSpeedLimitChoiceText(value), int(value));
    }
    {
        const qint64 limit = m_settings->downloadSpeedLimit();
        // 手改过配置、落在合法区间里的表外值：补一格（理由和上面并发数那条一样）。
        if (!speedPresets.contains(limit)) {
            speedLimitCombo->addItem(
                tr("%1（自定义）").arg(downloadSpeedLimitChoiceText(limit)), int(limit));
        }
        selectByEnumData(speedLimitCombo, int(limit));
    }

    // ---------- 「代理」 ----------
    // 三档那个表在 DownloadProxy.hpp（**唯一出处**）；itemData 存 int(枚举)。
    // 手动地址那一格只在「手动」档露出来（藏起来时连位置都不占 —— 它是这一行里的第二块，
    // 露着的话另外两档会白白多出一格空白输入框）。
    QComboBox *proxyCombo = m_ui->getDownloadProxyCombo();
    for (DownloadProxy::Mode mode : DownloadProxy::modes()) {
        proxyCombo->addItem(downloadProxyChoiceText(mode), int(mode));
    }
    selectByEnumData(proxyCombo, int(m_settings->proxyMode()));
    {
        QLineEdit *manual = m_ui->getDownloadProxyManualEdit();
        manual->setText(m_settings->proxyManualText());
        manual->setVisible(m_settings->proxyMode() == DownloadProxy::Mode::Manual);
    }

    // CurseForge 的 API key（同一个「下载源」组里）。信号已经被上面那几个 blocker 挡上了。
    m_ui->getCurseForgeApiKeyEdit()->setText(m_settings->curseForgeApiKey());

    // 「下载新模组时直接放进当前实例」。默认值同样只有 LauncherSettings
    // 那一处出处（默认勾上）—— 这里不写任何默认，灌的就是盘上那份。
    m_ui->getDownloadAutoIntoInstanceCheck()->setChecked(m_settings->downloadAutoIntoInstance());
    // 「下载缺前置时提醒」。同一个做法：不在这儿写默认。
    m_ui->getWarnMissingDependenciesCheck()->setChecked(m_settings->warnMissingDependencies());

    // ---------- 版本名格式 ----------
    pushVersionNameOptionsToUi(m_settings->versionNameOptions());

    // 下载源得立刻交给发请求的那一层：构造函数里紧接着的
    // fetchAvailableVersions() 就会走它。
    syncDownloadSource();
    // 「同时最多下几个」同理：不等用户去设置页改，开机那一刻就按盘上那一档发请求。
    syncDownloadConcurrency();
    // 「下载速度上限」同理：开机那一刻就按盘上那一档压着（0 = 不限）。
    syncDownloadSpeedLimit();
    // 「代理」同理：**这是要在任何请求发出去之前装的** —— 别的几项只影响下载器，
    // 这一项是进程级的（版本清单、皮肤、资源搜索全走它）。
    syncDownloadProxy();
    // CurseForge 那两项同理（虽然要等到导入整合包才用得上）。
    syncCurseForgeSettings();
}

void MainWindow::pushVersionNameOptionsToUi(const VersionNameOptions &options) {
    m_launcherPreferences->pushVersionNameOptions(options);
}

void MainWindow::updateJavaPathLabel() {
    m_javaSettingsController->updatePathLabel();
}

void MainWindow::scanLocalJava() {
    m_javaSettingsController->refreshList();
}

void MainWindow::applyDetectedJavaVersions(const QMap<int, QString> &versions) {
    m_javaSettingsController->applyDetected(versions);
}

void MainWindow::refreshJavaList() {
    m_javaSettingsController->refreshList();
}

void MainWindow::onMainTabChanged() {
    updateBottomBar();
    if (m_ui->getTabWidget()->currentIndex() == MainWindowUI::settingsTabIndex()) {
        refreshJavaList();
        refreshMemoryUi();
    }
}

void MainWindow::openJavaManage() {
    runWhenOverlayFree([this] { openJavaManageNow(); });
}

void MainWindow::openJavaManageNow() {
    m_javaSettingsController->openManagePage();
}

void MainWindow::pushJavaRowsToPage() {
    // JavaSettingsController owns and refreshes the page now.
}

void MainWindow::onJavaUseAuto() {
    m_javaSettingsController->useAuto();
}

void MainWindow::onJavaUseRuntime(int major) {
    m_javaSettingsController->useRuntime(major);
}

void MainWindow::onJavaDetectRequested() {
    m_javaSettingsController->detect();
}

void MainWindow::onJavaDownloadRequested(int major) {
    m_javaSettingsController->download(major);
}

void MainWindow::onJavaDownloadAllMissing() {
    m_javaSettingsController->downloadAllMissing();
}

void MainWindow::refreshMemoryUi() {
    m_javaSettingsController->refreshMemory();
}

void MainWindow::applyMemoryLimits(qint64 totalMb) {
    m_javaSettingsController->applyMemoryLimits(totalMb);
}

bool MainWindow::clampXmsToXmx(int xmxMb) {
    return m_javaSettingsController->clampXmsToXmx(xmxMb);
}

void MainWindow::onJavaWarnMismatchChanged() {
    m_javaSettingsController->setWarnMismatchFromUi();
}

void MainWindow::onMemoryChanged() {
    m_javaSettingsController->commitMemoryFromSpin();
}

void MainWindow::onMemorySliderMoved() {
    m_javaSettingsController->syncMemorySlider();
}

void MainWindow::onMemoryModeChanged() {
    m_javaSettingsController->setMemoryModeFromUi();
}

void MainWindow::onLauncherBehaviourChanged() {
    m_launcherPreferences->launcherBehaviourChanged();
}

void MainWindow::onDownloadSourceChanged() {
    m_launcherPreferences->downloadSourceChanged();
}

void MainWindow::onDownloadConcurrencyChanged() {
    m_launcherPreferences->downloadConcurrencyChanged();
}

void MainWindow::onDownloadSpeedLimitChanged() {
    m_launcherPreferences->downloadSpeedLimitChanged();
}

void MainWindow::onDownloadProxyModeChanged() {
    m_launcherPreferences->downloadProxyModeChanged();
}

void MainWindow::onDownloadProxyManualChanged() {
    m_launcherPreferences->downloadProxyManualChanged();
}

void MainWindow::onDownloadAutoIntoInstanceChanged() {
    m_launcherPreferences->downloadAutoIntoInstanceChanged();
}

void MainWindow::onWarnMissingDependenciesChanged() {
    m_launcherPreferences->warnMissingDependenciesChanged();
}

void MainWindow::onShowGameLogChanged() {
    m_launcherPreferences->showGameLogChanged();
}

void MainWindow::onWarnDuplicateLaunchChanged() {
    m_launcherPreferences->warnDuplicateLaunchChanged();
}

void MainWindow::onWarnWorldLossChanged() {
    m_launcherPreferences->warnWorldLossChanged();
}

void MainWindow::onProcessPriorityChanged() {
    m_launcherPreferences->processPriorityChanged();
}

void MainWindow::onGameWindowSizeChanged() {
    m_launcherPreferences->gameWindowSizeChanged();
}

void MainWindow::onGameWindowModeChanged() {
    m_launcherPreferences->gameWindowModeChanged();
}

void MainWindow::updateGameWindowSizeEnabled() {
    m_launcherPreferences->updateGameWindowSizeEnabled();
}
// ---------------------------------------------------------------------------
// 「背景图」那一节（设置页「个性化」）
// ---------------------------------------------------------------------------
// 主页左栏可以自定义图像，其他页面的背景也能换图。
//
// 这一节动的是**主题规格**（`ui/Theme.hpp`），不是 LauncherSettings —— 主题另有一组
// QSettings 键（`Theme/…`），落盘/读盘都走 `Theme::save()` / `Theme::load()`，而启动时
// 铺开那一次在 `src/main.cpp` 里。所以这边改完的顺序是固定的三步：
// **改进内存里那份规格 → 落盘 → `Theme::apply()` 立刻铺开**（少最后一步就是"改了没生效，
// 下次启动才出现"，那是这一节最要命的失败模式）。

void MainWindow::onThemeImageChooseClicked(int index) {
    m_themeSettingsController->chooseImage(index);
}

void MainWindow::onThemeImageClearClicked(int index) {
    m_themeSettingsController->clearImage(index);
}

void MainWindow::onThemeImageFitChanged(int index) {
    m_themeSettingsController->changeImageFit(index);
}

void MainWindow::onThemeImageOpacityChanged(int index) {
    m_themeSettingsController->changeImageOpacity(index);
}

void MainWindow::setThemePartImage(const QString &partKey, const QString &imagePath,
                                   const QString &imageName, int fit, int opacity) {
    m_themeSettingsController->setPartImage(partKey, imagePath, imageName, fit, opacity);
}

void MainWindow::refreshThemeImageRows() {
    m_themeSettingsController->refresh();
}

void MainWindow::applyThemeSpec(const Theme::Spec &spec) {
    m_themeSettingsController->applySpec(spec);
}

void MainWindow::refreshThemeControls() {
    m_themeSettingsController->refresh();
}

void MainWindow::onThemePresetChanged() {
    m_themeSettingsController->changePreset();
}

void MainWindow::onThemeAccentClicked() {
    m_themeSettingsController->chooseAccent();
}

void MainWindow::onThemeAccentResetClicked() {
    m_themeSettingsController->resetAccent();
}

void MainWindow::onThemeSemanticClicked(int index) {
    m_themeSettingsController->chooseSemantic(index);
}

void MainWindow::onThemeSemanticResetClicked() {
    m_themeSettingsController->resetSemantic();
}

void MainWindow::onThemeResetAllClicked() {
    m_themeSettingsController->resetAll();
}

void MainWindow::onThemeExportClicked() {
    m_themeSettingsController->exportTheme();
}

void MainWindow::onThemeImportClicked() {
    m_themeSettingsController->importTheme();
}

void MainWindow::onThemeTextOpacityChanged(int value) {
    m_themeSettingsController->changeTextOpacity(value);
}

void MainWindow::onAnimationsToggled(bool enabled) {
    m_themeSettingsController->setAnimationsEnabled(enabled);
}

void MainWindow::onTitleTextChanged() {
    m_themeSettingsController->updateTitleText();
}
void MainWindow::syncDownloadSource() {
    m_launcherPreferences->syncDownloadSource();
}

void MainWindow::syncDownloadConcurrency() {
    m_launcherPreferences->syncDownloadConcurrency();
}

void MainWindow::syncDownloadSpeedLimit() {
    m_launcherPreferences->syncDownloadSpeedLimit();
}

void MainWindow::syncDownloadProxy() {
    m_launcherPreferences->syncDownloadProxy();
}

void MainWindow::syncCurseForgeSettings() {
    m_launcherPreferences->syncCatalogCredentials();
}

void MainWindow::onCurseForgeApiKeyEdited() {
    m_launcherPreferences->curseForgeApiKeyEdited();
    // 兼容检查那条路读的是同一份设置（用户改完立刻生效，不用重启）。
    if (m_presetController != nullptr) {
        m_presetController->setCurseForgeApiKey(m_settings->curseForgeApiKey());
    }
}

void MainWindow::onVersionNameOptionChanged() {
    m_launcherPreferences->versionNameOptionChanged();
}

void MainWindow::onApplyExtraJvmArgsClicked() {
    m_launcherPreferences->applyExtraJvmArgs();
}
