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
static constexpr const char kDownloadSource[] = "Download/Source";

// 枚举存进 QSettings 的字符串。这几个串是**持久化格式的一部分**，不是内部标识：
// 改掉它们，用户盘上已经存好的值就会被当成"认不出来"、静默退回默认档。
static constexpr const char kBehaviourUnchanged[] = "unchanged";
static constexpr const char kBehaviourMinimize[] = "minimize";
static constexpr const char kBehaviourClose[] = "close";
static constexpr const char kBehaviourCloseAndRestore[] = "closeAndRestore";
static constexpr const char kSourceOfficial[] = "official";
static constexpr const char kSourceMirror[] = "mirror";

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

void LauncherSettings::load() {
    QSettings settings;
    m_lastLaunchedVersion = settings.value(kLastLaunched, m_lastLaunchedVersion).toString();
    m_launcherBehaviour = behaviourFromKey(
        settings.value(kAfterLaunch, behaviourKey(m_launcherBehaviour)).toString());
    m_downloadSource = downloadSourceFromKey(
        settings.value(kDownloadSource, downloadSourceKey(m_downloadSource)).toString());

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
    settings.setValue(kAfterLaunch, behaviourKey(m_launcherBehaviour));
    settings.setValue(kDownloadSource, downloadSourceKey(m_downloadSource));
    settings.setValue(kNameShowCustom, m_versionNameOptions.showCustomName);
    settings.setValue(kNameShowReal, m_versionNameOptions.showRealVersion);
    settings.setValue(kNameShowLoaderName, m_versionNameOptions.showLoaderName);
    settings.setValue(kNameShowLoaderVersion, m_versionNameOptions.showLoaderVersion);

    // 立刻落盘。这几项都是"关掉启动器之前必须记住"的状态，不像 GlobalJavaSettings
    // 那样攒到下载 Java 时才写一次；启动器被强杀也不该丢掉上次启动的版本。
    settings.sync();
}
