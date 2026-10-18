/**
 * @file ModInstallPlan.cpp
 * @brief 实现 ModInstallPlan.hpp（必要/可选、依赖展开、冲突、替代确认与落盘校验）。
 * @author 0x7fffff
 * @date 2026-10-18
 */

#include "ModInstallPlan.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QSet>

#include <functional>

namespace Presets {

namespace {

QString planText(const char *source) { return QCoreApplication::translate("Presets", source); }

QString resolvedTitle(const ModResolveInput &input, const ModReference &reference) {
    if (!input.title.trimmed().isEmpty()) {
        return input.title.trimmed();
    }
    if (!reference.name.trimmed().isEmpty()) {
        return reference.name.trimmed();
    }
    return reference.projectId;
}

QString effectiveGameVersion(const ModInstallRequest &request, const ModReference &reference) {
    return reference.gameVersion.trimmed().isEmpty() ? request.gameVersion.trimmed()
                                                     : reference.gameVersion.trimmed();
}

QString effectiveLoader(const ModInstallRequest &request, const ModReference &reference) {
    return reference.loader.trimmed().isEmpty() ? request.loader.trimmed()
                                                : reference.loader.trimmed();
}

QString contextText(const QString &gameVersion, const QString &loader) {
    QStringList parts;
    if (!gameVersion.isEmpty()) {
        parts.append(gameVersion);
    }
    if (!loader.isEmpty()) {
        parts.append(loader);
    }
    return parts.join(QLatin1Char(' '));
}

/// 这一版自己的元数据能不能装进这个上下文。
///
/// ★ 两格**都空**时不判：接口有时候不给（尤其 CurseForge 的旧数据），"不知道"不是
///   "不兼容"。只要给了、而且目标值也给了，就必须对上。
bool versionMatches(const ResourceCatalog::VersionEntry &version, const QString &gameVersion,
                    const QString &loader) {
    if (!gameVersion.isEmpty() && !version.gameVersions.isEmpty()) {
        bool hit = false;
        for (const QString &candidate : version.gameVersions) {
            if (candidate.compare(gameVersion, Qt::CaseInsensitive) == 0) {
                hit = true;
                break;
            }
        }
        if (!hit) {
            return false;
        }
    }
    if (!loader.isEmpty() && !version.loaders.isEmpty()) {
        bool hit = false;
        for (const QString &candidate : version.loaders) {
            if (candidate.compare(loader, Qt::CaseInsensitive) == 0) {
                hit = true;
                break;
            }
        }
        if (!hit) {
            return false;
        }
    }
    return true;
}

/// 从列表里挑第一版「上下文对得上、而且真有文件可下」的。
bool chooseVersion(const QList<ResourceCatalog::VersionEntry> &versions,
                   const QString &gameVersion, const QString &loader,
                   ResourceCatalog::VersionEntry *out, QString *why) {
    if (versions.isEmpty()) {
        *why = planText("平台没有给出任何可用的版本。");
        return false;
    }
    bool sawMatch = false;
    QString fileWhy;
    for (const ResourceCatalog::VersionEntry &version : versions) {
        if (!versionMatches(version, gameVersion, loader)) {
            continue;
        }
        sawMatch = true;
        const ResourceCatalog::FileEntry *file = version.primaryFile();
        if (file == nullptr) {
            fileWhy = planText("这一版没有可下载的文件。");
            continue;
        }
        if (!file->unavailableNote.trimmed().isEmpty()) {
            fileWhy = file->unavailableNote;
            continue;
        }
        if (file->url.trimmed().isEmpty()) {
            fileWhy = planText("这一版的文件没有下载地址。");
            continue;
        }
        *out = version;
        return true;
    }
    if (sawMatch && !fileWhy.isEmpty()) {
        *why = fileWhy;
    } else {
        const QString context = contextText(gameVersion, loader);
        *why = context.isEmpty()
                   ? planText("版本列表里没有能装进这个实例的发行版。")
                   : planText("版本列表里没有匹配「%1」的发行版。").arg(context);
    }
    return false;
}

QString platformOfKey(const QString &key) {
    const int colon = key.indexOf(QLatin1Char(':'));
    return colon > 0 ? key.left(colon) : QString();
}

}  // namespace

int ModInstallPlan::requiredFailures() const {
    int count = 0;
    for (const ModInstallRow &row : rows) {
        if (row.required && row.isFailure()) {
            ++count;
        }
    }
    return count;
}

int ModInstallPlan::optionalFailures() const {
    int count = 0;
    for (const ModInstallRow &row : rows) {
        if (!row.required && row.isFailure()) {
            ++count;
        }
    }
    return count;
}

QList<ModInstallRow> ModInstallPlan::downloads() const {
    QList<ModInstallRow> out;
    for (const ModInstallRow &row : rows) {
        if (row.status == ModInstallStatus::Download) {
            out.append(row);
        }
    }
    return out;
}

ModInstallPlan planModInstall(const ModInstallRequest &request) {
    ModInstallPlan plan;

    QSet<QString> seen;
    QSet<QString> optionalSeen;
    // 父行 key → 它选中的那一版声明为 incompatible 的项目 key
    QHash<QString, QSet<QString>> incompatible;

    // 已经递进来的引用，按 key 找（替代项目要用）。
    QHash<QString, const ModResolveInput *> inputsByKey;
    for (const ModResolveInput &input : request.references) {
        const QString key = input.reference.key();
        if (!key.isEmpty() && !inputsByKey.contains(key)) {
            inputsByKey.insert(key, &input);
        }
    }

    std::function<void(const ModReference &, bool, const ModResolveInput *, int)> visit;
    visit = [&](const ModReference &reference, bool fromDependency,
                const ModResolveInput *given, int depth) {
        const QString key = reference.key();
        ModInstallRow row;
        row.reference = reference;
        row.key = key;
        row.required = reference.required;
        row.fromDependency = fromDependency;

        if (key.isEmpty()) {
            row.status = ModInstallStatus::Blocked;
            row.problem = planText("这一条引用认不出来（平台或项目号是空的）。");
            plan.rows.append(row);
            return;
        }
        if (seen.contains(key)) {
            return;  // 同一项目只算一次（引用重复 / 依赖环）
        }
        seen.insert(key);

        // ---------- 实例里已经有了吗 ----------
        if (request.installedKeys.contains(key)) {
            row.status = ModInstallStatus::AlreadyInstalled;
            row.title = resolvedTitle(given ? *given : ModResolveInput(), reference);
            plan.rows.append(row);
            return;
        }

        // ---------- 拿这一条的答复 ----------
        ModResolveInput local;
        const ModResolveInput *input = given;
        if (input == nullptr) {
            if (request.dependencyAnswers.contains(key)) {
                local.reference = reference;
                local.queried = true;
                local.versions = request.dependencyAnswers.value(key);
                local.title = request.dependencyTitles.value(key);
                input = &local;
            }
        }
        if (input != nullptr) {
            row.title = resolvedTitle(*input, reference);
        } else {
            row.title = resolvedTitle(ModResolveInput(), reference);
        }
        if (row.title.isEmpty()) {
            row.title = reference.projectId;
        }

        if (input == nullptr) {
            row.status = ModInstallStatus::Unknown;
            row.problem = fromDependency
                              ? planText("还没问到这一个必要前置的版本列表。")
                              : planText("还没问到平台：这一条现在不能下。");
            plan.rows.append(row);
            return;
        }
        if (!input->queried) {
            row.status = ModInstallStatus::Unknown;
            row.problem = planText("还没轮到问平台。");
            plan.rows.append(row);
            return;
        }
        if (input->queryFailed) {
            row.status = ModInstallStatus::Unknown;
            row.problem = input->queryProblem.trimmed().isEmpty()
                              ? planText("问平台没成（网络或接口没有回答）。")
                              : input->queryProblem.trimmed();
            plan.rows.append(row);
            return;
        }

        const QString gameVersion = effectiveGameVersion(request, reference);
        const QString loader = effectiveLoader(request, reference);
        ResourceCatalog::VersionEntry chosen;
        QString why;
        if (!chooseVersion(input->versions, gameVersion, loader, &chosen, &why)) {
            // ---------- 缺失：先看有没有用户确认过的替代 ----------
            const QList<ModReference> alternatives = request.alternativesByKey.value(key);
            ModReference confirmed;
            bool hasConfirmed = false;
            for (const ModReference &candidate : alternatives) {
                if (!candidate.key().isEmpty() &&
                    request.confirmedAlternatives.contains(candidate.key())) {
                    confirmed = candidate;
                    hasConfirmed = true;
                    break;
                }
            }
            if (hasConfirmed) {
                const ModResolveInput *altInput = inputsByKey.value(confirmed.key());
                const QString altTitle =
                    altInput != nullptr ? resolvedTitle(*altInput, confirmed)
                                        : (confirmed.name.isEmpty() ? confirmed.projectId
                                                                    : confirmed.name);
                // ★ 替代项也走同一条解析路：它自己照样要过"上下文 + 文件"那两关，
                //   过不了就是它自己的失败（原那一条**不再单出一行**，免得同一条在表上
                //   出现两次、用户不知道该看哪一行）。
                visit(confirmed, fromDependency, altInput, depth);
                bool marked = false;
                for (int index = plan.rows.size() - 1; index >= 0; --index) {
                    ModInstallRow &candidate = plan.rows[index];
                    if (candidate.key != confirmed.key()) {
                        continue;
                    }
                    candidate.substituted = true;
                    candidate.substitutedKey = confirmed.key();
                    candidate.details.append(modInstallSubstitutedText(altTitle));
                    marked = true;
                    break;
                }
                if (!marked) {
                    // 替身那一行本来就在表上（例如它自己也是预设里的一条）时，
                    // 原条目不能就这么消失 —— 留一行点明"按你确认的那一条走"。
                    row.status = ModInstallStatus::Missing;
                    row.problem = why;
                    row.details.append(
                        planText("按你确认换成了「%1」，见它在表上那一行。").arg(altTitle));
                    plan.rows.append(row);
                }
                return;
            }
            row.status = ModInstallStatus::Missing;
            row.problem = why;
            if (!alternatives.isEmpty()) {
                row.status = ModInstallStatus::AlternativeNeedsConfirm;
                QStringList names;
                for (const ModReference &candidate : alternatives) {
                    names.append(candidate.name.trimmed().isEmpty() ? candidate.projectId
                                                                    : candidate.name.trimmed());
                }
                row.details.append(modInstallAlternativeText(names));
            }
            plan.rows.append(row);
            return;
        }

        row.version = chosen;
        const ResourceCatalog::FileEntry *file = chosen.primaryFile();
        row.file = *file;
        row.status = ModInstallStatus::Download;
        plan.rows.append(row);

        // ---------- 把这一版声明的依赖折进来 ----------
        for (const ResourceCatalog::Dependency &dependency : chosen.dependencies) {
            if (dependency.projectId.trimmed().isEmpty()) {
                continue;
            }
            const QString platform = platformOfKey(key);
            ModReference depRef;
            depRef.platform = platform;
            depRef.projectId = dependency.projectId.trimmed();
            depRef.name = request.dependencyTitles.value(depRef.key());
            // ★ 必要前置失败会不会拖垮整份，跟着**父行**走：可选模组的必要前置失败
            //   只算可选失败（用户本来就没说要这个模组）。
            depRef.required = reference.required;
            const QString depKey = depRef.key();

            if (dependency.type == QLatin1String("embedded")) {
                continue;  // 已经打进那个 jar 里，再下一份反而两份
            }
            if (dependency.type == QLatin1String("optional")) {
                if (!optionalSeen.contains(depKey)) {
                    optionalSeen.insert(depKey);
                    plan.optionalDependencyKeys.append(depKey);
                }
                continue;
            }
            if (dependency.type == QLatin1String("incompatible")) {
                incompatible[key].insert(depKey);
                continue;
            }
            if (!dependency.required()) {
                continue;
            }
            if (request.installedKeys.contains(depKey) || seen.contains(depKey)) {
                continue;
            }
            if (depth >= request.maxDependencyDepth) {
                ModInstallRow cut;
                cut.reference = depRef;
                cut.key = depKey;
                cut.title = depRef.name.isEmpty() ? depRef.projectId : depRef.name;
                cut.required = depRef.required;
                cut.fromDependency = true;
                cut.status = ModInstallStatus::Unknown;
                cut.problem = planText("前置链超过 %1 层，没有继续往下展开。")
                                  .arg(request.maxDependencyDepth);
                plan.rows.append(cut);
                continue;
            }
            visit(depRef, true, nullptr, depth + 1);
        }
    };

    for (const ModResolveInput &input : request.references) {
        visit(input.reference, false, &input, 0);
    }

    // ---------- 冲突复核：声明了 incompatible 的那一项在这一趟里 / 已经装着 ----------
    QSet<QString> planned;
    QHash<QString, QString> titleByKey;
    for (const ModInstallRow &row : plan.rows) {
        titleByKey.insert(row.key, row.title);
        if (row.status == ModInstallStatus::Download ||
            row.status == ModInstallStatus::AlreadyInstalled) {
            planned.insert(row.key);
        }
    }
    for (ModInstallRow &row : plan.rows) {
        if (row.key.isEmpty()) {
            continue;
        }
        const QSet<QString> bad = incompatible.value(row.key);
        for (const QString &other : bad) {
            if (other == row.key) {
                continue;
            }
            const bool inPlan = planned.contains(other);
            const bool onDisk = request.installedKeys.contains(other);
            if (!inPlan && !onDisk) {
                continue;
            }
            const QString otherTitle = titleByKey.value(other, other);
            row.status = ModInstallStatus::Incompatible;
            row.problem = planText("这一版声明和「%1」不兼容，没有把它排进这一趟。")
                              .arg(otherTitle);
            plan.conflicts.append(
                planText("「%1」和「%2」不能一起装。").arg(row.title, otherTitle));
            break;
        }
    }

    // ---------- 总账 ----------
    for (const ModInstallRow &row : plan.rows) {
        switch (row.status) {
        case ModInstallStatus::Download:
            ++plan.download;
            break;
        case ModInstallStatus::AlreadyInstalled:
            ++plan.alreadyInstalled;
            break;
        case ModInstallStatus::Missing:
            ++plan.missing;
            break;
        case ModInstallStatus::Unknown:
            ++plan.unknown;
            break;
        case ModInstallStatus::Incompatible:
            ++plan.incompatible;
            break;
        case ModInstallStatus::AlternativeNeedsConfirm:
            ++plan.needsConfirm;
            break;
        case ModInstallStatus::Blocked:
            ++plan.blocked;
            break;
        }
        if (row.substituted) {
            ++plan.substituted;
        }
    }
    plan.ok = !plan.hasRequiredFailure();
    return plan;
}

DownloadVerification verifyDownloadedFile(const QString &path, qint64 expectedSize,
                                          const QString &expectedSha1) {
    DownloadVerification out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        out.problem = planText("读不动刚下下来的文件「%1」：%2").arg(path, file.errorString());
        return out;
    }
    out.size = file.size();

    QCryptographicHash hash(QCryptographicHash::Sha1);
    qint64 total = 0;
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1 << 16);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            out.problem = planText("读「%1」读到一半出错了：%2").arg(path, file.errorString());
            return out;
        }
        total += chunk.size();
        hash.addData(chunk);
    }
    out.size = total;
    out.sha1 = QString::fromLatin1(hash.result().toHex());

    if (expectedSize >= 0 && total != expectedSize) {
        out.problem = planText("大小对不上：期望 %1 字节，实际 %2 字节。")
                          .arg(expectedSize)
                          .arg(total);
        return out;
    }
    if (!expectedSha1.trimmed().isEmpty()) {
        if (out.sha1.compare(expectedSha1.trimmed(), Qt::CaseInsensitive) != 0) {
            out.problem = planText("sha1 对不上：期望 %1，实际 %2。")
                              .arg(expectedSha1.trimmed(), out.sha1);
            return out;
        }
        out.ok = true;
        return out;
    }
    // 接口没给哈希：大小对上了也不能说"校验通过"。
    out.ok = true;
    out.unverified = true;
    out.problem = planText("平台没有给出这个文件的 sha1，只对过大小。");
    return out;
}

// ---------------------------------------------------------------------------
// 字
// ---------------------------------------------------------------------------

QString modInstallStatusText(ModInstallStatus status) {
    switch (status) {
    case ModInstallStatus::Download:
        return planText("下载");
    case ModInstallStatus::AlreadyInstalled:
        return planText("已经在实例里");
    case ModInstallStatus::Missing:
        return planText("缺失");
    case ModInstallStatus::Unknown:
        return planText("暂无法确认");
    case ModInstallStatus::Incompatible:
        return planText("不兼容");
    case ModInstallStatus::AlternativeNeedsConfirm:
        return planText("等你确认替代项目");
    case ModInstallStatus::Blocked:
        return planText("做不了");
    }
    return QString();
}

QString modInstallSummaryText(const ModInstallPlan &plan) {
    QStringList parts;
    if (plan.download > 0) {
        parts.append(planText("下载 %1 个").arg(plan.download));
    }
    if (plan.alreadyInstalled > 0) {
        parts.append(planText("已经在实例里 %1 个").arg(plan.alreadyInstalled));
    }
    if (plan.missing > 0) {
        parts.append(planText("缺失 %1 个").arg(plan.missing));
    }
    if (plan.unknown > 0) {
        parts.append(planText("暂无法确认 %1 个").arg(plan.unknown));
    }
    if (plan.incompatible > 0) {
        parts.append(planText("不兼容 %1 个").arg(plan.incompatible));
    }
    if (plan.needsConfirm > 0) {
        parts.append(planText("等你确认替代项目 %1 个").arg(plan.needsConfirm));
    }
    if (plan.blocked > 0) {
        parts.append(planText("做不了 %1 个").arg(plan.blocked));
    }
    const int required = plan.requiredFailures();
    if (required > 0) {
        parts.append(planText("其中必要项失败 %1 个").arg(required));
    }
    if (parts.isEmpty()) {
        return planText("没有要动的东西。");
    }
    return parts.join(planText("，")) + planText("。");
}

QString modInstallSubstitutedText(const QString &title) {
    return planText("按你确认换成了「%1」。").arg(title);
}

QString modInstallAlternativeText(const QStringList &names) {
    return planText("这几个有替代项目，换不换要你点头：%1。")
        .arg(names.join(planText("、")));
}

QString modInstallPartialText() {
    return planText("实例可用，预设部分应用。");
}

}  // namespace Presets
