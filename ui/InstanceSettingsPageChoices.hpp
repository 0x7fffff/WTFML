/**
 * @file InstanceSettingsPageChoices.hpp
 * @brief 「左栏显示哪几页」那点纯判断：存盘里那份怎么收、按什么顺序摆出来。
 *
 * 【为什么单独一层】
 *
 * 这两件事以前是 `InstanceSettingsDialog` 的私有函数（`setPageChoices()` / `shownEntryKeys()`）：
 * 一个从盘上那份里挑出**认得的键**，一个把"钉死的两页 + 可选的七页里用得上且没被勾掉的"
 * 按固定顺序拼出来。两边都只是**看键、看 bool**，不碰控件 —— 但和对话框的勾选框搅在一起，
 * 验证工程够不着，只能靠"开一页看一眼左栏"的间接用例。挪出来以后它们是纯函数，
 * `ui_check` 的 U9d 直接钉。
 *
 * 【两条规矩】
 *
 * - **认不出的键丢掉**：以后删掉一页时，盘上那条不该跟着在左栏复活（前向兼容）；
 *   非 bool 的值也当没写过。空对象 = 七页全显示（不写"默认值"）。
 * - **顺序由调用方给**：钉死的两页在前，可选的按 `optionalKeys` 顺序在后 —— 不能按
 *   `QHash` 遍历顺序拼，那样每次都可能不一样。
 */
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace InstanceSettingsPageChoices {

/// 从盘上那份里挑出 @p optionalKeys 认得的、值确实是 bool 的那些（其余丢掉）。
inline QJsonObject normalized(const QJsonObject &stored, const QStringList &optionalKeys) {
    QJsonObject out;
    for (const QString &key : optionalKeys) {
        const QJsonValue value = stored.value(key);
        if (value.isBool()) {
            out.insert(key, value.toBool());
        }
    }
    return out;
}

/// 左栏那一列该摆哪些键：钉死的在前；可选的只留"这一页可用 **且** 没被勾掉"的。
inline QStringList visible(const QStringList &pinnedKeys, const QStringList &optionalKeys,
                           const QHash<QString, bool> &available, const QJsonObject &overrides) {
    QStringList out = pinnedKeys;
    for (const QString &key : optionalKeys) {
        if (available.value(key, true) && overrides.value(key).toBool(true)) {
            out.append(key);
        }
    }
    return out;
}

} // namespace InstanceSettingsPageChoices
