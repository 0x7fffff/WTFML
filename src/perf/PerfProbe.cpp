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

#include "MainWindow.hpp"
#include "MainWindowUI.hpp"
#include "Theme.hpp"
#include "TitleBar.hpp"
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
#include <QTimer>
#include <QWidget>

#include <cmath>
#include <functional>
#include <utility>

namespace {

constexpr int kStallTickMs = 20;
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

    void start()
    {
        m_themeBefore = Theme::current().presetId;
        buildSteps();

        m_clock.start();
        m_lastTickMs = 0;
        m_watchdog = new QTimer(this);
        m_watchdog->setTimerType(Qt::PreciseTimer);
        m_watchdog->setInterval(kStallTickMs);
        connect(m_watchdog, &QTimer::timeout, this, [this] {
            const qint64 now = m_clock.elapsed();
            const qint64 gap = now - m_lastTickMs;
            m_lastTickMs = now;
            ++m_ticks;
            m_maxGapMs = std::max(m_maxGapMs, gap);
            if (gap > 50) {
                ++m_overFiftyMs;
            }
        });
        m_watchdog->start();

        // 0 ms：等窗口真正上屏、第一轮布局/绘制跑完再开始发动作。
        QTimer::singleShot(0, this, [this] { runNext(); });
    }

private:
    void buildSteps()
    {
        // 「点设置那一下到底贵在哪」的一半：`onMainTabChanged()` → `refreshList()` 会跑
        // 一次本地 Java 扫描。这里**头一个**就单独跑一遍同一个函数，把扫描那笔钱量出来；
        // 代价是后面第一次进设置页时扫描已经是热的 —— 所以 `nav:settings` 这一行量到的
        // 是"首次铺开（扫描已热）"，两次跑的数一减就是扫描的价（表里两行都在）。
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
            QTimer::singleShot(kFinalSettleMs, this, [this] { finish(); });
            return;
        }

        const Step &step = m_steps[static_cast<int>(m_next)];
        StepResult result;
        result.name = step.name;

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

        QJsonArray steps;
        for (const StepResult &result : std::as_const(m_results)) {
            QJsonObject entry;
            entry.insert(QStringLiteral("name"), result.name);
            entry.insert(QStringLiteral("actionMs"), round1(result.actionMs));
            entry.insert(QStringLiteral("repaintMs"), round1(result.repaintMs));
            steps.append(entry);
        }

        QJsonObject stalls;
        stalls.insert(QStringLiteral("tickMs"), kStallTickMs);
        stalls.insert(QStringLiteral("ticks"), m_ticks);
        stalls.insert(QStringLiteral("maxGapMs"), m_maxGapMs);
        stalls.insert(QStringLiteral("maxStallMs"),
                      std::max<qint64>(0, m_maxGapMs - kStallTickMs));
        stalls.insert(QStringLiteral("over50ms"), m_overFiftyMs);

        QJsonObject root;
        root.insert(QStringLiteral("schema"), 1);
        root.insert(QStringLiteral("kind"), QStringLiteral("ui-probe"));
        root.insert(QStringLiteral("probe"), QStringLiteral("WTFML_PERF_PROBE=1"));
        root.insert(QStringLiteral("dataDir"), AppPaths::minecraftDir());
        root.insert(QStringLiteral("appVersion"), m_app.applicationVersion());
        root.insert(QStringLiteral("qt"), QString::fromLatin1(qVersion()));
        root.insert(QStringLiteral("themeBefore"), m_themeBefore);
        root.insert(QStringLiteral("steps"), steps);
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

        m_app.quit();
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
    qint64 m_lastTickMs = 0;
    qint64 m_maxGapMs = 0;
    qint64 m_ticks = 0;
    qint64 m_overFiftyMs = 0;
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
