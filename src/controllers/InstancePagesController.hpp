/**
 * @file InstancePagesController.hpp
 * @brief 「实例设置」那几页**要什么事实**：哪几页用得上、那几页指向哪个目录、有结构的存档是哪几个。
 *
 * 【为什么要有这一层】
 *
 * `MainWindow::openInstanceSettingsNow()` 里那个 `refreshInstanceDirs` lambda 是这条路上
 * **判断最密**的一处（一百来行）：八个文件页指向哪个目录、左栏那一列该露哪几页、
 * 「结构/原理图」那一页顶上那两排「来源」摆什么……而它夹在一堆 `dialog.setXxx()` 中间，
 * 判断和数据摆在一块儿，谁也验不了（要验就得开一扇真的实例设置页）。
 *
 * 这一层把**判断**收过来，`MainWindow` 那边只剩"把这几格事实摆到对话框上"。
 *
 * 【边界（谁在这一层里，谁不在）】
 *
 * · 在：**页键 → 目录**那张表（`ResourceCatalog::folderForKind()` 是目录名的唯一出处，
 * 这一层是它的唯一读者；仓里那些调用点全都转发到这儿）。
 * · 在：**六条可用性判据** —— 模组（`instanceLoaderName()` 认得出来就算、`mods/` 里有 jar
 * 也算）、光影包（加载器**就是** OptiFine、或者 mods/ 里装着 iris / oculus / optifine）、
 * 结构/原理图（装了 Litematica **或**这个实例真有结构）、数据包（版本认不认）。
 * 这几条都是"只有业务层够得着"的：ui/ 不 include src/、也读不了版本 json。
 * · 在：**文件名切词命中**（`jarNameHasToken()`）—— 「装没装那个模组」的唯一出处。
 * 按前缀认不行：中文整合包和 PCL 会给 jar 前面挂一个中文名
 * （`[投影] litematica-….jar`），于是「光影包」「原理图」那两页谎报"用不上"。
 * · 在：**有结构的那些存档**（规矩见 `worldStructures()`）。它要读盘、要认"结构方块存下来的
 * 东西落在哪"，ui/ 一样都够不着。
 * · 不在：**中文与排头**。「存档」/「投影」那几个字是 `ui/InstanceFilesPage` 上的静态
 * 函数（唯一出处），这一层只说"哪几个存档有结构"，不说那两排叫什么。
 * · 不在：**摆控件**。`dialog.setModUsable(...)` 那一串留在 `MainWindow` —— 那正是
 * "灌数据"，不是判断。
 * · 不在：**服务器列表 / 图标反查 / 更新结果回灌**那三件事（`loadServerList` /
 * `requestInstanceFileIcons` / `pushInstanceFileUpdates`）：它们要么收一个对话框指针、
 * 要么起后台线程，是**动作**不是事实。
 */

#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

class VersionManager;
struct VersionDisplayParts;

class InstancePagesController final {
public:
    /// 一个实例里**有结构的那些存档** —— 名字和"那些 `.nbt` 在哪个目录"两张**平行**的表。
    struct WorldStructures {
        QStringList labels;
        QStringList paths;
    };

    /**
     * @brief 这个实例上那四页"用不用得上"。
     *
     * `worlds` / `litematica` 是**顺手带出来**的，不是判据的一部分：`schematic` 要拿它们算，
     * 而「结构/原理图」那一页顶上那两排「来源」也要拿它们摆 —— 一次算好，省得为了同一份
     * 事实再扫一遍盘（`worldStructures()` 每趟都要走遍所有存档目录）。
     */
    struct Usability {
        /// 「模组」那一页。
        bool mods = false;
        /// 「光影包」那一页。
        bool shader = false;
        /// 「结构/原理图」那一页（装了投影 **或**这个实例真有结构）。
        bool schematic = false;
        /// 「数据包」那一页。
        bool datapack = false;
        /// 这个实例里那几张"有结构的存档"表（可能两张都是空的）。
        WorldStructures worlds;
        /// mods/ 里装没装 Litematica（摆「投影」那一排要看它）。
        bool litematica = false;
    };

    /**
     * @brief **页键 → 目录**。那九项是 MC 自己的约定，目录名的出处是
     * `ResourceCatalog::folderForKind()`（能下载的那四类）和这一层的字面量（另外五样）。
     *
     * **唯一出处**。`MainWindow::instanceFileDirs()` 只是转发 —— 仓里有十来个读者
     * （落点、配置备份、存档页、图标反查、实例工具入口……），各写一遍的话，哪天目录名
     * 改了就会有一半人看着旧目录。
     */
    static QHash<QString, QString> fileDirs(const QString &versionId);

    /**
     * @brief 十三条「常用目录」里**现在真有东西**的那些（键序跟着 @p shortcutKeys 走）。
     *
     * 判据只有两条（要求「仅在**目录存在**或**相关模组可识别**时优先展示」）：
     * 目录存在；「录像」那一条额外认"装了 ReplayMod"（没录过的实例上那个目录往往还没建）。
     *
     * @param dirs `fileDirs()` 的结果**再并上那三个快捷项**（`game` / `replay` / `crash`）——
     * 要按"现在桌上摆着哪几条"来判断，就得看得见它们的路径。
     * @param shortcutKeys 界面那一列现在有哪几条（`InstanceSettingsDialog::shortcutPathKeys()`）。
     * 按参数收而不是在这儿写死：**有哪几条是界面的事**（这一层只说"哪几条有东西"），
     * 而且顺序就是屏幕上的次序。
     */
    static QStringList commonFolderKeys(const QString &versionId,
                                        const QHash<QString, QString> &dirs,
                                        const QStringList &shortcutKeys);

    /**
     * @brief 这个实例里**有结构的那些存档**（见 `ui/InstanceFilesPage::setStructureSources()`）。
     *
     * NOTE: 判据是"有没有"。这条路上**每一次**开实例设置都要跑（每个存档一次），所以
     * 一找到第一条 `.nbt` 就停 —— 一个存档存过几百次结构的话，把整棵子树走完是白走的。
     */
    static WorldStructures worldStructures(const QString &versionId);

    /// `Usability` 那四格 + 顺手带出来的那三格（见 `Usability` 的说明）。
    ///
    /// NOTE: 收**非 const** 的 `VersionManager *` 是因为 `instanceSupportsDatapacks()`
    /// 那一格本身就不是 const 的（它要读合并后的 json，见它自己的说明）。
    static Usability usability(VersionManager *versions, const QString &versionId);

    /**
     * @brief 这个实例的加载器**叫什么**（显示名：「Fabric」/「Forge」/「NeoForge」/「Quilt」/
     * 「OptiFine」），认不出来回空串。
     *
     * 先看 @p parts 里**已经读过**的那一格（`VersionManager::displayParts()` 顺手带出来的，
     * 不额外读盘），为空时再回扣一次 `VersionManager::instanceModLoader()`。
     *
     * 为什么要回扣：`wtfml.meta` 里的 `loaderName` **只有我们自己的安装器会写**。PCL /
     * HMCL 那类把整份 json 合进实例目录的实例（用户盘上 70 个里 67 个）那一格是空的 ——
     * 回扣那一趟照着版本 json 的 `libraries` 认，才是它们的加载器唯一的出处。
     *
     * **唯一出处**。`MainWindow` 那边那个同名自由函数只是转发 —— 仓里有十来个读者
     * （实例行那个标志 defaultInstanceMark、自动归类 defaultGroupIdFor、光影包那一页、
     * 装模组时的"加载器对不上"那道闸、快捷启动的目标、下载页那几处），各写一遍的话，
     * PCL 那些实例会在其中几处凭空变成"没有加载器"。
     *
     * @param versionOut 非空时填进加载器的**版本**（和名字同一次读取的两半）。
     */
    static QString loaderName(const VersionDisplayParts &parts, const VersionManager *versions,
                              QString *versionOut = nullptr);

    /// 这个实例的 mods/ 里有没有名字里带 @p nameToken 的 jar（见 `jarNameHasToken()`）。
    ///
    /// 只认 `.jar`：**被禁用的那些现在叫 `.jar.disabled`**（见 `identityPath()` 那段），
    /// 扫不到 —— 那正是"禁用"的意思。
    static bool hasMod(const QString &versionId, const QString &nameToken);

    /// 「模组」那一页用不用得上：**有一个模组加载器**，**或者** mods/ 里已经有 jar。
    ///
    /// NOTE: 不是 `displayParts().loaderName` 非空 —— 那一格只有我们自己的安装器会写，
    /// 用户盘上 70 个实例里 67 个（全是 PCL 装的）会被误判成"没有模组加载器"。
    /// ⇒ **OptiFine 也算加载器**，装 OptiFine 的实例会多一条「模组」。这是有意的：
    /// OptiFine 从 1.13 起就是塞进 mods/ 的，那一页在那样的实例上真的用得上。
    static bool modsUsable(VersionManager *versions, const QString &versionId);

    /// 「光影包」那一页用不用得上：加载器**就是** OptiFine（它自己就认 `shaderpacks/`），
    /// 或者 mods/ 里装了一个加载光影的模组（iris / oculus / optifine）。
    ///
    /// NOTE: 那三个名字**都是用户盘上真见过的**：`iris`（Fabric/NeoForge/Quilt 上那个）、
    /// `oculus`（Forge 上那个 Iris 移植 —— 三个整合包用的都是它）、`optifine`
    /// （1.12.2 及以前 OptiFine 是**当普通模组装进 mods/ 下**的）。
    /// 没有 `optifabric`：它要求 OptiFine 的 jar 也在 mods/ 里，上面那条本来就盖住了它。
    static bool shaderUsable(VersionManager *versions, const QString &versionId);

    /**
     * @brief 文件名切出来的"词"里有没有 @p token（忽略大小写）。**忽略大小写、按整段词认。**
     *
     * 切词规则：**只有 ASCII 的字母数字算"词里的字"**，别的一律当分隔符 —— `-` `_` `+` `.`
     * 空格、方括号，以及**中日韩字**（那一档是刻意的：`虹膜iris-….jar` 这种一个分隔符都不带
     * 的写法也切得出来，中文不会粘住后面的词）。
     * 命中判据两条：词**等于**它，或者词**以它开头**（`irisfly.jar` / `litematicafoo.jar`
     * 这种粘连写法照旧算 —— 和前缀语义对齐，不多不少）。
     */
    static bool jarNameHasToken(const QString &fileName, const QString &token);
};
