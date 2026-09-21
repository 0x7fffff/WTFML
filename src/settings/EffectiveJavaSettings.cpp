/**
 * @file EffectiveJavaSettings.cpp
 * @brief 实现覆盖层的合成与 wtfml.meta 之间的序列化。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 全是纯函数：没有 QObject、不读盘、不落盘。
 */
#include "EffectiveJavaSettings.hpp"

#include <QJsonArray>
#include <QJsonValue>

namespace {

/// meta 里的字段名。**只有这一处写它们**，读和写共用，免得哪天有人改了一边。
/// 和 VersionManager 里 kInstanceIdField / kDisplayNameField 同一个约定。
const QString kJavaPathField = QStringLiteral("javaPath");
const QString kXmxField = QStringLiteral("xmx");
const QString kXmsField = QStringLiteral("xms");
const QString kAutoMemoryField = QStringLiteral("autoMemory");
const QString kExtraJvmArgsField = QStringLiteral("extraJvmArgs");

/// 下限，和 GlobalJavaSettings::setXmx()/setXms() 里那两个数一致（那边也是唯一出处，
/// 这里重写一遍是因为那个类在 QObject 那一层、这个文件要能被不带 Qt Widgets 的
/// 验证工程直接链 —— 两个数各出现一次，比多一层依赖划算）。
constexpr int kMinXmxMb = 256;
constexpr int kMinXmsMb = 128;

/// 读一个字符串项：类型不对当没有。
std::optional<QString> readString(const QJsonObject &meta, const QString &key) {
    const QJsonValue value = meta.value(key);
    if (!value.isString()) {
        return std::nullopt;
    }
    return value.toString();
}

/// 读一个整数项：类型不对当没有。JSON 没有整数类型，只认能当整数用的数
/// （4096.0 收下，4096.5 也收下 —— toInt() 截断；写坏成字符串的当没写）。
std::optional<int> readInt(const QJsonObject &meta, const QString &key) {
    const QJsonValue value = meta.value(key);
    if (!value.isDouble()) {
        return std::nullopt;
    }
    return value.toInt();
}

}  // namespace

EffectiveJavaSettings resolveJavaSettings(const EffectiveJavaSettings &global,
                                          const JavaSettingsOverride &override) {
    EffectiveJavaSettings result = global;

    if (override.javaPath.has_value()) {
        result.javaPath = *override.javaPath;
    }
    if (override.extraJvmArgs.has_value()) {
        result.extraJvmArgs = *override.extraJvmArgs;
    }
    if (override.autoMemory.has_value()) {
        result.autoMemory = *override.autoMemory;
    }

    // 内存这两个数要**一起**处理：先定 Xmx（它是 Xms 的上界），再夹 Xms。
    // 分开写（各自 qMax 一下就完事）会漏掉"只覆盖了 Xms、而它比有效 Xmx 还大"这种
    // 组合 —— 那个值为真时 JVM 起不来，而且报的是一句看不懂的初始化错误。
    result.xmx = qMax(kMinXmxMb, override.xmx.value_or(global.xmx));
    result.xms = qBound(kMinXmsMb, override.xms.value_or(global.xms), result.xmx);

    return result;
}

JavaSettingsOverride javaOverrideFromJson(const QJsonObject &meta) {
    JavaSettingsOverride override;
    override.javaPath = readString(meta, kJavaPathField);
    override.xmx = readInt(meta, kXmxField);
    override.xms = readInt(meta, kXmsField);
    override.extraJvmArgs = readString(meta, kExtraJvmArgsField);

    const QJsonValue autoMemory = meta.value(kAutoMemoryField);
    if (autoMemory.isBool()) {
        override.autoMemory = autoMemory.toBool();
    }

    return override;
}

QJsonObject javaOverrideToJson(const JavaSettingsOverride &override) {
    QJsonObject fields;
    // 每一项：有值写值，没值写 Null（= 让 updateVersionMeta 把这个键删掉）。
    // 五项一个都不能漏 —— 漏掉的那一项会永远留在文件里，用户把它切回「跟随全局」
    // 之后它还在生效，而界面上已经不显示它了。
    fields.insert(kJavaPathField, override.javaPath.has_value()
                                      ? QJsonValue(*override.javaPath)
                                      : QJsonValue(QJsonValue::Null));
    fields.insert(kXmxField, override.xmx.has_value() ? QJsonValue(*override.xmx)
                                                      : QJsonValue(QJsonValue::Null));
    fields.insert(kXmsField, override.xms.has_value() ? QJsonValue(*override.xms)
                                                      : QJsonValue(QJsonValue::Null));
    fields.insert(kAutoMemoryField, override.autoMemory.has_value()
                                        ? QJsonValue(*override.autoMemory)
                                        : QJsonValue(QJsonValue::Null));
    fields.insert(kExtraJvmArgsField, override.extraJvmArgs.has_value()
                                          ? QJsonValue(*override.extraJvmArgs)
                                          : QJsonValue(QJsonValue::Null));
    return fields;
}

QJsonObject javaOverrideMetaFields(const JavaSettingsOverride &override,
                                   const QStringList &appendJvmArgs, const QString &jvmArgsKey) {
    QJsonObject fields = javaOverrideToJson(override);

    // 追加参数用**数组**不用空格分隔的一条串：参数本身可能含空格
    // （-Dfoo=bar baz 会被空格分隔切成两条）。
    if (appendJvmArgs.isEmpty()) {
        fields.insert(jvmArgsKey, QJsonValue(QJsonValue::Null));
    } else {
        QJsonArray array;
        for (const QString &arg : appendJvmArgs) {
            array.append(arg);
        }
        fields.insert(jvmArgsKey, array);
    }
    return fields;
}
