#include "controllers/JavaSettingsController.hpp"

#include "controllers/JavaRuntimeController.hpp"
#include "InlineMessage.hpp"
#include "JavaManageDialog.hpp"
#include "MainWindowUI.hpp"
#include "MemorySlider.hpp"
#include "OverlayHost.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include "util/SystemMemory.hpp"
#include "util/AppPaths.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

namespace {
QString memoryText(qint64 mb) {
    if (mb <= 0) return QStringLiteral("未知");
    if (mb >= 1024) return QStringLiteral("%1 GB").arg(QString::number(double(mb) / 1024.0, 'f', 1));
    return QStringLiteral("%1 MB").arg(mb);
}
}

JavaSettingsController::JavaSettingsController(MainWindowUI *ui, GlobalJavaSettings *settings,
                                               JavaRuntimeController *runtime,
                                               QWidget *dialogParent, Host host, QObject *parent)
    : QObject(parent), m_ui(ui), m_settings(settings), m_runtime(runtime),
      m_dialogParent(dialogParent), m_host(std::move(host)) {
    connect(runtime, &JavaRuntimeController::busyChanged, this, [this](bool busy) {
        if (m_page != nullptr) m_page->setBusy(busy);
    });
    connect(runtime, &JavaRuntimeController::statusChanged, this,
            [this](const QString &message) { log(message); });
    connect(runtime, &JavaRuntimeController::detectionFinished, this,
            &JavaSettingsController::applyDetected);
    connect(runtime, &JavaRuntimeController::runtimeFinished, this,
            [this](int major, bool ok, const QString &path, bool batch) {
        if (ok) {
            log(tr("Java %1 运行时已装好：%2").arg(major).arg(QDir::toNativeSeparators(path)));
        } else {
            log(tr("Java %1 运行时下载失败").arg(major));
            if (!batch && m_page != nullptr && m_page->isVisible())
                InlineMessage::warning(m_ui->overlay(), tr("Java 运行时下载失败"),
                    tr("没能下到 Java %1。多半是网络不通（下载走的是 Adoptium）。\n"
                       "可以自己装一个，再回来按「检测已有 Java」；或者去「实例设置…」里把某一份现成的 Java 指过去。")
                        .arg(major));
        }
    });
    connect(runtime, &JavaRuntimeController::downloadSequenceFinished, this,
            [this](bool batch, int okCount, int failCount, bool cancelled) {
        if (cancelled) log(tr("Java 下载队列已停止；当前下载完成后没有再开始下一份。"));
        if (batch && failCount > 0 && m_page != nullptr && m_page->isVisible())
            InlineMessage::warning(m_ui->overlay(), tr("有些 Java 没下成"),
                tr("下好了 %1 份，有 %2 份没下成。多半是网络不通（下载走的是 Adoptium）。\n"
                   "可以隔一会儿再点一次，或者自己装好之后按「检测已有 Java」。")
                    .arg(okCount).arg(failCount));
    });

    // 自动分配要"随系统可用内存更新"：设置页露着的时候每 2 秒重读一次并刷新展示。
    // 只在自动档 + 设置页可见时跑（见 refreshMemory() 末尾）—— 手动档下读系统内存
    // 没有任何意义，页不在眼前时更新也没人看。
    m_memoryRefreshTimer = new QTimer(this);
    m_memoryRefreshTimer->setInterval(2000);
    connect(m_memoryRefreshTimer, &QTimer::timeout, this, [this]() {
        if (!m_settings->autoMemory() || !memoryPageVisible()) {
            m_memoryRefreshTimer->stop();
            return;
        }
        refreshMemory();
    });
}

bool JavaSettingsController::memoryPageVisible() const {
    QTabWidget *tabs = m_ui->getTabWidget();
    QComboBox *mode = m_ui->getMemoryModeCombo();
    return tabs != nullptr && mode != nullptr &&
           tabs->currentIndex() == MainWindowUI::settingsTabIndex() && mode->isVisible();
}

void JavaSettingsController::log(const QString &message) const {
    if (m_host.log) m_host.log(message);
}

void JavaSettingsController::openManagePage() {
    OverlayHost *overlay = m_ui->overlay();
    if (overlay == nullptr) return;
    if (m_page == nullptr) {
        m_page = new JavaManageDialog(m_dialogParent);
        connect(m_page, &JavaManageDialog::downloadRequested, this,
                &JavaSettingsController::download);
        connect(m_page, &JavaManageDialog::downloadAllMissingRequested, this,
                &JavaSettingsController::downloadAllMissing);
        connect(m_page, &JavaManageDialog::detectRequested, this, &JavaSettingsController::detect);
        connect(m_page, &JavaManageDialog::useAutoRequested, this, &JavaSettingsController::useAuto);
        connect(m_page, &JavaManageDialog::useRuntimeRequested, this,
                &JavaSettingsController::useRuntime);
        connect(m_page, &QDialog::rejected, m_runtime,
                &JavaRuntimeController::cancelAfterCurrent);
    }
    pushRows();
    m_page->setBusy(m_runtime->isBusy());
    if (!m_runtime->isBusy()) m_runtime->detectInstalled();
    const int settingsTab = MainWindowUI::settingsTabIndex();
    if (QTabWidget *tabs = m_ui->getTabWidget(); settingsTab >= 0 && settingsTab < tabs->count())
        tabs->setCurrentIndex(settingsTab);
    // 非阻塞开页（见 `OverlayHost::showPage()`）：这一页后面没有一行代码要等它收掉；
    // 页上那一发 `rejected` 照旧接在 `cancelAfterCurrent` 上（返回箭头也是 reject）。
    if (!overlay->showPage(m_page, m_page->titleText(), settingsTab)) {
        log(tr("Java 管理那一页没开成：覆盖层刚被另一页占上了。"));
    }
}

void JavaSettingsController::resetDiscovery() {
    ++m_discoveryGeneration;
    m_discoveryPending = false;
    m_scanned = false;
    m_versions.clear();
}

void JavaSettingsController::refreshList() {
    if (m_scanned) return;
    m_scanned = true;
    const QString root = AppPaths::javaDir();
    const auto provider = m_host.scanJava;
    queueDiscovery([root, provider] {
        return provider ? provider(root) : GlobalJavaSettings::scanLocalJavaVersions(root);
    });
}

void JavaSettingsController::applyDetected(const QMap<int, QString> &versions) {
    // 显式检测/下载结果优先于先前自动扫描；自定义 Java 探测也不放回 GUI 线程。
    m_scanned = true;
    const QString pinned = m_settings->javaPath();
    if (pinned.isEmpty() || versions.values().contains(pinned)) {
        ++m_discoveryGeneration;
        m_discoveryPending = false;
        m_versions = versions;
        pushRows();
        emit discoveryFinished();
        return;
    }
    queueDiscovery([versions] { return versions; });
}

void JavaSettingsController::queueDiscovery(std::function<QMap<int, QString>()> scan) {
    const quint64 generation = ++m_discoveryGeneration;
    const QString root = AppPaths::javaDir();
    const QString pinned = m_settings->javaPath();
    m_discoveryPending = true;
    auto *watcher = new QFutureWatcher<QMap<int, QString>>(this);
    connect(watcher, &QFutureWatcher<QMap<int, QString>>::finished, this,
            [this, watcher, generation, root, pinned] {
        const auto versions = watcher->result();
        watcher->deleteLater();
        if (generation != m_discoveryGeneration) return;
        m_discoveryPending = false;
        if (root != AppPaths::javaDir() || pinned != m_settings->javaPath()) {
            m_scanned = false;
            refreshList();
            return;
        }
        m_versions = versions;
        pushRows();
        emit discoveryFinished();
    });
    watcher->setFuture(QtConcurrent::run([scan = std::move(scan), pinned] {
        auto versions = scan();
        if (!pinned.isEmpty() && !versions.values().contains(pinned)) {
            const int major = GlobalJavaSettings::probeJavaMajorVersion(pinned);
            if (major > 0 && !versions.contains(major)) versions.insert(major, pinned);
        }
        return versions;
    }));
}

void JavaSettingsController::pushRows() {
    if (m_page == nullptr) return;
    for (int major : JavaManageDialog::managedMajors()) m_page->setRuntime(major, m_versions.value(major));
    m_page->setSelection(m_settings->autoSelectJava(), m_settings->javaPath());
}

void JavaSettingsController::updatePathLabel() {
    QLabel *label = m_ui->getJavaPathLabel();
    const QString path = m_settings->javaPath();
    if (!m_settings->autoSelectJava())
        label->setText(path.isEmpty() ? tr("手动指定：还没有路径，启动会直接失败。")
                                      : tr("手动指定：%1").arg(path));
    else
        label->setText(path.isEmpty() ? tr("自动：还没挑过，启动时按版本要求找。")
                                      : tr("自动：上次用的是 %1").arg(path));
}

void JavaSettingsController::useAuto() {
    if (m_page == nullptr) return;
    m_settings->setAutoSelectJava(true);
    m_settings->save();
    updatePathLabel();
    pushRows();
}

void JavaSettingsController::useRuntime(int major) {
    if (m_page == nullptr || m_versions.value(major).isEmpty()) return;
    m_settings->setAutoSelectJava(false);
    m_settings->setJavaPath(m_versions.value(major));
    m_settings->save();
    updatePathLabel();
    pushRows();
}

void JavaSettingsController::detect() { if (m_page != nullptr) m_runtime->detectInstalled(); }
void JavaSettingsController::download(int major) { if (m_page != nullptr && major > 0) m_runtime->downloadRuntime(major); }

void JavaSettingsController::downloadAllMissing() {
    if (m_page == nullptr) return;
    QList<int> missing;
    for (int major : JavaManageDialog::managedMajors())
        if (m_versions.value(major).isEmpty()) missing.append(major);
    m_runtime->downloadMissing(missing);
}

void JavaSettingsController::applyMemoryLimits(qint64 totalMb) {
    const int cap = MainWindowUI::memoryCapForTotalMb(totalMb);
    const int min = MainWindowUI::memoryMaxMinMb();
    const QSignalBlocker blockSpin(m_ui->getMemoryMaxSpin());
    const QSignalBlocker blockSlider(m_ui->getMemoryMaxSlider());
    m_ui->getMemoryMaxSpin()->setRange(min, cap);
    m_ui->getMemoryMaxSlider()->setRangeMb(min, cap);
}

bool JavaSettingsController::clampXmsToXmx(int xmx) {
    if (m_settings->xms() <= xmx) return false;
    m_settings->setXms(xmx);
    return true;
}

void JavaSettingsController::setMemoryModeFromUi() {
    QComboBox *mode = m_ui->getMemoryModeCombo();
    if (mode == nullptr) return;
    const bool wantAuto = mode->currentData().toBool();
    if (wantAuto == m_settings->autoMemory()) {
        refreshMemory();
        return;
    }
    if (wantAuto) {
        // 这一句里面把当前 Xmx/Xms 记成"上次手动档的现场"并落盘。之后 refreshMemory()
        // 才按系统内存重算 —— 顺序不能反，反了记下的就是自动值。
        m_settings->setAutoMemory(true);
    } else {
        m_settings->restoreManualMemory();
    }
    refreshMemory();
}

void JavaSettingsController::refreshMemory() {
    const bool autoMode = m_settings->autoMemory();
    const int beforeXmx = m_settings->xmx();
    const int beforeXms = m_settings->xms();

    // 自动档：**现算**（`refreshSuggestedMemory()` 会读系统内存并改写 Xmx/Xms）；
    // 手动档：只读一次系统内存写说明，**一个数字都不许动**。
    const SystemMemory::Usage usage =
        autoMode ? m_settings->refreshSuggestedMemory() : SystemMemory::read();
    applyMemoryLimits(usage.totalMb);

    const int cap = m_ui->getMemoryMaxSpin()->maximum();
    int xmx = m_settings->xmx();
    if (xmx > cap) {
        log(tr("内存：%1 %2 MB 超过这台机器的物理内存上限，已降到 %3 MB。")
                .arg(autoMode ? tr("自动给出的最大内存") : tr("盘上记的最大内存"))
                .arg(xmx)
                .arg(cap));
        m_settings->setXmx(cap);
        xmx = cap;
    }
    if (clampXmsToXmx(xmx)) {
        log(tr("内存：最小内存不能超过最大内存，已跟着降到 %1 MB。").arg(xmx));
    }
    if (m_settings->xmx() != beforeXmx || m_settings->xms() != beforeXms) {
        // 自动值变了才落盘（定时器每 2 秒跑一次，不能每趟都写注册表）。
        m_settings->save();
    }

    const QSignalBlocker blockSpin(m_ui->getMemoryMaxSpin());
    const QSignalBlocker blockSlider(m_ui->getMemoryMaxSlider());
    const QSignalBlocker blockMode(m_ui->getMemoryModeCombo());
    if (QComboBox *mode = m_ui->getMemoryModeCombo()) {
        const int index = mode->findData(autoMode);
        if (index >= 0) mode->setCurrentIndex(index);
    }
    m_ui->getMemoryMaxSpin()->setValue(xmx);
    m_ui->getMemoryMaxSpin()->setEnabled(!autoMode);
    m_ui->getMemoryMaxSlider()->setEnabled(!autoMode);
    if (autoMode) {
        // 把手停在"上次手动档"的位置，进度条（虚条）跟着自动值走 —— 见 MemorySlider.hpp。
        m_ui->getMemoryMaxSlider()->setValueMb(m_settings->manualXmx());
        m_ui->getMemoryMaxSlider()->setAutoValueMb(xmx);
    } else {
        m_ui->getMemoryMaxSlider()->clearAutoValue();
        m_ui->getMemoryMaxSlider()->setValueMb(xmx);
    }

    QString hint;
    if (autoMode) {
        if (usage.totalMb <= 0) {
            hint = tr("自动分配：读不到系统内存，先用内置值 %1 MB。切回「手动分配」"
                      "会恢复你上次手动设的 %2 MB。")
                       .arg(xmx)
                       .arg(m_settings->manualXmx());
        } else {
            hint = tr("自动分配：系统总内存 %1、当前可用 %2；自动给到最大内存 %3 MB"
                      "（最小内存 %4 MB）。这个数会随系统可用内存自动更新；切回「手动分配」"
                      "会恢复你上次手动设的 %5 MB。")
                       .arg(memoryText(usage.totalMb), memoryText(usage.availableMb))
                       .arg(xmx)
                       .arg(m_settings->xms())
                       .arg(m_settings->manualXmx());
        }
    } else if (usage.totalMb <= 0) {
        hint = tr("读不到系统内存，最大内存直接用右边的数字框填。最小内存由启动器定（%1 MB），不用管它。")
                   .arg(m_settings->xms());
    } else {
        hint = tr("系统总内存 %1、当前可用 %2；现在最多给到 %3 MB。左边拖着改，也可以直接填数字。"
                  "最小内存由启动器定（%4 MB），不用管它。")
                   .arg(memoryText(usage.totalMb), memoryText(usage.availableMb))
                   .arg(m_settings->xmx()).arg(m_settings->xms());
    }
    m_ui->getMemoryHint()->setText(hint);

    // 自动档在设置页露着的时候才需要每 2 秒刷新一次；其他情况立刻停掉。
    if (autoMode && memoryPageVisible()) {
        m_memoryRefreshTimer->start();
    } else {
        m_memoryRefreshTimer->stop();
    }
}

void JavaSettingsController::setWarnMismatchFromUi() {
    const bool warn = m_ui->getJavaWarnMismatchCheck()->isChecked();
    m_settings->setWarnVersionMismatch(warn);
    log(warn ? tr("实例指定的 Java 版本不符时：会提醒。")
             : tr("实例指定的 Java 版本不符时：不再提醒，直接按它指定的 Java 启动。"));
}

void JavaSettingsController::commitMemoryFromSpin() {
    // 自动档下这两个控件是禁用的，值由 refreshMemory() 写；万一有谁绕过程序改了信号，
    // 也不能把自动算出来的值当成用户手填的写回盘。
    if (m_settings->autoMemory()) return;
    const int value = m_ui->getMemoryMaxSpin()->value();
    if (m_ui->getMemoryMaxSlider()->valueMb() != value) {
        const QSignalBlocker block(m_ui->getMemoryMaxSlider());
        m_ui->getMemoryMaxSlider()->setValueMb(value);
    }
    m_settings->setXmx(value);
    clampXmsToXmx(value);
    m_settings->save();
    refreshMemory();
}

void JavaSettingsController::syncMemorySlider() {
    if (m_settings->autoMemory()) return;
    const int value = m_ui->getMemoryMaxSlider()->valueMb();
    if (m_ui->getMemoryMaxSpin()->value() != value) {
        const QSignalBlocker block(m_ui->getMemoryMaxSpin());
        m_ui->getMemoryMaxSpin()->setValue(value);
    }
    if (!m_ui->getMemoryMaxSlider()->isSliderDown()) commitMemoryFromSpin();
}
