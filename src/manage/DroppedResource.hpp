/**
 * @file DroppedResource.hpp
 * @brief 用户把一个文件**拖进窗口**时，"这到底是哪一类东西"的唯一出处。
 *
 * 要求：「打开某个实例设置的时候如果拖入.jar/.zip之类的文件，检测是整合包还是资源包等」。
 * 「检测」这件事本身是**纯判断**：不弹窗、不落盘、不复制，读一眼文件（必要时读一眼压缩包的
 * 条目表）就答一句。所以它在 `src/manage/` 里单独一个模块，界面和数据层两边都用它 ——
 * 判据只留一处，别在 MainWindow 里再写一遍"后缀是 .jar 就是模组"。
 *
 * 【为什么不另立一个 Kind 枚举】
 * 认出来之后要问的三件事 —— 这个东西**叫什么**（那句话里那个词）、它**装进哪个目录**、
 * 下载页那一栏是哪一个 —— 全仓已经各只有一处（`ResourceCatalog::kindLabel()` /
 * `folderForKind()` / `kindKey()`）。再抄一份枚举，就是给"两处说法不一致"开路：
 * 用户会看见"将在此实例中安装 光影包"而目录写进了 `shaderpacks/`。
 * 所以这里直接用 `ResourceCatalog::Kind`，**多出来的那一档只有"认不出来"**。
 */

#pragma once

#include <QString>

#include "../download/ResourceCatalog.hpp"

namespace DroppedResource {

/// 一次判断的结果。
struct Result {
    /// 认出来了吗。**false 时 `kind` 那一格没有意义**（"不知道这是什么"）。
    bool known = false;
    /// 认出来是哪一类。见文件头那段：就是下载页那一套 Kind。
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    /// 用户拖进来那份文件的路径（**原样**：不 cleanPath、不改大小写、不解析链接）。
    QString path;
    /// 判据本身（"里面有一份 modrinth.index.json" / "打不开它"），给日志、给说话时的括号。
    /// **它不是给用户看的主句** —— 主句由调用方按 `kindLabel(kind)` 拼。
    QString detail;
};

/**
 * @brief 看一眼 @p path，说出它是哪一类。
 *
 * 判据（按这个次序问，**次序是有意义的**）：
 * 1. 后缀 `.jar` ⇒ **模组**（连盘都不读：一个 jar 里有没有 mods.toml 不归这一层管，
 * 游戏和加载器自己会判；而"jar 装的整合包"这种事不存在 —— 整合包是 zip）；
 * 2. 压缩包（`.zip` / `.mrpack`）里**根目录有** `modrinth.index.json` 或 `manifest.json`
 * ⇒ **整合包**（那两个文件名和 `ModpackImport` 同一个出处，不在这儿再抄一遍）；
 * 3. 有 `pack.mcmeta`：里面还有 `assets/` ⇒ **资源包**；还有 `data/` ⇒ **数据包**；
 * 4. 里面有 `shaders/` ⇒ **光影**（光影包没有 pack.mcmeta，这条是它唯一的脸）；
 * 5. 剩下的一律**认不出来**（`known == false`），`detail` 里写清楚是为什么。
 *
 * 第 3 步排在 4 前面是**必须的**：资源包里常见 `assets/.../shaders/`（核心着色器），
 * 反过来的话会把资源包认成光影。
 *
 * NOTE: 这一层**不联网、不落盘**，唯一的重活是压缩包那一趟 `ZipUtil::listEntries()`
 * （另起一个 PowerShell 进程，只读条目名、不解压任何东西）。所以它可以在
 * dropEvent 里同步调 —— 但它**不是**"随便调"的：别放进每秒都要跑的循环里。
 */
Result classify(const QString &path);

}  // namespace DroppedResource
