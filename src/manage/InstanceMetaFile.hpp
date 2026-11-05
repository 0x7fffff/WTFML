#pragma once
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

/// `wtfml.meta`（实例自己那份设置）唯一的读 / 合并写口径。
///
/// 这个文件是**用户可以手写**的：里面有显示名、JVM 参数、真实版本号这些。所以写侧
/// 只有一条规矩 —— 读懂了才往上合并，读不懂就一个字节都不动。这条规矩以前散在四处
/// （VersionManager::updateVersionMeta、copyInstance、整合包收尾、原地升级），
/// 各版本在大小上限、缩进输出、认不认版本号上都不一样；现在只有这一份。
namespace InstanceMetaFile {
/// 格式版本。文件里没有这个键 = 1（加键之前写出来的都是 1）。比这个新 ⇒ 只读不写：
/// 那种文件可能是更新的启动器写的，按 1 的规矩合并会把它的新字段解释错。
inline constexpr int kSchemaVersion = 1;
inline constexpr const char kSchemaKey[] = "schemaVersion";
/// 超过这么大的文件当读不懂处理 —— 实例设置不该有这么大，别把内存吃光。
inline constexpr qint64 kSizeLimit = 16 * 1024 * 1024;

enum class Status {
    Missing,     ///< 没有这个文件（绝大多数版本）—— 不是错误
    Ok,          ///< 读到了，格式版本能认（或者根本没有版本键）
    Unreadable,  ///< 打不开 / 过大 / 不是 JSON 对象
    BadVersion,  ///< `schemaVersion` 不是这个启动器认识的值
};

/// 读出来。没有文件时返回空对象、`Missing`；读不懂时返回空对象、`Unreadable`。
/// 版本比这个新时**内容照常返回**、`BadVersion` —— 显示名这类字段的意思没变，
/// 读侧不该跟着瞎掉，只有写侧才需要停手。
QJsonObject read(const QString &path, Status *statusOut = nullptr);

/// 读-合并-写。`fields` 里 null / undefined 表示**删掉**那个键（"改回跟随全局"要的
/// 就是"这个键不存在"）。
///
/// 读不懂、版本不认识、目录建不出来、写不进去 ⇒ 返回 false，原因写进 `errorOut`，
/// 盘上原有的字节一个不动。成功时文件里一定带着 `schemaVersion`。
bool mergeWrite(const QString &path, const QJsonObject &fields, QString *errorOut = nullptr);

/**
 * @brief 「这一批字段写下去会和盘上那份不一样吗」—— 不一样才值得写。
 *
 * 逐键比 @p fields 和 @p current（`read()` 那份）：`fields` 里的 **null 约定是"删掉
 * 这个键"**（`mergeWrite()` 的规矩），所以 null 只在盘上**本来就没有**这个键（`Undefined`）
 * 时才算相等。对象/数组逐层比：`{"save": false}` 和 `{}` 是两件不同的事。
 *
 * 只比 @p fields 里列出来的键：盘上多出来的键（别的版本写的 / 用户手写的）不算"不一样"
 * —— 一次没改动的保存不该因为它被触发。
 */
bool fieldsAlreadyEqual(const QJsonObject &current, const QJsonObject &fields);
} // namespace InstanceMetaFile
