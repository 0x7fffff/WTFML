/**
 * @file TaskDetailPage.hpp
 * @brief 任务详情页（coding plan §3.2 / §3.4）：点任务中心里的一行，在内容区整块开出来的那一页。
 * @author 0x7fffff
 * @date 2026-10-12
 *
 * 【它是"页"，不是窗口】
 *
 * 开出来的方式和资源详情页 / 实例设置一样：`OverlayHost::execPage()` 把这一页整块盖在内容区上
 * （**顶上四颗导航和底栏不盖**），顶上那条 `← 返回` 就是这一页的出路。`QDialog` 只是个壳 ——
 * 它是全仓所有"内嵌页"的共同基类形状（`OverlayHost` 认 `QDialog`），不是"要另开一个窗口"。
 *
 * 【它一个字节的业务都不碰】
 *
 * `ui/` 不 include `src/`。所以这一页拿到的是 `TaskCenter::Item`（面板那一行的同一份形状），
 * 用户点的那几颗按钮只发一个 `actionRequested(key, action)` 出去 —— 取消谁、重试什么、日志在哪、
 * 摘要是哪几句，全由 `MainWindow` 说了算（它手里才有任务账本、下载器、日志窗）。
 *
 * 【按钮为什么按"能力位"摆，而不按状态推】
 *
 * 上面那一排的露与不露**只读 `Item::capabilities`**（那是账本在收尾那一刻记下来的"当时允许
 * 什么"）。这一页要是自己按状态再推一遍，推出来的和账本记的迟早会漂开，而漂开的表现是
 * "界面上有一颗点了没反应的按钮" —— 正是 §3.4 想避免的那种。
 *
 * 【为什么"失败"要专门留一块地方写原因】
 *
 * §3.4：「任务失败能看到失败阶段、简短原因和'查看日志/重试'」。失败阶段 = 步骤那一栏里那个
 * `✗`；简短原因 = `Item::errorSummary` 那一块。两样都要**在页上一眼看得见**，不能只留在日志里
 * —— 日志是给"还想往下查"的人看的。
 */

#pragma once

#include <QDialog>
#include <QString>

#include "TaskCenterPanel.hpp"

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

class TaskDetailPage : public QDialog {
    Q_OBJECT

public:
    /// 用户在这一页上点了什么。**动作的语义全在 MainWindow 那边**，这里只报"点了哪一个"。
    enum class Action {
        Cancel,       ///< 「取消」：掐掉当前这一批下载。
        Retry,        ///< 「重试」：只重做上一次失败的那几个文件。
        OpenLog,      ///< 「打开日志」：开日志窗（那一个是全仓唯一允许另开窗口的东西）。
        OpenTarget,   ///< 「打开目标位置」：在文件管理器里打开这一项的目标（实例目录 / 落点目录）。
        CopySummary,  ///< 「复制摘要」：把这一项的摘要（含错因）放进剪贴板。
    };

    explicit TaskDetailPage(QWidget *parent = nullptr);

    /// 这一页的标题（`OverlayHost::execPage` 顶上那一条写的字）。
    static QString windowTitle() { return QStringLiteral("任务详情"); }

    /// 摆一项任务。**每次开页都重新喂一遍**（同一页对象会被复用，见 MainWindow 那边）。
    ///
    /// @param item 面板那一行的同一份形状（`MainWindow::taskCenterItems()` 是唯一翻译点）。
    void setTask(const TaskCenter::Item &item);

    /// 现在摆的是哪一项（上面那几个槽回话时要带上它 —— `OverlayHost` 是**嵌套**事件循环，
    /// 页开着的时候账本可能已经又动了）。
    QString taskKey() const { return m_key; }

    // ---------- 供验证工程与 MainWindow 调的那几个槽 ----------
    // NOTE: 按钮那条路是"点一下 → 发一个信号"，真正的干活在 MainWindow。这几个槽是**另一条
    //       路**（MainWindow 自己也可能从别处触发同一个动作），所以按钮的信号和这些槽
    //       故意不是同一个东西。

    /// 用户点了 @p action（按钮的信号槽直接转发到它）。`MainWindow` 接 `actionRequested`。
    void requestAction(Action action) { emit actionRequested(m_key, action); }

signals:
    /// 用户点了某颗按钮（`key` 是 `setTask()` 那一刻那一项的钥匙）。
    void actionRequested(const QString &key, TaskDetailPage::Action action);

private:
    /// 按 `m_item` 把整页重画一遍。
    void rebuild();
    /// 造一颗动作按钮（挂到自己身上，点了就 `requestAction()`）。
    QPushButton *makeActionButton(const QString &text, Action action,
                                  const QString &objectName);

    TaskCenter::Item m_item;
    QString m_key;
    QVBoxLayout *m_box = nullptr;
    /// 每次 `setTask()` 都会整块重建这一块内容（标题下面那几行）。
    QWidget *m_body = nullptr;

    QLabel *m_stateLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_detailLabel = nullptr;
    QLabel *m_bytesLabel = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QProgressBar *m_bar = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_retryButton = nullptr;
    QPushButton *m_logButton = nullptr;
    QPushButton *m_targetButton = nullptr;
    QPushButton *m_copyButton = nullptr;
};
