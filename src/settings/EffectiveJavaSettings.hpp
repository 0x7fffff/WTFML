/**
 * @file EffectiveJavaSettings.hpp
 * @brief 「每实例设置」的覆盖层，以及把全局设置与覆盖合成一份**有效值**的纯函数。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 只依赖 QString / QJsonObject / <optional>，没有 QObject、不落盘、不弹窗 ——
 *       「启动五项」两级（跟随全局 / 自定义）的那套判断全在这里，验证工程直接链。
 *
 * 三个层次，从下往上：
 *   JavaSettingsOverride —— 一个实例在 wtfml.meta 里**存下来**的那几项覆盖；
 *   EffectiveJavaSettings —— 这次启动**真的要用**的那一套值；
 *   resolveJavaSettings() —— 把全局值按覆盖盖一遍，得到后者。
 *
 * 【为什么每项是 optional 而不是"等于全局的值"】
 *
 * 没有值 = 跟随全局，有值 = 这个实例自己定。这两件事不能用同一个值表达：用户给某
 * 一项选了「跟随全局」时，如果往 meta 里写一个"当前恰好等于全局"的数，那之后用户改
 * 全局，这个实例**不会**跟着走 —— 表面上是"跟随"，行为上是钉死。存储侧也因此有那条
 * 约定：QJsonValue::Null 表示删掉这个键（见 VersionManager::updateVersionMeta）。
 */
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>

/**
 * @brief 一个实例对启动五项的覆盖。每一项独立：可以只覆盖 Xmx 而不碰别的。
 *
 * NOTE: 空串**也算有值**。「自定义 Java 路径、但还没填」和"跟随全局"必须分得开，
 *       否则用户清了路径框、点确定，再打开对话框时那一项会自己跳回「跟随全局」——
 *       看起来像他的选择没保存上。
 */
struct JavaSettingsOverride {
    std::optional<QString> javaPath;
    std::optional<int> xmx;
    std::optional<int> xms;
    std::optional<bool> autoMemory;
    std::optional<QString> extraJvmArgs;

    /// 五项一个都没覆盖（= 这个实例完全跟随全局）。启动路径拿它走"和没有覆盖层
    /// 之前逐字节一致"的那条路。
    bool isEmpty() const {
        return !javaPath.has_value() && !xmx.has_value() && !xms.has_value() &&
               !autoMemory.has_value() && !extraJvmArgs.has_value();
    }
};

/**
 * @brief 这次启动真的要用的那一套 Java 设置。
 *
 * 生产者只有两个：GlobalJavaSettings::effective()（全局那一层）和
 * resolveJavaSettings()（合成之后）。**不要**手搓一个出来当全局值用 —— 那个类的
 * 默认值（xmx/xms 都是 0）是给"必须被填过才有意义"这件事留的哨兵。
 *
 * NOTE: xmx/xms 不在这里钳位（构造完才发现要合并），钳位在 resolveJavaSettings() 里
 *       统一做，所以一份全 0 的结构体经过合成之后也是合法值。
 */
struct EffectiveJavaSettings {
    /// Java 可执行文件（或含 bin/java.exe 的运行时目录）。空 = 还没挑过。
    QString javaPath;
    /// 给 JVM 的 -Xmx / -Xms（MB）。
    int xmx = 0;
    int xms = 0;
    /// 内存是不是自动档（按系统内存现算）。true 时 xmx/xms 会被重算，
    /// 重算那一步**不在这个纯函数里** —— 它要用系统读数、还可能要写回磁盘，
    /// 是 MainWindow::resolveMemoryForLaunch() 的事。
    bool autoMemory = true;
    /// 给 JVM 的额外参数（空格分隔的一条串，要拆开用）。
    QString extraJvmArgs;
};

/**
 * @brief 把全局设置和某个实例的覆盖合成一份有效值。
 *
 * 规则：
 *   - 每一项：覆盖有值就用覆盖的，没有就用全局的；
 *   - Xmx 钳到 ≥ 256、Xms 钳到 ≥ 128（和 GlobalJavaSettings::setXmx/setXms 一致），
 *     并且**先定 Xmx 再拿它当 Xms 的上界** —— Xms > Xmx 会让 JVM 直接拒绝启动
 *     （Initial heap size ... larger than the maximum heap size）。用户可以只覆盖
 *     Xmx 不覆盖 Xms，也可以只覆盖 Xms 不覆盖 Xmx，两个方向都得兜住；
 *   - **不**做"自动内存"的重算：那需要读系统内存、还可能要把结果写回磁盘，
 *     这不是纯函数该干的事（见 MainWindow::resolveMemoryForLaunch()）。
 *     所以这里的结果在 autoMemory 为真时只是"两个占位的数"，别拿它当最终值用。
 *   - **不**判"这个版本要多新的 Java"：那是 ensureJavaForVersion() 的事。
 *
 * NOTE: 覆盖里的数**没有上限**。Xmx 只在下限钳位，因为 meta 是用户手写的文件，
 *       写一个 999999 的 Xmx 和自己改命令行是一回事（同 versionJvmArgs() 的态度：
 *       不做白名单校验）。对话框里那个数字框自己有上限（65536），走界面进来的值
 *       到不了这里就超界。
 */
EffectiveJavaSettings resolveJavaSettings(const EffectiveJavaSettings &global,
                                          const JavaSettingsOverride &override);

/**
 * @brief 从 wtfml.meta 的对象里读出覆盖层。
 * @param meta 整个 meta 对象（VersionManager::versionMeta()）。
 *
 * 字段名：javaPath / xmx / xms / autoMemory / extraJvmArgs。
 *
 * NOTE: 额外参数那个键叫 `extraJvmArgs` 而**不是** `jvmArgs`，这是刻意的：meta 里
 *       已有的 `jvmArgs`（数组）是"追加在这一实例命令行最后"的那一份，两者语义
 *       不同（一个**取代**全局的，一个排在全局**之后**）。名字混用不会报错，只会
 *       让用户写的参数在某一天悄悄换一种生效方式。
 *
 * 类型不对的项**当没有**（= 跟随全局），和读侧对 name / jvmArgs 的态度一致：
 * 文件是用户手写的，写错类型当它没写，不猜。
 */
JavaSettingsOverride javaOverrideFromJson(const QJsonObject &meta);

/**
 * @brief 把覆盖层转成**要写进 meta 的那批字段**。
 *
 * 有值的那几项写成值；**没有的那几项写成 QJsonValue::Null** —— updateVersionMeta()
 * 把 Null 解释成"删掉这个键"，这正是用户点「跟随全局」时想要的结果。所以这个函数
 * 的返回值可以直接交给 updateVersionMeta()，不用调用方再补一遍删除项。
 */
QJsonObject javaOverrideToJson(const JavaSettingsOverride &override);

/**
 * @brief 一份覆盖 + 追加参数 → 这一次要交给 updateVersionMeta() 的**整批字段**。
 *
 * = javaOverrideToJson(override)，再补上追加参数那个键。写侧（「实例设置…」点确定）
 * 要的就这一份：五项是"跟随全局就删键"，追加参数是"没有就删键"。
 *
 * 单独抽出来是为了**验得了**：MainWindow::applyInstanceSettings() 收的是一个对话框
 * 引用，验证工程没法按名字驱动它（自定义类型进不了 Q_ARG），于是那半条"值 → 要写什么"
 * 的判断就没有断言盯着。搬到这里之后它是一个纯函数，R 段直接喂值、比对 JSON。
 *
 * @param override 对话框摆好的覆盖。
 * @param appendJvmArgs 追加参数（一条一个元素）。**空**时那个键写 Null = 删键，
 *        而不是写一个空数组 —— 读侧两种都当空，但"这个实例没有追加参数"更诚实的
 *        表达是键不存在（手写文件的人看到一个空数组会以为该往里填）。
 * @param jvmArgsKey 追加参数那个键的名字。**按参数收**而不是在这里写死：这个名字的
 *        读侧在 VersionManager::versionJvmArgs()，键名定义也只有一处
 *        （VersionManager::jvmArgsField()）。settings/ 再写一遍字面量的话，两边
 *        哪天对不上就是**静默不生效**（键名不匹配不报错，参数就是不进命令行）。
 */
QJsonObject javaOverrideMetaFields(const JavaSettingsOverride &override,
                                   const QStringList &appendJvmArgs, const QString &jvmArgsKey);
