#include "Support.hpp"

#include "ChromeDialog.hpp"
#include "DownloadToolPanel.hpp"
#include "InstanceUpdateWizard.hpp"
#include "OverlayHost.hpp"
#include "TitleBar.hpp"

#include <QDirIterator>
#include <QMenu>
#include <QFileInfo>
#include <QRegularExpression>

// TL1：产品里（ui/ 与 src/，不含 tests/）会以独立顶层窗出现的对话框一律不带系统标题栏。
// 直接继承 QDialog / QWizard 的类要么改继承 ChromeDialog，要么进下面的白名单并写明理由；
// 也不许再裸开 QDialog / QWizard / QInputDialog / QMessageBox（那些都是带系统标题栏的独立窗）。
// TL2：换不了基类、自己装窄条的那几扇，实测一遍条在顶上、内容在条下面。
int UiTopLevelDialogChecks(QApplication &app) {
    // 白名单：从不以"带系统标题栏的顶层窗"出现的类。新加一个 QDialog 子类却没想清楚怎么弹，这里会红。
    const QHash<QString, QString> allowed{
        {QStringLiteral("ChromeDialog"), QStringLiteral("就是那层自绘窄标题条的壳本身")},
        {QStringLiteral("GameLogWindow"), QStringLiteral("游戏日志窗自己画标题条（WindowChrome + 窗内标题行）")},
        {QStringLiteral("InstanceUpdateWizard"), QStringLiteral("QWizard 换不了基类：自己摆 ChromeStrip + WindowChrome（TL2 实测）")},
        {QStringLiteral("InlineMessageBox"), QStringLiteral("提示卡，只经 OverlayHost::execPrompt 摆进内容区，自己从来不是窗口")},
        {QStringLiteral("JavaMismatchDialog"), QStringLiteral("只经 overlay()->execPrompt 摆进内容区（MainWindowLaunchJava）")},
        {QStringLiteral("AccountSkinDialog"), QStringLiteral("OverlayHost::showPage 覆盖页（MainWindowAccount）；摆不上只写日志")},
        {QStringLiteral("AgentSettingsPage"), QStringLiteral("OverlayHost::showPage 覆盖页（AgentController）；摆不上就删掉")},
        {QStringLiteral("GroupDialog"), QStringLiteral("OverlayHost::showPage 覆盖页（MainWindowGroup）；摆不上就删掉")},
        {QStringLiteral("InstanceSettingsDialog"), QStringLiteral("OverlayHost::showPage 覆盖页（InstanceDialogController）")},
        {QStringLiteral("IntegrityReportPage"), QStringLiteral("OverlayHost::showPage 覆盖页（IntegrityController）；摆不上只写日志")},
        {QStringLiteral("JavaManageDialog"), QStringLiteral("OverlayHost::showPage 覆盖页（JavaSettingsController）；摆不上只写日志")},
        {QStringLiteral("ResourceDetailPage"), QStringLiteral("OverlayHost::showPage 覆盖页（ResourcePageController）")},
        {QStringLiteral("ServerConsolePage"), QStringLiteral("OverlayHost::showPage 覆盖页（ServerConsoleController）")},
        {QStringLiteral("ServerSettingsPage"), QStringLiteral("OverlayHost::showPage 覆盖页（ServerSettingsPageController）")},
        {QStringLiteral("TaskDetailPage"), QStringLiteral("OverlayHost::showPage 覆盖页（TaskDetailController）")},
        {QStringLiteral("UpdateCenterPage"), QStringLiteral("OverlayHost::showPage 覆盖页（UpdateCenterController）")},
        {QStringLiteral("PresetEditorPage"), QStringLiteral("OverlayHost::showPage 覆盖页（PresetControllerEditor）")},
        {QStringLiteral("NewServerPage"), QStringLiteral("setWindowFlags(Qt::Widget) 嵌在服务器页的表单宿主里（MainWindowServer）")},
        {QStringLiteral("PresetManagePage"), QStringLiteral("以 Qt::Widget 嵌在实例页第三档（PresetController / PerfProbe）")},
        {QStringLiteral("PresetModEditor"), QStringLiteral("预设编辑页里的子页，PresetEditorPage 设成 Qt::Widget")},
        {QStringLiteral("PresetOptionsEditor"), QStringLiteral("预设编辑页里的子页，PresetEditorPage 设成 Qt::Widget")},
    };

    const QDir root = repoRoot();
    const QRegularExpression comments(QStringLiteral(R"(/\*.*?\*/|//[^\n]*)"),
                                      QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpression subclass(QStringLiteral(R"(class\s+(\w+)(?:\s+final)?\s*:\s*public\s+(?:QDialog|QWizard)\b)"));
    const QRegularExpression bare(
        QStringLiteral(R"(new\s+(?:QDialog|QWizard|QInputDialog|QMessageBox)\s*\(|^\s*(?:QDialog|QWizard|QInputDialog|QMessageBox)\s+\w+\s*[({;]|\b(?:QInputDialog|QMessageBox)::\w+\s*\()"),
        QRegularExpression::MultilineOption);
    QSet<QString> seen;
    QStringList offenders;
    QStringList adHoc;
    for (const QString &dir : {QStringLiteral("ui"), QStringLiteral("src")}) {
        QDirIterator it(root.absoluteFilePath(dir), {QStringLiteral("*.hpp"), QStringLiteral("*.h"), QStringLiteral("*.cpp")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) continue;
            const QString text = QString::fromUtf8(file.readAll()).remove(comments);
            const QString where = root.relativeFilePath(path);
            for (auto m = subclass.globalMatch(text); m.hasNext();) {
                const QString name = m.next().captured(1);
                seen.insert(name);
                if (!allowed.contains(name)) offenders << QStringLiteral("%1（%2）").arg(name, where);
            }
            for (auto m = bare.globalMatch(text); m.hasNext();)
                adHoc << QStringLiteral("%1: %2").arg(where, m.next().captured(0).trimmed());
        }
    }
    check(seen.size() >= 20, "TL1 扫到了 ui/ 与 src/ 下的对话框类（路径没走错）", QString::number(seen.size()));
    check(offenders.isEmpty(), "TL1 直接继承 QDialog/QWizard 的类要么改成 ChromeDialog，要么进白名单写明理由",
          offenders.join(QStringLiteral("；")));
    QStringList stale;
    for (auto it = allowed.cbegin(); it != allowed.cend(); ++it)
        if (!seen.contains(it.key())) stale << it.key();
    check(stale.isEmpty(), "TL1 白名单里没有已经不存在 / 已改成 ChromeDialog 的过期类名", stale.join(QStringLiteral("、")));
    check(adHoc.isEmpty(), "TL1 产品代码不再裸开 QDialog / QWizard / QInputDialog / QMessageBox",
          adHoc.join(QStringLiteral("；")));

    // ---- 高速下载：不再是独立窗口，而是工具箱里的一页（没有系统标题栏、没有第二层标题） ----
    {
        QWidget host;
        DownloadToolPanel panel(&host);
        check(!panel.isWindow() && qobject_cast<QDialog *>(&panel) == nullptr,
              "TL2 高速下载不是独立窗口 / 对话框，是嵌在工具箱里的一页");
        host.resize(800, 500);
        host.show();
        check(panel.findChild<QWidget *>(ChromeDialog::titleStripName()) == nullptr &&
                  panel.findChild<QLabel *>(ChromeDialog::titleLabelName()) == nullptr,
              "TL2 高速下载页里没有自己的标题条（标题是工具页的面包屑）");
        panel.resize(760, 400);
        panel.show();
        check(!panel.progressBox->isVisible(), "TL2 没有任务时进度区不画");
        panel.showProgress(42, 100, QStringLiteral("下载中"));
        check(panel.progressBox->isVisible() && panel.progress->height() <= 8 && !panel.progress->isTextVisible() &&
                  panel.percent->text() == QStringLiteral("42%"),
              "TL2 高速下载页进度条 ≤8px 高、百分比在右侧标签里",
              QString::number(panel.progress->height()) + QLatin1Char(' ') + panel.percent->text());
        int fired = -1;
        panel.onCache = [&fired](int action) { fired = action; };
        auto *trim = panel.findChild<QPushButton *>(QStringLiteral("downloadCacheAction1"));
        if (trim != nullptr) trim->click();
        check(trim != nullptr && panel.cacheButtons.size() == 3 && fired == 1,
              "TL2 高速下载页三个缓存操作是直接的按钮（按上限整理 / 清理过期 / 清空），点了照走 onCache");
        // 窄于 640：保存目录与文件名上下叠放。
        panel.resize(520, 400);
        QApplication::processEvents();
        check(panel.name->mapTo(&panel, QPoint(0, 0)).y() > panel.directory->mapTo(&panel, QPoint(0, 0)).y() &&
                  panel.name->mapTo(&panel, QPoint(0, 0)).x() == panel.directory->mapTo(&panel, QPoint(0, 0)).x(),
              "TL2 高速下载页窄于 640 时保存目录与文件名上下叠放");
    }

    // ---- TL2 实例更新向导（QWizard）：自己装的那条 + 内容整体下移 ----
    {
        InstanceUpdateWizard wizard(new QWidget, new QWidget, new QWidget, new QWidget);
        check(wizard.size() == QSize(720, 520) && wizard.minimumSize() == QSize(640, 460),
              "TL2 向导默认 720×520、最小 640×460（不再大而空）",
              QStringLiteral("%1x%2 min %3x%4").arg(wizard.width()).arg(wizard.height())
                  .arg(wizard.minimumWidth()).arg(wizard.minimumHeight()));
        wizard.show();
        app.processEvents();
        auto *strip = wizard.findChild<QWidget *>(ChromeDialog::titleStripName());
        auto *title = wizard.findChild<QLabel *>(ChromeDialog::titleLabelName());
        check(strip != nullptr && strip->geometry() == QRect(0, 0, wizard.width(), ChromeDialog::stripHeight()),
              "TL2 向导顶上横着一整条窄标题条",
              strip != nullptr ? QStringLiteral("%1,%2 %3x%4").arg(strip->x()).arg(strip->y()).arg(strip->width()).arg(strip->height()) : QString());
        check(title != nullptr && title->text() == wizard.windowTitle(), "TL2 向导条上写着 windowTitle");
        const QWidget *page = wizard.currentPage();
        bool ruler = false;  // Qt 那条 2px 凹槽线不该再露出来（换成 paintEvent 画的极淡线）
        for (QWidget *child : wizard.findChildren<QWidget *>())
            ruler = ruler || (child->isVisible() && child->height() == 2 && !qobject_cast<QAbstractButton *>(child));
        check(!ruler, "TL2 向导底栏不再有 Qt 自带的凹槽分隔线");
        const int pageTop = page != nullptr ? page->mapTo(&wizard, QPoint(0, 0)).y() : -1;
        check(pageTop >= ChromeDialog::stripHeight(), "TL2 向导页面整体在条下面，不被条压住", QString::number(pageTop));
        check(wizard.promptHost() != nullptr && wizard.promptHost()->parentWidget() != &wizard,
              "TL2 向导的提示卡盖内容区、不盖标题条（× 和拖动照常可用）");
        check(wizard.findChild<QAbstractButton *>(ChromeDialog::closeButtonName()) != nullptr
                  && wizard.findChildren<TitleBarButton *>().size() == 3,
              "TL2 向导条上有最小化 / 最大化 / 关闭三颗", QString::number(wizard.findChildren<TitleBarButton *>().size()));
        // 页头：每页自己的主标题 + 「第 n 步 / 共 4 步」+ 四颗步骤点；正文区离窗左右 24、离条下沿 16。
        for (int id = 0; id < 4; ++id) {
            if (id > 0) wizard.next();
            app.processEvents();
            QWidget *current = wizard.currentPage();
            auto *heading = current != nullptr ? current->findChild<QLabel *>(QStringLiteral("instanceUpdateStepTitle")) : nullptr;
            auto *step = current != nullptr ? current->findChild<QLabel *>(QStringLiteral("instanceUpdateStepSubtitle")) : nullptr;
            auto *dots = current != nullptr ? current->findChild<QWidget *>(QStringLiteral("instanceUpdateStepDots")) : nullptr;
            const QPoint at = heading != nullptr ? heading->mapTo(&wizard, QPoint(0, 0)) : QPoint(-1, -1);
            const int dotsRight = dots != nullptr ? dots->mapTo(&wizard, QPoint(dots->width(), 0)).x() : -1;
            check(wizard.currentId() == id && heading != nullptr && heading->isVisible()
                      && heading->font().pixelSize() >= 14 && step != nullptr
                      && step->text() == QStringLiteral("第 %1 步 / 共 4 步").arg(id + 1) && step->font().pixelSize() >= 12,
                  QStringLiteral("TL2 向导第 %1 页页头：主标题 ≥14 + 步骤副标题 ≥12").arg(id + 1).toUtf8().constData(),
                  heading != nullptr ? QStringLiteral("%1 %2px").arg(heading->text()).arg(heading->font().pixelSize()) : QString());
            check(at == QPoint(24, ChromeDialog::stripHeight() + 16) && dotsRight == wizard.width() - 24,
                  QStringLiteral("TL2 向导第 %1 页正文区左右 24、条下 16").arg(id + 1).toUtf8().constData(),
                  QStringLiteral("%1,%2 dotsRight=%3").arg(at.x()).arg(at.y()).arg(dotsRight));
        }
        QAbstractButton *finish = wizard.button(QWizard::FinishButton);
        check(finish != nullptr && finish->mapTo(&wizard, QPoint(finish->width(), finish->height()))
                                       == QPoint(wizard.width() - 24, wizard.height() - 16),
              "TL2 向导底栏按钮靠右下、和正文同一条边距");
        wizard.restart();
        if (auto *close = wizard.findChild<QAbstractButton *>(ChromeDialog::closeButtonName())) close->click();
        check(!wizard.isVisible(), "TL2 向导没在跑时点 × = 取消、收起");
    }
    return g_fail == 0 ? 0 : 1;
}
