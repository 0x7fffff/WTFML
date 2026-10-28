/**
 * @file InstanceSettingsDialogInternals.hpp
 * @brief `InstanceSettingsDialog` 那几个 .cpp 之间共用的**控件构造小工具**。
 *
 * 【为什么单独拿一个头文件】
 *
 * 「实例设置」这一页按"一页一摊"拆成了四个 .cpp：外壳与接线（`InstanceSettingsDialog.cpp`）、
 * 「概览」（`InstanceSettingsOverviewPage.cpp`）、「启动」（`InstanceSettingsLaunchPage.cpp`）、
 * 文件那几页（`InstanceSettingsFilePages.cpp`）。下面这些小工具原来住在主 .cpp 顶上的
 * **匿名命名空间**里 —— 匿名命名空间是内部链接，另外三个 .cpp 根本看不见它们。
 *
 * 搬过来、而不是拆开各抄一份：`layoutActionButtons()` 上面那几条规矩（"恒定宽度"与"左右
 * 充分利用"是同一条、少一颗的那一行不摊开）属于"两处各抄一份的话，只改一处不会报错"的
 * 那一类东西 —— 症状是别处那几颗按钮宽窄对不上，而从代码里看不出来。
 *
 * 【两条写法上的讲究】
 *
 * * 函数一律 `inline`：头文件里有定义，非 inline 的函数被两个 .cpp 各包含一次就是重定义
 *   （链接期 duplicate symbol，不是编译期）。
 * * `constexpr` 不用额外写 `inline`（本来就自带），这里仍写上，为的是读的时候一眼看出
 *   "这是头文件里的常量"。
 *
 * NOTE: 这里面的东西**不属于** `InstanceSettingsDialog` 的接口，类外面也不许用 —— 它只解决
 * "同一个类的实现分散在几个文件里"这一个问题。真要被第二处代码复用，那是往 `ui/` 的公用
 * 头搬的信号（`MainWindowPageLayout.hpp` 就是那样一个）。
 */

#pragma once

#include "InstanceSettingsDialog.hpp"
#include "Theme.hpp"

#include <QColor>
#include <QGridLayout>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>

namespace InstanceSettingsDialogInternals {

// ---------------------------------------------------------------------------
// 图标预览
// ---------------------------------------------------------------------------

/// 图标预览那一格的边长（唯一出口是 InstanceSettingsDialog::iconPreviewSize()，外面按它
/// 解图递进来）。
///
/// NOTE: 「概览」那一页与后面那几个图标动作（更改图标那个浮层）都要它，所以住在这一层。
inline constexpr int kIconPreviewSize = 64;

// ---------------------------------------------------------------------------
// 浮层
// ---------------------------------------------------------------------------

/// 浮层和它上面那颗按钮之间空多少（px）。和实例页那几块齿轮菜单同一档。
inline constexpr int kMenuGap = 2;
/// 浮层自己的内边距与行距。**和实例页那几块齿轮菜单同两个数**（`kFolderMenuPadding` /
/// `kFolderMenuRowSpacing`，见 MainWindowUI.cpp 那段说明）—— 同一族浮层留白不一样，用户
/// 一眼就看得出是两个东西。
///
/// NOTE: 这两个数在 MainWindowUI.cpp 里是**文件内**的常量（匿名 namespace），这一层拿不到；
/// 抄一份在这儿是最省的做法，但**它们是一份值** —— 那边改了这边要一起改。
inline constexpr int kMenuPadding = 8;
inline constexpr int kMenuRowSpacing = 4;

/// 往表格里加一行：**行名独占一行、内容在下一行**。跨度都是两列 —— 「启动」这一栏
/// **没有**"名字在左、控件在右"那一套。
///
/// @param row 行名那一行的下标。**内容占的是 `row + 1`** —— 调用方要 `row += 2`，不是
/// `row++`（少加的那一行会被下一项盖掉，界面上少一整行而没有任何报错）。
/// @param layoutOut 内容那一格的竖排布局（外面那层格子已经建好了，直接拿去加东西）。
/// @return 内容那一格（控件的父控件）。
inline QWidget *addTitledRow(QGridLayout *grid, int row, const QString &title, QWidget *parent,
                             QVBoxLayout **layoutOut = nullptr) {
    auto *label = new QLabel(title, parent);
    grid->addWidget(label, row, 0, 1, 2, Qt::AlignLeft);

    auto *cell = new QWidget(parent);
    auto *column = new QVBoxLayout(cell);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(4);
    grid->addWidget(cell, row + 1, 0, 1, 2);
    if (layoutOut != nullptr) {
        *layoutOut = column;
    }
    return cell;
}

// ---------------------------------------------------------------------------
// 页面按钮的大小与排布
// ---------------------------------------------------------------------------
//
// 页面按钮的规格：**恒定宽度和高度、每一行三个、间距适中且左右铺满**。下面这一小段
// 就是那三件事的唯一出处。
//
// / 那四个数的**壳**：出处是类里那四个 `constexpr` 函数（验证工程也读它们）。
// / 这里转一手只是为了在下面那几个 short 函数里少写一截限定名。
inline constexpr int kActionButtonsPerRow = InstanceSettingsDialog::actionButtonsPerRow();
inline constexpr int kActionButtonHeight = InstanceSettingsDialog::actionButtonHeight();
inline constexpr int kActionButtonSpacing = InstanceSettingsDialog::actionButtonSpacing();
inline constexpr int kActionButtonMinWidth = InstanceSettingsDialog::actionButtonMinWidth();

/// 一颗按钮按这一套的规格摆好（定高 / 最窄 / 横向可抻）。
///
/// NOTE: **凡是要进下面那张网格的按钮都得先过这一手** —— 少过一手的那颗就会比旁边那两颗
/// 矮一头或者窄一块，而"恒定"正是这一套唯一的卖点。
inline void prepareActionButton(QWidget *button) {
    button->setMinimumWidth(kActionButtonMinWidth);
    button->setFixedHeight(kActionButtonHeight);
    // 横向 Expanding：列有多宽它就有多宽 —— 这才是"恒定宽度"的来源（见下一个函数）。
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

/// 新建一张"每行三颗"的网格（列距 / 行距走 `kActionButtonSpacing`）。
inline QGridLayout *newActionGrid() {
    auto *layout = new QGridLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(kActionButtonSpacing);
    layout->setVerticalSpacing(kActionButtonSpacing);
    return layout;
}

/// 「一格 + 它那层无外边距的竖排」：这一页里十几处按钮 / 勾选框都长这样。
struct ZeroColumn {
    QWidget *cell = nullptr;
    QVBoxLayout *column = nullptr;
};

inline ZeroColumn makeZeroColumn(QWidget *parent, int spacing = 4) {
    auto *cell = new QWidget(parent);
    auto *column = new QVBoxLayout(cell);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(spacing);
    return {cell, column};
}

/// 把若干**格子**摆进"每行三颗"的网格，三列等宽、整幅铺满。格子自己长什么样不管 ——
/// 可以只是一颗按钮，也可以是「一颗按钮 + 它下面那句解释」（概览上那四颗就是后者）。
///
/// 「恒定宽度」和「左右充分利用」这两件事在这里是**同一条**：宽不钉像素，而是"三列各
/// 一份 + 网格铺满它所在那一行的整幅"。于是一组里每颗一样宽，**而且这一页上任何另一组
/// 三颗也一样宽**（每一组都跨着同样那幅宽）。钉死一个像素做不到这一点 —— 那样在别的
/// 字号 / 别的 DPI 下就不再"充分利用"了，字长一点的那颗还会开始打省略号。
///
/// NOTE: 少一颗的那一行（四颗 = 三颗 + 一颗）**不摊开**：那颗照旧三分之一宽。要的是
/// "恒定"，四颗里有一颗比别人宽就不是恒定了。
inline void layoutActionCells(QGridLayout *grid, const QList<QWidget *> &cells) {
    for (int i = 0; i < cells.size(); ++i) {
        grid->addWidget(cells.at(i), i / kActionButtonsPerRow, i % kActionButtonsPerRow);
    }
    for (int column = 0; column < kActionButtonsPerRow; ++column) {
        grid->setColumnStretch(column, 1);
    }
}

/// 一组裸按钮（格子就是按钮本身）摆成"每行三颗"。
inline void layoutActionButtons(QGridLayout *grid, const QList<QWidget *> &buttons) {
    for (QWidget *button : buttons) {
        prepareActionButton(button);
    }
    layoutActionCells(grid, buttons);
}

/**
 * @brief 「实例设置」里那两个浮层（更改图标 / 分组…）共用的外壳。
 *
 * 形状照着实例页那几块齿轮菜单（MainWindowUI.cpp 里的 FolderMenu / GroupMenu）：Qt::Popup
 * + 一圈细边 + 比内容区深一档的底色 + Esc 能关。**没抄过来的只有一样**：那两块的"再点同一
 * 颗齿轮收回"靠的是锚点矩形 + 重放压制，而这里的两颗按钮是**真的控件**，不需要那套几何。
 *
 * NOTE: 【为什么要有这个类，而不是直接 new QWidget(…, Qt::Popup)】只有一件事需要拦：
 * **外面那一下不许重放**。Qt 把一块 popup 关掉时，如果光标不在它的几何里，就会把
 * 同一发 press **重放**给光标底下那个控件 —— 在这一页上那意味着"点一下空白处想收起
 * 浮层，结果顺手按到了底下的东西"，最坏的一发是落在覆盖层左上角那颗**返回箭头**上
 * （浮层收起来的同时，整个对话框跟着被关掉）。
 * `WA_NoMouseReplay` 干的正是这件事，但**钉的时机**很刁：Qt 是"关的那一刻"读它，而
 * 这个属性会被 Qt 在"这个控件又收到鼠标事件"时清掉（那条实测经验写在
 * MainWindowUI.cpp 里 GroupMenu::handleAnchorPress() 上面）—— 所以挂在构造里没用，
 * 得拦在 mousePressEvent 里、落在框外面的那一发上。
 */
class MenuPopup : public QWidget {
public:
    explicit MenuPopup(QWidget *parent) : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint) {
        // 底色比内容区深一档（从调色板推，**不写死颜色**）—— 和那两块菜单逐字同款。
        // 那一档由 `Theme::partBackground(Part::Panel)` 说了算（这一族浮层共用一个色）。
        // NOTE: 这块框**每次现建** ⇒ 不用挂 follow()：主题换了之后下一次打开自然就是新的。
        setAutoFillBackground(true);
        QPalette menuPalette = QGuiApplication::palette();
        menuPalette.setColor(QPalette::Window, Theme::partBackground(Theme::Part::Panel));
        setPalette(menuPalette);
    }

    /// 贴着 @p anchor 那一格的**下边缘**弹出来（超出屏幕时往里收）。
    void popUpBelow(const QWidget *anchor) {
        adjustSize();
        QPoint target = anchor->mapToGlobal(QPoint(0, anchor->height() + kMenuGap));
        if (const QScreen *screen = QGuiApplication::screenAt(target)) {
            const QRect available = screen->availableGeometry();
            target.setX(qBound(available.left(), target.x(),
                               qMax(available.left(), available.right() - width() + 1)));
            target.setY(qBound(available.top(), target.y(),
                               qMax(available.top(), available.bottom() - height() + 1)));
        }
        move(target);
        show();
        raise();
        // 拿焦点只为一件事：Esc 能关（点外面那一下 Qt 自己会关）。
        setFocus(Qt::PopupFocusReason);
    }

protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) {
            close();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override {
        if (!rect().contains(event->position().toPoint())) {
            // 落在框外面的那一发：Qt 接下来就会把这块 popup 关掉，而它读 WA_NoMouseReplay
            // 正是在那一刻（理由见类头那段）。**钉在基类之前**。
            setAttribute(Qt::WA_NoMouseReplay, true);
        }
        QWidget::mousePressEvent(event);
    }

    /// 一圈细边（和齿轮菜单那条同一个理由）：同一档深浅在浅色主题下几乎看不出边界，
    /// 分不出"框到哪为止"。
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
};

/// 浮层里那一列按钮的外观。**和 GroupMenu::entryStyle() 逐字同形**（字重 / 对齐 / 悬停与
/// 按下那两档底色 / `:enabled` 那一条的理由都写在那边）—— 这里只有"不画红字"这一处差别
/// （这两个浮层里没有能删东西的项）。
inline QString menuEntryStyle(const QPalette &menuPalette) {
    QColor hover = menuPalette.color(QPalette::Highlight);
    hover.setAlpha(48);
    QColor pressed = menuPalette.color(QPalette::Highlight);
    pressed.setAlpha(96);
    return QStringLiteral("QPushButton { border: none; background: transparent;"
                          " font-weight: bold;"
                          " text-align: left; padding: 6px 6px; }"
                          "QPushButton:hover:enabled { background-color: %1; }"
                          "QPushButton:pressed:enabled { background-color: %2; }")
        .arg(hover.name(QColor::HexArgb), pressed.name(QColor::HexArgb));
}

}  // namespace InstanceSettingsDialogInternals
