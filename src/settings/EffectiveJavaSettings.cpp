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
///
/// javaPath 那个不在这里：它的写侧在 MainWindow（"改用匹配的 Java"要往该实例写回
/// 一个路径），所以它是公开的 javaPathMetaField()，这个文件里也走那一个出处。
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

/// 读一个整数项：类型不对当没有。JSON 没有整数类型，只认"塞得进 int"的数
/// （4096.0 收下、4096.5 收下并按截断算 4096；写坏成字符串的当没写）。
///
/// NOTE: 那句"toInt() 截断"原来是**错的**（2026-09-25 实测，探针见 tmp/probe_toint）：
///       QJsonValue::toInt() 遇到非整数的 double 返回的是**默认值**（不给就 0），它不截断 ——
///       4096.5 → 0、-1024.7 → 0、1e30 → 0，而 4096.0 → 4096。这个 0 **不是**一个显眼的
///       错值：它一路走到下面夹取那一段，最后变成"用户设了 4096 MB、起步内存 256 MB"这种
///       没人报错的错值（Xmx 被钳成下限）。所以截断自己来，越界（和 NaN，比较对它恒假）
///       当**没写** —— "没写"是这里唯一说得清、下游也认得的意思。
std::optional<int> readInt(const QJsonObject &meta, const QString &key) {
    const QJsonValue value = meta.value(key);
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double raw = value.toDouble();
    if (!(raw >= -2147483648.0 && raw <= 2147483647.0)) {
        return std::nullopt;
    }
    return static_cast<int>(raw);
}

}  // namespace

QString javaPathMetaField() { return QStringLiteral("javaPath"); }

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
    override.javaPath = readString(meta, javaPathMetaField());
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
    fields.insert(javaPathMetaField(), override.javaPath.has_value()
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
