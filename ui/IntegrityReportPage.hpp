/**
 * @file IntegrityReportPage.hpp
 * @brief 完整性检查报告页（coding plan §4.1）：体检跑完之后，把结论摊在内容区上的那一整页。
 * @author 0x7fffff
 * @date 2026-10-12
 *
 * 【它和任务详情页是一个形状的"页"，不是窗口】
 *
 * 开出来的方式一模一样：`OverlayHost::execPage()` 把这一页整块盖在内容区上（**顶上四颗导航
 * 和底栏不盖**），顶上那条 `← 返回` 是它的一条出路，页面上那颗「只查看」是另一条。`QDialog`
 * 只是全仓所有"内嵌页"共用的壳（`OverlayHost` 认它）。
 *
 * 【为什么它认的是自己那套 `Report`，不是 `VersionIntegrity::Report`】
 *
 * `ui/` **不 include `src/`**（见 `ui/MainWindowUI.hpp` 与 `MainWindow.cpp` 里那几段）。
 * 所以这一页拿到的是"画得出来的一组字符串"：组名、缺几个、坏几个、逐条的「名字 / 那句话 /
 * 路径」。把 `VersionIntegrity::Report` 翻成这个形状的**唯一翻译点**在 `MainWindow`
 * （`integrityReportForPage()`）—— 和 `taskCenterItem()` 翻任务账本是同一个路数。
 *
 * 【三颗按钮都不动手，只报"点了哪一个"】
 *
 * 「复制报告」「补齐缺失或损坏文件」真正的活（拼那份纯文本进剪贴板、开一批补文件的下载）
 * 全在 `MainWindow` —— 这一页手里既没有 `VersionIntegrity` 也没有 `VersionManager`。
 * 「只查看」不是动作，它就是 `reject()`（= 覆盖层那条返回路），所以它不走信号。
 */

#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

class IntegrityReportPage : public QDialog {
    Q_OBJECT

public:
    /// 报告里的一行：一个要修的文件（全是**显示用**的字符串）。
    struct Row {
        QString label;   ///< 给人看的名字（`客户端 jar` / 库坐标 / `资源对象 xxx`）
        QString detail;  ///< 那句话（`缺少文件` / `大小 … 字节，应为 …`）
        QString path;    ///< 它的绝对路径
    };
    /// 报告里的一组（客户端 / 库 / 资源索引 / 资源对象）。
    struct Group {
        QString name;      ///< 组名（`客户端` / `库` / …）
        int missing = 0;   ///< 这一组缺几个
        int corrupt = 0;   ///< 这一组坏几个
        QList<Row> rows;   ///< 这一组的逐条问题
        int problems() const { return missing + corrupt; }
    };
    /// 一整份报告（`MainWindow::integrityReportForPage()` 是唯一翻译点）。
    struct Report {
        QString instanceName;  ///< 哪个实例
        QString summary;       ///< 一句话结论（`3120 个文件里 2 个要修：…`）
        QString error;         ///< 非空 = 这一趟**压根没跑起来**（读不出 json 那种）
        /// 四组，**一个都不省**（`0 个要修`也留着 —— 省掉的话用户分不清是"没问题"还是
        /// "没查这一组"，和 `VersionIntegrity::allGroups()` 那条是同一件事）。
        QList<Group> groups;

        bool ok() const { return error.isEmpty() && problems() == 0; }
        int problems() const {
            int n = 0;
            for (const Group &g : groups) {
                n += g.problems();
            }
            return n;
        }
    };

    /// 用户在这一页上点了什么。**动作的语义全在 MainWindow 那边**。
    enum class Action {
        CopyReport,  ///< 「复制报告」：把这一份结论拼成纯文本放进剪贴板。
        Repair,      ///< 「补齐缺失或损坏文件」：开一批补文件的下载（复用补全那条路）。
    };

    explicit IntegrityReportPage(QWidget *parent = nullptr);

    /// 这一页的标题（`OverlayHost::execPage` 顶上那一条写的字）。
    static QString windowTitle() { return QStringLiteral("完整性检查"); }

    /// 摆一份报告。**每次开页都重新喂一遍**（同一页对象会被复用，见 MainWindow 那边）。
    void setReport(const Report &report);

    /// 用户点了 @p action（按钮的信号槽直接转发到它）。`MainWindow` 接 `actionRequested`。
    void requestAction(Action action) { emit actionRequested(action); }

signals:
    /// 用户点了某颗动作按钮（「只查看」不走这里，它是 `reject()`）。
    void actionRequested(IntegrityReportPage::Action action);

private:
    /// 按 `m_report` 把整页重画一遍。
    void rebuild();

    Report m_report;
    QVBoxLayout *m_box = nullptr;
    /// 每次 `setReport()` 都整块重建这一块（四组的内容都长在这儿）。
    QWidget *m_body = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_repairButton = nullptr;
};
