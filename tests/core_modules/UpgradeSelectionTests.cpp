/**
 * @file UpgradeSelectionTests.cpp
 * @brief 迁移式升级的九类选择：稳定键、默认值、指纹与依赖提示（纯逻辑，不碰盘）。
 */
#include "Support.hpp"

#include "operations/UpgradePageState.hpp"
#include "operations/UpgradeSelection.hpp"

int runUpgradeSelectionTests(const QString &) {
    using UpgradeSelection::Category;
    using UpgradeSelection::Plan;
    using UpgradeSelection::State;

    const QList<Category> all = UpgradeSelection::categories();
    check(all.size() == 9, "US1 迁移内容有九类", QString::number(all.size()));
    check(all.first().key == QStringLiteral("saves"), "US1 第一类是存档", all.first().key);
    {
        QSet<QString> seen;
        for (const Category &category : all) {
            seen.insert(category.key);
        }
        check(seen.size() == 9, "US1 九个键互不重复", QString::number(seen.size()));
    }
    check(UpgradeSelection::known(QStringLiteral("schematics")),
          "US1 原理图是一等类别（不是塞在别类里的子项）");
    check(!UpgradeSelection::known(QStringLiteral("datapacks")),
          "US1 存档内数据包不单列一类（随存档走，避免重复复制）");
    check(!UpgradeSelection::known(QStringLiteral("logs")), "US1 不认识日志这一类");

    bool screenshotsDefault = true;
    bool schematicsDefault = true;
    bool savesDefault = false;
    for (const Category &category : all) {
        if (category.key == QStringLiteral("screenshots")) screenshotsDefault = category.defaultSelected;
        if (category.key == QStringLiteral("schematics")) schematicsDefault = category.defaultSelected;
        if (category.key == QStringLiteral("saves")) savesDefault = category.defaultSelected;
    }
    check(!screenshotsDefault && !schematicsDefault,
          "US2 截图 / 原理图默认不勾（记录与素材，不是运行必需品）");
    check(savesDefault, "US2 存档默认勾");

    State state;
    check(!state.set(QStringLiteral("not-a-category"), true),
          "US3 未知类别不接受（不接受也不改状态）");
    check(state.selected.isEmpty(), "US3 拒绝未知类别之后状态是空的");

    state.set(QStringLiteral("servers"), true);
    state.set(QStringLiteral("saves"), true);
    check(state.ordered() == QStringList{QStringLiteral("saves"), QStringLiteral("servers")},
          "US4 选择顺序按九类的固定顺序，不按勾的先后", state.ordered().join(QLatin1Char(',')));
    state.set(QStringLiteral("saves"), false);
    check(state.ordered() == QStringList{QStringLiteral("servers")},
          "US5 取消一类只去掉那一类");
    state.selectAll();
    check(state.ordered().size() == 9, "US5 全选是九类");
    state.clear();
    check(state.ordered().isEmpty(), "US5 清空之后一个都不剩");

    // makePlan：只收认得且盘上真有的；默认值来自 categories()。
    QList<TransferPlanner::Item> items;
    {
        TransferPlanner::Item saves;
        saves.key = QStringLiteral("saves");
        saves.exists = true;
        items.append(saves);
        TransferPlanner::Item screenshots;
        screenshots.key = QStringLiteral("screenshots");
        screenshots.exists = true;
        items.append(screenshots);
        TransferPlanner::Item modsMissing;
        modsMissing.key = QStringLiteral("mods");
        modsMissing.exists = false;
        items.append(modsMissing);
    }
    const Plan plan = UpgradeSelection::makePlan(items, QStringLiteral("src"), QStringLiteral("dst"));
    check(plan.ok, "US6 有内容时计划可用");
    check(plan.state.ordered() == QStringList{QStringLiteral("saves")},
          "US6 默认只勾「默认勾 + 盘上真有」的项", plan.state.ordered().join(QLatin1Char(',')));

    const Plan empty = UpgradeSelection::makePlan({}, QStringLiteral("src"), QStringLiteral("dst"));
    check(!empty.ok && !empty.problem.isEmpty(), "US6 没有可迁移内容时不装作能搬");

    const QString first = UpgradeSelection::fingerprint(plan);
    check(first.size() == 40, "US7 指纹是 sha1 十六进制", first);
    Plan changedTarget = plan;
    changedTarget.targetId = QStringLiteral("dst-2");
    check(UpgradeSelection::fingerprint(changedTarget) != first,
          "US7 目标一变指纹就变（旧检查与旧选择作废的依据）");
    Plan changedSelection = plan;
    changedSelection.state.set(QStringLiteral("config"), true);
    check(UpgradeSelection::fingerprint(changedSelection) != first,
          "US7 选的类别一变指纹也变");
    Plan sameAgain = plan;
    check(UpgradeSelection::fingerprint(sameAgain) == first, "US7 同一份计划指纹稳定");

    Plan warnings = plan;
    warnings.state.selectAll();
    const QStringList allWarnings = UpgradeSelection::warnings(warnings);
    // 全选时"配置配模组""设置配资源包"这两条前提都满足了，只剩光影那条提醒。
    check(allWarnings.size() == 1 && allWarnings.first().contains(QStringLiteral("光影")),
          "US8 全选时只剩光影那条提示", allWarnings.join(QLatin1Char('|')));
    Plan configOnly;
    configOnly.state.set(QStringLiteral("config"), true);
    const QStringList configWarnings = UpgradeSelection::warnings(configOnly);
    check(configWarnings.size() == 1 && configWarnings.first().contains(QStringLiteral("配置")),
          "US8 只搬配置不搬模组时会提醒", configWarnings.join(QLatin1Char('|')));
    Plan configWithMods;
    configWithMods.state.set(QStringLiteral("config"), true);
    configWithMods.state.set(QStringLiteral("mods"), true);
    check(UpgradeSelection::warnings(configWithMods).isEmpty(),
          "US8 模组也搬了，配置那条提醒就消失",
          UpgradeSelection::warnings(configWithMods).join(QLatin1Char('|')));

    // US9：planner 的键 → 九类；体积/文件数按"选中的类别"合计。
    check(UpgradeSelection::categoryKeyForPlannerKey(QStringLiteral("structure"))
              == QStringLiteral("schematics"),
          "US9 现有的 structure 行归入「原理图」这一类");
    check(UpgradeSelection::categoryKeyForPlannerKey(QStringLiteral("defaultconfigs"))
              == QStringLiteral("config"),
          "US9 defaultconfigs 归入「Mod 配置」");
    check(UpgradeSelection::categoryKeyForPlannerKey(QStringLiteral("logs")).isEmpty()
              && UpgradeSelection::categoryKeyForPlannerKey(QStringLiteral("crashreports")).isEmpty(),
          "US9 日志 / 崩溃报告不属于九类");
    {
        TransferPlanner::Item config;
        config.key = QStringLiteral("config");
        config.exists = true;
        config.fileCount = 3;
        config.bytes = 300;
        TransferPlanner::Item defaultConfigs;
        defaultConfigs.key = QStringLiteral("defaultconfigs");
        defaultConfigs.exists = true;
        defaultConfigs.fileCount = 2;
        defaultConfigs.bytes = 200;
        TransferPlanner::Item structures;
        structures.key = QStringLiteral("structure");
        structures.exists = true;
        structures.fileCount = 1;
        structures.bytes = 100;
        TransferPlanner::Item shots;
        shots.key = QStringLiteral("screenshots");
        shots.exists = true;
        shots.fileCount = 5;
        shots.bytes = 500;
        const QList<TransferPlanner::Item> sample{config, defaultConfigs, structures, shots};
        check(UpgradeSelection::itemsFor(sample, QStringLiteral("config")).size() == 2,
              "US9 config 这一类同时命中 config 与 defaultconfigs 两行");
        const UpgradeSelection::Amounts configAmounts =
            UpgradeSelection::amounts(sample, {QStringLiteral("config")});
        check(configAmounts.files == 5 && configAmounts.bytes == 500,
              "US9 两类配置的 3+2 个文件合成一处",
              QStringLiteral("%1 个 / %2 字节").arg(configAmounts.files).arg(configAmounts.bytes));
        Plan picked;
        picked.state.set(QStringLiteral("config"), true);
        picked.state.set(QStringLiteral("screenshots"), true);
        const UpgradeSelection::Amounts selected = UpgradeSelection::selectedAmounts(picked, sample);
        check(selected.files == 10 && selected.bytes == 1000,
              "US9 选中合计 = 配置两行 + 截图",
              QStringLiteral("%1 个 / %2 字节").arg(selected.files).arg(selected.bytes));
        picked.state.set(QStringLiteral("screenshots"), false);
        const UpgradeSelection::Amounts withoutShots =
            UpgradeSelection::selectedAmounts(picked, sample);
        check(withoutShots.files == 5 && withoutShots.bytes == 500,
              "US9 取消截图之后合计只剩配置两行");
    }

    // US10：页面状态——目标/名字/选择/报告四道闸。
    {
        using UpgradePageState::Blocker;
        UpgradePageState::State page;
        page.sourceId = QStringLiteral("src-1");
        page.sourceGameVersion = QStringLiteral("1.20.1");
        page.sourceLoaderKey = QStringLiteral("fabric");
        page.sourceLoaderVersion = QStringLiteral("0.15.0");

        check(UpgradePageState::firstBlocker(page) == Blocker::TargetIncomplete,
              "US10 目标版本还没选 → 不许确认");
        check(UpgradePageState::setTargetVersion(page, QStringLiteral("1.21.4")),
              "US10 选目标版本");
        check(!UpgradePageState::setTargetVersion(page, QStringLiteral("1.21.4")),
              "US10 改成同一个值不算变化");
        check(UpgradePageState::firstBlocker(page) == Blocker::NameEmpty,
              "US10 名字空着 → 不许确认");
        check(!UpgradePageState::setInstanceName(page, QStringLiteral("  "), page.sourceId, {}),
              "US10 只填空格也算空名字");
        check(UpgradePageState::setInstanceName(page, QStringLiteral("新实例"), page.sourceId,
                                                {QStringLiteral("旧实例")}),
              "US10 不重名的名字收下");
        check(!UpgradePageState::setInstanceName(page, QStringLiteral("旧实例"), page.sourceId,
                                                {QStringLiteral("旧实例")}),
              "US10 和现有实例重名 → 拒收");
        check(!UpgradePageState::setInstanceName(page, page.sourceId, page.sourceId, {}),
              "US10 和来源实例同名 → 拒收");
        check(UpgradePageState::setInstanceName(page, QStringLiteral("新实例"), page.sourceId,
                                                {QStringLiteral("旧实例")}),
              "US10 名字改回合法值");

        check(UpgradePageState::firstBlocker(page) == Blocker::NoSelection,
              "US10 一类都没选 → 不许确认");
        page.selection.state.set(QStringLiteral("saves"), true);
        check(UpgradePageState::firstBlocker(page) == Blocker::ReportMissing,
              "US10 没检查过兼容性 → 不许确认");
        UpgradePageState::markReportChecked(page);
        check(UpgradePageState::firstBlocker(page) == Blocker::None,
              "US10 目标 + 名字 + 选择 + 报告齐了 → 可以确认");

        const QString before = UpgradePageState::planFingerprint(page);
        UpgradePageState::setInstanceName(page, QStringLiteral("换个名字"), page.sourceId, {});
        check(UpgradePageState::planFingerprint(page) == before,
              "US10 改名字不影响检查指纹（只有目标与选择参与）");
        check(UpgradePageState::firstBlocker(page) == Blocker::None,
              "US10 改名字之后仍然可以确认");

        UpgradePageState::setTargetVersion(page, QStringLiteral("1.21.5"));
        check(UpgradePageState::planFingerprint(page) != before,
              "US10 目标版本一改指纹就变");
        check(UpgradePageState::firstBlocker(page) == Blocker::ReportStale,
              "US10 报告过期 → 要重新检查");
        UpgradePageState::markReportChecked(page);
        check(UpgradePageState::firstBlocker(page) == Blocker::None,
              "US10 重新检查之后恢复可确认");

        page.selection.state.set(QStringLiteral("saves"), false);
        check(UpgradePageState::firstBlocker(page) == Blocker::NoSelection,
              "US10 取消最后一类 → 回到「至少选一类」那道闸");
        check(!UpgradePageState::blockerText(Blocker::NameTaken).isEmpty(),
              "US10 重名也有自己的说明");
    }

    // US11：选中类别 → 要复制的顶层路径（U5 的复制链路按它过滤）。
    {
        Plan roots;
        roots.state.set(QStringLiteral("config"), true);
        roots.state.set(QStringLiteral("schematics"), true);
        roots.state.set(QStringLiteral("servers"), true);
        const QStringList selectedRoots = UpgradeSelection::selectedRoots(roots);
        check(selectedRoots
                  == QStringList{QStringLiteral("config"), QStringLiteral("defaultconfigs"),
                                 QStringLiteral("structures"), QStringLiteral("servers.dat")},
              "US11 配置给两条、原理图给 structures、服务器列表是单个文件",
              selectedRoots.join(QLatin1Char(',')));
        Plan none;
        check(UpgradeSelection::selectedRoots(none).isEmpty(), "US11 一类没选 → 不复制任何顶层路径");
        Plan savesOnly;
        savesOnly.state.set(QStringLiteral("saves"), true);
        check(UpgradeSelection::selectedRoots(savesOnly) == QStringList{QStringLiteral("saves")},
              "US11 只选了存档 → 只复制 saves/");
    }

    // US12：类别 → 顶层路径；普通树里没有链接（不存在的类回空）。
    check(UpgradeSelection::rootsFor(QStringLiteral("config"))
              == QStringList{QStringLiteral("config"), QStringLiteral("defaultconfigs")},
          "US12 配置类的路径是两条");
    check(UpgradeSelection::rootsFor(QStringLiteral("unknown")).isEmpty(),
          "US12 未知类别没有路径");
    {
        QTemporaryDir linkTree;
        check(linkTree.isValid(), "US12 临时目录建得出来");
        QDir().mkpath(linkTree.filePath(QStringLiteral("saves/W")));
        QFile plain(linkTree.filePath(QStringLiteral("saves/W/level.dat")));
        check(plain.open(QIODevice::WriteOnly), "US12 摆一个普通文件");
        plain.write("w");
        plain.close();
        const UpgradeSelection::Links links =
            UpgradeSelection::scanLinks(linkTree.path(), {QStringLiteral("saves")});
        check(links.count == 0 && links.samples.isEmpty(),
              "US12 普通树里链接数为 0", QString::number(links.count));
    }

    // US13：来源实例被占用时，页面要先挡住（不能等点确认才报错）。
    {
        using UpgradePageState::Blocker;
        UpgradePageState::State busy;
        busy.sourceId = QStringLiteral("src");
        busy.target.gameVersion = QStringLiteral("1.21.4");
        busy.target.instanceName = QStringLiteral("新实例");
        busy.selection.state.set(QStringLiteral("saves"), true);
        UpgradePageState::markReportChecked(busy);
        check(UpgradePageState::firstBlocker(busy) == Blocker::None,
              "US13 先摆一份可确认的状态");
        busy.instanceBusy = true;
        check(UpgradePageState::firstBlocker(busy) == Blocker::InstanceBusy,
              "★★ US13 实例被占用时挡住确认");
        check(!UpgradePageState::blockerText(Blocker::InstanceBusy).isEmpty(),
              "US13 而且说得出为什么");
        busy.instanceBusy = false;
        check(UpgradePageState::firstBlocker(busy) == Blocker::None,
              "US13 占用解除之后恢复可确认");
    }

    // US14：勾了 Mod 配置却没勾 Mod 文件 —— 显式挡住，不去猜归属。
    {
        using UpgradePageState::Blocker;
        UpgradePageState::State page;
        page.target.gameVersion = QStringLiteral("1.21.4");
        page.target.instanceName = QStringLiteral("新实例");
        page.selection.state.set(QStringLiteral("config"), true);
        UpgradePageState::markReportChecked(page);
        check(UpgradePageState::firstBlocker(page) == Blocker::ConfigWithoutMods,
              "★★ US14 只勾配置不勾模组时挡住确认");
        check(!UpgradePageState::blockerText(Blocker::ConfigWithoutMods).isEmpty(),
              "US14 而且说得出为什么");
        page.selection.state.set(QStringLiteral("mods"), true);
        UpgradePageState::markReportChecked(page);
        check(UpgradePageState::firstBlocker(page) == Blocker::None,
              "US14 把模组一起勾上就放行");
        page.selection.state.set(QStringLiteral("config"), false);
        UpgradePageState::markReportChecked(page);
        check(UpgradePageState::firstBlocker(page) == Blocker::None,
              "US14 不搬配置、只搬模组时没有这条阻断");
    }

    return g_failures == 0 ? 0 : 1;
}










