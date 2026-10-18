#include "controllers/JavaSettingsController.hpp"

#include "controllers/JavaRuntimeController.hpp"
#include "InlineMessage.hpp"
#include "JavaManageDialog.hpp"
#include "MainWindowUI.hpp"
#include "MemorySlider.hpp"
#include "OverlayHost.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include "util/SystemMemory.hpp"

#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>

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
    overlay->execPage(m_page, m_page->titleText(), settingsTab);
}

void JavaSettingsController::resetDiscovery() {
    m_scanned = false;
    m_versions.clear();
}

void JavaSettingsController::refreshList() {
    if (m_scanned) return;
    m_scanned = true;
    applyDetected(GlobalJavaSettings::scanLocalJavaVersions());
}

void JavaSettingsController::applyDetected(const QMap<int, QString> &versions) {
    m_versions = versions;
    const QString pinned = m_settings->javaPath();
    if (!pinned.isEmpty()) {
        const int major = GlobalJavaSettings::probeJavaMajorVersion(pinned);
        if (major > 0 && !m_versions.contains(major)) m_versions.insert(major, pinned);
    }
    pushRows();
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

void JavaSettingsController::refreshMemory() {
    // ★ 2026-10-18：「自动（按系统内存）」那一档的界面撤掉了（用户原话「内存分配和最大
    //   内存这玩意完全就是重复的，只保留那个拖动的」），所以这里**只读系统内存写说明**，
    //   不再走 `refreshSuggestedMemory()`（那是自动档的"按可用内存重算并落盘"）。读它
    //   只是为了让下面那句说明能说清这台机器有多少内存。
    const SystemMemory::Usage usage = SystemMemory::read();
    applyMemoryLimits(usage.totalMb);
    const int cap = m_ui->getMemoryMaxSpin()->maximum();
    int xmx = m_settings->xmx();
    bool clamped = false;
    if (xmx > cap) {
        log(tr("内存：盘上记的最大内存 %1 MB 超过这台机器的物理内存，已降到 %2 MB。")
                .arg(xmx).arg(cap));
        m_settings->setXmx(cap);
        xmx = cap;
        clamped = true;
    }
    if (clampXmsToXmx(xmx)) {
        log(tr("内存：最小内存不能超过最大内存，已跟着降到 %1 MB。").arg(xmx));
        clamped = true;
    }
    if (clamped) m_settings->save();
    const QSignalBlocker blockSpin(m_ui->getMemoryMaxSpin());
    const QSignalBlocker blockSlider(m_ui->getMemoryMaxSlider());
    m_ui->getMemoryMaxSpin()->setValue(xmx);
    m_ui->getMemoryMaxSlider()->setValueMb(xmx);
    // 条和数字框**永远是活的**：能改的口子只剩这一个，再灰掉就没有地方能给 -Xmx 了。
    m_ui->getMemoryMaxSpin()->setEnabled(true);
    m_ui->getMemoryMaxSlider()->setEnabled(true);
    QString hint;
    if (usage.totalMb <= 0)
        hint = tr("读不到系统内存，最大内存直接用右边的数字框填。最小内存由启动器定（%1 MB），不用管它。")
                   .arg(m_settings->xms());
    else
        hint = tr("系统总内存 %1、当前可用 %2；现在最多给到 %3 MB。左边拖着改，也可以直接填数字。"
                  "最小内存由启动器定（%4 MB），不用管它。")
                   .arg(memoryText(usage.totalMb), memoryText(usage.availableMb))
                   .arg(m_settings->xmx()).arg(m_settings->xms());
    m_ui->getMemoryHint()->setText(hint);
}

void JavaSettingsController::setWarnMismatchFromUi() {
    const bool warn = m_ui->getJavaWarnMismatchCheck()->isChecked();
    m_settings->setWarnVersionMismatch(warn);
    log(warn ? tr("实例指定的 Java 版本不符时：会提醒。")
             : tr("实例指定的 Java 版本不符时：不再提醒，直接按它指定的 Java 启动。"));
}

void JavaSettingsController::commitMemoryFromSpin() {
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
    const int value = m_ui->getMemoryMaxSlider()->valueMb();
    if (m_ui->getMemoryMaxSpin()->value() != value) {
        const QSignalBlocker block(m_ui->getMemoryMaxSpin());
        m_ui->getMemoryMaxSpin()->setValue(value);
    }
    if (!m_ui->getMemoryMaxSlider()->isSliderDown()) commitMemoryFromSpin();
}
