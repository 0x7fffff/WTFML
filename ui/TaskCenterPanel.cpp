/**
 * @file TaskCenterPanel.cpp
 * @brief 任务中心那块面板的实现（形状与取舍见头文件）。
 * @author 0x7fffff
 * @date 2026-10-12
 */

#include "TaskCenterPanel.hpp"

#include <algorithm>

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
// `setProperty()` 收的是 QVariant（那一行"这行是谁"的钥匙）—— 少了它，编译器报的是
// "no matching function for call to setProperty(QString, const QString&)"，
// 看着像"这个函数不存在"，其实是 QVariant 没定义。
#include <QVariant>

#include "Animations.hpp"
#include "ElidedLabel.hpp"
#include "MainWindowUI.hpp"
#include "TaskProgressBar.hpp"
#include "Theme.hpp"
#include "VersionIcons.hpp"

namespace {

bool isActive(TaskCenter::State state) {
    return state == TaskCenter::State::Queued || state == TaskCenter::State::Running
           || state == TaskCenter::State::WaitingUser || state == TaskCenter::State::Paused;
}

// ---------- 这一块面板自己的几个尺寸 ----------
//
// NOTE: 这里的留白**另起一套**（不复用 `MainWindowUI.cpp` 里 `BottomBarPanel` 那几个
//       `kPanelPadding` / `kPanelRowSpacing`）：那一套是给"一行一个实例"的短行调的
//       （行高矮、行挨得近），这一块里是标题 + 小字 + 进度条 + 步骤，行高一档。两个面板
//       长得很不一样，共用一个数只会两边都不合适。
constexpr int kPanelPadding = 14;
/// 两行之间（同一个按钮那一栏里）。
constexpr int kRowSpacing = 9;
/// 两栏之间（「当前工作」和「已完成」中间那一道）：比行距大一倍，分开两栏。
constexpr int kSectionSpacing = 18;
/// 一行里面那几样之间。
constexpr int kRowInnerSpacing = 6;
/// 三列工作区的宽度；窄屏会自动退回单列任务列表。
constexpr int kCompactWidth = 520;
constexpr int kWorkspaceMinWidth = 820;
constexpr int kWorkspaceMaxWidth = 1120;
constexpr int kRunningWidth = 220;
constexpr int kDetailWidth = 300;
/// 高度上限 = 窗口可用高度的百分之多少（§3.2：60%）。
constexpr int kHeightPercent = 72;
/// 面板和那颗按钮之间那一道缝（和 `BottomBarPanel` 的 `kPanelGap` 同一个值 ——
/// 两块面板都是从同一颗按钮往上弹的，缝不一样宽看着就是"两块面板没对齐"）。
constexpr int kPanelGap = 6;
/// 标题/小字那一格比面板最多窄这么点（左右边距 + 右边那个百分比/时间的位置）。
constexpr int kTextMaxWidth = kCompactWidth - 2 * kPanelPadding - 56;

/// 小一号、灰一点的字（"正在下载资源对象"、"19:42"、栏名都用它）。
///
/// ★ 参数是 `QWidget *` 而不是 `QLabel *`：面板底下那颗「清除已完成」是 `QPushButton`，
///   它也要这一身灰字。**两个色角色都要摆** —— QLabel 的字取 `WindowText`，QPushButton
///   的字取的是 `ButtonText`；只摆前者的话按钮上那四个字会一点没变（不报错、不崩，
///   看着就是"这句 setPalette 没生效"）。
void applyMutedFont(QWidget *widget, int pointDelta) {
    QFont font = widget->font();
    font.setPointSize(qMax(1, font.pointSize() + pointDelta));
    widget->setFont(font);
    const QColor muted = widget->palette().color(QPalette::PlaceholderText);
    QPalette palette = widget->palette();
    palette.setColor(QPalette::WindowText, muted);
    palette.setColor(QPalette::ButtonText, muted);
    widget->setPalette(palette);
}

/// 一行的宽度上限：面板最宽那一档减掉左右边距、再给右边那个百分比/时间留一段。
ElidedLabel *makeTextLabel(QWidget *parent, const QString &objectName) {
    auto *label = new ElidedLabel(parent);
    label->setObjectName(objectName);
    label->setMaximumWidth(kTextMaxWidth);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return label;
}

/**
 * @brief 任务中心里的一行（活动的和已完成的共用这一个类，画法按 `Item::active` 分）。
 *
 * 行里每一块的 objectName 都带上**行号**（`MainWindowUI::taskCenterPanelRow*Name()` 给的）
 * —— 验证工程不用数"这一行里有几个 QLabel"这种一改就碎的判据。
 *
 * ★ 整行**可点**（2026-10-12，M2）：点一下进任务详情页（coding plan §3.2 的"点击任务进入
 *   内嵌 `TaskDetailPage`"）。M1 里这一行故意不点 —— 那时候详情页还不存在，"点了没反应的
 *   整行"比不给还糟。现在它只在**真挂上了处理器**的时候才装作可点（手型光标 + 悬停底色），
 *   没挂的时候就是一行字（`MainWindow` 那边永远会挂，挂不上的只有验证工程搭的裸面板）。
 */
class TaskCenterRowWidget : public QWidget {
public:
    TaskCenterRowWidget(const TaskCenter::Item &item, int index,
                        const std::function<void()> &onActivate, QWidget *parent,
                        const TaskCenter::RowActionHandler &onRowAction)
        : QWidget(parent), m_onActivate(onActivate) {
        if (m_onActivate) {
            setCursor(Qt::PointingHandCursor);
        }
        setObjectName(MainWindowUI::taskCenterPanelRowName(index));
        // 行是谁：面板里认这一行靠它（行号会随着"清掉一条已完成"整体挪位，钥匙不会）。
        // 这个角色只有一个读者 —— `MainWindow` 那边按 `Item::key` 喂进来，以后要做
        // "点这一行进详情页"时按它回认任务；验证工程也拿它核对"画出来的是不是那几条"。
        setProperty(MainWindowUI::taskCenterPanelRowKeyProperty(), item.key);

        auto *column = new QVBoxLayout(this);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(kRowInnerSpacing);

        // ---------- 第一行：记号（可选）+ 标题 + 右边那个数 ----------
        auto *titleRow = new QHBoxLayout();
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(kRowInnerSpacing);
        const bool active = isActive(item.state);
        const bool failed = item.state == TaskCenter::State::Failed;
        const bool interrupted = item.state == TaskCenter::State::Interrupted;
        if (item.unreadFailure) {
            // 用户还没看过的一次失败：一颗红点（图的颜色问 `folderDangerColor()`，和界面上
            // 别处的红是**同一个色** —— 见 MainWindowUI 里 runStateMarkColor() 那段）。
            auto *dot = new QLabel(this);
            dot->setObjectName(MainWindowUI::taskCenterPanelRowDotName(index));
            const int size = MainWindowUI::taskCenterPanelFailureDotSize();
            dot->setFixedSize(size, size);
            dot->setPixmap(VersionIcons::runStateMark(size,
                                                      MainWindowUI::folderDangerColor(palette())));
            titleRow->addWidget(dot, 0, Qt::AlignVCenter);
        } else if (!active) {
            // 已完成那一行左边那个记号：成功 / 失败 / 取消 / 中断各自说清楚。
            auto *mark = new QLabel(TaskCenter::terminalMark(item.state), this);
            if (failed || interrupted) {
                QPalette markPalette = mark->palette();
                markPalette.setColor(QPalette::WindowText,
                                     MainWindowUI::folderDangerColor(palette()));
                mark->setPalette(markPalette);
            } else {
                applyMutedFont(mark, 0);
            }
            titleRow->addWidget(mark, 0, Qt::AlignVCenter);
        }
        ElidedLabel *title = makeTextLabel(this, MainWindowUI::taskCenterPanelRowTitleName(index));
        title->setText(item.title);
        titleRow->addWidget(title, 1, Qt::AlignVCenter);
        // ---------- 行尾那颗「取消」（§3.2 的行内快捷动作）----------
        // 只在**能力位说可以**、而且外面真挂了处理器的时候才长。没挂却露出来 = 一颗点了
        // 没反应的按钮（这一面板是能单独搭起来用的，验证工程那几处就没挂）。
        // NOTE: 它是**真按钮**（不是画出来靠事件过滤器认位置），所以点它**不会**同时触发
        //       整行那一下 —— 父子控件的鼠标事件不会一起送给两边。
        if (item.capabilities.cancel && onRowAction) {
            auto *cancel = new QPushButton(MainWindowUI::taskCenterPanelRowCancelText(), this);
            cancel->setObjectName(MainWindowUI::taskCenterPanelRowCancelName(index));
            cancel->setCursor(Qt::PointingHandCursor);
            cancel->setFlat(true);
            applyMutedFont(cancel, -1);
            const QString key = item.key;
            // 收下这一下的**是这颗按钮自己**（上下文传 cancel）：这一行随时会被整块重建，
            // 挂在这一行身上等于给一个马上就要没的东西接信号。
            QObject::connect(cancel, &QPushButton::clicked, cancel, [onRowAction, key]() {
                if (onRowAction) {
                    onRowAction(key, TaskCenter::RowAction::Cancel);
                }
                // ★ 这里**故意不重建面板**：取消要走一趟异步（下载器得先把在飞的那几条收
                //   干净才发终态），当场重画的话画出来的还是"正在进行"那一行，看着像点了没
                //   反应。等账本那边 `taskChanged` 传来，`refreshTaskCenterPanel()` 会把这里
                //   重画一遍。没成功取消（那一批刚好已经收尾）时账本一动不动、这一行也
                //   不动 —— 那一刻它确实没有新状态可说。
            });
            titleRow->addWidget(cancel, 0, Qt::AlignVCenter);
        }
        if (active && item.percent >= 0) {
            auto *percent = new QLabel(MainWindowUI::taskCenterPercentText(item.percent), this);
            percent->setObjectName(MainWindowUI::taskCenterPanelRowPercentName(index));
            applyMutedFont(percent, 0);
            titleRow->addWidget(percent, 0, Qt::AlignVCenter);
        } else if (!active && item.finishedAt.isValid()) {
            auto *when = new QLabel(TaskCenter::timeText(item.finishedAt), this);
            when->setObjectName(MainWindowUI::taskCenterPanelRowTimeName(index));
            applyMutedFont(when, 0);
            titleRow->addWidget(when, 0, Qt::AlignVCenter);
        }
        column->addLayout(titleRow);

        // ---------- 第二行：小字（"正在下载资源对象 · 2.8 MB/s"） ----------
        if (!item.detail.isEmpty()) {
            ElidedLabel *detail =
                makeTextLabel(this, MainWindowUI::taskCenterPanelRowDetailName(index));
            detail->setText(item.detail);
            applyMutedFont(detail, -1);
            column->addWidget(detail);
        }

        // ---------- 第三样：进度条（只在**测得出百分比**时才有） ----------
        // NOTE: 测不出总量的时候**不画一根不确定态的条**（coding plan §3.4：「无
        //       Content-Length 的 Fabric 下载显示'已下载多少'，不显示假百分比」）。
        //       那种情况下这一行的信息在 `detail` 那句里（"已下载 2.8 MB"）。
        if (active && item.percent >= 0) {
            // 自绘那条（§3.3）：条本身就**不画数**，数在上面那一行的 `percent` 里。
            auto *bar = new TaskProgressBar(this);
            bar->setObjectName(MainWindowUI::taskCenterPanelRowBarName(index));
            bar->setRange(0, 100);
            bar->setValue(item.percent);
            bar->setFixedHeight(MainWindowUI::taskCenterPanelBarHeight());
            column->addWidget(bar);
        }

        // ---------- 第四样：步骤（"✓ 校验文件  ✓ 准备 Java  ● 等待游戏输出"） ----------
        if (!item.steps.isEmpty()) {
            auto *stepsRow = new QHBoxLayout();
            stepsRow->setContentsMargins(0, 0, 0, 0);
            stepsRow->setSpacing(MainWindowUI::taskCenterPanelStepSpacing());
            for (int i = 0; i < item.steps.size(); ++i) {
                const TaskCenter::Step &step = item.steps.at(i);
                auto *label = new QLabel(this);
                label->setObjectName(MainWindowUI::taskCenterPanelRowStepName(index, i));
                label->setText(TaskCenter::stepMark(step.state) + QLatin1Char(' ') + step.name);
                if (step.state == TaskCenter::StepState::Failed) {
                    QPalette stepPalette = label->palette();
                    stepPalette.setColor(QPalette::WindowText,
                                         MainWindowUI::folderDangerColor(palette()));
                    label->setPalette(stepPalette);
                } else {
                    applyMutedFont(label, -1);
                }
                stepsRow->addWidget(label, 0, Qt::AlignVCenter);
            }
            // 弹簧摆在最后：步骤从左往右排，右边不留空（不摊开）。
            stepsRow->addStretch(1);
            column->addLayout(stepsRow);
        }
    }

protected:
    void mousePressEvent(QMouseEvent *event) override {
        // 左键那一下才开口。**收下这一下**（不往下传）：面板是 Qt::Popup，不收的话它会一路
        // 走到面板的 handleOutsidePress()，把这一下当成"点在面板外面"顺手收回去 ——
        // 症状是"点一行，面板没了，详情页也没开"。
        if (event->button() == Qt::LeftButton && m_onActivate) {
            event->accept();
            m_onActivate();
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    std::function<void()> m_onActivate;
};

}  // namespace

QString TaskCenter::stepMark(StepState state) {
    // 四个记号是**画出来的一串字符**，不是图标：它们和那行步骤的正文同色同字号，混在
    // 一句里读得下去（换成图标就得跟着调色、还要在两栏里各摆一次）。
    switch (state) {
    case StepState::Pending:
        return QCoreApplication::translate("TaskCenter", "○");
    case StepState::Running:
        return QCoreApplication::translate("TaskCenter", "●");
    case StepState::Done:
        return QCoreApplication::translate("TaskCenter", "✓");
    case StepState::Failed:
        return QCoreApplication::translate("TaskCenter", "✗");
    }
    return QString();
}

QString TaskCenter::terminalMark(State state) {
    switch (state) {
    case State::Succeeded:
        return QCoreApplication::translate("TaskCenter", "✓");
    case State::Failed:
        return QCoreApplication::translate("TaskCenter", "✗");
    case State::Cancelled:
        return QCoreApplication::translate("TaskCenter", "—");
    case State::Interrupted:
        return QCoreApplication::translate("TaskCenter", "!");
    case State::Queued:
    case State::Running:
    case State::WaitingUser:
    case State::Paused:
        return QString();
    }
    return QString();
}

QString TaskCenter::timeText(const QDateTime &when) {
    if (!when.isValid()) {
        return QString();
    }
    // 只写"几点几分"：已完成那一栏里最远的东西也就是几十条之前那一次，日期那一截
    // （"2026-10-12 19:42"）会把这半行撑满，而它旁边那个位置本来就是给"什么时候"留的。
    return when.toString(QStringLiteral("HH:mm"));
}

TaskCenterPanel::TaskCenterPanel(QWidget *anchor, const TaskCenter::ItemProvider &provider,
                                 const TaskCenter::ClearFinishedHandler &onClear,
                                 const TaskCenter::RunningProvider &runningProvider,
                                 const TaskCenter::RunningActionHandler &runningAction,
                                 const OutsidePressHandler &onOutsidePress, QWidget *parent,
                                 bool embedded)
    : QWidget(parent, embedded ? Qt::Widget : Qt::Popup | Qt::FramelessWindowHint),
      m_anchor(anchor),
      m_provider(provider),
      m_onClear(onClear),
      m_runningProvider(runningProvider),
      m_runningAction(runningAction),
      m_onOutsidePress(onOutsidePress),
      m_embedded(embedded) {
    setObjectName(MainWindowUI::taskCenterPanelName());
    // 底色和底栏那两块面板**同一档**（`Theme::Part::Panel`）：三块都是从底栏弹出来的，
    // 颜色不一样就成了"三块不同的东西"。理由全文见 MainWindowUI.cpp 里 BottomBarPanel 那段。
    setAutoFillBackground(true);
    QPalette panelPalette = QApplication::palette();
    panelPalette.setColor(QPalette::Window, Theme::partBackground(Theme::Part::Panel));
    setPalette(panelPalette);

    auto *workspace = new QHBoxLayout(this);
    workspace->setContentsMargins(kPanelPadding, kPanelPadding, kPanelPadding, kPanelPadding);
    workspace->setSpacing(kSectionSpacing);

    // 左列：运行中实例。按钮可以把整列收成一条窄边，给任务列表让位置。
    m_runningPane = new QWidget(this);
    m_runningPane->setObjectName(QStringLiteral("taskCenterRunningPane"));
    m_runningPane->setFixedWidth(kRunningWidth);
    auto *runningLayout = new QVBoxLayout(m_runningPane);
    runningLayout->setContentsMargins(0, 0, 0, 0);
    runningLayout->setSpacing(kRowSpacing);
    auto *runningHeader = new QHBoxLayout;
    auto *runningTitle = new QLabel(tr("运行中实例"), m_runningPane);
    runningHeader->addWidget(runningTitle);
    runningHeader->addStretch(1);
    m_collapseRunning = new QPushButton(QStringLiteral("‹"), m_runningPane);
    m_collapseRunning->setObjectName(QStringLiteral("taskCenterRunningCollapse"));
    m_collapseRunning->setToolTip(tr("收起运行中实例"));
    m_collapseRunning->setAccessibleName(tr("收起运行中实例"));
    m_collapseRunning->setCheckable(true);
    m_collapseRunning->setChecked(true);
    // 鼠标点击不留下焦点框；Tab 仍可聚焦并用空格切换。
    m_collapseRunning->setFocusPolicy(Qt::TabFocus);
    Theme::markButton(m_collapseRunning, Theme::ButtonKind::Ghost);
    Theme::markIconButton(m_collapseRunning);
    m_collapseRunning->setFixedSize(28, 28);
    runningHeader->addWidget(m_collapseRunning);
    runningLayout->addLayout(runningHeader);
    m_runningScroll = new QScrollArea(m_runningPane);
    m_runningScroll->setFrameShape(QFrame::NoFrame);
    m_runningScroll->setWidgetResizable(true);
    m_runningScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_runningContent = new QWidget(m_runningScroll);
    m_runningContent->setLayout(new QVBoxLayout);
    m_runningScroll->setWidget(m_runningContent);
    runningLayout->addWidget(m_runningScroll, 1);
    workspace->addWidget(m_runningPane);

    connect(m_collapseRunning, &QPushButton::toggled, this, [this, runningTitle](bool expanded) {
        m_runningCollapsed = !expanded;
        runningTitle->setVisible(expanded);
        m_runningScroll->setVisible(!m_runningCollapsed);
        m_runningPane->setFixedWidth(m_runningCollapsed ? 36 : kRunningWidth);
        m_collapseRunning->setText(m_runningCollapsed ? QStringLiteral("›") : QStringLiteral("‹"));
        m_collapseRunning->setToolTip(m_runningCollapsed ? tr("展开运行中实例")
                                                         : tr("收起运行中实例"));
        m_collapseRunning->setAccessibleName(m_collapseRunning->toolTip());
        place(false);
    });

    // 中列：按时间和状态排列的任务横行。
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("taskCenterTaskList"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    // 横向**永远不滚**：中列跟随工作区宽度伸缩，行里的字自己省略号收尾。
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFocusPolicy(Qt::NoFocus);
    m_content = new QWidget(m_scroll);
    m_content->setAutoFillBackground(false);
    auto *contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(kRowSpacing);
    contentLayout->addStretch(1);
    m_scroll->setWidget(m_content);

    workspace->addWidget(m_scroll, 1);

    // 右列：选中任务的上下文详情；未选择时显示摘要。
    m_detailPane = new QWidget(this);
    m_detailPane->setObjectName(QStringLiteral("taskCenterDetailPane"));
    m_detailPane->setFixedWidth(kDetailWidth);
    auto *detailLayout = new QVBoxLayout(m_detailPane);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    m_detailScroll = new QScrollArea(m_detailPane);
    m_detailScroll->setFrameShape(QFrame::NoFrame);
    m_detailScroll->setWidgetResizable(true);
    m_detailScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_detailContent = new QWidget(m_detailScroll);
    m_detailContent->setObjectName(QStringLiteral("taskCenterDetailContent"));
    m_detailContent->setLayout(new QVBoxLayout);
    m_detailScroll->setWidget(m_detailContent);
    detailLayout->addWidget(m_detailScroll);
    workspace->addWidget(m_detailPane);

    rebuild();
}

void TaskCenterPanel::popUp() {
    rebuild();
    place(/*animate=*/true);
    show();
    raise();
    // 拿焦点只为一件事：Esc 能关（外面点一下 Qt 自己会关，那条不用管）。
    setFocus(Qt::PopupFocusReason);
    m_opened = true;
}

void TaskCenterPanel::showEmbedded() {
    m_opened = true;
    m_compact = width() > 0 && width() < kWorkspaceMinWidth;
    applyWorkspaceMode();
    rebuild();
    show();
}

std::function<void()> TaskCenterPanel::rowActivate(const QString &key) {
    // 每一行各捕一份**自己那把钥匙**：处理器只被告知"点的是哪一项"，不被告知"点的是第几行"
    // —— 行号会随着"清掉一条已完成"整体往上挪，面板开着的时候用户点的那一行早就不是
    // 他看见的那一行了。
    if (!m_embedded && !m_onRowActivated) {
        return {};
    }
    return [this, key]() {
        selectTask(key);
        if (m_embedded && m_compact) {
            m_compactDetailOpen = true;
            applyWorkspaceMode();
        } else if (m_compact && m_onRowActivated) {
            m_onRowActivated(key);
        }
    };
}

void TaskCenterPanel::applyWorkspaceMode() {
    if (!m_embedded) {
        return;
    }
    m_runningPane->setVisible(!m_compact);
    if (!m_compact) {
        m_scroll->show();
        m_detailPane->show();
        m_detailPane->setMinimumWidth(kDetailWidth);
        m_detailPane->setMaximumWidth(kDetailWidth);
        m_detailPane->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        return;
    }

    // 窄屏不再尝试从已经打开的 OverlayHost 再开一层详情页：任务列表和详情在同一页内切换。
    m_runningPane->hide();
    m_scroll->setVisible(!m_compactDetailOpen);
    m_detailPane->setVisible(m_compactDetailOpen);
    m_detailPane->setMinimumWidth(0);
    m_detailPane->setMaximumWidth(QWIDGETSIZE_MAX);
    m_detailPane->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void TaskCenterPanel::refresh() {
    if (!m_opened) {
        return;  // 还没弹出来过：下一次 popUp() 自己会重建
    }
    rebuild();
    place(/*animate=*/false);
}

void TaskCenterPanel::requestRefresh() {
    if (m_refreshPending) return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_refreshPending = false;
        if (m_opened && isVisible()) refresh();
    });
}

void TaskCenterPanel::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    requestRefresh();
}

void TaskCenterPanel::place(bool animate) {
    if (m_embedded) {
        const int availableWidth = qMax(0, width());
        m_compact = availableWidth > 0 && availableWidth < kWorkspaceMinWidth;
        applyWorkspaceMode();
        return;
    }
    if (m_anchor == nullptr) {
        return;
    }
    // 高度上限：屏幕**可用**高度（减掉任务栏那一截）的 60%。放在这里而不是构造里，
    // 是因为锚点所在的那块屏幕要等它真摆出来才知道（多显示器时"哪一块屏"由窗口位置定）。
    QScreen *screen = m_anchor->screen();
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect available = screen != nullptr ? screen->availableGeometry() : QRect();
    if (available.isValid()) {
        m_scroll->setMaximumHeight(available.height() * kHeightPercent / 100 - 2 * kPanelPadding);
        m_runningScroll->setMaximumHeight(m_scroll->maximumHeight());
        m_detailScroll->setMaximumHeight(m_scroll->maximumHeight());
    }

    const int availableWidth = available.isValid() ? available.width() - 16 : kWorkspaceMaxWidth;
    m_compact = availableWidth < kWorkspaceMinWidth;
    applyWorkspaceMode();
    adjustSize();
    const int wantedWidth = m_compact ? qMin(kCompactWidth, availableWidth)
                                      : qBound(kWorkspaceMinWidth, availableWidth,
                                               kWorkspaceMaxWidth);
    resize(wantedWidth, height());

    const QPoint anchorTopLeft = m_anchor->mapToGlobal(QPoint(0, 0));
    // 水平方向对齐那颗按钮的**中线**：这块面板通常比按钮宽得多，左对齐容易伸到窗口
    // 右边外面去。居中之后两边出头一样多。
    int x = anchorTopLeft.x() + m_anchor->width() / 2 - width() / 2;
    int y = anchorTopLeft.y() - height() - kPanelGap;
    if (available.isValid()) {
        // 贴边兜底：窗口贴着屏幕上边时（或任务多、面板高）往上放不下，就压进屏幕里
        // —— 顶出去的那一半用户是看不到的，也没法拖（它是 Qt::Popup）。
        const int xMax = available.right() - width() - 4;
        x = xMax >= available.left() + 4 ? qBound(available.left() + 4, x, xMax)
                                         : available.left();
        y = qMax(available.top() + 4, y);
    }
    const QPoint target(x, y);
    if (animate) {
        // 从下面滑上来（和底栏那两块面板同一个手法，见 ui/Animations.hpp）。
        Anim::slideUp(this, target);
    } else {
        move(target);
    }
}

void TaskCenterPanel::rebuild() {
    auto *layout = qobject_cast<QVBoxLayout *>(m_content->layout());
    if (layout == nullptr) {
        return;
    }
    // 清空（末尾那个 stretch 也一起拿走，重建时再补）。
    // NOTE: 这里用的是 delete（不是 deleteLater）：下面马上要往同一个布局里摆新行，
    //       留着一批"待删"的老行只会让一帧里看到新旧两套。这个函数**不许**被某一行的
    //       信号处理器直接调到（「清除已完成」那颗按钮走 QTimer 排下一轮，见下面），
    //       所以删到自己脚底下这种可能不存在。
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const QList<TaskCenter::Item> items =
        m_provider ? m_provider() : QList<TaskCenter::Item>();
    rebuildRunning();
    QList<TaskCenter::Item> active;
    QList<TaskCenter::Item> finished;
    for (const TaskCenter::Item &item : items) {
        (isActive(item.state) ? active : finished).append(item);
    }

    // 行的编号**两栏接着数**（不各从 0 开始）：行的名字是按号找的（验证工程靠它量），
    // 两栏各从 0 开始的话「当前工作」第 0 行和「已完成」第 0 行会撞成一个名字。
    int index = 0;
    const auto addSectionTitle = [&](const QString &text) {
        auto *label = new QLabel(text, m_content);
        applyMutedFont(label, -1);
        layout->addWidget(label);
    };

    if (items.isEmpty()) {
        // 一项都没有：面板里留一句话，不留一大块空白（和底栏那两块面板的空态一个路数）。
        auto *empty = new QLabel(MainWindowUI::taskCenterPanelEmptyText(), m_content);
        empty->setObjectName(MainWindowUI::taskCenterPanelEmptyLabelName());
        applyMutedFont(empty, 0);
        layout->addWidget(empty);
    }

    if (!active.isEmpty()) {
        addSectionTitle(MainWindowUI::taskCenterPanelActiveSectionText());
        for (const TaskCenter::Item &item : active) {
            layout->addWidget(new TaskCenterRowWidget(
                item, index, rowActivate(item.key), m_content, m_onRowAction));
            ++index;
        }
    }
    if (!finished.isEmpty()) {
        if (!active.isEmpty()) {
            layout->addSpacing(kSectionSpacing - kRowSpacing);
        }
        addSectionTitle(MainWindowUI::taskCenterPanelDoneSectionText());
        for (const TaskCenter::Item &item : finished) {
            layout->addWidget(new TaskCenterRowWidget(
                item, index, rowActivate(item.key), m_content, m_onRowAction));
            ++index;
        }
        // 「清除已完成」靠右摆（它是这一栏的收尾动作，不是一行的动作）。
        if (m_onClear) {
            auto *row = new QWidget(m_content);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->addStretch(1);
            auto *clear = new QPushButton(MainWindowUI::taskCenterPanelClearText(), row);
            clear->setObjectName(MainWindowUI::taskCenterPanelClearButtonName());
            clear->setCursor(Qt::PointingHandCursor);
            clear->setFlat(true);
            applyMutedFont(clear, -1);
            QWidget *const deferContext = m_content;  // 见下面那段 NOTE（别捕随时会没的东西）
            QObject::connect(clear, &QPushButton::clicked, clear,
                             [this, deferContext]() {
                                 if (m_onClear) {
                                     m_onClear();
                                 }
                                 // ★ 排到**下一轮**事件循环再重建：这一下是这颗按钮自己的
                                 //   信号处理器，当场重建等于把它自己在脚下删掉。
                                 QTimer::singleShot(0, deferContext, [this]() { refresh(); });
                             });
            rowLayout->addWidget(clear);
            layout->addWidget(row);
        }
    }
    // 内容比上限矮的时候，行堆在上面而不是被摊开（stretch 吃掉下面的空）。
    layout->addStretch(1);
    rebuildDetail(items);
}

void TaskCenterPanel::selectTask(const QString &key) {
    if (key.isEmpty() || key == m_selectedKey) {
        return;
    }
    m_selectedKey = key;
    const QList<TaskCenter::Item> items =
        m_provider ? m_provider() : QList<TaskCenter::Item>();
    rebuildDetail(items);
}

void TaskCenterPanel::rebuildRunning() {
    auto *layout = qobject_cast<QVBoxLayout *>(m_runningContent != nullptr
                                                  ? m_runningContent->layout()
                                                  : nullptr);
    if (layout == nullptr) {
        return;
    }
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QList<TaskCenter::RunningItem> rows =
        m_runningProvider ? m_runningProvider() : QList<TaskCenter::RunningItem>();
    if (rows.isEmpty()) {
        auto *empty = new QLabel(tr("当前没有运行中的实例"), m_runningContent);
        empty->setWordWrap(true);
        applyMutedFont(empty, -1);
        layout->addWidget(empty);
    }
    for (const TaskCenter::RunningItem &item : rows) {
        auto *card = new QWidget(m_runningContent);
        card->setObjectName(QStringLiteral("taskCenterRunningRow"));
        auto *column = new QVBoxLayout(card);
        column->setContentsMargins(0, 6, 0, 6);
        column->setSpacing(4);
        auto *head = new QHBoxLayout;
        if (!item.icon.isNull()) {
            auto *icon = new QLabel(card);
            icon->setPixmap(item.icon);
            icon->setFixedSize(24, 24);
            head->addWidget(icon, 0, Qt::AlignVCenter);
        }
        auto *texts = new QVBoxLayout;
        auto *title = new ElidedLabel(card);
        title->setText(item.title);
        texts->addWidget(title);
        if (!item.subtitle.isEmpty()) {
            auto *subtitle = new ElidedLabel(card);
            subtitle->setText(item.subtitle);
            applyMutedFont(subtitle, -1);
            texts->addWidget(subtitle);
        }
        head->addLayout(texts, 1);
        column->addLayout(head);
        if (!item.actions.isEmpty()) {
            auto *actions = new QHBoxLayout;
            actions->addStretch(1);
            for (const TaskCenter::RunningAction &action : item.actions) {
                auto *button = new QPushButton(card);
                if (!action.icon.isNull()) {
                    button->setIcon(QIcon(action.icon));
                    button->setIconSize(action.icon.size());
                    Theme::markIconButton(button);
                } else {
                    button->setText(action.text);
                    Theme::markButton(button, Theme::ButtonKind::Ghost);
                }
                button->setEnabled(action.enabled);
                button->setToolTip(action.tooltip);
                const QString key = item.key;
                const int actionId = action.action;
                connect(button, &QPushButton::clicked, button, [this, key, actionId]() {
                    if (m_runningAction) {
                        m_runningAction(key, actionId);
                    }
                });
                actions->addWidget(button);
            }
            column->addLayout(actions);
        }
        layout->addWidget(card);
    }
    layout->addStretch(1);
}

void TaskCenterPanel::rebuildDetail(const QList<TaskCenter::Item> &items) {
    auto *layout = qobject_cast<QVBoxLayout *>(m_detailContent != nullptr
                                                  ? m_detailContent->layout()
                                                  : nullptr);
    if (layout == nullptr) {
        return;
    }
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    auto selected = items.constEnd();
    if (!m_selectedKey.isEmpty()) {
        selected = std::find_if(items.constBegin(), items.constEnd(), [this](const auto &item) {
            return item.key == m_selectedKey;
        });
    }
    if (selected == items.constEnd()) {
        m_selectedKey.clear();
        m_detailContent->setProperty("taskKey", QVariant());
        int active = 0;
        int done = 0;
        int attention = 0;
        for (const TaskCenter::Item &item : items) {
            if (isActive(item.state)) {
                ++active;
            } else {
                ++done;
            }
            if (item.state == TaskCenter::State::WaitingUser
                || item.state == TaskCenter::State::Failed
                || item.state == TaskCenter::State::Interrupted) {
                ++attention;
            }
        }
        auto *title = new QLabel(tr("任务概览"), m_detailContent);
        QFont titleFont = title->font();
        titleFont.setBold(true);
        title->setFont(titleFont);
        layout->addWidget(title);
        for (const QString &line : {tr("正在进行：%1").arg(active),
                                    tr("已完成：%1").arg(done),
                                    tr("需要处理：%1").arg(attention)}) {
            auto *label = new QLabel(line, m_detailContent);
            layout->addWidget(label);
        }
        auto *hint = new QLabel(tr("在中间选择一项，可查看步骤、错误和可用操作。"),
                                m_detailContent);
        hint->setWordWrap(true);
        applyMutedFont(hint, -1);
        layout->addWidget(hint);
        layout->addStretch(1);
        return;
    }

    const TaskCenter::Item &item = *selected;
    m_detailContent->setProperty("taskKey", item.key);
    if (m_embedded && m_compact) {
        auto *back = new QPushButton(tr("← 返回任务列表"), m_detailContent);
        back->setObjectName(QStringLiteral("taskCenterCompactBack"));
        Theme::markButton(back, Theme::ButtonKind::Ghost);
        connect(back, &QPushButton::clicked, back, [this]() {
            m_compactDetailOpen = false;
            m_selectedKey.clear();
            rebuildDetail(m_provider ? m_provider() : QList<TaskCenter::Item>());
            applyWorkspaceMode();
        });
        layout->addWidget(back, 0, Qt::AlignLeft);
    }
    auto *title = new QLabel(item.title, m_detailContent);
    title->setObjectName(MainWindowUI::taskDetailTitleName());
    title->setWordWrap(true);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);
    auto *state = new QLabel(MainWindowUI::taskDetailStateText(item.state), m_detailContent);
    state->setObjectName(MainWindowUI::taskDetailStateName());
    applyMutedFont(state, 0);
    layout->addWidget(state);
    if (!item.detail.isEmpty()) {
        auto *detail = new QLabel(item.detail, m_detailContent);
        detail->setWordWrap(true);
        layout->addWidget(detail);
    }
    if (!item.bytesText.isEmpty()) {
        auto *bytes = new QLabel(item.bytesText, m_detailContent);
        applyMutedFont(bytes, -1);
        layout->addWidget(bytes);
    }
    if (isActive(item.state) && item.percent >= 0) {
        auto *bar = new TaskProgressBar(m_detailContent);
        bar->setRange(0, 100);
        bar->setValue(item.percent);
        bar->setFixedHeight(MainWindowUI::taskCenterPanelBarHeight());
        layout->addWidget(bar);
    }
    if (!item.errorSummary.isEmpty()) {
        auto *error = new QLabel(item.errorSummary, m_detailContent);
        error->setWordWrap(true);
        QPalette colors = error->palette();
        colors.setColor(QPalette::WindowText, MainWindowUI::folderDangerColor(colors));
        error->setPalette(colors);
        layout->addWidget(error);
    }
    if (!item.steps.isEmpty()) {
        auto *heading = new QLabel(tr("步骤"), m_detailContent);
        layout->addWidget(heading);
        for (const TaskCenter::Step &step : item.steps) {
            auto *label = new QLabel(TaskCenter::stepMark(step.state) + QLatin1Char(' ')
                                         + step.name
                                         + (step.note.isEmpty() ? QString()
                                                                : QStringLiteral(" · ") + step.note),
                                     m_detailContent);
            label->setWordWrap(true);
            layout->addWidget(label);
        }
    }
    auto addAction = [&](const QString &text, TaskCenter::RowAction action,
                         Theme::ButtonKind kind) {
        if (!m_onRowAction) {
            return;
        }
        auto *button = new QPushButton(text, m_detailContent);
        if (action == TaskCenter::RowAction::Cancel) {
            button->setObjectName(MainWindowUI::taskDetailCancelName());
        } else if (action == TaskCenter::RowAction::Retry) {
            button->setObjectName(MainWindowUI::taskDetailRetryName());
        }
        Theme::markButton(button, kind);
        const QString key = item.key;
        connect(button, &QPushButton::clicked, button, [this, key, action]() {
            if (m_onRowAction) {
                m_onRowAction(key, action);
            }
        });
        layout->addWidget(button);
    };
    if (item.capabilities.cancel) {
        addAction(tr("取消任务"), TaskCenter::RowAction::Cancel, Theme::ButtonKind::Neutral);
    }
    if (item.capabilities.retry) {
        addAction(tr("重试"), TaskCenter::RowAction::Retry, Theme::ButtonKind::Accent);
    }
    if (item.capabilities.openTarget) {
        addAction(tr("打开目标位置"), TaskCenter::RowAction::OpenTarget,
                  Theme::ButtonKind::Ghost);
    }
    if (item.capabilities.openLog) {
        addAction(tr("打开日志"), TaskCenter::RowAction::OpenLog, Theme::ButtonKind::Ghost);
    }
    if (item.capabilities.exportReport) {
        addAction(tr("导出错误报告"), TaskCenter::RowAction::ExportReport,
                  Theme::ButtonKind::Ghost);
    }
    if (item.capabilities.openCrashDir) {
        addAction(tr("打开崩溃报告目录"), TaskCenter::RowAction::OpenCrashDir,
                  Theme::ButtonKind::Ghost);
    }
    layout->addStretch(1);
}

void TaskCenterPanel::keyPressEvent(QKeyEvent *event) {
    if (!m_embedded && event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void TaskCenterPanel::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (!m_embedded) {
        return;
    }
    const bool compact = event->size().width() < kWorkspaceMinWidth;
    if (compact == m_compact) {
        return;
    }
    m_compact = compact;
    if (!m_compact) {
        m_compactDetailOpen = false;
    }
    applyWorkspaceMode();
}

bool TaskCenterPanel::handleOutsidePress(QMouseEvent *event) {
    if (m_embedded) {
        return false;
    }
    const QPoint local = event->position().toPoint();
    if (rect().contains(local)) {
        return false;  // 落在面板自己身上：照旧往下走
    }
    const QPoint global = event->globalPosition().toPoint();
    const bool claimed = m_onOutsidePress.claimed && m_onOutsidePress.claimed(global);
    if (claimed) {
        // **必须赶在 close() 之前**：Qt 是关窗那一刻读这个属性（理由全文见
        // MainWindowUI.cpp 里 BottomBarPanel::handleOutsidePress() 那两段 NOTE）。
        setAttribute(Qt::WA_NoMouseReplay, true);
    }
    close();
    // 收回去之后才轮到"要不要接着开另一块"：那一下要开的那一块，是**这一块已经收掉之后**
    // 才该出现的（顺序反了的话，新面板会被底下那段"收旧"再关一次）。
    if (claimed && m_onOutsidePress.dismissed) {
        m_onOutsidePress.dismissed(global);
    }
    event->accept();
    return true;
}

void TaskCenterPanel::mousePressEvent(QMouseEvent *event) {
    if (handleOutsidePress(event)) {
        return;
    }
    QWidget::mousePressEvent(event);
}

void TaskCenterPanel::mouseDoubleClickEvent(QMouseEvent *event) {
    // 点得**快**的时候，"外面的一下"到的是双击事件（Windows 上第二发就是它）。
    if (handleOutsidePress(event)) {
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TaskCenterPanel::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}
