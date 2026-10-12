/**
 * @file TaskCenterPanel.cpp
 * @brief 任务中心那块面板的实现（形状与取舍见头文件）。
 * @author 0x7fffff
 * @date 2026-10-12
 */

#include "TaskCenterPanel.hpp"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
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
#include "Theme.hpp"
#include "VersionIcons.hpp"

namespace {

// ---------- 这一块面板自己的几个尺寸 ----------
//
// NOTE: 这里的留白**另起一套**（不复用 `MainWindowUI.cpp` 里 `BottomBarPanel` 那几个
//       `kPanelPadding` / `kPanelRowSpacing`）：那一套是给"一行一个实例"的短行调的
//       （行高矮、行挨得近），这一块里是标题 + 小字 + 进度条 + 步骤，行高一档。两个面板
//       长得很不一样，共用一个数只会两边都不合适。
constexpr int kPanelPadding = 12;
/// 两行之间（同一个按钮那一栏里）。
constexpr int kRowSpacing = 7;
/// 两栏之间（「当前工作」和「已完成」中间那一道）：比行距大一倍，分开两栏。
constexpr int kSectionSpacing = 14;
/// 一行里面那几样之间。
constexpr int kRowInnerSpacing = 4;
/// 面板宽度（coding plan §3.2：约 440–520 px）。
constexpr int kMinWidth = 440;
constexpr int kMaxWidth = 520;
/// 高度上限 = 窗口可用高度的百分之多少（§3.2：60%）。
constexpr int kHeightPercent = 60;
/// 面板和那颗按钮之间那一道缝（和 `BottomBarPanel` 的 `kPanelGap` 同一个值 ——
/// 两块面板都是从同一颗按钮往上弹的，缝不一样宽看着就是"两块面板没对齐"）。
constexpr int kPanelGap = 6;
/// 标题/小字那一格比面板最多窄这么点（左右边距 + 右边那个百分比/时间的位置）。
constexpr int kTextMaxWidth = kMaxWidth - 2 * kPanelPadding - 56;

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
 * NOTE: 这一行**自己不点**。coding plan §3.2 的"点击任务进入内嵌 `TaskDetailPage`"不在
 *       M1 的范围里（M1 只做骨架：底栏那格 + 这块面板），行上也就没有可点的东西 —— 与其
 *       给一个点了没反应的整行，不如先不给。
 */
class TaskCenterRowWidget : public QWidget {
public:
    TaskCenterRowWidget(const TaskCenter::Item &item, int index, QWidget *parent)
        : QWidget(parent) {
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
        } else if (!item.active) {
            // 已完成那一行左边那个记号：办成了 ✓、没办成 ✗。
            auto *mark = new QLabel(TaskCenter::stepMark(item.failed ? TaskCenter::StepState::Failed
                                                                     : TaskCenter::StepState::Done),
                                    this);
            if (item.failed) {
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
        if (item.active && item.percent >= 0) {
            auto *percent = new QLabel(MainWindowUI::taskCenterPercentText(item.percent), this);
            percent->setObjectName(MainWindowUI::taskCenterPanelRowPercentName(index));
            applyMutedFont(percent, 0);
            titleRow->addWidget(percent, 0, Qt::AlignVCenter);
        } else if (!item.active && item.finishedAt.isValid()) {
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
        if (item.active && item.percent >= 0) {
            auto *bar = new QProgressBar(this);
            bar->setObjectName(MainWindowUI::taskCenterPanelRowBarName(index));
            bar->setRange(0, 100);
            bar->setValue(item.percent);
            bar->setTextVisible(false);  // 数写上面那一行去了，条里再写一遍是重复
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
                                 const OutsidePressHandler &onOutsidePress, QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint),
      m_anchor(anchor),
      m_provider(provider),
      m_onClear(onClear),
      m_onOutsidePress(onOutsidePress) {
    setObjectName(MainWindowUI::taskCenterPanelName());
    // 底色和底栏那两块面板**同一档**（`Theme::Part::Panel`）：三块都是从底栏弹出来的，
    // 颜色不一样就成了"三块不同的东西"。理由全文见 MainWindowUI.cpp 里 BottomBarPanel 那段。
    setAutoFillBackground(true);
    QPalette panelPalette = QApplication::palette();
    panelPalette.setColor(QPalette::Window, Theme::partBackground(Theme::Part::Panel));
    setPalette(panelPalette);

    // 内容整块装在一个滚动区里：任务一多（几十行历史）面板不能一直往上长 —— 高度上限
    // 是"屏幕可用高度的 60%"，超出去的部分由它自己滚。
    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    // 横向**永远不滚**：宽度是钉死的两档（440 / 520），行里的字自己省略号收尾。
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFocusPolicy(Qt::NoFocus);
    m_content = new QWidget(m_scroll);
    m_content->setAutoFillBackground(false);
    auto *contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(kRowSpacing);
    contentLayout->addStretch(1);
    m_scroll->setWidget(m_content);

    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(kPanelPadding, kPanelPadding, kPanelPadding, kPanelPadding);
    box->setSpacing(0);
    box->addWidget(m_scroll);

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

void TaskCenterPanel::refresh() {
    if (!m_opened) {
        return;  // 还没弹出来过：下一次 popUp() 自己会重建
    }
    rebuild();
    place(/*animate=*/false);
}

void TaskCenterPanel::place(bool animate) {
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
    }

    adjustSize();
    // 宽度钉在两档里（§3.2：约 440–520）：比 440 窄就撑开、比 520 宽就收住。
    // 高度交给内容（滚动区自己封了顶）。
    if (width() < kMinWidth) {
        resize(kMinWidth, height());
    } else if (width() > kMaxWidth) {
        resize(kMaxWidth, height());
    }

    const QPoint anchorTopLeft = m_anchor->mapToGlobal(QPoint(0, 0));
    // 水平方向对齐那颗按钮的**中线**：这块面板比按钮宽得多（440 vs 356），左对齐会伸到
    // 窗口右边外面去。居中之后两边出头一样多。
    int x = anchorTopLeft.x() + m_anchor->width() / 2 - width() / 2;
    int y = anchorTopLeft.y() - height() - kPanelGap;
    if (available.isValid()) {
        // 贴边兜底：窗口贴着屏幕上边时（或任务多、面板高）往上放不下，就压进屏幕里
        // —— 顶出去的那一半用户是看不到的，也没法拖（它是 Qt::Popup）。
        x = qBound(available.left() + 4, x, available.right() - width() - 4);
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
    QList<TaskCenter::Item> active;
    QList<TaskCenter::Item> finished;
    for (const TaskCenter::Item &item : items) {
        (item.active ? active : finished).append(item);
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
            layout->addWidget(new TaskCenterRowWidget(item, index, m_content));
            ++index;
        }
    }
    if (!finished.isEmpty()) {
        if (!active.isEmpty()) {
            layout->addSpacing(kSectionSpacing - kRowSpacing);
        }
        addSectionTitle(MainWindowUI::taskCenterPanelDoneSectionText());
        for (const TaskCenter::Item &item : finished) {
            layout->addWidget(new TaskCenterRowWidget(item, index, m_content));
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
}

void TaskCenterPanel::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool TaskCenterPanel::handleOutsidePress(QMouseEvent *event) {
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
