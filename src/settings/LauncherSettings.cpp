/**
 * @file LauncherSettings.cpp
 * @brief 实现启动器设置的读写。
 * @author 0x7fffff
 * @date 2026-09-20
 * @note 见 LauncherSettings.hpp 的说明。
 */

#include "LauncherSettings.hpp"

#include <QDebug>
#include <QSettings>

namespace {

// 键名带 Version/ 前缀，和 GlobalJavaSettings 的 Java/ 前缀一个路子：
// 不用 beginGroup，键名本身就是分组，在注册表里直接看得出归属。
static constexpr const char kLastLaunched[] = "Version/LastLaunched";
static constexpr const char kNameShowCustom[] = "Version/NameShowCustom";
static constexpr const char kNameShowReal[] = "Version/NameShowReal";
static constexpr const char kNameShowLoaderName[] = "Version/NameShowLoaderName";
static constexpr const char kNameShowLoaderVersion[] = "Version/NameShowLoaderVersion";
static constexpr const char kAfterLaunch[] = "Launcher/AfterLaunch";
static constexpr const char kFolders[] = "Launcher/Folders";
static constexpr const char kCurrentFolder[] = "Launcher/CurrentFolder";
static constexpr const char kFolderNames[] = "Launcher/FolderNames";
static constexpr const char kDownloadSource[] = "Download/Source";
static constexpr const char kShowGameLog[] = "Launcher/ShowGameLog";
static constexpr const char kGameWindowWidth[] = "Launcher/WindowWidth";
static constexpr const char kGameWindowHeight[] = "Launcher/WindowHeight";
static constexpr const char kProcessPriority[] = "Launcher/ProcessPriority";

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

void LauncherSettings::setProcessPriority(ProcessPriority priority) {
    if (m_processPriority == priority) {
        return;
    }
    m_processPriority = priority;
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
    // 认不出来（旧版本写的值、用户手改的注册表）就当"不变"：这是唯一一个
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
    // 默认值同样取当前成员：load() 被调第二次而盘上是空的时，值应该留着而不是被抹掉。
    m_folders = settings.value(kFolders, m_folders).toStringList();
    m_currentFolder = settings.value(kCurrentFolder, m_currentFolder).toString();
    m_folderNames = settings.value(kFolderNames, m_folderNames).toStringList();
    m_launcherBehaviour = behaviourFromKey(
        settings.value(kAfterLaunch, behaviourKey(m_launcherBehaviour)).toString());
    m_downloadSource = downloadSourceFromKey(
        settings.value(kDownloadSource, downloadSourceKey(m_downloadSource)).toString());
    m_showGameLog = settings.value(kShowGameLog, m_showGameLog).toBool();
    // 宽高**读回来就钳位**：这两个数会原样进游戏命令行，盘上那份不能当成可信的。
    // 存进去的值也可能是手改的，所以两边都要兜。
    m_gameWindowWidth =
        clampWindowWidth(settings.value(kGameWindowWidth, m_gameWindowWidth).toInt());
    m_gameWindowHeight =
        clampWindowHeight(settings.value(kGameWindowHeight, m_gameWindowHeight).toInt());
    m_processPriority = processPriorityFromKey(
        settings.value(kProcessPriority, processPriorityKey(m_processPriority)).toString());

    // 默认值取当前成员的值（构造时是 struct 默认值），这样"注册表里没这一项"就落到默认档，
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
    settings.setValue(kFolders, m_folders);
    settings.setValue(kCurrentFolder, m_currentFolder);
    settings.setValue(kFolderNames, m_folderNames);
    settings.setValue(kAfterLaunch, behaviourKey(m_launcherBehaviour));
    settings.setValue(kDownloadSource, downloadSourceKey(m_downloadSource));
    settings.setValue(kShowGameLog, m_showGameLog);
    settings.setValue(kGameWindowWidth, clampWindowWidth(m_gameWindowWidth));
    settings.setValue(kGameWindowHeight, clampWindowHeight(m_gameWindowHeight));
    settings.setValue(kProcessPriority, processPriorityKey(m_processPriority));
    settings.setValue(kNameShowCustom, m_versionNameOptions.showCustomName);
    settings.setValue(kNameShowReal, m_versionNameOptions.showRealVersion);
    settings.setValue(kNameShowLoaderName, m_versionNameOptions.showLoaderName);
    settings.setValue(kNameShowLoaderVersion, m_versionNameOptions.showLoaderVersion);

    // 立刻落盘。这几项都是"关掉启动器之前必须记住"的状态，不像 GlobalJavaSettings
    // 那样攒到下载 Java 时才写一次；启动器被强杀也不该丢掉上次启动的版本。
    settings.sync();
}
