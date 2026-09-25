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
 * 四个选项卡**都是左右两栏**（仿 PCL）：左栏是要操作的那点东西（选版本、按钮），
 * 右栏是列表或一整摊设置项。前三个（主页/实例/下载）由 buildTwoColumnPage() 搭出**真控件**，
 * 比例取自 leftColumnStretch(tab)/rightColumnStretch(tab)；「设置」页是一张两列的
 * QGridLayout，用默认那一对份数（3:7）表达比例（栏不是控件）。界面和验证工程都引这几对
 * 份数，不各写一份。
 *
 * NOTE: 比例**按页不同** —— 实例页/下载页是默认的"左 3 右 7"，主页是"左 7 右 3"
 *       （见 leftColumnIsWider()）。
 */

#pragma once

#include <QCoreApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QGroupBox>
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

class QGridLayout;
class QScrollArea;
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
    /// @param widget 要护住的控件（下拉框 / 数字框）。空指针什么都不做，调用方不必先判空。
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
    /// 设置页「账户」那一格里的入口按钮：「账号与外观…」。它和主页右栏那个入口打开的是
    /// **同一个窗**（文案也是同一份，见 accountSkinButtonText()），名字不同只是因为
    /// objectName 得唯一 —— findChild 靠它认控件。
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
    /// 「不装（纯原版）」那一个在两张表里的下标。想"回到装纯原版"就得能勾回它 ——
    /// 单选钮一旦勾上就没法用鼠标取消（见下面 buildTwoColumnPage 里那段），这个选项
    /// 就是那条退路；写死数字的地方一律用这个函数。
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
    ///       一样了**（`InstanceGroupDelegate::sizeHint()` 返回的就是 versionTreeRowHeight()，
    ///       用户原话「每个版本啥的上下高一点，像下载页那样」），16 px 装得下了；
    ///       `gearGlyphRect()` 里那道"按行高收一收"留着当兜底 —— 行高哪天再变矮，它自己缩。
    static int instanceGroupGearSize() { return 16; }
    // NOTE: **`Qt::UserRole + 12` 空着** —— 它原来是"这一行画哪一颗上/下箭头"那位掩码
    //       （2026-09-25 上半天的 rowGroupMoveRole() / groupMoveUpFlag() / groupMoveDownFlag()
    //       和那两句话一起删了）。那两颗箭头当天傍晚搬进了「分组设置…」那张表（用户原话：
    //       「这个箭头不应该在这里，而应该在 分组设置 里面，这里的删了」），而那张表里
    //       行序就是组序、**一个别的行都没有** ⇒ "能不能挪"是算出来的，不需要角色位
    //       （见 ui/GroupListDelegate 文件头）。所以这一档号码空着，别顺手拿它当"下一个"。
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
    static QString installBackButtonName() { return QStringLiteral("installBackButton"); }
    /// 每个选项卡页面贴着内容区留的那一圈边距。
    /// **唯一出处** —— 内容区那个覆盖层（`OverlayHost`）也是一整个"页面"，它那边留白
    /// 必须和别的页面一样，所以它读的是这一个数，而不是自己再写一个 9。
    static int pageMargin() { return 9; }

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
    /// 分类的键（进条目的 userData，也是那几张资源表的 objectName 前缀）。
    /// **顺序就是界面上从上到下的顺序**，第 0 个是「游戏」（它没有资源表，有别的两张）。
    static QStringList downloadCategoryKeys() {
        return {QStringLiteral("game"), QStringLiteral("modpack"), QStringLiteral("mod"),
                QStringLiteral("datapack"), QStringLiteral("resourcepack"),
                QStringLiteral("shader")};
    }
    /// 条目上写的字。和上面那张表**一一对应**，顺序不能各排各的。
    static QStringList downloadCategoryTexts() {
        return {QStringLiteral("游戏"), QStringLiteral("整合包"), QStringLiteral("模组"),
                QStringLiteral("数据包"), QStringLiteral("资源包"), QStringLiteral("光影")};
    }
    /// 有内容的那个分类的下标（「游戏」）。写死数字的地方一律用它。
    static int downloadCategoryGameIndex() { return 0; }
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
    /// 那几张资源表里**前面那个图标画多大**（版本树/加载器树是 18）。
    /// 用户要"会有图片"，18 px 那个尺寸看不清缩略图；行高仍是 versionTreeRowHeight()（46），
    /// 32 px 的图在行里上下各留 7 px，不挤。
    static int resourceTreeIconSize() { return 32; }
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
    // 实例页右栏那排按钮。**全按钮驱动、不做右键菜单** —— 仓库里右键菜单零先例，
    // 而且验证工程跑在 offscreen 上，QMenu::exec() 会阻塞在事件循环里，几乎驱动不了。
    //
    // NOTE: 2026-09-25 起**没有「停止游戏…」**了（用户原话：「实例里停止游戏按钮删了」）。
    //       停游戏这件事还在，只是入口只剩底栏左面板里那一行的电源（见
    //       MainWindow::stopGameSession()）—— 那一格本来就写着"哪一个会话在跑"，
    //       停在哪儿比在这个"对选中实例做什么"的排里更说得清。所以这里也不再留
    //       stopGameButtonName() / stopGameText()，别照着旧记性写。
    //
    // NOTE: 同一天，这一排从"三颗都是实例级的"变成了**两 + 一**：前两颗（重命名… /
    //       实例设置…）读的是列表里选中的那一个实例，第三颗（分组设置…）**不读它** ——
    //       用户原话：「下方的分组设置不再是针对单个实例的设置，而是对现有分组数目/
    //       名称等的设置」。所以"三颗按钮都作用于选中的那个实例"这句话**已经过期**，
    //       写新代码时别照它想。
    static QString renameInstanceButtonName() { return QStringLiteral("renameInstanceButton"); }
    static QString groupInstanceButtonName() { return QStringLiteral("groupInstanceButton"); }
    static QString instanceSettingsButtonName() {
        return QStringLiteral("instanceSettingsButton");
    }
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
    static QString renameInstanceText() { return QStringLiteral("重命名…"); }
    /// NOTE: 2026-09-25 起它开的那张窗**不再问"这个实例归到哪个组"**，改成了分组自己的
    ///       设置（有哪些组、叫什么名字、什么顺序、默认展开还是收着）—— 用户原话：
    ///       「不再是针对单个实例的设置，而是对现有分组数目/名称等的设置」。所以文案
    ///       跟着从「分组…」变成「分组设置…」，而且它**不读选中的实例**。
    ///       想按实例归组走组行行尾那颗齿轮（GroupActionAddSelected）或者分组窗里那条
    ///       归属单选钮原来那一套？—— 那一套**没有了**，别再照旧记性找。
    static QString groupInstanceText() { return QStringLiteral("分组设置…"); }
    static QString instanceSettingsText() { return QStringLiteral("实例设置…"); }
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
    /// 左边那两行字各自的宽度上限 = 那一格的宽度减掉**最左边那颗状态方块占掉的一段**，
    /// 再减掉右边那个箭头和它左边的间距。两行都封这个顶，布局就永远不会被长名字撑开。
    ///
    /// NOTE: 方块那一段（`bottomBarRunStateMarkWidth()`）**必须一起减掉**（2026-09-24 补的）：
    ///       方块和第一行文字在同一行里，不减的话那一行的需要宽度变成
    ///       16 + 152 = 168 > 152，方块要么被压掉、要么把这一格撑出 170 之外。
    ///       两行减掉**同一个数**是刻意的 —— 第二行还缩进了同样多（见 MainWindowUI.cpp
    ///       里 runStateDetailLabel 那几行），两行文字的左边因此对得齐。
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
    static QString statusLabelName() { return QStringLiteral("statusLabel"); }

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

    // ---------- 左右两栏（主页/实例/下载三页用；设置页是两列 grid） ----------
    // 栏是**真的控件**（不是裸布局）：这样才能按 objectName 找回来量几何、
    // 也才能在验证工程里断言左右两栏的份数（每一页各自的那一对）。
    //
    // NOTE: 只有前三个选项卡用 buildTwoColumnPage() 搭出这两个栏控件；「设置」页是
    //       一张两列的 QGridLayout（选项名称在列 0、控件在列 1），栏没有对应的控件，
    //       所以那两列**没有** objectName 可找 —— 设置页的比例靠 settingsForm 上
    //       setColumnStretch() 的两份份数表达，验证工程去量列 0/列 1 里控件的几何。
    /// @param tab "home" / "instance" / "download"。
    static QString leftColumnName(const QString &tab) {
        return tab + QStringLiteral("LeftColumn");
    }
    /// @param tab 同上。
    static QString rightColumnName(const QString &tab) {
        return tab + QStringLiteral("RightColumn");
    }
    /// 默认那一套份数：左 3 右 7。实例页，以及设置页那张两列表格都用它。
    static int leftColumnStretch() { return 3; }
    /// 右栏占的份数（默认那一套）。
    static int rightColumnStretch() { return 7; }
    /// 右栏在页面上是不是真的更宽（验证工程拿它当"默认的 3:7 生效了"的判据）。
    static bool rightColumnIsWider() { return rightColumnStretch() > leftColumnStretch(); }
    /// 主页那一套：7:3（把默认那一对对调过来），见 leftColumnIsWider()。
    static int homeLeftColumnStretch() { return 7; }
    static int homeRightColumnStretch() { return 3; }
    /// 下载页那一套：2:8。用户原话"下载页左右改成 2：8"。
    ///
    /// NOTE: 比默认的 3:7 更偏向右栏 —— 左栏那列分类（游戏/整合包/模组/资源包/光影）
    ///       最多三个字，而右栏现在要装下那棵树（每条还带一行发布日期）、以及安装页那一摊
    ///       （名称框 + 加载器那棵树）。左栏按 sizeHint 本来也只吃那么点宽度，把份数让给
    ///       右栏是"分类列表不被拉成一整条空白 + 右栏那棵树不用横向滚动"两边都对的取法。
    static int downloadLeftColumnStretch() { return 2; }
    static int downloadRightColumnStretch() { return 8; }

    /// 这一页是不是"左宽右窄"。
    /// @param tab "home" / "instance" / "download"。
    ///
    /// 只有主页是。主页的右栏现在**空着**（原来那张「版本列表」已经去掉 —— 实例页右边
    /// 已经有一张同样数据的列表了，主页那张是把同一件事说两遍），所以宽度让给左边的
    /// "选版本 + 启动"那两样。另外两页的左栏仍是"只放选一个"的窄栏。
    static bool leftColumnIsWider(const QString &tab) {
        return tab == QStringLiteral("home");
    }
    /// 某一页左栏占的份数（三页各一套，见上面那几个常数）。
    static int leftColumnStretch(const QString &tab) {
        if (tab == QStringLiteral("home")) {
            return homeLeftColumnStretch();
        }
        if (tab == QStringLiteral("download")) {
            return downloadLeftColumnStretch();
        }
        return leftColumnStretch();
    }
    /// 某一页右栏占的份数（见 leftColumnStretch(const QString&)）。
    static int rightColumnStretch(const QString &tab) {
        if (tab == QStringLiteral("home")) {
            return homeRightColumnStretch();
        }
        if (tab == QStringLiteral("download")) {
            return downloadRightColumnStretch();
        }
        return rightColumnStretch();
    }

    // ---------- 设置页 ----------
    static QString settingsScrollName() { return QStringLiteral("settingsScroll"); }
    static QString settingsFormName() { return QStringLiteral("settingsForm"); }
    static QString javaVersionComboName() { return QStringLiteral("javaVersionCombo"); }
    static QString javaPathLabelName() { return QStringLiteral("javaPathLabel"); }
    static QString javaWarnMismatchCheckName() { return QStringLiteral("javaWarnMismatchCheck"); }
    static QString memoryAutoCheckName() { return QStringLiteral("memoryAutoCheck"); }
    /// 「内存分配」那一行下面那句说明。文字随系统内存变（可用内存不是一个定值），
    /// 所以它是唯一一个要 MainWindow 反复改文字的 QLabel，得能按名字取回来。
    static QString memoryAutoHintName() { return QStringLiteral("memoryAutoHint"); }
    static QString memoryMaxSpinName() { return QStringLiteral("memoryMaxSpin"); }
    static QString memoryMinSpinName() { return QStringLiteral("memoryMinSpin"); }
    static QString launcherBehaviourComboName() { return QStringLiteral("launcherBehaviourCombo"); }
    static QString extraJvmArgsEditName() { return QStringLiteral("extraJvmArgsEdit"); }
    static QString applyExtraJvmArgsButtonName() {
        return QStringLiteral("applyExtraJvmArgsButton");
    }
    static QString downloadSourceComboName() { return QStringLiteral("downloadSourceCombo"); }

    // ---------- CurseForge API key（2026-09-25 加的，在「下载源」那一组里） ----------
    // CurseForge 格式的整合包只能靠接口换出下载地址（那份清单里只有编号），而官方接口
    // 要 key。默认走社区反代，这一格是给用户"自己填一份走官方"的退路 —— 所以它是
    // **可留空的一格**，不是必填项（空 = 反代）。
    static QString curseForgeApiKeyEditName() { return QStringLiteral("curseForgeApiKeyEdit"); }
    /// 这一行的行标题（左列那一格）。
    static QString curseForgeApiKeyRowText() { return QStringLiteral("CurseForge API key"); }
    /// 输入框里的占位文字（空着的时候显示那一句）。
    static QString curseForgeApiKeyPlaceholder() { return QStringLiteral("留空则用公共接口"); }
    static QString showGameLogCheckName() { return QStringLiteral("showGameLogCheck"); }
    static QString processPriorityComboName() { return QStringLiteral("processPriorityCombo"); }
    static QString gameWindowWidthSpinName() { return QStringLiteral("gameWindowWidthSpin"); }
    static QString gameWindowHeightSpinName() { return QStringLiteral("gameWindowHeightSpin"); }
    /// 「外观」那一格那个文本框：条上那行字（2026-09-25 加的）。
    static QString titleTextEditName() { return QStringLiteral("titleTextEdit"); }
    /// 「宽 × 高」中间那个乘号。它也要有名字：验证工程量这三格的几何时得能取到它。
    static QString gameWindowSizeTimesLabelName() { return QStringLiteral("gameWindowSizeTimes"); }
    /// 「启动选项」那一组里那几个勾选框/数字框的文案。
    /// 「显示游戏日志」这个串是**用户原话**（2026-09-24 提的需求），别改写成别的说法。
    static QString showGameLogCheckText() { return QStringLiteral("显示游戏日志"); }

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
    /// 那一格最左边那颗小方块的三种状态。
    ///
    /// 用户原话（2026-09-24）：「左下角没有实例的时候显示（红色的）■（默认的黑色）当前没有
    /// 运行中的实例，启动的时候那个符号（也可以画图代替）变成黄色，文本为版本名（没有就
    /// 版本号-加载器名称，不受名字显示设置影响）启动中... 启动以后符号变成绿色，和原来一样」。
    ///
    /// 「（默认的黑色）」说的是**文字**的颜色 —— 那两行字一直用调色板的默认正文色，
    /// 这一批一个字都没改（别顺手给"没有实例"那种状态把字也染红）。
    enum class RunStateMark {
        NotRunning,  ///< 红：一个实例都没在跑
        Launching,   ///< 黄：正在启动（进程起来了、游戏还没出话，见 m_launchingSessions）
        Running,     ///< 绿：有实例在跑
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
    /// 三态各自的颜色。深浅两套的判据走 paletteIsDark()：红的那一档**直接调
    /// folderDangerColor()** —— 同一个界面上两种红各调各的，迟早有一处会看着是脏的。
    ///
    /// 黄和绿是各挑的两个值（浅底用深一点的、深底用亮一点的，都是为了"在底色上看得见"）。
    /// 只说"红/黄/绿"而没有具体色值是可验的：这三个色相分得开（0 / 45 / 130 那一带），
    /// 验证工程量的就是色相落在哪一段（见 ui_check 的 V 段）。
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
    ///       左边那颗方块因此有了红的那一档 —— 光看字和别的状态分不出"完全没在跑"，
    ///       一眼看颜色能分出来。
    static QString runStateNotRunningText() { return QStringLiteral("当前没有运行中的实例"); }
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

    /// 底栏中间那格（进度条 + 进度文字）该不该显示。
    /// @param launching 正在启动游戏（进程起来了、游戏还没出话）。
    /// @param downloading 正在下载。
    /// @param blocking 正在**同步阻塞**地等一件事（换令牌、挑/下 Java、startGame()）。
    /// @return 三种"在忙"任意一种都算。
    ///
    /// "同步阻塞"那一种 2026-09-22 才加进来：那几段在 UI 线程上等网络/等进程，界面完全
    /// 不重绘，用户点了「启动游戏」看到的就是"卡死"（Windows 还会给标题挂上"无响应"）。
    /// 那几段报不出百分比，但"在忙"这件事必须说出来 —— 见 MainWindow::enterSyncWait()。
    static bool bottomBarShowsProgress(bool launching, bool downloading, bool blocking) {
        return bottomBarProgressOwner(launching, downloading, blocking)
               != BottomBarProgressOwner::None;
    }

    /// 底栏中间那格**归谁**（三件事都会占它，优先级从高到低）。
    enum class BottomBarProgressOwner {
        None,      ///< 没人在忙：进度条复位、那格清空
        Download,  ///< 下载 —— 最低的一档（它随时会被上面两档借走，借完要还回来）
        Blocking,  ///< 同步阻塞等待（换令牌 / 挑 Java / startGame，见 enterSyncWait()）
        Launch,    ///< 启动中（进程起来了、游戏还没出话）—— 最高的一档
    };

    /// @brief 底栏中间那格现在归谁。
    ///
    /// 这是"谁说话算数"的**唯一出处**：updateBottomBar() 按它画，下载进度那条每 200 ms
    /// 一帧的回调（MainWindow::updateDownloadProgressUi）也按它判"该不该动这一格"。
    ///
    /// 以前这条优先级只写在 updateBottomBar() 的 if 链里，下载进度回调**无条件**覆盖那格
    /// 文字与进度条 —— "边下边开"时，enterSyncWait() 写的「正在校验登录状态…」「正在准备
    /// Java 运行时…」和进程起来后的「正在启动 X …」会在 200 ms 内被顶掉，而那几句话正是
    /// 2026-09-22 专为"点了启动就像卡死"做的。
    static BottomBarProgressOwner bottomBarProgressOwner(bool launching, bool downloading,
                                                         bool blocking) {
        if (launching) {
            return BottomBarProgressOwner::Launch;
        }
        if (blocking) {
            return BottomBarProgressOwner::Blocking;
        }
        if (downloading) {
            return BottomBarProgressOwner::Download;
        }
        return BottomBarProgressOwner::None;
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
    // 实例页右栏那三个按钮，作用于「实例列表」里当前选中的那一个实例。
    QPushButton *getRenameInstanceButton() const { return renameInstanceButton; }
    QPushButton *getGroupInstanceButton() const { return groupInstanceButton; }
    QPushButton *getInstanceSettingsButton() const { return instanceSettingsButton; }
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
    /// 右栏那份 QStackedWidget。**下标和 downloadCategoryKeys() 一一对应** ——
    /// 点第 N 个分类就 setCurrentIndex(N)，这条不变量由界面自己保证（搭的时候就按
    /// 同一个表、同一个顺序加页），验证工程会数一遍两边的条数。
    QStackedWidget *getDownloadStack() const { return downloadStack; }
    /// 「游戏」那一页里的两层（版本树 / 安装页）。下标见 downloadGameTreeIndex()
    /// 和 downloadGameInstallIndex()。
    QStackedWidget *getGameStack() const { return gameStack; }
    /// 四条分类 + 版本号那份树。身份读每条的 `Qt::UserRole`（顶层是分类键、子条目是
    /// 版本号，空串 = 这一条不是版本号）—— 见 versionTreeName() 上面那段。
    QTreeWidget *getVersionTree() const { return versionTree; }
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
    QLineEdit *getDownloadSearchEdit(const QString &categoryKey) const {
        return downloadResourceSearches.value(categoryKey, nullptr);
    }

    // 设置页的控件。**只搭出来、只负责显隐**，值由 MainWindow 灌（它才知道
    // 那些设置存在哪、改了要不要落盘）。
    QComboBox *getJavaVersionCombo() const { return javaVersionCombo; }
    QLabel *getJavaPathLabel() const { return javaPathLabel; }
    QCheckBox *getJavaWarnMismatchCheck() const { return javaWarnMismatchCheck; }
    QCheckBox *getMemoryAutoCheck() const { return memoryAutoCheck; }
    QLabel *getMemoryAutoHint() const { return memoryAutoHint; }
    QSpinBox *getMemoryMaxSpin() const { return memoryMaxSpin; }
    QSpinBox *getMemoryMinSpin() const { return memoryMinSpin; }
    QComboBox *getLauncherBehaviourCombo() const { return launcherBehaviourCombo; }
    QPlainTextEdit *getExtraJvmArgsEdit() const { return extraJvmArgsEdit; }
    QPushButton *getApplyExtraJvmArgsButton() const { return applyExtraJvmArgsButton; }
    QComboBox *getDownloadSourceCombo() const { return downloadSourceCombo; }
    QLineEdit *getCurseForgeApiKeyEdit() const { return curseForgeApiKeyEdit; }
    QCheckBox *getShowGameLogCheck() const { return showGameLogCheck; }
    QCheckBox *getWarnDuplicateLaunchCheck() const { return warnDuplicateLaunchCheck; }
    QCheckBox *getWarnWorldLossCheck() const { return warnWorldLossCheck; }
    QComboBox *getProcessPriorityCombo() const { return processPriorityCombo; }
    QSpinBox *getGameWindowWidthSpin() const { return gameWindowWidthSpin; }
    QSpinBox *getGameWindowHeightSpin() const { return gameWindowHeightSpin; }
    QLineEdit *getTitleTextEdit() const { return titleTextEdit; }
    QCheckBox *getVersionNameCustomCheck() const { return versionNameCustomCheck; }
    QCheckBox *getVersionNameRealCheck() const { return versionNameRealCheck; }
    QCheckBox *getVersionNameLoaderCheck() const { return versionNameLoaderCheck; }
    QCheckBox *getVersionNameLoaderVersionCheck() const { return versionNameLoaderVersionCheck; }

    // 主页右栏那个可拖动旋转的 3D 皮肤预览，以及它下面那句说明。
    // 这个控件是**自己画的**（QWidget 子类 + paintEvent，不是标准控件），
    // 所以它比别处多一层：界面只管把贴图喂进去，贴图从哪来是业务层的事。
    SkinViewer *getSkinViewer() const { return skinViewer; }
    QLabel *getSkinHintLabel() const { return skinHintLabel; }
    /// 「账号与外观…」：登录多个账号、换皮肤换披风那个模块的入口。
    QPushButton *getAccountSkinButton() const { return accountSkinButton; }

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
    /// 底栏两头那两颗按钮（左：实例运行状态 · 右：头像/用户名）。点它们会**向上**弹出一块
    /// 面板（左边那块列在跑的实例、右边那块列最近用过的账号），内容由 MainWindow 灌进来
    /// （见 setBottomBarPanelContent()）。
    QPushButton *getRunStateButton() const { return runStateButton; }
    QPushButton *getAccountButton() const { return accountButton; }
    QLabel *getAvatarLabel() const { return avatarLabel; }
    QLabel *getAccountLabel() const { return accountLabel; }
    QLabel *getAccountTypeLabel() const { return accountTypeLabel; }
    // 进度只出现在底栏中间那一格，不进日志（旧版是钉在 QStatusBar 上的）
    QProgressBar *getProgressBar() const { return progressBar; }
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
    /// 搭设置页：滚动区里一张两列表格，列 0 是选项名称、列 1 是控件（3:7）。
    void setupSettingsPage(QWidget *page);
    /// 搭底部那条通栏状态条（左：实例运行状态，中：进度，右：头像/用户名 ——
    /// 2026-09 把左右两格对调过，见 .cpp 里的说明）。
    void setupBottomBar(QWidget *parent, QVBoxLayout *mainLayout);
    /// 行尾那颗齿轮被点了：把菜单贴在那颗按钮上弹出来（或者**收回去** —— 已经弹着、
    /// 而且就是这一颗弹的时候，再点一下是收回，和底栏那两块面板一个路数）。
    void openFolderMenu(const FolderRow &row, QWidget *gear);

    /// 底栏两头那两颗按钮里的一颗：是哪一颗（**按按钮指针认人**，不按"左右"）、
    /// 点开的是哪一块面板。
    struct BottomBarAnchor {
        QPushButton *button = nullptr;
        /// true = 右边那块（账号列表），false = 左边那块（实例列表）。
        bool account = false;
    };
    /// 底栏那两颗按钮"点开 / 收回"的唯一出处，三种情形都在这里分：
    /// 已经开着**同一颗**的 → 收回（不再开）；开着**另一颗**的 → 收旧的、开新的；
    /// 都没开着 → 开。**每一次点击都算数**（点另一颗那一下是面板收到的，见 .cpp 里
    /// BottomBarPanel::handleOutsidePress() 的第一段 NOTE）。
    void toggleBottomBarPanel(const BottomBarAnchor &anchor);
    /// 把 @p anchor 那块面板弹出来。**"收"的那一半在 toggleBottomBarPanel()**：
    /// 这个函数只负责开，不管现在开着什么。
    void openBottomBarPanel(const BottomBarAnchor &anchor);
    /// @p globalPos（全局坐标）落在底栏哪一颗按钮上。没落在任何一颗上 = 返回的 button 是空的。
    BottomBarAnchor bottomBarAnchorAt(const QPoint &globalPos) const;

    // 控件指针
    //
    // NOTE: 全部显式初始化成 nullptr（2026-09-24 加的）。这不是为了好看：下面那些
    //       访问器和 isHomeTabCurrent() 拿"空指针"当"setupUi() 还没跑完"的判据 ——
    //       成员没有初值时，那个判据读的是**未初始化的值**（UB），只是现在的调用点
    //       全都排在 setupUi() 之后才没炸。
    // NOTE: 新加控件指针时**一并写上 = nullptr**，别让它单独破例。
    QPushButton *startButton = nullptr;
    QPushButton *accountSkinSettingsButton = nullptr;
    QPushButton *addFolderButton = nullptr;
    QListWidget *folderList = nullptr;
    QTreeWidget *instanceList = nullptr;
    QPushButton *renameInstanceButton = nullptr;
    QPushButton *groupInstanceButton = nullptr;
    QPushButton *instanceSettingsButton = nullptr;
    QPushButton *importModpackButton = nullptr;
    QLabel *instanceFolderLabel = nullptr;
    QComboBox *installedVersionCombo = nullptr;
    /// 下载页左栏那份「下载分类」（见头文件上面那段）与右栏那份 QStackedWidget。
    /// 五页**全是真**的（2026-09-23 起）：第 0 页「游戏」是版本树 + 安装页，其余四页
    /// 各是一张「资源 → 游戏版本 → 版本号」的三层表。
    QListWidget *downloadCategoryList = nullptr;
    QStackedWidget *downloadStack = nullptr;
    /// 「游戏」那一页里的两层：版本树 / 安装页（见头文件上面那段）。
    QStackedWidget *gameStack = nullptr;
    QTreeWidget *versionTree = nullptr;
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
    QComboBox *javaVersionCombo = nullptr;
    QLabel *javaPathLabel = nullptr;
    QCheckBox *javaWarnMismatchCheck = nullptr;
    QCheckBox *memoryAutoCheck = nullptr;
    QLabel *memoryAutoHint = nullptr;
    QSpinBox *memoryMaxSpin = nullptr;
    QSpinBox *memoryMinSpin = nullptr;
    QComboBox *launcherBehaviourCombo = nullptr;
    QPlainTextEdit *extraJvmArgsEdit = nullptr;
    QPushButton *applyExtraJvmArgsButton = nullptr;
    QComboBox *downloadSourceCombo = nullptr;
    QLineEdit *curseForgeApiKeyEdit = nullptr;
    QCheckBox *showGameLogCheck = nullptr;
    QCheckBox *warnDuplicateLaunchCheck = nullptr;
    QCheckBox *warnWorldLossCheck = nullptr;
    QComboBox *processPriorityCombo = nullptr;
    QSpinBox *gameWindowWidthSpin = nullptr;
    QSpinBox *gameWindowHeightSpin = nullptr;
    QLineEdit *titleTextEdit = nullptr;
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
    QLabel *avatarLabel = nullptr;
    QLabel *accountLabel = nullptr;
    // 账户格那两行是**竖直排列**的（名字在上、登录方式在下），和左上角那格同构。
    QLabel *accountTypeLabel = nullptr;
    QProgressBar *progressBar = nullptr;
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
    /// 「文件夹齿轮菜单里某一项被点了」该告诉谁（见 setFolderActionHandler()）。
    FolderActionHandler m_folderActionHandler;
    /// 「组齿轮菜单里某一项被点了」和「挑目标框里某一组被点了」该告诉谁
    /// （见 setGroupActionHandler() / setGroupPickHandler()）。
    GroupActionHandler m_groupActionHandler;
    GroupPickHandler m_groupPickHandler;
};
