/**
 * @file OfflineSkin.hpp
 * @brief 离线账号**自己挑的那张皮肤**：存在哪、模型记在哪、怎么换回来。
 * @note 这是「离线皮肤的『换一张』入口」那一格的落点（数据层）。
 * 图本身不联网、也不进资源：它就是用户从盘上挑的一张 PNG，本模块只负责**按账号存一份**
 * 并记住模型（经典 / 纤细）。
 *
 * 【为什么按账号存，而不是记一个路径】
 *
 * 记路径的话，用户把原图挪走、删掉、或者换台机器，这条记录就指着一张不存在的图；而这里的图
 * 是**一份装饰**，坏了就只该退回默认外观，不该让启动器记住一个永远读不出来的东西。所以挑完
 * 就**复制**进 `<skinsDir>/manual-<账号键>.png`（和皮肤缓存同一个目录）。
 *
 * 【为什么模型要单独记一行】
 *
 * 皮肤图里**没有**"经典还是纤细"这一位（官方档案里模型是和皮肤地址并列的另一个字段）。
 * 同样一份 64×64 像素画，按经典画是四条边一样粗的手臂，按纤细画是 3 像素宽的那种 ——
 * 从像素里推不出来。所以挑的时候把下拉框那一档记进 `<skinsDir>/manual-<账号键>.txt`，
 * 读不到 / 读不懂就当经典（那是绝大多数皮肤）。
 *
 * 【和 PlayerSkinStore / DefaultSkin 的先后】
 *
 * 离线账号那张脸的取舍在 `PlayerSkinController::reload()`：**手动挑的那张最优先**（用户
 * 明确说了要哪一张），其次才是照游戏规矩算出来的官方默认皮肤，最后是内置 Steve。本模块只回答
 * "有没有手动挑过、是哪张、哪个模型"，不参与那个次序。
 */

#pragma once

#include <QImage>
#include <QString>

namespace OfflineSkin {

/// 手动挑的那张图存在哪：`<skinsDir>/manual-<账号键>.png`。
/// 账号 uuid 认不出来时返回**空串**（不拼一个野路径 —— 和 PlayerSkinStore 同一条规矩）。
QString skinPathFor(const QString &uuid);

/// 模型那一行存在哪：`<skinsDir>/manual-<账号键>.txt`（内容就是 `classic` / `slim`）。
QString modelPathFor(const QString &uuid);

/// 这个账号手动挑过一张吗（只看文件在不在，不验内容 —— 读不出来由显示那一层退回默认外观）。
bool hasSkin(const QString &uuid);

/// 读记下来的模型：`slim` 或 `classic`。没有记录 / 读不懂 / uuid 认不出来 ⇒ `classic`。
QString loadModel(const QString &uuid);

/// 把一张已经验过尺寸的皮肤存到这个账号名下（写 PNG + 模型那一行）。
/// @param variant `slim` / `classic`（别的值按 `classic` 记）。
/// @param errorOut 失败原因（给弹窗用）。
/// @return 图**真的落盘了**才返回 true。模型那一行写不进去不算失败（下次按经典显示）。
bool save(const QString &uuid, const QImage &image, const QString &variant,
          QString *errorOut = nullptr);

/// 把手动挑的那张撤掉（两张文件都删），下次回到"算出来的 / 内置 Steve"那一档。
/// @return 收尾之后确实**什么都没有**了才返回 true（删不掉时报 false，让调用方弹窗）。
bool clear(const QString &uuid);

}  // namespace OfflineSkin
