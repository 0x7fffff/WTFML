/**
 * @file VersionDisplay.hpp
 * @brief 版本显示名的拼装规则：把一个版本的各个组成部分按开关拼成界面上那一串。
 * @author 0x7fffff
 * @date 2026-09-20
 * @note 纯函数，不碰磁盘、不碰 widget。磁盘上的元数据由
 *       VersionManager::displayParts() 读，配置由 LauncherSettings 存。
 */
#pragma once

#include <QString>

#include <optional>

/**
 * @brief 「版本名格式」的四个开关。
 *
 * NOTE: 这是**版本名格式**设置的数据模型，界面入口在设置页的「版本名格式」那一行 ——
 *       摆的正是四个勾选框（ui/MainWindowUI.cpp 的 versionNameCustomCheck/RealCheck/
 *       LoaderCheck/LoaderVersionCheck），改动经 MainWindow::onVersionNameOptionChanged()
 *       归一化后落盘。当初留的那条余地依然有效：这四个布尔也可以改摆成一个"预设档位"的
 *       下拉框、由它去反写这四个布尔 —— 模型是四个布尔，界面想怎么摆都行，别改模型去迁就界面。
 */
struct VersionNameOptions {
    bool showCustomName = true;
    bool showRealVersion = true;
    bool showLoaderName = true;
    // 用户明确要求这一项默认关闭
    bool showLoaderVersion = false;

    // 逐字段比较。setter 靠它判断"值没变就别写盘"，验证工程靠它断言归一化结果。
    friend bool operator==(const VersionNameOptions &, const VersionNameOptions &) = default;
};

/**
 * @brief 一个版本参与显示名的各个部分。
 *
 * customName / loaderName / loaderVersion 都是"有就显示、没有就跳过"的可选项，
 * 它们的来源见 VersionManager::displayParts()。
 */
struct VersionDisplayParts {
    /// 版本目录名，也就是真正进 --version 和文件路径的那个 id。永远非空。
    QString id;
    /// 这个实例实际是哪个版本，例如 "1.18.2"。**为空时退化成 id**。
    ///
    /// 为什么不能拿 id 当实际版本：用户的目录名早就把加载器揉进去了，真机上是
    /// `1.18.2-Forge_40.3.12`、`1.21.1-NeoForge_21.1.210`、`26.2-Fabric 0.19.5`
    /// 这种。往 loaderName 里填 "Forge" 就会拼出
    /// `我的整合包-1.18.2-Forge_40.3.12-Forge` —— 去重只对**完全相同**的串生效，
    /// 救不了 `Forge` 和 `Forge_40.3.12`。PCL 用 Setup.ini 里的 VersionVanillaName
    /// 这个独立字段解决同一个问题（`海波篮子` 的真实版本是 `26.2`）。
    ///
    /// 来源是 wtfml.meta 里可选的 "realVersion"。**读侧已经全部打通**：displayParts() 读它、
    /// 这里拿它当"实际版本"段，所以用户手写这个字段会**立刻**反映到界面上的显示名。
    /// 缺的是**写侧** —— 还没有任何界面或代码会写它（那个入口排在"版本重命名 / 实例管理"里），
    /// 所以今天的常态是这一项为空、由上面的 id 兜底。
    QString realVersion;
    /// 重命名后的自定义名。没重命名过就是空串。
    QString customName;
    /// 模组加载器名，例如 "Forge"。没检测到就是空串。
    QString loaderName;
    /// 模组加载器版本，例如 "47.2.0"。没检测到就是空串。
    QString loaderVersion;
    /// 预留：这个实例单独覆盖全局的显示格式，对应 wtfml.meta 里可选的 "nameFormat"。
    /// 为空表示"跟随全局默认"。这就是以后"分开实例管理时自行调整特定实例的名称展示"
    /// 的挂点——现在没有任何代码会填它，`displayParts()` 已经会解析它。
    std::optional<VersionNameOptions> nameFormatOverride;
};

/**
 * @brief 把开关修正到合法状态。
 * @param options 原始开关。
 * @return 修正后的开关。
 *
 * 两条不变式，都是"把从属的那一项关掉"：
 *   - 自定义名和版本名不能同时关（否则拼出来是空串）：兜底把**版本名**打开。
 *     没有自定义名时名称槽本来就退化成实际版本，所以这等于回到"显示版本号"最安全的状态。
 *   - 加载器版本的前提是加载器名：前提不在就把**加载器版本**关掉（它才是从属项）。
 *
 * 加载设置时、setter 里、以及 formatVersionName() 内部都要过一遍。最后那道是必须的：
 * 前两道都可能被绕过（手改注册表、以后的新调用方），而"拼出空串"的后果是界面上一块空白。
 */
VersionNameOptions normalizeVersionNameOptions(VersionNameOptions options);

/**
 * @brief 按开关把一个版本拼成显示名。
 * @param parts 版本的各个组成部分。
 * @param globalDefault 全局默认开关；`parts.nameFormatOverride` 有值时以它为准。
 * @return 显示名，`-` 连接的若干段。
 *
 * 顺序固定是：自定义名槽 → 实际版本 → 加载器名 → 加载器版本。规则：
 *   - 名称槽在没有自定义名时**退化成实际版本**（用户要求：只勾了"自定义名"而没重命名过时，
 *     显示的就是版本号本身，也就是改动前的样子）；
 *   - "实际版本"指的是 `parts.realVersion`，为空时退化成 `parts.id`（目录名）。
 *     `id` 不会作为**独立的一段**出现，它只在上面这处兜底时被顶上来当实际版本用；
 *     而它真正的身份是进 --version 和文件路径的那个值，可以长得完全不像版本号
 *     （真机上有 `原版ATT2地图（无模组）` 这种）；
 *   - 文本重复的段跳过，所以"名称槽退化成版本号"不会拼出 `1.20.1-1.20.1`；
 *   - 空串段跳过，所以没加载器时不会留下多余的分隔符。
 *
 * NOTE: 已知且**刻意保留**的一个输出：`我的整合包-Forge`（自定义名 + 加载器名、关掉实际版本）。
 *       它看着有点怪，但两个"修掉"它的办法都更糟——静默压掉加载器违反开关语义；把版本名
 *       强行打开是隐式耦合，以后设置界面没法解释"我只勾了加载器，怎么版本也亮了"。别"修"它。
 *
 * NOTE: 输入按不可信数据清洗（去首尾空白、内部换行/制表符折成空格、长度封顶）。wtfml.meta
 *       的意义就是让用户手写，而一个含 '\n' 的名字会把"启动游戏"按钮的两行文字撑成三行。
 */
QString formatVersionName(const VersionDisplayParts &parts, const VersionNameOptions &globalDefault);
