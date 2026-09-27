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

/// 前置这一趟的消费方名字（**唯一一处** —— 拼钥匙和"那个槽认不认这一发"读的是同一个字）。
static QString dependencyConsumerName() { return QStringLiteral("dependency"); }

QList<Lookup> missingRequiredDependencies(const ResourceCatalog::VersionEntry &version,
                                          const QSet<QString> &installedKeys,
                                          const Target &target) {
    QList<Lookup> out;
    for (const ResourceCatalog::Dependency &dependency : version.dependencies) {
        // 这三条都是"不下它"，但**理由不一样**（写在这儿免得下次有人把后面两条当冗余删掉）：
        //   · optional / incompatible / embedded → 用户没要，或压根不该单独下一份；
        //   · projectId 空 → 接口给的烂数据（一版里出现过 `{"project_id":null}` 这种），
        //     拿它拼出来的地址会是一个"问了一堆不相干的东西"的请求；
        //   · 已经装着了 → 再下一份就是同一个模组在 mods/ 里两份，而游戏加载哪一份是
        //     按文件名顺序定的（用户看到的是"我明明换了版本，怎么还是老的"）。
        if (!dependency.required()) {
            continue;
        }
        if (dependency.projectId.isEmpty()) {
            continue;
        }
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
                          const QList<ResourceCatalog::VersionEntry> &versions) {
    UpdateVerdict verdict;
    verdict.item = item;
    verdict.state = UpdateState::Unknown;

    if (item.sha1.isEmpty()) {
        // 认不出来是哪个文件（读不动、或者哈希那一步没成）—— 这一条**不是**"已是最新"。
        verdict.reason = QStringLiteral("认不出盘上这个文件是哪一版。");
        return verdict;
    }
    if (versions.isEmpty()) {
        verdict.reason = QStringLiteral("没问到版本列表（网络或接口没成）。");
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

// ---------- 界面上的字 ----------

QString dependencyPromptTitle() { return QStringLiteral("还有前置没装"); }

QString dependencyPromptText(const QStringList &names) {
    if (names.isEmpty()) {
        return QString();
    }
    QStringList lines;
    lines.reserve(names.size());
    for (const QString &name : names) {
        lines.append(QStringLiteral("- ") + name);
    }
    return QStringLiteral("这个模组要用 %1 个前置模组，这几个还没装：\n\n%2\n\n要一起下载吗？")
        .arg(names.size())
        .arg(lines.join(QLatin1Char('\n')));
}

QString dependencyPromptUnknownText(const QStringList &names) {
    if (names.isEmpty()) {
        return QString();
    }
    QStringList lines;
    lines.reserve(names.size());
    for (const QString &name : names) {
        lines.append(QStringLiteral("- ") + name);
    }
    return QStringLiteral("这个模组要用 %1 个前置模组（还没看过这个实例里装了什么，"
                          "不知道你有没有）：\n\n%2\n\n要一起下载吗？")
        .arg(names.size())
        .arg(lines.join(QLatin1Char('\n')));
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

QString updateUpToDateText() { return QStringLiteral("已是最新"); }

QString updateOutdatedText() { return QStringLiteral("有新版本"); }

QString updateUnknownText() { return QStringLiteral("说不清"); }

QString updateSummaryText(int checked, int outdated) {
    return QStringLiteral("检查了 %1 个资源，%2 个有新版本。").arg(checked).arg(outdated);
}

}  // namespace ModDependencies
