/**
 * @file ZipUtil.hpp
 * @brief 通过 PowerShell 解压压缩包的通用工具。
 * @author 0x7fffff
 * @date 2026-09
 * @note 项目没有引入 zlib/QuaZip 之类的解压库，沿用 GlobalJavaSettings 里
 *       已有的"调用 PowerShell"方案。这里改用 .NET 的 ZipFile，因为
 *       Expand-Archive 既不能排除 META-INF/，对 .jar 扩展名也不可靠。
 */
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace ZipUtil {

/// 读一个条目的结果。**这四样必须分得清**：用户选的那个压缩包到底是不是整合包
/// （NotFound），和它是不是一个坏文件（NotAnArchive / Failed），要给的是两句完全不同的话。
enum class EntryStatus {
    Ok,           ///< 读到了，字节在 out 里
    NotFound,     ///< 压缩包能打开，但里面没有这个条目
    NotAnArchive, ///< 打不开：它不是 zip（改过扩展名的 rar/7z），或者文件坏了
    Failed,       ///< 别的失败（超时、写不出来），细节在 errorOut 里
};

/**
 * @brief 把多个压缩包解压到同一个目录。
 * @param archives 待解压的压缩包绝对路径（.zip 或 .jar）。
 * @param targetDir 目标目录，不存在则自动创建。
 * @param excludePrefixes 需要跳过的条目前缀，例如 {"META-INF/"}。
 * @param errorOut 失败时的错误描述。
 * @return 若全部解压成功则返回 true.
 */
bool extractArchives(const QStringList &archives, const QString &targetDir,
                     const QStringList &excludePrefixes = QStringList(),
                     QString *errorOut = nullptr);

/**
 * @brief 读压缩包里**一个条目**的原始字节（不解压其它任何东西）。
 * @param archive 压缩包绝对路径（.zip）。
 * @param entryName 条目名，`/` 分隔的完整路径，例如 `modrinth.index.json`。
 *        先按原样找，找不到再**不区分大小写**找一遍（有些整合包工具会把文件名写得大小写
 *        不一样，而 zip 的条目名是区分大小写的）。
 * @param out 成功时写入条目的原始字节（会被 clear()）。
 * @param errorOut 失败时的人类可读描述（NotAnArchive 时是空串 —— 那种情况调用方自己有话说）。
 * @param what 这次动作叫什么，拼进 errorOut 那几句话里（`读 X 超时` / `读 X 时进程被强行结束了`）。
 *        默认是通用的一句；**调用方要把它写清楚**，因为 Failed 那一路的 errorOut 有一处是
 *        **直接弹给用户看的**（整合包那条路），句子里的主语换成别人就说不通了。
 * @return 见 EntryStatus。**没这个条目**和**读失败**是两件事，调用方必须分开处理。
 */
EntryStatus readEntry(const QString &archive, const QString &entryName, QByteArray *out,
                      QString *errorOut = nullptr,
                      const QString &what = QStringLiteral("读压缩包条目"));

/**
 * @brief 只解压某几个前缀底下的条目，而且**把前缀剥掉**（`overrides/config/x` → `<目标>/config/x`）。
 * @param archives 待解压的压缩包绝对路径（.zip）。
 * @param targetDir 目标目录，不存在则自动创建。
 * @param roots 前缀（`overrides/` / `client-overrides/` 这种），**顺序有意义**：
 *        排后面的**覆盖**排前面的 —— 整合包的规矩是 client-overrides 盖过 overrides。
 * @param extractedOut 非空时回填真正写出去的条目数（给"解开了 N 个覆盖文件"那句话用）。
 * @param errorOut 失败时的描述。
 * @return 全部成功返回 true。
 *
 * 不属于任何前缀的条目**一个都不碰**（清单文件、README 之类都不会落进实例目录）。
 */
bool extractSubtrees(const QStringList &archives, const QString &targetDir,
                     const QStringList &roots, int *extractedOut = nullptr,
                     QString *errorOut = nullptr);

}  // namespace ZipUtil
