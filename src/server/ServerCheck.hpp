/**
 * @file ServerCheck.hpp
 * @brief 服务端的「**体检**」那一层（coding plan §6.3 / 里程碑 M4-4）：把手上关于一个服务端的
 *        全部事实喂进来，回一张"哪几件事不对劲、各自该怎么办"的单子。
 * @author 0x7fffff
 * @date 2026-10-13
 *
 * 【它是干什么的（以及和「修复」那颗按钮的关系）】
 *
 * M4-1 起那一行上就有一颗「修复」（= 按我们记的那一档**重装一遍**）。但"重装一遍"是个**能
 * 治很多病、也治不了很多病**的粗活：端口被别的程序占着它治不了，`server.properties` 里
 * `view-distance=abc` 它更治不了（那不是装出来的，是用户手写的）。所以在那颗按钮之前先来
 * 一遍**体检**：把"这一个服务端现在到底哪儿不对"一条一条列出来，每条后面跟着那一条自己的
 * 出路（重装 / 去把值改对 / 去同意 EULA / 开目录看一眼）。
 *
 * 【它为什么是纯函数】
 *
 * 这一层**自己不碰盘、不看时钟、不查端口**：`Facts` 里每一个 bool 都是调用方（`MainWindow`
 * 那一段）现查出来喂进来的。这么切的理由和 `ServerModel` / `ServerModel::Properties` 一样：
 * "哪几条该报、按什么次序报、每一条用哪句话"这件事是**规格**，规格要能被逐条念出来。真去
 * 读盘（`ServerRegistry`）、真去占端口（`ServerPortCheck`）都在外面。
 *
 * 【报的次序】
 *
 * 从**拦着它起不来**的往下排（没装好 → 记的那一份说不清 → EULA 没同意 → 值不合法 → 端口
 * 被占），然后是提醒（重复的键、备份一份都没有），最后是说明（世界还没生成、文件里有别的
 * 键）。界面上照着这个次序摆，颜色由 `Severity` 决定 —— 最要紧的那条永远在最上面。
 *
 * NOTE: 这一层**只报事实**，一句话都不重写界面上的措辞（`Severity` 那三个词在
 *       `severityName()` 里，唯一出处）。
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "server/ServerPortCheck.hpp"
#include "server/ServerPropertiesForm.hpp"

namespace ServerCheck {

/// 这一条有多要紧（决定界面上那一行的颜色）。
enum class Severity {
    Problem,  ///< 这样下去起不来 / 起起来是坏的（红）
    Warning,  ///< 能起，但多半不是你想要的（黄）
    Info,     ///< 只是告诉你一声（灰）
};

inline QString severityName(Severity severity) {
    switch (severity) {
    case Severity::Problem:
        return QStringLiteral("要处理");
    case Severity::Warning:
        return QStringLiteral("提醒");
    case Severity::Info:
        return QStringLiteral("说明");
    }
    return QString();
}

/// 这一条对应的出路。`None` = 只能告诉你一声（这一层不编一个"按一下就好了"的动作出来）。
enum class Action {
    None = 0,
    Reinstall,     ///< 按我们记的那一档重装一遍（入口就在行尾那颗「修复」上）
    AcceptEula,    ///< 走一遍 EULA 那道闸（勾选 + 写 eula.txt）
    FixValues,     ///< 把不合法的值改回缺省（改完在「常用设置」那一页能看见）
    OpenSettings,  ///< 去「常用设置」那一页自己改
    OpenBackups,   ///< 去「世界备份」那一页
};

/// 出路摆在界面上的那个**记号**（`ui/` 看不见这个枚举，只认字符串）。
/// 认人认记号、不认按钮上那行字（同服务端行尾那颗按钮的规矩）。
inline QString actionToken(Action action) {
    switch (action) {
    case Action::None:
        return QString();
    case Action::Reinstall:
        return QStringLiteral("reinstall");
    case Action::AcceptEula:
        return QStringLiteral("accept-eula");
    case Action::FixValues:
        return QStringLiteral("fix-values");
    case Action::OpenSettings:
        return QStringLiteral("open-settings");
    case Action::OpenBackups:
        return QStringLiteral("open-backups");
    }
    return QString();
}

/// 体检单上的一条。
struct Finding {
    Severity severity = Severity::Info;
    QString title;   ///< 那一行的大字（一句话说清是什么事）
    QString detail;  ///< 下面那行小字（为什么、怎么办；可以多行）
    Action action = Action::None;
};

/**
 * @brief 体检的原料 —— **全是调用方现查出来的事实**（这一层一个都不自己查）。
 *
 * NOTE: 每一个字段都有缺省值，且缺省值一律是"最无害的那一档"（比如 `metaKnown = false` 谁也
 *       不冤枉、`portState = Unknown` 不报"被占"）。所以调用方**只填自己查得到的那几格**也
 *       是对的：查不到的，这一层就当"没这回事"，绝不猜。
 */
struct Facts {
    /// 装好了没有（判据是 `ServerRegistry::Entry::installed`，就是"入口文件在不在"）。
    bool installed = false;
    /// 没装好时**缺什么**（`Entry::note` 原话，照抄）。
    QString installedNote;
    /// 我们记的那一份（`.wtfml-server`）读得动吗。用户从外面拖进来的服务端就是 false。
    bool metaKnown = false;
    /// 记的那一份够不够拼出启动命令（`ServerModel::hasLaunchableInfo()`）。
    bool launchable = false;
    /// 不够时那句话（`serverLaunchPlan()` 的 `problem`，照抄）。
    QString launchProblem;
    /// `eula.txt` 里写着接受了吗（`ServerRegistry::readEula()`）。
    bool eulaAccepted = false;
    /// 现在在跑吗（在跑的时候有几条要跳过 —— 端口那一项尤其：它自己就占着）。
    bool running = false;
    /// `server.properties` 里读出来的端口；`-1` = 那一项不合法（已经会以 `valueProblems` 里
    /// 那一条报出来，这一层不再重复报一次"端口不对"）。
    int port = -1;
    /// 那个端口现在有人占着吗（`ServerPortCheck::stateOf()` 的结论）。
    ServerPortCheck::State portState = ServerPortCheck::State::Unknown;
    /// `level-name` 指的那个目录名（`ServerPropertiesForm::worldDirName()`；空 = 那一项不合法）。
    QString worldName;
    /// 那个目录在不在。
    bool worldExists = false;
    /// 那个目录有多大（说明用；0 = 空着或没查）。
    qint64 worldBytes = 0;
    /// 模组 / 插件目录里有多少个 jar（说明用）。
    int modCount = 0;
    /// 这个世界的备份有几份（空 = 一份都没有）。
    int worldBackupCount = 0;
    /// `server.properties` 里值不合法的那几项（`ServerPropertiesForm::problemsIn()`）。
    QList<ServerPropertiesForm::ValueProblem> valueProblems;
    /// 同一个键写了两遍、值还不一样（`duplicateKeys()`）。
    QStringList duplicateKeys;
    /// 文件里我们认不出来的键（`unknownKeys()`）。**不是错**，只是说一声。
    QStringList unknownKeys;
};

/// 体检。**次序就是界面上从上到下的次序**（见文件头那段）。
QList<Finding> inspect(const Facts &facts);

/// 一项都没查出来时摆什么（别摆一张空表 —— 用户会以为这一页坏了）。
QString allGoodText();

/// 顶上那句总括：`2 项要处理 · 1 项提醒 · 3 项说明`（没有的那几档不出现）。
QString summaryText(const QList<Finding> &findings);

// ---------------------------------------------------------------------------
// 落点（inline 实现；这一层不碰盘，所以整个头都是纯的）
// ---------------------------------------------------------------------------

inline QString allGoodText() {
    return QStringLiteral("没有发现问题 —— 入口文件齐、配置文件的值都对、端口也没人占。");
}

inline QString summaryText(const QList<Finding> &findings) {
    if (findings.isEmpty()) {
        return allGoodText();
    }
    int problems = 0;
    int warnings = 0;
    int infos = 0;
    for (const Finding &finding : findings) {
        switch (finding.severity) {
        case Severity::Problem:
            ++problems;
            break;
        case Severity::Warning:
            ++warnings;
            break;
        case Severity::Info:
            ++infos;
            break;
        }
    }
    QStringList parts;
    if (problems > 0) {
        parts.append(QStringLiteral("%1 项要处理").arg(problems));
    }
    if (warnings > 0) {
        parts.append(QStringLiteral("%1 项提醒").arg(warnings));
    }
    if (infos > 0) {
        parts.append(QStringLiteral("%1 项说明").arg(infos));
    }
    return parts.join(QStringLiteral(" · "));
}

inline QList<Finding> inspect(const Facts &facts) {
    QList<Finding> findings;

    // ---------- ① 拦着它起不来的那几件（次序见文件头）----------
    if (!facts.installed) {
        QString detail = facts.installedNote;
        if (detail.isEmpty()) {
            detail = QStringLiteral("入口文件不在这个目录里。");
        }
        detail += QStringLiteral("\n点「修复（重新装一遍）」把该下的下下来。");
        findings.append(Finding{Severity::Problem, QStringLiteral("这个服务端还没装好"), detail,
                                Action::Reinstall});
    } else {
        // 装好了才有"记的那一份说不说得清"这一说（没装好那一条已经把话说了）。
        if (!facts.metaKnown) {
            findings.append(Finding{
                Severity::Warning, QStringLiteral("这不是启动器装的"),
                QStringLiteral("目录里没有我们记的那一份，所以我们不知道它原来按哪一档装、"
                               "该按哪一档重装。\n让它自己把入口文件备齐就能启动；"
                               "要按我们的方式管它，只能删了重新建一个。"),
                Action::None});
        } else if (!facts.launchable) {
            findings.append(Finding{
                Severity::Problem, QStringLiteral("记的那一份拼不出启动命令"),
                (facts.launchProblem.isEmpty()
                     ? QStringLiteral("我们记的那一份里缺了启动要用的版本信息。")
                     : facts.launchProblem)
                    + QStringLiteral("\n点「修复（重新装一遍）」按记的这一档补齐。"),
                Action::Reinstall});
        }
    }

    if (facts.installed && !facts.eulaAccepted) {
        findings.append(Finding{
            Severity::Problem, QStringLiteral("还没同意 Minecraft 的最终用户许可协议"),
            QStringLiteral("服务端第一次启动前必须写下 eula=true —— 它自己不会替你同意，"
                           "点了「同意并启动」之后才会写进 eula.txt。"),
            Action::AcceptEula});
    }

    for (const ServerPropertiesForm::ValueProblem &problem : facts.valueProblems) {
        findings.append(Finding{
            Severity::Problem,
            QStringLiteral("「%1」这一项的值不对").arg(problem.label),
            QStringLiteral("文件里现在写的是 %1。%2\n点「改回缺省值」把它收回来，"
                           "或者去「常用设置」自己改。")
                .arg(problem.value.isEmpty() ? QStringLiteral("（空的）") : problem.value,
                     problem.problem),
            Action::FixValues});
    }

    // 端口那一条：**只在查得出端口、而且没在跑的时候说**。在跑的时候它自己就占着那个端口，
    // 报"被人占了"是一句吓人的废话（见 Facts::portState 那段）。
    if (facts.installed && !facts.running && facts.port > 0
        && facts.portState == ServerPortCheck::State::InUse) {
        findings.append(Finding{
            Severity::Problem, QStringLiteral("端口 %1 现在有别的程序在用").arg(facts.port),
            QStringLiteral("服务端起手时会因为抢不到这个端口直接退出。\n"
                           "把那个程序关掉，或者到「常用设置」里换一个端口。"),
            Action::OpenSettings});
    }

    // ---------- ② 提醒 ----------
    if (!facts.duplicateKeys.isEmpty()) {
        findings.append(Finding{
            Severity::Warning,
            QStringLiteral("文件里有同一个键写了两遍：%1")
                .arg(facts.duplicateKeys.join(QStringLiteral("、"))),
            QStringLiteral("服务端按最后那一行算，前面那几行等于没写。"
                           "要去掉的话在「原始文本」里删一行就行 —— 我们这一页不会动它。"),
            Action::None});
    }
    if (facts.installed && facts.worldBackupCount <= 0 && facts.worldExists) {
        findings.append(Finding{
            Severity::Warning, QStringLiteral("这个世界还没有备份"),
            QStringLiteral("世界是这一整个目录里唯一重新下不回来的东西 —— 模组能再下、"
                           "服务端能再装，存档坏了就是坏了。\n去「世界备份」那一页点一下"
                           "「立即备份」。"),
            Action::OpenBackups});
    }

    // ---------- ③ 说明 ----------
    if (facts.installed && !facts.worldName.isEmpty() && !facts.worldExists) {
        // 新装的、还没跑过的服务端就是这样：世界要等它第一次起来才生成。
        findings.append(Finding{
            Severity::Info,
            QStringLiteral("世界「%1」还没生成").arg(facts.worldName),
            QStringLiteral("第一次启动之后它才会在服务端目录里建这个文件夹。"),
            Action::None});
    } else if (facts.installed && facts.worldExists) {
        findings.append(Finding{
            Severity::Info, QStringLiteral("世界「%1」在盘上").arg(facts.worldName),
            QStringLiteral("它在服务端目录里的 %1/ 底下%2。")
                .arg(facts.worldName,
                     facts.worldBytes > 0
                         ? QStringLiteral("，现在有 %1 MB")
                               .arg(QString::number(double(facts.worldBytes) / (1024.0 * 1024.0),
                                                    'f', 1))
                         : QString()),
            Action::None});
    }
    if (facts.modCount > 0) {
        findings.append(Finding{
            Severity::Info,
            QStringLiteral("装了 %1 个模组 / 插件").arg(facts.modCount),
            QStringLiteral("它们会跟着服务端一起起来；装完新模组记得重启服务端。"),
            Action::None});
    }
    if (!facts.unknownKeys.isEmpty()) {
        findings.append(Finding{
            Severity::Info,
            QStringLiteral("配置文件里还有 %1 项是我们不认识的")
                .arg(facts.unknownKeys.size()),
            QStringLiteral("这些键（%1）在「常用设置」里没有对应的格子，但我们**一个都不动**"
                           "—— 保存的时候它们原样留着。")
                .arg(facts.unknownKeys.mid(0, 6).join(QStringLiteral("、"))),
            Action::None});
    }
    return findings;
}

}  // namespace ServerCheck
