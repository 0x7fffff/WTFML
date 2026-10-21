#pragma once
#include "GroupBoxFixture.hpp"
#include <functional>
/**
 * @file main.cpp
 * @brief UI 回归测试入口：使用 offscreen 验证控件交互、布局和主题。
 *
 * 主验证工程（tests/）只链 Qt6::Core/Network、不编译任何 UI 代码，所以
 * "两个下拉框"这次改动的界面部分在这里验。用 offscreen 平台插件跑，不需要真屏幕。
 *
 * 那个环境变量**不许省**。省了跑的就是**真的 windows 平台插件**（会开真窗口），
 * 那几档"只在真窗口下才量得出错值"的断言会跟着变红（`ColorScheme`、三个框的高度…）。
 * 实测：不带它 = 2276 PASS / 7 FAIL；带上它 = 全绿。
 *
 * 这个工程有一份**偶发**段错误（ 又查了一轮）：**是布局 / 时序敏感的潜伏问题，
 * 不是产品逻辑，也不是本次那三条改动**。当天在同一台机器上量到的全部事实：
 * * 同一份 exe 连着 42 趟**全部**崩在 986 PASS；也连着 8 趟 2281/2281 全绿。崩起来是
 * 连成一片的（不是五五开）—— 换一次链接、或者干脆什么都不做过一会儿再跑，都可能翻面；
 * * 崩点上那一串栈**只有 Qt 自己**：`QAccessibleEvent::accessibleInterface()` ←
 * `QAccessible::updateAccessibility` ← `QWidgetPrivate::hideChildren` ←
 * `QDialog::~QDialog` ← `InstanceSettingsDialog::~InstanceSettingsDialog()` ← `main()`，
 * **一行我们的代码都没有**（崩的是 `main()` 里某个栈上的对话框）；
 * * 把产品改动全 stash 回 pristine（只留一个 `constexpr` 桩子）照样 8/8 崩在同一个计数；
 * * 塞进 gdb（慢很多）跑得完：2280/2281，唯一那条 FAIL 是一条**性能**断言（Z8「两万行点一下
 * ≤250 ms」，gdb 下量出来 752 ms）—— 它只是踩不到那个时序；
 * * 调试堆报过 `Free Heap block … modified … after it was freed`：gdb 断在 `RtlFreeHeap`
 * 上等到那一块再下硬件观察点，写它的栈一路到 `InstanceSettingsDialog` 的析构，中间仍然
 * 只有 Qt6Widgets / Qt6Gui。
 * ⇒ 崩了**先重跑**；还崩就把 gdb 挂上去看它是不是又在这条路上（是的话直接再跑），
 * **别去改产品**。
 *
 * `main()` 里 `QAccessible::setActive(false)` 那一行留着，但它**不是"解药"**（没有证据能
 * 证明它治得了上面那条）：理由只是"这个环境里的 A11y 和产品真实情形不一样"—— offscreen
 * 插件下 A11y 恒开，而产品跑 windows 插件时只有读屏软件连着才开。这个工程量的是布局 /
 * 几何 / 颜色 / 话术，**没有一条和无障碍有关**（全仓 `QAccessible` 只有那一处）。
 *
 * `WTFML_SKIP_JUNCTION=1` 那个逃生口**也治不了它**（崩点在逃生口**之前**那一发
 * `processEvents()`）—— 别再拿它当解药。
 */

#include <QApplication>
#include "ArmorTrimChecks.hpp"
#include <QAccessible>
#include <QStyleFactory>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>  // U10L：地址那一格右键（复制完整地址）那两发
#include <QDateTime>  // I4：面板那句"已运行…"的四档话术（纯函数，按毫秒差算）
#include <QDir>  // V：行下面那行路径给的是系统分隔符（QDir::toNativeSeparators）
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QFontMetrics>  // U10k：名字那一格装不下时"画出来是什么"要现算一次
#include <algorithm>
#include <cmath>  // W12：那两档"深一点浅一点"是拿 WCAG 对比度量的（pow 那条 sRGB 曲线）
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QAbstractItemView>  // U10Q：多选那一档（selectionMode() == MultiSelection）
#include <QLabel>
#include <QLineEdit>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QImage>
#include <QKeyEvent>
#include <QEnterEvent>
#include <QShortcut>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QClipboard>  // U40b：旗帜那条「复制指令」按下去真的进了剪贴板
#include <QPointer>
#include <QProcess>      // U10O：造一个**真** junction（mklink 是个 cmd 内建命令，得起进程）
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>   // I4：底栏弹层避让那一条要问"这一块屏的可用区域"（offscreen 是 800x600）
#include <QSet>
#include <QSettings>     // W7/W8：主题落盘那一趟（读写都在一个各自独立的窝里，见那一段）
#include <QSpinBox>
#include <QTableWidget>  // U40c：附魔那张表的「和上一行同一件」那一列
#include <QStackedWidget>
#include <QStandardItemModel>  // Z13：rowActionTip() 只要一个真 model 的 index（不挂视图）
#include <QStyleHints>   // W2：深浅那一档是平台给的（`colorScheme()`），offscreen 上恒 Unknown
#include <QStyleOptionViewItem>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTemporaryDir>
#include <QToolButton>
#include <QHeaderView>   // U10L：那三栏的伸缩策略（第 0 栏吃剩下、1/2 收着自己那点）
#include <QHelpEvent>
#include <QHoverEvent>   // Z11：条上那两颗按钮的悬停态（悬停那一位默认没人填，得自己发）
#include <QIcon>         // U10L：服务器图标那一格（真塞一枚进去，再看它落没落在那行上）
#include <QFocusEvent>   // U11c：合成一发 FocusIn（`setFocus()` 在 offscreen 底下送不到控件上）
#include <QEvent>        // W6：合成一发「调色板变了」，量 `Theme::follow()` 那一条登记
#include <QJsonDocument>  // U12：「哪几页收起来了」那一格来回比的时候好判
#include <QJsonObject>
#include <QTimer>
#include <QEventLoop>  // ⑬：动效要**真的把事件循环跑起来**才走完那一段（sleep 不算）
#include <QGraphicsEffect>  // ⑭：滚动条露头那一下挂的那层淡入（要读它的 objectName 认人）
#include <QVariantAnimation>  // ⑬：换页那一发的**时间轴**（干等 + 淡入，见那一节）
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QVector3D>
#include <QWheelEvent>

#include <cstdio>

#include "AccountSkinDialog.hpp"
// U10a / U11 那一段：「配置文件」那一页里嵌着的那个编辑器。
// ConfigEntries 是左边那一格「条目」的取数（一条 `键 = 值` 是怎么认出来的）——
// 它是纯函数，这一层正是它该被量的地方。
#include "ConfigEntries.hpp"
#include "ConfigFileEditorPage.hpp"
#include "ConfigFileText.hpp"
#include "GameLogWindow.hpp"
#include "GroupDialog.hpp"
#include "GroupListDelegate.hpp"  // N 段：分组设置那张组清单行尾那两颗上下移三角
#include "InlineMessage.hpp"      // O 段：内嵌提示面板（QMessageBox 的替身）
#include "InstanceGroupDelegate.hpp"
#include "InstanceFilesPage.hpp"
#include "InstanceSettingsDialog.hpp"
#include "ResourcePackPage.hpp"
#include "ToolboxPage.hpp"  // U39：信标计算 / 酿造助手那两片
#include "manage/ResourcePackOptions.hpp"
#include "IntegrityReportPage.hpp"
#include "UpdateCenterPage.hpp"  // U33：更新中心（「检查更新」查出来的结论摊成能动手的一页）
#include "JavaMismatchDialog.hpp"
#include "NewServerPage.hpp"  // U34：「新建服务端」那一页（coding plan §6.2 / M4）
#include "ServerConsolePage.hpp"  // U36：服务端控制台那一页（coding plan §6.2 / M4-3）
// U37：「服务端设置」那一整页（coding plan §6.3 第二阶段 / M4-4）—— 常用设置表单 /
// 原始文本编辑 / 世界备份 / 体检。这一页一个字节都不认 `server.properties` 的语法
// （那些全在 `src/server/ServerPropertiesForm*` 那一层，由 `MainWindow` 翻好喂进来）。
#include "ServerSettingsPage.hpp"
// U36：控制台那一页摆的那个状态字是 `ServerModel::statusName()`；那几条 EULA 的话
// （标题 / 正文 / 勾选框 / 链接）的唯一出处是 `ServerRun.hpp` —— 不在这节里另写一份。
#include "server/ServerModel.hpp"
#include "server/ServerRun.hpp"
#include "MainWindowUI.hpp"
// Z11 那两条原生消息的契约断言：拼 MSG 这件事单独一个 TU（chrome_messages.cpp），
// 免得这个一万多行的文件去 include <windows.h>（理由见那个文件的文件头）。
#include "chrome_messages.hpp"
#include "OverlayHost.hpp"  // O 段：内容区覆盖层（设置界面 / 提示都开在这儿）
#include "ResourceDetailPage.hpp"  // Z14：下载页点开一个资源之后那一整页
#include "TaskDetailPage.hpp"      // I5：任务详情页（点任务中心里的一行开出来的那一页）
// 自绘的进度条（，coding plan §3.3）：三处（底栏 / 面板行 / 详情页）共用的那一条。
// 这一层要量的是"那三处**真的**换成了它"：按 objectName 找得到 `QProgressBar`、但
// `qobject_cast<TaskProgressBar*>` 是空指针，就是漏换了一处（画出来还是原生那个样子）。
#include "TaskProgressBar.hpp"
#include "RowActivation.hpp"
// Z15：「启用 / 禁用」那枚开关（画出来的，所以下面要量像素）和 MOTD 那套配色
// （照游戏的样子画 MOTD 那一格）。
#include "MotdText.hpp"
#include "ToggleSwitch.hpp"
#include "SkinModel.hpp"
#include "SkinViewer.hpp"
// W：主题那一层（背景 / 文字不透明度 / 每一块骨架能不能单独换）。这一层是从各处那几句
// `darker()` 里收出来的，所以量的是"收得对不对、能不能整套换"，不是"某个色好不好看"。
#include "Animations.hpp"  // 动效总开关：这个工程把它关掉（2026-09-29）
#include "Theme.hpp"
// W11：主题里那两张背景图（主页左栏 / 页面背景）。这一层量的是"没设图时一个像素都不画"
// 和"设了图真的画上去了"—— 两件事都只能用一像素一像素比（见那一段开头的说明）。
#include "ThemeBackground.hpp"
#include "TitleBar.hpp"       // Z11：顶上那条自己画的标题栏
#include "VersionIcons.hpp"
#include "WindowChrome.hpp"   // Z11：`resizeBorder()` —— buttonInset() 要对着它比
#include "WorldSettingsPage.hpp"  // Z16：点存档那支笔之后那一页（世界规则表）
// Z16：规则行上盖的那层官方中文名（表和"认不出来原样回自己"那一条都在这儿量）。
#include "GameRuleNames.hpp"
#include "VersionTreeDelegate.hpp"
#include "download/ResourceCatalog.hpp"
// 更新检测那三档话术的唯一出处。概览页那颗「检查更新」的 tooltip 里写死的
// 「说不清」必须和它**是同一个字** —— 两边都在同一个 TU 里，这条等号才钉得住。
#include "download/ModDependencies.hpp"
// 「游戏窗口大小」那几个上下限在两侧各写了一遍（ui/ 不许 include src/，见
// MainWindowUI.hpp 里的说明），所以"两边必须一样"这件事只有把两个头放进同一个 TU 才
// 钉得住。这里只调它的**内联静态函数**，不构造 LauncherSettings，所以不需要编那个 .cpp。
#include "settings/LauncherSettings.hpp"



inline int g_pass = 0;
inline int g_fail = 0;

/// child 是不是 ancestor 的后代（含自身）。
inline bool isDescendantOf(QWidget *child, QWidget *ancestor) {
    for (QWidget *w = child; w != nullptr; w = w->parentWidget()) {
        if (w == ancestor) {
            return true;
        }
    }
    return false;
}

/// 「某一项那一整格」是不是被藏起来了。判据落在**这一项自己那一格**上（addOverrideRow
/// 建的 cell：选项行 choiceRow 的父控件），**不是**往上一直走到对话框 ——
/// NOTE: 往上走到对话框那条判据是假的：这一页在 QStackedWidget 里，不是当前那一页时
/// 整页都是 hidden，于是"隔壁那一项也藏着"。而 `hide()` 又不往下传染（父控件藏了，
/// 子控件自己的 isHidden() 仍是假），所以只能问**那一格本身**。
inline bool rowCellHidden(QWidget *dialog, const QString &field) {
    auto *choiceRow = dialog != nullptr
                          ? dialog->findChild<QWidget *>(InstanceSettingsDialog::choiceRowName(field))
                          : nullptr;
    return choiceRow != nullptr && choiceRow->parentWidget() != nullptr &&
           choiceRow->parentWidget()->isHidden();
}

inline void check(bool ok, const QString &what, const QString &detail = QString()) {
    if (ok) {
        ++g_pass;
        std::printf("  PASS  %s\n", qPrintable(what));
    } else {
        ++g_fail;
        std::printf("  FAIL  %s   %s\n", qPrintable(what), qPrintable(detail));
    }
    std::fflush(stdout);
}

/// 上一个被关掉的提示说了什么（标题 / 正文）。产品规矩是"出错必须弹窗"，
/// 所以"弹没弹、弹的是什么"本身就是要验的东西，不只是为了把测试跑通。
inline QString g_modalTitle;
inline QString g_modalText;

/// 让下一条**内嵌提示**自己点掉，并把它说了什么记下来。
///
/// NOTE: 两条 timer 的必要性和以前一样：0 毫秒那条在嵌套事件循环一进去就触发；
/// 300 毫秒那条是兜底（提示是先摆好再进循环的，个别平台上第一条可能跑在
/// 它真正露出来之前）。两条都响时后一条找不到东西，直接返回。
///
/// NOTE: 关它走的是**点第一颗按钮**，不是 `close()`。`QDialog::closeEvent` 只在
/// `isVisible()` 为真时才 `reject()` —— 而在 offscreen 里这一层有可能整个是
/// 藏着的，那样 close() 会变成"什么都没发生"，测试就挂死在嵌套循环里。
/// 点按钮走的是 `clicked → accept() → finished`，看不看得见都作数。
inline void dismissNextInline(OverlayHost *host, int buttonIndex = 0) {
    g_modalTitle.clear();
    g_modalText.clear();
    const auto dismiss = [host, buttonIndex] {
        if (host == nullptr) {
            return;
        }
        auto *box = host->findChild<InlineMessageBox *>();
        if (box == nullptr) {
            return;
        }
        if (auto *title = box->findChild<QLabel *>(InlineMessageBox::titleLabelName())) {
            g_modalTitle = title->text();
        }
        if (auto *text = box->findChild<QLabel *>(InlineMessageBox::textLabelName())) {
            g_modalText = text->text();
        }
        if (auto *button = box->findChild<QPushButton *>(InlineMessageBox::buttonName(buttonIndex))) {
            button->click();
        } else {
            box->close();
        }
    };
    QTimer::singleShot(0, dismiss);
    QTimer::singleShot(300, dismiss);
}

/// 把控件"现在长什么样"抓成一张图 —— 抓出来的是**逻辑像素**，跟控件的坐标同一个尺度。
inline QImage captureWidget(QWidget *widget) {
    QImage image(widget->size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget->render(&painter);
    return image;
}

/// 一块图里**出现次数最多的那个颜色**（按整块数票）。
///
/// 为什么这么量（，日志窗那几条底色断言）：一行里既有底色又有字，而字是另一种
/// 颜色 —— 直接取某一个像素太看运气（那一个点可能正好落在字形上、也可能落在行间空白上）。
/// 底色是**铺满那一行**的，所以按整块数票，多数那一票一定是它。
/// 顺带把"行高里到底铺了多少"这种样式细节也一起甩掉了：铺满也好、只铺字形那一圈也好，
/// 底色都是多数。
inline QColor dominantColor(const QImage &image, const QRect &area) {
    const QRect clip = area.intersected(image.rect());
    if (clip.isEmpty()) {
        return QColor();
    }
    QHash<QRgb, int> votes;
    QRgb best = 0;
    int bestCount = 0;
    for (int y = clip.top(); y <= clip.bottom(); ++y) {
        for (int x = clip.left(); x <= clip.right(); ++x) {
            const QRgb rgb = image.pixel(x, y);
            const int count = ++votes[rgb];
            if (count > bestCount) {
                bestCount = count;
                best = rgb;
            }
        }
    }
    return QColor(best);
}

/// 两个颜色差多远（三通道绝对值之和）。判"这两个色看得出来不一样"用。
inline int colorDistance(const QColor &a, const QColor &b) {
    return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue());
}

/// 这个控件装上了滚轮防护吗 —— 判据是**焦点策略**，不是"滚轮事件被吃掉了吗"。
///
/// 为什么要单独验这一条：防护本身的判据是 `!hasFocus()`，而 Qt 对**真滚轮
/// 事件**（spontaneous）是"先按焦点策略给焦点、再派发"（qapplication.cpp:2887 →
/// giveFocusAccordingToFocusPolicy() → :3694/:3732），偏偏 QComboBox 和 QAbstractSpinBox
/// 的默认焦点策略就是 Qt::WheelFocus（qcombobox.cpp:1066 / qabstractspinbox.cpp:1625）——
/// 于是过滤器跑起来时 hasFocus() 已经是 true，防护一次都不生效。
///
/// E2 那两条用的是 QApplication::sendEvent()（合成事件），走的是 qapplication.cpp 里
/// **不给焦点**的那条岔路，所以照不出这个病（探针里 hasFocus() 始终是 false，防护"看起来
/// 生效"）。真滚轮没法在探针里合成，于是这里改验**前提**：焦点策略里不许含 WheelFocus
/// 那一位 —— shouldSetFocus() 的判据就是 `(focusPolicy & policy) == policy`，
/// 不含 WheelFocus 就抢不走焦点。
///
/// 顺带守住"别改坏正常操作"：改完策略 Tab 还得走得到、点还得能聚焦。
/// @return 有问题返回一句人话；没问题返回空串。
inline QString wheelGuardProblem(QWidget *widget) {
    if (widget == nullptr) {
        return QStringLiteral("控件没找到");
    }
    const int policy = static_cast<int>(widget->focusPolicy());
    const int wheel = static_cast<int>(Qt::WheelFocus);
    if ((policy & wheel) == wheel) {
        return QStringLiteral("焦点策略含 WheelFocus 那一位（真滚轮照样抢焦点，防护是哑的）");
    }
    if ((policy & static_cast<int>(Qt::TabFocus)) != static_cast<int>(Qt::TabFocus)) {
        return QStringLiteral("Tab 走不到它了（改策略时误伤）");
    }
    if ((policy & static_cast<int>(Qt::ClickFocus)) != static_cast<int>(Qt::ClickFocus)) {
        return QStringLiteral("点它不再能聚焦（改策略时误伤）");
    }
    return QString();
}

  // namespace



int UiInstallLayoutCheck(QApplication &app);
int UiToolboxPolishCheck(QApplication &app);
int UiResourcePacksCheck(QApplication &app);
int UiLifecycleCheck(QApplication &app);
int UiUiRefreshCheck(QApplication &app);
int UiThemeCheck(QApplication &app);
int UiNavigationChecks(QMainWindow &window, MainWindowUI &ui, QComboBox * &installed, QPushButton * &startBtn, QStackedWidget * &gameStack, QTreeWidget * &versionTree, QLineEdit * &installNameEdit, QPushButton * &installBtn, QPushButton * &installBackBtn, QTreeWidget * &loaderTree, QListWidget * &folderList, QTreeWidget * &instanceList, QLabel * &instanceFolderLabel, QTabWidget * &tabs);
int UiVersionTreeGeometryChecks(QMainWindow &window, QTreeWidget * &versionTree);
int UiInstallFormLayoutChecks(MainWindowUI &ui, QLineEdit * &installNameEdit, QLabel * &installNameHint, QLabel * &installNameError, QPushButton * &installBtn, QTreeWidget * &loaderTree, QWidget * &downloadPage, const QRect &rEdit, const QRect &rInstall, QFrame * &loaderCard, QFrame * &installFooter, const QByteArray &surfaceKey, const std::function<QRect(QWidget *)> &rectInPage);
int UiDownloadCategoriesChecks(MainWindowUI &ui, QStackedWidget * &gameStack, QWidget * &downloadPage, const std::function<QRect(QWidget *)> &rectInPage);
int UiDownloadTypographyChecks(QMainWindow &window, MainWindowUI &ui);
int UiTaskSummaryChecks(MainWindowUI &ui);
int UiBottomBarGeometryChecks(MainWindowUI &ui, QFrame * &bar);
int UiTaskCenterChecks(QMainWindow &window, MainWindowUI &ui, QTabWidget * &tabs);
int UiTaskActionsChecks(QMainWindow &window, MainWindowUI &ui);
int UiBottomBarPopoversChecks(QMainWindow &window, MainWindowUI &ui, QTabWidget * &tabs);
int UiRunStateColorsChecks();
int UiColumnProportionsChecks(QComboBox * &installed, QPushButton * &startBtn, QListWidget * &folderList, QTreeWidget * &instanceList, QTabWidget * &tabs, const std::function<QString(QWidget *)> &surfaceValueOf, const std::function<bool(QWidget *, const QString &)> &inSurface, const std::function<QFrame *(QWidget *, const QString &)> &surfaceOf);
int UiSettingsLayoutChecks(MainWindowUI &ui, QTabWidget * &tabs);
int UiInstanceGroupingChecks(MainWindowUI &ui, QTreeWidget * &instanceList);
int UiGroupEditorChecks();
int UiInstanceOverridesChecks();
int UiContentPagesChecks(QFrame * &bar);
int UiConfigEditorChecks();
int UiSkinMeshChecks();
int UiAccountSkinChecks();
int UiWindowPreferencesChecks(QTabWidget * &tabs);
int UiGameLogActionsChecks(QFrame * &bar);
int UiGameLogPlacementChecks();
int UiLogClassificationChecks();
int UiLogFilteringChecks();
int UiLogColorsChecks();
int UiLogTogglesChecks();
int UiLogSeparatorsChecks();
int UiLogRetentionChecks();
int UiFolderDisplayChecks(MainWindowUI &ui, QListWidget * &folderList, QTabWidget * &tabs);
int UiOverlayGeometryChecks(QMainWindow &window, MainWindowUI &ui, QTabWidget * &tabs, QFrame * &bar);
int UiOverlayNavigationChecks();
int UiLaunchPromptsChecks(MainWindowUI &ui);
int UiTitleBarChecks(QMainWindow &window, MainWindowUI &ui);
int UiInstanceRowActionsChecks();
int UiInstanceRowPaintingChecks();
int UiContentRowsChecks();
int UiWorldEditorChecks();
int UiThemePaletteChecks(QApplication &app, QTabWidget * &tabs, QFrame * &bar);
int UiAnimationsChecks(QApplication &app, QFrame * &bar);
int UiScrollBarsChecks(QApplication &app, QFrame * &bar);
int UiIntegrityReportChecks();
int UiCrashReportChecks();
int UiBackupActionsChecks();
int UiInstanceSnapshotsChecks();
int UiInstanceFiltersChecks(QMainWindow &window, MainWindowUI &ui, QTabWidget * &tabs);
int UiUpdateCenterChecks();
int UiServerModeChecks(QMainWindow &window, MainWindowUI &ui, QTabWidget * &tabs);
int UiServerSettingsChecks();
int UiAccessibilityChecks(QMainWindow &window);
int UiBeaconBrewingChecks();
int UiBannerArmorChecks();

