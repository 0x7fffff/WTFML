# assets/loader —— 四个模组加载器的标志

这里是 `ui/VersionIcons.cpp::loaderMark()` 画的四个加载器的**真图**。用户 2026-09-28：
「把模组加载器的贴图换了」；再往前 2026-09-26 那句是「使用实际的那几个 mc 草块 / fabric
布料等的图片文件，不用担心侵权问题」—— 所以这四个不再是照着品牌色**自己画**的简化图形，
而是各家自己的图（那几个方块仍然是画的，见 `VersionIcons.hpp` 里那段说明）。

四张都是 **64 × 64** 的成品图（底色、圆角、留白都已经烤进去了）：界面那边只做等比缩放，
不再往上叠底色。之所以烤进来而不是运行时拼，是因为**最终长什么样在生成这一步就看得见**
（拼法见下面各自那一行），运行时那段代码因此只剩"读一张图、缩到要的边长"。

| 文件 | 是哪张图 | 怎么来的 |
| --- | --- | --- |
| `fabric.png` | Fabric 那个**米色织纹叶子**（官方现行标志） | 官方 `fabric-installer-1.1.2.jar` 里的 `icon.png`（512×512，透明底），按不透明部分居中缩到 78%，垫一块 `#38342A` 的圆角底 —— 那个色是从这张图自己的深色描边里取的（最多的一档，34816 格） |
| `forge.png` | Forge 那个**铁砧** | 官方 `forge-…-installer.jar` 里的 `big_logo.png`（256×64，铁砧在左边 x=2..80、y=13..55），裁出来缩到 82%，垫一块 `#F0A13C` 的圆角底（Forge 那个橙，和原来手画那张同一个色） |
| `neoforge.png` | NeoForge 那只**狐狸脑袋** | 官方 `neoforge-…-installer.jar` 里的 `icons/neoforged_background_128x128.png`，按不透明部分裁掉留白再缩到满格 |
| `optifine.png` | OptiFine 那个 **OF** | 官网 `https://optifine.net/images/of16r.png`（16×16，就是它那个 favicon），×4 最近邻放大到 64 |

圆角半径统一是边长的 3/16（64 px 那张就是 12 px）—— 和这四张图原来那套手画的"
圆角底 + 标志"一个半径，换图前后在列表里看着是同一档东西。

**侵权**：这四张是各家的标志，用户明确说「不用担心侵权问题」。要换成别的图：直接盖掉
同名文件就行，键（`fabric` / `forge` / `neoforge` / `optifine`）和
`VersionIcons::iconCandidateKeys()` 一个字都不用改 —— 那几个键**已经写进了实例的
`wtfml.meta`**（是稳定身份，见 `VersionIcons.hpp`）。
