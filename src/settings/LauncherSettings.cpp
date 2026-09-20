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

void LauncherSettings::load() {
    QSettings settings;
    m_lastLaunchedVersion = settings.value(kLastLaunched, m_lastLaunchedVersion).toString();

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
    settings.setValue(kNameShowCustom, m_versionNameOptions.showCustomName);
    settings.setValue(kNameShowReal, m_versionNameOptions.showRealVersion);
    settings.setValue(kNameShowLoaderName, m_versionNameOptions.showLoaderName);
    settings.setValue(kNameShowLoaderVersion, m_versionNameOptions.showLoaderVersion);

    // 立刻落盘。这几项都是"关掉启动器之前必须记住"的状态，不像 GlobalJavaSettings
    // 那样攒到下载 Java 时才写一次；启动器被强杀也不该丢掉上次启动的版本。
    settings.sync();
}
