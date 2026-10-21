#include "Support.hpp"

void runPresetStressTests() {
    // ---------- PN1 大量预设：建得出来、列得回来、次序就是下标那一次序 ----------
    {
        QTemporaryDir root;
        check(root.isValid(), "PN1 建得出临时预设根目录");
        if (root.isValid()) {
            QElapsedTimer timer;
            timer.start();
            QStringList ids;
            for (int i = 0; i < 150; ++i) {
                const Presets::PresetManifest manifest = Presets::create(
                    root.path(), QStringLiteral("预设 %1").arg(i), QString(),
                    Presets::PresetCategory::GameSettings);
                if (!manifest.id.isEmpty()) {
                    ids.append(manifest.id);
                }
            }
            check(ids.size() == 150, "★ PN1 连着建 150 份预设，一份都没丢",
                  QString::number(ids.size()));
            QList<Presets::BrokenPreset> broken;
            const QList<Presets::PresetSummary> summaries =
                Presets::list(root.path(), &broken);
            check(summaries.size() == 150 && broken.isEmpty(),
                  "★★ PN1 150 份全都读得回来（没有连坐）",
                  QStringLiteral("listed=%1 broken=%2")
                      .arg(summaries.size())
                      .arg(broken.size()));
            check(summaries.size() == 150 && !summaries.isEmpty() &&
                      summaries.first().id == ids.first() &&
                      summaries.last().id == ids.last(),
                  "PN1 列表次序 = 下标次序");
            check(timer.elapsed() < 30000,
                  "★ PN1 150 份的新建 + 列表没有把主线程拖死（粗检 30 秒）",
                  QStringLiteral("%1 ms").arg(timer.elapsed()));
        }
    }

    // ---------- PN2 大量引用：判据层线性收尾、去重不漏 ----------
    {
        ResourceCatalog::VersionEntry version;
        version.id = QStringLiteral("bulk-1");
        version.gameVersions = {QStringLiteral("1.20.1")};
        version.loaders = {QStringLiteral("fabric")};
        ResourceCatalog::FileEntry file;
        file.url = QStringLiteral("https://cdn.example.invalid/bulk.jar");
        file.filename = QStringLiteral("bulk.jar");
        file.primary = true;
        version.files.append(file);

        Presets::ModInstallRequest request;
        request.gameVersion = QStringLiteral("1.20.1");
        request.loader = QStringLiteral("fabric");
        for (int i = 0; i < 200; ++i) {
            Presets::ModReference reference;
            reference.platform = QStringLiteral("modrinth");
            reference.projectId = QStringLiteral("bulk-%1").arg(i);
            reference.name = QStringLiteral("批量 %1").arg(i);
            Presets::ModResolveInput input;
            input.reference = reference;
            input.queried = true;
            input.title = reference.name;
            input.versions = {version};
            request.references.append(input);
            request.references.append(input);  // 每一条都再来一遍
        }
        QElapsedTimer timer;
        timer.start();
        const Presets::ModInstallPlan plan = Presets::planModInstall(request);
        check(plan.rows.size() == 200 && plan.download == 200,
              "★★ PN2 400 条引用（两两重复）⇒ 200 行、200 个下载",
              QStringLiteral("rows=%1 download=%2")
                  .arg(plan.rows.size())
                  .arg(plan.download));
        check(plan.ok && plan.downloads().size() == 200,
              "PN2 计划可继续，下载清单和总账对得上");
        check(timer.elapsed() < 20000,
              "★ PN2 几百条引用算完不卡主线程（粗检 20 秒）",
              QStringLiteral("%1 ms").arg(timer.elapsed()));
    }

    // ---------- PN3 大量引用 + 并发上限：在途数不失控 ----------
    {
        SlowProvider provider;
        provider.delayMs = 1;
        Presets::CompatibilityChecker checker;
        checker.setProvider(&provider);
        checker.setOptions({8, 5000, 0});

        QList<Presets::ModReference> references;
        references.reserve(300);
        for (int i = 0; i < 300; ++i) {
            references.append(
                makeRef(QStringLiteral("modrinth"), QStringLiteral("stress-%1").arg(i)));
        }
        Presets::CompatibilityContext context;
        context.gameVersion = QStringLiteral("1.21.1");
        checker.start(references, context);
        check(waitForChecker(checker), "★★ PN3 300 条引用收得了尾（不许挂住）");
        check(provider.askedCount == 300, "PN3 每一条都问到了",
              QString::number(provider.askedCount));
        check(provider.maxSeen <= 8 && provider.maxSeen >= 1,
              "★★★ PN3 在途封顶：上限 8 ⇒ 任何时刻不超过 8",
              QString::number(provider.maxSeen));
        check(checker.summary().matched == 300 && checker.summary().pending == 0,
              "PN3 300 条都落成结论，没有留在「进行中」",
              checker.summary().text());
    }

    // ---------- PN4 连换几十代：旧回调一条都不许落 ----------
    {
        ControlledProvider provider;
        Presets::CompatibilityChecker checker;
        checker.setProvider(&provider);
        checker.setOptions({4, 5000, 0});

        Presets::CompatibilityContext context;
        context.gameVersion = QStringLiteral("1.21.1");
        for (int i = 0; i < 40; ++i) {
            checker.start(
                {makeRef(QStringLiteral("modrinth"), QStringLiteral("switch-%1").arg(i))},
                context);
        }
        check(provider.pending.size() == 40,
              "PN4 四十代各发出一发（每一代都记着收尾）",
              QString::number(provider.pending.size()));
        const Presets::CompatibilityProvider::Reply fresh = provider.pending.takeLast();
        for (const Presets::CompatibilityProvider::Reply &stale : provider.pending) {
            stale(Presets::CompatibilityState::Matched, QStringLiteral("旧的那一代"));
        }
        provider.pending.clear();
        QCoreApplication::processEvents();
        check(checker.running() && checker.summary().matched == 0 &&
                  checker.summary().pending == 1,
              "★★★ PN4 三十九发旧回调全部丢掉，界面还停在最新的那一代",
              checker.summary().text());

        fresh(Presets::CompatibilityState::Missing, QStringLiteral("最后那一代"));
        check(waitForChecker(checker), "PN4 最新一代收得了尾");
        check(checker.summary().missing == 1 && !checker.results().isEmpty() &&
                  checker.results().first().projectId == QStringLiteral("switch-39"),
              "★★ PN4 落下来的只有最后一代的结论",
              checker.summary().text());
    }

    // ---------- PN5 失败之后再查：不会把上一趟的「说不清」当成缓存结论 ----------
    {
        ImmediateProvider provider;
        provider.answers.insert(QStringLiteral("modrinth:flaky"),
                                Presets::CompatibilityState::Unknown);
        Presets::CompatibilityChecker checker;
        checker.setProvider(&provider);
        checker.setOptions({2, 5000, 10 * 60 * 1000});

        Presets::CompatibilityContext context;
        context.gameVersion = QStringLiteral("1.21.1");
        checker.start({makeRef(QStringLiteral("modrinth"), QStringLiteral("flaky"))}, context);
        // 先收尾再念总账：函数实参的求值次序没保证，写在一个表达式里会念到"检查中"。
        const bool firstDone = waitForChecker(checker);
        check(firstDone && checker.summary().unknown == 1,
              "PN5 第一趟问回来「说不清」", checker.summary().text());

        provider.answers.insert(QStringLiteral("modrinth:flaky"),
                                Presets::CompatibilityState::Matched);
        checker.start({makeRef(QStringLiteral("modrinth"), QStringLiteral("flaky"))}, context);
        check(waitForChecker(checker), "PN5 第二趟也收得了尾");
        check(provider.asked.size() == 2 && checker.summary().matched == 1,
              "★★ PN5 「说不清」不进缓存 ⇒ 第二趟真的重新问了",
              QStringLiteral("asked=%1 %2")
                  .arg(provider.asked.size())
                  .arg(checker.summary().text()));
    }

    // ---------- PN6 关闭那一页：检查器先没，晚到的回调不许碰它 ----------
    {
        ControlledProvider provider;
        auto *checker = new Presets::CompatibilityChecker;
        checker->setProvider(&provider);
        checker->setOptions({2, 5000, 0});
        Presets::CompatibilityContext context;
        context.gameVersion = QStringLiteral("1.21.1");
        checker->start({makeRef(QStringLiteral("modrinth"), QStringLiteral("late"))}, context);
        check(provider.pending.size() == 1, "PN6 有一发查在途");
        const Presets::CompatibilityProvider::Reply late = provider.pending.takeFirst();
        delete checker;
        late(Presets::CompatibilityState::Matched, QStringLiteral("晚了"));
        QCoreApplication::processEvents();
        check(true, "★★★ PN6 检查器先析构，晚到的回调被挡在门外（整进程还活着）");
    }
}

