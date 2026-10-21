/**
 * @file TransferPlanner.hpp
 * @brief 跨实例迁移 / 版本升级副本的**只读计划层**。
 *
 * 这一层只回答两件事：
 * ① 从这份实例搬到那一份，**能搬什么、默认该搬什么、哪些是风险**；
 * ② 目标版本 / 加载器和源不一致时，**如实说不一样**（并因此不默认勾选模组），
 * 而不是替用户猜"应该没问题"。
 *
 * 判据层不写盘：`plan()` 只读盘（目录项 + 文件大小），一个字节都不改。真正拷文件的是
 * `TransferApply`。这么切是为了两件事都能单独验：计划可以在没有任何写入的情况下量，
 * 而拷的那一半只需要一份计划当输入。
 *
 * 为什么"日志 / 截图 / 崩溃报告"写死默认不选：它们是**记录**，搬过去只是多占地方，
 * 而且日志里的时间戳在另一份实例上还会让人误以为那是目标实例自己的历史。用户仍然可以
 * 手动勾上（这一层只是不给默认值，不是拒绝）。
 *
 * 版本兼容判据只认盘上写着的事实：`realVersion` / `loaderName` / `loaderVersion`
 * （`VersionDisplayParts` 那几格）。认不出来就是「认不出来」—— 不拿版本号前缀、
 * 不拿发布年份去猜。
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace TransferPlanner {

/// 源与目标之间的兼容结论。**顺序有意义**：`Same` 是唯一"模组默认勾选"的那一档。
enum class Compatibility {
    Same,               ///< 游戏版本 + 加载器一致（加载器小版本不同不改变这一档，只在细节里说）
    GameVersionDiffers, ///< 游戏版本不同
    LoaderDiffers,      ///< 游戏版本相同、加载器不同（含「原版 vs 有加载器」）
    BothDiffer,         ///< 两边都不同
    Unknown,            ///< 有一边读不出游戏版本或加载器
};

QString compatibilityName(Compatibility compatibility);

/// 一句话说清"为什么是这个结论"（完整句子，界面直接用）。
QString compatibilityText(Compatibility compatibility, const QString &sourceGameVersion,
                          const QString &sourceLoaderName, const QString &targetGameVersion,
                          const QString &targetLoaderName);

/// 迁移范围里的一项（一块内容）。键是稳定字符串，界面按它记勾选状态。
struct Item {
    QString key;          ///< 稳定键（`saves` / `mods` / `config` / ...），界面与落盘判据都认它
    QString label;        ///< 用户看到的名字（「存档」「模组」……）
    QString relativePath; ///< 相对实例目录的路径（目录或单文件）
    bool isDirectory = true;
    bool exists = false;  ///< 盘上真有这块东西吗（不存在的不列勾选框，只报「没有」）
    bool defaultSelected = false;
    qint64 bytes = 0;     ///< 这块东西一共多少字节（只数，不读内容；目录不跟链接）
    int fileCount = 0;    ///< 这块东西一共有几个文件
    QString note;         ///< 为什么默认勾 / 不勾（一句话，空 = 不用解释）
};

struct Inputs {
    QString sourceDir;  ///< 源实例目录（`AppPaths::versionDir(<源 id>)`）
    /// 源实例在盘上写着的事实（`VersionDisplayParts`）。空串 = 认不出来。
    QString sourceGameVersion;
    QString sourceLoaderName;
    QString sourceLoaderVersion;
    /// 目标实例 / 目标版本在同一份盘上写着的事实。迁移时是目标实例，升级时是目标版本。
    QString targetGameVersion;
    QString targetLoaderName;
    QString targetLoaderVersion;
    bool upgrade = false;
};

struct Plan {
    bool ok = false;
    QString problem;  ///< `ok == false` 时给用户看的一句话
    Compatibility compatibility = Compatibility::Unknown;
    QString compatibilitySentence;
    QList<Item> items;
    bool upgrade = false;

    /// 顶上那一句：「迁移计划：目标与源一致，模组与配置可以原样搬过去 · 6 项 · 1.2 GB」。
    QString headline() const;
    /// 整页给人看的文本（标题 + 兼容性 + 每项一行 + 默认不选的说明）。
    QString detailText() const;
    /// 默认勾选的那些键（**只含盘上真有的项**）。
    QStringList defaultSelectedKeys() const;
    /// 按 @p keys 算一共多少文件 / 多少字节（不存在的项不算）。
    int selectedFileCount(const QStringList &keys) const;
    qint64 selectedBytes(const QStringList &keys) const;
    /// 真动手之前给用户看的那一句确认（「将把 3 项、共 214 个文件（1.2 GB）…」）。
    QString confirmText(const QStringList &keys) const;
};

/**
 * @brief 按 @p inputs 扫一遍源实例目录，得出一份计划。
 *
 * 只读盘，不写。源目录不存在时 `ok == false`（`problem` 说的是哪一条路径）。
 * 各块的默认勾选：
 * - 存档 / 配置 / 默认配置 / 资源包 / 光影包 / 游戏设置 / 服务器列表：有就默认勾；
 * - 模组：**只有兼容结论是 `Same` 才默认勾**；
 * - 截图 / 日志 / 崩溃报告：**永不默认勾**（仍然列出来，用户可以手动选）。
 */
Plan plan(const Inputs &inputs);

/// 这一块是不是"永远默认不选"的那一类（界面别另写一份判据）。
bool neverDefaultSelected(const QString &key);

}  // namespace TransferPlanner
