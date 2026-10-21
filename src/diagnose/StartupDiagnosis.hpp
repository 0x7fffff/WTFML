/**
 * 【为什么要有这一层】
 *
 * coding plan §9.5「综合启动诊断报告」：实例设置里已有日志、崩溃报告、完整性检查三处
 * 入口，用户启动失败时得自己来回翻。这一层把**启动那一刻真正相关的几件事**收成一份
 * 报告：这个实例是什么版本 / 有没有加载器 / Java 指没指定 / 内存分了多少 / mods 里
 * 有没有东西 / 最近一次启动怎么退出的 / 最近一份崩溃报告说了什么。
 *
 * 它**只读、只算**：进一份 `Inputs`（其中几个是盘上路径），出一份 `Report`。界面怎么
 * 摆由 ui/ 决定，这一层不 include 任何 ui/、也不 include src/controllers。
 *
 * 【纪律：不把推测写成结论】
 *
 * 这一页的每一条都必须是**盘上真读得到的东西**（文件存在、日志里那一行、崩溃报告里
 * 那个异常名）。不做「多半是 XX 模组导致的」这种猜测 —— 模组冲突那一层（§9.5 另一条）
 * 也守着同一条。认不出来的崩溃就如实说「报告里没认出常见的那几类」，让用户点进崩溃
 * 报告自己看。
 *
 * 【读哪些盘】
 *
 * · `mods/`：数 `.jar` 与 `.jar.disabled`（**只看名字，不解 jar**）；
 * · `<数据目录>/logs/wtfml.log`：只读**末尾 512 KB** —— 这份每次启动重写，但一次
 * 几百个模组的启动也能写出几十 MB，从头读到尾是拿一次翻页换几秒白等；
 * · `<数据目录>/crash-reports/`：只有 `crash-*.txt`，取**最新一份**，只读开头 64 KB。
 *
 * 【自动内存那一档】
 *
 * `Inputs` 里那两个数值由调用方算好递进来（自动档该用 `suggestXmxMb()`，和真正启动
 * 那一刻用的是同一个纯函数）。这一层不自己调 `refreshSuggestedMemory()` —— 那个会
 * 落盘，而「诊断」这一页一个字节都不该写。
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace StartupDiagnosis {

enum class Severity {
    Problem,  ///< 会直接导致启动失败 / 明显不成立
    Warning,  ///< 能启动，但很可能出问题
    Info,     ///< 只是报告一个事实
    Ok,       ///< 查过，正常
};

QString severityName(Severity severity);

struct Finding {
    Severity severity = Severity::Info;
    QString title;   ///< 一句话说是什么
    QString detail;  ///< 证据（盘上原文 / 具体数字）
    QString hint;    ///< 怎么办（没得说就空着）
};

struct Inputs {
    // ---- 这个实例是谁 ----
    QString instanceName;
    QString versionId;        ///< 实例目录名（显示用）
    QString gameVersion;      ///< 真正的游戏版本（`realVersion`，拿不到时是 id）
    QString loaderName;       ///< 空 = 原版
    QString loaderVersion;    ///< 可空
    int requiredJavaMajor = -1;  ///< version.json 里 `javaVersion.majorVersion`；-1 = 不知道

    // ---- Java / 内存（有效值，不是全局那一份）----
    QString configuredJavaPath;  ///< 空 = 没预先指定（自动挑）
    bool autoSelectJava = true;
    int xmxMb = 0;
    int xmsMb = 0;
    bool autoMemory = true;      ///< true 时 xmx/xms 是"这次大约会用多少"
    qint64 systemTotalMb = 0;    ///< 0 = 读不到
    qint64 systemAvailableMb = 0;

    // ---- 盘上路径 ----
    QString modsDir;   ///< 空 = 这个实例没有 mods 目录
    QString logPath;   ///< `<数据目录>/logs/wtfml.log`
    QString crashDir;  ///< `<数据目录>/crash-reports`
};

struct Report {
    QList<Finding> findings;
    int problems = 0;
    int warnings = 0;
    /// 顶上第二行那句「实例：名字（版本 · 加载器）」，由 analyze() 填好。
    QString instanceLine;

    /// 顶上那一句：`启动诊断：发现 1 个问题、2 条提醒` / `启动诊断：没有发现明显问题`。
    QString headline() const;
    QString toText() const;
};

/// 跑一遍。**不写盘、不弹窗、不联网。**
Report analyze(const Inputs &inputs);

}  // namespace StartupDiagnosis
