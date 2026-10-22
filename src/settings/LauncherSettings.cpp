/**
 * @file LauncherSettings.cpp
 * @brief 实现启动器设置的读写。
 * @note 见 LauncherSettings.hpp 的说明。
 */

#include "LauncherSettings.hpp"

#include <QDebug>
#include <QSettings>

namespace {

// 键名带 Version/ 前缀，和 GlobalJavaSettings 的 Java/ 前缀一个路子：
// 不用 beginGroup，键名本身就是分组，在注册表里直接看得出归属。
static constexpr const char kLastLaunched[] = "Version/LastLaunched";
static constexpr const char kHomeFavoriteCount[] = "Home/FavoriteCount";
static constexpr const char kHomeRecentCount[] = "Home/RecentCount";
static constexpr const char kNameShowCustom[] = "Version/NameShowCustom";
static constexpr const char kNameShowReal[] = "Version/NameShowReal";
static constexpr const char kNameShowLoaderName[] = "Version/NameShowLoaderName";
static constexpr const char kNameShowLoaderVersion[] = "Version/NameShowLoaderVersion";
static constexpr const char kAfterLaunch[] = "Launcher/AfterLaunch";
static constexpr const char kFolders[] = "Launcher/Folders";
static constexpr const char kCurrentFolder[] = "Launcher/CurrentFolder";
static constexpr const char kFolderNames[] = "Launcher/FolderNames";
static constexpr const char kDownloadSource[] = "Download/Source";
// 「同时最多下几个」（并发数预设）。同一个前缀：它也是"下载怎么走"。
// 存的是**整数**，不是 `Download/Source` 那种记号 —— 那四档要有第五档"自定义"的位置
// （手改过配置的人写个 [1,64] 里的别的数，界面上就该照他写的显示），记号表反而挡路。
static constexpr const char kDownloadConcurrency[] = "Download/Concurrency";
// 「下载速度上限」（限速）。单位**字节/秒**，0 = 不限；和其他几项同一个
// 前缀 —— 它也是"下载怎么走"。
static constexpr const char kDownloadSpeedLimit[] = "Download/SpeedLimitBytes";
// 「代理」。档位存记号（system/direct/manual），手动那一档的
// `host:port` 另存一格原串。同一个前缀：它也是"下载怎么走"。
static constexpr const char kProxyMode[] = "Download/ProxyMode";
static constexpr const char kProxyManual[] = "Download/ProxyManual";
// 「下载新模组时直接放进当前实例」（设置页「下载」栏那一项）。
// 和上面那条同一个前缀：它也是"下下来的东西往哪儿去"这一件事。
static constexpr const char kDownloadAutoIntoInstance[] = "Download/AutoIntoInstance";
// 「下载缺前置时提醒」（设置页「下载」栏那一项）。同一个前缀：它管的也是
// "点下载那一刻多问一句"这一类的事。
static constexpr const char kWarnMissingDependencies[] = "Download/WarnMissingDependencies";
static constexpr const char kShowGameLog[] = "Launcher/ShowGameLog";
static constexpr const char kGameWindowWidth[] = "Launcher/WindowWidth";
static constexpr const char kGameWindowHeight[] = "Launcher/WindowHeight";
// 窗口模式（小窗 / 最大化 / 全屏）。**值就是 GameWindowSettings 里那一份 token**
// （gameWindowModeToken / gameWindowModeFromToken）—— 别在这儿另写一遍串，那是"改了一处
// 另一处静默退回默认档"的典型写法。
static constexpr const char kGameWindowMode[] = "Launcher/WindowMode";
static constexpr const char kProcessPriority[] = "Launcher/ProcessPriority";
static constexpr const char kTitleText[] = "Launcher/TitleText";
static constexpr const char kWarnDuplicateLaunch[] = "Launcher/WarnDuplicateLaunch";
static constexpr const char kWarnWorldLoss[] = "Launcher/WarnWorldLoss";
/// 用户自己填的 CurseForge API key（空 = 走那台不要 key 的社区镜像）。
static constexpr const char kCurseForgeApiKey[] = "Launcher/CurseForgeApiKey";
/// CurseForge 接口的地址覆盖。**只有验证工程会写它**（见 .hpp 里那条 NOTE）：
/// 这里的 `load()` 读、`save()` **刻意不写**。
static constexpr const char kCurseForgeApiBase[] = "Launcher/CurseForgeApiBase";
/// Modrinth 接口（`api.modrinth.com/v2`）的地址覆盖，同上：只有验证工程会写它。
/// 导出整合包那条路要把它指到本机的假服务器上跑（导入那半走的是 CurseForge 那个口子）。
static constexpr const char kModrinthApiBase[] = "Launcher/ModrinthApiBase";
/// 版本清单、免鉴权皮肤档案、贴图和 Minecraft Services 的地址覆盖。和上面两条同一个
/// 来路：**只有验证工程会写**，`save()` 刻意不写 —— 产品永远走官方地址。
static constexpr const char kVersionManifestUrl[] = "Launcher/VersionManifestUrl";
static constexpr const char kSessionServerBase[] = "Launcher/SessionServerBase";
static constexpr const char kTextureBase[] = "Launcher/TextureBase";
static constexpr const char kMinecraftServicesBase[] = "Launcher/MinecraftServicesBase";

// 枚举存进 QSettings 的字符串。这几个串是**持久化格式的一部分**，不是内部标识：
// 改掉它们，用户盘上已经存好的值就会被当成"认不出来"、静默退回默认档。
static constexpr const char kBehaviourUnchanged[] = "unchanged";
static constexpr const char kBehaviourMinimize[] = "minimize";
static constexpr const char kBehaviourClose[] = "close";
static constexpr const char kBehaviourCloseAndRestore[] = "closeAndRestore";
static constexpr const char kSourceOfficial[] = "official";
static constexpr const char kSourceMirror[] = "mirror";
static constexpr const char kPriorityLow[] = "low";
static constexpr const char kPriorityBelowNormal[] = "belowNormal";
static constexpr const char kPriorityNormal[] = "normal";
static constexpr const char kPriorityAboveNormal[] = "aboveNormal";
static constexpr const char kPriorityHigh[] = "high";

/// 把窗口大小钳进合法区间。盘上的值是**不可信数据**（用户手改注册表、或者以后
/// 从别的地方同步过来），而这里放行的值会原样变成游戏命令行上的两个数字。
int clampWindowWidth(int width) {
    return qBound(LauncherSettings::gameWindowMinWidth(), width,
                  LauncherSettings::gameWindowMaxWidth());
}

int clampWindowHeight(int height) {
    return qBound(LauncherSettings::gameWindowMinHeight(), height,
                  LauncherSettings::gameWindowMaxHeight());
}

/// 把顶上那行字收拾干净：去掉两头的空白，再截到长度上限。
///
/// 两头空白要去掉是因为**它会被画在条上**（名字是从条的最左边开始画的，" WTFML " 看着
/// 就是没对齐）；截断是因为那一格地方是量的（见 MainWindowUI::titleTextMaxLength()），
/// 而盘上那份是**不可信数据**（用户手改过注册表、或者以后从别处同步过来）。
QString clampTitleText(const QString &text) {
    return text.trimmed().left(LauncherSettings::titleTextMaxLength());
}

}  // namespace

LauncherSettings::LauncherSettings(QObject *parent) : QObject(parent) { load(); }

QString LauncherSettings::lastLaunchedVersion() const { return m_lastLaunchedVersion; }

void LauncherSettings::setLastLaunchedVersion(const QString &versionId) {
    if (m_lastLaunchedVersion == versionId) {
        return;
    }
    m_lastLaunchedVersion = versionId;
    save();
}

void LauncherSettings::setHomeFavoriteCount(int count) {
    count = qBound(0, count, 5);
    if (m_homeFavoriteCount != count) {
        m_homeFavoriteCount = count;
        save();
    }
}

void LauncherSettings::setHomeRecentCount(int count) {
    count = qBound(0, count, 5);
    if (m_homeRecentCount != count) {
        m_homeRecentCount = count;
        save();
    }
}

QStringList LauncherSettings::folders() const { return m_folders; }

void LauncherSettings::setFolders(const QStringList &folders) {
    if (m_folders == folders) {
        return;
    }
    m_folders = folders;
    save();
}

QString LauncherSettings::currentFolder() const { return m_currentFolder; }

void LauncherSettings::setCurrentFolder(const QString &dir) {
    if (m_currentFolder == dir) {
        return;
    }
    m_currentFolder = dir;
    save();
}

QStringList LauncherSettings::folderNames() const { return m_folderNames; }

void LauncherSettings::setFolderNames(const QStringList &names) {
    if (m_folderNames == names) {
        return;
    }
    m_folderNames = names;
    save();
}

VersionNameOptions LauncherSettings::versionNameOptions() const { return m_versionNameOptions; }

void LauncherSettings::setVersionNameOptions(const VersionNameOptions &options) {
    // 先归一化再比较：传进来一个非法组合、但归一化之后和现值相同时，不该白写一次盘。
    const VersionNameOptions normalized = normalizeVersionNameOptions(options);
    if (m_versionNameOptions == normalized) {
        return;
    }
    m_versionNameOptions = normalized;
    save();
}

void LauncherSettings::setLauncherBehaviour(LauncherBehaviour behaviour) {
    if (m_launcherBehaviour == behaviour) {
        return;
    }
    m_launcherBehaviour = behaviour;
    save();
}

void LauncherSettings::setDownloadSource(DownloadSource source) {
    if (m_downloadSource == source) {
        return;
    }
    m_downloadSource = source;
    save();
}

void LauncherSettings::setDownloadConcurrency(int count) {
    // 存的是**归一化之后**的那个数：这样"盘上写的"和"真在用的"永远是同一个值
    // （越界的值在写的那一刻就归到默认，不留一个盘上写着 999、实际按 24 跑的形状）。
    const int normalized = DownloadConcurrency::normalize(count);
    if (m_downloadConcurrency == normalized) {
        return;
    }
    m_downloadConcurrency = normalized;
    save();
}

void LauncherSettings::setDownloadSpeedLimit(qint64 bytesPerSecond) {
    // 和上面并发数同一条规矩：存的是**归一化之后**的那个数 —— 盘上写的和真在用的
    // 永远是同一个值。0 是合法值（= 不限），越界回 0（理由见 DownloadSpeedLimit.hpp）。
    const qint64 normalized = DownloadSpeedLimit::normalize(bytesPerSecond);
    if (m_downloadSpeedLimit == normalized) {
        return;
    }
    m_downloadSpeedLimit = normalized;
    save();
}

void LauncherSettings::setProxyMode(DownloadProxy::Mode mode) {
    // 认不出来的档位（构造出别的 enum 值时）一律回出厂那一档 —— 和读盘那条路同一个判据。
    if (!DownloadProxy::modes().contains(mode)) {
        mode = DownloadProxy::kDefaultMode;
    }
    if (m_proxyMode == mode) {
        return;
    }
    m_proxyMode = mode;
    save();
}

void LauncherSettings::setProxyManualText(const QString &text) {
    const QString trimmed = text.trimmed();
    if (m_proxyManualText == trimmed) {
        return;
    }
    m_proxyManualText = trimmed;
    save();
}

void LauncherSettings::setCurseForgeApiKey(const QString &apiKey) {
    // 存下来的就是用户打进去的那串，但两头的空白修掉（从网页上复制 key 时很容易带一个
    // 尾随空格，而带空格的 key 会一路变成一个 403，看着像"key 不对"）。
    const QString trimmed = apiKey.trimmed();
    if (m_curseForgeApiKey == trimmed) {
        return;
    }
    m_curseForgeApiKey = trimmed;
    save();
}

void LauncherSettings::setDownloadAutoIntoInstance(bool autoIntoInstance) {
    if (m_downloadAutoIntoInstance == autoIntoInstance) {
        return;
    }
    m_downloadAutoIntoInstance = autoIntoInstance;
    save();
}

void LauncherSettings::setWarnMissingDependencies(bool warn) {
    if (m_warnMissingDependencies == warn) {
        return;
    }
    m_warnMissingDependencies = warn;
    save();
}

void LauncherSettings::setShowGameLog(bool show) {
    if (m_showGameLog == show) {
        return;
    }
    m_showGameLog = show;
    save();
}

void LauncherSettings::setGameWindowSize(int width, int height) {
    const int w = clampWindowWidth(width);
    const int h = clampWindowHeight(height);
    if (m_gameWindowWidth == w && m_gameWindowHeight == h) {
        return;
    }
    m_gameWindowWidth = w;
    m_gameWindowHeight = h;
    save();
}

void LauncherSettings::setGameWindowMode(GameWindowMode mode) {
    if (m_gameWindowMode == mode) {
        return;
    }
    m_gameWindowMode = mode;
    save();
}

GameWindowSettings LauncherSettings::gameWindowSettings() const {
    GameWindowSettings out;
    out.mode = m_gameWindowMode;
    out.width = m_gameWindowWidth;
    out.height = m_gameWindowHeight;
    return out;
}

void LauncherSettings::setProcessPriority(ProcessPriority priority) {
    if (m_processPriority == priority) {
        return;
    }
    m_processPriority = priority;
    save();
}

void LauncherSettings::setTitleText(const QString &text) {
    const QString cleaned = clampTitleText(text);
    // 比的是**收拾过的**那一份：用户接着打一个空格（"名字 " → "名字"）不该白写一次盘。
    if (m_titleText == cleaned) {
        return;
    }
    m_titleText = cleaned;
    save();
}

void LauncherSettings::setWarnDuplicateLaunch(bool warn) {
    if (m_warnDuplicateLaunch == warn) {
        return;
    }
    m_warnDuplicateLaunch = warn;
    save();
}

void LauncherSettings::setWarnWorldLoss(bool warn) {
    if (m_warnWorldLoss == warn) {
        return;
    }
    m_warnWorldLoss = warn;
    save();
}

QString LauncherSettings::behaviourKey(LauncherBehaviour behaviour) {
    switch (behaviour) {
    case LauncherBehaviour::Minimize:
        return QString::fromLatin1(kBehaviourMinimize);
    case LauncherBehaviour::Close:
        return QString::fromLatin1(kBehaviourClose);
    case LauncherBehaviour::CloseAndRestore:
        return QString::fromLatin1(kBehaviourCloseAndRestore);
    case LauncherBehaviour::Unchanged:
        break;
    }
    return QString::fromLatin1(kBehaviourUnchanged);
}

LauncherBehaviour LauncherSettings::behaviourFromKey(const QString &key) {
    if (key == QLatin1String(kBehaviourMinimize)) {
        return LauncherBehaviour::Minimize;
    }
    if (key == QLatin1String(kBehaviourClose)) {
        return LauncherBehaviour::Close;
    }
    if (key == QLatin1String(kBehaviourCloseAndRestore)) {
        return LauncherBehaviour::CloseAndRestore;
    }
    // 认不出来（别的名字写下的值、用户手改的注册表）就当"不变"：这是唯一一个
    // 什么副作用都没有的档位，猜错的代价比"最小化/关闭"小得多。
    return LauncherBehaviour::Unchanged;
}

QString LauncherSettings::downloadSourceKey(DownloadSource source) {
    return QString::fromLatin1(source == DownloadSource::Mirror ? kSourceMirror : kSourceOfficial);
}

DownloadSource LauncherSettings::downloadSourceFromKey(const QString &key) {
    if (key == QLatin1String(kSourceMirror)) {
        return DownloadSource::Mirror;
    }
    // 认不出来就当官方源。镜像源是第三方服务，不能替用户默认选它 ——
    // 万一镜像挂了或者内容不对，用户会觉得"这启动器下的文件是坏的"。
    return DownloadSource::Official;
}

QString LauncherSettings::processPriorityKey(ProcessPriority priority) {
    switch (priority) {
    case ProcessPriority::Low:
        return QString::fromLatin1(kPriorityLow);
    case ProcessPriority::BelowNormal:
        return QString::fromLatin1(kPriorityBelowNormal);
    case ProcessPriority::AboveNormal:
        return QString::fromLatin1(kPriorityAboveNormal);
    case ProcessPriority::High:
        return QString::fromLatin1(kPriorityHigh);
    case ProcessPriority::Normal:
        break;
    }
    return QString::fromLatin1(kPriorityNormal);
}

ProcessPriority LauncherSettings::processPriorityFromKey(const QString &key) {
    if (key == QLatin1String(kPriorityLow)) {
        return ProcessPriority::Low;
    }
    if (key == QLatin1String(kPriorityBelowNormal)) {
        return ProcessPriority::BelowNormal;
    }
    if (key == QLatin1String(kPriorityAboveNormal)) {
        return ProcessPriority::AboveNormal;
    }
    if (key == QLatin1String(kPriorityHigh)) {
        return ProcessPriority::High;
    }
    // 认不出来就当 Normal：它和"不设置"的默认结果一样，是最没有副作用的一档。
    // 「高」那一档猜错的代价是把用户的机器拖慢，绝不能当兜底值。
    return ProcessPriority::Normal;
}

void LauncherSettings::load() {
    QSettings settings;
    m_lastLaunchedVersion = settings.value(kLastLaunched, m_lastLaunchedVersion).toString();
    m_homeFavoriteCount = qBound(0, settings.value(kHomeFavoriteCount, 3).toInt(), 5);
    m_homeRecentCount = qBound(0, settings.value(kHomeRecentCount, 3).toInt(), 5);
    // 默认值同样取当前成员：load() 被调第二次而盘上是空的时，值应该留着而不是被抹掉。
    m_folders = settings.value(kFolders, m_folders).toStringList();
    m_currentFolder = settings.value(kCurrentFolder, m_currentFolder).toString();
    m_folderNames = settings.value(kFolderNames, m_folderNames).toStringList();
    m_launcherBehaviour = behaviourFromKey(
        settings.value(kAfterLaunch, behaviourKey(m_launcherBehaviour)).toString());
    m_downloadSource = downloadSourceFromKey(
        settings.value(kDownloadSource, downloadSourceKey(m_downloadSource)).toString());
    // 默认值取当前成员（构造时 = 判据层那个出厂值）：load() 被调第二次而盘上是空的时值留着。
    // 归一化在这一句里 —— 盘上写着 999 或者一个负数时，读出来的是默认那一档。
    m_downloadConcurrency = DownloadConcurrency::normalize(
        settings.value(kDownloadConcurrency, m_downloadConcurrency).toInt());
    // 同上：盘上写着负数 / 一个荒唐的大数时，读出来的是"不限"。
    m_downloadSpeedLimit = DownloadSpeedLimit::normalize(
        settings.value(kDownloadSpeedLimit, m_downloadSpeedLimit).toLongLong());
    // 代理：档位认不出来回 System；手动那一格原样读，不在这里校验。
    m_proxyMode = DownloadProxy::modeFromKey(
        settings.value(kProxyMode, DownloadProxy::modeKey(m_proxyMode)).toString());
    m_proxyManualText = settings.value(kProxyManual, m_proxyManualText).toString().trimmed();
    // 默认值取当前成员（构造时是 true）：盘上那一项是个布尔，手改成一个不认识的值时
    // toBool() 给 false —— 那等于"用户关掉了"，也就是**每次都弹保存框**，这个方向是安全的
    // （多问一句 vs 悄悄把文件放进他不知道的地方）。
    m_downloadAutoIntoInstance =
        settings.value(kDownloadAutoIntoInstance, m_downloadAutoIntoInstance).toBool();
    // 同上：默认值取当前成员（构造时是 true），认不出来的值给 false = 关掉提醒 ——
    // 那个方向只是"少问一句"，不会替用户把东西装进他不知道的地方。
    m_warnMissingDependencies =
        settings.value(kWarnMissingDependencies, m_warnMissingDependencies).toBool();
    m_showGameLog = settings.value(kShowGameLog, m_showGameLog).toBool();
    // 宽高**读回来就钳位**：这两个数会原样进游戏命令行，盘上那份不能当成可信的。
    // 存进去的值也可能是手改的，所以两边都要兜。
    m_gameWindowWidth =
        clampWindowWidth(settings.value(kGameWindowWidth, m_gameWindowWidth).toInt());
    m_gameWindowHeight =
        clampWindowHeight(settings.value(kGameWindowHeight, m_gameWindowHeight).toInt());
    // 窗口模式：盘上那个串认不出来时**留在默认档（小窗）**，不猜也不报错 —— 认不出来的
    // 两种来历（手改过、别的名字写下的值）都该退回默认档。
    {
        GameWindowMode parsed = m_gameWindowMode;
        const QString token = settings.value(kGameWindowMode, gameWindowModeToken(m_gameWindowMode))
                                  .toString();
        if (gameWindowModeFromToken(token, &parsed)) {
            m_gameWindowMode = parsed;
        }
    }
    m_processPriority = processPriorityFromKey(
        settings.value(kProcessPriority, processPriorityKey(m_processPriority)).toString());
    // 顶上那行字也**读回来就收拾**：盘上那一份会被原样画到条上，多长、带不带空格都不该
    // 信它（和宽高那两个数同一个理由）。
    m_titleText = clampTitleText(settings.value(kTitleText, m_titleText).toString());
    // 两个提醒开关：默认值同样取当前成员（构造时是 true）。盘上那一份是布尔，
    // 手改成一个不认识的值时 toBool() 给 false —— 那正好等于"用户关掉了"，
    // 不会把一个"看不懂的值"变成弹窗，这个方向是安全的。
    m_warnDuplicateLaunch = settings.value(kWarnDuplicateLaunch, m_warnDuplicateLaunch).toBool();
    m_warnWorldLoss = settings.value(kWarnWorldLoss, m_warnWorldLoss).toBool();
    // 两头的空白在**读回来的这一侧也修**：盘上那份可以是手改的（见 setCurseForgeApiKey()）。
    m_curseForgeApiKey = settings.value(kCurseForgeApiKey, m_curseForgeApiKey).toString().trimmed();
    // NOTE: 这一项**只有验证工程会写**（见头里那条），产品从不写它（save() 里没有它）。
    m_curseForgeApiBaseOverride =
        settings.value(kCurseForgeApiBase, m_curseForgeApiBaseOverride).toString().trimmed();
    m_modrinthApiBaseOverride =
        settings.value(kModrinthApiBase, m_modrinthApiBaseOverride).toString().trimmed();
    m_versionManifestUrlOverride =
        settings.value(kVersionManifestUrl, m_versionManifestUrlOverride).toString().trimmed();
    m_sessionServerBaseOverride =
        settings.value(kSessionServerBase, m_sessionServerBaseOverride).toString().trimmed();
    m_textureBaseOverride =
        settings.value(kTextureBase, m_textureBaseOverride).toString().trimmed();
    m_minecraftServicesBaseOverride =
        settings.value(kMinecraftServicesBase, m_minecraftServicesBaseOverride).toString().trimmed();

    // 和 GlobalJavaSettings::load() 用 m_xmx 当默认值是同一个写法。取成员而不是新造一个
    // struct：万一 load() 被调第二次而注册表是空的，值应该留着，而不是被重置回默认。
    VersionNameOptions options = m_versionNameOptions;
    options.showCustomName = settings.value(kNameShowCustom, options.showCustomName).toBool();
    options.showRealVersion = settings.value(kNameShowReal, options.showRealVersion).toBool();
    options.showLoaderName = settings.value(kNameShowLoaderName, options.showLoaderName).toBool();
    options.showLoaderVersion =
        settings.value(kNameShowLoaderVersion, options.showLoaderVersion).toBool();

    // 存进去的值可能是手改的，也可能是以后降级回退留下的。不变式在这里也要兜一遍，
    // 不能假设盘上的东西是合法的。
    m_versionNameOptions = normalizeVersionNameOptions(options);

    // NOTE: 状态要查。QSettings 在没有 organizationName 时，写入是**静默丢弃**的：
    // 注册表路径表整个裹在 `if (!organization.isEmpty())` 里（qsettings_win.cpp:382），
    // 空表直接 setStatus(AccessError)，writeHandle() 返回 0，set() 一声不吭地 return。
    // 不查的话，"记住上次启动的版本"会毫无征兆地失效，而且看不出任何迹象。
    // QCoreApplication 的 org/app 名在 src/main.cpp 里设，必须在构造 MainWindow 之前——
    // 那几行和这里的检查是一对的，别把 order 调换。
    if (settings.status() != QSettings::NoError) {
        qWarning() << "LauncherSettings: QSettings 状态异常，设置可能存不下来（组织名/应用名没设？），"
                      "status =" << settings.status();
    }
}

void LauncherSettings::save() const {
    QSettings settings;
    settings.setValue(kLastLaunched, m_lastLaunchedVersion);
    settings.setValue(kHomeFavoriteCount, m_homeFavoriteCount);
    settings.setValue(kHomeRecentCount, m_homeRecentCount);
    settings.setValue(kFolders, m_folders);
    settings.setValue(kCurrentFolder, m_currentFolder);
    settings.setValue(kFolderNames, m_folderNames);
    settings.setValue(kAfterLaunch, behaviourKey(m_launcherBehaviour));
    settings.setValue(kDownloadSource, downloadSourceKey(m_downloadSource));
    settings.setValue(kDownloadConcurrency, m_downloadConcurrency);
    settings.setValue(kDownloadSpeedLimit, m_downloadSpeedLimit);
    settings.setValue(kProxyMode, DownloadProxy::modeKey(m_proxyMode));
    settings.setValue(kProxyManual, m_proxyManualText);
    settings.setValue(kDownloadAutoIntoInstance, m_downloadAutoIntoInstance);
    settings.setValue(kWarnMissingDependencies, m_warnMissingDependencies);
    settings.setValue(kShowGameLog, m_showGameLog);
    settings.setValue(kGameWindowWidth, clampWindowWidth(m_gameWindowWidth));
    settings.setValue(kGameWindowHeight, clampWindowHeight(m_gameWindowHeight));
    settings.setValue(kGameWindowMode, gameWindowModeToken(m_gameWindowMode));
    settings.setValue(kProcessPriority, processPriorityKey(m_processPriority));
    settings.setValue(kTitleText, clampTitleText(m_titleText));
    settings.setValue(kWarnDuplicateLaunch, m_warnDuplicateLaunch);
    settings.setValue(kWarnWorldLoss, m_warnWorldLoss);
    settings.setValue(kCurseForgeApiKey, m_curseForgeApiKey);
    // NOTE: `kCurseForgeApiBase` / `kModrinthApiBase` **刻意不在这里** —— 那两个键是验证
    // 工程的口子，产品一写就等于把用户自己指的地址抹掉（见 .hpp 里那条 NOTE）。
    settings.setValue(kNameShowCustom, m_versionNameOptions.showCustomName);
    settings.setValue(kNameShowReal, m_versionNameOptions.showRealVersion);
    settings.setValue(kNameShowLoaderName, m_versionNameOptions.showLoaderName);
    settings.setValue(kNameShowLoaderVersion, m_versionNameOptions.showLoaderVersion);

    // 立刻落盘。这几项都是"关掉启动器之前必须记住"的状态，不像 GlobalJavaSettings
    // 那样攒到下载 Java 时才写一次；启动器被强杀也不该丢掉上次启动的版本。
    settings.sync();
}
