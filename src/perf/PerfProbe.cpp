/**
 * @file PerfProbe.cpp
 * @brief 测量构建（`-DWTFML_PERF_PROBE=ON`）里的界面探针：按真实入口发动作、记耗时。
 *
 * 三件东西：
 *  1. **按用户点的那一颗按钮发动作**（`TitleBar::navButton()`、设置页那个主题下拉框），
 *     不绕过业务层 —— 翻页之后那一串懒加载、列表重建都算进时间里；
 *  2. 每一步量两段：动作本身（同步那部分）+ `repaint()`（把脏的整窗立刻画一遍）。
 *     后者是**上限**：真实使用里只重画脏的那几块，但上限也可以横向比较；
 *  3. 一个 20 ms 的看门狗定时器：主线程被什么占住时它会晚到，晚到多少就是阻塞多少
 *     （报告里 `maxGapMs` 含 20 ms 的节拍本身，`maxStallMs` 是减掉节拍之后的值）。
 *
 * 只有一条入口：`attach()`。argv 里没有 `--wtfml-perf-probe=<文件>` 时立即返回，
 * 所以这个 exe 不测量时和普通启动器没有区别。
 *
 * 跑法见 `tools/perf-startup.ps1 -Probe`（沙盒 + 注册表还原那一套都在那边）。
 */

#include "perf/PerfProbe.hpp"

#include "perf/StallSampler.hpp"

#include "MainWindow.hpp"
#include "MainWindowUI.hpp"
#include "Theme.hpp"
#include "TitleBar.hpp"
#include "TaskCenterPanel.hpp"
#include "PresetManagePage.hpp"
#include "presets/PresetStore.hpp"
#include "presets/PresetCatalog.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include "util/AppPaths.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLineEdit>
#include <QPointer>
#include <QTimer>
#include <QTemporaryDir>
#include <QUuid>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>

namespace {

constexpr int kStallTickMs = 20;
// 心跳停两拍半才算"卡住"：低于这个数多半是正常的一帧，采下来只是噪声。
constexpr int kStallSampleAfterMs = 50;
constexpr int kBetweenStepsMs = 150;
constexpr int kFinalSettleMs = 300;

QString probeTarget(const QApplication &app)
{
    const QString prefix = QStringLiteral("--wtfml-perf-probe=");
    const QStringList arguments = app.arguments();
    for (const QString &argument : arguments) {
        if (argument.startsWith(prefix)) {
            return argument.mid(prefix.size());
        }
    }
    return QString();
}

double milliseconds(const QElapsedTimer &timer)
{
    return static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
}

double round1(double value)
{
    return std::round(value * 10.0) / 10.0;
}

struct StepResult {
    QString name;
    double actionMs = 0.0;
    double repaintMs = 0.0;
    /// 这一步执行时后台目录加载是否还在跑。多任务那一组 A/B 靠它判断这一发到底算不算"加载中"。
    bool loadActive = false;
};

class Probe final : public QObject {
public:
    Probe(QApplication &app, MainWindow &window, QString outPath)
        : QObject(&window)
        , m_app(app)
        , m_window(window)
        , m_outPath(std::move(outPath))
    {
    }

    ~Probe() override
    {
        // 中途关窗时 finish() 不会跑。采样线程要是还 joinable，进程退出时
        // std::thread 的析构会直接 terminate —— 这里兜住。
        StallSampler::stop();
    }

    void start()
    {
        m_themeBefore = Theme::current().presetId;
        buildSteps();

        m_clock.start();
        m_lastTickMs = 0;
        // 采样器会把主线程暂停几毫秒。要分清"某一步慢"是产品本身的，还是采样器压出来的，
        // 就得能单独关掉它：`WTFML_PERF_NO_STALL_SAMPLER=1` 只停采样，看门狗（心跳 /
        // 最大间隔 / slowSpans）照旧。
        m_stallSamplerOn = qEnvironmentVariableIsEmpty("WTFML_PERF_NO_STALL_SAMPLER");
        if (m_stallSamplerOn) {
            StallSampler::start(kStallSampleAfterMs);
        }
        m_watchdog = new QTimer(this);
        m_watchdog->setTimerType(Qt::PreciseTimer);
        m_watchdog->setInterval(kStallTickMs);
        connect(m_watchdog, &QTimer::timeout, this, [this] {
            // 同时是采样线程的心跳：它停了多久 = 主线程被占住了多久。
            StallSampler::beat(static_cast<int>(m_next));
            const qint64 now = m_clock.elapsed();
            const qint64 gap = now - m_lastTickMs;
            m_lastTickMs = now;
            ++m_ticks;
            if (gap > m_maxGapMs) {
                m_maxGapMs = gap;
                m_maxGapFrom = m_lastTickStep;
                m_maxGapAt = m_currentStep;
            }
            if (gap > 50) {
                ++m_overFiftyMs;
                m_slowSpans.append(QJsonObject{{QStringLiteral("gapMs"), gap},
                    {QStringLiteral("fromStep"), m_lastTickStep}, {QStringLiteral("atStep"), m_currentStep}});
            }
            m_lastTickStep = m_currentStep;
            if (m_asyncCatalogPending) {
                ++m_asyncCatalogTicks;
                m_asyncCatalogMaxGap = std::max(m_asyncCatalogMaxGap, gap);
            }
        });
        m_watchdog->start();

        // 0 ms：等窗口真正上屏、第一轮布局/绘制跑完再开始发动作。
        QTimer::singleShot(0, this, [this] { runNext(); });
    }

private:
    void buildSteps()
    {
        // 单独测扫描成本，保留优化前后相同的探针步骤。设置页现已后台扫描，下面这条
        // 刻意同步的测量不代表产品翻页路径；它会预热文件缓存，也会进入总看门狗峰值。
        // 不把 scan:java 的耗时或总峰值误当成优化后的设置页同步阻塞。
        m_steps.push_back({QStringLiteral("scan:java"),
                           [] { (void)GlobalJavaSettings::scanLocalJavaVersions(); }});

        // 顶上四颗导航：走 `TitleBar` 上那一颗真按钮（它底下连着 MainWindowUI 的翻页
        // 和 MainWindow 的 `onNavigationApplied`，懒加载都在这条路上）。
        m_steps.push_back({QStringLiteral("nav:home"),
                           [this] { clickNav(MainWindowUI::homeTabIndex()); }});
        m_steps.push_back({QStringLiteral("nav:instances"),
                           [this] { clickNav(MainWindowUI::instancesTabIndex()); }});
        m_steps.push_back({QStringLiteral("nav:download"),
                           [this] { clickNav(MainWindowUI::downloadTabIndex()); }});
        m_steps.push_back({QStringLiteral("nav:settings"),
                           [this] { clickNav(MainWindowUI::settingsTabIndex()); }});

        // 主题：设置页那个下拉框，用户改主题走的就是 `currentIndexChanged` → 控制器
        // 读当前项 → 落盘 + `Theme::apply()`。这里只改下标，不自己调 Theme。
        if (QComboBox *combo = themeCombo()) {
            const QStringList presets = Theme::presetIds();
            for (const QString &preset : presets) {
                const int index = combo->findData(preset);
                if (index < 0) {
                    continue;
                }
                m_steps.push_back({QStringLiteral("theme:%1").arg(preset),
                                   [combo, index] { combo->setCurrentIndex(index); }});
            }
        }

        // 收尾：回主页，并把主题还原成跑之前那一档。
        m_steps.push_back({QStringLiteral("nav:home-again"),
                           [this] { clickNav(MainWindowUI::homeTabIndex()); }});
        m_steps.push_back({QStringLiteral("theme:restore"),
                           [this] { restoreTheme(); }});

        // 再进一次设置页：`onMainTabChanged()` 每次进来都刷 Java 列表 / 内存那两行，
        // 但 `JavaSettingsController::refreshList()` 里那次本地 Java 扫描有 `m_scanned`
        // 挡着。第一下和这一下的差 = 只花一次的那笔钱（页面首次铺开 + 扫描）。
        m_steps.push_back({QStringLiteral("nav:settings-again"),
                           [this] { clickNav(MainWindowUI::settingsTabIndex()); }});
        m_steps.push_back({QStringLiteral("nav:home-final"),
                           [this] { clickNav(MainWindowUI::homeTabIndex()); }});

        // Synthetic view workload: real task widgets, no network/jobs/accounts.
        // Seed outside timed actions, then change just one running task per tick.
        for (int i = 0; i < 200; ++i) {
            TaskCenter::Item item;
            item.key = QStringLiteral("perf-task-%1").arg(i);
            item.title = QStringLiteral("性能测量任务 %1").arg(i);
            item.state = i < 20 ? TaskCenter::State::Running : TaskCenter::State::Succeeded;
            item.percent = i < 20 ? 10 : 100;
            item.detail = QStringLiteral("文件校验与资源准备");
            item.finishedAt = QDateTime::fromSecsSinceEpoch(1700000000 + i);
            item.capabilities.cancel = i < 20;
            item.steps = {{QStringLiteral("检查文件"), TaskCenter::StepState::Done, {}},
                          {QStringLiteral("准备资源"), i < 20 ? TaskCenter::StepState::Running : TaskCenter::StepState::Done, {}}};
            m_tasks.append(item);
        }
        m_steps.push_back({QStringLiteral("tasks:open-200"), [this] {
            m_taskPanel = new TaskCenterPanel(&m_window, [this] { return m_tasks; }, {}, {}, {}, {}, &m_window, true);
            m_taskPanel->setGeometry(m_window.rect().adjusted(12, 80, -12, -72));
            m_taskPanel->setRowActionHandler([](const QString &, TaskCenter::RowAction) {});
            m_taskPanel->setRowActivatedHandler([](const QString &) {});
            m_taskPanel->showEmbedded();
            m_taskPanel->raise();
        }});
        for (int tick = 0; tick < 20; ++tick) {
            m_steps.push_back({QStringLiteral("tasks:update-%1").arg(tick + 1, 2, 10, QLatin1Char('0')),
                               [this, tick] {
                m_tasks[tick].percent = 20 + tick;
                m_tasks[tick].detail = QStringLiteral("正在下载资源 / %1 MB/s").arg(tick + 1);
                if (m_taskPanel) m_taskPanel->refresh();
            }});
        }
        m_steps.push_back({QStringLiteral("tasks:close"), [this] { delete m_taskPanel.data(); }});

        for (int i = 0; i < 1000; ++i) {
            PresetManagePage::PresetItem item;
            item.id = QStringLiteral("perf-preset-%1").arg(i);
            item.name = QStringLiteral("%1预设 %2").arg(i % 2 ? QStringLiteral("生存") : QStringLiteral("建筑")).arg(i, 4, 10, QLatin1Char('0'));
            item.description = QStringLiteral("游戏设置与按键说明 %1").arg(i);
            item.categoryKey = i % 2 ? QStringLiteral("mods") : QStringLiteral("settings");
            item.categoryText = i % 2 ? QStringLiteral("模组") : QStringLiteral("游戏设置");
            item.sectionCount = 2;
            item.isDefault = i == 0;
            m_presets.append(item);
        }
        m_steps.push_back({QStringLiteral("presets:open-1000"), [this] {
            m_presetPage = new PresetManagePage(&m_window);
            m_presetPage->setWindowFlags(Qt::Widget);
            m_presetPage->setGeometry(m_window.rect().adjusted(12, 80, -12, -72));
            m_presetPage->setCategories({{QString(), QStringLiteral("全部"), true},
                                          {QStringLiteral("settings"), QStringLiteral("游戏设置"), false},
                                          {QStringLiteral("mods"), QStringLiteral("模组"), false}});
            m_presetPage->setPresets(m_presets, {}, m_presets.first().id);
            m_presetPage->show();
            m_presetPage->raise();
        }});
        const QStringList queries{QStringLiteral("预设"), QStringLiteral("生存"), QString(),
                                  QStringLiteral("002"), QStringLiteral("说明"), QStringLiteral("不匹配"), QString()};
        for (int i = 0; i < queries.size(); ++i) {
            m_steps.push_back({QStringLiteral("presets:search-%1").arg(i + 1), [this, query = queries.at(i)] {
                if (m_presetPage) {
                    auto *search = m_presetPage->findChild<QLineEdit *>(QStringLiteral("presetSearchEdit"));
                    if (search) search->setText(query);
                }
            }});
        }
        m_steps.push_back({QStringLiteral("presets:close"), [this] { delete m_presetPage.data(); }});
        m_steps.push_back({QStringLiteral("presets:prepare-store-1000"), [this] {
            m_storeDir = std::make_unique<QTemporaryDir>(AppPaths::minecraftDir() + QStringLiteral("/perf-presets-XXXXXX"));
            if (!m_storeDir->isValid()) { m_storeFailed = true; return; }
            Presets::PresetIndex index;
            for (const auto &item : m_presets) {
                Presets::PresetManifest manifest;
                manifest.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                manifest.name = item.name;
                manifest.description = item.description;
                manifest.category = Presets::PresetCategory::GameSettings;
                if (!Presets::saveManifest(m_storeDir->path(), manifest)) { m_storeFailed = true; return; }
                index.order.append(manifest.id);
            }
            m_storeFailed = !Presets::saveIndex(m_storeDir->path(), index);
        }});
        for (int i = 0; i < 3; ++i) {
            m_steps.push_back({QStringLiteral("presets:read-store-%1").arg(i + 1), [this] {
                if (m_storeFailed || !m_storeDir) { m_storeFailed = true; return; }
                QList<Presets::BrokenPreset> broken;
                m_storeRows = Presets::list(m_storeDir->path(), &broken).size();
                m_storeFailed = m_storeRows != 1000 || !broken.isEmpty();
            }});
        }
        // "下一次启动"那一趟：只清进程里那张摘要，盘上那份留着（第一趟读 manifest 时写的）。
        // 量的就是正文那段省掉之后的代价 —— 每份一次 stat + 读一份摘要文件。
        m_steps.push_back({QStringLiteral("presets:read-store-from-disk-cache"), [this] {
            if (m_storeFailed || !m_storeDir) { m_storeFailed = true; return; }
            Presets::forgetSummaryCache(m_storeDir->path(), true);
            QList<Presets::BrokenPreset> broken;
            m_storeRows = Presets::list(m_storeDir->path(), &broken).size();
            m_storeFailed = m_storeRows != 1000 || !broken.isEmpty();
        }});
        // 多任务负载：同一串翻页动作跑两趟 —— 先空载（对照），再在后台目录加载期间跑。
        // 真实使用里用户在预设页转圈时照样点翻页；旧探针会把这一步之后的动作全挡住
        // （`runNext()` 里等异步目录），那种重叠量不到。
        const QList<int> navCycle{MainWindowUI::homeTabIndex(), MainWindowUI::instancesTabIndex(),
                                  MainWindowUI::downloadTabIndex(), MainWindowUI::settingsTabIndex()};
        const auto addNavCycle = [this, &navCycle](const QString &prefix) {
            for (int round = 0; round < 2; ++round) {
                for (int i = 0; i < navCycle.size(); ++i) {
                    const int index = navCycle.at(i);
                    m_steps.push_back({QStringLiteral("%1-%2")
                                           .arg(prefix)
                                           .arg(round * navCycle.size() + i + 1, 2, 10, QLatin1Char('0')),
                                       [this, index] { clickNav(index); }});
                }
            }
        };
        addNavCycle(QStringLiteral("concurrent:calm"));

        m_steps.push_back({QStringLiteral("presets:start-background-store"), [this] {
            if (m_storeFailed || !m_storeDir) { m_storeFailed = true; return; }
            // 上面那三趟 `presets:read-store-*` 已经把摘要缓存捂热了，不丢的话这一趟
            // 11 ms 就回来，"加载中"那半组翻页就压在空载上，量不到东西。
            Presets::forgetSummaryCache(m_storeDir->path());
            m_asyncCatalogPending = true;
            m_asyncCatalogClock.start();
            m_catalogLoader = new Presets::CatalogLoader(this);
            m_catalogLoader->load(m_storeDir->path(), [this](const Presets::CatalogSnapshot &snapshot) {
                m_asyncCatalogElapsed = m_asyncCatalogClock.elapsed();
                m_asyncCatalogRows = snapshot.presets.size();
                m_storeFailed = m_asyncCatalogRows != 1000 || !snapshot.broken.isEmpty();
                m_asyncCatalogPending = false;
            });
        }});
        addNavCycle(QStringLiteral("concurrent:busy"));
    }

    QComboBox *themeCombo() const
    {
        return m_window.findChild<QComboBox *>(MainWindowUI::themePresetComboName());
    }

    TitleBar *titleBar() const { return m_window.findChild<TitleBar *>(); }

    void clickNav(int index)
    {
        TitleBar *bar = titleBar();
        if (bar == nullptr) {
            return;
        }
        if (QAbstractButton *button = bar->navButton(index)) {
            button->click();
        }
    }

    void restoreTheme()
    {
        QComboBox *combo = themeCombo();
        if (combo == nullptr) {
            return;
        }
        const int index = combo->findData(m_themeBefore);
        if (index >= 0) {
            combo->setCurrentIndex(index);
        }
    }

    void runNext()
    {
        if (m_next >= m_steps.size()) {
            // 步骤走完了。后台那趟目录加载还没回来的话，等它收尾再落盘（有上限），
            // 这样报告里的 elapsedMs / guiTicks 才是完整的一趟。
            if (m_asyncCatalogPending) {
                if (m_asyncCatalogClock.elapsed() > 30000) {
                    m_catalogLoader->invalidate();
                    m_storeFailed = true;
                    m_asyncCatalogPending = false;
                } else {
                    QTimer::singleShot(10, this, [this] { runNext(); });
                    return;
                }
            }
            QTimer::singleShot(kFinalSettleMs, this, [this] { finish(); });
            return;
        }

        const Step &step = m_steps[static_cast<int>(m_next)];
        m_currentStep = step.name;
        StepResult result;
        result.name = step.name;
        result.loadActive = m_catalogLoader != nullptr && m_catalogLoader->isLoading();

        QElapsedTimer timer;
        timer.start();
        step.action();
        result.actionMs = milliseconds(timer);

        timer.restart();
        m_window.repaint();
        result.repaintMs = milliseconds(timer);

        m_results.push_back(result);
        ++m_next;

        QTimer::singleShot(kBetweenStepsMs, this, [this] { runNext(); });
    }

    void finish()
    {
        m_watchdog->stop();
        StallSampler::stop();

        QJsonArray steps;
        for (const StepResult &result : std::as_const(m_results)) {
            QJsonObject entry;
            entry.insert(QStringLiteral("name"), result.name);
            entry.insert(QStringLiteral("actionMs"), round1(result.actionMs));
            entry.insert(QStringLiteral("repaintMs"), round1(result.repaintMs));
            entry.insert(QStringLiteral("loadActive"), result.loadActive);
            steps.append(entry);
        }

        QJsonObject stalls;
        stalls.insert(QStringLiteral("tickMs"), kStallTickMs);
        stalls.insert(QStringLiteral("ticks"), m_ticks);
        stalls.insert(QStringLiteral("maxGapMs"), m_maxGapMs);
        stalls.insert(QStringLiteral("maxStallMs"),
                      std::max<qint64>(0, m_maxGapMs - kStallTickMs));
        stalls.insert(QStringLiteral("over50ms"), m_overFiftyMs);
        stalls.insert(QStringLiteral("maxGapFromStep"), m_maxGapFrom);
        stalls.insert(QStringLiteral("maxGapAtStep"), m_maxGapAt);
        stalls.insert(QStringLiteral("slowSpans"), m_slowSpans);

        // 卡住那一刻的调用栈。上面那些只有"卡了多久"，这里回答"卡在哪一行"：
        // 本程序的帧是 `exe+0xRVA`（姓名交给 tools/symbolize-stalls.ps1），
        // 系统 DLL 的帧带 dbghelp 认得出来的函数名。
        QJsonArray stallStacks;
        for (const QJsonObject &sample : StallSampler::samples()) {
            QJsonObject entry = sample;
            const int index = entry.value(QStringLiteral("stepIndex")).toInt(-1);
            entry.remove(QStringLiteral("stepIndex"));
            entry.insert(QStringLiteral("step"),
                         index >= 0 && index < m_steps.size() ? m_steps[index].name
                                                              : QStringLiteral("startup"));
            stallStacks.append(entry);
        }
        stalls.insert(QStringLiteral("samples"), stallStacks);

        QJsonObject root;
        root.insert(QStringLiteral("schema"), 1);
        root.insert(QStringLiteral("kind"), QStringLiteral("ui-probe"));
        root.insert(QStringLiteral("probe"), QStringLiteral("WTFML_PERF_PROBE=1"));
        root.insert(QStringLiteral("stallSampler"), m_stallSamplerOn);
        root.insert(QStringLiteral("dataDir"), AppPaths::minecraftDir());
        root.insert(QStringLiteral("appVersion"), m_app.applicationVersion());
        root.insert(QStringLiteral("qt"), QString::fromLatin1(qVersion()));
        root.insert(QStringLiteral("themeBefore"), m_themeBefore);
        root.insert(QStringLiteral("steps"), steps);
        root.insert(QStringLiteral("taskWorkload"), QJsonObject{
            {QStringLiteral("source"), QStringLiteral("synthetic-view-only")},
            {QStringLiteral("rows"), 200}, {QStringLiteral("running"), 20},
            {QStringLiteral("updates"), 20}, {QStringLiteral("changedPerUpdate"), 1}});
        root.insert(QStringLiteral("presetWorkload"), QJsonObject{
            {QStringLiteral("source"), QStringLiteral("synthetic-view-only")},
            {QStringLiteral("rows"), 1000}, {QStringLiteral("queries"), 7}});
        root.insert(QStringLiteral("presetStoreWorkload"), QJsonObject{
            {QStringLiteral("source"), QStringLiteral("synthetic-disk-metadata-only")},
            {QStringLiteral("expectedRows"), 1000}, {QStringLiteral("actualRows"), m_storeRows},
            {QStringLiteral("ok"), !m_storeFailed}, {QStringLiteral("reads"), 3}});
        // 多任务那一组：同一串翻页动作，空载一趟 / 后台目录加载中一趟。两趟都在同一份
        // 报告里，别拿别的构建、别的机器上的数来比。`busyDuringLoad` 说明"加载中"那
        // 一趟里有几发点下去时目录还在读 —— 少于总数的话，剩下那几发其实已经空载了。
        double calmSum = 0.0;
        double calmMax = 0.0;
        double busySum = 0.0;
        double busyMax = 0.0;
        int calmCount = 0;
        int busyCount = 0;
        int busyDuringLoad = 0;
        for (const StepResult &result : std::as_const(m_results)) {
            if (result.name.startsWith(QStringLiteral("concurrent:calm-"))) {
                calmSum += result.actionMs;
                calmMax = std::max(calmMax, result.actionMs);
                ++calmCount;
            } else if (result.name.startsWith(QStringLiteral("concurrent:busy-"))) {
                // 只把"目录还在读"的那几发算进 `busy`：加载从 2.3 s 缩到 0.4 s 之后
                // （2026-11-01 预设并行读盘），8 发里后几发其实已经空载了 —— 混进平均数
                // 只会把 `busy` 拉回 `calm`，看着"多任务不挡事"，其实那几发根本没压在窗口上。
                if (result.loadActive) {
                    busySum += result.actionMs;
                    busyMax = std::max(busyMax, result.actionMs);
                    ++busyCount;
                    ++busyDuringLoad;
                }
            }
        }
        root.insert(QStringLiteral("concurrentWorkload"), QJsonObject{
            {QStringLiteral("source"), QStringLiteral("nav-cycle")},
            {QStringLiteral("calm"), QJsonObject{
                {QStringLiteral("count"), calmCount},
                {QStringLiteral("avgMs"), calmCount > 0 ? round1(calmSum / calmCount) : 0.0},
                {QStringLiteral("maxMs"), round1(calmMax)}}},
            {QStringLiteral("busy"), QJsonObject{
                {QStringLiteral("count"), busyCount},
                {QStringLiteral("avgMs"), busyCount > 0 ? round1(busySum / busyCount) : 0.0},
                {QStringLiteral("maxMs"), round1(busyMax)}}},
            {QStringLiteral("busyDuringLoad"), busyDuringLoad},
            {QStringLiteral("catalogElapsedMs"), m_asyncCatalogElapsed}});
        root.insert(QStringLiteral("backgroundPresetStore"), QJsonObject{
            {QStringLiteral("rows"), m_asyncCatalogRows},
            {QStringLiteral("elapsedMs"), m_asyncCatalogElapsed},
            {QStringLiteral("guiTicks"), m_asyncCatalogTicks},
            {QStringLiteral("maxGuiGapMs"), m_asyncCatalogMaxGap}});
        root.insert(QStringLiteral("stalls"), stalls);

        QJsonObject window;
        window.insert(QStringLiteral("width"), m_window.width());
        window.insert(QStringLiteral("height"), m_window.height());
        root.insert(QStringLiteral("window"), window);

        QFile file(m_outPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            file.close();
        }

        m_app.exit(m_storeFailed ? 1 : 0);
    }

    struct Step {
        QString name;
        std::function<void()> action;
    };

    QApplication &m_app;
    MainWindow &m_window;
    QString m_outPath;
    QList<Step> m_steps;
    QList<StepResult> m_results;
    QElapsedTimer m_clock;
    QTimer *m_watchdog = nullptr;
    QString m_themeBefore;
    QList<TaskCenter::Item> m_tasks;
    QPointer<TaskCenterPanel> m_taskPanel;
    QList<PresetManagePage::PresetItem> m_presets;
    QPointer<PresetManagePage> m_presetPage;
    std::unique_ptr<QTemporaryDir> m_storeDir;
    qsizetype m_storeRows = 0;
    bool m_storeFailed = false;
    QString m_currentStep = QStringLiteral("startup");
    QString m_lastTickStep = QStringLiteral("startup");
    QString m_maxGapFrom;
    QString m_maxGapAt;
    QJsonArray m_slowSpans;
    Presets::CatalogLoader *m_catalogLoader = nullptr;
    bool m_asyncCatalogPending = false;
    QElapsedTimer m_asyncCatalogClock;
    qint64 m_asyncCatalogElapsed = 0;
    qsizetype m_asyncCatalogRows = 0;
    qint64 m_asyncCatalogTicks = 0;
    qint64 m_asyncCatalogMaxGap = 0;
    qint64 m_lastTickMs = 0;
    qint64 m_maxGapMs = 0;
    qint64 m_ticks = 0;
    qint64 m_overFiftyMs = 0;
    bool m_stallSamplerOn = true;
    qsizetype m_next = 0;
};

} // namespace

void PerfProbe::attach(QApplication &app, MainWindow &window)
{
    const QString target = probeTarget(app);
    if (target.isEmpty()) {
        return;
    }
    auto *probe = new Probe(app, window, target);
    probe->start();
}
