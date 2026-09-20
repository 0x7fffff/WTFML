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

#include <QString>
#include <QStringList>

namespace ZipUtil {

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

}  // namespace ZipUtil
