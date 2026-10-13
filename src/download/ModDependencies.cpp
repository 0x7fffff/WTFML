/**
 * @file ModDependencies.cpp
 * @brief ModDependencies.hpp 的实现。**一行网络、一个文件都不碰**（见那边文件头）。
 * @author 0x7fffff
 * @date 2026-09-28
 */

#include "ModDependencies.hpp"

namespace ModDependencies {

QString versionsKeyFor(const QString &consumer, const QString &projectId,
                       const QString &gameVersion, const QString &loader,
                       ResourceCatalog::Source source) {
    // 分隔符 `|`（`versionsRequestKey` 里面用的也是它，那几个字段里都不会有它）。
    return ResourceCatalog::versionsRequestKey(projectId, gameVersion, loader, source) +
           QLatin1Char('|') + consumer;
}

QString projectsKeyFor(const QString &consumer, const QStringList &projectIds,
                       ResourceCatalog::Source source) {
    return ResourceCatalog::projectsRequestKey(projectIds, source) + QLatin1Char('|') + consumer;
}

bool kindUsesLoader(ResourceCatalog::Kind kind) {
    // **只有模组那一类要**（理由全在 .hpp 的 `InstalledItem::loaderApplies` 那段里）。
    // 写成一串 `==` 而不是"排除那几个"：后加一类资源时，默认**不带**比默认带上去安全 ——
    // 带错了的症状是"这一类一条都判不出来"，而不带的症状最坏是"问回来的是另一条版本线"
    // （那一条至少还在报告里，用户看得见）。
    switch (kind) {
    case ResourceCatalog::Kind::Mod:
    case ResourceCatalog::Kind::Modpack:
        return true;
    case ResourceCatalog::Kind::DataPack:
    case ResourceCatalog::Kind::Shader:
    case ResourceCatalog::Kind::ResourcePack:
        return false;
    }
    return false;
}

/// 前置这一趟的消费方名字（**唯一一处** —— 拼钥匙和"那个槽认不认这一发"读的是同一个字）。
static QString dependencyConsumerName() { return QStringLiteral("dependency"); }

/// 「这种前置不用在详情页反复列它」—— 现在只排掉 **Fabric API**。
///
/// 凭据只能是**项目号**：接口给的那一条 `dependencies[]` 里除了项目号什么都没有（Modrinth
/// 是 `project_id`、CurseForge 是 `modId`，都不带名字/文件名）。所以这里**两家的号都列上**
/// —— 两个号的形状天然撞不上（Modrinth 是 8 位 base62、CurseForge 是纯数字），放一张表里
/// 不会互相误伤。**两个号都实测过**（`api.modrinth.com/v2/project/fabric-api` 回来的是
/// `P7dR8mSH`；CurseForge 那个页面上的 Project ID 写着 `306612`）。
///
/// ★ 它**只影响展示清单**。安装判定仍走 `requiredDependenciesOf()`：
///   实例缺 Fabric API 时仍会进入「一起下载」。把它从真实依赖集合里删掉会让 UI
///   看起来干净，但最后装出一份缺前置的实例。
///
/// NOTE: 同一条理由以后要隐藏别人时加进这张表就行，别污染真实依赖判定。
static bool isObviousPrerequisite(const QString &projectId) {
    static const QSet<QString> kIds = {
        QStringLiteral("P7dR8mSH"),  // Modrinth: fabric-api
        QStringLiteral("306612"),    // CurseForge: Fabric API
    };
    return kIds.contains(projectId);
}

QList<ResourceCatalog::Dependency> requiredDependenciesOf(
    const ResourceCatalog::VersionEntry &version) {
    QList<ResourceCatalog::Dependency> out;
    for (const ResourceCatalog::Dependency &dependency : version.dependencies) {
        // 这两条都是"不算必装依赖"，但理由不一样：
        //   · optional / incompatible / embedded → 用户没要，或压根不该单独下一份；
        //   · projectId 空 → 接口给的烂数据（一版里出现过 `{"project_id":null}` 这种），
        //     拿它拼出来的地址会是一个"问了一堆不相干的东西"的请求。
        // "已经装着了"**不在这一层**：那是 `missingRequiredDependencies()` 的事，
        // 因为"装没装"是这个函数不该知道的东西（见 .hpp 文件头那段）。
        if (!dependency.required()) {
            continue;
        }
        if (dependency.projectId.isEmpty()) {
            continue;
        }
        out.append(dependency);
    }
    return out;
}

QList<ResourceCatalog::Dependency> displayedRequiredDependencies(
    const ResourceCatalog::VersionEntry &version) {
    QList<ResourceCatalog::Dependency> out;
    for (const ResourceCatalog::Dependency &dependency : requiredDependenciesOf(version)) {
        if (!isObviousPrerequisite(dependency.projectId)) {
            out.append(dependency);
        }
    }
    return out;
}

QList<Lookup> missingRequiredDependencies(const ResourceCatalog::VersionEntry &version,
                                          const QSet<QString> &installedKeys,
                                          const Target &target) {
    QList<Lookup> out;
    // 「什么算必装前置」那两条判据在 requiredDependenciesOf() 里（**唯一一处**），
    // 这里只往下接第三条 —— 已经装着了就不再下一份。
    for (const ResourceCatalog::Dependency &dependency : requiredDependenciesOf(version)) {
        if (installedKeys.contains(target.installedKey(dependency.projectId))) {
            continue;
        }

        Lookup lookup;
        lookup.dependency = dependency;
        // 地址和钥匙都走 ResourceCatalog 那两个唯一出处（两个来源的参数名不一样，
        // 而且 CurseForge 那边加载器要的是**数字**，见 versionsUrl 里那段）。
        lookup.url = ResourceCatalog::versionsUrl(dependency.projectId, target.gameVersion,
                                                 target.loader, target.source, target.apiBase);
        lookup.requestKey = versionsKeyFor(dependencyConsumerName(), dependency.projectId,
                                           target.gameVersion, target.loader, target.source);
        out.append(lookup);
    }
    return out;
}

bool resolveDependency(const ResourceCatalog::Dependency &dependency,
                       const QList<ResourceCatalog::VersionEntry> &versions, Resolved *out,
                       QString *why) {
    const auto fail = [why](const QString &reason) {
        if (why != nullptr) {
            *why = reason;
        }
        return false;
    };

    if (versions.isEmpty()) {
        // 空的两种（那一发失败 / 这个项目在这个游戏版本+加载器下没有版本）在这一层分不开
        // —— 分得开的那一位在调用方手上（那一发有没有报错）。**这句话两边都说得通**，
        // 所以就把两边都说清楚，别替另一件下结论。
        return fail(QStringLiteral("没问到能用的版本（网络没通，或者它在这个游戏版本上没有版"
                                   "本）。"));
    }

    const ResourceCatalog::VersionEntry *picked = nullptr;
    if (!dependency.versionId.isEmpty()) {
        for (const ResourceCatalog::VersionEntry &version : versions) {
            if (version.id == dependency.versionId) {
                picked = &version;
                break;
            }
        }
        if (picked == nullptr) {
            // ★ **不退而求其次挑最新的一条**。作者钉版本往往是因为新旧不兼容，悄悄换一版
            //   装进去，症状是"这几个模组一起不生效"，而那一刻离这里已经很远了。
            return fail(QStringLiteral("作者钉的那一版（%1）在这个实例的版本下找不到。")
                            .arg(dependency.versionId));
        }
    } else {
        // 没钉 = "哪个版本都行" ⇒ 拿最新的那条。这份列表接口给的就是新的在前
        //（`parseVersions` 那段有实测），所以**不排序、不按版本号比大小**。
        picked = &versions.first();
    }

    const ResourceCatalog::FileEntry *file = picked->primaryFile();
    if (file == nullptr) {
        return fail(QStringLiteral("挑出来的那一版（%1）没有文件。").arg(picked->versionNumber));
    }
    if (file->url.isEmpty()) {
        // 作者关掉了第三方分发时，那边写好的那句话比"没有可下载的文件"准确得多
        //（见 FileEntry::unavailableNote 那段），有就用它。
        return fail(file->unavailableNote.isEmpty()
                        ? QStringLiteral("挑出来的那一版（%1）没有可下载的文件。")
                              .arg(picked->versionNumber)
                        : file->unavailableNote);
    }

    if (out != nullptr) {
        out->dependency = dependency;
        out->version = *picked;
        out->file = *file;
    }
    return true;
}

Plan buildPlan(const QList<Lookup> &lookups,
               const QHash<QString, QList<ResourceCatalog::VersionEntry>> &answers,
               const QHash<QString, QString> &titlesByProjectId) {
    Plan plan;
    for (const Lookup &lookup : lookups) {
        const QString projectId = lookup.dependency.projectId;
        // 名字取不到就退成项目号（`nvQzSEkH` 这种用户认不出，但比"有一个前置没找到"强）。
        QString label = titlesByProjectId.value(projectId);
        if (label.isEmpty()) {
            label = projectId;
        }

        const auto answersIt = answers.constFind(lookup.requestKey);
        if (answersIt == answers.constEnd()) {
            // 钥匙不在表里 = 那一发没回来（或者回来时没被记下来）。和"它没有版本"分开说。
            plan.unresolved.append(
                QStringLiteral("%1：没问到版本列表（网络或接口没成）。").arg(label));
            continue;
        }

        Resolved resolved;
        QString why;
        if (!resolveDependency(lookup.dependency, answersIt.value(), &resolved, &why)) {
            plan.unresolved.append(QStringLiteral("%1：%2").arg(label, why));
            continue;
        }
        resolved.title = label;
        plan.downloads.append(resolved);
    }
    return plan;
}

UpdateVerdict judgeUpdate(const InstalledItem &item,
                          const QList<ResourceCatalog::VersionEntry> &versions,
                          const QString &problem) {
    UpdateVerdict verdict;
    verdict.item = item;
    verdict.state = UpdateState::Unknown;

    if (item.sha1.isEmpty()) {
        // 认不出来是哪个文件（读不动、或者哈希那一步没成）—— 这一条**不是**"已是最新"。
        verdict.reason = QStringLiteral("认不出盘上这个文件是哪一版。");
        return verdict;
    }
    if (versions.isEmpty()) {
        // ★★ 2026-10-11：那一发**自己报的那句话**优先（429 那条尤其）——
        //   从前这里一律写"没问到版本列表（网络或接口没成）"，于是"问得太勤了"这个**真因
        //   一个字都不会露给用户**：他看到的每一条都是"说不清、网络没成"，于是在那儿反复
        //   重试网络，而问题在"一次问得太多"上（用户那句「无法正常获取更新」就是这个症状）。
        verdict.reason = problem.isEmpty() ? QStringLiteral("没问到版本列表（网络或接口没成）。")
                                           : problem;
        return verdict;
    }

    int found = -1;
    for (int i = 0; i < versions.size(); ++i) {
        for (const ResourceCatalog::FileEntry &file : versions.at(i).files) {
            if (!file.sha1.isEmpty() && file.sha1.compare(item.sha1, Qt::CaseInsensitive) == 0) {
                found = i;
                break;
            }
        }
        if (found >= 0) {
            break;
        }
    }
    if (found < 0) {
        // 三种情况合成一句（列表筛过、作者删了老版本、用户改过那个 jar），
        // 见头文件里那段 —— 下一步都是"去项目页自己看"。
        verdict.reason = QStringLiteral("这一版不在列表里（可能不是给这个实例的，或者作者已经删"
                                        "了它）。");
        return verdict;
    }

    verdict.latestVersionId = versions.first().id;
    verdict.latestVersionNumber = versions.first().versionNumber;
    const ResourceCatalog::FileEntry *latestFile = versions.first().primaryFile();
    verdict.latestFileName = latestFile == nullptr ? QString() : latestFile->filename;
    verdict.newerCount = found;
    verdict.state = found == 0 ? UpdateState::UpToDate : UpdateState::Outdated;
    return verdict;
}

UpdateVerdict judgeUpdateAgainstLatest(const InstalledItem &item,
                                       const ResourceCatalog::VersionEntry &latest,
                                       const QString &problem) {
    UpdateVerdict verdict;
    verdict.item = item;
    verdict.state = UpdateState::Unknown;

    if (item.sha1.isEmpty()) {
        // 和上面那条**逐字同一句话**：认不出来是哪一版，就谈不上"是不是最新的"。
        verdict.reason = QStringLiteral("认不出盘上这个文件是哪一版。");
        return verdict;
    }
    if (latest.id.isEmpty()) {
        // 接口对这个哈希**什么都没给**。三种可能分不开（这一层手上没有分得开的东西）：
        // 那一批自己没成（problem）、这个项目在这个游戏版本/加载器下没有版本、
        // 或者它压根不在 Modrinth 上。所以 reason 那句按 problem 说（429 那条尤其 ——
        // 见 .hpp 里那段：真因不许被吞成"网络没成"）。
        verdict.reason =
            problem.isEmpty()
                ? QStringLiteral("没查到它能用的版本（这个项目在这个游戏版本/加载器下没有"
                                 "版本）。")
                : problem;
        return verdict;
    }

    // 判据和 `judgeUpdate()` 那一条**是同一句话**：盘上这一份的 sha1 就是"最新那一版"的
    // 某个文件的 sha1 ⇒ 已是最新。这里不必（也没法）说"比它新的有几条" —— 接口给的就是
    // 最新的那一条，`newerCount` 于是只在"可更新"这一档给一个 ≥1 的值。
    bool sameFile = false;
    for (const ResourceCatalog::FileEntry &file : latest.files) {
        if (!file.sha1.isEmpty() && file.sha1.compare(item.sha1, Qt::CaseInsensitive) == 0) {
            sameFile = true;
            break;
        }
    }

    verdict.latestVersionId = latest.id;
    verdict.latestVersionNumber = latest.versionNumber;
    const ResourceCatalog::FileEntry *latestFile = latest.primaryFile();
    verdict.latestFileName = latestFile == nullptr ? QString() : latestFile->filename;
    verdict.newerCount = sameFile ? 0 : 1;
    verdict.state = sameFile ? UpdateState::UpToDate : UpdateState::Outdated;
    return verdict;
}

QString latestVersionsKeyFor(const QString &consumer, const QString &gameVersion,
                             const QString &loader) {
    // 分隔符 `|`：项目号 / 游戏版本 / 加载器 / 消费方这几个字段里都不会出现它，所以不用转义
    //（和 `versionsRequestKey()` 那边逐字同一个规矩）。**来源不需要**：这一发只有 Modrinth
    // 有（CurseForge 没有对应的批量接口），所以拼不拼上它都是同一把钥匙。
    return QStringLiteral("latest|") + gameVersion + QLatin1Char('|') + loader +
           QLatin1Char('|') + consumer;
}

// ---------- 界面上的字 ----------
namespace {

/// 名单怎么摆 —— **唯一出处**（界面和验证工程都走这儿）。
///
/// 用户 2026-10-09 的原话：「（模组名称，一行一个，如果多于 3 个的话写 还有 x 个未列出...
/// （当然如果刚好还剩一个就列出来了不要搞一行显示 1 个未列出这种高血压操作）」
///
/// 翻成两条判据：
///   · **总数 ≤ 4 ⇒ 一条不省，全列**（"刚好还剩一个"的就是 4 条这一档，它走的是全列）；
///   · **总数 > 4 ⇒ 列前 3 条 + 「还有 N 个未列出…」**（N = 总数 − 3）。
/// 所以「还有 1 个未列出」这一行**永远画不出来** —— 那正是用户点名不要的那一句。
QString dependencyNameLines(const QStringList &names) {
    /// 到这一档为止一律全列（含 4 —— 多出来的第 4 条宁可多占一行，也不给用户看
    /// 「还有 1 个未列出」）。
    static constexpr int kListAllUpTo = 4;
    /// 真要截断时留下几条。
    static constexpr int kKeepWhenTruncated = 3;

    QStringList lines;
    if (names.size() <= kListAllUpTo) {
        lines.reserve(names.size());
        for (const QString &name : names) {
            lines.append(QStringLiteral("- ") + name);
        }
        return lines.join(QLatin1Char('\n'));
    }
    lines.reserve(kKeepWhenTruncated + 1);
    for (int i = 0; i < kKeepWhenTruncated; ++i) {
        lines.append(QStringLiteral("- ") + names.at(i));
    }
    // 这一行**不带短横**：它不是名单里的一条，是一句"后面还有"。
    lines.append(QStringLiteral("还有 %1 个未列出…").arg(names.size() - kKeepWhenTruncated));
    return lines.join(QLatin1Char('\n'));
}

}  // namespace

QString dependencyPromptHead(ResourceCatalog::Kind kind) {
    return kind == ResourceCatalog::Kind::Shader ? QStringLiteral("此光影包缺失必要的前置：")
                                                 : QStringLiteral("此模组缺失必要的前置：");
}

QString dependencyPromptTitle() { return QStringLiteral("还有前置没装"); }

QString dependencyPromptText(ResourceCatalog::Kind kind, const QStringList &names) {
    if (names.isEmpty()) {
        return QString();
    }
    return QStringLiteral("%1\n\n%2\n\n要一起下载吗？")
        .arg(dependencyPromptHead(kind), dependencyNameLines(names));
}

QString dependencyPromptUnknownText(ResourceCatalog::Kind kind, const QStringList &names) {
    if (names.isEmpty()) {
        return QString();
    }
    return QStringLiteral("%1（还没看过这个实例里装了什么，不知道你有没有）：\n\n%2\n\n"
                          "要一起下载吗？")
        .arg(dependencyPromptHead(kind), dependencyNameLines(names));
}

QString dependencyPromptNoteText() {
    // 那一行灰字。**唯一出处**：界面（InlineMessage::ask 的 note）和验证工程都读它。
    // 写法照「提示」栏里那两条（重复启动 / 进度提醒）—— 都是"提醒本身 + 去哪儿关"。
    return QStringLiteral("可以在设置中关闭此提示（设置页「下载」→「缺失前置提醒」）。");
}

QString dependencyAllButtonText() { return QStringLiteral("一起下载"); }

QString dependencyOnlyThisButtonText() { return QStringLiteral("只下这个"); }

QString unresolvedNoteText(const QStringList &lines) {
    if (lines.isEmpty()) {
        return QString();
    }
    return QStringLiteral("有几个前置没找到能装的版本：\n\n%1\n\n"
                          "它们下不下来，其余的都照下。可以去项目页自己挑一版。")
        .arg(lines.join(QLatin1Char('\n')));
}

QString dependencyLookupLogText(int count) {
    return QStringLiteral("正在问 %1 个前置模组要下哪一版…").arg(count);
}

QString detailPrerequisiteText(const QStringList &names) {
    if (names.isEmpty()) {
        return QString();   // 一条前置都没有 = 这一行整个不画（调用点判空）
    }
    // 顿号连起来。**不截断**：前置一般就一两条，真遇上十条的（那种"全家桶"整合包里的
    // 核心模组）也该照实列全 —— 截断省下的那点宽度换来的是"还有一个是谁"。
    return QStringLiteral("这一版要装的前置：%1").arg(names.join(QStringLiteral("、")));
}

QString detailPrerequisiteCountText(int count) {
    if (count <= 0) {
        return QString();
    }
    return QStringLiteral("这一版要 %1 个前置模组（名字没取到）。").arg(count);
}

QString updateUpToDateText() { return QStringLiteral("已是最新"); }

QString updateOutdatedText() { return QStringLiteral("有新版本"); }

QString updateUnknownText() { return QStringLiteral("说不清"); }

QString updateSummaryText(int checked, int outdated) {
    return QStringLiteral("检查了 %1 个资源，%2 个有新版本。").arg(checked).arg(outdated);
}

}  // namespace ModDependencies
