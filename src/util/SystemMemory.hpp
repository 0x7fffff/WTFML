/**
 * @file SystemMemory.hpp
 * @brief 读系统物理内存（总量 / 当前可用），给「内存分配自动档」用。
 * @author 0x7fffff
 * @date 2026-09-20
 * @note 这一层只做一件事：把操作系统那边的数字拿回来。**怎么用它算 JVM 内存是策略**，
 *       在 GlobalJavaSettings::suggestXmxMb()/suggestXmsMb() 里，那里是纯函数、可以逐条钉住。
 *       分开的理由：读系统内存取不到值（别的平台、调用失败）时策略照样要能算出一个合理的
 *       兜底值，两件事混在一起就没法分开验。
 */

#pragma once

#include <QtGlobal>

namespace SystemMemory {

/**
 * @brief 一次读数。
 *
 * NOTE: 两个字段都可能是 0 —— 读不到（非 Windows 平台、或者系统调用失败）。调用方
 *       **必须**处理这种情况（退回内置默认值），不能把 0 当成"这台机器没有内存"。
 */
struct Usage {
    qint64 totalMb = 0;     ///< 物理内存总量
    qint64 availableMb = 0; ///< 当前可用（能立刻复用、不必先写回磁盘的那部分）
};

/**
 * @brief 读一次系统内存。
 * @return 读不到就是 {0, 0}。
 */
Usage read();

}  // namespace SystemMemory
