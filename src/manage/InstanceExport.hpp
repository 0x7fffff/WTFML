/**
 * @file InstanceExport.hpp
 * @brief 「普通实例导出」：把一个已装好的实例打成 Prism/MultiMC 能直接导入的 zip。
 *
 * 为什么不是 `.mrpack` / CurseForge 清单：那两种格式只描述"装哪个加载器 + 下哪些文件"，
 * 放不下自定义主类、JVM 参数和专用库。GTNH 这类包只有 Prism/MultiMC 的实例结构能完整
 * 带着走。普通实例（原版 / Forge / Fabric / NeoForge / Quilt）导出成这个形状，
 * Prism、MultiMC 都能直接"拖进去"建实例。
 *
 * 结构：`instance.cfg`（名字）+ `mmc-pack.json`（net.minecraft + 加载器组件）+
 * `.minecraft/`（实例目录里的实际内容）。不复制 `libraries/`（那是启动器级共享目录，
 * 导入方会按组件自己解析）。
 */
#pragma once

#include <QString>

namespace InstanceExport {

struct Result {
    bool ok = false;
    QString problem;   ///< 失败原因（ok=false 时有）
    QString warning;   ///< 成功但有风险时的提醒（例如带自定义启动配置 / 体积过大）
    qint64 bytes = 0;
    int files = 0;
};

/// @param instanceId `versions/` 下的实例 id
/// @param gameVersion 原版版本号（如 1.20.1）
/// @param loaderKey 空 = 纯原版；否则 fabric/forge/neoforge/quilt
/// @param loaderVersion 加载器版本（空 = 不带加载器组件）
/// @param displayName 写进 instance.cfg 的名字
/// @param customLaunch 实例是否带自定义启动配置（主类/JVM 参数），决定 warning
Result exportInstance(const QString &instanceId, const QString &gameVersion,
                      const QString &loaderKey, const QString &loaderVersion,
                      const QString &displayName, bool customLaunch, const QString &targetZip);

}  // namespace InstanceExport
