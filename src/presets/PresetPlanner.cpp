/**
 * @file PresetPlanner.cpp
 * @brief 实现 PresetPlanner.hpp（一段一行地算这一趟会怎么样）。取舍都写在那边文件头。
 * @author 0x7fffff
 * @date 2026-10-15
 *
 * 【这一层里那些"数"是从哪儿来的（一段一段地对）】
 *
 *   · `VanillaOptions` / `VanillaKeys` —— `planOptionsApply()` 的 `OptionsApplyReport`
 *     （`written` / `added` / `removed` / `skipped` / `conflicts`）。
 *   · `ServerList` —— `planServerListApply()` 的 `ServerListReport`
 *     （`written` / `added` / `removed` / `skipped` / `untouchable`）。
 *   · `GenericFiles` —— `planFileGroup()` 的 `GenericPlan`
 *     （`replaced + copied` / `skipped` / `needsChoice`）+ 逐项的理由。
 *
 * ★ **四个报告都不在这儿重算**：这一层只做"取数 + 翻译"。哪天某个适配器改了算法的口径，
 *   表上跟着变的是它自己那一行 —— 这一层里的加法和它无关。
 *
 * 【"还没确认"的那一趟为什么照样算】
 *
 * 两个适配器的计划函数在 `replaceConfirmed = false` 时都会**拒算**（回 `ok = false`、
 * `needsConfirm = true`），因为它们的调用方是"真的要写"那一路。可这一层是**预览**：
 * 用户正要拿这张表去决定要不要点头，跟他说"这一行是覆盖，几项不知道"等于没给他任何
 * 信息。所以这一层一律按 `replaceConfirmed = true` 去算，再把"还没点头"**自己**标在
 * 行上（`mode == Replace && !request.replaceConfirmed`）。
 *
 * ★ 那两个适配器里 `needsConfirm` 那一档**照样把数给出来**（`ServerListPreset` 那边
 *   明说了"报告里那几个数照给，界面正是拿它去问用户"），所以哪天想改成直接透传它们那份
 *   报告也行 —— 只要别把"数"丢掉。
 */

#include "PresetPlanner.hpp"

#include <QCoreApplication>
#include <QSet>

namespace Presets {

namespace {

/// 这一层的提示语都在同一个上下文里（和 `VanillaOptions` / `ServerListPreset` /
/// `GenericFiles` 同一个名字）。
QString plannerText(const char *source) { return QCoreApplication::translate("Presets", source); }

/// 一段「覆盖」还没被点头时那一行下面那句话。
QString confirmReason(SectionKind kind) {
    switch (kind) {
    case SectionKind::VanillaOptions:
    case SectionKind::VanillaKeys:
        return plannerText("「覆盖」会把目标上这一类的设置整个换成预设里那些，需要先确认。");
    case SectionKind::ServerList:
        return plannerText("「覆盖」会拿掉目标上对不上的服务器条目，需要先确认。");
    case SectionKind::GenericFiles:
        return plannerText("「覆盖」会把目标上同名的那几份换成预设里的（多出来的文件不动），"
                           "需要先确认。");
    case SectionKind::MalilibKeys:
    case SectionKind::Custom:
        break;
    }
    return plannerText("「覆盖」会改动目标上已有的东西，需要先确认。");
}

/// 这一段的种类这一版还认不认识（P4 的 MaLiLib 还没落 —— 那一段落 `blocked` 是**对的**，
/// 悄悄跳过的话用户会以为"MaLiLib 的热键跟着预设过来了"）。
bool plannerKnowsKind(SectionKind kind) {
    return kind == SectionKind::VanillaOptions || kind == SectionKind::VanillaKeys ||
           kind == SectionKind::ServerList || kind == SectionKind::GenericFiles;
}

QString unknownKindReason(SectionKind kind) {
    if (kind == SectionKind::MalilibKeys) {
        return plannerText("MaLiLib 那一族的热键这一版的启动器还不认识，这一段这一趟不动。");
    }
    return plannerText("这一段的种类这一版的启动器不认识，这一段这一趟不动。");
}

/// 「覆盖」那一行：点头这件事是**这一层**判的（见文件头那段）。
void markConfirm(ApplyPlanRow *row, const PlanRequest &request) {
    if (row->blocked || row->mode != ApplyMode::Replace || request.replaceConfirmed) {
        return;
    }
    row->needsConfirm = true;
    row->problem = confirmReason(row->kind);
}

/// 落一段 `VanillaOptions` / `VanillaKeys` 的计划。
void planOptionsRow(const PlanRequest &request, const PresetSection &section, ApplyPlanRow *row) {
    bool readable = false;
    const OptionsFile source = optionsFromSections({section}, &readable);
    if (!readable) {
        row->blocked = true;
        row->problem = plannerText("这一段读不回来（载荷不是这一层认得的那样），这一趟不动它。");
        return;
    }

    // ★ 「勾了哪些键」= **这一段里有的那些键**。段里的内容本来就是用户勾过的那一套
    //   （`sectionsFromOptions()` 只写勾中的），所以这一趟不该拿"默认该勾什么"再猜一遍 ——
    //   猜的后果是用户特意没勾的硬件项被我们偷偷写进去（§6 P2 点名的「显示器、全屏分辨率等
    //   硬件相关项默认不选」）。
    QSet<QString> selected;
    for (const QString &key : source.keys()) {
        selected.insert(key);
    }

    // ★ 一律按"已确认"算（见文件头），"还没点头"由 `markConfirm()` 标在行上。
    const OptionsApplyReport report =
        planOptionsApply(section.kind, source, request.target.options, selected, section.mode, true);
    if (!report.ok) {
        row->blocked = true;
        row->problem = report.problem;
        return;
    }

    row->count = report.written;
    row->removed = report.removed;
    row->skipped = report.skipped;
    row->conflictCount = report.conflicts.size();
}

/// 落一段 `ServerList` 的计划。
void planServerRow(const PlanRequest &request, const PresetSection &section, ApplyPlanRow *row) {
    if (!request.target.serversProblem.isEmpty()) {
        row->blocked = true;
        row->problem = request.target.serversProblem;
        return;
    }

    QList<NbtUtil::ServerEntry> source;
    QString why;
    if (!serverListFromSection(section, &source, &why)) {
        row->blocked = true;
        row->problem = why;
        return;
    }

    const ServerListReport report =
        planServerListApply(section.mode, request.target.servers, request.target.serversUntouchable,
                            source, true);
    if (!report.ok) {
        row->blocked = true;
        row->problem = report.problem;
        return;
    }

    row->count = report.written;
    row->removed = report.removed;
    row->skipped = report.skipped;
}

/// 落一段 `GenericFiles` 的计划。**这一档是四段里唯一会读盘的**（干跑数目标上缺哪几份）。
void planGenericFilesRow(const PlanRequest &request, const PresetSection &section,
                         ApplyPlanRow *row) {
    QList<GenericItem> items;
    QString why;
    if (!fileGroupFromSection(section, &items, &why)) {
        row->blocked = true;
        row->problem = why;
        return;
    }

    // 这一组是哪一组：**先看段的身份（id）**，空段（一条都没有）只有它说得出来。
    FileGroup group = FileGroup::Config;
    const bool idSays = fileGroupFromSectionId(section.id, &group);
    if (!idSays && items.isEmpty()) {
        row->blocked = true;
        row->problem =
            plannerText("这一段说不清是哪个文件组（id 不认得、载荷又是空的），这一趟不动它。");
        return;
    }
    if (idSays && !items.isEmpty()) {
        // ★ 两份说法对了一下：id 说是 A 组、载荷里每一条路径却在 B 组底下 —— 这种段**不认**。
        //   （`fileGroupFromSection()` 只保证载荷内部自洽；和 id 是否一致只有这儿判得了。）
        FileGroup fromPaths = FileGroup::Config;
        if (fileGroupOfPath(items.at(0).relativePath, &fromPaths) && fromPaths != group) {
            row->blocked = true;
            row->problem = plannerText("这一段的身份（%1）和它的内容（%2）对不上，这一趟不动它。")
                               .arg(fileGroupDirName(group), fileGroupDirName(fromPaths));
            return;
        }
    }
    if (request.presetFilesRoot.trimmed().isEmpty() && !items.isEmpty()) {
        // 没有源根就数不出"目标上缺哪几份"。★ 这一档**不许**当成"没有东西要拷"（那会让
        // 用户以为预设里那几份配置是空的）。
        row->blocked = true;
        row->problem = plannerText("没有给出预设那一侧的文件根（files/），数不出这一组缺哪几份。");
        return;
    }

    const GenericPlan plan =
        planFileGroup(request.presetFilesRoot, items, request.target.instanceRoot, section.mode);
    if (!plan.ok) {
        row->blocked = true;
        row->problem = plan.problem;
        return;
    }

    row->count = plan.replaced + plan.copied;
    row->skipped = plan.skipped;
    row->needsChoice = plan.needsChoice;
}

}  // namespace

// ---------------------------------------------------------------------------
// 那几口"字"的唯一出处
// ---------------------------------------------------------------------------

QString applyPlanVerb(ApplyMode mode) {
    switch (mode) {
    case ApplyMode::Merge:
        return plannerText("合并");
    case ApplyMode::Replace:
        return plannerText("覆盖");
    case ApplyMode::Append:
        return plannerText("追加");
    case ApplyMode::CopyIfMissing:
        return plannerText("缺了才拷");
    case ApplyMode::Skip:
        return plannerText("跳过");
    }
    return QString();
}

QString applyPlanUnit(SectionKind kind) {
    switch (kind) {
    // `options.txt` 那两段动的是**键**，说「项」（§7：「游戏设置 合并 18 项」）。
    case SectionKind::VanillaOptions:
    case SectionKind::VanillaKeys:
        return plannerText("项");
    // 服务器条目、配置文件、资源包都是"一个东西"，说「个」（§7：「服务器列表 追加 3 个」）。
    case SectionKind::ServerList:
    case SectionKind::GenericFiles:
        return plannerText("个");
    case SectionKind::MalilibKeys:
    case SectionKind::Custom:
        break;
    }
    return plannerText("项");
}

QString applyPlanRowText(const ApplyPlanRow &row) {
    if (row.blocked) {
        return plannerText("做不了");
    }

    // 这一档我们做不了主 ⇒ 这一行只说得出口这一件事（`count` 那一趟必然是 0）。
    if (row.needsChoice > 0) {
        return plannerText("要你选 %1 %2")
            .arg(row.needsChoice)
            .arg(applyPlanUnit(row.kind));
    }

    const QString unit = applyPlanUnit(row.kind);
    QStringList parts;

    if (row.count > 0) {
        parts.append(applyPlanVerb(row.mode) + QLatin1Char(' ') + QString::number(row.count) +
                     QLatin1Char(' ') + unit);
    } else if (row.removed > 0) {
        // ★ 会动的数是 0、却要删东西 —— 这就是"空段铺上去 = 把那一片清空"那一档。
        //   只写「覆盖 0 项」等于把最要紧的那半截藏起来了。
        parts.append(applyPlanVerb(row.mode));
    }

    if (!parts.isEmpty()) {
        if (row.removed > 0) {
            parts.append(plannerText("删掉 %1 %2").arg(row.removed).arg(unit));
        }
        if (row.conflictCount > 0) {
            parts.append(plannerText("冲突 %1 项").arg(row.conflictCount));
        }
        if (row.skipped > 0) {
            parts.append(plannerText("跳过 %1 %2").arg(row.skipped).arg(unit));
        }
        return parts.join(plannerText("，"));
    }

    // 会动的、要删的、有冲突的都没有 ⇒ 这一行要么是"跳过"，要么是"没什么可动的"。
    if (row.skipped > 0) {
        return plannerText("跳过 %1 %2").arg(row.skipped).arg(unit);
    }
    if (row.mode == ApplyMode::Skip) {
        return applyPlanVerb(ApplyMode::Skip);
    }
    return plannerText("不动");
}

// ---------------------------------------------------------------------------
// 整份计划
// ---------------------------------------------------------------------------

int ApplyPlan::totalCount() const {
    int total = 0;
    for (const ApplyPlanRow &row : rows) {
        total += row.count;
    }
    return total;
}

int ApplyPlan::riskyRowCount() const {
    int total = 0;
    for (const ApplyPlanRow &row : rows) {
        if (row.risky()) {
            ++total;
        }
    }
    return total;
}

int ApplyPlan::blockedRowCount() const {
    int total = 0;
    for (const ApplyPlanRow &row : rows) {
        if (row.blocked) {
            ++total;
        }
    }
    return total;
}

bool ApplyPlan::needsConfirm() const {
    for (const ApplyPlanRow &row : rows) {
        if (row.needsConfirm) {
            return true;
        }
    }
    return false;
}

ApplyPlan planPresetApplication(const PlanRequest &request) {
    ApplyPlan plan;

    if (request.target.instanceRoot.trimmed().isEmpty()) {
        plan.problem = plannerText("没有指定目标实例，算不出这一趟要做什么。");
        return plan;
    }

    // 游戏在跑这一条是**整趟**的闸（不是某一段的事），理由见头文件那一位。
    if (request.gameRunning) {
        plan.blockedReason = plannerText("这个实例现在正在跑。游戏退出时会把它自己的那一份"
                                        "整片写回去，我们这一趟写进去的东西会被盖掉 —— "
                                        "先把它关掉再应用预设。");
    }

    for (const PresetSection &section : request.sections) {
        ApplyPlanRow row;
        row.sectionId = section.id;
        row.kind = section.kind;
        row.title = section.title;
        row.mode = section.mode;

        if (!plannerKnowsKind(section.kind)) {
            row.blocked = true;
            row.problem = unknownKindReason(section.kind);
        } else if (optionsHandlesSection(section.kind)) {
            planOptionsRow(request, section, &row);
        } else if (serverListHandlesSection(section.kind)) {
            planServerRow(request, section, &row);
        } else if (genericFilesHandlesSection(section.kind)) {
            planGenericFilesRow(request, section, &row);
        } else {
            // 三张分派表都不认 —— 那说明"认识的种类"和"有人接手的种类"对不上了（加适配器
            // 时漏改一处）。落 `blocked` 而不是崩：这一趟少一段，用户看得见。
            row.blocked = true;
            row.problem = plannerText("这一段没有任何适配器接手，这一趟不动它。");
        }

        if (!row.blocked && row.mode == ApplyMode::Skip) {
            // 「跳过」那一段：适配器照样算（数出来给用户看"你这一段跳过了多少东西"），
            // 但一个字节都不会动 —— 所以"会动"的那个数清零，免得计划表上虚报。
            row.count = 0;
            row.removed = 0;
        }

        markConfirm(&row, request);
        plan.rows.append(row);
    }

    plan.ok = true;
    return plan;
}

}  // namespace Presets
