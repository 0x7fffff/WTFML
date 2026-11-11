/**
 * @file AgentWinPath.hpp
 * @brief Windows 路径/对象身份校验：字符串归一化 + 句柄级最终路径/文件 ID/reparse。
 *
 * 边界（Agent.md §9.3）：字符串检查只做快速拒绝，**不能单独当安全结论**。已存在的路径
 * 必须再用 `CreateFileW`（目录加 `FILE_FLAG_BACKUP_SEMANTICS`，最终组件加
 * `FILE_FLAG_OPEN_REPARSE_POINT`）打开，核对最终路径、卷序列 + 文件 ID、reparse 属性。
 *
 * NOTE: 本文与实现都**不宣称彻底解决 TOCTOU**。Windows 没有等价的 `openat`；
 * 句柄校验 + 写后身份比对只能缩小竞态窗口。失败时明确报错，不假装成功。
 */

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace AgentWinPath {

struct Info {
    QString finalPath;
    bool exists = false;
    bool isDirectory = false;
    bool reparse = false;
    quint64 volumeSerial = 0;
    QByteArray fileId;
    qint64 size = 0;
    qint64 mtimeMs = 0;
};

/// 只接受相对路径。拒绝绝对路径、盘符、UNC、`..`、ADS `:`、保留设备名、尾点/空格、
/// 控制字符和过长路径。成功时 @p normalized 是 `/` 分隔、无空段的形式。
bool normalizeRelative(const QString &relative, QString *normalized, QString *problem);

/// 打开已存在路径并做句柄级校验。@p expectDirectory 只用于提前说明期望类型。
bool resolveRoot(const QString &root, Info *info, QString *problem);

bool resolveExisting(const QString &root, const QString &relative, bool expectDirectory,
                     Info *info, QString *problem);

/// 校验目标文件的父目录（父目录必须已存在、非 reparse、最终路径与预期一致）。
bool resolveParent(const QString &root, const QString &relative, Info *info, QString *problem);

/// `v1:<volume>:<fileId>:<size>:<mtime>`；sha256 单独作为 `expectedSha256`。
QString identityToken(const Info &info);

/// 比对 token 与当前对象身份（不比对内容 hash；hash 由调用方单独核）。
bool verifyIdentityToken(const QString &token, const Info &info);

bool isProtectedRelative(const QString &relative);
bool isTextExtension(const QString &relative);
bool looksBinary(const QByteArray &bytes);

}  // namespace AgentWinPath
