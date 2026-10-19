/**
 * @file MainWindowUI.hpp
 * @brief 声明 UI 构建器类，负责创建主窗口的所有控件和布局。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 实现界面与业务逻辑的分离，提高代码可维护性。
 *
 * 窗口结构自 2026-09 起是：顶部 QTabWidget（主页 / 实例 / 下载 / 设置）+
 * 底部一条通栏状态条（左：实例运行状态；中：启动/下载进度；右：头像与用户名）。
 * 原先的「日志输出」面板已经去掉 —— 日志走 Qt 的日志系统，在命令行里看。
 *
 * 四个选项卡**都是左右两栏**（仿 PCL）：左栏是要操作的那点东西（选版本、一列分类、
 * 按钮），右栏是列表或一整摊设置项。四页**一律**由 buildTwoColumnPage() 搭出**真控件**，
 * 比例取自 leftColumnStretch(tab)/rightColumnStretch(tab)。界面和验证工程都引这几对
 * 份数，不各写一份。
 *
 * NOTE: 2026-09-26 起「设置」页也走这条路（它原来是**一张通到底的两列表格**，栏不是控件）：
 *       左栏是五条分类（启动/游戏/提示/个性化/下载），右栏是堆叠页 —— 每一栏里是一张
 *       grid，但那张表现在只管**栏内**那一层（见「设置页」那一段）。
 *
 * NOTE: 2026-09-26 同日稍后，设置页每一栏里那些行也改成了**上下结构**（行名独占一行、
 *       内容在下一行，见 cpp 里的 addSettingRow()）—— 从前是"名字在左、控件在右"的
 *       两列行。于是栏内那张 grid 的 3:7 现在**一行都没在用它分列**（每一格都跨两列），
 *       留着它是为了让五个分栏的表长得一样、也为了让验证工程量得着（见
 *       leftColumnStretch()/rightColumnStretch()）。
 *
 * NOTE: 比例**按页不同** —— 实例页是默认的"左 3 右 7"，主页是"左 7 右 3"
 *       （见 leftColumnIsWider()），下载页与设置页都是"左 2 右 8"（左栏都只是一列分类）。
 */

#pragma once

#include <QCoreApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMainWindow>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVector>
#include <QWidget>

#include <functional>

// 「一个组在界面上写成什么」这条规则的唯一出处（分组对话框和实例列表那张树都用它）。
// 让 MainWindowUI 反过来去 include 同一个 ui/ 下的对话框头，比在两边各写一份
// "%1 (%2)" 强：那种写法不会报错，只会在哪天有人改了其中一处之后让同一个组在两处
// 显示得不一样。
#include "GroupDialog.hpp"
// 「最大内存」那一行左半边那根条（见 MemorySlider.hpp 开头那段）。它和上面的对话框头
// 一样属于 ui/ 自己的东西，不是业务层的。
#include "MemorySlider.hpp"
// 任务中心那块面板（2026-10-12）。这里要它的头只为一件事：`setTaskCenterContent()` 的
// 参数类型和那个成员指针 —— 面板自己长什么样、怎么弹，全在它的 .cpp 里。
#include "TaskCenterPanel.hpp"
// 自绘的进度条（2026-10-13，coding plan §1.2 / §3.3）。这里要它的**完整定义**而不只是
// 前向声明：`getProgressBar()` 的内联体要把 `TaskProgressBar *` 上转成 `QProgressBar *`，
// 类型不完整就转不过去（实测报 "cannot convert … in return"）。
#include "TaskProgressBar.hpp"

class QGridLayout;
class QHBoxLayout;
class QScrollArea;
class QSpacerItem;
class QVBoxLayout;
class OverlayHost;
class TitleBar;
class WindowChrome;

/**
 * @brief 挡掉没有焦点时的滚轮事件，防止鼠标划过就把控件里的值改掉。
 *
 * 两种控件都有这个行为，滚轮事件都按**鼠标位置**派发、和焦点无关：
 *   - QComboBox 把滚轮当成"切换选中项" —— 鼠标从下拉框上滚过（哪怕焦点在别处）
 *     就静默换一个版本。「下载」页那个下拉框旁边就是「下载版本」，手一滑可能把一次
 *     GB 级的下载指到别的版本上。设置页那三个下拉框同理（Java 运行时 / 启动后 / 下载源）。
 *   - QSpinBox（QAbstractSpinBox）把滚轮当成"加一步/减一步" —— 内存那两个数字框就是。
 *     翻设置页时从"最大内存"上滚过去，值就变了，而**界面上看不出来它变了**（没有焦点
 *     就没有高亮），下一次启动游戏用的就是那个被滚出来的数。
 *   - QSlider（QAbstractSlider）同理：滚一格就是一档 —— 「最大内存」那根条装上它，
 *     鼠标从条上滚过去就把内存挪了（而且和别的控件一样，没焦点时看不出来）。
 *
 * 判据只有一个：**没有焦点就不认滚轮**。有焦点时照常放行（用户是特意点进来的）。
 *
 * 【这条判据要成立，必须连焦点策略一起改 —— 只装过滤器是哑的】
 *
 * 2026-09-24 修的。以前这里只有 installEventFilter()，判据读到的 hasFocus() 却**永远
 * 已经是 true**：Qt 对**真滚轮事件**（spontaneous）是"先按焦点策略给焦点、再派发"，
 *   - qapplication.cpp 的 case QEvent::Wheel 里先调
 *     QApplicationPrivate::giveFocusAccordingToFocusPolicy(w, e, relpos)（:2887），
 *     真正的 d->notify_helper(w, &we)（事件过滤器就是在这一句里跑的）在 :2895 之后；
 *   - giveFocusAccordingToFocusPolicy() 对 wheel 这一档用的策略是 Qt::WheelFocus（:3694），
 *     shouldSetFocus() 的判据是 (w->focusPolicy() & policy) == policy（:3732）；
 *   - 偏巧 QComboBox（非 macOS 那条分支）和 QAbstractSpinBox 的**默认焦点策略就是
 *     WheelFocus**（qcombobox.cpp:1066 / qabstractspinbox.cpp:1625）。
 * 于是判据通过 → 事件过滤器跑的那一刻 hasFocus() 已经是 true → 一律放行 → 值照样被改。
 * 改成 StrongFocus（= TabFocus|ClickFocus|0x8，**不含** WheelFocus 多出来的那个 0x4 位）
 * 之后 shouldSetFocus() 不通过、滚轮不再抢焦点，!hasFocus() 才恢复它字面上的意思：
 * 鼠标划过（焦点在别处）吃掉、用户点/切进来的那一个照常认滚轮。
 *
 * NOTE: 这两件事**必须一起做**，所以入口只有一个 WheelGuard::install()。直接
 *       new WheelGuard(...) + installEventFilter(...) 会得到一个看着装上了、其实
 *       一次都不生效的防护 —— 这正是它原来那个样子。
 *
 * NOTE: 验证工程里那条用例用的是 QApplication::sendEvent()（合成事件），而
 *       qapplication.cpp 对 !wheel->spontaneous() 走的是"不给焦点、直接 notify_helper"
 *       那条岔路 —— 探针里 hasFocus() 仍是 false、防护"看起来生效"。真机上会不会生效
 *       因此只能靠**焦点策略**那条断言守着（ui_check 里"滚轮防护"那一段）。
 *
 * NOTE: QObject::eventFilter 是虚函数、不是槽，所以这里不需要 Q_OBJECT，
 *       AUTOMOC 不会牵扯进来，也不用在 cpp 末尾补 .moc 包含。
 *
 * NOTE: 放在这个头里是为了让 ui/InstanceSettingsDialog 也能用同一份
 *       （那里两个"只对这个实例生效"的内存数字框一模一样）—— 各写一份的话，
 *       哪天补一种控件只会补到其中一处。
 */
class WheelGuard : public QObject {
public:
    using QObject::QObject;

    /// 装防护的**唯一入口**：改焦点策略 + 装过滤器，两件事一起做（理由见上面那段）。
    /// @param widget 要护住的控件（下拉框 / 数字框 / 条）。空指针什么都不做，调用方不必先判空。
    static void install(QWidget *widget) {
        if (widget == nullptr) {
            return;
        }
        // StrongFocus 里**没有** WheelFocus 那一位，滚轮因此不再抢焦点 —— 这是判据成立的前提。
        widget->setFocusPolicy(Qt::StrongFocus);
        widget->installEventFilter(new WheelGuard(widget));
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        // hasFocus() 是 QWidget 的，eventFilter 交回来的却是 QObject*，所以得转一手。
        // 转不出来就原样放行（这个过滤器只挂在下面那几种控件上，正常不会走到）。
        QWidget *widget = qobject_cast<QWidget *>(watched);
        if (widget != nullptr && event->type() == QEvent::Wheel && !widget->hasFocus()) {
            return true;  // 吃掉：没有焦点就不认滚轮
        }
        return QObject::eventFilter(watched, event);
    }
};

// 主页右栏那个 3D 皮肤预览控件。它同样是纯界面（只认 QImage，不认任何业务对象）：
// 界面只管把贴图喂进去，"贴图从哪来"是业务层的事。
// NOTE: 这里非得 include 不可（别的控件类型前置声明就够了）：skinViewerName() 里
//       要调 SkinViewer::viewerName()，而那是个要在头文件里内联展开的静态函数。
#include "SkinViewer.hpp"

class MainWindowUI {
public:
    explicit MainWindowUI(QMainWindow *parent);

    // ---------- 控件的 objectName ----------
    // 验证工程靠这些名字认控件。**不要**改回按占位文案或条目文本去认——那些是
    // 会被产品逻辑改写的显示内容（占位文案现在就要随"有没有可选版本"切换），
    // 拿显示内容当身份标识的测试会在产品改动时以看不懂的方式挂掉。
    /// 设置页「个性化」栏里「账户」那一格的入口按钮：「账号与外观…」。主页可见入口已经
    /// 搬到底栏账号面板；隐藏兼容控件仍沿用同一文案，避免旧接线分叉。
    static QString accountSkinSettingsButtonName() {
        return QStringLiteral("accountSkinSettingsButton");
    }
    static QString installedVersionComboName() { return QStringLiteral("installedVersionCombo"); }

    // ---------- 安装页那份「模组加载器」（一棵树，和上一页的版本树同一个模式） ----------
    //
    // 「选中哪一条」决定「安装」装什么（0..3 = 加载器，见 VersionManager::loaderKindFromIndex），
    // 而那一个「不装」选项是**默认选中的**，它代表装纯原版那条路。
    //
    // NOTE: 2026-09-23 起这一组是**一棵树**（用户原话："做成和上一页的版本选择相同的
    //       模式，点击以后展开版本，选择版本以后收缩，显示版本"）：
    //         · 顶层五条 = 四个加载器 + 「不装（纯原版）」，前面各挂自己那个标志；
    //         · 点一条加载器 → 它**展开**，底下先摆那两档、再列出它能装的版本；
    //         · 点一个版本 → 那一列**收起来**，选中的版本号写回这条的第二行小字。
    //       换掉的是一组单选钮 + 四个下拉框（`modLoaderGroupName()` /
    //       `loaderVersionComboName()` 那两个名字随之作废）。下标、文案、那五条的顺序
    //       一条都没变 —— 变的只是它们现在长成一棵树。上一版是"钮在左、组合框在右"，
    //       因为那时没能展开/收起；现在展开在**同一行下面**，和版本树那边长得一样。
    //
    /// 那五条在**代码里**叫什么，顺序就是界面上从上到下的顺序。
    ///
    /// NOTE: 树里的条目不是 QObject、没有 objectName —— 所以这已经不是"控件的名字"了。
    ///       它现在只剩三个用处：①给"第 i 条是哪个加载器"这件事一个写得出来的称呼
    ///       （注释、日志、验证工程的失败信息里用）；②条数（5 = 四个加载器 + 一个「不装」）；
    ///       ③和下面那张文案表**一一对应**的那条约束（多一条少一条，界面上就会有条目
    ///       既没称呼也对不上字）。身份仍然只认下标，见 loaderTreeName() 那一段。
    ///
    /// NOTE: 「不装」排在四个加载器**后面**（不是最前面）是刻意的：这样下标 0..3 和
    ///       VersionManager::LoaderKind 的枚举顺序一一对应，中间那层翻译
    ///       （loaderKindFromIndex）就少一个"顺序对不上、勾了 A 装成 B"的机会。
    static QStringList modLoaderOptionNames() {
        return {QStringLiteral("modLoaderFabric"), QStringLiteral("modLoaderForge"),
                QStringLiteral("modLoaderNeoForge"), QStringLiteral("modLoaderOptiFine"),
                QStringLiteral("modLoaderNone")};
    }
    /// 界面上那五条**写出来的字**（树是照这张表摆的），和上面那张表**一一对应**，
    /// 顺序不能各排各的。
    ///
    /// NOTE: 这是**显示内容**（将来要跟着语言/产品文案走），不是身份标识 —— 选中的是
    ///       哪一条看下标（见 loaderTreeName() 那一段），核文案时才比这张表。
    static QStringList modLoaderOptionTexts() {
        return {QStringLiteral("Fabric"), QStringLiteral("Forge"), QStringLiteral("NeoForge"),
                QStringLiteral("OptiFine"), QStringLiteral("不装（纯原版）")};
    }
    /// 「不装（纯原版）」那一条在两张表里的下标。加载器那一组现在是一棵树（见下面
    /// buildTwoColumnPage 里那段），选中它就是"回到装纯原版"—— 初始选中的也是它。
    /// 写死数字的地方一律用这个函数。
    static int modLoaderNoneIndex() { return 4; }
    static QString modLoaderLabelText() { return QStringLiteral("模组加载器"); }
    /// 那棵树自己的 objectName。**条目没有 objectName**（QTreeWidgetItem 不是 QObject）——
    /// 认一条的身份看它 `Qt::UserRole`：顶层是 modLoaderOptionNames() 里的下标（整数），
    /// 子条目是"挑哪一档"的键或者一个具体版本号（两种都是非空串，见
    /// loaderVersionLatestText 上面那段）。判"这是加载器还是版本号"看的是**有没有父条目**，
    /// 不是值的类型。
    static QString loaderTreeName() { return QStringLiteral("loaderTree"); }
    /// 每一条身份存在第几列。只有一列（和版本树同一个约定）。
    static int loaderTreeKeyColumn() { return 0; }
    /// 这棵树最高长到几行：五条加载器都收起来时是 5 行，展开其中一条之后按
    /// "5 + 那条的版本数"往上长，长到这么多行就停（再多就出滚动条）。
    /// 太小的话展开一条只能看见两三个版本，太大（比如整页铺满）会让「安装」按钮被推到
    /// 窗口最底下 —— 用户要的是"安装键做大点在右侧居中"，它得一直在看得见的地方。
    static int loaderTreeMaxRows() { return 10; }
    /// 选中了某个加载器时，**其余那几个加载器**那条上写的话（用户原话："剩余的加载器
    /// （如果不兼容的话）显示为与【已选中的加载器名称】不兼容"）。
    ///
    /// NOTE: 现在**任何两个加载器都不兼容** —— 本启动器一次只装一个加载器
    ///       （`VersionManager::installNamedLoader()` 收的就是一个枚举），所以"选中的那个"
    ///       和"其余的"在安装这件事上就是互斥的。这句小字说的是这条事实，不是"这两家
    ///       上游合不来"。哪天要支持 Forge + OptiFine 那种搭配，改的是**这一处**和安装
    ///       那一条路，界面上别的都不用动。
    static QString loaderConflictText(const QString &selectedName) {
        return QCoreApplication::translate("MainWindowUI", "与 %1 不兼容").arg(selectedName);
    }
    /// 展开之后**头两条**：不钉具体版本号时按哪一档挑（用户 2026-09-23 原话："模组加载器
    /// 选择界面把自动那一行变成下载最新版（alpha/beta）和下载稳定版（release）两行"）。
    ///
    /// NOTE: 那两句里的括号**不上界面**（用户 2026-09-23 又一句："那个稳定版（release）那个
    ///       括号啥的是给你看的，不用实际放在界面里面"）—— 它解释的是"最新版"连 alpha/beta
    ///       一起算、"稳定版"就是 release，写给读代码的人看；界面上那两行就四个字 / 五个字。
    ///
    /// 这两条的 userData 是 LoaderMeta::loaderChannelKey() 那两个键（`@latest` / `@stable`），
    /// 再往下那些才是具体版本号。**认得出这两个键的地方只有一处**：
    /// LoaderMeta::loaderChannelFromKey()（安装那条路在 installLoaderVersion 里解码一次）。
    ///
    /// NOTE: 改动前这里是**一条**「自动（推荐）」，userData 是空串。那一档在数据层挑的是
    ///       "推荐/正式版"（Fabric 最新的正式版、Forge/NeoForge 推荐的那个、OptiFine 官方页
    ///       那一行），也就是现在这条「下载稳定版」—— 空串这个值仍然有意义
    ///       （"没取到列表、用户什么都没点"时按稳定档算），只是不再有一条界面条目去写它。
    static QString loaderVersionLatestText() {
        return QCoreApplication::translate("MainWindowUI", "下载最新版");
    }
    static QString loaderVersionStableText() {
        return QCoreApplication::translate("MainWindowUI", "下载稳定版");
    }
    /// 稳定那一档上**一条正式版都没有**时，那一行第二行写的小字（那一行本身也会变灰、
    /// 点不动 —— 挑一条必然装不出来的路比不挑更糟）。
    ///
    /// NOTE: 只有稳定档会缺。最新档是"上游发过什么就列什么"，列表非空它就一定挑得出东西。
    static QString loaderVersionStableMissingText() {
        return QCoreApplication::translate("MainWindowUI", "没有正式版");
    }
    /// 某一档被挑中之后，加载器那一行第二行的小字（短一点 —— 那一行本来就窄）。
    static QString loaderVersionChannelShortText(bool newestChannel) {
        return newestChannel ? QCoreApplication::translate("MainWindowUI", "最新版")
                             : QCoreApplication::translate("MainWindowUI", "稳定版");
    }
    /// 具体版本号那一条第二行的小字：这一条不是正式版（alpha/beta/快照/预览版）。
    /// 用户原话是"beta 版虽然不稳定，但是也能使用"—— 所以照列，但得看出来"这不是正式版"。
    static QString loaderVersionBetaSubText() {
        return QCoreApplication::translate("MainWindowUI", "测试版");
    }
    /// 版本列表还没回来时，展开的那条底下先摆一个这个（灰的、点不动 —— 它不是能选的东西）。
    /// NOTE: 一次请求 60~250 KB，等一会儿是常事，所以得有个"在等"的样子，不能让那一条
    ///       展开之后空着（空树看着像"这个加载器一个版本都没有"）。
    static QString loaderVersionFetchingText() {
        return QCoreApplication::translate("MainWindowUI", "正在获取…");
    }
    /// 版本列表没拉到时的那个灰条目。具体原因（超时？404？）写进日志 ——
    /// 这条只是告诉用户"列不出来"，而这个加载器照样能装（上面那两档不依赖列表）。
    static QString loaderVersionFailedText() {
        return QCoreApplication::translate("MainWindowUI", "列表没取到");
    }
    /// **这个版本上这个加载器一个能装的都没有**时那个灰条目（用户原话："如果对应版本
    /// 不支持……这个情况下的 列表没取到 换成无可用版本"）。
    /// 它和 loaderVersionFailedText() 是两件事，别混：那是"**没问到**"（网络的事，换一次
    /// 可能就有了，所以那条底下仍然留着那两档）；这是"**问到了、就是没有**"
    /// （真结论 —— 比如 Forge 还没支持某个刚发的快照）—— 这种情况下底下**连那两档都不留**：
    /// 留着它等于让用户挑一条必然装不出来的路（装的时候会以"还没有支持 X 的版本"收场），
    /// 而"这一条我装不了"本来就该在这一眼说完。
    ///
    /// NOTE: 2026-09-23 起"**有预览版、没有正式版**"的那种版本**不算**"一个能装的都没有"
    ///       （用户原话："beta 版虽然不稳定，但是也能使用，要列出来"）—— 列表非空就摆那两档
    ///       和实有的版本号，只有稳定档那一行是灰的（第二行写着「没有正式版」）。
    static QString loaderVersionUnavailableText() {
        return QCoreApplication::translate("MainWindowUI", "无可用版本");
    }

    // ---------- 下载页「游戏」里那两层：版本树 / 安装页 ----------
    //
    // 用户原话：「下载版本那里改成四个分类（正式版 快照版 远古版 愚人节版），每一个占一条，
    // 点开后展开对应版本，再点击具体版本后进入一个新页面……顶上预留版本名称输入框……
    // 再下面放四个选项卡分别对应加载器（默认不选就是原版）也是可以点开选择加载器版本的」。
    //
    // 所以「游戏」那一页里再分两层（QStackedWidget）：
    //   [0] 版本树 —— 四条分类，点开是版本号；点一个版本号去 [1]
    //   [1] 安装页 —— 名称框 + 加载器那棵树（点开选版本）+ 「安装」
    // 左栏那列分类、右栏这份堆叠、底下状态栏都不动（用户明确说了那几样不变）。
    static QString downloadGameStackName() { return QStringLiteral("downloadGameStack"); }
    static int downloadGameTreeIndex() { return 0; }
    static int downloadGameInstallIndex() { return 1; }
    /// 四条分类 + 版本号那份树。**条目没有 objectName**（QTreeWidgetItem 不是 QObject）——
    /// 身份读它的 `Qt::UserRole`：顶层是分类键（见 VersionCategory::channelKey()），
    /// 子条目是版本号。空 = "这一条不是版本号"（顶层那四条就是这个情况）。
    static QString versionTreeName() { return QStringLiteral("versionChannelTree"); }
    /// 每一条身份存在第几列。只有一列，写 0 是给"将来加一列（比如发布日期）"留个说明：
    /// 那一列要是插在前面，认条目的代码就全得改。
    static int versionTreeKeyColumn() { return 0; }
    /// 版本树上面那个筛选框（2026-10-18，coding plan §9「版本树没有过滤/搜索」）。
    /// 筛的是**版本号**（子条目 `Qt::UserRole` 那串），不是分类文字：分类名那四个字
    /// 本来就在眼前，按它筛只会把整栏一起留下或者一起藏掉。
    /// 空格分词、每个词都得出现（和实例搜索、文件搜索同一条规矩）；不区分大小写。
    static QString versionFilterEditName() { return QStringLiteral("versionFilterEdit"); }
    /// 筛选框右边那句结果（`找到 N 个版本` / `没有版本号匹配「…」`）。空筛选时藏着。
    static QString versionFilterHintName() { return QStringLiteral("versionFilterHint"); }
    static QString versionFilterPlaceholderText() {
        return QCoreApplication::translate("MainWindowUI", "按版本号筛选（空格分词，全部匹配）");
    }
    static QString versionFilterNoMatchText(const QString &text) {
        return QCoreApplication::translate("MainWindowUI", "没有版本号匹配「%1」").arg(text);
    }
    static QString versionFilterMatchText(int count) {
        return QCoreApplication::translate("MainWindowUI", "找到 %1 个版本").arg(count);
    }
    /// 条目**下面**那行小字挂在哪个角色上（两棵树共用这一个角色）。
    /// 版本树上它是发布时间（精确到日）；加载器那棵树上是「与 X 不兼容」或者选中的版本号。
    /// NOTE: 它和正文在**同一列**里（由 VersionTreeDelegate 分两行画出来），所以
    ///       不能拿"第二列"当位置 —— 那会多出半张表、还要处理表头。
    /// NOTE: 空串 = 这一行不画第二行（版本树里清单没给时间的、加载器里没选中的都是）。
    ///
    /// 【代理那三个"行装饰"角色自成一档：UserRole + 8 / 9 / 10】
    ///
    /// 这三个（rowSubTextRole / rowClearRole / rowRefreshRole）是**画**那一层读的，
    /// 和"条目身份"那一批（`Qt::UserRole` / `+1` / `+2` / `+3`…，各张表各用各的）必须岔开。
    ///
    /// Why: 2026-09-23 撞过一次，症状很难认 —— `rowRefreshRole()` 和
    ///      `resourceKindRole()`（资源表上每一行都写的那一格）都是 `Qt::UserRole + 3`，
    ///      于是**模组/资源包/光影那三张表的每一行右端都多出一个刷新圈**，正压在展开用的
    ///      那个 `>` 上（用户原话："这个刷新图标会在模组/…的右侧那个 > 的地方重叠一份"）。
    ///      两边各自看都写得没错，是**号码撞了**：谁也编不过、谁也不报错，只是屏幕上多一笔。
    ///      所以这一档整体挪到 8 起步，中间那几格空着不占（将来加角色也先看这里）。
    ///      ui_check 里有一条断言把"这三个和 resourceKindRole() 两两不同"钉住。
    static int rowSubTextRole() { return Qt::UserRole + 8; }
    /// 条目前面那个图标挂在哪个角色上（`Qt::DecorationRole`）—— 见 VersionTreeDelegate：
    /// 给了就画它，没给就按"版本号前面是草方块"那条老规矩画。
    /// 加载器那棵树五条都是给全的（各自那个标志）。
    static int rowIconRole() { return Qt::DecorationRole; }
    /// 「这一行尾那个小叉要画」挂在哪个角色上（真 = 画）。只有安装页那棵树上"**选中的那条
    /// 加载器**"会写真（见 MainWindow::updateLoaderRows()），点了它 = 取消装这个加载器、
    /// 退回「不装（纯原版）」（用户原话："选中以后在那个 > 左侧加一个 x 来取消安装对应版本"）。
    /// NOTE: 版本树从来不写这个角色 —— 那棵树上没有"取消掉这一条"这种事。
    /// NOTE: 号码为什么从 `UserRole + 1` 挪到 `UserRole + 9`，见 rowSubTextRole() 上面那段。
    static int rowClearRole() { return Qt::UserRole + 9; }
    /// 「这一行尾那个**刷新按钮**要画」挂在哪个角色上（真 = 画）。现在是下载页左栏那五个
    /// 分类条目在用（用户原话："每一项的右边放一个刷新按钮"），点它 = 这一类重新问一遍
    /// （见 MainWindow::refreshDownloadCategory()）。画在哪一格、怎么认下那一下，
    /// 见 VersionTreeDelegate::refreshRect()。
    /// NOTE: 和 rowClearRole()（那个「取消」小叉）是两件不同的事，但**一行不能同时挂这两个**：
    ///       那两块地方在行尾是重叠的（一个挂在箭头左边、一个贴着右边缘），同时挂会叠在一起。
    ///       今天界面上没有任何一行同时挂着，ui_check 里有一条断言把这条互斥钉住。
    /// NOTE: 号码为什么从 `UserRole + 3` 挪到 `UserRole + 10` —— 见 rowSubTextRole() 上面
    ///       那一段（原来是撞了 resourceKindRole()，资源表上每一行都长出一个圈）。
    /// ★ 2026-09-25：这个角色现在只表示"这一行**有**这么一个按钮"，**不再表示"这会儿要画"**
    ///   —— 它画不画还看鼠标有没有悬停在这一行上（用户原话：「下载界面只有鼠标悬停在按钮上时
    ///   才显示对应的刷新按钮」）。那一半判据在 VersionTreeDelegate::paint() 里，理由是
    ///   "悬停这一位得视图先填、而视图默认不填"（探针在 tmp/probe_hover）。
    ///   所以：**别拿这个角色当"屏幕上有这一格"用**（命中判定/几何仍然照旧，那两处只认几何，
    ///   而要点到它就必须先悬停在它上面）。
    static int rowRefreshRole() { return Qt::UserRole + 10; }
    /// 「这一组行尾那颗**齿轮**要画」挂在哪个角色上（真 = 画）。2026-09-25 加的，
    /// 用户原话：「每一个组和版本下载几乎一致，点一下打开/关闭，右侧有个箭头和齿轮，
    /// 齿轮可以批量管理实例或者编辑该分组的设置」。
    ///
    /// 它挂在实例页那张树的**每一个分组头**上（`groupIdRole()` 有值的那种行），**虚拟的
    /// 「未分组」那一格也挂** —— 那一格能做的事只有一件（把选中的实例移进某个组），
    /// 可那一件是**唯一的一件**：一个组里一个实例都没有时那个组头是藏着的，当所有真实
    /// 分组都空着的时候，这格齿轮是"把实例放进某个组"的最后一条路（见 GroupRow::
    /// virtualGroup 那段）。它那一档的菜单因此只剩一项，见 groupActionVisible()。
    /// 画在哪一格、怎么认下那一下，见 `InstanceGroupDelegate::gearRect()`（几何的唯一出处）。
    ///
    /// NOTE: 号码从 `UserRole + 11` 起（前三个行装饰占 8/9/10，见 rowSubTextRole() 那段）。
    ///       加新角色时先看上面那段 —— 撞号的表现是"屏幕上多出一笔"，不报任何东西。
    static int rowGroupGearRole() { return Qt::UserRole + 11; }
    /// 悬停在那个齿轮上时弹的那句话（和 rowRefreshTipText() 一个规矩：只在**真落在
    /// 那一格上**才说话，整条行不弹）。
    static QString rowGroupGearTipText() { return QStringLiteral("这一组的更多操作"); }
    /// 分组头行尾那颗齿轮的边长（像素）。比文件夹行那颗（24）小一号 —— 那是一行 56 px 的
    /// 两行式的行，这一行只有一行字，齿轮得装在行高里面。
    ///
    /// NOTE: 这个数是**画出来**的那一圈的边长；命中格还要四周各留 3 px
    ///       （`InstanceGroupDelegate::gearRect()`）。实例树的行高 2026-09-25 起**和版本树
    ///       一样了**（用户原话「每个版本啥的上下高一点，像下载页那样」）、2026-09-29 起
    ///       换成它自己的 `instanceTreeRowHeight()`（58，见上面那一段），16 px 一直装得下；
    ///       `gearGlyphRect()` 里那道"按行高收一收"留着当兜底 —— 行高哪天再变矮，它自己缩。
    static int instanceGroupGearSize() { return 16; }
    // ---------- 实例行行尾那一排按钮（2026-09-25 晚） ----------
    //
    // 用户原话：「实例页面，实例设置也是放在每一条的右侧，鼠标悬停时右侧显示上箭头和垃圾桶，
    // 分别对应 置顶和删除实例，删除前需要弹窗提示，直接点空白区域（和左边图标版本名）进入
    // 实例设置」。于是这一页右边那一排按钮（重命名… / 分组设置… / 实例设置…）整个没了，
    // 每一条实例行自己带上入口。**一行三颗，右起**：
    //
    //   [垃圾桶] [上箭头] [齿轮]      ← 齿轮在**最右边**（这一行的锚）
    //     悬停     悬停    常显
    //
    // 为什么垃圾桶摆最里面（离手最近的那颗是齿轮）：齿轮和上箭头点错了都无害（一个是开设置、
    // 一个是移进置顶组，都能再点回来），垃圾桶是不可逆的那个。它点下去还要过一次确认框，
    // 但"少制造一次误点"和"多弹一个框"比，前者更值。
    //
    // 为什么齿轮**在最右边**（不是挨着组行那颗齿轮对齐成一列）：实例行是这一页的主要内容，
    // 一行里的东西一律贴着右边缘站；组行最右边那颗是展开箭头，两边本来就不在同一列上，
    // 硬对齐反而会留出一截"什么都没有的右边缘" —— 而这一栏宽度是左右两栏里分出来的，一格都紧。
    /// 实例行行尾那三颗按钮的边长（齿轮 / 上箭头 / 垃圾桶共用）。
    static int instanceRowActionSize() { return 16; }
    /// 相邻两颗之间留多少。
    static int instanceRowActionGap() { return 6; }
    /// 命中格比**画出来**的字形每边大多少 —— 和 `gearRect()` / `clearRect()` 那个 3 同款
    /// （字形本身 12~16 px，"点得到的那一圈"得比它大一点）。
    static int instanceRowActionMargin() { return 3; }
    /// 这一行的实例**还能置顶**（= 还没在置顶组里）⇒ 悬停时画得出那颗上箭头。
    ///
    /// 语义和 `rowRefreshRole()` 一样是"这一行**有**这么一个按钮"，**不是**"这会儿要画"
    /// （那一刻归悬停态管，见 InstanceGroupDelegate::paint()）。
    ///
    /// NOTE: 号码 **`Qt::UserRole + 12`** —— 这一档原来是"这一行画哪一颗上/下箭头"那位掩码
    ///       （2026-09-25 上半天的 rowGroupMoveRole() 和那两句话一起删了），一直空到这一条
    ///       回来为止（"今天"指的就是 2026-09-25 加下面这颗箭头的那一天，不是"到现在还空着"）。
    ///       加新角色前先看 rowSubTextRole() 上面那段：撞号的表现是"屏幕上多出一笔"。
    static int rowPinRole() { return Qt::UserRole + 12; }

    /// 这一行的实例**已经在置顶组里** ⇒ 悬停时**同一格**画得出那颗下箭头，
    /// 点它 = 取消置顶（挪出置顶组，落到「未分组」）。
    ///
    /// ★ 用户 2026-09-25 原话：「置顶组里的实例悬停时右侧显示下箭头，即取消置顶」。
    ///   于是那一格现在是**一对反义词共用一格**：不在里面 → ↑「置顶」，在里面 → ↓
    ///   「取消置顶」。一个实例**永远只可能是其中一边**（`groupId` 要么等于置顶组、要么
    ///   不等于），所以这一格没有"两颗都画"或者"一颗都不画"的中间态 ——
    ///   这也正是当初只留一格的原因：那一排右边紧贴着就是垃圾桶，一行里塞不下第四颗。
    ///
    /// NOTE: 它和 rowPinRole() 是**两格相反的角色**，不是"同一个含义的两个写法"：
    ///       `rowPinRole()` = 还挪得动（画 ↑），`rowUnpinRole()` = 已经在里面（画 ↓）。
    ///       **两者互斥**（fillInstanceTree 里一个判 `!=`、一个判 `==`，同一个
    ///       `pinnedGroupId`）。拆成两格而不是"一格 + 另一个方向位"的理由：代理和
    ///       eventFilter 两边各自读一个字就能决定画什么、按下去调谁，不用再对着
    ///       枚举解一遍方向。
    ///
    /// NOTE: 号码 **`Qt::UserRole + 13`**（紧挨着 rowPinRole()，见上一段那两条）。
    static int rowUnpinRole() { return Qt::UserRole + 13; }

    /// 这一行要**照游戏里的样子**画出来的那段 MOTD（**原样**：带 `§` 颜色码和换行）。
    ///
    /// 挂在「服务器列表」那一页每一行的第 0 列上，值是 `ServerPing::Status::motdRaw` 那一串。
    /// 代理按 `MotdText::parse()` 把它拆成一段一段、各带颜色和字形，在那个"第二行小字"的
    /// 位置上画出来（**最多两行** —— 游戏那边也只画两行）。
    ///
    /// NOTE: 和 `rowSubTextRole()`（那一格放的是**洗过的一行**）是**两件事**：洗过的那份在
    ///       tooltip 上（鼠标停上去看全），带码的那份在这一格上（屏幕上按游戏的样子看）。
    ///       两个都留是有意的 —— 只留带码的那份的话，tooltip 里会冒出一串 `§a`。
    ///
    /// NOTE: 号码 **`Qt::UserRole + 14`**（14 是 `rowMotdRawRole` 起的头，见
    ///       rowSubTextRole() 上面那段"撞号的表现是屏幕上多出一笔"）。
    static int rowMotdRawRole() { return Qt::UserRole + 14; }

    /// 「服务器列表」那一页**地址那一格（第 1 列）里真正的那个地址**（没遮过的那一串，
    /// 就是 `servers.dat` 里写的 `host:port`）。
    ///
    /// Why 要专门存一格：那一格**显示的是遮过的写法**（`m-.h-.c-`，见
    /// `InstanceSettingsDialog::maskedServerAddress()`，那是给截图/直播看的），所以"用户
    /// 要的那串真地址"在屏幕上根本不存在 —— 右键复制要复制的正是它。悬停提示里虽然也写着
    /// 一串，但让代码去拆 tooltip 的字符串是另一种撞号（改一个字就悄悄坏掉）。
    ///
    /// NOTE: 号码 **`Qt::UserRole + 15`**（14 是 rowMotdRawRole() 的，见
    ///       rowSubTextRole() 上面那段"撞号的表现是屏幕上多出一笔"）。
    static int serverAddressRole() { return Qt::UserRole + 15; }

    /// 「服务器列表」那一页第 0 列里**那一台服务器真正的名字**（`servers.dat` 里写的那个，
    /// 可以是空串）。
    ///
    /// Why 要专门存一格：那一格显示的是 `unnamedMark()`（「（无名）」）—— 名字空的那些在屏幕
    /// 上写的是一句**我们编的话**，而「编辑」那颗按钮要预填的是**文件里真正的那一串**（空就
    /// 是空，用户想让它继续空着）。拿屏幕上的字当数据用的话，用户一点「编辑」就会看见
    /// 「（无名）」被预填进输入框，一确定就把这四个字**真的写进 servers.dat** 了。
    ///
    /// NOTE: 号码 **`Qt::UserRole + 16`**（15 是 serverAddressRole() 的，见
    ///       rowSubTextRole() 上面那段"撞号的表现是屏幕上多出一笔"）。
    static int serverNameRole() { return Qt::UserRole + 16; }

    /// 「服务器列表」那一页的行高：比版本树那个常数**高一档**（多出来的那一档正好是
    /// 第二行小字的高度）。
    ///
    /// Why 不是"按内容算"：那一棵树开着 `uniformRowHeights(true)`（所有行一样高才排得稳），
    /// 而 MOTD 有的服务器给两行、有的只给一行 —— 按内容算的话"一行的那些"会被按两行的行高
    /// 裁掉，或者反过来把整张表撑得忽高忽低。所以**统一按两行留**，只给一行的那些下面空一点。
    static int serverRowHeight() { return versionTreeRowHeight() + 18; }

    // ---------- 文件管理那几页行里的那两件东西（2026-09-27）----------

    /// 行首那个**资源图标**的边长。和下载页那几张表同一个尺寸（`resourceTreeIconSize()`）
    /// —— 同一个模组在下载页和在本地列表里该长得一样大。
    static int instanceFileIconSize() { return resourceTreeIconSize(); }

    /// 「启用 / 禁用」那枚**拨动开关**的尺寸（宽 × 高）。高度是行高（46）装得下的一档。
    static int toggleSwitchWidth() { return 34; }
    static int toggleSwitchHeight() { return 18; }
    /// 开关里那个圆钮四周留的白。
    static int toggleSwitchKnobMargin() { return 2; }
    /// 行尾那三颗按钮各自弹的那句话（和 rowRefreshTipText() 一个规矩：**只在这一格上**才
    /// 说话，整条行不弹；谁弹的、按哪一格判，见 InstanceGroupDelegate::rowActionTip()）。
    /// 齿轮那句和那一页自己的页眉（`InstanceSettingsDialog::titleText()`）是**两回事**：
    /// 这里是提示，那里是标题。原来那颗按钮上写的「实例设置…」带省略号（Qt 的规矩：点下去
    /// 还要问点什么的才带），这个齿轮点下去直接把那一页开出来、不先问什么，所以不带。
    static QString rowInstanceSettingsTipText() { return QStringLiteral("实例设置"); }
    static QString rowPinTipText() { return QStringLiteral("收藏"); }
    /// 同一格反过来那一颗（下箭头）弹的话。它是 `rowPinTipText()` 的反义词，**不是**
    /// 「取消置顶」四个字以外的东西 —— 用户原话就把它写成"即取消置顶"。
    static QString rowUnpinTipText() { return QStringLiteral("取消收藏"); }
    static QString rowDeleteInstanceTipText() { return QStringLiteral("删除这个实例"); }
    /// 悬停在那两个**行里的按钮**上时弹的那句话（2026-09-24 用户原话：「鼠标悬停在箭头上
    /// 出现刷新二字，之后不少功能要做悬停」）。谁把它弹出来的、按哪一格判，见
    /// VersionTreeDelegate::rowActionTip()（那儿是**几何的唯一出处**，这儿只是文案）。
    ///
    /// NOTE: 只有"行里的按钮"才有话 —— 整条行不弹。整行弹一句的话，鼠标扫过一列版本号
    ///       会一路弹个不停，而那条行上真正的按钮只有一小格（用户要的是"对着那个圈/那个
    ///       叉才有"）。条目自己写 `Qt::ToolTipRole` 的老路照旧（那条走代理的兜底）。
    static QString rowRefreshTipText() { return QStringLiteral("刷新"); }
    static QString rowClearTipText() { return QStringLiteral("取消"); }
    /// 树上用的字号相对**默认**字号的偏移：用户要"文本放大一点"，+2。
    /// 默认字号在系统上是 9~12 pt 不等，所以这里记的只能是偏移，不能记死一个点数。
    static int versionTreeFontStep() { return 2; }
    /// 发布时间那行小字相对**树**字号的偏移（-3 → = 默认字号 -1，比正文还小一号）。
    /// 两处的关系写在这里：那行小字是从 option.font（= 树字号）推出来的，见代理。
    static int versionDateFontDelta() { return -3; }
    /// 树里每一行的高度：两行字 + 上下留白。用户要"上下间距放大一点"。
    /// NOTE: 这是个**常数**，不走"按内容算行高" —— 树上开着 uniformRowHeights，
    ///       第一行（分类，一行字）和底下的版本号（两行字）高度必须一样。
    static int versionTreeRowHeight() { return 46; }
    /// 版本号前面那个草方块图标的边长。
    static int versionTreeIconSize() { return 18; }
    /// 最右边那个 >/V 的边长。
    static int versionTreeArrowSize() { return 12; }
    /// 行内左右留白。
    static int versionTreeRowPadding() { return 6; }

    // ---------- 「实例列表」那张树自己的三个数（2026-09-29） ----------
    //
    // 用户原话：「实例列表里每一个之间留一点空的距离，每一行高一点，图标大一点」。
    // 在那之前这一页借的是版本树那把尺子（行高 46 / 图标 18）—— 用户 2026-09-25 那句
    // 「每个版本啥的上下高一点，像下载页那样」要的就是"两棵树一样高"，所以当时是借的。
    // 现在用户点名"实例列表这一页再高一点、图标再大一点"，于是它有了自己的三个数。
    //
    // ★ 借来的那三个版本树的数**一个字都没动**：下载页那棵版本树、安装页那棵加载器树、
    //   文件管理页那几条、服务端列表那一档，全都还走 versionTreeRowHeight() /
    //   versionTreeIconSize()。"改一处等于改五页"正是把它们分开的理由。
    //
    // NOTE: 组行和实例行**必须一样高**：树上开着 uniformRowHeights（第一行多高整棵树就多高），
    //       而平铺那一档第一行是实例、有分组时第一行是组头 —— 两个数不一样的话，"这张树
    //       多高"会随"有没有分组"翻个面。`InstanceGroupDelegate::sizeHint()` 两种行都返回它。
    /// 实例页那张树（「实例列表」）的行高。
    static int instanceTreeRowHeight() { return 58; }
    /// 实例行前面那个图标（草方块 / 铁砧 / Fabric…）的边长。
    ///
    /// ★ 这一个数**有三处读者**，改它三处必须一起看：① 视图的 `setIconSize()`（图标按多大摆）；
    ///   ② `MainWindow::refreshInstanceList()` 里现算那些 QPixmap 时递进去的边长（画多大）；
    ///   ③ 验证工程里 `item->icon(0).pixmap(边长, 边长)`（**量**多大 —— 那句必须拿这个数去量）。
    ///   ①/③ 和 ② 对不上的症状是"图标被重新采样过"：`QIcon::pixmap()` 会把手里那张**缩**成
    ///   要的尺寸，而 `VersionIcons` 现画的那一张走的是另一条路（16×16 那块贴图是最近邻放的）
    ///   ⇒ 两张的像素**不相等**，断言假红，而屏幕上只是略糊。
    static int instanceTreeIconSize() { return 26; }
    /// 实例列表里**行与行之间**空出来的高度（上下各分一半）。
    ///
    /// NOTE: 空出来的那一带露的是树自己的底色 —— 行本身没有自己的底色（白底白行），所以
    ///       平时看不出来，**选中那一行**的底色带两头缩进去之后才看得见
    ///       （`InstanceGroupDelegate::paint()` 把整条行按这个数上下各收一半再画）。
    ///       想让"每一行之间"都看得见空，得先给行加底色（卡片观感）—— 那是另一件事，没做。
    /// NOTE: `InstanceGroupDelegate` 里那些按整条行算的几何（行尾三颗按钮、组行那颗齿轮和
    ///       箭头）**读的仍然是整条行**：它们在竖直方向一律是"居中"，而收过的这一圈中心
    ///       与整条行中心重合（缺口上下各半分）⇒ 画出来的字形和命中格不会错开。
    static int instanceTreeRowGap() { return 6; }
    /// 安装页那一整页（名称框、四个加载器、安装按钮都在它里面）。
    static QString installPageName() { return QStringLiteral("versionInstallPage"); }
    /// 安装页顶上那句"要装的是基于哪个原版的什么"（点的是哪个版本，到这里必须还看得见）。
    static QString installTargetLabelName() { return QStringLiteral("installTargetLabel"); }
    /// 「版本名称」框：实例的目录名（`versions/<这个名字>`），默认名由 MainWindow 按
    /// "原版号 + 加载器"填（见 MainWindow::defaultInstanceName()）。
    static QString installNameEditName() { return QStringLiteral("installNameEdit"); }
    /// 名称框下面那行灰色小字：**说这一份装下去是什么** —— 平时的说明，或者重名提示。
    /// 两种取值一处决定（MainWindow::updateInstallNameHint()）。
    ///
    /// NOTE: 2026-09-23 起它**不再兼职报错**。名字不能用（有非法字符、空着）时那句原因
    ///       挪到下面那行红字里去了，这一行照旧说"装好之后会在哪儿"（用户原话："如果版本
    ///       名字存在非法字符，不要替换掉那个「装好后在实例...」，而是在下面添加一行红色字"）。
    static QString installNameHintName() { return QStringLiteral("installNameHint"); }
    /// 名称框底下那行**红字**：名字不能用时那句原因（`InstanceFiles::validateDirName()` 的
    /// 返回值，界面这边不抄一份规则）。名字能用的时候它是空的、也藏起来 —— 于是名字框、
    /// 加载器树、安装按钮在正常情况下一个像素都不动。
    static QString installNameErrorName() { return QStringLiteral("installNameError"); }
    /// 安装页那棵树里「预设」那一行（coding plan §7.1 那张图：就在 OptiFine 下面、
    /// 「不装（纯原版）」上面）。
    ///
    /// ★ 它**不是加载器**：`Qt::UserRole` 是下面那个哨兵值，不是 `modLoaderOptionNames()`
    ///   的下标。点它 = 开那张四类的紧凑弹窗（`PresetInstallPicker`），不是选加载器。
    ///   行号不写死：摆的时候插在 `modLoaderNoneIndex()` 那一条**前面**，加载器那五条的
    ///   UserRole 一个都不动（身份认的是 UserRole，不是行号）。
    static int installPresetRowRole() { return -1; }
    /// 那一行左边那两个字（树里没有 objectName，认这一行就靠 UserRole + 这句话）。
    static QString installPresetRowText() {
        return QCoreApplication::translate("MainWindowUI", "预设");
    }
    /// 那一行第二行小字（摘要）：一类都没选 = 「未选择」，选了就说几类。
    /// **唯一出处** —— MainWindow::updateInstallPresetSummary() 和验证工程都读它。
    static QString installPresetSummaryText(int selectedCategories) {
        return selectedCategories <= 0
                   ? QCoreApplication::translate("MainWindowUI", "未选择")
                   : QCoreApplication::translate("MainWindowUI", "已选择 %1 类")
                         .arg(selectedCategories);
    }
    /// 那张弹窗的标题、底上两颗按钮的字。
    static QString installPresetPickerTitle() {
        return QCoreApplication::translate("MainWindowUI", "实例预设");
    }
    static QString installPresetConfirmText() {
        return QCoreApplication::translate("MainWindowUI", "确定选择");
    }
    static QString installPresetCancelText() {
        return QCoreApplication::translate("MainWindowUI", "取消");
    }
    /// 那行红字的红。色值放在这儿当唯一出处（验证工程量像素时要认的就是它）。
    ///
    /// NOTE: 2026-09-25 起**跟着调色板分深浅两档**，不再是一个写死的值。那句"产品里只有
    ///       这一处红字"当时就不对（`folderDangerColor()` 早就有了），更要紧的是**它不分
    ///       深浅这件事本身就是个洞**：这个红是给浅底配的，量出来对窗口底色（浅色主题下
    ///       `#f0f0f0`）是 4.77:1；同一个色落到深色主题的底上只剩 2.26:1（WCAG 正文要
    ///       4.5:1）—— 名字写错了却不给原因，正是"用户以为界面坏了"的那一类。
    ///       现在浅底那一档还是原来那个色，深底那一档借用同一个界面里已经调好的浅红。
    static QColor installNameErrorColor(const QPalette &palette);
    static QString installButtonName() { return QStringLiteral("installButton"); }
    /// 安装页最下面那条**固定底栏**（一条分隔线 + 右下角那颗「安装」）。
    ///
    /// ★ 2026-10-12：用户那份改版文档要求"确认操作放右下角或固定底栏，不悬浮在页面中间"，
    ///   那一颗原来是 `Qt::AlignHCenter` 摆的（就是"悬浮在中间"）。这一格的父窗就是那条
    ///   底栏，样式由 `wtfmlSurface="footer"` 那条 QSS 给（只有一条上边框，不上底色）。
    static QString installFooterName() { return QStringLiteral("installFooter"); }
    static QString installBackButtonName() { return QStringLiteral("installBackButton"); }
    /// 安装页中间那一层滚动区（名称框 + 加载器卡片），见 cpp 里 `installPage` 那段
    /// 「遮挡」的 NOTE。
    ///
    /// ★ 2026-10-18：用户报「下载页（选择模组加载器的页）那个遮挡」—— 这一页原来
    ///   **一个能滚的地方都没有**：名称框 + 卡片 + 五条加载器 + 底栏在默认 900×620 的
    ///   窗口里比正文面高，底下那几条加载器被卡片的下沿裁掉了（展开版本号之后更甚）。
    ///   现在名称框和卡片装在这一层里，顶上那行「← 返回 + 基于哪个原版」和底下那条
    ///   固定底栏留在外面不动。
    static QString installScrollName() { return QStringLiteral("installScroll"); }
    /// 滚动区里那个内容容器（名称框 + 加载器卡片长在它上面）。
    static QString installScrollBodyName() { return QStringLiteral("installScrollBody"); }
    /// 每个选项卡页面贴着内容区留的那一圈边距。**唯一出处** —— 内容区那个覆盖层
    /// （`OverlayHost`）也是一整个"页面"，它那边留白必须和别的页面一样，所以它读的是
    /// 这一个数，而不是自己再写一个。
    ///
    /// ★ 2026-10-12（coding plan §9.1）从 9 抬到 **20**：那一版文档给的"页面外边距
    ///   20–24"，而 9 是照着"导航栏那种紧凑骨架"定的老数 —— 正文面本身已经是一张带圆角
    ///   的卡片了，它离窗口边只有 9 px 时，四边那一圈呼吸空间全挤在选项卡自己的内边距上，
    ///   看着是"内容顶在框上"。20 落在文档那一档的下沿（窗口 900 px 时左右各让出 11 px，
    ///   正文面还够宽）。
    static int pageMargin() { return 20; }

    /// 「安装」按钮的字号相对**默认**字号的偏移。用户原话"安装键做大点"。
    /// NOTE: 记的是**偏移**不是点数（和 versionTreeFontStep() 一个道理：默认字号在系统上
    ///       是 9~12 pt 不等）。
    static int installButtonFontStep() { return 2; }
    /// 「安装」按钮的最小高度（字大了以后再顶着老高度会显得挤）。
    static int installButtonMinHeight() { return 40; }
    static QString installNameLabelText() {
        return QCoreApplication::translate("MainWindowUI", "版本名称");
    }
    static QString installButtonText() { return QCoreApplication::translate("MainWindowUI", "安装"); }
    static QString installBackButtonText() {
        return QCoreApplication::translate("MainWindowUI", "← 返回版本列表");
    }
    /// 安装页顶上那句话：告诉用户这一份是**基于哪个原版**装的（点了树里哪一条，到这里
    /// 必须还看得见 —— 否则"我在给哪个版本装加载器"就全靠记忆了）。
    static QString installTargetText(const QString &gameVersion) {
        return QCoreApplication::translate("MainWindowUI", "将基于原版 %1 安装").arg(gameVersion);
    }

    // ---------- 下载页左栏那份「下载分类」（仿 PCL） ----------
    //
    // 左栏原来是"挑加载器 + 挑版本"，2026-09-22 换成 PCL 那种**一列分类**：游戏 / 整合包 /
    // 模组 / 数据包 / 资源包 / 光影。点哪个，右栏那份 QStackedWidget 就换到哪一页。
    //
    // NOTE: 2026-09-23 起后面几页**全是真的**了：第 0 页「游戏」还是原来那一摊（挑加载器、
    //       挑版本、下载），其余几页是"一个搜索框 + 一棵三层树"（资源 → 游戏版本 → 版本号，
    //       见下面 downloadResourceTreeName() 那段）。原来那几句"还没做"的话连同
    //       downloadPlaceholderText() 一起删掉了 —— 留着就是那几页在说假话。
    //       2026-09-25 加了「数据包」一条（用户原话：「下载那一栏少了个数据包类型，把它放在
    //       模组和资源包中间加上」）—— 插在「模组」和「资源包」之间，其余几条的位置不动
    //       （「模组」还是第 2 条，写死下标的地方不受影响）。
    static QString downloadCategoryListName() { return QStringLiteral("downloadCategoryList"); }
    static QString downloadStackName() { return QStringLiteral("downloadStack"); }
    /// 「游戏」那一页（唯一有内容的那页）的 objectName。
    static QString downloadGamePageName() { return QStringLiteral("downloadGamePage"); }
    /// 下载页页头那个标题（2026-10-12 那次改版加的，见 cpp 里 `buildTwoColumnPage()` 那段）。
    static QString downloadPageTitle() { return QStringLiteral("下载"); }
    /// 页头那行说明写什么 —— **跟着当前停在哪一类变**（第 index 个分类）。
    ///
    /// ★ 2026-10-12：用户那份改版文档要的是"页面头部 = 标题 + 当前状态/来源说明"，而这一
    ///   页的"当前状态"就是"你现在停在哪一类、那一类能干什么"。原来那句"这一栏能干什么"
    ///   只写在搜索框的提示文字里（空框里才看得见，一打字就没了），现在它同时挂在页头上。
    ///   「游戏」那一类没有搜索框（提示是空串），它自己有一句（挑版本、挑加载器、装）。
    static QString downloadHeaderSubtitle(int index) {
        // 「服务端」那一栏不是资源搜索页（见 downloadCategoryServerIndex() 那段），
        // 它自己有一句；漏了这一条就会掉到下面「游戏」那一句上，页头说的和这一页
        // 能干的完全不是一回事。
        if (index == downloadCategoryServerIndex()) {
            return QStringLiteral("新建一个服务端；装好之后在「管理 → 服务端」里启动和看控制台");
        }
        const QString hint = downloadSearchHint(index);
        if (!hint.isEmpty()) {
            return hint;
        }
        return QStringLiteral("挑一个游戏版本，挑加载器，装进实例");
    }
    /// 页头右上角那颗刷新（**只作用于当前这一类**）。
    ///
    /// ★ 2026-10-12：用户那份改版文档把"刷新"从每个分类行里挪到了页面头部（原文：
    ///   「刷新按钮属于"当前分类"自己的页面头部」）。行尾那一颗**留着**（那是用户
    ///   2026-09-23 点名要的，见 VersionTreeDelegate::refreshRect()），页头这一颗是
    ///   同一个动作的第二个入口 —— 两处都调 `MainWindow::refreshDownloadCategory()`。
    static QString downloadHeaderRefreshButtonName() {
        return QStringLiteral("downloadHeaderRefreshButton");
    }
    /// 页头那颗刷新的字。**唯一出处**（原来这一串字只有那颗按钮上写了一遍）。
    static QString downloadHeaderRefreshText() {
        return QCoreApplication::translate("MainWindowUI", "刷新");
    }
    /// 分类的键（进条目的 userData，也是那几张资源表的 objectName 前缀）。
    /// **顺序就是界面上从上到下的顺序**，第 0 个是「游戏」（它没有资源表，有别的两张）。
    ///
    /// ★ 2026-10-18：「服务端」补在**表尾**（coding plan §1.4 那条「下载页里没有
    ///   『服务端』入口栏」）。补在末尾不是为了好写 —— 第 0/1/2 条的下标
    ///   （downloadCategoryGameIndex / ModpackIndex / ModIndex）是别处写死判据的，
    ///   插在中间会把它们全挪位。它和「游戏」一样**没有资源搜索那一套**（见
    ///   downloadCategoryServerIndex() 那段），所以后面那些"按分类键建资源页"的循环
    ///   必须和跳过第 0 条一样跳过它。
    static QStringList downloadCategoryKeys() {
        return {QStringLiteral("game"), QStringLiteral("modpack"), QStringLiteral("mod"),
                QStringLiteral("datapack"), QStringLiteral("resourcepack"),
                QStringLiteral("shader"), QStringLiteral("server")};
    }
    /// 条目上写的字。和上面那张表**一一对应**，顺序不能各排各的。
    static QStringList downloadCategoryTexts() {
        return {QStringLiteral("游戏"), QStringLiteral("整合包"), QStringLiteral("模组"),
                QStringLiteral("数据包"), QStringLiteral("资源包"), QStringLiteral("光影"),
                QStringLiteral("服务端")};
    }
    /// 有内容的那个分类的下标（「游戏」）。写死数字的地方一律用它。
    static int downloadCategoryGameIndex() { return 0; }
    /// 另外两条要按"是哪一页"分岔的分类的下标：
    ///   「整合包」—— 那一页多一颗「导入整合包…」（用户原话「整合包那一页带一个导入功能」）
    ///   「模组」  —— 只有那一页有「模组加载器」这个筛子（用户原话「模组加载器只在模组那栏存在」）
    ///
    /// 判据用**下标**（和 downloadCategoryGameIndex() 一个规矩），不在这里再写一遍分类键的
    /// 字面量：键只在 downloadCategoryKeys() 那张表里写一次，两处各写一份迟早会写歪，
    /// 而写歪的症状是"那一页多长了一个控件/少长了一个控件"。表里现在这两条的位子见那张表。
    static int downloadCategoryModpackIndex() { return 1; }
    static int downloadCategoryModIndex() { return 2; }
    /// 「服务端」那一栏的下标（表尾那一条，2026-10-18 加的）。
    ///
    /// ★ 它**不是**资源搜索页：没有搜索框、没有那张三层树、没有「刷新」那一格（三样
    ///   都没有意义 —— 它不向任何一家资源站问东西）。那一页只有一句说明 + 一颗
    ///   「新建服务端…」，按下去走的是实例页那颗「新建服务端…」同一条路
    ///   （`MainWindow::openNewServerPage()`）。
    ///
    /// ★ 凡是"除「游戏」以外每一类都建一张资源页"的循环，都要和跳过第 0 条一样跳过
    ///   这一条 —— 漏了它，那一页会被建成一棵空的三层树，而「刷新」那一格点下去
    ///   什么都不做（`refreshDownloadCategory()` 认不出这个键）。
    static int downloadCategoryServerIndex() { return 6; }
    /// 「服务端」那一页（一个说明 + 一颗按钮）。
    static QString downloadServerPageName() { return QStringLiteral("serverPage"); }
    /// 那一页上那句说明（**唯一出处**）。
    ///
    /// 为什么要把"装好之后去哪儿"写出来：这一页只负责**造**，造完的服务端不住在
    /// 下载页里（它在「管理 → 服务端」那一页的列表里）。不写这一句，用户装完就停在
    /// 这一页上，找不到刚才那个东西。
    static QString downloadServerHintName() { return QStringLiteral("downloadServerHint"); }
    static QString downloadServerHintText() {
        return QStringLiteral("服务端装好之后会出现在「管理 → 服务端」那一页：启动、看控制台、"
                              "改设置都在那儿。");
    }
    /// 「新建服务端…」那一颗。**和实例页那颗是同一个动作**（同一串字、同一条路），
    /// 只是换个入口；字直接复用 `newServerText()`，别在这儿再写一遍。
    static QString downloadNewServerButtonName() {
        return QStringLiteral("downloadNewServerButton");
    }
    /// 每个分类**能在这儿干什么**的一句话（「游戏」那条是空的：它没有搜索框）。
    ///
    /// 这句话现在的去处是那一页搜索框里的**提示文字** —— 用户原话「上方预留搜索功能
    ///（如果简单就直接做了）」，做成能用的之后，这句话正好落在最该看见它的地方
    ///（一个还没输入过的空框里）。
    /// NOTE: 2026-09-23 之前它是"还没做"那几页上的一句话，措辞是"将来放什么"；现在改成了
    ///       "在这儿能干什么"。顺手把原来写的 "CurseForge" 去掉了 —— 这一轮只做 Modrinth，
    ///       提示里写着它、点进去却没有，就是撒谎（CurseForge 那件事另有原因，见 ModpackImport）。
    /// NOTE: 数据包那一条**必须**说清"下进实例的 datapacks/、要自己放进存档"：原版只认
    ///       `<存档>/datapacks/`，而这一轮做的就是"和三个兄弟类一样下进实例目录"
    ///       （见 ResourceCatalog::folderForKind 那段）。不写的话，用户装完进游戏找不到，
    ///       只会当成装坏了 —— 那句话是这一页唯一说得出这件事的地方。
    static QStringList downloadSearchHints() {
        return {QString(),
                QStringLiteral("搜整合包名，回车；挑一个游戏版本，再挑一个版本号导入"),
                QStringLiteral("搜模组名，回车；挑一个游戏版本，再挑一个版本号装进实例"),
                QStringLiteral("搜数据包名，回车；下进实例的 datapacks/，用的时候放进存档"),
                QStringLiteral("搜资源包名，回车；挑一个游戏版本，再挑一个版本号装进实例"),
                QStringLiteral("搜光影包名，回车；挑一个游戏版本，再挑一个版本号装进实例")};
    }
    /// 第 index 个分类那一页搜索框里提示什么（「游戏」那页没有搜索框，返回空串）。
    static QString downloadSearchHint(int index) {
        const QStringList hints = downloadSearchHints();
        return index >= 0 && index < hints.size() ? hints.at(index) : QString();
    }

    // ---- 后面那几页的那几张资源表（整合包 / 模组 / 数据包 / 资源包 / 光影）----
    //
    // 每页两样东西：一个搜索框 + 一棵**三层**树。三层的走法和用户描述的那条路一一对应：
    //   第一层 资源（图标 + 名字 + 「作者 · 下载量」）→ 点开
    //   第二层 游戏版本 → 点开（这时才去问接口）→
    //   第三层 版本号（文件名 + 「release · 日期 · 加载器」）→ 点它 = 下它
    // 用户原话：「点开模组后是选择游戏版本，在选择游戏版本后展开下方的版本列表，
    // 类似模组加载器的样子」。三层用的是**上一页那两棵树的同一个代理**，所以长得一样。
    //
    // 控件的名字按分类键拼（`modTree` / `shaderSearchEdit` 这种）：这几页的控件一模一样、
    // 只有那几个字不同，所以下面是一段循环搭几遍，不是抄几份。
    static QString downloadResourceTreeName(const QString &categoryKey) {
        return categoryKey + QStringLiteral("Tree");
    }
    static QString downloadSearchEditName(const QString &categoryKey) {
        return categoryKey + QStringLiteral("SearchEdit");
    }

    // ---- 下载页左上角那颗「返回」（2026-09-28）----
    //
    // 用户原话：「点击下载新模组/资源包/...时左上角保留一个返回键，返回到实例设置里面，
    // 和资源包详情啥的一致」。实例设置那几个文件页顶上那颗「下载新模组」把用户送到**这一
    // 页**，而那一下会把实例设置那一页收掉（见 MainWindow::openDownloadCategory）—— 用户
    // 于是丢了原来的地方（连页码带勾选）。这一颗就是让他回得去的那条路。
    //
    // ★★ 2026-09-28 第二版：用户原话「返回按钮也是放在左上角（就是左边栏的顶上）」。
    //    原来是**五栏各一颗**、摆在右栏那行筛子的最前头；现在**六栏共用一颗**，摆在
    //    **左栏顶上**。两件事一起变对了：① 「回到刚才那个实例」与用户此刻翻到哪一栏
    //    无关，本来就不该一栏一颗；② 它在左栏顶上，翻栏时不动，不会跟着右栏刷没了。
    //
    // ★ 它**只在这一次是从实例设置进来的时候才露面**（`setDownloadBackVisible()`），别的
    //   时候连位置都不占 —— 一个常驻的"返回实例设置"在"我就是来逛下载页"的时候是一颗
    //   点了不知道回哪儿的键。
    static QString downloadBackButtonName() { return QStringLiteral("downloadBackButton"); }
    /// 那颗键上的字。**唯一出处** —— 界面和验证工程都读它。
    static QString downloadBackButtonText() { return QStringLiteral("← 返回实例设置"); }
    /// 鼠标停上去那句话：说清"回的是哪个实例的那一页"，以及"从这儿下的东西照旧"。
    static QString downloadBackTooltip() {
        return QStringLiteral("回到刚才那个实例的设置页。从这儿下的东西还是下进那个实例。");
    }

    // ---- 每一页顶上那一行筛子（2026-09-25）----
    //
    // 用户原话：「搜索，加入按版本 模组加载器 来源 类型 名称搜索，一个搜索键和一个清空条件键，
    // 名字/版本可自行输入，这两个框选中时按回车等同于按搜索键，当然模组加载器只在模组那栏
    // 存在，整合包那一页带一个导入功能」。
    //
    // 一行筛子一共这几件：名称（自由输入，就是原来那个搜索框）/ 版本（可输入的下拉）/
    // 加载器（只有模组页）/ 来源 / 类型，外加「搜索」和「清空条件」两颗键。
    // 摆成**两行**（为什么不摆一行见 .cpp 里那一段）：第一行「名称 + 搜索 + 清空条件」，
    // 第二行四个下拉框。
    //
    // 【这一层只知道控件长什么样，不知道每一项是什么】
    // 四个下拉框里的**内容**一个字都不在这儿：来源是 `ResourceCatalog::allSources()`、类型是
    // `categoryOptions()`、加载器是 `loaderOptions()`，那些表都在 src/ 那一侧，而 ui/ 不
    // include src/（见文件头）。所以这一层把空框搭出来，由 MainWindow 灌（和版本树、
    // 加载器树、目录树那几棵一个规矩）。**"类型那一栏要整个藏起来"也是 MainWindow 做的**
    // —— 它按 `categoryOptions()` 有没有回空表决定（回空表 = 这一来源 × 这一类没有分类
    // 可选），藏的是 `getDownloadFilterTypeBox()` 那一个小组（连标签一起，理由见那一段）。
    //
    // 【模组加载器只有模组页有】用户点名的一条。摆法上就是"循环里加一个 if"：只有**模组**
    // 那一页建这个下拉框，别的页连控件都没有（不是建了再藏起来 —— 藏着的控件仍然会被验证
    // 工程按名字找出来，那就不叫"只在模组那栏存在"了）。
    static QString downloadFilterVersionComboName(const QString &categoryKey) {
        return categoryKey + QStringLiteral("VersionCombo");
    }
    static QString downloadFilterLoaderComboName(const QString &categoryKey) {
        return categoryKey + QStringLiteral("LoaderCombo");
    }
    static QString downloadFilterSourceComboName(const QString &categoryKey) {
        return categoryKey + QStringLiteral("SourceCombo");
    }
    static QString downloadFilterTypeComboName(const QString &categoryKey) {
        return categoryKey + QStringLiteral("TypeCombo");
    }
    static QString downloadFilterSearchButtonName(const QString &categoryKey) {
        return categoryKey + QStringLiteral("SearchButton");
    }
    static QString downloadFilterResetButtonName(const QString &categoryKey) {
        return categoryKey + QStringLiteral("ResetButton");
    }
    /// 四个筛子前面那四个小标签上的字。顺序和摆法见 .cpp 里那一段。
    ///
    /// 「模组加载器」写全称而不是「加载器」：用户原话就是这四个字，而且它只在模组那一页上
    /// 出现（别的页没有这一栏），说得全一点不占别处的地方。
    static QString downloadFilterVersionLabel() { return QStringLiteral("版本"); }
    static QString downloadFilterLoaderLabel() { return QStringLiteral("模组加载器"); }
    static QString downloadFilterSourceLabel() { return QStringLiteral("来源"); }
    static QString downloadFilterTypeLabel() { return QStringLiteral("类型"); }
    /// 「搜索」那颗键上的字。用户原话「一个搜索键」。
    static QString downloadFilterSearchText() { return QStringLiteral("搜索"); }
    /// 「清空条件」那颗键上的字。用户原话「一个清空条件键」。
    ///
    /// NOTE: 它清的是**筛子**（四个下拉框 + 两个输入框），不是搜索结果 —— 名字里带"条件"
    ///       就是为了跟"清空列表"分开。
    /// NOTE: 2026-09-26 起清完**重搜一发**（用户原话「修改任何内容（不论是来源网站还是…）
    ///       都刷新一下」）。从前这里是"清完不动" —— 于是清完的界面和列表对不上：六个框全
    ///       空着，底下还摆着上一批按老条件筛出来的结果，而那正是用户点这颗键要摆脱的东西。
    static QString downloadFilterResetText() { return QStringLiteral("清空条件"); }
    /// 版本那一格空着时里面的灰字。**必须 ≤ `downloadFilterMinimumContentsLength()` 个字符**，
    /// 否则会把框顶宽（那个坑见 .cpp 里 installedVersionCombo 那一段）。
    static QString downloadFilterVersionPlaceholder() { return QStringLiteral("全部版本"); }
    /// 四个筛子下拉框的 `minimumContentsLength`（宽度按这个算，不按内容算）。
    ///
    /// 为什么不按内容算（`AdjustToContentsOnFirstShow`，也是 Qt 的默认）：那一个在**第一次
    /// 显示**时把宽度定死，而这一层搭出来的那一刻框还是空的 —— 之后 MainWindow 灌进来的
    /// 是「1.21.1-pre1」这种长度不定的版本号，宽度就再也跟不上（灌长版本号时框会突然变宽，
    /// 整行跟着跳）。8 是量着"`1.21.1` 这种版本号放得下"取的。
    static int downloadFilterMinimumContentsLength() { return 8; }

    /// 四个筛子下拉框的**宽度上限**。上面那个 `minimumContentsLength` 管的是"有多宽"，
    /// 这一个管的是"不许比这更宽"——两个一起才把这一行的宽度钉住。
    ///
    /// 为什么非要有上限：**这一行摆不下就顶大整窗的 sizeHint**，而 E5 那条断言（≤900）
    /// 是主窗口默认宽度的依据。2026-09-25 量的：加上这一行之后整窗 sizeHint 从 ≤900 涨到
    /// **934**，正好是"模组那一页四个下拉框"顶出来的（模组页 644，别的页 454）—— 也就是说
    /// 到了默认那个 900 px 宽的窗口上，这一行会被右边界切掉一块（四个框各自的
    /// minimumSizeHint 就是它的 sizeHint，挤不下去）。
    /// 96 是照着"四个框 + 四个标签 + 七道 8 px 间隙 ≤ 610（900 px 窗口下右栏的宽度）"
    /// 反算的，留了三十来 px 余量。代价：框里那串字会被省略（「CurseForge」显示成
    /// 「CurseFor…」）—— 展开下拉框看得见全的，而这些框本来就是"点开挑一个"的东西。
    static int downloadFilterComboMaxWidth() { return 96; }
    /// 整合包那一页那颗「导入整合包…」的 objectName。用户原话「整合包那一页带一个导入功能」。
    ///
    /// NOTE: 字和实例页那颗是**同一句**（`importModpackText()`，一个"导入整合包…"不该有
    ///       两种说法），但**是两颗不同的按钮**：那一个在实例页左栏、走的是"往当前实例里
    ///       导入"，这一颗在下载页、走的是同一条路。名字故意不一样（"importModpackButton"
    ///       已经给实例页那一颗用了），不然 `findChild` 会撞出个先找到谁看运气。
    static QString downloadFilterImportModpackButtonName() {
        return QStringLiteral("modpackImportButton");
    }
    /// 那几张资源表里**前面那个图标画多大**（版本树/加载器树是 18）。
    /// 用户要"会有图片"，18 px 那个尺寸看不清缩略图；行高仍是 versionTreeRowHeight()（46），
    /// 32 px 的图在行里上下各留 7 px，不挤。
    static int resourceTreeIconSize() { return 32; }
    /// 实例设置「文件管理 → 服务器列表」那一页前面那个**服务器图标**画多大（2026-09-27）。
    ///
    /// 和资源表那一条同一个理由（18 px 看不清图）：那一格现在是服务器自己给的
    /// **favicon**（64×64 起步的 PNG），画成 18 px 就是一团分不出是什么的色块。
    /// 行高仍是 versionTreeRowHeight()（46）—— 32 px 上下各留 7 px，不挤。
    static int serverIconSize() { return 32; }
    /// 资源那一行（最上面那一层）上挂的"这是哪一类"：存**分类键**
    ///（`downloadCategoryKeys()` 里那几个之一，也是 `ResourceCatalog::kindKey()`）。
    ///
    /// 为什么要挂在条目上：用户点**第三层**（一个版本号）那一下要算"下到哪儿去"
    ///（模组 → `mods/`、数据包 → `datapacks/`、资源包 → `resourcepacks/`、
    /// 光影 → `shaderpacks/`、整合包 → 缓存目录），
    /// 而那一刻手上只有条目 —— 从它往上两层、回到第一层那一行读回来就是了。
    /// 不反查树控件的 objectName（那是控件的名字，不是数据），也不按页面下标猜。
    /// 存的为什么是键而不是枚举的整数值：读的时候可以直接走
    /// `ResourceCatalog::kindFromCategoryKey()` —— 那个函数认得出来才是"这一行是我们建的"，
    /// 而把一个 int 硬转成枚举是没有这层校验的（值是脏的就会变成一个不存在的 Kind）。
    ///
    /// NOTE: 挑号码的时候看一眼 rowSubTextRole() 上面那段 —— 2026-09-23 这里原来和
    ///       `rowRefreshRole()` 撞了（都是 `UserRole + 3`），症状是这几张表上**每一行**
    ///       右端都多画一个刷新圈压在 `>` 上。代理那三个角色现在自成一档（+8/+9/+10），
    ///       这一档（条目身份）用 +0/+1/+2/+3…，两边别再串。
    static int resourceKindRole() { return Qt::UserRole + 3; }

    /// 这一条是**从哪个来源**搜出来的（存 `ResourceCatalog::sourceKey()` 那个键，
    /// `modrinth` / `curseforge`）。
    ///
    /// 为什么要挂在条目上：点开第二层去问"这个资源有哪些版本"时，**必须问对它那一侧** ——
    /// 两个来源的 id 空间会撞（Modrinth 的 base62 id 里 `238222` 也是合法的，而 CurseForge
    /// 那边真有这么个号），问错了不会报错，只会显示另一个资源的版本列表。
    /// 存键不存枚举：理由和 resourceKindRole() 一样（读回来走 `sourceFromKey()`，
    /// 认不出来才是"这一行不是我们建的"），而且**来源是会变的**（用户换一下下拉框，
    /// 同一行文字就来自另一边了），键比一个 int 更经得起写进盘、写进日志。
    static int resourceSourceRole() { return Qt::UserRole + 4; }

    /// 这一格的**图标身份**：`ResourceCatalog::Hit::iconCacheId()`（带来源前缀）。
    ///
    /// 为什么要单独挂一格：图标那条频道回来说的是 cacheId（见 ResourceStore::iconReady），
    /// 而它和 `Qt::UserRole` 里那个 project_id **不是一回事** —— 两个来源的 id 会撞，
    /// 只按 project_id 找行的症状是"这一格画着另一个来源里同号那个资源的图标"。
    /// 在这里存一次（灌条目那一刻从 Hit 取），回来时按它直接认，别在两处各拼一遍。
    static int resourceIconIdRole() { return Qt::UserRole + 5; }

    static QString folderListName() { return QStringLiteral("folderList"); }
    static QString instanceListName() { return QStringLiteral("instanceList"); }
    /// 实例页页头那个**标题**。
    ///
    /// 2026-10-12：用户那份界面改版文档给每一页定了同一套骨架（侧栏 + 页头 + 一个正文面），
    /// 页头第一行就是页面标题。文案只有这一处，验证工程也按这个名字/这句话找。
    static QString instancePageTitle() { return QStringLiteral("实例"); }
    /// 侧栏那个小节标题（原来是一个 `QGroupBox("游戏文件夹")` 的缺口式标题）。
    ///
    /// ★ 2026-10-12：那份文档点名「小节使用"标题 + 内容卡片"，避免传统 QGroupBox 的缺口式
    ///   标题」—— 于是它变成一行普通的粗体字（`wtfmlTextRole="sectionTitle"`），框整块没了。
    ///   文案一个字没改：它一直说的是"这一列管的是哪一份 .minecraft 目录"。
    static QString folderSectionTitleText() { return QStringLiteral("游戏文件夹"); }
    static QString folderSectionTitleName() { return QStringLiteral("folderSectionTitle"); }
    static QString instanceFolderLabelName() { return QStringLiteral("instanceFolderLabel"); }
    // 「游戏文件夹」那一列上的按钮。这里**必须**有 objectName —— 「移除」的文案当初
    // 从「删除文件夹」改成过「移除文件夹…」（那个词会让人以为盘上的文件也没了），按文案
    // 找控件的写法会在改文案的第一次跑就变成"找不到控件、后面全不跑"。文案另有 *Text()，
    // 只用来核对显示内容。
    //
    // NOTE: 「移除文件夹…」那个按钮与列表下面那段提示 **2026-09-24 一起删掉了**（用户
    //       原话：「下面那个移除文件夹也可以弃掉了，下面那个文本也删了」）—— 「移除」
    //       现在只有行尾齿轮菜单里那一项。这里也不再留有 removeFolderButtonName() /
    //       removeFolderText() / folderHintLabelName()，别照着旧记性写。
    //
    // NOTE: 2026-09-25 起这一列是**两个**按钮：「添加文件夹…」+ 下面那颗「导入整合包…」
    //       （用户原话：「导入整合包放在左边添加文件夹下面」）。它们俩同处「游戏文件夹」
    //       这一组、宽度都顶满这一栏 —— 都是"往这一列里添东西"的动作。
    static QString addFolderButtonName() { return QStringLiteral("addFolderButton"); }
    static QString addFolderText() { return QStringLiteral("添加文件夹…"); }
    /// 启动器自带的那一份（和 exe 同一个目录里的 .minecraft）在列表**上面那行**写什么。
    ///
    /// NOTE: 用户 2026-09-24 原话：「显示的时候，和启动器在一个目录里的叫当前文件夹」。
    ///       注意它不是"正在用的那一份"（那一份是**选中**哪一行说了算，两者大多数时候
    ///       是同一个，但用户加了别的目录之后就不是了）—— 这一份认的是位置，不是状态。
    static QString folderAssistantTitle() { return QStringLiteral("当前文件夹"); }
    // 实例页**右栏那一排按钮 2026-09-25 傍晚没了**（用户原话：「删掉实例页面右侧三个按钮
    // （因为都已经分别有入口了），顺便把下面的文本干掉」）。三颗各自的去处：
    //
    //   - 「实例设置…」= **点这一条实例行**（空白、左边那个图标、版本名都算），行尾还有
    //     一颗**常显**的齿轮做同一件事（追加答复：「要，一颗齿轮常显」）；
    //   - 「重命名…」= 搬进了「实例设置」那一页；
    //   - 「分组设置…」= 挪到「实例列表」那个框**外面**的右上角（追加答复：「分组设置先放
    //     右上角框外不删了」），所以只剩它这一颗按钮，名字和文案都留着。
    //
    // 所以 `renameInstanceButtonName()` / `instanceSettingsButtonName()` 这两个**已经删了**，
    // 别照着旧记性写；`instanceSettingsText()` 留着（底栏那行齿轮的 tooltip 还在用它）。
    //
    // NOTE: 这里**不做右键菜单**（仓库里至今一处 QMenu 都没有 —— 右键那几处一律走
    //       Qt::CustomContextMenu 或事件过滤器自己认坐标，不弹菜单；而且验证工程跑在
    //       offscreen 上，QMenu::exec() 会阻塞在事件循环里几乎驱动不了）—— 行里的东西
    //       一律是"画出来的一格 + 事件过滤器认下那一下"，见 InstanceGroupDelegate 那三处几何。
    //
    // NOTE: 2026-09-25 上半天的「停止游戏…」也不在这排里了（入口只剩底栏左面板里那一行的
    //       电源，见 MainWindow::stopGameSession()），所以这里也没有 stopGameButtonName()。
    static QString groupInstanceButtonName() { return QStringLiteral("groupInstanceButton"); }
    /// 「导入整合包…」。它和上面那三颗**不是一回事**：那三颗改的是已有的东西（前两颗
    /// 作用于选中的实例、第三颗作用于分组清单），这个是**造**一个新实例出来。所以它不在右栏那一排里 —— 2026-09-25 起它归左栏
    /// 「游戏文件夹」那一组，紧挨在「添加文件夹…」下面（用户原话：「导入整合包放在左边
    /// 添加文件夹下面」）。两者都是"往这一列里添东西"。
    static QString importModpackButtonName() { return QStringLiteral("importModpackButton"); }
    /// 那颗按钮**下面**那句说明（"收 Modrinth 格式的整合包…"）。
    ///
    /// ★ 挂名字是为了钉住它**只能这么短**：那句话是折行的，而折行 QLabel 的 sizeHint
    ///   宽度是横向 advance 的分档 —— 写长一点就会把**整窗**的 sizeHint 顶过 900
    ///   （ui_check E5）。量到过两个数：第一版 70 个汉字 → 整窗 1042（红），现在这句
    ///   → 228、整窗 818（绿）。理由全文在 .cpp 那一段。
    static QString importHintLabelName() { return QStringLiteral("importHintLabel"); }
    // 按钮上的字。验证工程**不要**拿它们当身份标识（那是显示内容），只用来核对文案。
    /// NOTE: 「重命名…」那颗按钮**2026-09-25 傍晚删了** —— 改名搬进了「实例设置」那一页
    ///       （用户原话：「重命名进实例设置」）。所以 `renameInstanceText()` 也一起没了，
    ///       别再照着旧记性写；那一页里那行自己的文案在 InstanceSettingsDialog 里。
    ///
    /// NOTE: 2026-09-25 起它开的那张窗**不再问"这个实例归到哪个组"**，改成了分组自己的
    ///       设置（有哪些组、叫什么名字、什么顺序、默认展开还是收着、**哪一个是置顶组**）
    ///       —— 用户原话：「不再是针对单个实例的设置，而是对现有分组数目/名称等的设置」。
    ///       所以文案跟着从「分组…」变成「分组设置…」，而且它**不读选中的实例**。
    ///       想按实例归组走组行行尾那颗齿轮（GroupActionAddSelected）或者分组窗里那条
    ///       归属单选钮原来那一套？—— 那一套**没有了**，别再照旧记性找。
    static QString groupInstanceText() { return QStringLiteral("分组设置…"); }
    /// NOTE: `instanceSettingsText()`（「实例设置…」，那颗按钮上的字）**2026-09-25 傍晚跟着
    ///       按钮一起删了**。现在这一页的那句话有**两个**出处，各管一件事，别再合成一个：
    ///       `rowInstanceSettingsTipText()`（实例行行尾那颗齿轮的悬停提示）和
    ///       `InstanceSettingsDialog::titleText()`（覆盖层页眉上那行「实例设置：<实例名>」）。
    /// NOTE: 带省略号 —— 按 Qt 的规矩，点了之后还要弹框问点什么的，文案结尾就带省略号
    ///       （这个字上传出去的信息是"点下去不等于事情就开始了"，和上面三个一致）。
    static QString importModpackText() { return QStringLiteral("导入整合包…"); }
    // ---------- 实例列表那张树里的「组」 ----------
    // 组头不是实例：选中它不等于选中某个版本。判据是**它有没有组 id**（存在
    // Qt::UserRole+1 里，见 .cpp），而不是"它的 UserRole 是不是空的"—— 后者在
    // "某个实例的 id 恰好是空串"时会误判（今天不可能，但判据不该依赖那个）。
    /// 实例条目上挂着实例 id 的 role（和下拉框同一个约定）。
    static int instanceIdRole() { return Qt::UserRole; }
    /// 组头上挂着组 id 的 role。**没有这个 role 的才是实例条目**。
    static int groupIdRole() { return Qt::UserRole + 1; }

    /// 这一条目是不是一个**分组头**（而不是一个实例）。
    ///
    /// 判据是**它有没有组 id 那个 role** —— 和 `instanceIdOf()` 那道守卫是同一条
    /// （`instanceIdOf()` 返回空串的两种情形里，只有"组头"是正常的）。做成唯一一处
    /// 是因为读它的地方已经有三个：灌树（决定挂不挂齿轮）、代理（决定按哪种行画）、
    /// 开关行的判据（组行的激活是"开/关"，见 ui/RowActivation.hpp）。
    static bool isGroupItem(const QTreeWidgetItem *item) {
        return item != nullptr && item->data(0, groupIdRole()).isValid();
    }
    /// 「未分组」那一格的名字。它是个**虚拟**的组（不进文件），只在它下面真的挂着实例时
    /// 才画出来 —— 一个组都没有的时候列表是平铺的，不插任何组头。
    ///
    /// NOTE: 这一句原来住在 GroupDialog 里（跟着"这个实例归到哪个组"那排单选钮一起）。
    ///       2026-09-25 那半搬走了，分组设置窗也**不再认得**「未分组」这个东西（它只认
    ///       真实的分组），于是这句话搬到这里 —— 现在唯一的读者是灌树那个函数和
    ///       "移到未分组"那条提示。文件里的那个保留值另有出处（InstanceGroupStore::
    ///       ungroupedMarker()），和这一句是两回事，别混。
    static QString ungroupedGroupText() { return QStringLiteral("未分组"); }
    /// 组头的文字（组名 + 里面有几个实例）。规则和分组对话框里那张列表共用一份。
    static QString groupItemText(const QString &name, int count) {
        return GroupDialog::groupItemText(name, count);
    }

    // ---------- 实例列表那张树的搜索与筛选（coding plan §7.2）----------
    //
    // 一页两件东西：一个搜索框 + 四颗筛选 chip，摆在正文面里、树的上方。**判据不在这儿**
    // —— "哪几个串参与搜索、四档筛子怎么算"全在 `src/manage/InstanceFilter.hpp` 那一层
    // （纯函数，verify 里逐格念），这一层只摆控件、只读自己的 `isChecked()`。
    //
    // ★ 四颗的关系**不是**下载页那种"一排单选"：原版 / 模组 是一对互斥的档，可更新 /
    //   最近运行各自独立，四颗全不勾 = "不管"。互斥由 `MainWindow` 那一层认（点「模组」
    //   把「原版」摘掉）—— 控件这一层**没有** `setAutoExclusive`（那样会把"再点一下取消"
    //   也一起禁掉，而"点一颗自己就能取消"正是这里要的手感）。
    enum class InstanceFilterButton {
        Vanilla,    ///< 「原版」：没检测到模组加载器
        Modded,     ///< 「模组」：检测到模组加载器
        Updatable,  ///< 「可更新」
        Recent,     ///< 「最近运行」
    };
    /// 那一排筛选 chip 的外框（验证工程按它量"整排有没有超出去"）。
    static QString instanceFilterRowName() { return QStringLiteral("instanceFilterRow"); }
    static QString instanceSearchName() { return QStringLiteral("instanceSearch"); }
    /// 搜索框里那句占位说明。§7.2 点名的三样（实例名 / 版本号 / 加载器）就写在这儿 ——
    /// 用户不用去猜"这一格搜的是什么"。
    static QString instanceSearchPlaceholder() {
        return QStringLiteral("搜索实例名、版本号或加载器");
    }
    /// 那一格搜不到东西时说的话。
    ///
    /// ★ 它**只在真筛过、且一条都没剩下**的时候露出来：不筛的时候树是空的（"一个实例都
    ///   没有"）是另一件事，那句话由页头那行说明（`当前文件夹：…（0 个实例）`）来说。
    ///   两件事混成一句的症状是"用户明明有 60 个实例，却看见'没有符合条件的实例'"。
    static QString instanceNoMatchText() { return QStringLiteral("没有符合筛选条件的实例"); }
    static QString instanceNoMatchLabelName() { return QStringLiteral("instanceNoMatch"); }

    /// 某颗筛选 chip 的 objectName。**按这一档点名找**（不解析文字）：文字会改。
    static QString instanceFilterButtonName(InstanceFilterButton button) {
        switch (button) {
        case InstanceFilterButton::Vanilla:
            return QStringLiteral("instanceFilterVanilla");
        case InstanceFilterButton::Modded:
            return QStringLiteral("instanceFilterModded");
        case InstanceFilterButton::Updatable:
            return QStringLiteral("instanceFilterUpdatable");
        case InstanceFilterButton::Recent:
            return QStringLiteral("instanceFilterRecent");
        }
        return QString();
    }
    /// 那一颗上写的字。**唯一出处**（产品、验证工程、tooltip 都读它）。
    static QString instanceFilterText(InstanceFilterButton button) {
        switch (button) {
        case InstanceFilterButton::Vanilla:
            return QStringLiteral("原版");
        case InstanceFilterButton::Modded:
            return QStringLiteral("模组");
        case InstanceFilterButton::Updatable:
            return QStringLiteral("可更新");
        case InstanceFilterButton::Recent:
            return QStringLiteral("最近运行");
        }
        return QString();
    }
    /// 那一颗的悬停说明。**口径都写在这儿**（尤其"可更新"和"最近运行"这两档，用户没法
    /// 从两个字里猜出它们认的是什么）。
    static QString instanceFilterTipText(InstanceFilterButton button) {
        switch (button) {
        case InstanceFilterButton::Vanilla:
            return QStringLiteral("只看没装模组加载器的实例（原版）。");
        case InstanceFilterButton::Modded:
            return QStringLiteral("只看装了模组加载器的实例（Forge / Fabric / NeoForge / Quilt）。");
        case InstanceFilterButton::Updatable:
            return QStringLiteral("只看查过更新、而且真有几条能更的实例"
                                  "（在实例设置里点过「检查更新」的才算）。");
        case InstanceFilterButton::Recent:
            return QStringLiteral("只看启动过、账本上还留着记录的实例（最近 20 次以内）。");
        }
        return QString();
    }

    // ---------- 实例页顶部的模式切换：客户端实例 | 服务端（coding plan §6.1）----------
    //
    // §6.1 原话：「实例管理页顶部增加模式切换：`客户端实例 | 服务端`。两边复用"左侧根目录 +
    // 右侧条目"的页面骨架，但使用不同模型和操作」。
    //
    // ★ 摆位：这一排**在正文面里、最上面**（筛选行/树那一切的上方），不在页头右上角那排
    //   页面级操作里 —— 切换的是"下面这一页说的是什么"，和「分组设置…」不是一类东西。
    // ★ 左边那一栏（游戏文件夹）**两种模式共用**：服务端的根挂在 `<数据目录>/Servers` 底下，
    //   "选哪一份 .minecraft"这件事两边都要（换了数据目录 = 换了一整套实例，也换了一整套服务端）。
    enum class InstanceMode {
        Client,  ///< 客户端实例（老那一页）
        Server,  ///< 服务端（M4 新那一页）
        Preset,  ///< 实例预设（2026-10-18 从设置页搬进来：管理页不再是一扇覆盖页）
    };
    static QString instanceModeRowName() { return QStringLiteral("instanceModeRow"); }
    /// 某一颗模式按钮的 objectName。**按这一档点名找**（不解析文字）。
    static QString instanceModeButtonName(InstanceMode mode) {
        switch (mode) {
        case InstanceMode::Client:
            return QStringLiteral("instanceModeClient");
        case InstanceMode::Server:
            return QStringLiteral("instanceModeServer");
        case InstanceMode::Preset:
            return QStringLiteral("instanceModePreset");
        }
        return QString();
    }
    /// 那一颗上写的字。**唯一出处**（§6.1 点名的那两个词）。
    static QString instanceModeText(InstanceMode mode) {
        switch (mode) {
        case InstanceMode::Client:
            return QStringLiteral("客户端实例");
        case InstanceMode::Server:
            return QStringLiteral("服务端");
        case InstanceMode::Preset:
            return QStringLiteral("预设");
        }
        return QString();
    }
    static QString instanceModeTipText(InstanceMode mode) {
        switch (mode) {
        case InstanceMode::Client:
            return QStringLiteral("这一页管的是客户端实例（装在 versions/ 底下、点一下能启动"
                                  "游戏的那些）。");
        case InstanceMode::Server:
            return QStringLiteral("这一页管的是服务端（装在数据目录的 Servers/ 底下、能开服"
                                  "的那些）。");
        case InstanceMode::Preset:
            return QStringLiteral("这一页管的是实例预设：把一份实例的按键、游戏设置、服务器"
                                  "列表这些收成一份，再落到别的实例上。");
        }
        return QString();
    }
    /// 正文那一层 `QStackedWidget`（第 0 页 = 客户端，第 1 页 = 服务端，第 2 页 = 预设）。
    static QString instanceModeStackName() { return QStringLiteral("instanceModeStack"); }
    /// 「预设」那一页的宿主（内容由 `PresetController` 塞进来）。
    static QString instancePresetPageName() { return QStringLiteral("instancePresetPage"); }

    // ---------- 服务端那一页（coding plan §6 / M4）----------
    static QString serverListName() { return QStringLiteral("serverList"); }
    /// 「一个服务端都还没有」那句（和实例页「没有符合筛选条件的实例」不是一回事：这一句
    /// 说的是"**盘上真的一条都没有**"）。平时收着，列表空着时才露。
    static QString serverEmptyLabelName() { return QStringLiteral("serverEmpty"); }
    static QString serverEmptyText() {
        return QStringLiteral("还没有服务端。点下面的「新建服务端…」装一个。");
    }
    /// 「新建服务端…」那一颗（正文面底部）。**是造一个新东西出来**，不是改已有的 ——
    /// 所以它是这一页的主操作，摆在列表下面、整栏宽。
    static QString newServerButtonName() { return QStringLiteral("newServerButton"); }
    static QString newServerText() { return QStringLiteral("新建服务端…"); }

    // ---------- 「新建服务端」那一页（coding plan §6.2 / M4）----------
    //
    // ★ `ui/` **不 include `src/`**（见 GroupDialog 文件头那段老规矩）：这一页只摆控件、
    //   只把"用户填了什么"如实报出去；八档的表、每一档要填哪几格、"这一档要下什么"那句
    //   计划说明，全由 `MainWindow` 那一个翻译点喂进来（`NewServerPage::setFlavors()` /
    //   `setSummary()`）。
    static QString newServerPageName() { return QStringLiteral("newServerPage"); }
    static QString newServerNameEditName() { return QStringLiteral("newServerNameEdit"); }
    static QString newServerFlavorComboName() { return QStringLiteral("newServerFlavorCombo"); }
    static QString newServerGameVersionEditName() {
        return QStringLiteral("newServerGameVersionEdit");
    }
    static QString newServerLoaderEditName() { return QStringLiteral("newServerLoaderEdit"); }
    static QString newServerInstallerEditName() {
        return QStringLiteral("newServerInstallerEdit");
    }
    static QString newServerBuildEditName() { return QStringLiteral("newServerBuildEdit"); }
    /// 「目录」那一行那个**只读**的字（`Servers/<名字>`，算出来的，不是选出来的）。
    static QString newServerTargetDirName() { return QStringLiteral("newServerTargetDir"); }
    static QString newServerMemorySpinName() { return QStringLiteral("newServerMemorySpin"); }
    /// 「这一档要下什么、装完是什么样」那一句（计划说明）。由 `MainWindow` 现算现喂。
    static QString newServerSummaryLabelName() { return QStringLiteral("newServerSummary"); }
    static QString newServerCreateButtonName() { return QStringLiteral("newServerCreateButton"); }

    // ---------- 服务端行尾那颗主操作（coding plan §6.2 的操作 / M4-3）----------
    //
    // ★ 用户 2026-10-13 定的：「行尾按钮 + 右键菜单」。所以一行上**只有一颗**按主操作
    //   （未装好 = 修复 / 能启动 = 启动 / 在跑 = 停止），其余的（强制结束、打开控制台、
    //   打开目录、删掉…）全在右键菜单里 —— 一行塞四颗按钮，扫一眼就不知道点哪颗了。
    //
    // 那一颗挂在列表的**第 1 列**上（第 0 列是名字/状态那串字，宽度自适应；第 1 列定宽）。
    // 认它走 objectName，**不认文字**（文字按状态变）。
    static QString serverRowActionName() { return QStringLiteral("serverRowAction"); }
    /// 行尾那一列多宽（够一颗「强制结束」四个字还有余量）。
    static int serverRowActionWidth() { return 96; }
    /// 服务端那一行的行高。摆得下一颗 24 高的按钮还要留出上下间距。
    /// NOTE: 名字**故意**不叫 `serverRowHeight()` —— 那个是上面「服务器列表」（`servers.dat`
    ///       那一页，一行两行小字的那种）的行高，两件事差得远，重名只会让人读错。
    static int serverWorkspaceRowHeight() { return 44; }

    /// 行尾那颗按钮上的字。**唯一出处** —— 界面照它摆，验证工程照它核。
    static QString serverStartText() { return QStringLiteral("启动"); }
    static QString serverStopText() { return QStringLiteral("停止"); }
    static QString serverRepairText() { return QStringLiteral("修复"); }
    /// 停止中（那颗按钮是灰的，等它自己退干净）。
    /// NOTE: **没有"启动中"那一档** —— 启动中照样让用户按得动「停止」（它就在读 stdin 了，
    ///       写进去那一句会被它在起来之后读到）。摆一颗灰着的名字，只会让"起不来"那一刻
    ///       没有出路。
    static QString serverStoppingText() { return QStringLiteral("停止中…"); }

    /// 右键菜单里那几项。**顺序就是菜单里的顺序**（唯一出处）。「强制结束」那一项在
    /// 宽限时间没到之前是灰的（见 `ServerProcess::forceKillAllowed()`）。
    enum class ServerMenuAction {
        Console = 0,   ///< 打开控制台
        Folder,        ///< 打开目录
        Settings,      ///< 服务端设置（server.properties / 备份 / 体检）
        Repair,        ///< 修复（重跑安装）
        ForceKill,     ///< 强制结束
        /// 异常退出后自动重启（**可勾选**的一项，见 `showServerContextMenu`）。放在
        /// 「强制结束」后面、「删除」前面：删除永远在最后（不可逆的那一项别夹在中间）。
        RestartOnCrash,
        Remove,        ///< 删掉这个服务端
    };
    static int serverMenuActionCount() { return 7; }
    static QString serverMenuText(ServerMenuAction action) {
        switch (action) {
        case ServerMenuAction::Console:
            return QStringLiteral("打开控制台");
        case ServerMenuAction::Folder:
            return QStringLiteral("打开目录");
        case ServerMenuAction::Settings:
            return QStringLiteral("服务端设置…");
        case ServerMenuAction::Repair:
            return QStringLiteral("修复（重新装一遍）");
        case ServerMenuAction::ForceKill:
            return QStringLiteral("强制结束");
        case ServerMenuAction::RestartOnCrash:
            return QStringLiteral("异常退出后自动重启");
        case ServerMenuAction::Remove:
            return QStringLiteral("删除这个服务端…");
        }
        return QString();
    }
    /// 「异常退出后自动重启」那一项的悬停说明（勾没勾都摆同一句）。
    static QString serverRestartMenuNote() {
        return QStringLiteral("连着崩几次就收手（默认 3 次，每次之间等一会儿）。\n"
                              "你自己按「停止」或者它正常退出都不会被拉起来。");
    }

    // ---------- 服务端控制台那一页（coding plan §6.2 / M4-3）----------
    //
    // 整页（`OverlayHost::execPage()`）—— 日志要多宽有多宽，底下留一行输入框。
    // 它和 `NewServerPage` 同一条规矩：**只摆控件、判据一条都不在这儿**（`ui/` 不 include
    // `src/`），"这是哪一档状态""能不能强制结束"全由 `MainWindow` 现算现喂。
    static QString serverConsolePageName() { return QStringLiteral("serverConsolePage"); }
    static QString serverConsoleTitleName() { return QStringLiteral("serverConsoleTitle"); }
    static QString serverConsoleStatusName() { return QStringLiteral("serverConsoleStatus"); }
    static QString serverConsoleLogName() { return QStringLiteral("serverConsoleLog"); }
    static QString serverConsoleCommandEditName() {
        return QStringLiteral("serverConsoleCommandEdit");
    }
    static QString serverConsoleSendButtonName() {
        return QStringLiteral("serverConsoleSend");
    }
    static QString serverConsoleStopButtonName() {
        return QStringLiteral("serverConsoleStop");
    }
    static QString serverConsoleForceKillButtonName() {
        return QStringLiteral("serverConsoleForceKill");
    }
    static QString serverConsoleSendText() { return QStringLiteral("发送"); }
    static QString serverConsoleStopText() { return QStringLiteral("停止服务端"); }
    static QString serverConsoleForceKillText() { return QStringLiteral("强制结束"); }

    // ---------- 服务端设置那一页（coding plan §6.3 第二阶段 / M4-4）----------
    //
    // 整页（`OverlayHost::execPage()`）：左栏四页（常用设置 / 原始文本 / 世界备份 / 体检），
    // 右边一个堆叠页，底下一条保存带。它和 `NewServerPage` / `ServerConsolePage` 同一条
    // 规矩：**只摆控件、判据一条都不在 ui/ 里**（哪几个字段、哪些值合法、该报哪几条，全由
    // `MainWindow` 从那几张表翻好喂进来）。
    //
    // ★ 这一页手里**始终有一份原文**（`rawText()`）—— 它是唯一会被写回盘上的东西，表单里
    //   改过的那几项是叠在它上面的补丁。两种编辑方式（表单 / 原始文本）怎么不打起来，见
    //   `ServerSettingsPage.hpp` 文件头那段。
    static QString serverSettingsPageName() { return QStringLiteral("serverSettingsPage"); }
    static QString serverSettingsTitleName() { return QStringLiteral("serverSettingsTitle"); }
    static QString serverSettingsStatusName() { return QStringLiteral("serverSettingsStatus"); }
    /// 左栏那一列（四页）。
    static QString serverSettingsSectionListName() {
        return QStringLiteral("serverSettingsSections");
    }
    static QString serverSettingsStackName() { return QStringLiteral("serverSettingsStack"); }
    /// 「常用设置」那一页里那张表单（每一格的名字见 `serverSettingsFieldName()`）。
    static QString serverSettingsFormName() { return QStringLiteral("serverSettingsForm"); }
    /// 「服务器简介」那一格下面那块预览（§9.7「MOTD 提供格式化预览」）。
    static QString serverSettingsMotdPreviewName() {
        return QStringLiteral("serverSettingsMotdPreview");
    }
    /// 预览底下那行小字。**它和预览同进同退**（`FieldView::preview` 为真才有）。
    static QString serverSettingsMotdNoteName() {
        return QStringLiteral("serverSettingsMotdNote");
    }
    /// 预览底下那行小字说的话：**只画两行**，别让用户以为第三行也会出现。
    static QString serverSettingsMotdNote() {
        return QStringLiteral("游戏里大概长这样：服务器列表只画前两行，颜色和粗斜体都照 § 码来。");
    }
    /// 简介是空的时候预览盘上摆什么（不摆空白 —— 空白看起来像"预览坏了"）。
    static QString serverSettingsMotdEmptyText() {
        return QStringLiteral("（空的 —— 服务器列表那一行会是空白）");
    }
    /// 「资源包 SHA-1」那一格旁边那颗「选择本地 zip…」（§9.7）。
    static QString serverSettingsPickZipButtonName() {
        return QStringLiteral("serverSettingsPickZip");
    }
    static QString serverSettingsPickZipText() { return QStringLiteral("选择本地 zip…"); }
    static QString serverSettingsRawEditName() { return QStringLiteral("serverSettingsRawText"); }
    static QString serverSettingsRawNoteName() { return QStringLiteral("serverSettingsRawNote"); }
    static QString serverSettingsBackupNoteName() {
        return QStringLiteral("serverSettingsBackupNote");
    }
    static QString serverSettingsBackupListName() {
        return QStringLiteral("serverSettingsBackups");
    }
    static QString serverSettingsCheckListName() {
        return QStringLiteral("serverSettingsFindings");
    }
    // ---------- 玩家名单（§9.7「玩家管理」）----------
    static QString serverSettingsPlayerNoteName() {
        return QStringLiteral("serverSettingsPlayerNote");
    }
    /// 那四份名单的下拉框。
    static QString serverSettingsPlayerComboName() {
        return QStringLiteral("serverSettingsPlayerList");
    }
    /// 名单本身那张表。
    static QString serverSettingsPlayerTreeName() {
        return QStringLiteral("serverSettingsPlayerEntries");
    }
    static QString serverSettingsPlayerAddButtonName() {
        return QStringLiteral("serverSettingsPlayerAdd");
    }
    static QString serverSettingsPlayerRemoveButtonName() {
        return QStringLiteral("serverSettingsPlayerRemove");
    }
    static QString serverSettingsCheckSummaryName() {
        return QStringLiteral("serverSettingsCheckSummary");
    }
    /// 体检那一条的出路按钮摆在这一格里面（按记号新建 / 复用）。
    static QString serverSettingsCheckActionsName() {
        return QStringLiteral("serverSettingsCheckActions");
    }
    static QString serverSettingsRevertButtonName() {
        return QStringLiteral("serverSettingsRevert");
    }
    static QString serverSettingsSaveButtonName() { return QStringLiteral("serverSettingsSave"); }
    static QString serverSettingsSaveNoteName() { return QStringLiteral("serverSettingsSaveNote"); }
    static QString serverSettingsBackupNowButtonName() {
        return QStringLiteral("serverSettingsBackupNow");
    }
    static QString serverSettingsRestoreButtonName() {
        return QStringLiteral("serverSettingsRestore");
    }
    static QString serverSettingsDeleteBackupButtonName() {
        return QStringLiteral("serverSettingsDeleteBackup");
    }
    static QString serverSettingsOpenBackupFolderButtonName() {
        return QStringLiteral("serverSettingsOpenBackups");
    }
    static QString serverSettingsRecheckButtonName() {
        return QStringLiteral("serverSettingsRecheck");
    }

    /// 表单里某一格的控件叫什么（**认人走它**）。前缀 + 键：`serverSettingsField_server-port`。
    /// ★ 键是 `server.properties` 里那个词，所以我们这边加一项、验证工程不用改一个字。
    static QString serverSettingsFieldName(const QString &key) {
        return QStringLiteral("serverSettingsField_") + key;
    }

    /// 左栏那六页的标题。**顺序就是左栏的顺序**（`ServerSettingsPage::Section` 按下标对齐）。
    /// 「内容」那一页是 §9.7 的内容管理（模组 / 数据包）。
    static int serverSettingsSectionCount() { return 6; }
    static QString serverSettingsSectionTitle(int index) {
        switch (index) {
        case 0:
            return QStringLiteral("常用设置");
        case 1:
            return QStringLiteral("原始文本");
        case 2:
            return QStringLiteral("内容");
        case 3:
            return QStringLiteral("世界备份");
        case 4:
            return QStringLiteral("玩家");
        case 5:
            return QStringLiteral("体检");
        }
        return QString();
    }

    static QString serverSettingsSaveText() { return QStringLiteral("保存"); }
    static QString serverSettingsRevertText() { return QStringLiteral("放弃改动"); }
    static QString serverSettingsBackupNowText() { return QStringLiteral("立即备份"); }
    static QString serverSettingsRestoreText() { return QStringLiteral("从这一份恢复…"); }
    static QString serverSettingsDeleteBackupText() { return QStringLiteral("删掉这一份…"); }
    static QString serverSettingsOpenBackupFolderText() {
        return QStringLiteral("打开备份文件夹");
    }
    static QString serverSettingsRecheckText() { return QStringLiteral("重新检查"); }

    /// 保存带左边那行小字。**唯一出处**（`ServerSettingsPage::setSaveState()` 收的就是它
    /// 拼好的整句）。五个状态各一句：表单脏 / 文本脏 / 两边都脏 / 干净的 / 刚存过。
    ///
    /// NOTE: "在跑所以改不了"那一句不在这一组里（`serverSettingsRunningNote()`）—— 那个是
    ///       "整页只读"的原因，不是保存带自己的状态。
    static QString serverSettingsDirtyFieldsNote(int count) {
        return QStringLiteral("改过 %1 项 —— 保存之后写进 server.properties，别的行不动。")
            .arg(count);
    }
    static QString serverSettingsDirtyRawNote() {
        return QStringLiteral("原始文本动过 —— 保存之后整份写回去。");
    }
    static QString serverSettingsDirtyBothNote(int count) {
        return QStringLiteral("改过 %1 项，原始文本也动过 —— 保存时合成一份写回去。").arg(count);
    }
    static QString serverSettingsCleanNote() { return QStringLiteral("没改过什么。"); }
    static QString serverSettingsSavedNote() { return QStringLiteral("已经保存好了。"); }
    /// 服务端在跑时整页只读的那句原因（`setEditable(false, …)` 收的就是它）。
    static QString serverSettingsRunningNote() {
        return QStringLiteral("服务端正在跑，停下来才改得动 —— 它退出时会把自己内存里那份写回"
                              "盘上，现在改的会被盖掉。");
    }
    /// 顶上状态那行字的尾巴（在跑时挂上去）。
    static QString serverSettingsRunningStatusSuffix() {
        return QStringLiteral("（正在跑 —— 先停下来才能改）");
    }

    /// 「原始文本」那一页顶上那句（这一页不重排、不整理的说明）。
    static QString serverSettingsRawNoteText() {
        return QStringLiteral(
            "这里就是 server.properties 全文，原样摆出来：注释、你没见过的那些键、"
            "键的顺序、行尾都按盘上那一份。\n"
            "在「常用设置」里改过的项，切到这一页时会先合进来 —— 保存写回去的就是这一份。");
    }

    /// 备份那一列的三格。**唯一出处**（表的列数、每列标题都读它）。
    static QStringList serverSettingsBackupColumns() {
        return QStringList{QStringLiteral("时间"), QStringLiteral("大小"),
                           QStringLiteral("这一份是什么")};
    }
    /// 体检那一列的两格：左边那个词（`ServerCheck::severityName()`）右边那一句。
    static QStringList serverSettingsCheckColumns() {
        return QStringList{QStringLiteral("这一条"), QStringLiteral("说的是什么")};
    }

    // ---------- 内容（§9.7 内容管理）----------
    //
    // 两类：模组（服务端目录的 `mods/`）和数据包（世界目录的 `datapacks/`）。插件（Paper /
    // Spigot）等服务器类型稳定之后再单独开——这一版不做（见 `ServerContent.hpp` 文件头）。
    static QString serverSettingsContentNoteName() {
        return QStringLiteral("serverSettingsContentNote");
    }
    static QString serverSettingsContentComboName() {
        return QStringLiteral("serverSettingsContentKind");
    }
    static QString serverSettingsContentTreeName() {
        return QStringLiteral("serverSettingsContentEntries");
    }
    static QString serverSettingsContentAddButtonName() {
        return QStringLiteral("serverSettingsContentAdd");
    }
    static QString serverSettingsContentToggleButtonName() {
        return QStringLiteral("serverSettingsContentToggle");
    }
    static QString serverSettingsContentOpenFolderButtonName() {
        return QStringLiteral("serverSettingsContentOpenFolder");
    }
    static QString serverSettingsContentRefreshButtonName() {
        return QStringLiteral("serverSettingsContentRefresh");
    }
    /// 那两类。**顺序就是下拉框的顺序**（`ServerContent::Kind` 按下标对齐）。
    static int serverSettingsContentKindCount() { return 2; }
    static QString serverSettingsContentKindTitle(int index) {
        switch (index) {
        case 0:
            return QStringLiteral("模组");
        case 1:
            return QStringLiteral("数据包");
        }
        return QString();
    }
    /// 内容那张表的三格。
    static QStringList serverSettingsContentColumns() {
        return QStringList{QStringLiteral("名字"), QStringLiteral("大小"),
                           QStringLiteral("状态")};
    }
    /// 状态那一格：禁用着的那一行说得明白一点（它还在文件夹里，只是不加载）。
    static QString serverSettingsContentStateText(bool disabled) {
        return disabled ? QStringLiteral("已禁用（不加载）") : QStringLiteral("启用中");
    }
    /// 这一页顶上那句说明（哪个文件夹、禁用是什么意思、在跑时为什么改了也不生效）。
    static QString serverSettingsContentNoteText(int kind, const QString &folder, bool running) {
        QString text = kind == 1
                           ? QStringLiteral("这一页读写世界目录下的 datapacks 文件夹（%1）。")
                                 .arg(folder)
                           : QStringLiteral("这一页读写服务端目录下的 mods 文件夹（%1）。")
                                 .arg(folder);
        text += QStringLiteral("\n禁用只是把后缀改成 .disabled —— 文件还在，内容一个字节都不动，"
                               "想恢复就再点一次「启用」。");
        if (running) {
            text += QStringLiteral("\n服务端正在跑：现在改它也不会加载，重启服务端之后才生效。");
        }
        return text;
    }
    static QString serverSettingsContentEmptyText() { return QStringLiteral("这个文件夹还是空的。"); }
    static QString serverSettingsContentAddText() { return QStringLiteral("添加…"); }
    static QString serverSettingsContentEnableText() { return QStringLiteral("启用"); }
    static QString serverSettingsContentDisableText() { return QStringLiteral("禁用"); }
    static QString serverSettingsContentOpenFolderText() { return QStringLiteral("打开文件夹"); }
    static QString serverSettingsContentRefreshText() { return QStringLiteral("刷新"); }

    // ---------- 玩家名单 ----------
    /// 那四份名单。**顺序就是下拉框的顺序**（`ServerPlayers::List` 按下标对齐）。
    static int serverSettingsPlayerListCount() { return 4; }
    static QString serverSettingsPlayerListTitle(int index) {
        switch (index) {
        case 0:
            return QStringLiteral("白名单");
        case 1:
            return QStringLiteral("管理员");
        case 2:
            return QStringLiteral("封禁的玩家");
        case 3:
            return QStringLiteral("封禁的 IP");
        }
        return QString();
    }
    /// 名单那张表的三格。
    static QStringList serverSettingsPlayerColumns() {
        return QStringList{QStringLiteral("玩家"), QStringLiteral("身份"),
                           QStringLiteral("说明")};
    }
    /// 「加一个…」那颗按钮上的字（封禁 IP 那一档问的是地址，不是名字）。
    static QString serverSettingsPlayerAddText(int index) {
        return index == 3 ? QStringLiteral("封禁一个地址…") : QStringLiteral("加一个人…");
    }
    static QString serverSettingsPlayerRemoveText() { return QStringLiteral("移除…"); }
    static QString serverSettingsPlayerKickButtonName() {
        return QStringLiteral("serverSettingsPlayerKick");
    }
    /// 「踢出…」：只对**正在跑**的服务端有意义，而且必须走控制台（§9.7 那句）。
    static QString serverSettingsPlayerKickText() { return QStringLiteral("踢出…"); }
    /// 这一页顶上那句说明（哪个文件、在跑时为什么改不了）。
    static QString serverSettingsPlayerNoteText(int index, bool running) {
        const QString file = index == 0 ? QStringLiteral("whitelist.json")
                            : index == 1 ? QStringLiteral("ops.json")
                            : index == 2 ? QStringLiteral("banned-players.json")
                                         : QStringLiteral("banned-ips.json");
        QString text = QStringLiteral("这一页读写服务端目录里的 %1。").arg(file);
        if (index == 0) {
            text += QStringLiteral("白名单开不开在「常用设置」里的「启用白名单」那一格。");
        } else if (index == 1) {
            text += QStringLiteral("这一份里的人是管理员；等级 4 = 全部权限。");
        } else if (index == 2 || index == 3) {
            text += QStringLiteral("过期的条目服务端下次启动时会自己清掉。");
        }
        if (running) {
            text += QStringLiteral("\n服务端正在跑 —— 它退出时会把内存里那份名单写回来，"
                                   "所以这一页现在改不了。")
                    + QStringLiteral("\n「踢出…」不一样：它把一条 kick 命令送进正在跑的服务端，"
                                     "结果会回显在控制台里。");
        }
        return text;
    }

    /**
     * @brief 体检单上某一条的出路，那颗按钮上写什么。
     *
     * @param token `ServerCheck::actionToken()` 那个记号（**认人认记号，不认这行字**）。
     *        认不出来的记号回空串 —— 界面照旧把这一条摆出来，只是不配按钮（比编一个
     *        "按一下就好了"出来强）。
     */
    static QString serverCheckActionText(const QString &token) {
        if (token == QStringLiteral("reinstall")) {
            return QStringLiteral("修复（重新装一遍）");
        }
        if (token == QStringLiteral("accept-eula")) {
            return QStringLiteral("同意 EULA…");
        }
        if (token == QStringLiteral("fix-values")) {
            return QStringLiteral("改回缺省值");
        }
        if (token == QStringLiteral("open-settings")) {
            return QStringLiteral("去「常用设置」");
        }
        if (token == QStringLiteral("open-backups")) {
            return QStringLiteral("去「世界备份」");
        }
        return QString();
    }

    static QString tabWidgetName() { return QStringLiteral("mainTabWidget"); }
    static QString homeTabName() { return QStringLiteral("homeTab"); }
    static QString instanceTabName() { return QStringLiteral("instanceTab"); }
    static QString downloadTabName() { return QStringLiteral("downloadTab"); }
    static QString settingsTabName() { return QStringLiteral("settingsTab"); }
    static QString skinViewerName() { return SkinViewer::viewerName(); }
    static QString skinHintLabelName() { return QStringLiteral("skinHintLabel"); }
    static QString accountSkinButtonName() { return QStringLiteral("accountSkinButton"); }
    /// 那个入口按钮上写的字。和其它文案一样做成静态访问器 —— 验证工程引它，
    /// 产品里改一个字就不用两边各写一遍字面量。
    static QString accountSkinButtonText() { return QStringLiteral("账号与外观…"); }

    static QString homeFavoriteCountSpinName() { return QStringLiteral("homeFavoriteCountSpin"); }
    static QString homeRecentCountSpinName() { return QStringLiteral("homeRecentCountSpin"); }
    static QString homeInstanceSelectButtonName() {
        return QStringLiteral("homeInstanceSelectButton");
    }
    static QString homeInstancePickerName() { return QStringLiteral("homeInstancePicker"); }

    enum class HomeQuickAction { Launch, Edit, ChooseInstance, OpenFolder };
    enum class HomeQuickMode { Current, Recent, Favorite };
    struct HomeQuickItem {
        QString key;
        QString name;
        QString detail;
        QIcon icon;
        /// 固定槽位还没有实例时为 true。空槽仍参与布局，但不会伪装成可启动实例。
        bool empty = false;
        bool current = false;
        bool recent = false;
        bool favorite = false;
    };
    using HomeQuickActionHandler =
        std::function<void(const QString &key, HomeQuickAction action)>;
    void setHomeQuickRows(const QList<HomeQuickItem> &current,
                          const QList<HomeQuickItem> &favorites,
                          const QList<HomeQuickItem> &recent);
    /// 旧验证和旧调用点仍按“收藏 / 最近”两排传参；保留这个适配入口，避免它们为了布局迁移
    /// 误把第一份列表当成“当前”。产品代码统一走三参版本。
    void setHomeQuickRows(const QList<HomeQuickItem> &favorites,
                          const QList<HomeQuickItem> &recent) {
        setHomeQuickRows({}, favorites, recent);
    }
    void setHomeQuickActionHandler(const HomeQuickActionHandler &handler) {
        m_homeQuickActionHandler = handler;
    }
    HomeQuickMode homeQuickMode() const { return m_homeQuickMode; }
    void setHomeQuickMode(HomeQuickMode mode);
    struct HomePickerFolder {
        QString path;
        QString title;
        QString subtitle;
        bool current = false;
    };
    struct HomePickerInstance {
        QString folderPath;
        QString id;
        QString title;
        QString subtitle;
        QString loaderKey;
        QIcon icon;
        bool favorite = false;
        bool recent = false;
        QString searchKeywords = {};
    };
    using HomePickerFolderHandler = std::function<void(const QString &path)>;
    using HomePickerInstanceHandler =
        std::function<void(const QString &folderPath, const QString &instanceId)>;
    void openHomeInstancePicker(const QList<HomePickerFolder> &folders,
                                const QList<HomePickerInstance> &instances,
                                const HomePickerFolderHandler &folderHandler,
                                const HomePickerInstanceHandler &instanceHandler);
    void closeHomeInstancePicker();
    bool homeInstancePickerOpen() const;
    static QString bottomBarName() { return QStringLiteral("bottomBar"); }
    static QString avatarLabelName() { return QStringLiteral("avatarLabel"); }
    /// 底栏头像那一块的边长（像素）。**唯一出处**：主窗口拿它去裁头像
    /// （SkinModel::headAvatar() 出来必须是这个尺寸，否则图会被 QLabel 缩放）、
    /// 验证工程拿它断言 pixmap 的大小。三处各写一个数字迟早对不上。
    static int avatarSize() { return 32; }
    static QString accountLabelName() { return QStringLiteral("accountLabel"); }
    /// 底栏账户格**第二行**：这次启动用的是哪种身份。
    static QString accountTypeLabelName() { return QStringLiteral("accountTypeLabel"); }
    /// 第二行那三种说法。
    ///
    /// NOTE: **三种而不是两种** —— 没勾离线、又没登录的时候写「离线登录」是骗人：
    ///       那种状态点下去会被"请先登录"挡回来（见 MainWindow::onStartButtonClicked()
    ///       里 token.isEmpty() 那条分支）。照实说「未登录」，用户才知道该去登录。
    static QString accountTypeOnlineText() { return QStringLiteral("正版登录"); }
    static QString accountTypeOfflineText() { return QStringLiteral("离线登录"); }
    static QString accountTypeNotLoggedInText() { return QStringLiteral("未登录"); }
    /// 底栏**左边那颗按钮（实例运行状态）的固定宽度**（像素）。中间那格（进度）要稳定，
    /// 就只能让旁边这几格不再跟着自己的文字长短变 —— 拉伸因子管的是「富余宽度怎么分」，
    /// 管不了「谁的最小宽度更大」，所以光靠 0/4/0 那份比例挡不住左边那几行字
    /// 把进度条挤短。
    ///
    /// NOTE: **多开之后左边那格的内容是真的会变长的**：第一行从「实例运行中」变成
    ///       「3 个实例运行中」、第二行从实例名变成「A（账号甲）、B（账号乙）」；
    ///       一变中间那根条就跟着长一截短一截 —— 用户报的「条会变长变短」就是这个。
    ///       超出去的字由那两行自己截成省略号（QLabel 没有 elide API，硬裁会把最后一个字
    ///       切成半个，所以那两行用的是 MainWindowUI.cpp 里那个 ElidedLabel），
    ///       文字能占多宽由 bottomBarRunStateTextWidth() 说了算。
    ///       **左边这格钉死是安全的**：它贴着窗口左边缘，多出来的空档落在文字右边。
    ///       另外，钉死的那个是**装字的按钮**（不是那两行字本身）：那两行有藏起来的时候
    ///       （没有实例在跑时），而布局里藏起来的控件宽度是 0 —— 钉在字上等于没钉
    ///       （实测条会从 582 跳到 440）。详见 MainWindowUI.cpp 里 runStateButton 那段。
    ///
    /// 宽度**从 200 改到 170**（2026-09-23）：两头那两格加起来原来是 458 px，
    /// 900 px 的窗口里中间那根条只剩 262 px（条还没那两格宽），用户报「奇小无比」。
    /// 那一格装的是「3 个实例运行中」+ 一行实例名 + 箭头，170 够（文字能占 152）。
    static int bottomBarRunStateColumnWidth() { return 170; }
    /// 左边那两行字**各自留给字**的宽度上限 = 那一格的宽度减掉**最左边那颗状态方块占掉的
    /// 一段**，再减掉右边那个箭头和它左边的间距。封这个顶，布局就永远不会被长名字撑开。
    ///
    /// NOTE: 方块那一段（`bottomBarRunStateMarkWidth()`）**必须一起减掉**（2026-09-24 补的）：
    ///       方块和第一行文字在同一行里，不减的话那一行的需要宽度变成
    ///       16 + 152 = 168 > 152，方块要么被压掉、要么把这一格撑出 170 之外。
    ///
    /// ★ 这个数说的是"**留给字的**宽度"，两行都是它 —— 但两行**控件本身的**上限不是同一个：
    ///   第一行那个方块是布局里单独的控件（占的是这个上限**之外**的位置，所以这 136 全是给
    ///   字的），第二行的缩进是 `contentsMargins`（那 16 px 从上限**里面**扣）。所以第二行
    ///   那个 QLabel 的上限要写 `bottomBarRunStateTextWidth() + bottomBarRunStateMarkWidth()`，
    ///   两行留给字的宽度才真的都是 136（2026-09-26 修：原来两行都写这个数，第二行实际只有
    ///   120，右边短一截）。
    static int bottomBarRunStateTextWidth() {
        return bottomBarRunStateColumnWidth() - bottomBarChevronSpacing() - bottomBarChevronSize() -
               bottomBarRunStateMarkWidth();
    }
    /// 底栏**右边那颗按钮（头像 + 用户名 + 箭头）的固定宽度**（像素）。右边那颗和左边的
    /// 一样**要钉死**：进度条那一边彻底不再受"用户名多长"的影响（以前只给用户名封了个顶，
    /// 换账号时这一格仍会变 200 px）。
    ///
    /// NOTE: 按钮里的那一组是**贴着按钮左边缘**摆的（头像一个格、名字一个格、箭头钉在
    ///       右边缘，和左边那颗按钮一个形状）。**不是靠右摆** —— 靠右摆会在头像左边空出
    ///       一大段（实测 115 px），用户 2026-09-23 报的就是它。
    static int bottomBarAccountButtonWidth() {
        return avatarSize() + bottomBarAccountRowSpacing() + bottomBarAccountColumnWidth() +
               bottomBarChevronSpacing() + bottomBarChevronSize();
    }
    /// 底栏那颗账号按钮里**用户名那一格的宽度上限**（像素）：16 个字符的 ID 按当前字体
    /// 大约就是这么宽，再长的名字截成省略号。
    ///
    /// 宽度**从 200 改到 150**（2026-09-23，和左边那格一起收）：右上角那格原来跟左边
    /// 一样宽（200），可它装的是"32 的头像 + 名字 + 箭头"，200 是上限、不是需要 ——
    /// 16 个字符的正版名在 150 里放得下（放不下的名字截成省略号，它用的是 ElidedLabel）。
    static int bottomBarAccountColumnWidth() { return 150; }
    /// 底栏**中间那格**进度文字（`已下载 d / t  文件名`）的宽度上限（像素）。
    ///
    /// 2026-09-24 补的：那一格原来**只有最小宽度、没有上限**（见 MainWindowUI.cpp 里
    /// statusLabel 那几行）。它顶宽窗口走的是 **sizeHint** 那条路 —— 那个 ElidedLabel 的
    /// sizeHint() 就是拿 maximumWidth() 当上限的（见 MainWindowUI.cpp 里那个类）。实测
    /// （ui_check 的 E5 段）：300 个字符的文件名，不设上限时这一格 sizeHint 3912、
    /// 底栏 4338、**主窗口 4338**（默认窗口只有 900×620）；设了上限是 320 / 746 / 818。
    /// 任何走 sizeHint 的路径（adjustSize()、平台给初次尺寸、以后哪天加的"记住窗口大小"）
    /// 都会因此拿到一个四倍宽的窗口。
    ///
    /// NOTE: 报告里"整窗**最小宽度**被顶到 900+、窗口缩不回去"那半句**不成立**，
    ///       别照着它改：那一格还有 setMinimumWidth(kStatusLabelMinWidth)，而
    ///       qSmartMinSize() 在 `minSize.width() > 0` 时直接把最小尺寸**钉成**
    ///       minSize.width()（qlayoutengine.cpp:331）—— 文字再长，这一格的最小贡献都是
    ///       260，整窗最小宽度实测两态都是 686。填 maxWidth 之前的 `d->sh`/`sizeForWidth`
    ///       那套（QLabel 的 minimumSizeHint 确实是整句宽度）在布局那一层被那一句挡住了。
    ///
    /// 取值按"900 px 的窗口里中间那格大致能有多少"：左右两格（170 + 头像那格）和两头的
    /// 边距扣掉之后剩下的就是它，取 320 让长文件名一定截得住，同时短句不会被截。
    static int bottomBarStatusTextWidth() { return 320; }

    /// 底栏两颗按钮里**头像和文字之间的间距**（和原来那格一样是 8）。
    static int bottomBarAccountRowSpacing() { return 8; }
    /// 底栏两颗按钮右边那个**朝上箭头**的边长（像素）：点开是向上弹的，箭头就朝上。
    static int bottomBarChevronSize() { return 12; }
    /// 箭头和它左边那些字之间的间距（像素）。
    static int bottomBarChevronSpacing() { return 6; }
    /// 左下那一格（**能点开的时候**）鼠标停上去显示的那句话。
    ///
    /// NOTE: 这里是它的**唯一出处**：装控件的时候挂一次，点不开的时候要把它摘掉
    ///       （见 setRunPanelAvailable()），两处抄同一句话迟早会对不上。
    static QString bottomBarRunPanelTip() { return QStringLiteral("点一下向上展开实例列表"); }

    static QString runStateButtonName() { return QStringLiteral("runStateButton"); }
    static QString accountButtonName() { return QStringLiteral("accountButton"); }
    static QString toolboxButtonName() { return QStringLiteral("toolboxButton"); }
    static QString toolboxPanelName() { return QStringLiteral("toolboxPanel"); }

    // ---------- 上拉面板里"一行长什么样" ----------
    //
    // 面板**自己不去问数据**（它连 LauncherManager 都认不得，ui/ 不许 include src/）：
    // MainWindow 每次点开时把"现在有几个实例在跑 / 最近用过哪三个账号"翻成下面这两层
    // 结构递给它（setBottomBarPanelContent()），回来的是"第几行第几个动作被点了"。
    // 于是这里只管画：图标 + 一列字 + 行尾几个按钮，怎么弹、弹在哪，也都是这里的事。
    /// 行尾的一个动作按钮（左面板是电源/齿轮两颗图标，右面板是「选择」「登录」「登出」一个词）。
    /// 图标和文字**二选一**：给了 icon 就画图标，否则画 text。
    struct PanelAction {
        QPixmap icon;
        QString text;
        /// 鼠标停上去显示的那句话（图标按钮**必须**有 —— 一个电源符号说明不了
        /// "点了会把游戏杀掉"，文字按钮可省）。
        QString tooltip;
        /// false = 灰掉点不动。**现在右面板里没有哪一颗走这一档**（当前账号那一行原先
        /// 灰着，2026-09-24 换成了可点的「登出」）；留着是给将来"这一下现在做不了"
        /// 那种按钮用的 —— 灰掉必须是真灰（Qt 的 disabled 按钮根本不发 clicked）。
        bool enabled = true;
    };
    /// 面板里的一行。
    struct PanelRow {
        /// **这一行是谁**。面板不认识它，只在行尾按钮被点时原样还回来 —— 左面板放会话号、
        /// 右面板放账号 uuid。
        ///
        /// NOTE: 认身份得靠它，**不能靠"第几行"**：面板弹出来到某个按钮被点之间隔着用户
        ///       看那两眼的时间，这中间列表是会变的（一个实例跑完了、后台把账号凭据刷新了），
        ///       按行号回头去查就会"关掉另一个游戏 / 切到另一个账号"。
        QString key;
        /// 行首那个图标。可以给空的 —— 那一行就不留图标位（"什么都没有"那句话用的）。
        QPixmap icon;
        /// 主行文字（实例名 / 账号名）。
        QString title;
        /// 次行文字（已运行时长 / 账号类型）。空 = 这一行只有一行字。
        QString subtitle;
        /// 行尾的动作按钮，从左到右。
        QList<PanelAction> actions;
        /// true = 这行是"什么都没有"那句话：文字用次要色、行首没图标、行尾没按钮。
        /// **不是**给真行用的（真行有图标和按钮，灰掉某一颗走 PanelAction::enabled）。
        bool placeholder = false;
    };
    /// 「点开面板时去哪要这几行」和「行里的按钮被点了」。那个 bool：true = 右边那块
    /// （账号），false = 左边那块（实例）。动作回调里的 key 就是那一行的 PanelRow::key
    /// （**不是行号** —— 理由见 PanelRow::key 那段 NOTE）。
    using PanelRowsProvider = std::function<QList<PanelRow>(bool account)>;
    using PanelActionHandler = std::function<void(bool account, const QString &key, int action)>;
    /// 把上面那两个回调装进来。**由 MainWindow 在构造时调一次**（数据在它手里）；
    /// 没装的话面板就弹一块空的 —— 它不会崩，只是没内容。
    void setBottomBarPanelContent(PanelRowsProvider provider, PanelActionHandler handler) {
        m_panelRowsProvider = std::move(provider);
        m_panelActionHandler = std::move(handler);
    }

    /// 左下那一格（实例列表）现在**能不能点开**。没有实例在跑时点不开、箭头也收起来
    /// —— 那种时候面板里只有"现在没有实例在运行"那一句话，一个可点的东西都没有。
    /// （用户 2026-09-25 原话：「没有运行中的实例时让那个实例栏点不开，箭头隐藏」）
    ///
    /// **由 MainWindow 每次重画底栏时给**（`updateBottomBar()`，判据是 `runningGames()`
    /// 空不空 —— 那也正是"左面板里会不会有行"的唯一出处）。界面这一头**不查数据**，只画。
    ///
    /// NOTE: 判据里**"正在启动"也算有** —— 那一段里会话已经在 `runningGames()` 里了
    ///       （见 MainWindow::updateBottomBar() 里三态那段注释），所以黄灯那一档点得开。
    ///
    /// NOTE: "点不开"是**收不到鼠标**（`WA_TransparentForMouseEvents`），**不是**
    ///       `setEnabled(false)`：那一格里「当前没有运行中的实例」是给人看的状态，按钮一
    ///       灰，装在它里面的那几行字会跟着进 Disabled 调色板一起变暗 —— 用户要的只是
    ///       那一下点不动，不是把那句话弄糊。连带的：鼠标指针也不会变手型，划过不再高亮。
    void setRunPanelAvailable(bool available);

    /// 把**任务中心**要画的东西装进来。**由 MainWindow 在构造时调一次**（任务账本在
    /// 它手里，见 src/tasks/TaskManager.hpp）；没装的话面板里只有一句"现在没有任务"。
    ///
    /// @param provider 面板打开/刷新时**现问一遍**（不缓存：任务随时会多一个、会完一个，
    ///        "上一次那几行"留着迟早会在界面上说假话 —— 和 `PanelRowsProvider` 一个路数）。
    /// @param onClear 「清除已完成」被点了该告诉谁（可以给空的 = 那就没有那颗按钮）。
    void setTaskCenterContent(TaskCenter::ItemProvider provider,
                              TaskCenter::ClearFinishedHandler onClear) {
        m_taskCenterProvider = std::move(provider);
        m_taskCenterClearHandler = std::move(onClear);
    }

    /// 任务中心面板里**某一行的那一行字点了**要告诉谁（参数是那一行的 `key`，见
    /// `TaskCenterPanel::setRowActivatedHandler()`）。**由 MainWindow 在构造时调一次**。
    /// 没装的话那几行点了没反应（而且不会有手型光标，"看着能点"的假象都不会有）。
    void setTaskCenterRowActivatedHandler(const std::function<void(const QString &key)> &handler) {
        m_taskCenterRowActivated = handler;
    }

    /// 底栏那把扳手弹出来的面板上，**某一颗工具按钮被点了**要告诉谁。
    ///
    /// 参数是那件工具的键（`ToolboxPage::toolKey()`）。**这一层只管发这一句** —— 面板按下
    /// 哪一件、页面开在哪一颗 chip 上、算什么东西，全在 `ToolboxController` 那一边（用户
    /// 2026-10-16 原话：「不要再往 mainwindow 里堆超级屎山了，用 controller 等等方式合理
    /// 分散功能」）。没装 handler 的话那五颗按钮点了什么都不发生（而且不会崩）。
    void setToolboxToolHandler(const std::function<void(const QString &key)> &handler) {
        m_toolboxToolHandler = handler;
    }

    /// 没装的话行尾什么都不长。
    void setTaskCenterRowActionHandler(const TaskCenter::RowActionHandler &handler) {
        m_taskCenterRowAction = handler;
    }

    /// 任务中心那块面板开着的话，把里面的内容重画一遍（**没开着就什么都不做** ——
    /// 下一次 `popUp()` 自己会重建）。`MainWindow` 在"任务变了"之后喊一声。
    void refreshTaskCenterPanel();
    /// 那两颗按钮点开之后**向上**弹出来的那块面板。名字挂在这里是为了让验证工程
    /// 能按 objectName 把它找回来量几何（它是个 Qt::Popup 窗口，父子关系照旧）。
    static QString bottomBarPanelName() { return QStringLiteral("bottomBarPanel"); }
    /// 面板里第 @p index 行的 objectName（行、那一行的两行字、行尾第 @p action 个按钮）。
    static QString bottomBarPanelRowName(int index) {
        return QStringLiteral("bottomBarPanelRow%1").arg(index);
    }
    /// 行首那个图标（**行没有图标的时候这一格不存在** —— 比如"没有实例在运行"那句、
    /// 以及右面板那几行账号）。左面板每行的方块由 MainWindow 按那条实例的分类给。
    static QString bottomBarPanelRowIconName(int index) {
        return QStringLiteral("bottomBarPanelRowIcon%1").arg(index);
    }
    static QString bottomBarPanelRowTitleName(int index) {
        return QStringLiteral("bottomBarPanelRowTitle%1").arg(index);
    }
    static QString bottomBarPanelRowSubtitleName(int index) {
        return QStringLiteral("bottomBarPanelRowSubtitle%1").arg(index);
    }
    static QString bottomBarPanelRowActionName(int index, int action) {
        return QStringLiteral("bottomBarPanelRowAction%1_%2").arg(index).arg(action);
    }
    /// 右面板里那几行右边那颗按钮上的字。**离线账号写「选择」、正版账号写「登录」**
    /// （用户 2026-09-24 原话：「离线的右侧按钮写 选择，正版的右侧写 登录（链接令牌刷新）」）：
    /// 离线账号没有凭据，切过去就完事；正版账号切过去之后还要看一眼盘上那张令牌够不够用
    /// （<6 小时就顺手换一张，见 MainWindow）。
    static QString bottomBarPanelSelectText() { return QStringLiteral("选择"); }
    static QString bottomBarPanelLoginText() { return QStringLiteral("登录"); }
    /// 当前账号那一行：按钮写「登出」、**可点**。
    ///
    /// NOTE: 用户 2026-09-24 原话：「把那个账户管理里的当前改成登出」—— 原先是写「当前」
    ///       并把那颗按钮**灰掉**（点自己等于什么都不做）。
    ///
    /// NOTE: 登出**不删账号**：只清掉盘上的登录状态，那一项还留在列表里，点它那行的
    ///       「选择」/「登录」就回来了（见 AccountStore::logout()）。想彻底不要这个账号
    ///       走「账号与外观…」里的「删除所选」。
    static QString bottomBarPanelLogoutText() { return QStringLiteral("登出"); }
    /// 登出那颗按钮的悬停提示。文字按钮本可以不写 —— 但这一颗要说清"账号还在不在"，
    /// 不然用户不敢点（"登出"听起来像是要把账号弄没）。
    static QString bottomBarPanelLogoutTip() {
        return QStringLiteral("退出登录：清掉盘上的登录状态，这个账号还留在列表里");
    }
    /// 右面板里那一行**第二句**说的"这是个什么账号"。
    ///
    /// NOTE: 和底栏那两句（accountTypeOnlineText() = 「正版登录」）**不是一回事**：底栏说的是
    ///       "这一趟怎么启动"，这里说的是"这个账号是哪一种"。两句混用的话，面板上会出现
    ///       "正版登录"——那读起来像"这个账号现在是登录状态"，而这一行恰恰是**还没登录**
    ///       的那个账号。
    static QString bottomBarPanelOnlineAccountText() { return QStringLiteral("正版账号"); }
    static QString bottomBarPanelOfflineAccountText() { return QStringLiteral("离线账号"); }
    /// 左面板里"一份实例都没在跑"时那两句话。
    static QString bottomBarPanelNoRunningTitle() { return QStringLiteral("现在没有实例在运行"); }
    static QString bottomBarPanelNoRunningNote() {
        return QStringLiteral("从主页点「启动游戏」跑一个");
    }
    /// 右面板里"一个账号都没保存"时那两句话（不说清"为什么是空的"，用户只能去别处瞎找）。
    static QString bottomBarPanelNoAccountTitle() { return QStringLiteral("还没有保存的账号"); }
    static QString bottomBarPanelNoAccountNote() {
        return QStringLiteral("在「账号与外观…」里登录或添加离线账号");
    }
    /// 左面板**运行中那一行**最后那颗按钮上的字（2026-10-14 加，coding plan §3.3
    /// 「运行中实例弹层优先展示实例名、运行时长和『打开日志/停止』等可用动作」）。
    ///
    /// ★ 这一颗**故意是字的、不是图标**（和右面板那颗「选择」同一个写法）：另外两颗
    ///   （电源 / 齿轮）是"画个符号全世界都认得"的那一类，而"日志"在 `bottomBarPanelActionIconSize()`
    ///   （14 px）那么大的格子里画不出一个一眼认得出的形状 —— 画三条横线读起来像菜单，
    ///   画个方框像文件。字最省事也最不含糊。
    ///
    /// NOTE: 摆在这一行的**最后**（`kRunningPanelLogAction` 那个编号）—— 前两颗是"对**这一份
    ///       会话**做什么"（把它关了 / 改它的设置），这一颗是"看它说了什么"，最不打紧的一下
    ///       摆在边上（和日志窗里那两颗导出键摆「关闭」左边是同一个道理）。
    static QString bottomBarPanelLogText() { return QStringLiteral("日志"); }
    /// 左面板里每行那句「跑了多久」（用户 2026-09-24 原话：「一行一个会话，有时长是好主意，
    /// 做进去」）。
    ///
    /// 精确到**分钟**是刻意的：面板是弹出来那一刻的一张快照，开着的这几秒里不会自己重画
    /// （见 MainWindowUI 里 BottomBarPanel 那段），写成"3 分 12 秒"过两秒就成假的了。
    ///
    /// NOTE: 放在这里（而不是 MainWindow 里当一个文件级函数）是为了让它**能被验**：
    ///       四档话术（不到 1 分钟 / N 分钟 / N 小时 M 分钟 / N 天 M 小时）各是一句用户
    ///       会看到的话，而"已经跑了 3 天"这一档在验证工程里没法靠真的等出来。文案和
    ///       算它那几行在一起，是本项目一贯的取法（见 skinHint*Text 那几句）。
    /// @param startedAtMs 那一份会话起来的时刻（LauncherManager::GameSession::startedAtMs）。
    /// @param nowMs 现在（画这一行那一刻）。
    static QString runningDurationText(qint64 startedAtMs, qint64 nowMs);
    /// 面板里那一列文字的宽度上限（像素）。**要封顶**：面板的宽度是"内容多宽就多宽"
    /// （popUp() 里 adjustSize()），长实例名会长到把面板顶出窗口 —— 超出去的部分由
    /// 那一行自己截成省略号（ElidedLabel）。
    static int bottomBarPanelTextWidth() { return 180; }
    /// 行首那个图标的边长（像素）。和底栏头像一个量级，比字大一号。
    static int bottomBarPanelIconSize() { return 24; }
    /// 行尾**图标按钮**的边长（像素）：里面装着下面那个图形，剩下的是点击范围 ——
    /// 点不中的按钮等于没有（22 减 14 剩下的 8 px 就是那一圈）。
    static int bottomBarPanelActionSize() { return 22; }
    /// 行尾图标按钮里那个图形的边长（像素）。**唯一出处**：MainWindow 按它去要字形
    /// （VersionIcons::power() / gear()），按钮再按图形的实际大小设 iconSize ——
    /// 两边各写一个数字的结果是"图标被拉变形"或者"按钮里空一圈"。
    static int bottomBarPanelActionIconSize() { return 14; }
    /// 那两颗按钮右边那个朝上箭头的 objectName。两颗**共用**这一个名字 ——
    /// findChild 只会拿到其中一颗，要两颗一起用 findChildren。
    static QString bottomBarChevronName() { return QStringLiteral("bottomBarChevron"); }
    static QString runStateLabelName() { return QStringLiteral("runStateLabel"); }
    static QString runStateDetailLabelName() { return QStringLiteral("runStateDetailLabel"); }
    static QString progressBarName() { return QStringLiteral("progressBar"); }
    /// 那根条右边那一格里的百分比（「42%」）。**名字是 2026-10-13 加的**（§3.3：条改成
    /// 自绘、不再自己画数，数挪到条右边这一格）。
    static QString taskPercentLabelName() { return QStringLiteral("taskPercentLabel"); }
    static QString statusLabelName() { return QStringLiteral("statusLabel"); }

    // ------------------------------------------------------------------
    // 底栏中间那格 = **任务摘要**（2026-10-12，coding plan §3.1）
    // ------------------------------------------------------------------
    // 用户要的形状（§3.1 的原图）：
    //
    //   ┌ 任务摘要 ───────────────────┐
    //   │ ↓ 安装 1.21.4 Fabric   62%  ∧ │
    //   │ ███████████░░░░░░░░          │
    //   └───────────────────────────────┘
    //
    // 这一格以前是「一句字 + 一根条」，三件事（启动 / 下载 / 同步等待）抢着往里写
    // （那套优先级原来叫 `bottomBarProgressOwner`）。现在这一格只说**一句话**：手上
    // 最该被看见的那一项任务 —— 谁说话算数由 `Tasks::collapsedSummary()` 说了算（那是
    // 账本层的纯函数，见 src/tasks/TaskSnapshot.hpp），这里只负责把结果画出来。
    // ★ 启动因此**不再抢走并覆盖下载**（§3.1 明写的那一条）：它俩是任务表里并列的两行，
    //   收起态挑一行展示而已 —— 旧那套"启动最大、借完要还"的优先级整个撤掉了。
    // ------------------------------------------------------------------

    /// 底栏中间那格的**全部内容**。`MainWindow` 每次重画底栏都摆一份进来。
    struct BottomBarTaskSummary {
        /// false = 一项都没有 ⇒ 画一句低强调度的占位话（§3.1：「无任务时不显示空进度条」）。
        bool hasTask = false;
        /// 保留完整状态，取消/中断不能被“非失败”误画成成功。
        TaskCenter::State state = TaskCenter::State::Queued;
        /// 用户**还没看过**的一次失败 ⇒ 标题前面挂一颗红点。
        bool unreadFailure = false;
        /// -1 = **测不出百分比** ⇒ 进度条画不确定态（不编一个数出来）。
        int percent = -1;
        /// 除了正显示的这一项之外，还有几项在跑 —— 右边那句「另有 2 项」的数就是它。
        int extraCount = 0;
        QString title;
    };

    /// 摆上底栏中间那一格**现在该说什么**。**由 MainWindow 每次重画底栏时给**
    /// （`updateBottomBar()`，谁说话算数由 `Tasks::collapsedSummary()` 定）。
    ///
    /// 界面这一头**不查数据**：它不知道有没有任务、更不知道哪一项该被看见 —— 那些都是
    /// 账本那边的事（这里是 `ui/`，看不到 `src/`，见文件头那段）。
    ///
    /// NOTE: 声明摆在这个结构**后面**（这个头里别的地方都是"函数在前、它用的类型在后"），
    ///       因为参数是**按值传**的 —— 按值用就得先看得见那个完整类型。别为了整齐把它挪到
    ///       上面去，挪了就编不过（"incomplete type" 那一类）。
    void setBottomBarTaskSummary(const BottomBarTaskSummary &summary);

    /// 一项任务都没有时那一格写什么。
    static QString taskSummaryNoTaskText() { return QStringLiteral("暂无后台任务"); }
    /// 还有几项没显示出来：「另有 N 项」。**N ≥ 1 才该被调到**（0 项是"没有"，不是"另有"）。
    static QString taskSummaryMoreText(int count) {
        return QCoreApplication::translate("MainWindowUI", "另有 %1 项").arg(count);
    }
    /// 已经收尾那一项标题后面缀的词。**收尾 = 过去式**：刚做完的一件事在收起态里还顶着
    /// 现在进行时的样子，看着像"它还在跑"。
    static QString taskSummaryDoneSuffix() {
        return QCoreApplication::translate("MainWindowUI", "（已完成）");
    }
    static QString taskSummaryFailedSuffix() {
        return QCoreApplication::translate("MainWindowUI", "（失败）");
    }
    static QString taskSummaryCancelledSuffix() {
        return QCoreApplication::translate("MainWindowUI", "（已取消）");
    }
    static QString taskSummaryInterruptedSuffix() {
        return QCoreApplication::translate("MainWindowUI", "（已中断）");
    }
    /// 收起态那一行**最终**写出来的字。**唯一出处**：MainWindow 拿它填、验证工程拿它断言。
    static QString taskSummaryText(const QString &title, TaskCenter::State state) {
        if (state == TaskCenter::State::Failed) {
            return title + taskSummaryFailedSuffix();
        }
        if (state == TaskCenter::State::Cancelled) {
            return title + taskSummaryCancelledSuffix();
        }
        if (state == TaskCenter::State::Interrupted) {
            return title + taskSummaryInterruptedSuffix();
        }
        return state == TaskCenter::State::Succeeded ? title + taskSummaryDoneSuffix() : title;
    }
    /// 悬停在那一格上显示什么（它现在是一整颗能点的按钮）。
    static QString taskSummaryTip() {
        return QCoreApplication::translate("MainWindowUI", "点一下展开任务中心");
    }
    /// 那一格右边小字（「另有 N 项」）的宽度上限（像素）—— `ElidedLabel` 的 sizeHint 就
    /// 封在这个数上，所以再长的句子也顶不宽中间那一格（E5 那一段量的就是这件事）。
    static int taskExtraTextWidth() { return 64; }
    /// 一件任务**完成之后**还能在收起态那格上露多久（毫秒）。§3.1：「完成任务短暂显示后
    /// 收进历史」—— "短暂"这个数就是它。
    ///
    /// NOTE: 它是**界面**这一层的数，不是账本那一层的：账本只知道"这件事什么时候收的尾"
    ///       （`TaskSnapshot::finishedAt`），"还该不该露着"是这一格自己算的（判据在
    ///       `Tasks::collapsedSummary()` 的 `recentWindowMs` 参数）。放在这里是因为这一格
    ///       的尺寸/时限都归这一节管，验证工程也只需要读这一处。
    static int taskSummaryRecentWindowMs() { return 8000; }

    static QString taskSummaryButtonName() { return QStringLiteral("taskSummaryButton"); }
    static QString taskExtraLabelName() { return QStringLiteral("taskExtraLabel"); }
    /// 那一颗"还没看过的一次失败"的红点（**收着的时候它也在**，只是布局里占 0 宽）。
    static QString taskSummaryDotName() { return QStringLiteral("taskSummaryDot"); }

    /// 任务中心那块面板（`ui/TaskCenterPanel.hpp`）。
    static QString taskCenterPanelName() { return QStringLiteral("taskCenterPanel"); }
    /// 面板宽度那一档（§3.2：约 440–520）。面板按内容 `adjustSize()` 之后夹在这两档里。
    static int taskCenterPanelMinWidth() { return 440; }
    static int taskCenterPanelMaxWidth() { return 520; }
    /// 面板高度上限 = 屏幕**可用**高度的百分之多少（§3.2：60%）。
    static int taskCenterPanelHeightPercent() { return 60; }
    /// 进度条那一根**细轨道**的高度（像素）。**三处共用的唯一出处**：任务中心面板里每一行、
    /// 底栏中间那格、任务详情页 —— 2026-10-13（§3.3 统一自绘组件）起三处都读它（原来名字只
    /// 提"面板"，详情页还各自写死 8；现在统一）。
    ///
    /// NOTE: 名字先没改（`ui_modernize.py` 的 `task_progress_height` 令牌按这个名字定位）。
    static int taskCenterPanelBarHeight() { return 4; }
    /// 步骤之间空多少（像素）。
    static int taskCenterPanelStepSpacing() { return 14; }
    /// 那一颗"还没看过的一次失败"的红点边长（像素）。比底栏那颗状态方块小一号：
    /// 它挨着的是一行正文，不是整格的角标。
    static int taskCenterPanelFailureDotSize() { return 8; }

    /// 面板里一行上面那几个词的**唯一出处**（面板画它、验证工程断言它）。
    static QString taskCenterPanelActiveSectionText() { return QStringLiteral("当前工作"); }
    static QString taskCenterPanelDoneSectionText() { return QStringLiteral("已完成"); }
    static QString taskCenterPanelClearText() { return QStringLiteral("清除已结束记录"); }
    static QString taskCenterPanelEmptyText() { return QStringLiteral("现在没有任务"); }
    /// 一行是**谁**。值就是 `TaskCenter::Item::key`（任务号的十进制字符串）。
    ///
    /// NOTE: 认一行要靠它、**不能靠行号** —— 「清除已完成」一按，下面那一栏整段没了，
    ///       行的编号当场全变（和 `PanelRow::key` / `FolderRow::path` 是同一条道理）。
    /// NOTE: 回的是 `const char *` 而不是 `QString` —— `QObject::setProperty()` / `property()`
    ///       第一个参数只收 `const char *`（`QString` 转不过去，报的是"没有这个函数"，
    ///       看着像 setProperty 不存在）。和 `InstanceFilesPage::rowPathProperty()` 同一个写法。
    static const char *taskCenterPanelRowKeyProperty() { return "wtfmlTaskKey"; }
    /// 一行里那几块的名字。**都带行号**，验证工程按号找控件，不用数"这行里有几个 QLabel"。
    static QString taskCenterPanelRowName(int index) {
        return QStringLiteral("taskCenterPanelRow%1").arg(index);
    }
    static QString taskCenterPanelRowTitleName(int index) {
        return QStringLiteral("taskCenterPanelRow%1Title").arg(index);
    }
    static QString taskCenterPanelRowDetailName(int index) {
        return QStringLiteral("taskCenterPanelRow%1Detail").arg(index);
    }
    static QString taskCenterPanelRowPercentName(int index) {
        return QStringLiteral("taskCenterPanelRow%1Percent").arg(index);
    }
    static QString taskCenterPanelRowTimeName(int index) {
        return QStringLiteral("taskCenterPanelRow%1Time").arg(index);
    }
    static QString taskCenterPanelRowBarName(int index) {
        return QStringLiteral("taskCenterPanelRow%1Bar").arg(index);
    }
    static QString taskCenterPanelRowDotName(int index) {
        return QStringLiteral("taskCenterPanelRow%1Dot").arg(index);
    }
    static QString taskCenterPanelRowStepName(int index, int step) {
        return QStringLiteral("taskCenterPanelRow%1Step%2").arg(index).arg(step);
    }
    static QString taskCenterPanelClearButtonName() {
        return QStringLiteral("taskCenterPanelClear");
    }
    /// 那一行**行尾那颗「取消」**（只有能力位说可以、而且外面挂了处理器时才长）。
    static QString taskCenterPanelRowCancelName(int index) {
        return QStringLiteral("taskCenterPanelRow%1Cancel").arg(index);
    }
    static QString taskCenterPanelRowCancelText() { return QStringLiteral("取消"); }
    static QString taskCenterPanelEmptyLabelName() {
        return QStringLiteral("taskCenterPanelEmpty");
    }

    // ------------------------------------------------------------------
    // 任务详情页（`ui/TaskDetailPage.hpp`，2026-10-12 加）
    // ------------------------------------------------------------------

    /// ★ **每一件东西的名字都从这里来**（验证工程按名字找控件量状态，找不到的按"没画"算）。
    static QString taskDetailPageName() { return QStringLiteral("taskDetailPage"); }
    static QString taskDetailStateName() { return QStringLiteral("taskDetailState"); }
    static QString taskDetailTimeName() { return QStringLiteral("taskDetailTime"); }
    static QString taskDetailTitleName() { return QStringLiteral("taskDetailTitle"); }
    static QString taskDetailDetailName() { return QStringLiteral("taskDetailDetail"); }
    static QString taskDetailBarName() { return QStringLiteral("taskDetailBar"); }
    static QString taskDetailPercentName() { return QStringLiteral("taskDetailPercent"); }
    static QString taskDetailBytesName() { return QStringLiteral("taskDetailBytes"); }
    static QString taskDetailStepsName() { return QStringLiteral("taskDetailSteps"); }
    static QString taskDetailStepName(int index) {
        return QStringLiteral("taskDetailStep%1").arg(index);
    }
    static QString taskDetailNoStepsName() { return QStringLiteral("taskDetailNoSteps"); }
    static QString taskDetailErrorName() { return QStringLiteral("taskDetailError"); }
    static QString taskDetailCancelName() { return QStringLiteral("taskDetailCancel"); }
    static QString taskDetailRetryName() { return QStringLiteral("taskDetailRetry"); }
    static QString taskDetailLogName() { return QStringLiteral("taskDetailLog"); }
    static QString taskDetailTargetName() { return QStringLiteral("taskDetailTarget"); }
    static QString taskDetailReportName() { return QStringLiteral("taskDetailReport"); }
    static QString taskDetailCrashDirName() { return QStringLiteral("taskDetailCrashDir"); }
    static QString taskDetailCopyName() { return QStringLiteral("taskDetailCopy"); }
    static QString taskDetailCloseName() { return QStringLiteral("taskDetailClose"); }

    /// 这一页上那几个词。**唯一出处**：页面里画它、验证工程比对它，都读这几个函数。
    static QString taskDetailStepsTitleText() { return QStringLiteral("到了哪一步"); }
    static QString taskDetailErrorTitleText() { return QStringLiteral("出了什么问题"); }
    static QString taskDetailNoStepsText() { return QStringLiteral("这一项没有分步。"); }
    static QString taskDetailNoTitleText() { return QStringLiteral("（没有名字的任务）"); }
    static QString taskDetailCancelText() { return QStringLiteral("取消"); }
    static QString taskDetailRetryText() { return QStringLiteral("重试"); }
    static QString taskDetailLogText() { return QStringLiteral("打开日志"); }
    static QString taskDetailTargetText() { return QStringLiteral("打开目标位置"); }
    static QString taskDetailReportText() { return QStringLiteral("导出错误报告"); }
    static QString taskDetailCrashDirText() { return QStringLiteral("打开崩溃报告目录"); }
    static QString taskDetailCopyText() { return QStringLiteral("复制摘要"); }
    static QString taskDetailCloseText() { return QStringLiteral("关闭"); }

    /// 状态那一句（"已完成" / "失败" / "已取消" / "已中断" / "正在进行" / "等待中" /
    /// "已暂停" / "等你决定"）。**不走 `taskSummaryText()`**：那一个拼的是底栏那一格
    /// （"下载 1.20.1（已完成）"），这一页要的是**单独一个词** —— 标题和记号都已经在
    /// 它旁边了，再拼一遍就是同一句话说两遍。
    static QString taskDetailStateText(TaskCenter::State state) {
        // NOTE: 写成逐条列出的 switch 而不是一张查表，是为了**将来多一个状态时编译器会替你
        //       盯一眼**（`terminalMark()` 和 `taskCenterState()` 那两处是同一个理由）。
        switch (state) {
        case TaskCenter::State::Queued:
            return QStringLiteral("等待中");
        case TaskCenter::State::Running:
            return QStringLiteral("正在进行");
        case TaskCenter::State::WaitingUser:
            return QStringLiteral("等你决定");
        case TaskCenter::State::Paused:
            return QStringLiteral("已暂停");
        case TaskCenter::State::Succeeded:
            return QStringLiteral("已完成");
        case TaskCenter::State::Failed:
            return QStringLiteral("失败");
        case TaskCenter::State::Cancelled:
            return QStringLiteral("已取消");
        case TaskCenter::State::Interrupted:
            return QStringLiteral("已中断");
        }
        return QString();
    }

    /// 起止那两个时间怎么写成一行（"19:42 开始 · 19:48 结束"）。两样都没有时回空串
    /// （这一页那一行就收起来，不留一行空白）。
    ///
    /// NOTE: **同一天只写时分，跨天才会冒出日期那半截** —— 和历史面板那一行用同一个
    ///       `TaskCenter::timeText()`（"几点几分"），多出来的那一截只在真的有用时才出现
    ///       （"昨天 23:50 开始的这一批"要是只写 23:50，看着像今天的事）。
    static QString taskDetailTimeLineText(const QDateTime &started, const QDateTime &finished) {
        const QString startedText = TaskCenter::timeText(started);
        const QString finishedText = TaskCenter::timeText(finished);
        if (startedText.isEmpty() && finishedText.isEmpty()) {
            return QString();
        }
        const auto withDate = [&](const QDateTime &when, const QString &text) {
            if (!text.isEmpty() && started.isValid() && finished.isValid()
                && started.date() != finished.date()) {
                return when.toString(QStringLiteral("MM-dd HH:mm"));
            }
            return text;
        };
        if (startedText.isEmpty()) {
            return QStringLiteral("%1 结束").arg(finishedText);
        }
        if (finishedText.isEmpty()) {
            return QStringLiteral("%1 开始，还没结束").arg(startedText);
        }
        return QStringLiteral("%1 开始 · %2 结束")
            .arg(withDate(started, startedText), withDate(finished, finishedText));
    }
    /// 一行右边那个百分比写成什么（「62%」）。**唯一出处**。
    static QString taskCenterPercentText(int percent) {
        return QStringLiteral("%1%").arg(qBound(0, percent, 100));
    }

    // ------------------------------------------------------------------
    // 完整性检查报告页（coding plan §4.1，2026-10-12）
    // ------------------------------------------------------------------
    // 这一页是**体检结论**摊开看的那一块：一句总述 + 按「客户端 / 库 / 资源索引 / 资源对象」
    // 分的四组 + 一整排按钮（只查看 / 复制报告 / 补齐缺失或损坏文件）。形状照任务详情页
    // （同一个 `execPage()` 覆盖层路数），名字和文案按那一套来。
    static QString integrityReportPageName() { return QStringLiteral("integrityReportPage"); }
    static QString integrityReportSummaryName() { return QStringLiteral("integrityReportSummary"); }
    static QString integrityReportErrorName() { return QStringLiteral("integrityReportError"); }
    /// 第几组的标题那一行（`客户端（2 个要修）`）。
    static QString integrityReportGroupName(int index) {
        return QStringLiteral("integrityReportGroup%1").arg(index);
    }
    /// 第几组里逐条问题的那一块（`m_body` 里那一组自己的容器）。
    static QString integrityReportGroupBodyName(int index) {
        return QStringLiteral("integrityReportGroupBody%1").arg(index);
    }
    /// 某一条问题的那一行（`label：detail` + 换行缩进的路径）。index 是它在整页里的序号。
    static QString integrityReportRowName(int index) {
        return QStringLiteral("integrityReportRow%1").arg(index);
    }
    static QString integrityReportCopyName() { return QStringLiteral("integrityReportCopy"); }
    static QString integrityReportRepairName() { return QStringLiteral("integrityReportRepair"); }
    static QString integrityReportCloseName() { return QStringLiteral("integrityReportClose"); }

    /// 一组标题怎么写：`客户端（2 个要修）`。
    static QString integrityReportGroupTitleText(const QString &name, int problems) {
        return QStringLiteral("%1（%2 个要修）").arg(name).arg(problems);
    }
    /// 一组里"缺几个、坏几个"那一句。**和总述用的是同一个分法**（见 VersionIntegrity）。
    static QString integrityReportGroupDetailText(int missing, int corrupt) {
        return QStringLiteral("缺 %1 个、坏 %2 个").arg(missing).arg(corrupt);
    }
    /// 一条问题那一行：`名字：那句话`，路径缩进换一行。路径单独一行是因为它可能很长。
    static QString integrityReportRowText(const QString &label, const QString &detail,
                                          const QString &path) {
        return QStringLiteral("%1：%2\n    %3").arg(label, detail, path);
    }
    /// 报告页那一句出错时前面加的标题。
    static QString integrityReportErrorTitleText() { return QStringLiteral("体检没跑起来"); }
    /// 四组一条问题都没有时写的那句（比留一块空白强，也让验证工程分得清）。
    static QString integrityReportAllGoodText() { return QStringLiteral("这一组没有要修的文件。"); }
    static QString integrityReportCopyText() { return QStringLiteral("复制报告"); }
    static QString integrityReportRepairText() { return QStringLiteral("补齐缺失或损坏文件"); }
    static QString integrityReportCloseText() { return QStringLiteral("只查看"); }

    // ------------------------------------------------------------------
    // 更新中心那一页（coding plan §7.1，2026-10-13）
    // ------------------------------------------------------------------
    // 「检查更新」查出来的那一份结论摊成能动手的一页（形状照完整性报告页：同一个
    // `execPage()` 覆盖层路数）。名字和文案按那一套来 —— 这一页上出现的每一个字都从这儿取，
    // 验证工程（ui_check / ui_shot）认的也是这里的名字，别在 .cpp 里另写字面量。
    static QString updateCenterPageName() { return QStringLiteral("updateCenterPage"); }
    static QString updateCenterSummaryName() { return QStringLiteral("updateCenterSummary"); }
    static QString updateCenterErrorName() { return QStringLiteral("updateCenterError"); }
    static QString updateCenterNoteName() { return QStringLiteral("updateCenterNote"); }
    /// 第几组的标题那一行（`有新版本（3 条）`）。
    static QString updateCenterGroupName(int index) {
        return QStringLiteral("updateCenterGroup%1").arg(index);
    }
    /// 第几组里装行的那一块。
    static QString updateCenterGroupBodyName(int index) {
        return QStringLiteral("updateCenterGroupBody%1").arg(index);
    }
    /// 第几行那条资源（一个 QWidget：勾选框 + 名字 + 版本变化 + 那三颗 chip）。
    static QString updateCenterRowName(int index) {
        return QStringLiteral("updateCenterRow%1").arg(index);
    }
    static QString updateCenterRowCheckName(int index) {
        return QStringLiteral("updateCenterRowCheck%1").arg(index);
    }
    static QString updateCenterRowNameLabelName(int index) {
        return QStringLiteral("updateCenterRowLabel%1").arg(index);
    }
    static QString updateCenterRowStateName(int index) {
        return QStringLiteral("updateCenterRowState%1").arg(index);
    }
    /// 名字下面那一行小字（盘上那个文件的名字）。
    static QString updateCenterRowFileNameLabelName(int index) {
        return QStringLiteral("updateCenterRowFile%1").arg(index);
    }
    /// 行尾那三颗：就更新这一条 / 忽略这一版 / 固定这一版。
    static QString updateCenterRowUpdateName(int index) {
        return QStringLiteral("updateCenterRowUpdate%1").arg(index);
    }
    static QString updateCenterRowIgnoreName(int index) {
        return QStringLiteral("updateCenterRowIgnore%1").arg(index);
    }
    static QString updateCenterRowPinName(int index) {
        return QStringLiteral("updateCenterRowPin%1").arg(index);
    }
    static QString updateCenterUpdateSelectedName() {
        return QStringLiteral("updateCenterUpdateSelected");
    }
    static QString updateCenterUndoName() { return QStringLiteral("updateCenterUndo"); }
    static QString updateCenterCloseName() { return QStringLiteral("updateCenterClose"); }
    /// 装"没有结论"那句话的那一块（error 非空时露）。
    static QString updateCenterEmptyTitleText() { return QStringLiteral("还没有结果可以看"); }
    /// 两组的名字。**顺序就是摆的顺序**：能动手的那一组在上面（点这一下就是来看它的）。
    static QString updateCenterOutdatedGroupTitleText(int count) {
        return QStringLiteral("有新版本（%1 条）").arg(count);
    }
    static QString updateCenterRestGroupTitleText(int count) {
        return QStringLiteral("已是最新或说不清（%1 条）").arg(count);
    }
    /// 某一行的名字那一格。`name` 取不到时（反查没认出来）退回文件名 —— 和报告那一版
    /// （`finishUpdateCheck()` 里那个 displayName）**逐字同一个规矩**：手上总得有个东西
    /// 能让用户对着它去盘上找。
    static QString updateCenterRowLabelText(const QString &name, const QString &fileName) {
        return name.isEmpty() ? fileName : name;
    }
    /// 某一行的状态那一格（右边那小字）。**这一句是"它现在算不算更新"的唯一说法**。
    /// @param suppressed 「忽略这一版」/「固定这一版」那一档（`UpdateCenterPage::Suppress`）。
    static QString updateCenterRowStateText(int state, int suppressed, const QString &versionText) {
        // state: 0 = 有新版本、1 = 已是最新、2 = 说不清（`UpdateCenterPage::State`）
        // suppressed: 0 = 没有、1 = 已忽略、2 = 已固定（`UpdateCenterPage::Suppress`）
        switch (state) {
        case 0:
            if (suppressed == 1) {
                return QStringLiteral("已忽略这一版（%1）").arg(versionText);
            }
            if (suppressed == 2) {
                return QStringLiteral("已固定（不再提示）");
            }
            return QStringLiteral("可更新到 %1").arg(versionText);
        case 1:
            // 「已是最新」那一档顺带把**现在这一版**写出来（用户来这一页就是想知道"它到
            // 底是哪一版"）。拿不到版本号时不留一个空括号。
            return versionText.isEmpty() ? QStringLiteral("已是最新")
                                         : QStringLiteral("已是最新（%1）").arg(versionText);
        default:
            return QStringLiteral("说不清");
        }
    }
    /// 那一行「更新」那颗 chip 的字。
    static QString updateCenterRowUpdateText() { return QStringLiteral("更新"); }
    static QString updateCenterRowIgnoreText() { return QStringLiteral("忽略这一版"); }
    static QString updateCenterRowIgnoreUndoText() { return QStringLiteral("不再忽略"); }
    static QString updateCenterRowPinText() { return QStringLiteral("固定这一版"); }
    static QString updateCenterRowPinUndoText() { return QStringLiteral("不再固定"); }
    /// 行尾那三颗 chip 各自的鼠标提示（说清按下去会发生什么）。
    static QString updateCenterRowUpdateTipText() {
        return QStringLiteral("就更新这一条（换掉这个文件，换之前先留一份回滚点）");
    }
    static QString updateCenterRowIgnoreTipText() {
        return QStringLiteral("这一版以后别再提示我；作者再发新版本时它会回来");
    }
    static QString updateCenterRowPinTipText() {
        return QStringLiteral("我就停在这一版上，以后不提示更新；哪天文件换了它会自己失效");
    }
    /// 底下那颗主按钮：选了几条就写几条。**0 条时不写"更新选中的 0 项"**（那句话没有意义）。
    static QString updateCenterUpdateSelectedText(int count) {
        return count > 0 ? QStringLiteral("更新选中的 %1 项").arg(count)
                         : QStringLiteral("更新选中的…");
    }
    /// 底下那颗「撤销上一次更新」。N = 回滚点里存了几个文件。
    static QString updateCenterUndoText(int fileCount) {
        return QStringLiteral("撤销上一次更新（%1 个文件）").arg(fileCount);
    }
    /// 那颗按钮的鼠标提示：说清它会把实例改回什么样子。
    static QString updateCenterUndoTipText() {
        return QStringLiteral("把上一次更新换掉的文件退回原样（那次更新新添的文件会删掉）");
    }
    static QString updateCenterCloseText() { return QStringLiteral("关闭"); }
    /// 没查过（或者这个实例一条能查的都没有）时那一整块：一句大话 + 一句下一步。
    static QString updateCenterEmptyHintText() {
        return QStringLiteral("先在「实例设置 → 概览」里点一下「检查更新」，回来这儿就有东西可看了。");
    }

    // ------------------------------------------------------------------
    // 实例页左栏那张「游戏文件夹」列表（2026-09-24 改造：两行式 + 行尾齿轮菜单）
    // ------------------------------------------------------------------
    // 用户原话：「显示的时候和版本下载那里类似，上面（版本名同样的地方）写 customname，
    // 下面（发布日期那里）写路径，分栏做高一点不要像现在那么小，每个版本右侧有个齿轮，
    // 点击以后会有个类似右键菜单的页面（框），里面是刷新 重命名 打开（explorer.exe）
    // 移除 删除（红色字）」。
    //
    // 形状照抄版本树那几行：**上面一行正文 + 下面一行小字**，行高因此比原来那条单行
    // 列表高一倍（见 folderRowHeight()）。行尾那颗齿轮是**真按钮**（不是画出来再靠
    // 事件过滤器认位置）—— 和底栏面板那几颗同一个理由：真控件白得 hover / 按下 /
    // disabled 三态，验证工程也能按 objectName 把它找回来点。
    // ------------------------------------------------------------------

    /// 列表里的一行。
    struct FolderRow {
        /// **这一行是谁**：根目录的绝对路径。齿轮菜单里那几个动作原样带着它回来。
        ///
        /// NOTE: 认身份必须靠它，**不能靠"第几行"** —— 菜单弹出来到用户点某一下之间
        ///       隔着看两眼的时间，这中间列表是会变的（别处删掉了一份文件夹）。
        ///       这和 PanelRow::key 那条 NOTE 是同一个道理。
        QString path;
        /// 上面那行字：用户起的 customname；没起过时由 MainWindow 给默认显示名
        /// （自带那份是「当前文件夹」，别的目录是目录名）。
        QString title;
        /// 下面那行字：完整路径（系统分隔符）。目录不在盘上时后面还缀一句。
        QString subtitle;
        /// true = 启动器自带的**那一份**。它同时是两件事：行尾齿轮的提示里说明是哪一份，
        /// 以及菜单里「移除」「删除」两项**灰掉**的理由（那一份移不掉也删不掉）。
        bool assistant = false;
        /// true = 这一行就是启动器当前在用的那一份（列表里选中它）。
        bool current = false;
    };

    /// 齿轮弹出来那五项，**顺序就是它们显示的顺序**（枚举值不是下标，别拿它去索引）。
    /// 每一项对应 MainWindow 那边的一个槽；「删除」是红色那项。
    enum FolderAction {
        FolderActionRefresh,  ///< 刷新：重新扫一遍这个文件夹
        FolderActionRename,   ///< 重命名：改的是 customname，不碰磁盘上的目录名
        FolderActionOpen,     ///< 打开：拿系统资源管理器打开那个目录
        FolderActionRemove,   ///< 移除：只从列表里拿掉，盘上不动（和下面那个按钮同一件事）
        FolderActionDelete,   ///< 删除：**连盘上的目录一起删掉**（红色那一项）
    };
    /// 一共几项（菜单是照它循环摆的，验证工程也按它数行）。
    static int folderActionCount() { return 5; }
    /// 第 @p action 项上写的字。
    static QString folderActionText(int action);
    /// 第 @p action 项鼠标停上去显示的那句话。
    static QString folderActionTooltip(int action);
    /// 第 @p action 项前面那个图标（全是代码画的字形，见 VersionIcons）。
    static QPixmap folderActionIcon(int action, int size, const QColor &color);
    /// 这块调色板的底色算不算"深"那一档。
    ///
    /// NOTE: 判据**只此一处** —— 红的深浅两档（下面的 `folderDangerColor()`、安装页那行
    ///       红字）、底栏那颗方块的三态都拿它分档。原来 `folderDangerColor()` 和
    ///       `runStateMarkColor()` 各写了一遍同一个 `lightness() < 128`，第三个要分档的
    ///       地方（安装页那行红字）就漏了 —— 阈值这种东西一散开就会这样。
    ///       取的是**窗口底色的明度**，不是某个"主题叫什么"的标志位：将来是哪套主题谁也
    ///       不知道，明度是量得出来的、也是真决定了"看不看得见"的那个量。
    static bool paletteIsDark(const QPalette &palette);
    /// 「删除」那一项的字色。用户点名要的（原话里就写着"红色字"）。
    ///
    /// NOTE: 红分两档（浅色主题用深红、深色主题用浅红）：深红落在深色底上只剩一团黑，
    ///       而这颗红字正是"这一项会把东西删掉"这件事的唯一提示，看不见就等于没有。
    ///       判据走 paletteIsDark() —— 和 QSS 那几处"从调色板推颜色"同一个路数。
    static QColor folderDangerColor(const QPalette &palette);

    /// 一行的高度（像素）。两行字 + 上下各一圈边距，比原来那条单行列表高不少 ——
    /// 用户原话「分栏做高一点不要像现在那么小」。
    static int folderRowHeight() { return 56; }

    /// 齿轮弹出来那个框。名字挂在这里是为了让验证工程能按 objectName 把它找回来
    /// （它是个 Qt::Popup 窗口，父子关系照旧，所以 findChild 找得到）。
    static QString folderMenuName() { return QStringLiteral("folderMenu"); }
    // NOTE: **不用 QMenu::exec()**。仓库里右键菜单本来是零先例的，原因写在实例页那三个
    //       按钮那段注释里：验证工程跑在 offscreen 上，exec() 会阻塞在事件循环里，
    //       几乎驱动不了。所以这里和底栏那两块面板一样 —— Qt::Popup + show()，
    //       点外面/Esc 自己关，但**不阻塞**，验证工程能把它捞回来逐项点。
    /// 菜单里第 @p action 项的 objectName。
    static QString folderMenuActionName(int action) {
        return QStringLiteral("folderMenuAction%1").arg(action);
    }
    /// 菜单每一项的图标边长（像素）。
    static int folderMenuIconSize() { return 16; }
    /// 行尾那颗齿轮按钮的边长（像素）。里面装的是 folderMenuIconSize() 那么大一个字形。
    static int folderRowGearSize() { return 24; }

    /// 列表里第 @p index 行的 objectName（行本身、两行字、行尾那颗齿轮）。
    static QString folderRowName(int index) { return QStringLiteral("folderRow%1").arg(index); }
    static QString folderRowTitleName(int index) {
        return QStringLiteral("folderRowTitle%1").arg(index);
    }
    static QString folderRowSubtitleName(int index) {
        return QStringLiteral("folderRowSubtitle%1").arg(index);
    }
    /// 第 @p index 行**路径那行**从哪一头省略（Qt::ElideMiddle）。
    ///
    /// NOTE: 这是 `folderRowSubtitle%1` 那个标签自己的 `elideMode` 属性（见
    ///       MainWindowUI.cpp 里的 ElidedLabel），不是 QListWidget 上那个
    ///       `setTextElideMode()` —— 那一张列表的条目**一个字都没有**（内容全是
    ///       setItemWidget() 塞进去的真控件），所以 2026-09-25 起那处设置已经删掉了。
    ///       放在 MainWindowUI 里是为了让验证工程量得到"用户看到的那个标签"。
    static Qt::TextElideMode folderRowPathElideMode() { return Qt::ElideMiddle; }
    static QString folderRowGearName(int index) {
        return QStringLiteral("folderRowGear%1").arg(index);
    }

    /// 「齿轮菜单里某一项被点了」。
    ///
    /// NOTE: 只有这一个回调 —— 点行本身（选中那一份文件夹）走的还是 QListWidget 自己那套，
    ///       界面这一层不插一手（行里的字和图标都是**不吃鼠标**的普通控件，点下去会一路
    ///       冒泡到 viewport，选中照旧生效；行尾那颗齿轮按钮才吃掉自己的那一下）。
    using FolderActionHandler = std::function<void(const QString &path, int action)>;
    /// 装上那个回调。**由 MainWindow 在构造时调一次**（数据在它手里）；没装的话
    /// 菜单照样弹得出来，只是点了没反应。
    void setFolderActionHandler(FolderActionHandler handler) {
        m_folderActionHandler = std::move(handler);
    }

    /// 把左边那张列表整个重灌一遍（几行、每行写什么，全由 MainWindow 翻好递进来）。
    ///
    /// NOTE: 里面**挡住了信号** —— 重灌不是"用户改选"，但为了摆出选中态最后要调一次
    ///       setCurrentItem()，那个会发 currentRowChanged。不挡的话每刷一次列表就等于
    ///       又切一次游戏文件夹（而"切换"是要重扫磁盘、重灌界面的）。
    void setFolderRows(const QList<FolderRow> &rows);

    // ------------------------------------------------------------------
    // 实例页右栏那张树里的**组行**：右边一个箭头 + 一颗齿轮，齿轮弹一个框。
    //
    // 用户原话：「每一个组和版本下载几乎一致，点一下打开/关闭，右侧有个箭头和齿轮，
    // 齿轮可以批量管理实例或者编辑该分组的设置」。
    //
    // 做法照左栏那个文件夹齿轮（Qt::Popup + findChild 认"现在开着的是哪一块"、
    // 灰着的项必须配一句为什么）。三处不同：
    //   * 那块框贴在**行**上，而行是代理画出来的（不是控件）⇒ 锚点交给的是一个**全局
    //     矩形**（InstanceGroupDelegate::gearRect()），不是一颗按钮；
    //   * 认人靠组 id（文件夹那边是路径）；
    //   * 多一个"挑目标"的模式：那颗齿轮里有一项要弹**第二个**框（把实例移进哪一组）——
    //     见下面 GroupMenu 的两套构造。
    // ------------------------------------------------------------------

    /// 弹菜单那一刻捕下来的**一个组**的快照。
    struct GroupRow {
        /// 这一行是谁：组 id（`default-*` 是内置那五个；空串 = 虚拟的「未分组」）。
        /// 动作原样带着它回来，界面这一层不猜。
        ///
        /// NOTE: 和 FolderRow::path 同一条：认身份靠 id，**不能靠"第几行"** ——
        ///       菜单开着的时候用户看两眼，这中间别处一行代码就可能把组删了。
        QString groupId;
        /// 组名（不带后面那个计数）。
        QString name;
        /// 这一组里几个实例。**0 个的组在树里根本不画**（用户要的"空白的分组隐藏"），
        /// 所以它只可能是"菜单开着的时候实例被别处搬走了"这种过期快照 —— 那几个动作
        /// 落到 MainWindow 那头会按 id 重新查一遍，查不到就当没发生。
        int memberCount = 0;
        /// 这一组现在在树里是展开的还是收着的。「默认展开：开/关」那一项写的字、
        /// 以及点了之后要翻成什么，都用它。
        bool expanded = true;
        /// 此刻列表里**有没有**"选中的实例"，以及那个实例**是不是就在这一组里**。
        /// 「把选中的实例移入分组…」能不能点看第一个；挑目标那个框里"就是本组"那一条
        /// 灰掉看第二个。
        bool hasSelection = false;
        bool containsSelected = false;
        /// true = 这是那个**虚拟的「未分组」**（组 id 是空串）。
        ///
        /// NOTE: 它不是真的组：不能改名、不能删、不能排序、也没有"默认展开"这一说
        ///       （它收起来这件事不落盘 —— 文件里没有它的记录，每次重灌树它都是展开的）。
        ///       它那一档的菜单因此只剩「把选中的实例移入分组…」一项 —— 那一项是
        ///       **回到分组里的唯一入口**：一个组里一个实例都没有时那个组头是藏着的，
        ///       想往空组里放第一个实例就得从这儿进（见 groupActionVisible）。
        bool virtualGroup = false;
    };

    /// 组行行尾那颗齿轮弹出来的动作，**顺序就是它们显示的顺序**
    /// （枚举值不是下标，别拿它去索引）。
    enum GroupAction {
        GroupActionMoveInto,     ///< 把选中的实例移入分组…（再弹一个"移进哪一组"的框）
        GroupActionClearMembers, ///< 把这一组里的实例都移出分组（组留着、实例一个不删）
        GroupActionRename,       ///< 重命名这一组…
        GroupActionToggleExpand, ///< 默认展开：开 / 关（这一项的字**带着状态**）
        GroupActionDelete,       ///< 删除这一组…（红色那一项；里面的实例一个都不删）
    };
    /// 一共几项（菜单照它循环摆，"未分组"那一档再按 groupActionVisible 滤一遍）。
    static int groupActionCount() { return 5; }
    /// 第 @p action 项对 @p row 这一组**显不显示**。
    ///
    /// NOTE: 虚拟的「未分组」只有第一项 —— 上面那四条理由写在 GroupRow::virtualGroup
    ///       那段。**是"不显示"而不是"灰着"**：灰着还要配一句"为什么不能给未分组改名"，
    ///       而这件事没有为什么，它压根不是一个组。
    static bool groupActionVisible(int action, const GroupRow &row) {
        return !(row.virtualGroup && action != GroupActionMoveInto);
    }
    /// 第 @p action 项上写的字。**要用到 @p row**：展开那一项把状态写在字里
    /// （「默认展开：开」/「默认展开：关」）—— 不摆勾字形，免得菜单里"有的项前面有
    /// 东西、有的没有"，文字对不齐。
    static QString groupActionText(int action, const GroupRow &row);
    /// 第 @p action 项现在能不能点。
    static bool groupActionEnabled(int action, const GroupRow &row);
    /// 第 @p action 项鼠标停上去显示的那句话。**点不动的那几项由它说清为什么**
    /// （灰着不说话，用户只会以为界面坏了 —— 和文件夹齿轮那条同一个规矩）。
    static QString groupActionTooltip(int action, const GroupRow &row);
    /// 「删除这一组…」那一项的字色。复用文件夹齿轮那一处红（同一个调色板规则）。
    static QColor groupDangerColor(const QPalette &palette) { return folderDangerColor(palette); }

    /// 齿轮弹出来的那块框的 objectName，以及里面每一项的 objectName。
    ///
    /// NOTE: 那块框和"挑目标"那块框**共用同一个名字** —— 同一颗齿轮下只可能开着
    ///       其中一个，验证工程按名字捞回来的就该是当下那一个（它再按 pickMode() 分开）。
    static QString groupMenuName() { return QStringLiteral("groupMenu"); }
    static QString groupMenuActionName(int action) {
        return QStringLiteral("groupMenuAction%1").arg(action);
    }
    /// "挑目标"那块框里，第 @p index 行的 objectName（0 起，顺序就是我们递进去的顺序）。
    static QString groupPickRowName(int index) {
        return QStringLiteral("groupPickRow%1").arg(index);
    }
    /// 这两块框里每一项的图标边长（像素）。和文件夹那块一样 16。
    static int groupMenuIconSize() { return 16; }

    /// 「组齿轮菜单里某一项被点了」。
    ///
    /// NOTE: 点组行**本身**（展开/收起）不走这里 —— 那条由 MainWindow 接
    ///       ui/RowActivation.hpp 那套。这儿只有齿轮弹出框里那几项。
    using GroupActionHandler = std::function<void(const QString &groupId, int action)>;
    /// 「挑目标框里某一组被点了」：把选中的实例移进 @p groupId（空串 = 未分组）。
    using GroupPickHandler = std::function<void(const QString &groupId)>;
    /// 装上这两个回调（MainWindow 构造时各调一次）。没装的话框照样弹得出来，
    /// 只是点了没反应。
    void setGroupActionHandler(GroupActionHandler handler) {
        m_groupActionHandler = std::move(handler);
    }
    void setGroupPickHandler(GroupPickHandler handler) {
        m_groupPickHandler = std::move(handler);
    }

    /// 在 @p anchorGlobal 那一格（组行行尾那颗齿轮，**全局坐标**；形状见
    /// InstanceGroupDelegate::gearRect()）弹出 @p row 这一组的动作菜单。
    ///
    /// NOTE: "再点一次同一颗齿轮 = 收回"靠 groupId 认人（锚点矩形是按行算的，
    ///       同一行滚动一下就不一样了，拿它当身份不牢）。
    void openGroupMenu(const GroupRow &row, const QRect &anchorGlobal);

    /// 在 @p anchorGlobal 那一格弹出"把选中的实例移进哪一组"（@p rows 是**全部**
    /// 分组 + 虚拟的「未分组」，含**一个实例都没有**的那些 —— 一个空组在树里是藏着
    /// 的，这个框是把它放进第一个实例的唯一入口）。
    void openGroupPicker(const QList<GroupRow> &rows, const QRect &anchorGlobal);

    // ---------- 左右两栏（四个选项卡都用） ----------
    // 栏是**真的控件**（不是裸布局）：这样才能按 objectName 找回来量几何、
    // 也才能在验证工程里断言左右两栏的份数（每一页各自的那一对）。
    //
    // NOTE: 2026-09-26 起「设置」页也用 buildTwoColumnPage() 了 —— 左栏是那五条分类、
    //       右栏是一个堆叠页（原来它是一张通到底的两列 grid，见 setupSettingsPage 那段）。
    //       所以栏内那张 grid 现在**每一栏各有一张**（objectName 见 settingsFormName(key)），
    //       它管的是**栏内**那一层；而左右两栏之间那一层是这一对份数。
    //       栏内那些行 2026-09-26 又改成了上下结构（每格跨两列，见 cpp 的 addSettingRow()），
    //       所以这张 grid 的列份数如今只摆形状、不再分隔任何一行。
    /// @param tab "home" / "instance" / "download" / "settings"。
    static QString leftColumnName(const QString &tab) {
        return tab + QStringLiteral("LeftColumn");
    }
    /// @param tab 同上。
    static QString rightColumnName(const QString &tab) {
        return tab + QStringLiteral("RightColumn");
    }

    /// 侧栏那一列的宽度（**唯一出处**，2026-10-12 那套统一页面骨架里那一档）。
    ///
    /// 用户 2026-10-12 那份改版文档：`侧栏 208~220 px`。导航那一列是**固定宽度**、不再按
    /// 份数分（见 `buildTwoColumnPage()` 里那段）；只有主页不套这一档 —— 它左边是"选版本 +
    /// 启动"，那是正文。验证工程拿它当"侧栏就是该这么宽"的期望值，界面和测试各写一份的话
    /// 改宽度时一定会有一边忘了改。
    static int sidebarWidth() { return 216; }

    /// 主页**右栏**（「我的角色」那一栏）的宽度（**唯一出处**，2026-10-16 起）。
    ///
    /// ★ 主页右栏**不再按份数分宽**：装上「我的角色」那张卡片之后，这一栏的宽度由卡片
    ///   自己钉死（卡片里那 160 px 的人物画布 + 左右各 8 px 内边距），左栏吃剩下的全部。
    ///   从前那个"精确的 7:3"（见 `homeLeftColumnStretch()`）是"这一栏只由份数决定宽度"
    ///   那个前提下的说法 —— 前提没了，那份比例也就不再是这一栏的真话（验证工程按它量
    ///   会量出 664:176 这种数）。
    ///   NOTE: 右栏列本身在 `buildTwoColumnPage()` 里就 `setFixedWidth()` 到这一个数，
    ///         所以它是**跟窗口宽度无关**的常量 —— 这正是验证工程能拿它当期望值的原因。
    static constexpr int homeRightColumnWidth() { return 176; }

    /// 正文那一栏顶上那个页头（页面标题那一行 + 说明那一行）的名字。
    /// @param tab "instance" / "download" / "settings"（主页没有页头）。
    static QString pageHeaderName(const QString &tab) {
        return tab + QStringLiteral("PageHeader");
    }
    /// 页头里那一行标题。
    static QString pageTitleName(const QString &tab) {
        return tab + QStringLiteral("PageTitle");
    }
    /// 页头里那一行说明（路径 / 当前状态那一类；空着时整行收起来）。
    static QString pageSubtitleName(const QString &tab) {
        return tab + QStringLiteral("PageSubtitle");
    }
    /// 栏**内**那些表格的份数：左 3 右 7。设置页每一栏那张 grid（见「设置页」那一段）
    /// 和实例设置里那几张表格用它 —— ★ 它**不再是页面两栏的比例**（2026-10-12 起
    /// 侧栏是固定宽度，见 sidebarWidth()）。
    static int leftColumnStretch() { return 3; }
    /// 栏内那张表格右列占的份数（默认那一套）。
    static int rightColumnStretch() { return 7; }
    /// 栏内那张表格里右列是不是真的更宽（验证工程拿它当"默认的 3:7 生效了"的判据）。
    static bool rightColumnIsWider() { return rightColumnStretch() > leftColumnStretch(); }
    /// 主页那一套份数：7:3。
    /// ★ 2026-10-16：这一对**不再是主页两栏的宽度规则** —— 主页右栏装上「我的角色」
    ///   之后宽度被 `setFixedWidth()` 钉死（见 `homeRightColumnWidth()`），左栏吃剩下的
    ///   全部。这两个数现在只是 `buildTwoColumnPage()` 里那一次 `addWidget(..., stretch)`
    ///   的入参（右栏定宽之后 stretch 对它无效，左栏是唯一能长的那一栏，所以 7 也一样
    ///   不影响结果）。**量宽度请看 `homeRightColumnWidth()`**，别拿 7:3 去算。
    static int homeLeftColumnStretch() { return 7; }
    static int homeRightColumnStretch() { return 3; }

    /// 这一页是不是"左宽右窄"。
    /// @param tab "home" / "instance" / "download"。
    ///
    /// 只有主页是。主页右栏装的是「我的角色」（3D 预览），它是**定宽**的一栏；左栏是
    /// "选实例 + 启动"那一摊，吃剩下的全部宽度。另外三页的左栏是**固定宽度的侧栏**
    /// （见 sidebarWidth()），份数给 0 —— 它多宽由那一栏自己的宽度定，和比例无关。
    static bool leftColumnIsWider(const QString &tab) {
        return tab == QStringLiteral("home");
    }
    /// 某一页左栏占的份数。
    ///
    /// ★ 2026-10-12：只给主页一个非 0 的份数。另三页（实例/下载/设置）的左栏
    ///   换成了**固定宽度的侧栏**（`sidebarWidth()`，用户那份改版文档：`侧栏 208 px`，
    ///   理由见 `kSidebarWidth` 那段）—— 侧栏自己那一位给 0，宽度全归正文那一栏。
    ///   从前那两套 `downloadLeftColumnStretch()`（2:8）/`settingsLeftColumnStretch()`
    ///   （也是 2:8）**连函数一起删了**：那两份数字是在"按比例分宽"那个前提下定的，
    ///   侧栏定死之后它们没有任何读者，留着就是一份和界面无关的假规格。
    /// ★ 2026-10-16：主页这一对也**退化成入参**了（右栏定宽，见 homeLeftColumnStretch()）
    ///   —— 主页真正的宽度规则是 `homeRightColumnWidth()`。
    static int leftColumnStretch(const QString &tab) {
        return tab == QStringLiteral("home") ? homeLeftColumnStretch() : 0;
    }
    /// 某一页右栏占的份数（见 leftColumnStretch(const QString&)）—— 左栏定死宽度之后，
    /// 多出来的宽度**全归这一栏**（所以除了主页，这里一律是 1）。
    static int rightColumnStretch(const QString &tab) {
        return tab == QStringLiteral("home") ? homeRightColumnStretch() : 1;
    }

    // ---------- 设置页：左栏那几条分类 ----------
    // 2026-09-26 起设置页**分了栏**（用户原话：「将设置分栏，分为启动/提示/个性化/… 然后
    // 分门别类放进去」）。形状和下载页一模一样：左栏一列分类、右栏一份堆叠页，
    // **两边的下标一一对应**（搭的时候按同一张表、同一个顺序加页，点第 N 条就
    // setCurrentIndex(N)）—— 验证工程会数一遍两边的条数。
    //
    // NOTE: 从前这里只有 settingsScrollName()/settingsFormName() 两个名字 —— 那时候
    //       整页就是**一张通到底的两列表格**（列 0 选项名称、列 1 控件、3:7），
    //       没有栏的概念。现在那张表格**每一栏各有一张**（objectName 见
    //       settingsFormName(key)），它管的是**栏内**那一层；栏与栏之间那一层换成
    //       真控件（settingsCategoryList + settingsCategoryStack），比例走
    //       settingsLeftColumnStretch()/settingsRightColumnStretch()。
    /// 分栏的键。**顺序就是界面上从上到下的顺序**，也是堆叠页的下标顺序。
    /// 进条目的 `Qt::UserRole`（条目文字是显示内容、会随文案改，不能拿它当身份）。
    static QStringList settingsCategoryKeys() {
        return {QStringLiteral("launch"), QStringLiteral("game"), QStringLiteral("warn"),
                QStringLiteral("look"), QStringLiteral("download")};
    }
    /// 同上的显示文字（左栏上看到的）。**和键一条一条对齐**（两边条数一样是条不变量）。
    static QStringList settingsCategoryTexts() {
        return {QStringLiteral("启动"), QStringLiteral("游戏"), QStringLiteral("提示"),
                QStringLiteral("个性化"), QStringLiteral("下载")};
    }
    static QString settingsCategoryListName() { return QStringLiteral("settingsCategoryList"); }
    static QString settingsCategoryStackName() { return QStringLiteral("settingsCategoryStack"); }
    /// 设置页页头那个标题（2026-10-12 那次改版给每一页都加了页头，见 cpp 里
    /// `buildTwoColumnPage()` 那段）。这一页**没有说明那一行**，只有标题。
    static QString settingsPageTitle() { return QStringLiteral("设置"); }
    /// 设置页那张表单**最宽**多宽（逻辑像素）。**唯一出处**（`addSettingsPanel()` 用它
    /// 给 `panel.form` 限宽）。
    ///
    /// 它治的是"设置页内容横向无限伸展，说明文字一行跨得太长"：窗口拉宽时表单不再跟着长，
    /// 而是**居中**摆着（多出来的空白留在两边）。
    ///
    /// ★ 2026-10-12（coding plan §9.1）从 820 到 **960**：改版那份文档给的是 760~840
    ///   （取中间 820），plan 这份给的是"正文最大可读宽度约 900–1040"，**后者更新**，
    ///   取 960（下沿往上一点，窗口 900 px 时表单仍然铺满，不会平白留一圈空）。
    ///
    /// NOTE: 每一栏**各自**限一次（三栏三张表单），因为它挂在 `form` 上而不是滚动区上 ——
    ///       滚动条仍然贴着正文面最右边那条线，只有表单收窄。
    static int settingsFormMaxWidth() { return 960; }
    /// 某一栏那一整页的 objectName（堆叠页里的一页 = 一层滚动区，见 cpp 里的
    /// addSettingsPanel()）。
    static QString settingsPanelName(const QString &key) {
        return QStringLiteral("settingsPanel-") + key;
    }
    /// 某一栏里那张 grid（3:7；**每一行都跨两列** —— 行名一行、内容下一行，见 cpp 的
    /// addSettingRow()）的 objectName。
    static QString settingsFormName(const QString &key) {
        return QStringLiteral("settingsForm-") + key;
    }
    /// ★ 2026-09-28：原来这一格是个下拉框（`javaVersionCombo`），用户点名的改法是
    /// 「把设置里 java 版本换成一个单独的按钮 java 管理」—— 选哪个 Java 那件事整个搬进了
    /// 「Java 管理」那一页，这里只剩一颗开门按钮。**别把那个下拉框加回来**：
    /// 加了就成两处都在说"这一台机器用哪个 Java"，而它们谁也不会跟着谁变。
    static QString javaManageButtonName() { return QStringLiteral("javaManageButton"); }
    /// 「这一层现在用的是哪个 Java」。和那颗按钮**不是一回事**：按钮是"去哪儿改"，
    /// 这一行是"现在是什么"（「自动：上次用的是 …」/「手动指定：…」）。
    static QString javaPathLabelName() { return QStringLiteral("javaPathLabel"); }
    static QString javaWarnMismatchCheckName() { return QStringLiteral("javaWarnMismatchCheck"); }
    /// 这一行的行标题（左列那一格）。★ 2026-09-26：它原来叫「版本提醒」—— 说不清是提醒
    /// 什么，而这一行自己说的是"实例指定的 Java 对不上"。分栏之后它和「重复启动」
    /// 「进度提醒」并排（都在「提示」栏里），标题跟着改成同一族的说法（说的是"什么时候
    /// 会被问一句"）。**勾选框上那句话一个字都没动**。
    static QString javaWarnMismatchRowText() { return QStringLiteral("Java 版本"); }
    static QString memoryHintName() { return QStringLiteral("memoryHint"); }
    /// 「最大内存」那一行下面那句说明。文字随系统内存变（可用内存不是一个定值），
    /// 所以它是这个设置页里唯一一个要 MainWindow 反复改文字的 QLabel，得能按名字取回来。
    /// ★ 2026-10-18：原来挂在「分配方式」那一行下面（叫 memoryAutoHint），那行撤掉之后
    ///   并到了「最大内存」这一行。
    static QString memoryMaxSpinName() { return QStringLiteral("memoryMaxSpin"); }
    /// 「最大内存」这一行是**一根条 + 一个数字框**（两件东西说的是同一件事）。★ 2026-09-26：
    /// 条是这一笔加的（用户要 PCL 那种拖着改的），数字框留着 —— 精细那几下还是打字快，
    /// 而且验证工程一直是按 objectName 找它、拿它的范围当"上限真是本机物理内存"的凭据。
    static QString memoryMaxSliderName() { return QStringLiteral("memoryMaxSlider"); }
    // NOTE: 「最小内存」（-Xms）那个数字框**没有了**（2026-09-26）—— 用户原话是「不要 xms 了
    // （或者隐藏起来），按照默认值」。值仍然存在设置里、命令行照发 -Xms（见
    // LauncherManager 里那两行），少的只是界面上那一格。
    static QString launcherBehaviourComboName() { return QStringLiteral("launcherBehaviourCombo"); }
    static QString extraJvmArgsEditName() { return QStringLiteral("extraJvmArgsEdit"); }
    static QString applyExtraJvmArgsButtonName() {
        return QStringLiteral("applyExtraJvmArgsButton");
    }
    static QString downloadSourceComboName() { return QStringLiteral("downloadSourceCombo"); }

    // ---------- 「同时最多下几个」（2026-10-13 加的，§9.2 并发数预设，在「下载源」那一组里） ----------
    // 和 gameWindowModeCombo 同一个形状：**只有框没有项** —— 四档那个表在 src/ 的
    // `DownloadConcurrency.hpp` 里（唯一出处），而 ui/ 不许 include src/（分层铁律）。
    // 项由 MainWindow 灌（每项 itemData 存那个 int），出厂选中的那一档也由它选。
    static QString downloadConcurrencyComboName() {
        return QStringLiteral("downloadConcurrencyCombo");
    }
    /// 这一行的行标题（左列那一格）。
    static QString downloadConcurrencyRowText() { return QStringLiteral("并发数"); }

    // ---------- 「下载速度上限」（2026-10-18 加的，§9.2 限速，在「下载源」那一组里） ----------
    // 和上面那颗同一个形状：**只有框没有项** —— 档位表（字节/秒）在 src/ 的
    // `DownloadSpeedLimit.hpp` 里（唯一出处；ui/ 不许 include src/）。项由 MainWindow 灌
    // （每项 itemData 是 qint64 的字节/秒，0 = 不限）。
    static QString downloadSpeedLimitComboName() {
        return QStringLiteral("downloadSpeedLimitCombo");
    }
    /// 这一行的行标题（左列那一格）。
    static QString downloadSpeedLimitRowText() { return QStringLiteral("速度上限"); }

    // ---------- 「代理」（2026-10-18 加的，§9.2，在「下载源」那一组里） ----------
    // 三档（跟随系统 / 直连 / 手动）的语义和 `host:port` 怎么解析在 src/ 的
    // `DownloadProxy.hpp`（唯一出处；ui/ 不许 include src/）。下拉框**只有框没有项**，
    // 项由 MainWindow 灌；手动那一格只在「手动」档露出来。
    static QString downloadProxyComboName() { return QStringLiteral("downloadProxyCombo"); }
    static QString downloadProxyManualEditName() { return QStringLiteral("downloadProxyManualEdit"); }
    /// 这一行的行标题（左列那一格）。
    static QString downloadProxyRowText() { return QStringLiteral("代理"); }

    // ---------- CurseForge API key（2026-09-25 加的，在「下载源」那一组里） ----------
    // CurseForge 那一条路（下载页把来源选成 CurseForge、以及 CurseForge 格式的整合包导入）
    // 都要靠接口：前者搜项目、后者把那份只有编号的清单换成下载地址 —— 而官方接口要 key。
    // 默认走一台不要 key 的社区镜像，这一格是给用户"自己填一份走官方"的退路 —— 所以它是
    // **可留空的一格**，不是必填项（空 = 镜像）。**现在用的是哪一台、为什么换过**，
    // 见 ModpackImport::curseForgeApiBase（2026-09-26 换过一次）。
    static QString curseForgeApiKeyEditName() { return QStringLiteral("curseForgeApiKeyEdit"); }
    /// 这一行的行标题（左列那一格）。
    static QString curseForgeApiKeyRowText() { return QStringLiteral("CurseForge API key"); }
    /// 输入框里的占位文字（空着的时候显示那一句）。
    static QString curseForgeApiKeyPlaceholder() { return QStringLiteral("留空则用公共接口"); }

    // ---------- 「下载新模组时直接放进当前实例」（2026-09-28 加的，在「下载」栏里） ----------
    // 语义的**唯一出处**是 LauncherSettings 里那一节（那是用户原话）。这里只有形状：
    // 一个勾选框 + 一句话说清"勾上是什么、不勾是什么"。
    //
    // NOTE: 那一句话必须把**两种进入方式**都写出来 —— 这一项的名字（"放进当前实例"）只说得
    //       清勾上的那一半，而它真正在管的是"要不要弹保存框"，那件事还得看用户是从哪儿点
    //       进来的。只写一半的话，用户会以为不勾就"什么都不下"。
    static QString downloadAutoIntoInstanceCheckName() {
        return QStringLiteral("downloadAutoIntoInstanceCheck");
    }
    /// 这一行的行标题（左列那一格）。
    static QString downloadAutoIntoInstanceRowText() {
        return QStringLiteral("下载新模组");
    }
    /// 勾选框自己的文字。
    static QString downloadAutoIntoInstanceCheckText() {
        return QStringLiteral("从实例设置里点「下载新模组」时直接放进该实例");
    }
    // ---------- 「下载缺前置时提醒」（2026-10-09 加的，也在「下载」栏里） ----------
    // 语义的**唯一出处**是 LauncherSettings 里那一节（用户原话）。这里只有形状。
    //
    // NOTE: 那一句话要说清**关掉之后是什么样** —— 用户 2026-10-09 把它定死成「什么都不装，
    //       只下用户点的那一版」。只说"关掉提醒"的话，用户会以为前置照装，只是不弹窗。
    static QString warnMissingDependenciesCheckName() {
        return QStringLiteral("warnMissingDependenciesCheck");
    }
    /// 这一行的行标题（左列那一格）。
    static QString warnMissingDependenciesRowText() {
        return QStringLiteral("缺失前置提醒");
    }
    /// 勾选框自己的文字。
    static QString warnMissingDependenciesCheckText() {
        return QStringLiteral("下载模组 / 光影包时，先检查该实例缺不缺前置");
    }
    static QString showGameLogCheckName() { return QStringLiteral("showGameLogCheck"); }
    static QString processPriorityComboName() { return QStringLiteral("processPriorityCombo"); }
    /// 「窗口模式」那个下拉框（小窗 / 最大化 / 全屏）。2026-09-28 加的。
    ///
    /// NOTE: 里面那几项**不由本类填** —— 档位那一侧的枚举（GameWindowMode）在 src/ 里，
    ///       而 ui/ 不许 include src/（分层铁律）。所以这里是**只有框没有项**，项由
    ///       MainWindow 灌（每项的 itemData 存 int(GameWindowMode)）—— 和 processPriorityCombo
    ///       从头就是同一个形状。验证工程想断言"三档都在"要拿 mw_check（那边能看见 MainWindow）。
    static QString gameWindowModeComboName() { return QStringLiteral("gameWindowModeCombo"); }
    static QString gameWindowWidthSpinName() { return QStringLiteral("gameWindowWidthSpin"); }
    static QString gameWindowHeightSpinName() { return QStringLiteral("gameWindowHeightSpin"); }
    /// 「外观」那一格那个文本框：条上那行字（2026-09-25 加的）。
    static QString titleTextEditName() { return QStringLiteral("titleTextEdit"); }
    /// 「宽 × 高」中间那个乘号。它也要有名字：验证工程量这三格的几何时得能取到它。
    static QString gameWindowSizeTimesLabelName() { return QStringLiteral("gameWindowSizeTimes"); }
    /// 「启动选项」那一组里那几个勾选框/数字框的文案。
    /// 「显示游戏日志」这个串是**用户原话**（2026-09-24 提的需求），别改写成别的说法。
    static QString showGameLogCheckText() { return QStringLiteral("显示游戏日志"); }
    /// 这一行的行标题（左列那一格）。★ 2026-09-26：它原来就是上面那句勾选框文字 ——
    /// 于是同一句话在界面上并排出现两遍（左列一句、勾选框上一句）。这一组里另外两条
    /// 勾选框行是"短标题 + 长句子"（「重复启动」/「进度提醒」），这里照那个形状走。
    /// **勾选框那一句一个字都不许动**（用户原话，ui_check Z2 钉着），改的是行标题。
    static QString showGameLogRowText() { return QStringLiteral("游戏日志"); }

    // ---------- 「启动提醒」那一组（2026-09-25 加的） ----------
    // 两个勾选框各管一条启动前的确认，默认都是勾上的（理由见 LauncherSettings 那一节）。
    // 行标题与勾选框文字**只此一处** —— 界面、验证工程、提示框上那句小字都引这里。

    /// 第一条：十秒内用同一个身份再启动同一个实例。
    static QString warnDuplicateLaunchCheckName() {
        return QStringLiteral("warnDuplicateLaunchCheck");
    }
    /// 这一行的行标题（左列那一格）。
    static QString warnDuplicateLaunchRowText() { return QStringLiteral("重复启动"); }
    /// 勾选框自己的文字。
    static QString warnDuplicateLaunchCheckText() {
        return QStringLiteral("十秒内重复启动同一实例时先问一句");
    }

    /// 第二条：这个实例已经有进程在跑。
    static QString warnWorldLossCheckName() { return QStringLiteral("warnWorldLossCheck"); }
    static QString warnWorldLossRowText() { return QStringLiteral("进度提醒"); }
    static QString warnWorldLossCheckText() {
        return QStringLiteral("同一实例已在运行时提醒可能丢进度");
    }
    static QString versionNameCustomCheckName() {
        return QStringLiteral("versionNameCustomCheck");
    }
    static QString versionNameRealCheckName() { return QStringLiteral("versionNameRealCheck"); }
    static QString versionNameLoaderCheckName() { return QStringLiteral("versionNameLoaderCheck"); }
    static QString versionNameLoaderVersionCheckName() {
        return QStringLiteral("versionNameLoaderVersionCheck");
    }

    // ---------- 内存分配的取值范围 ----------
    // 和 GlobalJavaSettings 的钳位保持一致（那边 setXmx 最少 256、setXms 最少 128），
    // 否则界面允许填的值会被后端悄悄改掉，用户看到"填了不生效"。
    static int memoryMaxMinMb() { return 256; }
    static int memoryMinMinMb() { return 128; }
    static int memoryLimitMaxMb() { return 65536; }
    static int memoryStepMb() { return 256; }

    /// 这台机器的物理内存总量（MB）→「最大内存」那一格实际允许拖到的上限。
    ///
    /// NOTE: **两处都要按它收口** —— 设置页那一行（MainWindow::applyMemoryLimits）和实例
    ///       设置里那一行（InstanceSettingsDialog）。从前只有设置页收口，实例页的上限一直是
    ///       写死的 65536，用户能给一个实例填出比整机内存还大的数，而 JVM 那边只会
    ///       "内存不足"地起不来（见 2026-09-27 审计）。
    ///
    /// NOTE: 条上的档位和数字框的步长都是 memoryStepMb() ⇒ 上限也**往下取到它的整数倍**：
    ///       一台物理内存 16257 MB 的机器上，上限写 16257、条拖到头只能停在 16128（档位表最后
    ///       一格），而数字框里打得进 16257 —— 同一件事两个数，用户只会觉得是坏的。
    ///
    /// NOTE: `totalMb <= 0`（读不到系统内存）时给内置上限，**不能**当成"这台机器没有内存"。
    ///       反过来总数小得离谱时至少留"一格"（下限），不然 min > max 会让 qBound/setRange
    ///       取到没意义的值。
    static int memoryCapForTotalMb(qint64 totalMb) {
        int capMb = memoryLimitMaxMb();
        if (totalMb > 0) {
            const qint64 aligned = (totalMb / memoryStepMb()) * memoryStepMb();
            capMb = aligned < capMb ? int(aligned) : capMb;
        }
        return capMb < memoryMaxMinMb() ? memoryMaxMinMb() : capMb;
    }

    // ---------- 游戏窗口大小的取值范围 ----------
    // 规矩同上：和 LauncherSettings::gameWindowMinWidth() 那一组**必须一样**（那是
    // 读盘时的钳位，这边是界面允许填的范围）。ui/ 这一侧不许 include src/，
    // 所以两份是各写一遍的 —— 改一边就得改另一边，验证工程里有一条断言盯着它们相等。
    //
    // 上限不是随手定的：7680×4320 是 8K，比这个还大的游戏窗口没有意义（而且 -width 是
    // 交给游戏自己的两个整数，给个天文数字只会让某些版本的窗口创建直接失败）。
    static int windowMinWidth() { return 320; }
    static int windowMaxWidth() { return 7680; }
    static int windowMinHeight() { return 240; }
    static int windowMaxHeight() { return 4320; }
    /// 点上下箭头一下走多少。16 是"拖一下就能从 854 走到 1920 那类常见值"。
    static int windowSizeStep() { return 16; }

    // ---------- 「外观」那格那个文本框的长度上限 ----------
    // 和 LauncherSettings::titleTextMaxLength() **必须一样**（那边是读盘/写盘时的截断，
    // 这边是输入框允许打进去的长度）。ui/ 这一侧不许 include src/，所以两份各写一遍，
    // 验证工程里有一条断言盯着它们相等 —— 和上面「游戏窗口大小的取值范围」同一个约定。
    // 不一致的症状是"打上去的字自己少了几个"。
    //
    // 16 这个数是按**条上留给名字的那点地方**量的：条宽 900 时，名字从最左边画到导航那一组
    // 左边还剩 260 px 上下，16 个汉字（约 224 px）正好不撞上（Latin 的 16 个字符更窄）。
    // 再长的名字不会把条撑坏（`TitleBar::paintEvent` 会拿省略号截），但那属于"写了也看不见"。
    static int titleTextMaxLength() { return 16; }

    // ---------- 「背景图」那一节（2026-09-28 加的） ----------
    // 用户原话：「能给主页左侧自定义图像吗？其他页面的背景能换图吗」。两块底各一行：
    // 主页左栏（`Theme::Part::HomeLeft`）+ 四个页面的内容区（`Theme::Part::PageBackground`）。
    // **没设图时一个像素都不改**，所以这一节可以一直不用（见 ui/ThemeBackground.hpp）。
    //
    // NOTE: 一行里那几颗控件的名字以 **partKey 打头**（`homeLeftImageChoose`）—— 认这一行管的是
    //       哪一块靠的是那个串，不是行的下标（下标只是"第几行"）。

    /// 一行里那几颗控件的名字。参数是 `Theme::partKey()` 的结果（"homeLeft" / "pageBackground"）。
    /// **这六个是唯一出处**：验证工程要取这几颗控件时走这里，别照字面再拼一遍。
    static QString themeImageChooseName(const QString &partKey) {
        return partKey + QStringLiteral("ImageChoose");
    }
    static QString themeImageClearName(const QString &partKey) {
        return partKey + QStringLiteral("ImageClear");
    }
    static QString themeImageFitName(const QString &partKey) {
        return partKey + QStringLiteral("ImageFit");
    }
    static QString themeImageOpacityName(const QString &partKey) {
        return partKey + QStringLiteral("ImageOpacity");
    }
    /// 滑块右边那个「100%」—— **不放进滑块自己**（QSlider 画不了文字）。
    static QString themeImagePercentName(const QString &partKey) {
        return partKey + QStringLiteral("ImagePercent");
    }
    /// 底下那行"现在铺的是哪一张"（没选图时是 `themeImageNoneText()`）。
    static QString themeImageStateName(const QString &partKey) {
        return partKey + QStringLiteral("ImageState");
    }

    /// 两颗按钮上的字。
    static QString themeImageChooseText() { return QStringLiteral("选择图片…"); }
    static QString themeImageClearText() { return QStringLiteral("清除"); }
    /// 那一行状态文字在"还没选图"时说的话。
    static QString themeImageNoneText() { return QStringLiteral("还没选图。"); }
    /// 滑块右边那个百分号。**只有这一处**：数字是空的、百分号跟着一起空。
    static QString themeImagePercentText(int opacity) {
        return QStringLiteral("%1%").arg(opacity);
    }
    /// 摆法那个下拉框的宽（装得下「铺满（裁剪）」这四个字再加一个箭头）。
    static int themeImageFitComboWidth() { return 140; }
    /// 不透明度那根条多长。再长就把「不透明度」那四个字挤到下一行了（一行里已经有
    /// 两颗按钮 + 一个下拉框）。
    static int themeImageOpacityWidth() { return 150; }
    /// 滑块右边那个百分数占多宽（三位的「100%」量出来的），免得拖着拖着整行跟着抖。
    static int themeImagePercentWidth() { return 44; }
    /// 一行里两颗控件之间那点缝。和 `kWindowSizeRowSpacing` 是一回事：一句标签和它管着的
    /// 那颗控件要挨近些，两组之间才分得开。
    static int themeImageRowSpacing() { return 8; }
    /// 一行里两组之间那道更宽的缝。
    static int themeImageGroupSpacing() { return 16; }

    // ---------- 「主题」那一节（2026-10-13 加的，coding plan §8.3） ----------
    //
    // 用户 2026-09-28 立的底：「各个地方的背景等允许切换……先不急着做入口，把 ui 框架改成
    // 能高度个性化定义的状态」。框架（`ui/Theme` 的 `Spec` / `Role` / `Part`）铺了半个多月，
    // 这一节就是那个"做入口"：**配色（跟随系统 / 浅色 / 深色）+ 主题色 + 文字不透明度 +
    // 四个语义色**，外加恢复默认与导入/导出。
    //
    // ★ 这一节只搭控件：**改完立刻生效也立刻落盘**（那三步在 `MainWindow::applyThemeSpec()`）。
    //   `ui/` 不许 include `src/`，所以没有任何一项的语义写在这里。
    // ★ 那几个"色块"是**带自己样式表的 QPushButton**（应用级那份只对"素"按钮生效，
    //   控件自己挂的样式表赢）—— 面上就是它现在那个色。键盘焦点也照常给（默认 StrongFocus）。

    /// 配色那个下拉框（跟随系统 / 浅色 / 深色）。**项在这一层灌**（`Theme::presetIds()` ——
    /// 预设这个枚举本来就在 ui/），itemData 存的是预设 id 那个串。
    static QString themePresetComboName() { return QStringLiteral("themePresetCombo"); }
    /// 主题色那块色（点一下弹调色板）和它右边那颗「恢复默认」。
    static QString themeAccentButtonName() { return QStringLiteral("themeAccentButton"); }
    static QString themeAccentResetName() { return QStringLiteral("themeAccentResetButton"); }
    /// 文字不透明度那根条 + 它右边那个百分数。
    static QString themeTextOpacitySliderName() { return QStringLiteral("themeTextOpacitySlider"); }
    static QString themeTextOpacityPercentName() { return QStringLiteral("themeTextOpacityPercent"); }
    /// 四个语义色的色块（下标见 `themeSemanticKey()`）。
    static QString themeSemanticButtonName(int index) {
        return QStringLiteral("themeSemantic") + themeSemanticKey(index);
    }
    /// 那一颗「跟随预设」（把四个语义色一起清掉）。
    static QString themeSemanticResetName() { return QStringLiteral("themeSemanticResetButton"); }
    /// 一整节的「恢复全部默认」+ 导入/导出。
    static QString themeResetAllButtonName() { return QStringLiteral("themeResetAllButton"); }
    static QString themeExportButtonName() { return QStringLiteral("themeExportButton"); }
    static QString themeImportButtonName() { return QStringLiteral("themeImportButton"); }
    /// 「动效」那个勾（§8.3 后半句：总开关继续保留 + 尊重系统的"减少动态效果"）。
    static QString animationsCheckName() { return QStringLiteral("animationsCheck"); }

    /// 语义色一共几个（危险 / 警告 / 正常 / 报错）。三个地方共用这一个数：这一节搭控件、
    /// `MainWindow` 灌色、验证工程数按钮。
    static int themeSemanticCount() { return 4; }
    /// 第 @p index 个语义色在 `Theme::Spec` 里是哪一位（"danger" / "warning" / "ok" /
    /// "errorText"）。**和 `Theme::accentSettingKey()` 那一套同名** —— 认的是它，不是下标。
    static QString themeSemanticKey(int index);
    /// 界面上那颗色块旁边写什么（「危险」/「警告」/「正常」/「报错」）。
    static QString themeSemanticText(int index);
    /// 那一行左列的行标题。
    static QString themePresetRowText() { return QStringLiteral("配色"); }
    static QString themeAccentRowText() { return QStringLiteral("主题色"); }
    static QString themeTextOpacityRowText() { return QStringLiteral("文字不透明度"); }
    static QString themeSemanticRowText() { return QStringLiteral("语义色"); }
    static QString themeResetAllText() { return QStringLiteral("恢复全部默认"); }
    static QString themeExportText() { return QStringLiteral("导出主题…"); }
    static QString themeImportText() { return QStringLiteral("导入主题…"); }
    static QString themeAccentResetText() { return QStringLiteral("恢复默认"); }
    static QString themeSemanticResetText() { return QStringLiteral("跟随预设"); }
    static QString animationsCheckText() { return QStringLiteral("界面动效"); }
    /// 文字不透明度那个百分数（和背景图那一行同一套写法）。
    static QString themePercentText(int opacity) { return QStringLiteral("%1%").arg(opacity); }
    /// 色块那颗按钮多大（宽 × 高）。**和输入框那一档 24 一样高**（并排摆着才齐），宽取 64 ——
    /// 够看出是个什么色，又不至于把一行撑开。
    static int themeSwatchWidth() { return 64; }
    static int themeSwatchHeight() { return 24; }
    /// 「配色」那个下拉框的宽（装得下「跟随系统」再加一个箭头）。和背景图那一档同一个量级。
    static int themePresetComboWidth() { return 160; }
    /// 一行里两颗控件之间那点缝。和 `themeImageRowSpacing()` 是一回事（8）。
    static int themeSwatchRowSpacing() { return 8; }
    /// 一行里两组之间那道更宽的缝（16，同上）。
    static int themeSwatchGroupSpacing() { return 16; }
    /// 「整份主题」那一行的行标题（恢复默认 / 导出 / 导入三颗按钮都归它）。
    static QString themeFileRowText() { return QStringLiteral("整份主题"); }
    /// 文字不透明度那根条多长（和背景图那一根同一个量级）。
    static int themeOpacityWidth() { return 180; }
    static int themePercentWidth() { return 44; }

    // ---------- 「已安装版本」下拉框的占位文案 ----------
    // 空状态有两种，由 MainWindow::refreshInstalledVersions() 按当前情形切换：
    //   NoInstalledVersion —— 磁盘上一个装好的版本都没有
    //   ChooseVersion      —— 有可选版本，但用户还没选（含"上次启动的版本已经没了"）
    // 两个串都必须非空：空串会让 QComboBox 在往里灌数据时静默选中第 0 项，
    // 见本文件 .cpp 里的说明。放在这里当唯一出处，免得界面初始值和 MainWindow
    // 刷新时的值各写一份、慢慢就写歪了。
    static QString noInstalledVersionPlaceholder() { return QStringLiteral("暂无已安装版本"); }
    static QString chooseVersionPlaceholder() { return QStringLiteral("请选择一个版本"); }

    // ---------- 底栏左下角那颗状态方块（2026-09-24 加的） ----------
    /// 那一格最左边那颗小方块的状态。
    ///
    /// 用户原话（2026-09-24）：「左下角没有实例的时候显示（红色的）■（默认的黑色）当前没有
    /// 运行中的实例，启动的时候那个符号（也可以画图代替）变成黄色，文本为版本名（没有就
    /// 版本号-加载器名称，不受名字显示设置影响）启动中... 启动以后符号变成绿色，和原来一样」。
    ///
    /// 「（默认的黑色）」说的是**文字**的颜色 —— 那两行字一直用调色板的默认正文色，
    /// 这一批一个字都没改（别顺手给哪一档状态把字也染上色）。
    ///
    /// ★ 2026-10-15 用户改的**只有红那一档**：「黄和绿不变，红色改有实例报错，没有实例用
    ///   灰色」。于是"一个实例都没在跑"从红退成灰，红腾出来专说"出过事"：
    ///   - 灰 = 没实例、也没欠着什么事（`Idle`）；
    ///   - 红 = **账上还有没处理的异常退出**（`Error`，见 `runStateErrorText()`）——
    ///     判据那一头是 `CrashReportController::pendingRecordIds()`（任务中心里那一项还在，
    ///     用户把它清掉之后红自己就没了），**不是**"此刻有没有进程在跑"。
    ///
    /// NOTE: 红这下说的是**已经发生过**的事，和"此刻在不在跑"是两个维度 —— 所以它不是
    ///       "灰的升级版"，两份东西可以同时为真（有实例跑着、账上又欠着一笔）。那时候按
    ///       §底栏那条优先级出红：出过事比"还在跑"更需要被看见。
    enum class RunStateMark {
        Idle,        ///< 灰：一个实例都没在跑，也没有没处理的异常退出
        Launching,   ///< 黄：正在启动（进程起来了、游戏还没出话，见那一层的 m_launchingSessions）
        Running,     ///< 绿：有实例在跑，而且没有没处理的异常退出
        Error,       ///< 红：有实例异常退出（账上那一项还没处理）
    };

    /// 那颗方块的边长（像素）。取 10 而不是别的图标那个 16：它挨着的是一行正文，
    /// 16 就长得跟字一样高、看着像又一个图标，而不是"这一格的状态点"。
    static int runStateMarkSize() { return 10; }
    /// 方块与第一行文字之间的间距（像素）。
    static int runStateMarkSpacing() { return 6; }
    /// 方块（加上右边那点间距）占掉的宽度。**两行文字都要减掉它**（第一行是因为方块跟它
    /// 同一行，第二行是因为它要缩进同样多才跟第一行对得齐）—— 见
    /// bottomBarRunStateTextWidth() 那条注释。
    static int bottomBarRunStateMarkWidth() { return runStateMarkSize() + runStateMarkSpacing(); }
    /// 那颗方块的 objectName（给验证工程按名字找控件用）。
    static QString runStateMarkName() { return QStringLiteral("runStateMark"); }
    /// 各档各自的颜色。深浅两套的判据走 paletteIsDark()：红的那一档**直接调
    /// folderDangerColor()** —— 同一个界面上两种红各调各的，迟早有一处会看着是脏的。
    ///
    /// 黄和绿是各挑的两个值（浅底用深一点的、深底用亮一点的，都是为了"在底色上看得见"）。
    /// 只说"红/黄/绿"而没有具体色值是可验的：这三个色相分得开（0 / 45 / 130 那一带），
    /// 验证工程量的就是色相落在哪一段（见 ui_check 的 V 段）。
    ///
    /// ★ 灰那一档**没有色相可言**，所以它不能照那三档量色相：验证工程量的变成"饱和度低到
    ///   一定程度"（灰就是"说不上是什么颜色"）。取色走调色板的"灰掉"那一档
    ///   （`QPalette::Disabled` 的 `WindowText`）—— 深浅两套各自有一个现成的灰，
    ///   不用我们再挑一个数，它也会跟着主题走。
    static QColor runStateMarkColor(RunStateMark state, const QPalette &palette);
    /// 那颗方块画出来的样子。MainWindow 按状态要一张图，取色的规则因此只有一处。
    static QPixmap runStateMarkPixmap(RunStateMark state, const QPalette &palette);

    // ---------- 底栏左下角"实例运行中"那行的正文 ----------
    // 放在这里当唯一出处：MainWindow 拿它填文字，验证工程拿它做断言，两边不会写歪。
    static QString runStateRunningText() { return QStringLiteral("实例运行中"); }
    /// 一个实例都没在跑时那一行写什么。
    ///
    /// NOTE: 2026-09-23 用户要的：「没有实例运行的时候就写实例未运行」（以前是整格藏起来、
    ///       底下空一块）。改成一句话还有个连带的好处 —— 那一格的文字**任何时候都非空**，
    ///       布局上也就没有"从无到有"这一步了（那一步正是"中间那根条突然缩短"的来源，
    ///       见 MainWindowUI.cpp 里 runStateButton 那段）。
    ///
    /// NOTE: 2026-09-24 用户把这一句改成「当前没有运行中的实例」（原来写的是「实例未运行」）。
    ///       左边那颗方块那一批因此有了红的那一档 —— 光看字和别的状态分不出"完全没在跑"，
    ///       一眼看颜色能分出来。**2026-10-15 起这一档的方块是灰的**（红挪去说"出过事"了），
    ///       但那句话一个字没动：它说的仍然是"没实例在跑"这件事本身。
    static QString runStateNotRunningText() { return QStringLiteral("当前没有运行中的实例"); }
    /// 有实例异常退出、而且账上那一项还没处理掉时那一行写什么。
    ///
    /// ★ 用户 2026-10-15 定的是这一句（原话：「有实例异常退出」）。它说的是**发生过的事**，
    ///   不是"现在怎么了"：那一项在任务中心里也有一条（点开能把退出码、日志、错误报告
    ///   一起拿出来），用户把它清掉之后这里的红自己就没了 —— 两处说的是同一笔账，
    ///   不是两件事。
    static QString runStateErrorText() { return QStringLiteral("有实例异常退出"); }
    /// 正在启动时那一行：`<实例名>启动中...`。
    /// @param name 实例名。**不受「版本名格式」那四个开关影响**，由
    ///        MainWindow::launchingStatusName() 走 VersionDisplay::launchStatusName() 算出来
    ///        （有自定义名就只用它，没有就是"版本号-加载器名"）—— 这里不碰那个规则。
    ///
    /// NOTE: 和中间那格的 `launchingStatusText()`（「正在启动 X ...」）**不是同一句话**，
    ///       两句各有一个出处是刻意的：中间那格说的是"正在做的事"（跟下载/等待挤同一格），
    ///       这一句是左下角那格的**状态**（和红/绿两个状态同一处说话）。
    static QString runStateLaunchingText(const QString &name) {
        return QCoreApplication::translate("MainWindowUI", "%1启动中...").arg(name);
    }
    /// 两份以上在跑时第一行写什么。**一份时不用这一句**（那时是上面那句），
    /// 所以这个函数只接 count >= 2 的输入。
    static QString runStateRunningCountText(int count) {
        return QCoreApplication::translate("MainWindowUI", "%1 个实例运行中").arg(count);
    }
    /// 多开时第二行里每一个实例的写法：名字后面缀上这次启动用的身份。
    /// 分开成"名字 +（账号）"是因为多开最要紧的一件事就是"哪个是哪个账号"。
    static QString runStateDetailWithAccount(const QString &name, const QString &account) {
        return QCoreApplication::translate("MainWindowUI", "%1（%2）").arg(name, account);
    }
    /// 实例页那棵树上"这个实例正在跑"的后缀。和上面几条同一个理由：唯一出处。
    static QString instanceRunningSuffixText() {
        return QCoreApplication::translate("MainWindowUI", "（运行中）");
    }

    // ---------- 主页右栏那个 3D 小人下面那句话 ----------
    // 和上面那条同一个理由：MainWindow 拿它填文字，验证工程拿它做断言，
    // 两边共用一个出处，改文案时不会只有一边跟着变。
    /// 没登录（或者登录过但一直没取到过外观）时显示的是程序画的那套默认外观。
    static QString skinHintNotLoggedInText() {
        return QStringLiteral("未登录，显示的是默认外观（经典 Steve）");
    }
    /// 离线账号要到了一张随机外观。
    ///
    /// NOTE: 这一句非有不可：上面那两句都不对 —— skinHintLoadedText() 说的是「正版外观」，
    ///       而这张图跟正版没有任何关系（它是随机挑的）；skinHintNotLoggedInText() 说的是
    ///       「显示的是默认外观」，而这时候屏幕上恰恰不是那张默认外观。
    ///       后面那半句也不是废话：这张图**只在启动器里**显示，游戏里那张脸是游戏自己定的。
    static QString skinHintOfflineLoadedText(const QString &name) {
        return QStringLiteral("%1 的外观（离线账号 · 启动器里随机挑的）").arg(name);
    }
    /// 离线账号那张**官方默认皮肤** —— 名字算得出游戏会给哪一张的时候走这一句
    /// （见 DefaultSkin：那是确定的，不是随机的）。
    ///
    /// NOTE: 这一句和上面那句的差别是**这张图是从哪来的**：随机那句说的是"启动器里挑的"
    ///       （跟游戏没关系），这一句说的是"那个版本的客户端本来就会给他分配的这一张"。
    ///       两张都印同一句话是不行的：随机那张要是写"游戏里就是这张"就是骗人。
    ///       带上版本号也不是废话 —— 换版本这张脸**真的会变**（两套规矩算出来的不是同一张，
    ///       见 DefaultSkin.hpp 文件头），那句话要能解释这件事。
    /// @param versionId 实例的显示名（和启动按钮第二行同一个出处），不是目录名。
    static QString skinHintOfflineDefaultSkinText(const QString &name, const QString &versionId) {
        return QStringLiteral("%1 的外观（离线账号 · %2 的默认皮肤）").arg(name, versionId);
    }
    /// 离线账号**用户自己挑的那张**（「账号与外观」里「换一张…」）。不能借用上面那两句：
    /// 随机那句说的是"启动器里挑的"，默认皮肤那句说的是"跟这个实例走" —— 自己挑的这张
    /// 两样都不对（它是明确的"我要这张"，而且换实例也不变）。
    static QString skinHintOfflineCustomSkinText(const QString &name) {
        return QStringLiteral("%1 的外观（离线账号 · 你自己换的）").arg(name);
    }
    /// 正在去官方要。
    static QString skinHintLoadingText(const QString &name) {
        return QStringLiteral("正在获取 %1 的外观…").arg(name);
    }
    /// 要到了。@param withCape 这人有没有披风（有就在后面缀一句，不然用户会以为是没画出来）。
    static QString skinHintLoadedText(const QString &name, bool withCape) {
        return withCape ? QStringLiteral("%1 的正版外观（含披风）").arg(name)
                        : QStringLiteral("%1 的正版外观").arg(name);
    }
    /// 要不到。**不弹窗**（这是可选的外观功能，见 MainWindow.cpp 里的说明），
    /// 但也不能一声不吭 —— 用户会觉得是画错了。
    static QString skinHintFailedText(const QString &name) {
        return QStringLiteral("取不到 %1 的皮肤，显示的是默认外观").arg(name);
    }

    // ---------- 选项卡下标 ----------
    // 底栏左侧的显示规则要知道"当前是不是在启动界面（主页）"：在主页且选中的就是
    // 正在跑的那个实例时只显示一行，否则加第二行说明是哪个实例。
    static int homeTabIndex() { return 0; }
    /// 「实例」页（实例列表 + 实例设置）的下标。
    ///
    /// ★ 2026-09-28：它是**「实例设置」那一页的家** —— `MainWindow::openInstanceSettings()`
    ///   开页时把选项卡切到这一颗、并把那一页的归属钉在它身上（见 `OverlayHost::openPage()`
    ///   的 `ownerTab`）。不钉的话，从下载页那颗「← 返回实例设置」进来的那一次会把这一页
    ///   记成**下载页的**页面：用户点「实例」看见的是实例列表、那一页收着看不见，而它占着
    ///   覆盖层那一位 ⇒ 此后每次开实例设置都被"已经有一页开着"挡下（用户报的"卡住进不去
    ///   实例设置、还老被拽到下载页"）。
    static int instancesTabIndex() { return 1; }
    /// 「下载」页（版本树 + 安装页那一整套）的下标。
    /// NOTE: 这个值本身产品里没人拿它做判断（是"点选项卡"进去的），但验证工程要
    ///       把它翻到前面 —— 隐藏着的控件不排布局，那种状态下量出来的
    ///       visualItemRect() 一律是空矩形，"子条目排没排进布局"就量不出来了。
    static int downloadTabIndex() { return 2; }
    /// 「设置」页的下标。用来做"第一次切到设置页才扫 Java"这条懒加载。
    static int settingsTabIndex() { return 3; }
    /// 当前显示的选项卡是不是主页（即"启动界面"）。
    bool isHomeTabCurrent() const;

    /// 底栏左下角要不要显示第二行（正在跑的那个实例的名字）。
    /// @param homeTabCurrent 当前是不是停在主页（启动界面）。
    /// @param selectedVersionId "已安装版本"下拉框当前选中的版本号（没选是空串）。
    /// @param runningVersionId 正在运行的版本号（没有实例在跑是空串）。
    /// @param launching 正在启动（见 RunStateMark::Launching）。
    /// @return 启动中**一律** false；否则只有"在主页、且选中的就是正在跑的那个实例"
    ///         这一种情况返回 false。
    ///
    /// 只有这两种情况能省掉名字：用户眼前这个界面说的就是它。其余情况（在别的选项卡
    /// 上，或者下拉框里选着另一个版本）都得靠名字说清楚跑的是谁 —— 尤其是"选中的和
    /// 在跑的不是同一个"这种最容易看错的场面。
    ///
    /// NOTE: `launching` 这一档是 2026-09-24 加的。**启动中必须一律不显示第二行**：
    ///       第一行那时候已经是「<实例名>启动中...」（见 runStateLaunchingText()），
    ///       实例名就在那句话里；第二行再报一遍同一个名字看着就像界面出了故障。
    ///       而且这一档跟"在哪个选项卡""下拉框选的谁"都没关系 —— 那两句问的是
    ///       "正在跑的是不是眼前这个"，启动中时第一行已经把名字说全了。
    ///
    /// 做成静态纯函数是为了能把这几种组合逐条钉住：它是这条规则的唯一出处，
    /// MainWindow::updateBottomBar() 只负责把结果画出来。
    static bool runStateShowsDetail(bool homeTabCurrent, const QString &selectedVersionId,
                                    const QString &runningVersionId, bool launching) {
        if (launching) {
            return false;
        }
        return !(homeTabCurrent && selectedVersionId == runningVersionId);
    }

    /// 底栏中间那格"正在启动"那句文案。**唯一一处**拼它。
    ///
    /// 两个调用点说的是同一件事：startGame() 之前那次 enterSyncWait()，和进程起来之后
    /// updateBottomBar()。两处各写一遍字面量，改了其中一处就会出现"文字在启动的一瞬间
    /// 变一下"。
    static QString launchingStatusText(const QString &displayName) {
        // 这个类不是 QObject（没有 tr()），所以用带上下文的静态形式。
        // NOTE: 上下文写死 "MainWindowUI" 是为了以后真要做翻译时 lupdate 认得这一条。
        return QCoreApplication::translate("MainWindowUI", "正在启动 %1 ...").arg(displayName);
    }

    // 控件访问接口
    QPushButton *getStartButton() const { return startButton; }
    QPushButton *getHomeInstanceSelectButton() const { return homeInstanceSelectButton; }
    QPushButton *getAccountSkinSettingsButton() const { return accountSkinSettingsButton; }
    QPushButton *getAddFolderButton() const { return addFolderButton; }
    // 实例页左边那张列表：一行一个「游戏文件夹」，选中的那一份就是启动器当前用的
    // .minecraft。右边那张**树**是它下面扫出来的实例（按分组归好）。
    QListWidget *getFolderList() const { return folderList; }
    // NOTE: 是 QTreeWidget 不是 QListWidget —— 分组要求两层。一个组都没有的时候
    //       它是平铺的（不插组头），观感和改动前的列表一样。
    QTreeWidget *getInstanceList() const { return instanceList; }
    // 右边那句"当前文件夹：<路径>"。换文件夹时它跟着变 —— 没有它的话，右边那些实例
    // 是从哪一份 .minecraft 里扫出来的就说不清了。
    QLabel *getInstanceFolderLabel() const { return instanceFolderLabel; }

    /// 实例页那个搜索框（coding plan §7.2）。里面那一行就是**筛选条件之一**：谁读它、
    /// 什么时候重灌树，见 `MainWindow::setupConnections` 里那条 `textChanged`。
    QLineEdit *getInstanceSearchEdit() const { return instanceSearchEdit; }
    /// 那一排筛选 chip 里第 @p button 颗。越界回 nullptr（不崩）。
    QPushButton *getInstanceFilterButton(InstanceFilterButton button) const {
        const int index = static_cast<int>(button);
        return index >= 0 && index < instanceFilterButtons.size() ? instanceFilterButtons.at(index)
                                                                  : nullptr;
    }
    /// 那一排 chip 的外框（量"整排有没有超出去"用的）。
    QWidget *getInstanceFilterRow() const { return instanceFilterRow; }
    /// "没有符合筛选条件的实例"那一行字（平时收着，见 `instanceNoMatchText()` 那段）。
    QLabel *getInstanceNoMatchLabel() const { return instanceNoMatchLabel; }

    // ---------- 实例页的模式切换 + 服务端那一页（coding plan §6.1 / M4）----------
    /// 某一颗模式按钮（越界/认不出回 nullptr）。点它换页这件事由 `MainWindow` 接 ——
    /// 这一层只摆控件、只把自己的 `isChecked()` 如实报出去（和筛选 chip 同一个路数）。
    QPushButton *getInstanceModeButton(InstanceMode mode) const {
        const int index = static_cast<int>(mode);
        return index >= 0 && index < instanceModeButtons.size() ? instanceModeButtons.at(index)
                                                                : nullptr;
    }
    /// 正文那一层 `QStackedWidget`（0 = 客户端实例，1 = 服务端，2 = 预设）。
    QStackedWidget *getInstanceModeStack() const { return instanceModeStack; }
    /// 「预设」那一页的宿主（内容由 `PresetController` 塞进来）。
    QWidget *getInstancePresetPage() const { return instancePresetPage; }
    /// 服务端那一页的列表（一行一个服务端）。
    QTreeWidget *getServerList() const { return serverList; }
    /// 「还没有服务端」那句（平时收着）。
    QLabel *getServerEmptyLabel() const { return serverEmptyLabel; }
    /// 「新建服务端…」那一颗。
    QPushButton *getNewServerButton() const { return newServerButton; }
    // 实例页右栏那三个按钮，作用于「实例列表」里当前选中的那一个实例。
    /// 「分组设置…」那一颗 —— 2026-09-25 傍晚起它在「实例列表」那个框**外面**的右上角
    /// （这一页右栏那一排三颗只剩它一颗，见上面那段 NOTE）。
    QPushButton *getGroupInstanceButton() const { return groupInstanceButton; }
    /// 「导入整合包…」。它不看"有没有选中实例"，任何时候都能点（和上面那三个一个口径：
    /// 能不能点由状态决定的话，用户会先花时间猜"为什么它是灰的"）。**它在左栏那一组里**
    /// （「游戏文件夹」组，「添加文件夹…」下面），不在上面那三个那一排。
    QPushButton *getImportModpackButton() const { return importModpackButton; }
    // 两个下拉框，数据源互不相干，**也因此不在一页上**：
    //   installedVersionCombo —— 磁盘上已经装好的（.minecraft/versions/<id>/<id>.jar），
    //                            在「主页」，启动用它
    //   —— 原来下载页那个 availableVersionCombo 没了（2026-09-22 换成版本树 + 安装页，
    //      见下面那一段）：在线版本现在是**树**里的条目，不再是下拉框里的一行。
    QComboBox *getInstalledVersionCombo() const { return installedVersionCombo; }
    // 剩下的两张列表和它们各自的下拉框是同一份数据的两种看法（条目文本也是同一套
    // 显示名），选中项双向同步：点列表 = 在下拉框里选中它，反之亦然。
    //   instanceList —— 实例页右边，见上面 getInstanceList()
    /// 下载页左栏那份「下载分类」（仿 PCL，见上面那一段）。**条目没有 objectName** ——
    /// 认一个条目用的是它的 `Qt::UserRole`（= downloadCategoryKeys() 里那个键）：
    /// 条目文字是显示内容，会随产品文案走，不能拿它当身份。
    QListWidget *getDownloadCategoryList() const { return downloadCategoryList; }

    /// 下载页**左栏顶上**那颗「返回实例设置」（见上面 downloadBackButtonName 那段）。
    ///
    /// NOTE: 2026-09-28 起**整个下载页只有这一颗**（原来是五栏各一颗、摆在右栏那行筛子
    ///       里）。所以它不再按分类键索引 —— 用户翻到哪一栏，它都在同一个地方。
    QPushButton *getDownloadBackButton() const { return downloadBackButton; }

    /// 下载页页头右上角那颗**刷新**（只作用于当前停着的那一类）。
    ///
    /// ★ 2026-10-12 加的（用户那份改版文档：「刷新按钮属于"当前分类"自己的页面头部」）。
    ///   它和分类行行尾那一颗是同一个动作的两个入口，都接
    ///   `MainWindow::refreshDownloadCategory()`；这一颗作用于
    ///   `getDownloadCategoryList()->currentRow()`。
    QPushButton *getDownloadHeaderRefreshButton() const { return downloadHeaderRefreshButton; }

    /// 下载页「服务端」那一栏里那颗「新建服务端…」（2026-10-18）。
    ///
    /// ★ 它和实例页那颗（`getNewServerButton()`）**是同一个动作**：都接
    ///   `MainWindow::openNewServerPage()`。这一颗的存在只是把那条路也摆到下载页上
    ///   （coding plan §1.4：「服务端功能齐全，但下载页那一栏始终没接」）。
    QPushButton *getDownloadNewServerButton() const { return downloadNewServerButton; }

    /// 下载页页头**那一整块**（标题 + 说明 + 右上角动作）。
    ///
    /// ★ 2026-10-18：用户原话「那个页面顶上的大标题和刷新键不要（点进去就隐藏，外面
    ///   还保留）」—— 这里的"那页"是「下载 → 游戏」里面那一层**安装页**（挑加载器那页）。
    ///   它一打开，这一整块就整块收起来（不是只收标题和按钮、留一行空说明）；退回版本树
    ///   或者翻到别的分类，它又露出来。判据只有一处：`syncDownloadHeaderVisibility()`
    ///   （cpp 匿名命名空间），挂在 `downloadStack` / `gameStack` 两发 `currentChanged` 上。
    QWidget *getDownloadHeader() const { return downloadHeader; }
    /// 安装页中间那层滚动区（见 installScrollName() 那段）。
    QScrollArea *getInstallScroll() const { return installScroll; }

    /// 把下载页页头那行说明刷成第 @p row 类的那一句（`downloadHeaderSubtitle()`）。
    ///
    /// ★ 由 `MainWindow` 在"当前分类变了"那一条路上调（那一层才知道分类列表翻到哪儿了，
    ///   见 `MainWindow::setupConnections` 里那条 `currentRowChanged`）。传越界的下标
    ///   什么都不改（页头宁可停在上一句，也不要空成一行）。
    void setDownloadHeaderRow(int row);

    /// 把实例页正文切到 @p mode 那一页（客户端实例 / 服务端）。
    ///
    /// ★ **唯一出处** —— 顶上那两颗按钮的勾、`instanceModeStack` 停在第几页、以及"当前是
    ///   哪一档"这件事，全从这一个函数走。`MainWindow` 在按钮那一路和"记住/恢复模式"那一路
    ///   都调它，别在两处各设一遍（那样两颗按钮的勾和实际停着的页迟早对不上）。
    /// 传越界的值什么都不做（宁可停在上一档，也不要切到一页不存在的页上）。
    void setInstanceMode(InstanceMode mode);

    /// 让它露面/收起来。**收起时连位置都不占**（`setVisible(false)`，不是 `setEnabled`）。
    void setDownloadBackVisible(bool visible) {
        if (downloadBackButton != nullptr) {
            downloadBackButton->setVisible(visible);
        }
    }
    /// 右栏那份 QStackedWidget。**下标和 downloadCategoryKeys() 一一对应** ——
    /// 点第 N 个分类就 setCurrentIndex(N)，这条不变量由界面自己保证（搭的时候就按
    /// 同一个表、同一个顺序加页），验证工程会数一遍两边的条数。
    QStackedWidget *getDownloadStack() const { return downloadStack; }
    /// 设置页左栏那份「分类」与右栏那份堆叠页（2026-09-26 加）。形状与上面那两份一一对应，
    /// 认一个条目的身份同样看 `Qt::UserRole`（= settingsCategoryKeys() 里那个键），
    /// 下标同样和 settingsCategoryKeys() 一一对应。**两个都可能是 nullptr** ——
    /// setupSettingsPage() 里才建。
    QListWidget *getSettingsCategoryList() const { return settingsCategoryList; }
    QStackedWidget *getSettingsCategoryStack() const { return settingsCategoryStack; }
    /// 「游戏」那一页里的两层（版本树 / 安装页）。下标见 downloadGameTreeIndex()
    /// 和 downloadGameInstallIndex()。
    QStackedWidget *getGameStack() const { return gameStack; }
    /// 四条分类 + 版本号那份树。身份读每条的 `Qt::UserRole`（顶层是分类键、子条目是
    /// 版本号，空串 = 这一条不是版本号）—— 见 versionTreeName() 上面那段。
    QTreeWidget *getVersionTree() const { return versionTree; }
    /// 版本树上面那个筛选框（见 versionFilterEditName() 那段）。
    QLineEdit *getVersionFilterEdit() const { return versionFilterEdit; }
    /// 筛选框右边那句结果（空筛选时藏着）。
    QLabel *getVersionFilterHint() const { return versionFilterHint; }
    /// 按筛选框里现在的文字把树过滤一遍。
    ///
    /// ★ **每次重灌版本树之后必须再调一次**（`MainWindow::refreshAvailableVersions` 末尾）：
    ///   框里的字是用户打的，重灌不清它；不重过滤的话，屏幕上是"框里写着 1.20、树里
    ///   却把 742 条全摆出来"，而且没有任何报错。
    void applyVersionFilter();
    /// 「版本名称」框（安装页顶上那个）。
    QLineEdit *getInstallNameEdit() const { return installNameEdit; }
    /// 名称框下面那行灰色小字（平时的说明 / 重名提示，两种取值一处决定）。
    QLabel *getInstallNameHint() const { return installNameHint; }
    /// 名称框底下那行红字（名字不能用时的原因；能用时它空着且藏着）。
    QLabel *getInstallNameError() const { return installNameError; }
    /// 安装页顶上那句"将基于原版 X 安装"。
    QLabel *getInstallTargetLabel() const { return installTargetLabel; }
    /// 安装页那个「安装」按钮。原来那个「下载版本」按钮的位子 —— 它现在只在这一页上，
    /// 而"装什么"由这一页上的名称框 + 加载器那几条决定。
    QPushButton *getInstallButton() const { return installButton; }
    /// 「← 返回版本列表」：回树那一层（不改任何选择，回去还是原来那棵树）。
    QPushButton *getInstallBackButton() const { return installBackButton; }
    /// 安装页那棵「模组加载器」树（见上面那段）。
    ///
    /// NOTE: 界面上那五条**没有 objectName**（条目不是 QObject）：身份读每条的
    ///       `Qt::UserRole`（顶层是下标、子条目是版本号）。"选中了哪一条"这件事
    ///       不在这棵树上 —— 它住在 MainWindow 里（`selectedLoaderIndex()`），
    ///       这一层只管把状态画出来。
    QTreeWidget *getLoaderTree() const { return loaderTree; }

    /// 后面四页（模组/整合包/资源包/光影）那四张资源表，按分类键取（键就是
    /// downloadCategoryKeys() 里那四个之一）。认不出来（比如传 "game"）时返回 nullptr ——
    /// 「游戏」那一页没有这张表，它有它自己那棵树。
    ///
    /// NOTE: 条目的身份也读每条的 `Qt::UserRole`，但**含义按层不同**：第一层是 project_id、
    ///       第二层是游戏版本、第三层是版本号。认层用的是"有没有爹"——和代理里那条
    ///       isChannel 是同一个判据（别用"UserRole 看着像不像版本号"去猜）。
    QTreeWidget *getDownloadResourceTree(const QString &categoryKey) const {
        return downloadResourceTrees.value(categoryKey, nullptr);
    }
    /// 那一页上面那个搜索框（回车就搜，见 downloadSearchHint() 那段）。
    /// 2026-09-25 起它同时是筛子行里的**「名称」那一格**（行的排法见 .cpp 里那一段）。
    QLineEdit *getDownloadSearchEdit(const QString &categoryKey) const {
        return downloadResourceSearches.value(categoryKey, nullptr);
    }
    /// 筛子行里那四个下拉框（按分类键取）。**内容全是空白的** —— 里面装什么由 MainWindow
    /// 灌（来源 = allSources()、类型 = categoryOptions()、加载器 = loaderOptions()，
    /// 见头文件里 downloadFilterVersionComboName() 那段）。
    ///
    /// NOTE: 「模组加载器」那一个**只有模组页有**（别的页回 nullptr）：用户原话
    ///       「模组加载器只在模组那栏存在」。拿它之前先判空 —— 资源包/光影/数据包/整合包
    ///       那四页**压根没有这个控件**，不是"有但是藏着"。
    QComboBox *getDownloadFilterVersionCombo(const QString &categoryKey) const {
        return downloadFilterVersionCombos.value(categoryKey, nullptr);
    }
    QComboBox *getDownloadFilterLoaderCombo(const QString &categoryKey) const {
        return downloadFilterLoaderCombos.value(categoryKey, nullptr);
    }
    QComboBox *getDownloadFilterSourceCombo(const QString &categoryKey) const {
        return downloadFilterSourceCombos.value(categoryKey, nullptr);
    }
    QComboBox *getDownloadFilterTypeCombo(const QString &categoryKey) const {
        return downloadFilterTypeCombos.value(categoryKey, nullptr);
    }
    /// 「类型」那一栏的**整个小组**（标签 + 下拉框）。藏的是它，不是那个下拉框 ——
    /// 只藏框的话会剩一个「类型」两个字孤零零挂在行里，比不藏还费解。
    ///
    /// 什么时候藏：`ResourceCatalog::categoryOptions()` 回空表的时候（那是"这一来源 × 这一类
    /// 根本没有分类可选"的唯一出处，实测只有 Modrinth 的数据包那一档）。摆一个只有「全部」
    /// 一项的框等于骗人。**藏不藏由 MainWindow 说了算** —— 这一层不知道那张表长什么样。
    QWidget *getDownloadFilterTypeBox(const QString &categoryKey) const {
        return downloadFilterTypeBoxes.value(categoryKey, nullptr);
    }
    /// 「搜索」和「清空条件」那两颗键（每一页都有）。按它们干什么见
    /// downloadFilterSearchText() / downloadFilterResetText() 那两段。
    QPushButton *getDownloadFilterSearchButton(const QString &categoryKey) const {
        return downloadFilterSearchButtons.value(categoryKey, nullptr);
    }
    QPushButton *getDownloadFilterResetButton(const QString &categoryKey) const {
        return downloadFilterResetButtons.value(categoryKey, nullptr);
    }
    /// 整合包那一页那颗「导入整合包…」。**别的页回 nullptr**（用户要的是"整合包那一页带一个
    /// 导入功能"，别的页没有这一颗）。它走的路和实例页那颗是**同一条**。
    QPushButton *getDownloadFilterImportModpackButton() const {
        return downloadImportModpackButton;
    }

    // 设置页的控件。**只搭出来、只负责显隐**，值由 MainWindow 灌（它才知道
    // 那些设置存在哪、改了要不要落盘）。
    QPushButton *getJavaManageButton() const { return javaManageButton; }
    QLabel *getJavaPathLabel() const { return javaPathLabel; }
    QCheckBox *getJavaWarnMismatchCheck() const { return javaWarnMismatchCheck; }
    QLabel *getMemoryHint() const { return memoryHint; }
    QSpinBox *getMemoryMaxSpin() const { return memoryMaxSpin; }
    QSpinBox *getHomeFavoriteCountSpin() const { return homeFavoriteCountSpin; }
    QSpinBox *getHomeRecentCountSpin() const { return homeRecentCountSpin; }
    MemorySlider *getMemoryMaxSlider() const { return memoryMaxSlider; }
    QComboBox *getLauncherBehaviourCombo() const { return launcherBehaviourCombo; }
    QPlainTextEdit *getExtraJvmArgsEdit() const { return extraJvmArgsEdit; }
    QPushButton *getApplyExtraJvmArgsButton() const { return applyExtraJvmArgsButton; }
    QComboBox *getDownloadSourceCombo() const { return downloadSourceCombo; }
    QComboBox *getDownloadConcurrencyCombo() const { return downloadConcurrencyCombo; }
    QComboBox *getDownloadSpeedLimitCombo() const { return downloadSpeedLimitCombo; }
    QComboBox *getDownloadProxyCombo() const { return downloadProxyCombo; }
    QLineEdit *getDownloadProxyManualEdit() const { return downloadProxyManualEdit; }
    QLineEdit *getCurseForgeApiKeyEdit() const { return curseForgeApiKeyEdit; }
    QCheckBox *getShowGameLogCheck() const { return showGameLogCheck; }
    QCheckBox *getDownloadAutoIntoInstanceCheck() const { return downloadAutoIntoInstanceCheck; }
    QCheckBox *getWarnMissingDependenciesCheck() const { return warnMissingDependenciesCheck; }
    QCheckBox *getWarnDuplicateLaunchCheck() const { return warnDuplicateLaunchCheck; }
    QCheckBox *getWarnWorldLossCheck() const { return warnWorldLossCheck; }
    QComboBox *getProcessPriorityCombo() const { return processPriorityCombo; }
    QComboBox *getGameWindowModeCombo() const { return gameWindowModeCombo; }
    /// 「宽 × 高」中间那个乘号。**它也要给出来**：MainWindow 按窗口模式灰/亮那一行时要连
    /// 它一起 —— 只灰两个数字框、留一个亮着的 × 在中间，看着像那一格坏了。
    /// （MainWindowUI 不是 QObject，所以是给一个 getter、不是让外面 `findChild`。）
    QLabel *getGameWindowSizeTimesLabel() const { return gameWindowSizeTimesLabel; }
    QSpinBox *getGameWindowWidthSpin() const { return gameWindowWidthSpin; }
    QSpinBox *getGameWindowHeightSpin() const { return gameWindowHeightSpin; }
    QLineEdit *getTitleTextEdit() const { return titleTextEdit; }

    /// 「背景图」那一节里，**一块底**对应的一组控件（主页左栏一组、页面背景一组）。
    ///
    /// NOTE: 这一组是**当值存下来**的（`m_themeImageRows`），外面拿到的是引用 —— 和别处那些
    ///       一个一个 getter 的写法不一样，理由是这五颗控件**只在一起用**（换一张图的动作要
    ///       同时动"现在铺的是哪一张"那行字），拆成五个 getter 只是让每一处调用都写五遍。
    struct ThemeImageRow {
        /// `Theme::partKey()` 的结果（"homeLeft" / "pageBackground"）—— 认这一行管的是哪一块，
        /// 靠的是这个串，不是行的下标。
        QString partKey;
        QPushButton *choose = nullptr;  ///< 「选择图片…」
        QPushButton *clear = nullptr;   ///< 「清除」（没设图时由 MainWindow 灰掉）
        QComboBox *fit = nullptr;       ///< 摆法；itemData 存 `Theme::imageFitKey()`
        QSlider *opacity = nullptr;     ///< 不透明度 0~100
        QLabel *percent = nullptr;      ///< 滑块右边那个「100%」
        QLabel *state = nullptr;        ///< 底下那行"现在铺的是哪一张"
    };

    /// 这一节一共几行（今天 = 主页左栏 + 页面背景两行）。
    static int themeImageRowCount() { return 2; }
    /// 第 @p index 行；越界给一个**空行**（那几个指针都是 nullptr），调用方自己判空。
    const ThemeImageRow &getThemeImageRow(int index) const;

    /// 「主题」那一节那几颗（见上面那一节的说明）。色块都允许返回 nullptr（没建出来时）。
    QComboBox *getThemePresetCombo() const { return themePresetCombo; }
    QPushButton *getThemeAccentButton() const { return themeAccentButton; }
    QPushButton *getThemeAccentResetButton() const { return themeAccentResetButton; }
    QSlider *getThemeTextOpacitySlider() const { return themeTextOpacitySlider; }
    QLabel *getThemeTextOpacityPercent() const { return themeTextOpacityPercent; }
    /// 第 @p index 个语义色的色块；越界给 nullptr。
    QPushButton *getThemeSemanticButton(int index) const;
    QPushButton *getThemeSemanticResetButton() const { return themeSemanticResetButton; }
    QPushButton *getThemeResetAllButton() const { return themeResetAllButton; }
    QPushButton *getThemeExportButton() const { return themeExportButton; }
    QPushButton *getThemeImportButton() const { return themeImportButton; }
    /// 「动效」那个勾。
    QCheckBox *getAnimationsCheck() const { return animationsCheck; }

    QCheckBox *getVersionNameCustomCheck() const { return versionNameCustomCheck; }
    QCheckBox *getVersionNameRealCheck() const { return versionNameRealCheck; }
    QCheckBox *getVersionNameLoaderCheck() const { return versionNameLoaderCheck; }
    QCheckBox *getVersionNameLoaderVersionCheck() const { return versionNameLoaderVersionCheck; }

    // 主页右栏那个可拖动旋转的 3D 皮肤预览；说明标签保留作状态/无障碍数据但不再显示。
    // 这个控件是**自己画的**（QWidget 子类 + paintEvent，不是标准控件），
    // 所以它比别处多一层：界面只管把贴图喂进去，贴图从哪来是业务层的事。
    SkinViewer *getSkinViewer() const { return skinViewer; }
    QLabel *getSkinHintLabel() const { return skinHintLabel; }
    QLabel *getHomeAccountLabel() const { return homeAccountLabel; }
    QLabel *getHomeAvatarLabel() const { return homeAvatarLabel; }
    QLabel *getHomeAccountTypeLabel() const { return homeAccountTypeLabel; }
    /// 主页右上那张账号卡片（头像 + 名字 + 「⌄」）。**2026-10-16 起它是账号那个入口**：
    /// 点一下在卡片**下面**展开「最近用过的账号」那一块面板（见 `toggleAccountsPanel()`）。
    QPushButton *getAccountSkinButton() const { return accountSkinButton; }

    /// 点一下主页右上那张账号卡片：把「最近用过的账号」那块面板挂在卡片下面开/收。
    ///
    /// ★ 2026-10-16（用户原话：「这个的对应功能继承到右上角那个按钮去」）：底栏那颗账号
    ///   按钮被 `hide()` 之后（账号身份整个搬到主页右栏），那一块面板就**换锚点**到了这张
    ///   卡片上 —— 面板本身（行、行尾那颗「登录/登出」、末行「账号与外观」）一个字没改，
    ///   变的只是"点哪里打开它"和"它往哪个方向弹"。
    void toggleAccountsPanel() {
        toggleBottomBarPanel({accountSkinButton, BottomBarAnchorKind::Accounts, /*dropDown=*/true});
    }

    QTabWidget *getTabWidget() const { return tabWidget; }

    /// 内容区上面那一层。**设置界面和各种提示的唯一去处** —— 业务层要开一个"整页"或者
    /// 问一句，一律走它，别再 `new` 一个对话框出来（见 OverlayHost.hpp 开头那段）。
    OverlayHost *overlay() const { return overlayHost; }

    /// 顶上那条自己画的标题栏（草方块 + 那五个字母 + 四颗导航按钮 + 最小化/关闭）。
    /// **它不在覆盖层的范围里** —— 覆盖层挂在 tabWidget 上，条在 tabWidget 上面那一行
    /// （见 setupUi 里的 addWidget 顺序）。四颗导航按钮就是"四个功能"的入口，标签栏
    /// 自己藏起来了（`setupUi` 里 `tabWidget->tabBar()->hide()`）。
    TitleBar *titleBar() const { return bar; }
    /// 外框那一份（把系统的标题栏去掉、客户区铺满整窗、条上哪一片能拖）。`MainWindow::nativeEvent()`
    /// 转给它的 handleNativeEvent()，`setupUi` 之外没有任何人碰它。
    WindowChrome *chrome() const { return windowChrome; }
    // NOTE: 产品代码没有取过底栏本身（只取它里面的几个标签/进度条），只有验证工程
    //       用它来量底栏的几何。
    QFrame *getBottomBar() const { return bottomBar; }
    /// 底栏那几颗按钮（左：实例运行状态 · 中：任务摘要 · 右：工具箱）。点它们会**向上**弹出
    /// 一块面板（左那块列在跑的实例、中间那块是任务中心、右那块是工具箱菜单），内容由
    /// MainWindow 灌进来（见 setBottomBarPanelContent()）。
    QPushButton *getRunStateButton() const { return runStateButton; }
    /// 底栏右头那一格（头像 + 用户名 + 「⌃」）。★ 2026-10-16 起它**不摆出来了**
    /// （`setupBottomBar()` 里 `hide()`）：账号身份与入口整个搬到主页右上那张卡片，
    /// 这里只留下这一组控件供业务层继续灌数据（`updateBottomBar()` 那两行文字）。
    /// 账号面板现在的锚点是 **`getAccountSkinButton()` 那张卡片**。
    QPushButton *getAccountButton() const { return accountButton; }
    QPushButton *getToolboxButton() const { return toolboxButton; }
    /// 底栏中间那一格（2026-10-12 起它也是一整颗按钮：点一下向上展开任务中心）。
    QPushButton *getTaskSummaryButton() const { return taskSummaryButton; }
    /// 那格右边那句小字（「另有 N 项」）。**空着的时候它自己就收起来了**（布局里宽度 0）。
    QLabel *getTaskExtraLabel() const { return taskExtraLabel; }
    QLabel *getAvatarLabel() const { return avatarLabel; }
    QLabel *getAccountLabel() const { return accountLabel; }
    QLabel *getAccountTypeLabel() const { return accountTypeLabel; }
    // 进度只出现在底栏中间那一格，不进日志（旧版是钉在 QStatusBar 上的）
    QProgressBar *getProgressBar() const { return progressBar; }
    /// 那根条右边那一格里的百分比（「42%」）。测不出百分比时它是**藏着的**（和条一起）。
    QLabel *getTaskPercentLabel() const { return taskPercentLabel; }
    QLabel *getStatusLabel() const { return statusLabel; }
    // 底栏左侧：第一行是状态正文（它左边还有一颗三态的小方块），第二行是实例名
    // （只在需要区分时显示）
    QLabel *getRunStateLabel() const { return runStateLabel; }
    QLabel *getRunStateDetailLabel() const { return runStateDetailLabel; }
    // 那颗三态的小方块。**图的颜色由 MainWindow 按状态来要**（runStateMarkPixmap），
    // 这里只是个装图的格子 —— 它自己不知道现在是哪个状态。
    QLabel *getRunStateMarkLabel() const { return runStateMarkLabel; }

private:
    void setupUi(QMainWindow *parent);
    /// 搭设置页：左栏一列分类（启动/游戏/提示/个性化/下载）、右栏一份堆叠页，两边下标
    /// 一一对应（见头文件上面「设置页：左栏那几条分类」那段）。每个分栏里才是那张 grid
    /// （3:7，行是上下结构），见 addSettingsPanel()。
    void setupSettingsPage(QWidget *page);
    /// 设置页那五个分栏的**内容**（各自往堆叠页里加一页，顺序就是 settingsCategoryKeys()
    /// 的顺序）。拆成五个函数只为一件事：`row` 那个行号计数本来就是"每个分栏各数各的"
    /// （从前是十个花括号块顺序往下数），一个函数一个分栏之后那件事就不言自明了。
    /// 每个分栏都是"自己的滚动区 + 自己的表格"，页面从 addSettingsPanel() 拿。
    void setupSettingsLaunchPanel(QStackedWidget *stack);
    void setupSettingsGamePanel(QStackedWidget *stack);
    void setupSettingsWarnPanel(QStackedWidget *stack);
    void setupSettingsLookPanel(QStackedWidget *stack);
    void setupSettingsDownloadPanel(QStackedWidget *stack);
    /// 搭底部那条通栏状态条（左：实例运行状态，中：进度，右：头像/用户名 ——
    /// 2026-09 把左右两格对调过，见 .cpp 里的说明）。
    void setupBottomBar(QWidget *parent, QVBoxLayout *mainLayout);
    /// 行尾那颗齿轮被点了：把菜单贴在那颗按钮上弹出来（或者**收回去** —— 已经弹着、
    /// 而且就是这一颗弹的时候，再点一下是收回，和底栏那两块面板一个路数）。
    void openFolderMenu(const FolderRow &row, QWidget *gear);

    /// 底栏那三格里的一格：是哪一颗按钮（**按按钮指针认人**，不按"左右"）、点开的是
    /// 哪一块面板。三块面板的形状完全不同，所以这里是**一个枚举**而不是两个布尔量。
    enum class BottomBarAnchorKind {
        None,       ///< 没落在任何一颗按钮上
        Instances,  ///< 左下那格：列正在跑的实例（`BottomBarPanel`）
        Accounts,   ///< 右下那格：列最近用过的账号（`BottomBarPanel`）
        Tasks,      ///< 中间那格：任务中心（`TaskCenterPanel`，2026-10-12 加）
        Tools,      ///< 右下那格：工具箱轻量菜单
    };
    struct BottomBarAnchor {
        QPushButton *button = nullptr;
        BottomBarAnchorKind kind = BottomBarAnchorKind::None;
        /// 面板挂在锚点**下面**（往下弹），不是上面。只有一档是 true：主页右上那张
        /// 账号卡片 —— 它本身就贴着头顶，往上弹只会跑到屏幕外面去（见 BottomBarPanel::popUp()）。
        /// ★ 2026-10-16（用户原话：「这个的对应功能继承到右上角那个按钮去」）：底栏那颗
        ///   账号按钮 `hide()` 之后，账号面板换到主页卡片上，方向也就跟着翻了过来。
        bool dropDown = false;
        bool account() const { return kind == BottomBarAnchorKind::Accounts; }
    };
    /// 底栏那三颗按钮"点开 / 收回"的唯一出处，三种情形都在这里分：
    /// 已经开着**同一颗**的 → 收回（不再开）；开着**另一颗**的 → 收旧的、开新的；
    /// 都没开着 → 开。**每一次点击都算数**（点另一颗那一下是面板收到的，见 .cpp 里
    /// BottomBarPanel::handleOutsidePress() 的第一段 NOTE）。
    ///
    /// NOTE: "现在开着哪一块"对中间那格要**另外问一句**（`taskCenterPanel()`）——
    ///       两块 `BottomBarPanel` 都叫 `bottomBarPanel` 那一个名字，任务中心那块是
    ///       另一个类、另一个名字，两者不在同一个 findChild 里。
    void toggleBottomBarPanel(const BottomBarAnchor &anchor);
    void toggleToolboxPanel();
    /// 收掉工具箱那一块上拉面板（没开着就什么都不做）。
    ///
    /// ★ 两条路都走它：「再点一次那颗扳手 = 收回」和「面板里点了某一件工具 = 先收掉再把
    ///   那一件交给 handler」。面板是 `WA_DeleteOnClose` 的（**不存指针，一律现找**），
    ///   所以"收"这件事只有这一处说得出口，别在别处再写一遍 `findChild`。
    void closeToolboxPanel();
    /// 把 @p anchor 那块面板弹出来。**"收"的那一半在 toggleBottomBarPanel()**：
    /// 这个函数只负责开，不管现在开着什么。
    void openBottomBarPanel(const BottomBarAnchor &anchor);
    /// 开任务中心那一块（中间那格用的；它和上面那个不是同一个类，见 toggle 的 NOTE）。
    void openTaskCenterPanel();
    /// 现在开着的任务中心面板（没开着 = nullptr）。它是个 Qt::Popup 窗口，close() 之后
    /// 要等 deleteLater 才真删 —— 所以同一轮事件处理里还找得到（和 `bottomBarPanelName()`
    /// 那两个 findChild 是同一个路数，见 toggleBottomBarPanel()）。
    TaskCenterPanel *taskCenterPanel() const;
    /// @p globalPos（全局坐标）落在底栏哪一颗按钮上。没落在任何一颗上 = 返回的 kind 是 None。
    BottomBarAnchor bottomBarAnchorAt(const QPoint &globalPos) const;

    // 控件指针
    //
    // NOTE: 全部显式初始化成 nullptr（2026-09-24 加的）。这不是为了好看：下面那些
    //       访问器和 isHomeTabCurrent() 拿"空指针"当"setupUi() 还没跑完"的判据 ——
    //       成员没有初值时，那个判据读的是**未初始化的值**（UB），只是现在的调用点
    //       全都排在 setupUi() 之后才没炸。
    // NOTE: 新加控件指针时**一并写上 = nullptr**，别让它单独破例。
    QPushButton *startButton = nullptr;
    QPushButton *homeInstanceSelectButton = nullptr;
    QWidget *homeLaunchArea = nullptr;
    QWidget *homeHeroArea = nullptr;
    QWidget *homePickerHost = nullptr;
    QWidget *homeProfileCard = nullptr;
    QWidget *homeRightColumn = nullptr;
    QSpacerItem *homeLaunchSpacer = nullptr;
    QWidget *homeQuickSection = nullptr;
    QPushButton *homeQuickCurrentTab = nullptr;
    QPushButton *homeQuickRecentTab = nullptr;
    QPushButton *homeQuickFavoriteTab = nullptr;
    QWidget *homeQuickCurrentSection = nullptr;
    QWidget *homeFavoriteSection = nullptr;
    QWidget *homeQuickRecentSection = nullptr;
    QHBoxLayout *homeQuickCurrentRow = nullptr;
    QHBoxLayout *homeFavoriteRow = nullptr;
    QHBoxLayout *homeQuickRecentRow = nullptr;
    QPushButton *accountSkinSettingsButton = nullptr;
    QLabel *homeAccountLabel = nullptr;
    QLabel *homeAvatarLabel = nullptr;
    QLabel *homeAccountTypeLabel = nullptr;
    QPushButton *addFolderButton = nullptr;
    QListWidget *folderList = nullptr;
    QTreeWidget *instanceList = nullptr;
    /// 「分组设置…」。**右栏那一排放按钮的地方现在只剩它一颗**，而且它在框外右上角。
    QPushButton *groupInstanceButton = nullptr;
    QPushButton *importModpackButton = nullptr;
    QLabel *instanceFolderLabel = nullptr;
    /// 实例页那一行搜索与筛选（coding plan §7.2）：一个搜索框 + 四颗 chip，摆进正文面里
    /// 树的上方。**外框是自己一个控件**（不是裸布局）：验证工程要按 objectName 找回来量
    /// "整排有没有超出正文面"（和下载页页头那条同一套做法）。
    QWidget *instanceFilterRow = nullptr;
    QLineEdit *instanceSearchEdit = nullptr;
    /// 四颗 chip，下标 = `InstanceFilterButton` 的序号（见 `getInstanceFilterButton()`）。
    QList<QPushButton *> instanceFilterButtons;
    /// "没有符合筛选条件的实例"那一行（默认收着）。
    QLabel *instanceNoMatchLabel = nullptr;
    /// 实例页正文那一层（第 0 页 = 客户端实例，第 1 页 = 服务端，第 2 页 = 预设）。
    QStackedWidget *instanceModeStack = nullptr;
    /// 「预设」那一页的宿主（`PresetController` 把管理页塞进这一格）。
    QWidget *instancePresetPage = nullptr;
    /// 顶上那三颗模式按钮，下标 = `InstanceMode` 的序号（见 `getInstanceModeButton()`）。
    QList<QPushButton *> instanceModeButtons;
    /// 服务端那一页：列表 + 空态那句 + 「新建服务端…」那颗（coding plan §6 / M4）。
    QTreeWidget *serverList = nullptr;
    QLabel *serverEmptyLabel = nullptr;
    QPushButton *newServerButton = nullptr;
    QComboBox *installedVersionCombo = nullptr;
    /// 下载页左栏那份「下载分类」（见头文件上面那段）与右栏那份 QStackedWidget。
    /// 五页**全是真**的（2026-09-23 起）：第 0 页「游戏」是版本树 + 安装页，其余四页
    /// 各是一张「资源 → 游戏版本 → 版本号」的三层表。
    QListWidget *downloadCategoryList = nullptr;
    /// 每一栏左上角那颗「返回实例设置」（键 = 分类键，见 downloadBackButtonName 那段）。
    /// 「游戏」那一页**不在里面**（它不是从实例设置进来的——那一页根本没有"下载新游戏"
    /// 这颗按钮）。
    /// 下载页左栏顶上那颗「返回实例设置」。**整个下载页一颗**（见 getDownloadBackButton）。
    QPushButton *downloadBackButton = nullptr;
    /// 下载页页头右上角那颗刷新（见 getDownloadHeaderRefreshButton() 那段）。
    QPushButton *downloadHeaderRefreshButton = nullptr;
    /// 下载页「服务端」那一栏里那颗「新建服务端…」（见 getDownloadNewServerButton()）。
    QPushButton *downloadNewServerButton = nullptr;
    /// 下载页页头那一整块（见 getDownloadHeader() 那段）。
    QWidget *downloadHeader = nullptr;
    /// 安装页中间那层滚动区（见 installScrollName() 那段）。
    QScrollArea *installScroll = nullptr;
    /// 下载页页头那行说明（= 第 N 类的那一句，见 setDownloadHeaderRow()）。
    /// ★ 它**就是** `buildTwoColumnPage()` 给正文那一栏建的标题下面那一行 —— 不是另建一个
    ///   标签，页头那一行字只有这一处。
    QLabel *downloadHeaderSubtitleLabel = nullptr;
    QStackedWidget *downloadStack = nullptr;
    /// 设置页左栏那份「分类」与右栏那份堆叠页（见头文件上面「设置页：左栏那几条分类」那段）。
    /// 五页全是真的，每一页是一层滚动区（addSettingsPanel() 搭），堆叠页的下标就是
    /// settingsCategoryKeys() 的下标。
    QListWidget *settingsCategoryList = nullptr;
    QStackedWidget *settingsCategoryStack = nullptr;
    /// 「游戏」那一页里的两层：版本树 / 安装页（见头文件上面那段）。
    QStackedWidget *gameStack = nullptr;
    QTreeWidget *versionTree = nullptr;
    /// 版本树上面那个筛选框和它右边那句结果（见 versionFilterEditName() 那段）。
    QLineEdit *versionFilterEdit = nullptr;
    QLabel *versionFilterHint = nullptr;
    /// 筛选**第一次生效之前**每个分类是展开还是收着（键 = 分类键）。
    /// 筛完清空时按它还原 —— 不然"筛一下再清空"会把用户原来只展开一个分类的树撑成
    /// 四栏全开，那不是用户自己动的手。
    QHash<QString, bool> versionFilterExpandedBefore;
    /// 上一趟过滤是不是"真的在筛"（空串 / 纯空白 = 没在筛）。
    /// 用来判"要不要先把展开状态记下来"，以及"这一趟是不是第一次生效"。
    bool versionFilterActive = false;
    QLabel *installTargetLabel = nullptr;
    QLineEdit *installNameEdit = nullptr;
    QLabel *installNameHint = nullptr;
    QLabel *installNameError = nullptr;
    QPushButton *installButton = nullptr;
    QPushButton *installBackButton = nullptr;
    /// 安装页那棵「模组加载器」树（见头文件上面那段）。顶层五条按 modLoaderOptionNames()
    /// 的顺序摆，前四条各带自己的版本列表（点开才灌）、第 5 条是「不装（纯原版）」。
    QTreeWidget *loaderTree = nullptr;
    /// 后面四页那四张资源表 + 各自的搜索框，按分类键索引（见头文件上面那段）。
    /// 五页里「游戏」不在里头（它不用搜），所以这里是四对。
    QHash<QString, QTreeWidget *> downloadResourceTrees;
    QHash<QString, QLineEdit *> downloadResourceSearches;
    /// 那几页筛子行里的控件（按分类键索引，键就是 downloadCategoryKeys() 里那几个之一）。
    /// 「模组加载器」那一份**只有模组页有一个条目**（别的页没有这个键）—— 取的时候回 nullptr，
    /// 见 getDownloadFilterLoaderCombo() 那段。
    QHash<QString, QComboBox *> downloadFilterVersionCombos;
    QHash<QString, QComboBox *> downloadFilterLoaderCombos;
    QHash<QString, QComboBox *> downloadFilterSourceCombos;
    QHash<QString, QComboBox *> downloadFilterTypeCombos;
    /// 「类型」那一栏的整个小组（标签 + 框），为了能一起藏起来（见 getDownloadFilterTypeBox()）。
    QHash<QString, QWidget *> downloadFilterTypeBoxes;
    QHash<QString, QPushButton *> downloadFilterSearchButtons;
    QHash<QString, QPushButton *> downloadFilterResetButtons;
    /// 整合包那一页那颗「导入整合包…」（见 downloadFilterImportModpackButtonName() 那段）。
    QPushButton *downloadImportModpackButton = nullptr;
    /// 「Java 管理…」那颗按钮（见 javaManageButtonName()）。
    QPushButton *javaManageButton = nullptr;
    QLabel *javaPathLabel = nullptr;
    QCheckBox *javaWarnMismatchCheck = nullptr;
    QLabel *memoryHint = nullptr;
    QSpinBox *memoryMaxSpin = nullptr;
    QSpinBox *homeFavoriteCountSpin = nullptr;
    QSpinBox *homeRecentCountSpin = nullptr;
    MemorySlider *memoryMaxSlider = nullptr;
    QComboBox *launcherBehaviourCombo = nullptr;
    QPlainTextEdit *extraJvmArgsEdit = nullptr;
    QPushButton *applyExtraJvmArgsButton = nullptr;
    QComboBox *downloadSourceCombo = nullptr;
    /// 「同时最多下几个」那个下拉框（2026-10-13，§9.2）。项由 MainWindow 灌。
    QComboBox *downloadConcurrencyCombo = nullptr;
    /// 「下载速度上限」那个下拉框（2026-10-18，§9.2）。项由 MainWindow 灌（0 = 不限）。
    QComboBox *downloadSpeedLimitCombo = nullptr;
    /// 「代理」那颗下拉框与手动地址那一格（2026-10-18，§9.2）。项由 MainWindow 灌。
    QComboBox *downloadProxyCombo = nullptr;
    QLineEdit *downloadProxyManualEdit = nullptr;
    QLineEdit *curseForgeApiKeyEdit = nullptr;
    QCheckBox *showGameLogCheck = nullptr;
    /// 「下载」栏里那个勾选框（2026-09-28）。
    QCheckBox *downloadAutoIntoInstanceCheck = nullptr;
    /// 同一栏里「缺失前置提醒」那个勾选框（2026-10-09）。
    QCheckBox *warnMissingDependenciesCheck = nullptr;
    QCheckBox *warnDuplicateLaunchCheck = nullptr;
    QCheckBox *warnWorldLossCheck = nullptr;
    QComboBox *processPriorityCombo = nullptr;
    /// 「窗口模式」那个下拉框；**项由 MainWindow 灌**（见 gameWindowModeComboName()）。
    QComboBox *gameWindowModeCombo = nullptr;
    /// 「宽 × 高」中间那个乘号（`gameWindowSizeTimesLabelName()`）。存成成员是因为
    /// MainWindow 要按窗口模式灰/亮它 —— 见它的 getter 上面那段。
    QLabel *gameWindowSizeTimesLabel = nullptr;
    QSpinBox *gameWindowWidthSpin = nullptr;
    QSpinBox *gameWindowHeightSpin = nullptr;
    QLineEdit *titleTextEdit = nullptr;
    /// 「背景图」那一节的各行控件，按 `ThemeImageRow::partKey` 认（见这一节在头文件里那段）。
    QVector<ThemeImageRow> m_themeImageRows;
    /// 「主题」那一节（2026-10-13）。顺序按 `themeSemanticKey()` 那四个来。
    QComboBox *themePresetCombo = nullptr;
    QPushButton *themeAccentButton = nullptr;
    QPushButton *themeAccentResetButton = nullptr;
    QSlider *themeTextOpacitySlider = nullptr;
    QLabel *themeTextOpacityPercent = nullptr;
    QVector<QPushButton *> m_themeSemanticButtons;
    QPushButton *themeSemanticResetButton = nullptr;
    QPushButton *themeResetAllButton = nullptr;
    QPushButton *themeExportButton = nullptr;
    QPushButton *themeImportButton = nullptr;
    /// 「动效」那个勾（还是「个性化」栏，摆在「主题」之后）。
    QCheckBox *animationsCheck = nullptr;
    QCheckBox *versionNameCustomCheck = nullptr;
    QCheckBox *versionNameRealCheck = nullptr;
    QCheckBox *versionNameLoaderCheck = nullptr;
    QCheckBox *versionNameLoaderVersionCheck = nullptr;
    SkinViewer *skinViewer = nullptr;
    QLabel *skinHintLabel = nullptr;
    QPushButton *accountSkinButton = nullptr;
    QTabWidget *tabWidget = nullptr;
    OverlayHost *overlayHost = nullptr;
    // 顶上那条自己画的标题栏 + 外框那一份（见各自的文件头）。两个都是 setupUi 里建的。
    TitleBar *bar = nullptr;
    WindowChrome *windowChrome = nullptr;
    QFrame *bottomBar = nullptr;
    /// 底栏两头那两颗按钮：整格都是一颗按钮（里面装着原来的那几行字 + 右边一个朝上的箭头）。
    QPushButton *runStateButton = nullptr;
    QPushButton *accountButton = nullptr;
    QPushButton *toolboxButton = nullptr;
    /// 中间那一格**也是一整颗按钮**（2026-10-12，coding plan §3.1「整块可点击」）：
    /// 里面装着"任务标题 + 右边那句小字 + 一块进度条 + 右边一个朝上的箭头"。
    /// 宽度**不钉**（中间那格本来就是吃富余宽度的那一份，见 .cpp 里那段 6:1:1）。
    QPushButton *taskSummaryButton = nullptr;
    QLabel *taskExtraLabel = nullptr;
    /// 那一格标题左边那颗红点（"用户还没看过的一次失败"）。**藏着的时候不占宽度**。
    QLabel *taskFailureDot = nullptr;
    QLabel *avatarLabel = nullptr;
    QLabel *accountLabel = nullptr;
    // 账户格那两行是**竖直排列**的（名字在上、登录方式在下），和左上角那格同构。
    QLabel *accountTypeLabel = nullptr;
    /// 自绘进度条（`ui/TaskProgressBar`）。**声明成基类指针**：`getProgressBar()` 照旧回
    /// `QProgressBar *`，测试那边按那个类型找。
    TaskProgressBar *progressBar = nullptr;
    /// 那根条右边那一格里的百分比（「42%」）—— 条不再自己画数了（见 `TaskProgressBar.hpp`）。
    QLabel *taskPercentLabel = nullptr;
    QLabel *statusLabel = nullptr;
    QLabel *runStateLabel = nullptr;
    QLabel *runStateDetailLabel = nullptr;
    QLabel *runStateMarkLabel = nullptr;
    /// 左下那一格右边那个朝上的箭头。**单独存一份指针**是因为它要跟着
    /// setRunPanelAvailable() 显隐（右边那颗按钮上那个不需要，一直亮着）。
    QLabel *runStateChevron = nullptr;
    /// 左下那一格现在点不点得开。初值 true = 装好就能点 —— 界面**还没被谁摆过**的时候
    /// 不该是"点不动"的（独立构造 MainWindowUI 的验证工程就吃这个初值，见
    /// setRunPanelAvailable()）。
    bool m_runPanelAvailable = true;
    /// 「点开面板时去哪要那几行」和「行尾的按钮被点了」。由 MainWindow 在构造时装进来
    /// （见 setBottomBarPanelContent()）—— 界面这一头**不查数据**，只画。
    PanelRowsProvider m_panelRowsProvider;
    PanelActionHandler m_panelActionHandler;
    /// 任务中心那两块：**去哪要那几行**（见 setTaskCenterContent()）。
    TaskCenter::ItemProvider m_taskCenterProvider;
    TaskCenter::ClearFinishedHandler m_taskCenterClearHandler;
    /// 「任务中心里某一行被点了」该告诉谁（见 setTaskCenterRowActivatedHandler()）。
    std::function<void(const QString &key)> m_taskCenterRowActivated;
    /// 「工具箱面板里某一颗工具按钮被点了」该告诉谁（见 setToolboxToolHandler()）。
    std::function<void(const QString &key)> m_toolboxToolHandler;
    /// 「那一行行尾那颗小按钮被点了」该告诉谁（见 setTaskCenterRowActionHandler()）。
    TaskCenter::RowActionHandler m_taskCenterRowAction;
    HomeQuickActionHandler m_homeQuickActionHandler;
    HomeQuickMode m_homeQuickMode = HomeQuickMode::Current;
    /// 「文件夹齿轮菜单里某一项被点了」该告诉谁（见 setFolderActionHandler()）。
    FolderActionHandler m_folderActionHandler;
    /// 「组齿轮菜单里某一项被点了」和「挑目标框里某一组被点了」该告诉谁
    /// （见 setGroupActionHandler() / setGroupPickHandler()）。
    GroupActionHandler m_groupActionHandler;
    GroupPickHandler m_groupPickHandler;
};
