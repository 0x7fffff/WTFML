/**
 * @file WorldFiles.hpp
 * @brief 世界（存档）目录上那几个**改盘**的动作：认它是不是一个世界、把它拷一份进来、改它的名字。
 * @author 0x7fffff
 * @date 2026-09-28
 * @note 为什么单独有这个文件：实例设置「存档」那一页里，顶上那颗「导入世界」和行尾那支笔
 *       （「编辑」）最后都要落到磁盘上，而界面层（ui/）的规矩是**只发信号、不碰盘**。把这些
 *       动作收在 src/manage 里还有第二个好处：验证工程可以绕开"点按钮 → 弹原生选择框"这条
 *       路（原生对话框在测试里驱动不了），直接把函数调起来，把"重名挡住没有""拷了一半失败
 *       会不会留半份"这些事钉死。
 *
 * 和 `VersionManager::copyInstance()` 的分工：那一个多出来的是一份**实例**（版本目录，
 * 除了目录本身还要把 jar/json 改成新名字、刷新 `instanceId`），这里多出来的是一份**世界**
 * —— `saves/` 底下的一个普通目录，除了目录名以外它没有别的身份（世界的真名写在
 * `level.dat` 里面，启动器和游戏都不靠目录名认它）。
 */
#pragma once

#include <QString>

namespace WorldFiles {

/**
 * @brief 这个目录像不像一个世界。
 * @param worldDir 待判定的目录。
 * @return 里面有 `level.dat` 就是 true。
 *
 * 「导入世界」用它判用户挑的那一格是不是世界。判据只有 `level.dat` 这一条：
 *   * 不判 `region/`、不判 `level.dat_old` —— 一个刚建好还没存过档的世界也有 level.dat，
 *     而 `region/` 是可以为空的；
 *   * **故意不去读 `level.dat` 里的世界名**：那是 NBT，得自己解压缩再解析，而它的内容
 *     和目录名不一致时该怎么办（以哪个为准）是个产品问题 —— 现在两处**都不读**它，
 *     一致性由"导入时用目录名"和"游戏自己按目录名找世界"保证。
 */
bool looksLikeWorld(const QString &worldDir);

/**
 * @brief 世界的目录名合不合法。
 * @param rawName 用户输入的名字。
 * @return 合法返回空串；否则返回给用户看的一句原因。
 *
 * NOTE: 走的是**实例目录名**那一套（`InstanceFiles::validateDirName`：不能有分隔符、
 *       不能是 Windows 保留设备名、不能以点结尾、不超过 64 个字符）。世界是普通的
 *       目录，本来只需要"Windows 建得出来"，但两边各写一套判据的结果是"某一类名字在
 *       改名这里过得了、在实例那里过不了"这种没人说得清的区别。**故意复用**。
 */
QString nameProblem(const QString &rawName);

/**
 * @brief 把一个世界目录拷成 `<savesDir>/<name>`。
 * @param worldDir 源世界目录（用户挑进来的那一格）。
 * @param savesDir 目的地：某个实例的 `saves` 目录（**必须先存在**）。
 * @param name 拷过去之后那个目录叫什么。
 * @param problemOut 非空时，失败的原因写进来（成功时清空）。
 *
 * @return 成功 true；失败 false 且 **盘上不会留下半份**（自己收拾掉，见下）。
 *
 * 规矩：
 *   * `worldDir` 里没有 `level.dat` → 拒绝（拿一整个 `.minecraft` 当世界拷进来过，
 *     结果是在 saves 底下多出一大坨东西，而且**不报错**）；
 *   * 名字先过 `nameProblem()`；目标**已经存在** → 拒绝（**绝不**合并、绝不覆盖 ——
 *     `QFile::copy()` 遇到同名文件返回 false，所以"合并"在这里根本做不成，与其半路
 *     失败不如一开始就挡住）；
 *   * 用的是 `copyTreeWithoutFollowingLinks()`：隐藏文件一个不落，链接（junction）
 *     直接判失败，**不会**把用户挂到别的盘上的东西抄一份过来。
 *
 * NOTE: 失败时**删掉自己那份半成品** —— 这一点和整合包导入那条路（"失败不删半成品"，
 *       好让用户接着下）**故意相反**：世界是几百 MB 的东西、不是几百个文件分着下的，
 *       没有"接着拷"这回事，留一半在 saves 里只会让游戏列表里多出一个打不开的世界。
 *       删不掉时返回的失败理由里会**附一句**"那一份也没能删掉"，不让用户以后自己撞上
 *       一个来路不明的目录还不知道是谁留的。
 */
bool copyWorldInto(const QString &worldDir, const QString &savesDir, const QString &name,
                   QString *problemOut = nullptr);

/**
 * @brief 把 `<某目录>/<老名字>` 改成同目录下的 `<新名字>`。
 * @param worldDir 现在这个世界的目录（含路径）。
 * @param newName 新名字。
 * @param problemOut 非空时，失败的原因写进来（成功时清空）。
 * @return 成功 true。
 *
 * 判据和 `VersionManager::renameInstance()` 那一路一致：名字过 `nameProblem()`、"新名字
 * 和现在一模一样"直接算成功（不报错、也不动盘）、**同目录下已经有同名**（不区分大小写）
 * 就拒绝。
 *
 * NOTE: 改名**只动目录名**，不碰 `level.dat` 里的任何东西。改完之后游戏的世界列表里
 *       也会跟着变 —— 1.13 起游戏列的就是目录名（旧的 `LevelName` 那一位已经不看了），
 *       所以"改个名"这件事在游戏那侧是**真的改名**，不是只改了个文件夹。
 */
bool renameWorld(const QString &worldDir, const QString &newName, QString *problemOut = nullptr);

}  // namespace WorldFiles
