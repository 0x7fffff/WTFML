/**
 * @file DownloadDiagnostics.hpp
 * @brief 下载链路的"特供版"详细日志（普通构建里一行都不多写）。
 * @author 0x7fffff
 * @date 2026-10-17
 *
 * 背景：有一位测试员的网络环境下下载一直失败，而产品日志里只有 `Download failed: ...`
 * 那一行 —— 「失败」这个词后面没有任何能区分"连不上 / 连上了但没数据 / 镜像坏了 /
 * 校验不过 / 写盘失败"的信息。这一层把每一条请求的地址、状态码、协议、字节数、耗时、
 * 关键响应头和每一次换源/重试/放弃的**决定**都记下来。
 *
 * 两条打开方式（唯一判据在 enabled()）：
 *   · `-DWTFML_DIAG_DOWNLOAD=ON`：编译进 exe，永远开着（发给测试员的特供版）；
 *   · 环境变量 `WTFML_DOWNLOAD_DEBUG=1`：普通构建临时打开（开发机排查用）。
 *
 * 落点：`<数据目录>/logs/wtfml-download.log`（每次启动截断）。同时每一行也走 qInfo，
 * 所以同一份明细在 `wtfml.log` 里也看得到 —— 用户只需要把 logs 目录整个发回来。
 *
 * NOTE: 编译期那个宏**只作用于 DownloadDiagnostics.cpp 一个文件**（见 CMakeLists）：调用点
 *       一律问 `enabled()`，所以普通构建里它只是一个缓存成 false 的普通函数调用，
 *       而切换特供版开关只重编这一个文件。
 * NOTE: 这一层**只读**：不碰任何下载状态、不改判据、失败也只是少一份日志。
 * NOTE: 「进度不进日志」这条产品约束照旧成立：这里记的是每条请求的**结果**和决策，
 *       不是每一跳的百分比（看门狗自己那条 tick 也不写）。
 */

#pragma once

#include <QString>
#include <QUrl>
#include <functional>

namespace DownloadDiag {

// Standalone diagnostics may replace file/qInfo output with a redacting structured sink.
// Empty restores normal launcher behavior. Configure before starting downloads.
void setSink(std::function<void(const QString &)> sink);

/// 这一份构建 / 这一次进程要不要记明细。
///
/// 编译期打开时恒真；否则看环境变量 `WTFML_DOWNLOAD_DEBUG`（空 / `0` = 关）。
/// 结果缓存一次 —— 它在一个进程里不会变。
bool enabled();

/// 打开（截断）本次启动的诊断日志。@p logsDir 一般是 `<数据目录>/logs`。
///
/// 不调用也能记：第一行会按当时的数据目录自己开文件。main() 里排在 openFileLog()
/// 之后调，是为了和 wtfml.log 落在同一个目录、同一份"这一次启动"上。
void init(const QString &logsDir);

/// 诊断日志的完整路径（没开成 / 没启用时为空串）。
QString logFilePath();

/// 记一行明细。普通构建里直接返回；失败（开不了文件）时只走 qInfo。
void line(const QString &text);

/// 启动环境快照：构建形态、版本、系统、路径、代理、TLS、磁盘余量。
/// 一次启动记一次 —— 排查"只有他的机器失败"时，最先要看的就是这一份。
void logEnvironment();

/// URL 的展示形式：去掉用户名 / 口令。日志会被用户原样发回来，不该带着凭据。
QString safeUrl(const QString &url);
QString safeUrl(const QUrl &url);

}  // namespace DownloadDiag
