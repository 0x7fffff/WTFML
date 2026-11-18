/**
 * @file LauncherSettings.cpp
 * @brief 实现启动器设置的读写。
 * @note 见 LauncherSettings.hpp 的说明。
 */

#include "LauncherSettings.hpp"
#include "util/ServerPing.hpp"
#include "util/SettingsSyncRetry.hpp"
#include <QHostAddress>

#include <QDebug>
#include <QSettings>

namespace {

// 键名带 Version/ 前缀，和 GlobalJavaSettings 的 Java/ 前缀一个路子：
// 不用 beginGroup，键名本身就是分组，在注册表里直接看得出归属。
static constexpr const char kLastLaunched[] = "Version/LastLaunched";
static constexpr const char kLastSelected[] = "Version/LastSelected";
static constexpr const char kHomeFavoriteCount[] = "Home/FavoriteCount";
static constexpr const char kHomeRecentCount[] = "Home/RecentCount";
// NOTE: 原先还有 `Version/NameShowCustom`…`Version/NameShowLoaderVersion` 四个键
// （「版本名格式」那四个开关）。UI 问题 #14 删掉了那一组设置，这四个键**不再读也不再写**；
// 盘上留着的旧值没人看，显示名一律按出厂那一份拼。
static constexpr const char kAfterLaunch[] = "Launcher/AfterLaunch";
static constexpr const char kFolders[] = "Launcher/Folders";
static constexpr const char kCurrentFolder[] = "Launcher/CurrentFolder";
static constexpr const char kFolderNames[] = "Launcher/FolderNames";
static constexpr const char kServerFavorites[] = "Servers/Favorites";
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
// 保留原设置键，让已关闭内置落点的用户继续使用系统文件夹选择。
static constexpr const char kDownloadAutoIntoInstance[] = "Download/AutoIntoInstance";
// 「下载缺前置时提醒」（设置页「下载」栏那一项）。同一个前缀：它管的也是
// "点下载那一刻多问一句"这一类的事。
static constexpr const char kWarnMissingDependencies[] = "Download/WarnMissingDependencies";
static constexpr const char kShowGameLog[] = "Launcher/ShowGameLog";
// 主页「每日一言」的开关（默认开）。和 ShowGameLog 同一个前缀：都是「启动器自己的一次
// 动作要不要做」，不是某个游戏实例的属性。
static constexpr const char kDailyQuote[] = "Launcher/DailyQuote";
// 上次打开启动器的本地日期（ISO 串，主页问候判「首次 / 好久不见」用）。
static constexpr const char kLastOpenedDate[] = "Launcher/LastOpenedDate";
static constexpr const char kGameWindowWidth[] = "Launcher/WindowWidth";
static constexpr const char kGameWindowHeight[] = "Launcher/WindowHeight";
// 窗口模式（小窗 / 最大化 / 全屏）。**值就是 GameWindowSettings 里那一份 token**
// （gameWindowModeToken / gameWindowModeFromToken）—— 别在这儿另写一遍串，那是"改了一处
// 另一处静默退回默认档"的典型写法。
static constexpr const char kGameWindowMode[] = "Launcher/WindowMode";
static constexpr const char kProcessPriority[] = "Launcher/ProcessPriority";
// NOTE: 原先还有一条 `Launcher/TitleText`（条上那行字）。这一项在 UI 问题 #3 里撤掉了：
// 商标固定，**不再读也不写**。老用户盘上那份值就那么留着（没人读它，界面一律显示 WTFML），
// 刻意不在 load() 里抹掉它 —— 抹掉是"替用户改盘"，而没有读者时它和不存在没有区别。
// 启动器自己那扇窗的大小（上面 Launcher/WindowWidth 那两项是**游戏**窗口的，别混）。
static constexpr const char kMainWindowWidth[] = "Launcher/MainWindowWidth";
static constexpr const char kMainWindowHeight[] = "Launcher/MainWindowHeight";
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

/// 主窗口大小收拾干净：盘上那份不可信（手改过、别处同步来的）。宽或高不是正数 = 没记过（0×0），
/// 过大的截到上限；夹到最小尺寸和屏幕可用区在界面那一侧做。
QSize clampMainWindowSize(const QSize &size) {
    if (size.isEmpty()) return QSize(0, 0);
    return size.boundedTo(QSize(LauncherSettings::mainWindowMaxExtent(), LauncherSettings::mainWindowMaxExtent()));
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

void LauncherSettings::setLastSelectedVersion(const QString &versionId) {
    if (m_lastSelectedVersion == versionId) {
        return;
    }
    m_lastSelectedVersion = versionId;
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

QString LauncherSettings::serverFavoriteKey(const QString &address) {
    const auto parsed = ServerPing::splitAddress(address);
    if (!parsed.valid || address.trimmed().endsWith(QLatin1Char(':'))) return {};
    for (const auto ch : parsed.host)
        if (ch.isSpace() || QStringLiteral("/\\?#@").contains(ch)) return {};
    QHostAddress ip;
    QString host = ip.setAddress(parsed.host) ? ip.toString().toLower() : parsed.host.toLower();
    if (host.contains(QLatin1Char(':'))) host = QLatin1Char('[') + host + QLatin1Char(']');
    // An explicit :25565 suppresses SRV lookup, so it is a distinct endpoint.
    return parsed.portExplicit ? host + QLatin1Char(':') + QString::number(parsed.port) : host;
}

bool LauncherSettings::isServerFavorite(const QString &address) const {
    const auto key = serverFavoriteKey(address);
    return !key.isEmpty() && m_serverFavorites.contains(key);
}

bool LauncherSettings::setServerFavorite(const QString &address, bool favorite) {
    const auto key = serverFavoriteKey(address);
    if (key.isEmpty()) return false;
    const auto before = m_serverFavorites;
    if (favorite && !m_serverFavorites.contains(key)) m_serverFavorites.append(key);
    if (!favorite) m_serverFavorites.removeAll(key);
    m_serverFavorites.sort();
    save();
    const bool saved = m_persisted.value(QString::fromLatin1(kServerFavorites)).toStringList() == m_serverFavorites;
    if (!saved) m_serverFavorites = before;
    return saved;
}

void LauncherSettings::setFolderNames(const QStringList &names) {
    if (m_folderNames == names) {
        return;
    }
    m_folderNames = names;
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

void LauncherSettings::setDailyQuote(bool enabled) {
    if (m_dailyQuote == enabled) {
        return;
    }
    m_dailyQuote = enabled;
    save();
}

void LauncherSettings::setLastOpenedDate(const QDate &date) {
    if (m_lastOpenedDate == date) {
        return;
    }
    m_lastOpenedDate = date;
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

void LauncherSettings::setMainWindowSize(const QSize &size) {
    const QSize cleaned = clampMainWindowSize(size);
    if (cleaned.isEmpty() || m_mainWindowSize == cleaned) {
        return;
    }
    m_mainWindowSize = cleaned;
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
    m_lastSelectedVersion = settings.value(kLastSelected, m_lastSelectedVersion).toString();
    m_homeFavoriteCount = qBound(0, settings.value(kHomeFavoriteCount, 3).toInt(), 5);
    m_homeRecentCount = qBound(0, settings.value(kHomeRecentCount, 3).toInt(), 5);
    // 默认值同样取当前成员：load() 被调第二次而盘上是空的时，值应该留着而不是被抹掉。
    m_folders = settings.value(kFolders, m_folders).toStringList();
    m_serverFavorites.clear();
    for (const auto &address : settings.value(kServerFavorites).toStringList()) {
        const auto key = serverFavoriteKey(address);
        if (!key.isEmpty() && !m_serverFavorites.contains(key)) m_serverFavorites.append(key);
    }
    m_serverFavorites.sort();
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
    // 默认值取当前成员（构造时是 true）：盘上认不出来的值给 false = 关掉那一行，
    // 方向安全 —— 关着就没有任何外发请求。
    m_dailyQuote = settings.value(kDailyQuote, m_dailyQuote).toBool();
    // 认不出来的串读成无效日期 = 没记录，最坏是多说一次「欢迎」。
    m_lastOpenedDate = QDate::fromString(settings.value(kLastOpenedDate).toString(), Qt::ISODate);
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
    // 主窗口大小同样读回来就收拾（认不出来的值 toInt() 给 0 = 没记过）。
    m_mainWindowSize = clampMainWindowSize(QSize(settings.value(kMainWindowWidth, m_mainWindowSize.width()).toInt(),
                                                 settings.value(kMainWindowHeight, m_mainWindowSize.height()).toInt()));
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

    // NOTE: 原先这里有「版本名格式」那四个开关（`Version/NameShow*`）。UI 问题 #14 撤掉了
    // 这一组设置：**不再读**盘上那四个键（老用户存过的值一律按出厂默认算），也不再写它们。
    // 拼法固定成 `formatVersionName()` 那一份，和没改过设置的用户看到的一样。

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

    // 从这一刻起，"盘上该是什么"就是这一份。save() 只写与它不同的项（见 .hpp 里那条
    // NOTE：全局设置是几份启动器共用的，整份写回会把别人刚改的一项打回去）。
    // 放在 load() 的**最后**：前面所有归一化（钳位、认不出来的档位退回默认）都已经
    // 落进成员了，快照就该记"归一化之后的值"，不能记盘上那个原始串 —— 否则第一次
    // save() 会把一个本进程没动过的项当成"改过了"重写一遍。
    m_persisted = persistedValues();
}

QVariantMap LauncherSettings::persistedValues() const {
    // 键名 → 值，**和 save() 以前那张逐条 setValue 的表一一对应**（顺序不同不要紧：
    // QVariantMap 自带排序，而且写的是一次一项）。值都是写进 QSettings 的最终形态：
    // 枚举已经翻成记号串、宽高与标题已经钳过 —— 比较的是"盘上该有的那份"，
    // 不是成员里还没归一化的原始值。
    QVariantMap values;
    values.insert(QString::fromLatin1(kLastLaunched), m_lastLaunchedVersion);
    values.insert(QString::fromLatin1(kLastSelected), m_lastSelectedVersion);
    values.insert(QString::fromLatin1(kServerFavorites), m_serverFavorites);
    values.insert(QString::fromLatin1(kHomeFavoriteCount), QVariant(m_homeFavoriteCount));
    values.insert(QString::fromLatin1(kHomeRecentCount), QVariant(m_homeRecentCount));
    values.insert(QString::fromLatin1(kFolders), m_folders);
    values.insert(QString::fromLatin1(kCurrentFolder), m_currentFolder);
    values.insert(QString::fromLatin1(kFolderNames), m_folderNames);
    values.insert(QString::fromLatin1(kAfterLaunch), behaviourKey(m_launcherBehaviour));
    values.insert(QString::fromLatin1(kDownloadSource), downloadSourceKey(m_downloadSource));
    values.insert(QString::fromLatin1(kDownloadConcurrency), QVariant(m_downloadConcurrency));
    values.insert(QString::fromLatin1(kDownloadSpeedLimit), QVariant(m_downloadSpeedLimit));
    values.insert(QString::fromLatin1(kProxyMode), DownloadProxy::modeKey(m_proxyMode));
    values.insert(QString::fromLatin1(kProxyManual), m_proxyManualText);
    values.insert(QString::fromLatin1(kDownloadAutoIntoInstance),
                  QVariant(m_downloadAutoIntoInstance));
    values.insert(QString::fromLatin1(kWarnMissingDependencies),
                  QVariant(m_warnMissingDependencies));
    values.insert(QString::fromLatin1(kShowGameLog), QVariant(m_showGameLog));
    values.insert(QString::fromLatin1(kDailyQuote), QVariant(m_dailyQuote));
    values.insert(QString::fromLatin1(kLastOpenedDate),
                  m_lastOpenedDate.isValid() ? m_lastOpenedDate.toString(Qt::ISODate) : QString());
    values.insert(QString::fromLatin1(kGameWindowWidth), QVariant(clampWindowWidth(m_gameWindowWidth)));
    values.insert(QString::fromLatin1(kGameWindowHeight),
                  QVariant(clampWindowHeight(m_gameWindowHeight)));
    values.insert(QString::fromLatin1(kGameWindowMode), gameWindowModeToken(m_gameWindowMode));
    values.insert(QString::fromLatin1(kProcessPriority), processPriorityKey(m_processPriority));
    values.insert(QString::fromLatin1(kMainWindowWidth), QVariant(clampMainWindowSize(m_mainWindowSize).width()));
    values.insert(QString::fromLatin1(kMainWindowHeight), QVariant(clampMainWindowSize(m_mainWindowSize).height()));
    values.insert(QString::fromLatin1(kWarnDuplicateLaunch), QVariant(m_warnDuplicateLaunch));
    values.insert(QString::fromLatin1(kWarnWorldLoss), QVariant(m_warnWorldLoss));
    values.insert(QString::fromLatin1(kCurseForgeApiKey), m_curseForgeApiKey);
    // NOTE: `kCurseForgeApiBase` / `kModrinthApiBase` / 那四个地址覆盖**刻意不在这里** ——
    // 那几个键是验证工程的口子，产品一写就等于把用户自己指的地址抹掉（见 .hpp 里那条 NOTE）。
    return values;
}

void LauncherSettings::save() const {
    const QVariantMap now = persistedValues();
    QVariantMap changed;
    for (auto it = now.cbegin(); it != now.cend(); ++it) {
        // **只写本进程真改过的那几项**。和 m_persisted 一样的不碰盘 —— 那一项可能是
        // 另一份启动器刚改过的（全局设置按机器共用，见 .hpp 里那条 NOTE）。
        if (m_persisted.value(it.key()) == it.value()) {
            continue;
        }
        changed.insert(it.key(), it.value());
    }
    // 一项都没变：不写、不 sync。`setXxx()` 里已经挡了一层"和成员相同就不 save"，这里是
    // 第二道 —— 挡住"值本来就和盘上一样、但调用方还是叫了一趟 save()"。
    if (changed.isEmpty()) {
        return;
    }

    // 立刻落盘。这几项都是"关掉启动器之前必须记住"的状态，不像 GlobalJavaSettings
    // 那样攒到下载 Java 时才写一次；启动器被强杀也不该丢掉上次启动的版本。
    //
    // 走 SettingsSyncRetry（和 TokenStore / AccountStore 同一条路）：QSettings::sync()
    // 在 Windows 上做 rename，装了杀毒/管家的机器上会被过滤驱动短暂占住几毫秒、报
    // AccessError。只试一次的话这一次改动就没了，之后读回来还是旧值 —— 用户看到的是
    // "改了设置，过一会儿又变回去了"。重试只在这一批键上做，每次尝试都新建 QSettings
    // 重新读写，最终文件内容只取决于最后一次成功的那次。
    QString fileName;
    int attempts = 0;
    const bool ok = SettingsSyncRetry::write(
        [&changed](QSettings &settings) {
            for (auto it = changed.cbegin(); it != changed.cend(); ++it) {
                settings.setValue(it.key(), it.value());
            }
        },
        &fileName, &attempts);
    if (!ok) {
        // 基线不推进：下一次改同一项（或者用户再点一次）会重试。把"写失败"当成
        // "已经写进去"才是更糟的那一种 —— 用户以为设置生效了，重启又变回去。
        qWarning() << "LauncherSettings: 写入设置失败（已试" << attempts
                   << "次），未推进已保存基线：" << fileName;
        return;
    }
    m_persisted = now;
}
