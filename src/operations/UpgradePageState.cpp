/**
 * @file UpgradePageState.cpp
 * @brief 见头文件：升级页的状态判断（纯逻辑）。
 */
#include "UpgradePageState.hpp"

#include <QCryptographicHash>

namespace UpgradePageState {
namespace {

QString combine(const State &state) {
    // 目标版本 / 加载器 + 选择的已选键；名字不进来（改名不该让检查作废）。
    return state.target.gameVersion + QLatin1Char('\n') + state.target.loaderKey + QLatin1Char('\n')
           + state.target.loaderVersion + QLatin1Char('\n')
           + state.selection.state.ordered().join(QLatin1Char(','));
}


}  // namespace

QString planFingerprint(const State &state) {
    const QString material = combine(state);
    return QString::fromLatin1(
        QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha1).toHex());
}

bool setTargetVersion(State &state, const QString &gameVersion) {
    if (state.target.gameVersion == gameVersion) {
        return false;
    }
    state.target.gameVersion = gameVersion;
    return true;
}

bool setTargetLoader(State &state, const QString &loaderKey, const QString &loaderVersion) {
    if (state.target.loaderKey == loaderKey && state.target.loaderVersion == loaderVersion) {
        return false;
    }
    state.target.loaderKey = loaderKey;
    state.target.loaderVersion = loaderVersion;
    return true;
}

bool setInstanceName(State &state, const QString &name, const QString &sourceId,
                     const QStringList &existingNames) {
    state.target.instanceName = name;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    if (trimmed.compare(sourceId, Qt::CaseInsensitive) == 0) {
        return false;
    }
    for (const QString &existing : existingNames) {
        if (trimmed.compare(existing, Qt::CaseInsensitive) == 0) {
            return false;
        }
    }
    return true;
}

void markReportChecked(State &state) {
    state.checkedFingerprint = planFingerprint(state);
    state.reportValid = true;
}

Blocker firstBlocker(const State &state) {
    const bool versionMissing = state.target.gameVersion.trimmed().isEmpty();
    const bool loaderVersionMissing = !state.target.loaderKey.isEmpty()
                                      && state.target.loaderVersion.trimmed().isEmpty();
    if (versionMissing || loaderVersionMissing) {
        return Blocker::TargetIncomplete;
    }
    if (state.instanceBusy) {
        return Blocker::InstanceBusy;
    }
    const QString name = state.target.instanceName.trimmed();
    if (name.isEmpty()) {
        return Blocker::NameEmpty;
    }
    if (state.selection.state.ordered().isEmpty()) {
        return Blocker::NoSelection;
    }
    // 配置的归属不能按文件名猜：没搬模组就别单独搬配置，让用户在页面上二选一。
    if (state.selection.state.contains(QStringLiteral("config"))
        && !state.selection.state.contains(QStringLiteral("mods"))) {
        return Blocker::ConfigWithoutMods;
    }
    if (!state.reportValid) {
        return Blocker::ReportMissing;
    }
    if (state.checkedFingerprint != planFingerprint(state)) {
        return Blocker::ReportStale;
    }
    return Blocker::None;
}

QString blockerText(Blocker blocker) {
    switch (blocker) {
    case Blocker::TargetIncomplete:
        return QStringLiteral("先把目标版本和加载器选全。");
    case Blocker::InstanceBusy:
        return QStringLiteral("这个实例正在运行或被别的操作占用，等它停下来再升级。");
    case Blocker::NameEmpty:
        return QStringLiteral("新实例的名字不能空着。");
    case Blocker::NameTaken:
        return QStringLiteral("这个名字已经有一个实例在用了，换一个。");
    case Blocker::NoSelection:
        return QStringLiteral("至少选一类要迁移的内容。");
    case Blocker::ConfigWithoutMods:
        return QStringLiteral("勾了 Mod 配置却没勾 Mod 文件 —— 配置归属不能按文件名猜："
                              "要么把 Mod 一起搬，要么别搬配置。");
    case Blocker::ReportMissing:
        return QStringLiteral("先做一次兼容性检查，再确认迁移。");
    case Blocker::ReportStale:
        return QStringLiteral("目标或选择改过了，需要重新检查一次。");
    case Blocker::None:
        break;
    }
    return QString();
}

}  // namespace UpgradePageState




