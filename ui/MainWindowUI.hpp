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
#include <QListWidget>
#include <QMainWindow>
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

// 「一个组在界面上写成什么」这条规则的唯一出处（分组对话框和实例列表那张树都用它）。
// 让 MainWindowUI 反过来去 include 同一个 ui/ 下的对话框头，比在两边各写一份
// "%1 (%2)" 强：那种写法不会报错，只会在哪天有人改了其中一处之后让同一个组在两处
// 显示得不一样。
#include "GroupDialog.hpp"

class QGridLayout;
class QScrollArea;
class QVBoxLayout;

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
    /// 主页那个「离线模式」勾选框。**只有它没有 objectName** 的那阵子，验证工程要
    /// 驱动"离线启动"就只能按文字找控件 —— 而文字是产品文案，会变。
    static QString offlineModeCheckBoxName() { return QStringLiteral("offlineModeCheckBox"); }
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
    static int rowRefreshRole() { return Qt::UserRole + 10; }
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
    /// 那行红字的红。产品里**只有这一处**红字，所以色值放在这儿当唯一出处
    /// （验证工程量像素时要认的就是它）。
    static QColor installNameErrorColor() { return QColor(0xc0, 0x39, 0x2b); }
    static QString installButtonName() { return QStringLiteral("installButton"); }
    static QString installBackButtonName() { return QStringLiteral("installBackButton"); }
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
    // 模组 / 资源包 / 光影。点哪个，右栏那份 QStackedWidget 就换到哪一页。
    //
    // NOTE: 2026-09-23 起五页**全是真的**了：第 0 页「游戏」还是原来那一摊（挑加载器、
    //       挑版本、下载），其余四页是"一个搜索框 + 一棵三层树"（资源 → 游戏版本 → 版本号，
    //       见下面 downloadResourceTreeName() 那段）。原来那四句"还没做"的话连同
    //       downloadPlaceholderText() 一起删掉了 —— 留着就是四页在说假话。
    static QString downloadCategoryListName() { return QStringLiteral("downloadCategoryList"); }
    static QString downloadStackName() { return QStringLiteral("downloadStack"); }
    /// 「游戏」那一页（唯一有内容的那页）的 objectName。
    static QString downloadGamePageName() { return QStringLiteral("downloadGamePage"); }
    /// 分类的键（进条目的 userData，也是"还没做"那几页的 objectName 前缀）。
    /// **顺序就是界面上从上到下的顺序**，第 0 个是唯一有内容的那个。
    static QStringList downloadCategoryKeys() {
        return {QStringLiteral("game"), QStringLiteral("modpack"), QStringLiteral("mod"),
                QStringLiteral("resourcepack"), QStringLiteral("shader")};
    }
    /// 条目上写的字。和上面那张表**一一对应**，顺序不能各排各的。
    static QStringList downloadCategoryTexts() {
        return {QStringLiteral("游戏"), QStringLiteral("整合包"), QStringLiteral("模组"),
                QStringLiteral("资源包"), QStringLiteral("光影")};
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
    static QStringList downloadSearchHints() {
        return {QString(),
                QStringLiteral("搜整合包名，回车；挑一个游戏版本，再挑一个版本号导入"),
                QStringLiteral("搜模组名，回车；挑一个游戏版本，再挑一个版本号装进实例"),
                QStringLiteral("搜资源包名，回车；挑一个游戏版本，再挑一个版本号装进实例"),
                QStringLiteral("搜光影包名，回车；挑一个游戏版本，再挑一个版本号装进实例")};
    }
    /// 第 index 个分类那一页搜索框里提示什么（「游戏」那页没有搜索框，返回空串）。
    static QString downloadSearchHint(int index) {
        const QStringList hints = downloadSearchHints();
        return index >= 0 && index < hints.size() ? hints.at(index) : QString();
    }

    // ---- 后面四页那四张资源表（模组 / 整合包 / 资源包 / 光影）----
    //
    // 每页两样东西：一个搜索框 + 一棵**三层**树。三层的走法和用户描述的那条路一一对应：
    //   第一层 资源（图标 + 名字 + 「作者 · 下载量」）→ 点开
    //   第二层 游戏版本 → 点开（这时才去问接口）→
    //   第三层 版本号（文件名 + 「release · 日期 · 加载器」）→ 点它 = 下它
    // 用户原话：「点开模组后是选择游戏版本，在选择游戏版本后展开下方的版本列表，
    // 类似模组加载器的样子」。三层用的是**上一页那两棵树的同一个代理**，所以长得一样。
    //
    // 控件的名字按分类键拼（`modTree` / `shaderSearchEdit` 这种）：四页的控件一模一样、
    // 只有那几个字不同，所以下面是一段循环搭四遍，不是抄四份。
    static QString downloadResourceTreeName(const QString &categoryKey) {
        return categoryKey + QStringLiteral("Tree");
    }
    static QString downloadSearchEditName(const QString &categoryKey) {
        return categoryKey + QStringLiteral("SearchEdit");
    }
    /// 那四张资源表里**前面那个图标画多大**（版本树/加载器树是 18）。
    /// 用户要"会有图片"，18 px 那个尺寸看不清缩略图；行高仍是 versionTreeRowHeight()（46），
    /// 32 px 的图在行里上下各留 7 px，不挤。
    static int resourceTreeIconSize() { return 32; }
    /// 资源那一行（最上面那一层）上挂的"这是哪一类"：存**分类键**
    ///（`downloadCategoryKeys()` 里那四个之一，也是 `ResourceCatalog::kindKey()`）。
    ///
    /// 为什么要挂在条目上：用户点**第三层**（一个版本号）那一下要算"下到哪儿去"
    ///（模组 → `mods/`、资源包 → `resourcepacks/`、光影 → `shaderpacks/`、整合包 → 缓存目录），
    /// 而那一刻手上只有条目 —— 从它往上两层、回到第一层那一行读回来就是了。
    /// 不反查树控件的 objectName（那是控件的名字，不是数据），也不按页面下标猜。
    /// 存的为什么是键而不是枚举的整数值：读的时候可以直接走
    /// `ResourceCatalog::kindFromCategoryKey()` —— 那个函数认得出来才是"这一行是我们建的"，
    /// 而把一个 int 硬转成枚举是没有这层校验的（值是脏的就会变成一个不存在的 Kind）。
    ///
    /// NOTE: 挑号码的时候看一眼 rowSubTextRole() 上面那段 —— 2026-09-23 这里原来和
    ///       `rowRefreshRole()` 撞了（都是 `UserRole + 3`），症状是这三张表上**每一行**
    ///       右端都多画一个刷新圈压在 `>` 上。代理那三个角色现在自成一档（+8/+9/+10），
    ///       这一档（条目身份）用 +0/+1/+2/+3…，两边别再串。
    static int resourceKindRole() { return Qt::UserRole + 3; }

    static QString folderListName() { return QStringLiteral("folderList"); }
    static QString instanceListName() { return QStringLiteral("instanceList"); }
    static QString instanceFolderLabelName() { return QStringLiteral("instanceFolderLabel"); }
    // 「游戏文件夹」那两个按钮、以及下面那句提示。这里**必须**有 objectName ——
    // 移除那个按钮的文案这一阵子正在从「删除文件夹」改成「移除文件夹…」（那个词会让人
    // 以为盘上的文件也没了），按文案找控件的写法会在改文案的第一次跑就变成"找不到控件、
    // 后面全不跑"。文案另有 *Text()，只用来核对显示内容。
    static QString addFolderButtonName() { return QStringLiteral("addFolderButton"); }
    static QString removeFolderButtonName() { return QStringLiteral("removeFolderButton"); }
    static QString folderHintLabelName() { return QStringLiteral("folderHintLabel"); }
    static QString addFolderText() { return QStringLiteral("添加文件夹…"); }
    /// NOTE: 带省略号 —— 点下去先弹一个确认框，而那个框是这一步**唯一**说清
    ///       "只动列表、盘上文件一个字节都不动"的地方（用户最容易误会的就是这一步）。
    static QString removeFolderText() { return QStringLiteral("移除文件夹…"); }
    // 实例页右栏那三个按钮。**全按钮驱动、不做右键菜单** —— 仓库里右键菜单零先例，
    // 而且验证工程跑在 offscreen 上，QMenu::exec() 会阻塞在事件循环里，几乎驱动不了。
    static QString renameInstanceButtonName() { return QStringLiteral("renameInstanceButton"); }
    static QString groupInstanceButtonName() { return QStringLiteral("groupInstanceButton"); }
    static QString instanceSettingsButtonName() {
        return QStringLiteral("instanceSettingsButton");
    }
    /// 「停止游戏…」。和上面三个**同一排**（它作用的也是"列表里选中的那个实例"）。
    static QString stopGameButtonName() { return QStringLiteral("stopGameButton"); }
    /// 「导入整合包…」。它和上面那三个**不是一回事**：那三个作用于列表里选中的实例，
    /// 这个是造一个新实例出来。所以它单独一行、摆在它们上面（见 .cpp 里那一段）。
    static QString importModpackButtonName() { return QStringLiteral("importModpackButton"); }
    // 按钮上的字。验证工程**不要**拿它们当身份标识（那是显示内容），只用来核对文案。
    static QString renameInstanceText() { return QStringLiteral("重命名…"); }
    static QString groupInstanceText() { return QStringLiteral("分组…"); }
    static QString instanceSettingsText() { return QStringLiteral("实例设置…"); }
    /// NOTE: 带省略号 —— 按 Qt 的规矩，点了之后还要弹框问点什么的，文案结尾就带省略号。
    ///       这一个的框尤其要紧：它要说清「停」等于**强杀**、游戏不会存档（见
    ///       MainWindow::onStopGameClicked()）。
    static QString stopGameText() { return QStringLiteral("停止游戏…"); }
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
    /// 「未分组」那一格的名字。它是个**虚拟**的组（不进文件），只在存在真实分组时
    /// 才画出来 —— 一个组都没有的时候列表是平铺的，不插任何组头。
    static QString ungroupedGroupText() { return GroupDialog::ungroupedText(); }
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
    /// 左边那两行字各自的宽度上限 = 那一格的宽度减掉右边那个箭头和它左边的间距。
    /// 两行都封这个顶，布局就永远不会被长名字撑开。
    static int bottomBarRunStateTextWidth() {
        return bottomBarRunStateColumnWidth() - bottomBarChevronSpacing() - bottomBarChevronSize();
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

    static QString runStateButtonName() { return QStringLiteral("runStateButton"); }
    static QString accountButtonName() { return QStringLiteral("accountButton"); }
    /// 那两颗按钮点开之后**向上**弹出来的那块面板。名字挂在这里是为了让验证工程
    /// 能按 objectName 把它找回来量几何（它是个 Qt::Popup 窗口，父子关系照旧）。
    static QString bottomBarPanelName() { return QStringLiteral("bottomBarPanel"); }
    /// 面板里第 @p index 行的 objectName（现在放的是占位文字，见 .cpp 里那段）。
    static QString bottomBarPanelRowName(int index) {
        return QStringLiteral("bottomBarPanelRow%1").arg(index);
    }
    /// 那两颗按钮右边那个朝上箭头的 objectName。两颗**共用**这一个名字 ——
    /// findChild 只会拿到其中一颗，要两颗一起用 findChildren。
    static QString bottomBarChevronName() { return QStringLiteral("bottomBarChevron"); }
    static QString runStateLabelName() { return QStringLiteral("runStateLabel"); }
    static QString runStateDetailLabelName() { return QStringLiteral("runStateDetailLabel"); }
    static QString progressBarName() { return QStringLiteral("progressBar"); }
    static QString statusLabelName() { return QStringLiteral("statusLabel"); }

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
    static QString showGameLogCheckName() { return QStringLiteral("showGameLogCheck"); }
    static QString processPriorityComboName() { return QStringLiteral("processPriorityCombo"); }
    static QString gameWindowWidthSpinName() { return QStringLiteral("gameWindowWidthSpin"); }
    static QString gameWindowHeightSpinName() { return QStringLiteral("gameWindowHeightSpin"); }
    /// 「宽 × 高」中间那个乘号。它也要有名字：验证工程量这三格的几何时得能取到它。
    static QString gameWindowSizeTimesLabelName() { return QStringLiteral("gameWindowSizeTimes"); }
    /// 「启动选项」那一组里那几个勾选框/数字框的文案。
    /// 「显示游戏日志」这个串是**用户原话**（2026-09-24 提的需求），别改写成别的说法。
    static QString showGameLogCheckText() { return QStringLiteral("显示游戏日志"); }
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

    // ---------- 「已安装版本」下拉框的占位文案 ----------
    // 空状态有两种，由 MainWindow::refreshInstalledVersions() 按当前情形切换：
    //   NoInstalledVersion —— 磁盘上一个装好的版本都没有
    //   ChooseVersion      —— 有可选版本，但用户还没选（含"上次启动的版本已经没了"）
    // 两个串都必须非空：空串会让 QComboBox 在往里灌数据时静默选中第 0 项，
    // 见本文件 .cpp 里的说明。放在这里当唯一出处，免得界面初始值和 MainWindow
    // 刷新时的值各写一份、慢慢就写歪了。
    static QString noInstalledVersionPlaceholder() { return QStringLiteral("暂无已安装版本"); }
    static QString chooseVersionPlaceholder() { return QStringLiteral("请选择一个版本"); }

    // ---------- 底栏左下角"实例运行中"那行的正文 ----------
    // 放在这里当唯一出处：MainWindow 拿它填文字，验证工程拿它做断言，两边不会写歪。
    static QString runStateRunningText() { return QStringLiteral("实例运行中"); }
    /// 一个实例都没在跑时那一行写什么。
    ///
    /// NOTE: 2026-09-23 用户要的：「没有实例运行的时候就写实例未运行」（以前是整格藏起来、
    ///       底下空一块）。改成一句话还有个连带的好处 —— 那一格的文字**任何时候都非空**，
    ///       布局上也就没有"从无到有"这一步了（那一步正是"中间那根条突然缩短"的来源，
    ///       见 MainWindowUI.cpp 里 runStateButton 那段）。
    static QString runStateNotRunningText() { return QStringLiteral("实例未运行"); }
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
    /// @return 只有"在主页、且选中的就是正在跑的那个实例"这一种情况返回 false。
    ///
    /// 只有这一种情况能省掉名字：用户眼前这个界面说的就是它。其余情况（在别的选项卡
    /// 上，或者下拉框里选着另一个版本）都得靠名字说清楚跑的是谁 —— 尤其是"选中的和
    /// 在跑的不是同一个"这种最容易看错的场面。
    ///
    /// 做成静态纯函数是为了能把这四种组合逐条钉住：它是这条规则的唯一出处，
    /// MainWindow::updateBottomBar() 只负责把结果画出来。
    static bool runStateShowsDetail(bool homeTabCurrent, const QString &selectedVersionId,
                                    const QString &runningVersionId) {
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
    QCheckBox *getOfflineModeCheckBox() const { return offlineModeCheckBox; }
    QPushButton *getAddFolderButton() const { return addFolderButton; }
    QPushButton *getRemoveFolderButton() const { return removeFolderButton; }
    /// 列表下面那句提示（「自带的那一份移不掉…盘上的文件夹和文件都留着。」）。
    QLabel *getFolderHintLabel() const { return folderHintLabel; }
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
    QPushButton *getStopGameButton() const { return stopGameButton; }
    /// 「导入整合包…」。它不看"有没有选中实例"，任何时候都能点（和上面那三个一个口径：
    /// 能不能点由状态决定的话，用户会先花时间猜"为什么它是灰的"）。
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
    QCheckBox *getShowGameLogCheck() const { return showGameLogCheck; }
    QComboBox *getProcessPriorityCombo() const { return processPriorityCombo; }
    QSpinBox *getGameWindowWidthSpin() const { return gameWindowWidthSpin; }
    QSpinBox *getGameWindowHeightSpin() const { return gameWindowHeightSpin; }
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
    // NOTE: 产品代码没有取过底栏本身（只取它里面的几个标签/进度条），只有验证工程
    //       用它来量底栏的几何。
    QFrame *getBottomBar() const { return bottomBar; }
    /// 底栏两头那两颗按钮（左：实例运行状态 · 右：头像/用户名）。点它们会**向上**弹出一块
    /// 面板，内容现在是占位文字（用户 2026-09-23 要的，见 .cpp 里 buildBottomBarPanel()）。
    QPushButton *getRunStateButton() const { return runStateButton; }
    QPushButton *getAccountButton() const { return accountButton; }
    QLabel *getAvatarLabel() const { return avatarLabel; }
    QLabel *getAccountLabel() const { return accountLabel; }
    QLabel *getAccountTypeLabel() const { return accountTypeLabel; }
    // 进度只出现在底栏中间那一格，不进日志（旧版是钉在 QStatusBar 上的）
    QProgressBar *getProgressBar() const { return progressBar; }
    QLabel *getStatusLabel() const { return statusLabel; }
    // 底栏左侧：第一行是状态正文，第二行是实例名（只在需要区分时显示）
    QLabel *getRunStateLabel() const { return runStateLabel; }
    QLabel *getRunStateDetailLabel() const { return runStateDetailLabel; }

private:
    void setupUi(QMainWindow *parent);
    /// 搭设置页：滚动区里一张两列表格，列 0 是选项名称、列 1 是控件（3:7）。
    void setupSettingsPage(QWidget *page);
    /// 搭底部那条通栏状态条（左：实例运行状态，中：进度，右：头像/用户名 ——
    /// 2026-09 把左右两格对调过，见 .cpp 里的说明）。
    void setupBottomBar(QWidget *parent, QVBoxLayout *mainLayout);

    // 控件指针
    //
    // NOTE: 全部显式初始化成 nullptr（2026-09-24 加的）。这不是为了好看：下面那些
    //       访问器和 isHomeTabCurrent() 拿"空指针"当"setupUi() 还没跑完"的判据 ——
    //       成员没有初值时，那个判据读的是**未初始化的值**（UB），只是现在的调用点
    //       全都排在 setupUi() 之后才没炸。
    // NOTE: 新加控件指针时**一并写上 = nullptr**，别让它单独破例。
    QPushButton *startButton = nullptr;
    QPushButton *accountSkinSettingsButton = nullptr;
    QCheckBox *offlineModeCheckBox = nullptr;
    QPushButton *addFolderButton = nullptr;
    QLabel *folderHintLabel = nullptr;
    QPushButton *removeFolderButton = nullptr;
    QListWidget *folderList = nullptr;
    QTreeWidget *instanceList = nullptr;
    QPushButton *renameInstanceButton = nullptr;
    QPushButton *groupInstanceButton = nullptr;
    QPushButton *instanceSettingsButton = nullptr;
    QPushButton *stopGameButton = nullptr;
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
    QCheckBox *showGameLogCheck = nullptr;
    QComboBox *processPriorityCombo = nullptr;
    QSpinBox *gameWindowWidthSpin = nullptr;
    QSpinBox *gameWindowHeightSpin = nullptr;
    QCheckBox *versionNameCustomCheck = nullptr;
    QCheckBox *versionNameRealCheck = nullptr;
    QCheckBox *versionNameLoaderCheck = nullptr;
    QCheckBox *versionNameLoaderVersionCheck = nullptr;
    SkinViewer *skinViewer = nullptr;
    QLabel *skinHintLabel = nullptr;
    QPushButton *accountSkinButton = nullptr;
    QTabWidget *tabWidget = nullptr;
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
};
