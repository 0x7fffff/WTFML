/**
 * @file InstanceSettingsDialog.hpp
 * @brief 「实例设置…」对话框。**2026-09-27 起左栏是一列按实例现算的页表**（概览 / 启动
 *        ＋ 存档 / 服务器列表 / 资源包 / 数据包 / 模组 / 光影包 / 原理图里"这个实例用得上
 *        且用户没勾掉"的那些，见 pinnedPageKeys() / optionalPageKeys()），右边一块堆叠页；
 *        要显示哪几页在概览页最下面那张勾选表里调（`settingsPages`，落在 wtfml.meta）。
 *        中间那一列小类（2026-09-26 那版的三栏）已经拆掉了。
 *        再往前它是一张通到底的表：上面一行实例名，下面给单个实例覆盖启动五项
 *        （Java 路径 / Xmx / Xms / 内存自动 / 额外 JVM 参数），每项都是两级
 *        「跟随全局 / 自定义」。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 进参出参都是**值**，本文件不 include 任何 src/ 下的头 —— 和 GroupDialog 同一条
 *       约定（ui/ 不依赖业务层）。所以这里的 Override 是它自己的一份，和
 *       src/settings/EffectiveJavaSettings.hpp 里的 JavaSettingsOverride 逐字段相同，
 *       由 MainWindow 在两边转换。
 *
 * 【哪五项能覆盖，别的为什么不能】
 *
 * 只做**启动五项**。其余设置项各有各的归属，混进来只会让"这个实例"变成一个说不清的概念：
 *   - 下载源（Download/Source）：下载那一刻的决定，和"启动这个实例"无关；
 *   - 启动后（Launcher/AfterLaunch）：那是启动器自己的行为，不是这个实例的属性；
 *   - 版本名格式那四个开关：读侧 nameFormat 早就支持，本轮不做界面。
 *
 * 【约定：对话框只改自己手里那份值，不碰磁盘】
 *
 * **这一层一个字节都不写盘**：它把控件读成一份值（override() / appendJvmArgs() /
 * iconChoice() …）交给调用方。落盘那一步**只有一条路**：`MainWindow::applyInstanceSettings()`
 * 把这些值读成一份 `InstanceSettingsController::Edit`，再交给
 * `InstanceSettingsController::apply()`（2026-10-15 从主窗口搬过去的；主窗口那一层只剩
 * "读对话框"这个适配器和一个按 `Notice::Kind` 挑中文的地方）。
 *
 * ★ 2026-09-26 起这一页**没有「确定」也没有「取消」**（用户原话：「实例设置里面的那个
 * 取消和确定删了，修改了的设置实时写入」）。于是"什么时候写"从"点确定那一刻"变成了
 * "值一改就写" —— 报信的机制是 `settingsEdited()` 那一发，而"写什么"一个字都没变：
 * 仍是上面那几问现读（所以实时写和从前按确定写，落到盘上的字节是一样的）。
 *
 * 唯一的例外是**改名**：那一格动的是磁盘上的目录名，它走自己那条信号
 * （renameRequested()，回车 / 失焦才发），理由写在那个信号上面。
 *
 * 【约定：控件的值就是结果（没有内部影子状态）】
 *
 * override() **现场读控件**，不缓存一份 m_override。这样"摆好控件 → 读结果"在验证
 * 工程里可以被直接驱动，而不用先走一遍 exec()（模态窗在 offscreen 下驱动不了）。
 */
#pragma once

#include <QDialog>
#include <QHash>
#include <QIcon>  // ServerRowStatus 里那一枚（服务器图标）
#include <QJsonObject>  // pageChoices() / setPageChoices()（"藏起来的那几页"）
#include <QList>
#include <QPair>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <optional>

// `openConfigEditor()` 那个进参是**按值**要一份 `ConfigFileText::Loaded`（见 .cpp 里
// 那段说明：`encode()` 要靠它还原行尾和 BOM），所以这儿不能只前置声明。
#include "ConfigFileText.hpp"
// `openWorldSettings()` 的进参是 `WorldSettingsPage::WorldView` —— **嵌套类型**，
// 前置声明一个 `class WorldSettingsPage;` 是说不清它的，得把定义拿进来。
#include "WorldSettingsPage.hpp"

class ConfigFileEditorPage;
class InstanceFilesPage;
class MemorySlider;

class QCheckBox;
class QComboBox;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QMimeData;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QWidget;
class ResourcePackPage;

class InstanceSettingsDialog : public QDialog {
    Q_OBJECT

public:
    ResourcePackPage *resourcePackPage() const { return m_resourcePackPage; }
    /// 这个实例覆盖的那几项。没有值 = 跟随全局（空串也算有值，见 EffectiveJavaSettings.hpp）。
    ///
    /// NOTE: 「窗口模式」这一项存的是**代号字符串**（`windowMode` 那一格），不是枚举 ——
    ///       档位那个枚举（GameWindowMode）在 `src/settings/GameWindowSettings.hpp` 里，
    ///       而 `ui/` 不许 include `src/`（分层铁律）。这一页只认"标签 + 代号"这一对，
    ///       代号长什么样它不管；代号表由调用方喂进来（见构造函数那个 @c windowModes）。
    ///       于是这里既不需要一份枚举的拷贝，也不用在 ui/ 里再写一遍那三个串。
    struct Override {
        std::optional<QString> javaPath;
        std::optional<int> xmx;
        std::optional<int> xms;
        std::optional<bool> autoMemory;
        std::optional<QString> extraJvmArgs;
        std::optional<QString> windowMode;
        std::optional<int> windowWidth;
        std::optional<int> windowHeight;
    };

    /// 「窗口模式」那个下拉框里的一项：显示的字 + 存进 meta 的代号。
    ///
    /// 为什么是调用方喂而不是这一页自己写：那三个代号是**持久化格式的一部分**，唯一出处是
    /// `src/settings/GameWindowSettings.cpp` 里的 gameWindowModeToken()。在 ui/ 里再写一遍
    /// 就等于开第二个出处 —— 改了一处对不上时的症状是"选了档位、盘上没变"，不报任何错。
    struct WindowModeChoice {
        QString label;  ///< 下拉框上显示的字（调用方 tr 过）。
        QString token;  ///< 存进 wtfml.meta 的代号。
    };

    /// 全局那一层的值。只用来写「跟随全局（当前 …）」那句话 —— 用户要能看见自己
    /// 跟随的到底是什么数，否则"跟随全局"就是一格写不出内容的空白。
    struct GlobalValues {
        QString javaPath;
        int xmx = 0;
        int xms = 0;
        bool autoMemory = true;
        QString extraJvmArgs;
        /// 「最大内存」那一格允许拖到的上限（MB）—— 这台机器的物理内存，**由调用方算好
        /// 喂进来**。`ui/` 不许依赖 `src/`，这一页自己也读不到系统内存。
        ///
        /// NOTE: <= 0 表示调用方没给（验证工程里那些只验结构的构造点），退回内置的
        ///       MainWindowUI::memoryLimitMaxMb()。**别**把 0 当成"这台机器没有内存"。
        int memoryCapMb = 0;
        /// 全局那一档「窗口模式」的**代号**（和 WindowModeChoice::token 同一个出处）。
        /// 它是这一项「跟随全局」时下拉框要摆出来的那一项 —— 不给的话摆第 0 项。
        QString windowMode;
        /// 全局那一档「窗口模式」的**显示名**（"小窗"/"最大化"/"全屏"）。只用来写
        /// 「跟随全局（当前 …）」那句话 —— 和 javaPath 那几项同一个道理：用户得能看见
        /// 自己跟随的到底是什么。空串 = 调用方没给（那句话说成"跟随全局"不带括号）。
        QString windowModeLabel;
        /// 全局的窗口大小。**它是"跟随全局"那一档下面那两个数字框的禁用值来源**吗？不是 ——
        /// 那两个框在「跟随全局」时被整格禁掉，值由全局那份决定；这两个数只用来填
        /// 「跟随全局（当前 854×480）」那一句。
        int windowWidth = 0;
        int windowHeight = 0;
    };

    /**
     * @brief 构造对话框。
     * @param instanceName 显示在标题里的实例名（用户认得出改的是哪一个）。
     * @param instanceId 这个实例**现在的目录名**（`versions/<它>`），也是「实例名」那一格
     *        的初值 —— 改了它就是要改名（见 instanceName()）。
     *
     * NOTE: 它和 instanceName 是**两个不同的东西**，都要传：显示名可能被用户改过、
     *       也可能被"版本名格式"那套开关拼成了别的东西，而目录名是磁盘上真正的身份。
     *       拿显示名当初值的话，用户点进设置页什么都没改、顺手改了别的某一格，实例就被改成了
     *       一个奇怪的名字。
     * @param current 这个实例**现在**的覆盖（wtfml.meta 里读出来的）。
     * @param global 全局有效值（GlobalJavaSettings::effective()），用来写「当前 …」。
     * @param appendJvmArgs 这个实例现有的「追加参数」（wtfml.meta 的 jvmArgs 数组）。
     *        它不是覆盖层的一项，和全局没有"跟随"关系，所以单独一行、没有那对单选钮。
     * @param warnJavaMismatch 这个实例的「Java 版本不符时提醒」。和 appendJvmArgs 一样
     *        不是覆盖层的一项（存 meta 的另一个键），所以也没有那对单选钮。
     * @param windowModes 「窗口模式」那个下拉框里的档位表（显示的字 + 存进 meta 的代号，
     *        **顺序就是界面上的顺序**）。由调用方喂是分层的要求，理由见 WindowModeChoice
     *        上面那一段。空表 = 不建「窗口模式」这一行（验证工程里那些只验别的结构的构造点
     *        就传空表）—— 那时「窗口大小」那一行照样建，它不依赖这份表。
     * @param parent 父窗口。
     */
    InstanceSettingsDialog(const QString &instanceName, const QString &instanceId,
                           const Override &current, const GlobalValues &global,
                           const QStringList &appendJvmArgs, bool warnJavaMismatch,
                           const QList<WindowModeChoice> &windowModes = QList<WindowModeChoice>(),
                           QWidget *parent = nullptr);

    /// 用户摆好的覆盖。每一项各自：那行选了「自定义」就有值，选了「跟随全局」就没有值。
    ///
    /// ★ 2026-09-28：「窗口大小」这一项有**两个**值（宽和高），但界面上是**一行**、共用
    ///   一对单选钮 —— 所以要么两个都有值、要么两个都没有。实现上就是"这一行选自定义时
    ///   两个框的值一起收，选跟随时两个都不收"（见 .cpp 里那一行）。**不会**出现只有宽
    ///   有值的情况。
    ///
    /// NOTE: ★ 2026-10-18 起「内存分配」那一档整行撤掉了（用户原话「内存分配和最大内存
    ///       这玩意完全就是重复的，只保留那个拖动的」）⇒ 这里**不再产出 autoMemory**
    ///       覆盖。`Override::autoMemory` 字段还在（老数据、老测试要用），只是这一页
    ///       永远不填它；读侧 `resolveJavaSettings()` 也当它不存在。
    Override override() const;

    /// 高级选项里那两行之一：**追加**在命令行最后的 JVM 参数（写进 meta 的 jvmArgs，
    /// 是数组不是空格分隔的串 —— 参数本身可能含空格）。
    QStringList appendJvmArgs() const;

    /// 高级选项里那两行之一：这个实例的「Java 版本不符时提醒」。true = 提醒（默认）。
    ///
    /// NOTE: **刻意不进 Override**。那五项是"值覆盖"，每一项都能"跟随全局"；这是一个
    ///       行为开关，存的是 wtfml.meta 的另一个键（skipJavaVersionWarning），由
    ///       MainWindow 经 updateVersionMeta() 合并写进去。混进 Override 会让
    ///       isEmpty()、javaOverrideFromJson/ToJson 都跟着改，而那五项和那个键的
    ///       生命周期本来就不一样。
    bool warnJavaMismatch() const;

    /// 高级选项那一块现在是不是展开着。默认折叠。
    bool advancedExpanded() const;

    /// 用户在「实例名」那一格里现在写着什么（**没有** trim）。它和构造进来的 instanceId
    /// 不一样时 = 用户要改名；一样（或者只有空格差别）时 = 不改名。
    ///
    /// NOTE: **改名这件事不在这里做** —— 这一层只把控件读成一份值（和 override() 一样），
    ///       真的改目录在 MainWindow::openInstanceSettings() 那一段里（先改名再写设置）。
    ///       本文件不 include 任何 src/ 下的头，也就够不着 renameInstance()。
    QString instanceName() const;

    /// 自动内存是不是**有效**开着（这一行自定义了就听它的，否则听全局的）。
    /// 界面用它决定下面两个数字框禁不禁用；验证工程用它当"这个实例这次是自动还是手动"
    /// 的判据。
    bool effectiveAutoMemory() const;

    /// 刷新每一行下面那句话，以及两行内存的可用性（自动档下整行禁用）。
    /// 每次那对单选钮或自动勾选框变化都会调它。
    void refreshHints();

    // ---------- 左栏那一列页（2026-09-27 重画）----------
    //
    // 用户 2026-09-27 原话（逐字）：「针对每个实例，在实例设置里个性化自定义，概览里面调整
    // 是否显示，把整个文件管理换了，"快捷方式"放到概览的下方，剩下的和概览/启动等并列，
    // 是否显示的调整放到"快捷方式"的下方，默认情况下展示（记住都按这个顺序）存档 服务器
    // 列表 资源包 数据包 如果是有模组加载器的版本，在此基础上增加模组，有光影加载器的
    // 在模组后面加光影，有 litematica 的在最后加原理图」。
    //
    // 于是**从前的三栏（大类 | 小类 | 内容）没了**：左边只剩一列，摆的是**按这个实例现算
    // 出来**的一份页表（`shownEntryKeys()`）：
    //
    //   概览 启动                                ← 钉死的那两条（`pinnedPageKeys()`），关不掉
    //   存档 服务器列表 资源包 数据包 模组 光影包 原理图  ← `optionalPageKeys()` 的顺序，
    //                                             可用 + 没被用户勾掉才出现
    //
    // 右栏还是那份堆叠页，**顺序一个字没动**（`filePageKeys()`，见 `pageCount()`）；左栏的
    // 先后与堆叠页的先后从此互不相干 —— 跳页一律按**键**（`stackPageIndexOf()`），不按行号。
    //
    // 用户能调的是"显示哪些页"：概览页最下面那张勾选表（`pageCheckName()`）；勾掉哪几页记在
    // 这个实例自己的 wtfml.meta 里（`settingsPages`，见 `pageChoices()` 与 setPageChoices()）。
    //
    // 键表与 objectName 的写法照抄设置页（settingsCategoryKeys / settingsPanelName）：
    // **身份走 objectName**，验证工程不靠位置认控件。

    /// 左栏里**钉死的那两条**的键（顺序就是界面上从上到下的顺序）。
    ///
    /// 用户 2026-09-27：「概览/启动 不能关」（原话里的「快捷方式」也是一条，但它 2026-09-27
    /// 起不再是一页了 —— 那九颗按钮并进了概览页，见 shortcutTexts()）。
    static QString overviewCategoryKey() { return QStringLiteral("overview"); }
    static QString launchCategoryKey() { return QStringLiteral("launch"); }
    static QStringList pinnedPageKeys() {
        return {overviewCategoryKey(), launchCategoryKey()};
    }
    static QStringList pinnedPageTexts() {
        return {QStringLiteral("概览"), QStringLiteral("启动")};
    }

    /// **能勾、能关**的那几页的键，**顺序就是用户点名的那一串**
    /// （「默认情况下展示（记住都按这个顺序）存档 服务器列表 资源包 数据包 …模组…光影…原理图」）。
    ///
    /// NOTE: 这一份是 `filePageKeys()` 去掉「配置文件」之后的一个**排列**（同一批键、不同
    ///       次序），验证工程钉着这个等式。堆叠页那边的顺序（filePageKeys()）是另一件事：
    ///       它按"跳板在最前 / 往里放 / 从里面看"排过，且 `serverPageKey()` 那句"最后那一个"
    ///       还挂在它身上 —— 两边的次序**本来就该分**，别再拿其中一张去算另一张。
    static QStringList optionalPageKeys() {
        return {QStringLiteral("save"),    QStringLiteral("server"),  QStringLiteral("resourcepack"),
                QStringLiteral("datapack"), QStringLiteral("mod"),   QStringLiteral("shader"),
                QStringLiteral("schematic")};
    }
    static QStringList optionalPageTexts() {
        return {QStringLiteral("存档"), QStringLiteral("服务器列表"), QStringLiteral("资源包"),
                QStringLiteral("数据包"), QStringLiteral("模组"),   QStringLiteral("光影包"),
                QStringLiteral("结构/原理图")};
    }

    /// 第 @p key 那一页在界面上叫什么（**唯一出处**：左栏那一条的字、勾选框上的字、
    /// 报错里点名都用它）。认不出来的键回空串。
    static QString pageText(const QString &key);

    /// 堆叠页里那些**文件页**的键，**顺序就是堆叠页里的顺序**。
    ///
    /// NOTE: 左栏那一列的先后**不是**这个（见 optionalPageKeys()）：这一张是"哪几页存在、
    ///       在堆叠页里排第几"，那一张是"用户眼里从上到下怎么排"。
    ///
    /// NOTE: 「配置文件」是 2026-09-27 加的那一页（用户要的："在启动器就直接编辑模组的
    ///       config.json"）。**插在原理图与存档之间**，不是为了好看：`serverPageKey()` 的
    ///       唯一出处是"最后那一个"，往尾巴上接会把它那句话变成假的。
    static QStringList filePageKeys() {
        return {QStringLiteral("mod"),    QStringLiteral("resourcepack"),
                QStringLiteral("datapack"), QStringLiteral("shader"), QStringLiteral("schematic"),
                QStringLiteral("config"), QStringLiteral("save"),   QStringLiteral("server")};
    }
    static QStringList filePageTexts() {
        return {QStringLiteral("模组"),   QStringLiteral("资源包"),
                QStringLiteral("数据包"), QStringLiteral("光影包"), QStringLiteral("结构/原理图"),
                QStringLiteral("配置文件"), QStringLiteral("存档"), QStringLiteral("服务器列表")};
    }

    /// 「配置文件」那一页的键。
    ///
    /// NOTE: 这一页**一个入口都没有**（用户 2026-09-27 原话「我说配置文件收起来」）—— 它
    ///       不在 optionalPageKeys() 里、所以左栏和勾选表里都没有它，只有验证工程拿
    ///       showPage() 够得着。要放出来：把它接进 optionalPageKeys()（并给一条默认关的
    ///       选择），别的地方一个字都不用改。
    static QString configPageKey() { return QStringLiteral("config"); }

    /// 「服务器列表」那一页的键（也就是 filePageKeys() 里最后那一个）。
    ///
    /// NOTE: **唯一出处** —— MainWindow 那两条接线（自动测速、以及"这一页里那两颗按钮分别
    ///       干什么"）都按它认页。写第二遍字面量的症状是**静默失效**：自动测速那一发
    ///       永远不发（少一句话、不报错），而且页还是照开的。
    static QString serverPageKey() { return QStringLiteral("server"); }

    /// 堆叠页里「文件页」那些页的起始下标（0/1 是概览与启动，排在前面）。
    /// 换言之 `stackIndex = filePageFirstIndex() + filePageKeys() 里的下标`。
    static int filePageFirstIndex() { return 2; }
    /// 堆叠页一共有几页。
    static QStringList lifecyclePageKeys() {
        return {QStringLiteral("worldbackup"), QStringLiteral("screenshots"),
                QStringLiteral("replay"), QStringLiteral("structure"),
                QStringLiteral("integrity"), QStringLiteral("repair"),
                QStringLiteral("logs"), QStringLiteral("crash"), QStringLiteral("diagnosis"),
                QStringLiteral("snapshots"), QStringLiteral("export"),
                QStringLiteral("migration"), QStringLiteral("upgrade"),
                QStringLiteral("integritymenu"), QStringLiteral("diagnosticsmenu"), QStringLiteral("operationsmenu")};
    }
    static QStringList lifecyclePageTexts() {
        return {QStringLiteral("世界备份"), QStringLiteral("截图"), QStringLiteral("Replay"),
                QStringLiteral("结构"), QStringLiteral("完整性检查"), QStringLiteral("实例修复"),
                QStringLiteral("日志"), QStringLiteral("崩溃报告"), QStringLiteral("启动诊断"),
                QStringLiteral("快照与恢复"), QStringLiteral("导出整合包"),
                QStringLiteral("实例迁移"), QStringLiteral("版本升级"),
                QStringLiteral("完整性"), QStringLiteral("诊断"), QStringLiteral("实例操作")};
    }
    static int pageCount() { return filePageFirstIndex() + filePageKeys().size() + lifecyclePageKeys().size(); }

    /// @p key 那一页在堆叠页里占第几格。认不出来的键回 -1（调用方要判）。
    ///
    /// 两段：钉死的那两条（概览 / 启动）在**头**、文件那八页在 `filePageFirstIndex()` 之后
    /// —— 所以这个函数是"键 → 下标"那张表的**唯一出处**，别处的 `filePageFirstIndex() + 下标`
    /// 都该走它。（钉死那两条也得认：`showPage("overview")` 是最普通的一次调用，漏了它们
    /// 的症状是**这一页摆不动**——`showPage()` 判 -1 直接 return，界面上什么都不报。）
    ///
    /// NOTE: **按"页"认人是这一页唯一的跳页口径** —— 左栏那一列是按实例现算的，行号
    ///       随时会变（勾掉一条就少一格），拿左栏的行号去认页必然错位。
    static int stackPageIndexOf(const QString &key) {
        const int pinned = pinnedPageKeys().indexOf(key);
        if (pinned >= 0) {
            return pinned;
        }
        const int index = filePageKeys().indexOf(key);
        if (index >= 0) return filePageFirstIndex() + index;
        const int extra = lifecyclePageKeys().indexOf(key);
        return extra < 0 ? -1 : filePageFirstIndex() + filePageKeys().size() + extra;
    }

    /// 旧的左栏那一列（`QListWidget`）。★ 2026-10-16 起它已经**不是界面上的导航**了 ——
    /// 界面导航统一由 `navigationTreeName()` 那棵树承担，这一列在构造里 `hide()` 掉、只剩
    /// "旧访问接口"这一个身份（见 .cpp 里那句 `m_pageList->hide()`）。要量"左栏"请用树。
    static QString pageListName() { return QStringLiteral("instanceSettingsPageList"); }
    /// 左栏那棵导航树的 objectName（界面导航的唯一出处）。
    static QString navigationTreeName() { return QStringLiteral("instanceLifecycleTree"); }
    /// 左栏那棵导航树的宽度（**定宽**：它是一页的骨架，不跟着"现在露着哪几页"变 —— 见
    /// .cpp 里 `setFixedWidth()` 那一句和 ui_check 的 U0g-2）。
    static int navigationTreeWidth() { return 208; }
    static QString launchInstanceButtonName() {
        return QStringLiteral("instanceSettingsLaunchInstance");
    }
    static QString launchInstanceText() { return QStringLiteral("▶ 启动实例"); }
    static QString launchInstanceHint() {
        return QStringLiteral("从实例设置的任意页面启动当前实例");
    }
    static QString stackName() { return QStringLiteral("instanceSettingsStack"); }
    /// 堆叠页里某一页的 objectName（@param key 见 pinnedPageKeys() / filePageKeys()）。
    static QString panelName(const QString &key) {
        return QStringLiteral("instanceSettingsPanel-") + key;
    }
    /// 包着那一页的那层滚动区的 objectName（**一页一层**，见 .cpp 里 addPage() 那段）。
    static QString scrollName(const QString &key) {
        return QStringLiteral("instanceSettingsScroll-") + key;
    }
    /// 那一页里那张表单的 objectName（概览 / 启动 各有一张；文件管理那几页还没有）。
    static QString formName(const QString &key) {
        return QStringLiteral("instanceSettingsForm-") + key;
    }

    /// 摆到 @p key 那一页上。认不出来的键 = 什么都不做。
    ///
    /// **按"左栏里有没有这一条"分两种情况** —— 有就走"选中那一条"这条路（和用户点那一条
    /// 是同一个动作）；没有（「配置文件」这种没有入口的页、或者被用户勾掉了的页）就直接
    /// 摆右栏、把左栏的选中清掉。
    ///
    /// 这正是验证工程该用的那一个入口：**要验的是那一页本身，不是"用户点得着"** ——
    /// 没有入口的「配置文件」照样得有人在盯着（它只是先不给看，不是烂在那儿）。
    void showPage(const QString &key);

    /// ★ 关页会不会被"有没保存的改动"挡住（= `reject()` 里那两道闸）。
    ///
    /// 用户 2026-09-29 那条"拖进来一个整合包就离开实例设置"要**先问这一句**：那是程序
    /// 自己发起的关页，被闸挡住时不能当作"关掉了"继续往下走（底下那一格压着改了一半的
    /// 配置，继续走等于替用户丢掉它）。复述一遍那两条判据是错的 —— 此处**就是**它们的
    /// 定义处，`reject()` 也走这里。
    bool leaveBlockedByUnsavedEdits() const;

    /// 这一次拖放里**我们认的**本地文件（绝对路径，原样）。
    ///
    /// 认的只有"盘上存在的本地文件"（`QUrl::toLocalFile()` 非空那条路）—— 一张图片的
    /// 原始数据、一段从浏览器里拖出来的文本、一个 http 链接，一律**一个都不认**。
    /// 判断放在这一层是因为它同时是 `dragEnterEvent` 的准入判据：
    /// "哪些拖放我们接"只该有一处说法。
    static QStringList droppedFilePaths(const QMimeData *mime);

    /// 此刻摆在右栏那一页是**哪一页**（认不出来回空串）。`showPage()` 的反面。
    ///
    /// 给**"回来时还开在原来那一页上"**用（2026-09-28）：`MainWindow::openInstanceSettings()`
    /// 每次都是现 new 一个对话框（栈上局部对象，见那边），所以"上一次停在哪一页"这一层
    /// 自己也记不住 —— 想记住的一方得在 `execPage()` **返回之后**（那时对象还在栈上）
    /// 读一次这个值，下次开页时再 `showPage()` 放回去。
    ///
    /// NOTE: 判据只有堆叠页的 `currentIndex()` 一处（和左栏反查用的是同一个函数）——
    ///       另存一份"现在在哪一页"的影子状态迟早会和右栏对不上。
    QString currentPageKey() const;

    /// 这几条给验证工程问几何/结构用（认控件主要还是按 objectName）。
    ///
    /// ★ 2026-10-16：**左栏那条导航现在是 `navigationTree()`**。`pageList()` 返回的是
    ///   那一列旧的 `QListWidget`（构造里就 `hide()` 了）—— 它还是"旧访问接口"，但内容
    ///   已经不跟着页面表走了（还是老的四组十三行），**别再拿它量左栏**。
    QListWidget *pageList() const { return m_pageList; }
    QTreeWidget *navigationTree() const { return m_navigationTree; }
    QStackedWidget *stack() const { return m_stack; }

    /// 左栏**此刻**摆着哪几条（键，从上到下）。**唯一出处**就是它：左栏那一列、
    /// 验证工程、以及"当前这一页掉出去之后回哪一页"都由它说了算。
    QStringList shownEntryKeys() const;

    /// 勾选表上那些"显示哪些页"的勾选框：这一刻用户勾掉的页（键 → false）。
    ///
    /// ★ **只可能装 false，不会装 true**：勾选框只在那一页**可用**时能勾，而"可用"就是
    ///   "默认显示" —— 所以能存下来的只可能是"用户把它藏起来了"。这一份不是"选择表"，
    ///   是一张**藏起来的页**的名单（读侧 = `显示 = 可用 && 名单里没写它`）。
    ///   空对象 = 一页都没藏（写盘那边按空对象**删键**，见 MainWindow::applyInstanceSettings）。
    QJsonObject pageChoices() const;

    /// 灌这个实例**存在盘上**的那份选择（键见 optionalPageKeys()）。认不出来的键丢掉。
    /// 调用方（MainWindow）从 wtfml.meta 的 `settingsPages` 里取出来递进去。
    void setPageChoices(const QJsonObject &stored);

    /// 「模组」那一页在这个实例上**用得上吗**（= 它装没装模组加载器）。
    ///
    /// NOTE: 和 setShaderUsable() / setSchematicUsable() 一样，**判据不在这一层** ——
    ///       MainWindow 读 wtfml.meta 的加载器名 / 扫 mods/ 目录，算完把结论递进来。
    ///       ★ 默认是**可用**：忘了调 = 原版实例上也列出「模组」那一页。
    void setModUsable(bool usable);

    /// 「数据包」那一页在这个实例上**用得上吗**（= 这个游戏版本有没有数据包这回事）。
    /// 判据是"游戏版本 ≥ 1.13"（数据包是快照 17w43a（2017-10）引进的），出处见
    /// VersionManager::instanceSupportsDatapacks()。默认可用，同上。
    void setDatapackUsable(bool usable);

    // ---------- 文件管理那八页（2026-09-26，见上面 filePageKeys()）----------
    //
    // 八页分成三种形状：
    //   - **快捷方式**：一排按钮，每颗开一个文件夹。没有目录可列。
    //   - **服务器列表**：读 servers.dat，一行一个服务器。**它读不了盘**（那一层不 include
    //     src/），所以列表是调用方解析好递进来的（setServerList()）。
    //   - 中间那六页：同一个 InstanceFilesPage，只是喂进去的目录不同（见 setFileDirs()）。

    /// 这一页暂时用不上：**装了 Iris 或者 OptiFine 才谈得上光影包**。
    /// @param usable false 时那一页换成一句解释（"要装 Iris 或者 OptiFine"）。
    ///
    /// NOTE: 判据由调用方算 —— 它要读 wtfml.meta 的加载器名、还要扫 mods/ 里有没有
    ///       `iris*.jar`，而 ui/ 够不着这两样（见 .hpp 开头那段"本文件不 include 任何
    ///       src/ 下的头"）。这一层只负责把结果画出来。
    void setShaderUsable(bool usable);

    /// 同上：**装了 Litematica 才谈得上这一页**（那是 Litematica 自己的目录）。
    ///
    /// ★ 2026-10-10：这一页在界面上叫「结构/原理图」了（用户原话「那个原理图那个改成
    ///   结构/原理图」），但**判据一个字没动** —— 结构方块那一路还没做（用户点头"先看
    ///   可行性"，见 MainWindow 那边 `setSchematicUsable()` 的调用点）。所以现在是
    ///   "有投影模组 ⇒ 列出来且默认勾着"，正是用户要的那一半。
    void setSchematicUsable(bool usable);

    /// 「加入服务器」那一颗的文字与悬停提示（**临时占位**，2026-10-10）。
    ///
    /// @param text 空串 = 这一颗**收起来**（这个版本没有 `--quickPlayMultiplayer`）。
    ///
    /// 和 `InstanceFilesPage::setJoinText()` 是同一条规矩：**能不能用由调用方说了算** ——
    /// 「这个版本有没有那个启动参数」要读版本 json（`QuickPlay::supports()`），而 ui/ 够不着
    /// src/。这一层只负责画：文字非空就露出来、空就收起来。
    ///
    /// NOTE: 默认是**收着的**（构造里就是空文字）。所以那些不关心它的构造点（验证工程里
    ///       一大片）一个像素都不会变 —— 和 `setJoinText()` 逐字同一条理由。
    void setServerJoinText(const QString &text, const QString &tip);

    /// 服务器列表那一页的内容。
    /// @param servers 名字 → 地址，按文件里原来的顺序。
    /// @param problem 读不动的原因（空串 = 读到了）。
    ///
    /// NOTE: 「读到了但是一条都没有」和「没读到」要分开给 —— 前者那一页说"还没加过服务器"，
    ///       后者说清楚为什么没读到（像版本树那条"问到了就是没有"与"列表没取到"）。
    ///
    /// NOTE: 这个函数**把每一行的"现在什么样"清空**（图标、MOTD、状态那一格都回到
    ///       「还没测」）—— 它换掉的是整张表，旧的那些状态配的是旧的那些行。
    void setServerList(const QList<QPair<QString, QString>> &servers, const QString &problem);

    /// 一台服务器**现在**什么样（一次 ping 的结果，2026-09-27）。
    ///
    /// NOTE: 「连上去问一句」那一半在 `src/util/ServerPing` 里 —— 这一层连 QTcpSocket 都
    ///       够不着（ui/ 不 include src/），也**不该**自己去连：一次点击发多少网络请求是
    ///       调用方说了算的事。这一层只负责把结果画出来。
    struct ServerRowStatus {
        bool tested = false;    ///< 测过没有（没测过那一格写「还没测」）
        bool online = false;    ///< 通了没有（false 时看 error）
        QString error;          ///< 没通的理由（人话，来自 ServerPing）
        /// 没通的那一档里**是不是超时**（来自 ServerPing::Status::timedOut）。
        ///
        /// ★ 界面上**只写「超时」两个字**（用户 2026-09-27），而 `error` 那句话仍是全的
        ///   （「超时（网络不通）」/「超时（连上了，但服务器没有回答）」—— 两者是不同的事，
        ///   留在那一份里给排查用）。不许在这里拿 error 的字符串去认前缀：判据只有
        ///   ServerPing 那一处（`isTimeoutError()`）。
        ///
        /// ★ 缓存那一份（`fromCache()`）也带着这一位（盘上存着，见 ServerStatusCache::
        ///   Entry::timedOut）：没通那一档两处写**同一句字**。用户 2026-09-27 先是
        ///   「服务器那个会显示上次:没连上，也改成超时」、接着「‘上次’两个字也不要」——
        ///   于是这一位成了**两处唯一的判据**（那一句过去时的前缀已经整个删了）。
        bool timedOut = false;
        int latencyMs = -1;     ///< 延迟（毫秒；-1 = 不知道）
        int playersOnline = -1; ///< 在线人数（**-1 = 服务器没说**，和 0 个人不是一回事）
        int playersMax = -1;    ///< 人数上限（-1 = 服务器没说）
        QString motd;           ///< MOTD（已经洗过：颜色码去掉、多行合成一行）
        /// MOTD 的**原样**（`§` 码和换行都留着）—— 列表里那一格按游戏的样子画它
        /// （见 MainWindowUI::rowMotdRawRole()）。空串 = 没有这一份，那一格退回上面那条
        /// 洗过的路（悬停提示用的也是上面那份）。
        QString motdRaw;
        /// 这一份是**盘上缓存**里"上一次问到"的样子，不是刚刚问的。
        ///
        /// ★ 它**不再影响**状态那一格写出来的字（2026-09-27 用户：「‘上次’两个字也不要」
        ///   —— 那一句过去时的前缀原来就是唯一读它的地方）。这一位留着是因为**它还得管
        ///   别的**：没通那一档只有它分得出"理由要不要重说"（见 serverStatusLines() 里
        ///   那两条支路）。一有真的结果就整条盖掉（fromCache 回到假）。
        bool fromCache = false;
        QIcon icon;             ///< 服务器图标（favicon；服务器没给就是空的）
    };

    /// 把每台服务器现在的样子灌进去（**下标和 setServerList() 那一份一一对应**）。
    /// 短了的话后面那几条回到「还没测」、长了多出来的丢掉 —— 绝不按下标越界写。
    void setServerStatuses(const QList<ServerRowStatus> &statuses);

    /// 正在测速。true 时那颗「测速」换成「测速中…」并且点不动（连点三下发三遍请求是
    /// 用户没要的，而且每一遍都要等满超时）。
    void setServerPinging(bool pinging);

    /// 状态那一格要写的**那几行**（用户 2026-09-27：「在线延迟和人数换到两行来」）。
    ///
    /// 第一行是**延迟**（`42 ms`），第二行是人数（`7/20 人`）。没通那一档只有一行（那句话
    /// 本来就长；超时的那一档更短，只有「超时」两个字）。**唯一出处** —— 格子、悬停提示、
    /// 验证工程都看它。
    ///
    /// ★ 2026-09-27 傍晚改了两个字面：**「在线」那两个字删掉了**（用户原话：「把服务器那个
    ///   在线 文本删了，只保留延迟和人数，超时的只写超时」）。`42 ms` 摆在那儿本来就说明
    ///   通了 —— 那两个字占的是这一格最窄的一栏里的宽度，而值是零。同理超时那一档只写
    ///   「超时」，理由那半句（网络不通 / 它没答话）留在 `ServerRowStatus::error` 里。
    ///
    /// NOTE: 兜底那一条还留着「在线」两个字（见 .cpp）：**通了但一个数都没量到**时那一格
    ///       不能空着（空着和"还没测"长得一样），而那时候除了这两个字没别的可说。
    static QStringList serverStatusLines(const ServerRowStatus &status);

    /// 上面那几行的**单行写法**（用「 · 」串起来）—— 悬停提示用。验证工程也按它核对。
    ///
    /// NOTE: 它就是 `serverStatusLines().join(" · ")`（`42 ms · 7/20 人`），所以格子从
    ///       那几行变成两行之后，这里**一个字都没变**。
    static QString serverStatusText(const ServerRowStatus &status);

    /// 把六页目录列表**重新列一遍**。MainWindow 动过盘（删了 / 改了名）之后喊它。
    void refreshFilePages();

    // ---- 概览页那排常用目录的两段（§5.3）----
    /// 按「在盘的在外、其余收着」把那一排**重摆一次**（唯一出处，见 .cpp 里那段）。
    void relayoutShortcuts();
    /// 点「更多目录…」/「收起更多目录」。
    void toggleMoreShortcuts();

    // ---- 「配置文件」那一页的两条（见 openConfigFileRequested 那段）----
    /// 用户打开了 `config/` 底下的一条：目录 = 进去（这一层自己办），文件 = 发信号。
    void onConfigEntryActivated(const QString &path, bool isDir);
    /// 点「上级」：退到上一层（到根为止）。
    void leaveConfigDir();
    /// 按"现在在哪"决定那颗「上级」画不画。**进来、出去、换实例（setFileDirs）之后都要喊**。
    void updateConfigUpButton();

    /// 某一趟更新检测的结果（见 InstanceFilesPage::setUpdateResult）—— 转给**开得了
    /// 「可更新」那一格**的那几页（判据同构造里那一条）。
    ///
    /// 收在这一层的原因和 setFileIcon() 逐字相同（哪一页有这个路径只有这几页自己知道），
    /// 外加一条：**哪几页有「可更新」这一格也只有这一层知道**。
    ///
    /// @param outdatedPaths 查出来有新版本的那些文件的绝对路径（空表 + checked 为真
    ///        = 查过了、一条都没有）。
    /// @param checked       这个实例查过没有（"没查过"和"查过、没有"是两句话）。
    ///
    /// NOTE: 这个实例正在跑一趟检测的时候用户**换了实例**的话，回来的那一份是按
    ///       **那一趟的实例**递的 —— 所以调用方（MainWindow）只在自己那趟的实例上喊它
    ///       （见 finishUpdateCheck 里"先记账再递"那一段）。
    void setFileUpdates(const QStringList &outdatedPaths, bool checked);

    /// 给某一条文件行换上它的**真图标**（见 InstanceFilesPage::setIconForPath）。
    ///
    /// 收在这一层是因为**路径属于哪一页只有六页自己知道** —— 调用方（MainWindow）手上
    /// 只有一串路径，挨页去问一遍的话，"哪一页有这个路径"这件事就跑到了业务层去。
    /// 转给六页各一次（不在那一页的那一条会自己说"没有"，不是错误）。
    void setFileIcon(const QString &path, const QPixmap &icon);

    /// 某一条文件**认出来了是哪个资源** ⇒ 它的名字点一下能进它自己的那一页
    /// （见 InstanceFilesPage::setResourceForPath()）。
    ///
    /// 收在这一层的原因和 setFileIcon() 逐字相同（哪一页有这个路径只有这几页自己知道），
    /// 外加一条：**哪几页接这件事也只有这一层知道**（见 filesNameOpensResource()）。
    ///
    /// @param title 那个资源的名字（接口给的那个）。**空串 = 没认出来** —— 名字点不动。
    void setFileResource(const QString &path, const QString &title);

    /// 给验证工程用的访问器：某一页那个文件列表控件（键见 filePageKeys()）。
    InstanceFilesPage *filesPage(const QString &key) const;
    QList<QWidget *> shortcutButtons() const { return m_shortcutButtons; }
    QTreeWidget *serverTree() const { return m_serverTree; }
    QLabel *serverNotice() const { return m_serverNotice; }
    QPushButton *serverRefreshButton() const { return m_serverRefresh; }
    /// 「编辑」（改这一台的名字和地址，2026-10-10）。
    QPushButton *serverEditButton() const { return m_serverEdit; }
    /// 「添加」（多一台，2026-10-10；**常亮**，不跟着选中走）。
    QPushButton *serverAddButton() const { return m_serverAdd; }
    /// 「删除」（拿掉选中那一台，2026-10-10；没选中时灰着）。
    QPushButton *serverDeleteButton() const { return m_serverDelete; }
    /// 「加入服务器」（**临时占位**，2026-10-10；没这个参数的版本上它是收着的）。
    QPushButton *serverJoinButton() const { return m_serverJoin; }

    // ---------- 概览栏那三行（图标 / 分组 / 置顶，2026-09-26）----------
    //
    // 它们和「实例名」是一类东西：说的都是**这个实例自己是谁**（叫什么、长什么样、摆在
    // 哪儿），不是它的启动参数。所以归「概览」，不归「启动」。
    //
    // 【为什么用 setter 灌初值，而不是往构造函数塞参数】
    // 这个东西有十几处构造点（ui_check 那一片 + mw_check + ui_shot），加三个参数等于让
    // 每一处都改一行 —— 而其中绝大多数根本不关心图标和分组。构造函数只收"这一页一定
    // 要有的东西"，可选的那几样走 setter：和 MainWindowUI::setFolderRows() /
    // setGroupPickHandler() 是同一个套路。
    //
    // 【出参：控件的值就是结果（图标那一行是唯一的例外）】
    // 分组和置顶照旧"现场读控件"。图标那一行有十档（默认 + 八个内置 + 本地那张），其中
    // **「本地图片」这一档没有任何一个控件能表示它**（它是一张从盘上读来的图，不是一个
    // 勾选状态），所以这一行的当前值落在 m_iconChoice 里，八个按钮只是它的一幅画。
    // 这是全页唯一一处影子状态，别的每一行都还是"控件的值就是结果"。

    /// 这个实例的目录（绝对路径，`versions/<实例名>`）。
    ///
    /// NOTE: 它和「概览」栏那行图标**没有关系** —— 那一行的预览是调用方把图解好递进来的
    ///       （见 setIcon()：实例目录下那份 icon.png 由 MainWindow 读，ui/ 够不着 AppPaths）。
    ///       这一份是文件那几页用的：概览页上那排快捷按钮的「版本目录」开的就是它。
    void setInstanceDir(const QString &dir);

    /// 「文件管理」那几页各看哪个目录：**键 → 绝对路径**。
    ///
    /// 一共**九条**：有页的那七页各一条（`mod` / `resourcepack` / `datapack` / `shader` /
    /// `schematic` / `config` / `save`），外加两页的文件列表要的（`screenshots` / `logs` ——
    /// 它们没有自己的文件管理页，但左栏「截图」「日志」那两页要列目录）。`dir` 和 `server`
    /// 两个键不用给：前者开的是 setInstanceDir() 那一份，后者给的是**解析好的一份列表**
    /// （见 setServerList()）。
    ///
    /// NOTE: 上面那九条是**文件管理那几页**要的，一条不多。★ 2026-10-13（§5.3）那排按钮又
    ///       多要三路目录（`game` / `replay` / `crash`），它们**并在这同一份里一起灌**进来：
    ///       这一层只是"按某个键取一个路径"，多出来的键没有任何人当"文件管理页"读过（那几页
    ///       认的是 `filePageKeys()`），而 MainWindow 那边的 `instanceFileDirs()` 形状保持
    ///       不变（它有断言在数九条，为了这三条去动它不划算）。
    ///       ★ 2026-10-18 去重之后：`game` / `crash` 是概览那排快捷方式要的，`replay` 是
    ///       左栏「Replay」那一页要的 —— 三条都还有读者。
    ///
    /// NOTE: 路径**由调用方算好递进来**，这一层不自己拼 `<实例目录>/mods` —— 那几个目录名
    ///       是 MC 的约定，仓库里已经有唯一一份出处（src/download/ResourceCatalog 的
    ///       folderForKind()，整合包那一路也照着它写），ui/ 抄第二份迟早会跟它对不上。
    void setFileDirs(const QHash<QString, QString> &dirs);

    // ---------- 左侧那两类文件的「选中一行说一句」那一格（2026-10-18，§9.5）----------
    //
    // 「Replay」和「结构」两页今天只是把文件列出来。选中一行之后底下那一格要说的是：
    // 这份录像录了多久、什么版本、多少位玩家；这份结构多大、多少种方块、和这个实例的
    // 版本兼不兼容。**算的活不在这层**（读 `metaData.json` / gzip NBT 都在 src/），
    // ui/ 只负责"选中换了" → 发 `lifecycleFileInfoRequested()`，拿到答案再摆上去。
    //
    // NOTE: 和 `InstanceFilesPage` 那边同一套口子（`setInfoText(path, …)`）：答案带回
    //       它是**哪一条**的 —— 用户快速上下翻行时，前一发算得慢、后一发算得快，
    //       先回来的旧答案不能盖住新的（这里按路径比对，见 .cpp）。
    static QString lifecycleInfoLabelName(const QString &key) {
        return QStringLiteral("instanceLifecycleInfo_%1").arg(key);
    }
    /// 还没算出来时那一格摆的字（验证工程也拿它当"正在读"的判据）。
    static QString lifecycleInfoPendingText() { return QStringLiteral("正在读取…"); }

    /// 把 @p key 那一页的信息摆出来。
    /// @param path 这个答案讲的是**哪一条**（就是 `lifecycleFileInfoRequested()` 递出去的）。
    void setLifecycleInfoText(const QString &key, const QString &path, const QString &text,
                              const QString &toolTip = QString());

    // ---------- 「启动诊断」那一页（2026-10-18，§9.5「综合启动诊断报告」）----------
    //
    // 那一页要的是把实例本身 / Java / 内存 / 模组 / 最近一次启动日志 / 最近一份崩溃报告
    // 放在一起看。**算的活全在 `src/diagnose/StartupDiagnosis`**（读盘、认日志那几行都
    // 是业务层的事），这一层只留一个只读的文本框，进页时喊一声 `diagnosisRequested()`。
    static QString diagnosisReportName() { return QStringLiteral("instanceDiagnosisReport"); }
    static QString diagnosisRefreshButtonName() { return QStringLiteral("instanceDiagnosisRefresh"); }
    /// 还没算出来时摆的那一句（验证工程也拿它当"接没接上"的判据）。
    static QString diagnosisPendingText() { return QStringLiteral("正在检查…"); }
    void setDiagnosisText(const QString &text);

    // ---------- 「迁移」与「升级」那两页（2026-10-18，§9.9 第 15 项）----------
    //
    // 两页共用一套控件：目标下拉框 + 计划正文 + 可勾选的范围表 + 动手那一颗。**算的活
    // 全在 `src/operations/TransferPlanner`**（扫盘、兼容结论、默认勾选都在那儿），
    // 写盘在 `TransferApply`；这一层只摆字、记勾选、发两发信号。
    //
    // NOTE: 目标那一列是**这个游戏文件夹里已经装好的其他版本**（migration 是把内容搬进
    //       其中一份，upgrade 是拿其中一份当底造升级副本）。列表由 controller 灌进来 ——
    //       ui/ 不认识 VersionManager，也不知道"哪一份是当前这一份"。
    static QString transferTargetComboName(const QString &key) {
        return QStringLiteral("instanceTransferTarget_%1").arg(key);
    }
    static QString transferPlanTextName(const QString &key) {
        return QStringLiteral("instanceTransferPlan_%1").arg(key);
    }
    static QString transferItemsListName(const QString &key) {
        return QStringLiteral("instanceTransferItems_%1").arg(key);
    }
    static QString transferApplyButtonName(const QString &key) {
        return QStringLiteral("instanceTransferApply_%1").arg(key);
    }
    /// 灌目标列表（(显示名, id) 对）。**不发信号** —— 开页时由 controller 紧接着算第一份
    /// 计划；用户在下拉框里换目标才发 `transferTargetChosen()`。
    void setTransferTargets(const QString &key, const QList<QPair<QString, QString>> &targets);
    /// 摆计划正文（`TransferPlanner::Plan::detailText()` 那一份）。
    void setTransferPlan(const QString &key, const QString &text);
    /// 摆可勾选的范围表（(显示名, 键) 对 + 默认勾选的键）。
    void setTransferItems(const QString &key, const QList<QPair<QString, QString>> &items,
                          const QStringList &defaultKeys);
    /// 现在选中的目标 id（没有目标 = 空串）。
    QString transferTarget(const QString &key) const;
    /// 现在勾着的那几个键（**顺序与范围表一致**）。
    QStringList transferSelectedKeys(const QString &key) const;

    // ---------- 「配置文件」那一页的第二个格子（2026-09-27）----------
    //
    // 【为什么是"格子里再嵌一格"，而不是再开一整页覆盖层】
    //
    // 因为**开不出来**：`OverlayHost::openPage()` 在已经有人占着那一层时直接返回 nullptr
    // （一层只放一个页面，见 OverlayHost.hpp 那段），而用户点这一行的那一刻，「实例设置」
    // 自己正占着那一层。所以「配置文件」这一页是个**两格子的堆叠**：
    //
    //   ① 文件列表（+「上一级」+底下那行小字）  ② 编辑器
    //
    // 点一个文件 = 切到 ②，点「返回」（或按 Esc）= 切回 ①。切的事由这一层办（它俩都是
    // 它的孩子），而**盘上的事不办** —— 读、写、开目录、弹话都还是 MainWindow 的
    // （这一层没有 OverlayHost）。所以这一层只提供"把编辑器切出来 / 切回去"两个动作，
    // 外加一个把它**露出来**给 MainWindow 接线的口子。

    /// 那一格编辑器（**一次建好、一直留着**）。
    ///
    /// NOTE: 给的是裸指针是为了让 MainWindow **接一次线就完事**：它在摆好这一页的时候
    ///       连上 `saveRequested` / `backRequested` 两发，之后每次开文件只是往这一格里
    ///       灌内容（`openConfigEditor()`），不再重新连。所以**接回来的时候不要把它当
    ///       一次性的**（那会连第二遍，于是保存一次写两个盘）。
    ///
    /// NOTE: 读它之前先确认这一页开着没有 —— 没开的话它一样在（构造时就建了），只是
    ///       没露脸。这和"资源详情页那一个对象建一次留着"是同一条规矩。
    ConfigFileEditorPage *configFilePage() const { return m_configEditor; }

    /// 把编辑器切出来，并摆上这一份文件。
    /// @param path   绝对路径。
    /// @param loaded 那一份字节解出来的东西（见 ConfigFileText::decode）。**由 MainWindow
    ///        读盘 + 解码之后递进来** —— 这一层不碰盘。
    /// @param stamp  读之前盘上那一份的样子（见 ConfigFileText::Stamp）。**只有读盘的人量得
    ///        出来**，所以也是递进来的；保存前拿它比对（"这中间别人动过没有"）。
    void openConfigEditor(const QString &path, const ConfigFileText::Loaded &loaded,
                          const ConfigFileText::Stamp &stamp = ConfigFileText::Stamp());

    /// 这个文件**留过备份没有**（有 ⇒ 那一格画出「还原上一次」）。和 stamp 一样是"读盘的人
    /// 才知道"的事（那儿才知道备份根底下有没有它），所以由 MainWindow 递进来。
    void setConfigBackupAvailable(bool available);

    /// 切回文件列表那一格（并让那一页重新列一遍目录）。
    ///
    /// NOTE: **调用它之前先看 `configFilePage()->isDirty()`**：改过的东西不许悄悄没掉，
    ///       那一问（"改了没保存，还返回吗"）得由 MainWindow 弹（这一层没有覆盖层），
    ///       用户答"不保存"之后要先 `discardChanges()` 再走这儿。这一层不自己问 ——
    ///       它连"现在是脏的"这件事都不看，谁调谁负责。
    void closeConfigEditor();

    // ---------- 「存档」那一页的第二个格子（2026-09-28）----------
    //
    // 和上面「配置文件」那一段**同一个做法、同一个理由**（开不出第二个整页覆盖层）：
    //  ① 世界列表（+「导入世界」「批量编辑」+底下那行小字）  ② 世界设置（那支笔点开的）
    //
    // NOTE: 这是**第二份**"两格子"，不是把上面那一份抽成一个通用的容器。抽的话要动的
    //       是 `m_configStack` / `m_configEditor` / `m_configUp` / `m_configRoot` 这一整套
    //       （全是单份成员，且 `openConfigEditor` / `closeConfigEditor` 是公开 API，
    //       MainWindow 和两个验证工程都接着它们），而两边**要摆的东西完全不一样**
    //       （一个文件列表 + 上级目录，一个世界表单）—— 抽出来的那个"通用子页"只共用
    //       "一个 QStackedWidget 两个格子"这一句，剩下的全是各自的。重复的那二十来行
    //       比一个漏参数的抽象便宜。

    /// 那一格世界设置（**一次建好、一直留着**，理由同 `configFilePage()`）。
    WorldSettingsPage *worldSettingsPage() const { return m_worldSettings; }

    /// 把世界设置切出来，并摆上这一个世界。
    ///
    /// @param view 摆上去的一切。**由 MainWindow 从盘上读出来、翻译过来**（这一层不碰盘，
    ///        也够不着 `WorldFiles::WorldInfo` —— `ui/` 不许 include `src/`，见
    ///        WorldSettingsPage 文件头）。
    void openWorldSettings(const WorldSettingsPage::WorldView &view);

    /// 把世界设置摆成"读不出来"那一档（坏档警告之后落下来的地方）。
    void openWorldSettingsUnreadable(const QString &dirName, const QString &reason);

    /// 切回世界列表那一格（并让那一页重新列一遍目录）。
    ///
    /// NOTE: 这一条**没有**"改了没保存"要问的那种事：世界设置那一格上改的东西全在控件里，
    ///       关掉这一格就是把它们丢了 —— 所以"改过没有"得由 MainWindow 自己记（它手上
    ///       有 `editedView()`，和打开时那一份比一下就知道了）。
    void closeWorldSettings();

    /// 这个世界的存档现在**读不出来的话**，那一页上那颗「保存」本来就不该露 —— 给
    /// MainWindow 一个问的地方（它不用自己去翻那一格的状态）。
    bool worldSettingsUnreadable() const;

    /// 图标预览那一格的边长（像素）。**外面要按这个边长把图解好递进来** —— 递一张 16×16 的
    /// 进来会被放大成一团糊（那几张方块是像素画，而用户挑的照片本来就有的是像素）。
    static int iconPreviewSize();

    /// 图标那一行的初值。
    /// @param icon wtfml.meta 里那个 icon 键的原文（空 = 默认）。
    /// @param defaultMark 「默认」那一档画什么。**由调用方算好递进来**：它取决于这个实例
    ///        的版本分类 / 加载器（见 MainWindow::instanceRowMark()），而 ui/ 够不着那个
    ///        判断（本文件不 include 任何 src/ 下的头）。
    /// @param localMark 「本地图片」那一档现在长什么样（调用方把实例目录下那份 icon.png
    ///        读出来递进来；没有那份文件、或者它读不动，给一张空图 —— 那一档会自动退回
    ///        defaultMark，预览是给人看的，空着一块方格比"显示上一张"更让人以为选上了）。
    ///
    /// NOTE: 三张图都由调用方解好，这一层不碰盘 —— 它只在用户**刚挑了一张新图**的时候
    ///       读那个路径（那一下它手上只有一条路径，见 pickedIconPath()）。
    void setIcon(const QString &icon, const QPixmap &defaultMark,
                 const QPixmap &localMark = QPixmap());

    /// 「分组…」那一格可选的那几组（**点了按钮才看得见**，见 .cpp 里那两个浮层）。
    /// @param groups **组名 → 组 id**，按用户在「分组设置」里排的顺序。
    ///        第一项「未分组」由这一层自己加在最前面（它的 id 是空串），调用方不用管。
    /// @param currentGroupId 它**老家**在哪一组（空 = 未分组）。
    ///
    /// NOTE: 传 (名字, id) 成对而不是只传一串 id：组名用户可以改、id 才是身份，而这一页
    ///       只负责**显示** —— 名字从哪来（读盘？显示格式？）是调用方的事。
    /// NOTE: 认不出来的 currentGroupId（那一组刚被删掉）落在「未分组」上 —— 和实例树那边
    ///       「组被删之后它自然落到未分组」是同一条规矩，不为它写清理代码。
    /// NOTE: **传的是「老家」不是「存的是哪一组」**。这两个对"正在置顶"的实例不是一个答案
    ///       （存的那一格指着置顶组），而用户在这一格说的永远是"它本来属于哪儿"—— 两边都
    ///       显示之后，勾上置顶不再挪这一格（见 setPinned()）。
    void setGroupChoices(const QList<QPair<QString, QString>> &groups, const QString &currentGroupId);

    /// 置顶那一行的初值。
    /// @param pinned 这个实例现在在不在置顶组里（「显示在置顶组中」勾没勾）。
    /// @param pinnedGroupId 置顶组是哪一组，只用来写进那一句解释里（"会在「置顶」里也摆
    ///        一条"）。**它不再影响任何控件** —— 早先勾上置顶会把「分组」那一格挪到置顶组
    ///        上并禁用，那是"置顶 = 搬进置顶组"那套旧语义留下的；现在两边都显示，分组那
    ///        一格是**老家**，和勾选框互不相干。空串 = 这一份 .minecraft 里没有置顶组
    ///        （用户把它删了）：勾选框照旧能勾，但勾上这一下真的什么都不会发生
    ///        （MainWindow::pinInstance() 直接返回）。
    ///
    /// NOTE: 和 setGroupChoices() **没有先后要求**了（旧版有：置顶要把下拉框挪过去）。
    void setPinned(bool pinned, const QString &pinnedGroupId);

    /// 图标那一行现在选的是什么：空串 = 默认、`builtin:<键>` = 内置候选、`local` = 本地那张。
    /// 形态与 VersionManager::iconField() 逐字一致（那也是写进 wtfml.meta 的东西）。
    QString iconChoice() const;

    /// 「本地图片」那一档在 wtfml.meta 里写成什么（`local`）。
    ///
    /// NOTE: 拼它的地方是这一页，认它的地方在 MainWindow::instanceRowMark() ——
    ///       这就是它有唯一出处的理由（和 VersionIcons::iconKeyFromValue() 同一个道理）。
    static QString localIconValue() { return QStringLiteral("local"); }

    /// 用户从盘上挑的那张图的路径（没挑过 = 空）。
    ///
    /// NOTE: 挑完**不在这里**拷走 —— 拷进实例目录是调用方的事
    ///       （MainWindow::applyInstanceSettings()：挑完当场拷一份，见那边 `pickedIcon`
    ///       那一段）。这一层碰不了盘（ui/ 够不着 AppPaths），也**不该**碰：它手上的是
    ///       "用户挑了这张"，"这张要不要真的占住那个文件名"是落盘那一侧的决定。
    QString pickedIconPath() const;

    /// 分组那一行选的是哪一组（空串 = 未分组）。判据是那一格的 **userData**、不是文字 ——
    /// 组名会因为改名 / 换语言而变。
    QString chosenGroupId() const;

    /// 置顶那一行勾没勾。
    bool pinned() const;

    // ---------- 控件的 objectName ----------
    // 验证工程靠这些名字认控件，别改回按文字去认（文字会随全局值变）。
    /// 某一项的「跟随全局 / 自定义」那个下拉框。@param field 见下面那五个常量。
    ///
    /// ★ 2026-09-28：**这一格从前是一对单选钮**（两个 objectName：`override_<field>_follow`
    ///   / `override_<field>_custom`），用户原话「所有单选框换成下拉的」之后成了**一个**
    ///   下拉框，那两个名字一起没了。验证工程改成按这一个名字找、按**下标**选
    ///   （`choiceFollowIndex()` / `choiceCustomIndex()`）。
    static QString choiceComboName(const QString &field) {
        return QStringLiteral("override_%1_choiceCombo").arg(field);
    }
    /// 档位那个下拉框里两项的下标。**档位的唯一出处就是这两个数** —— 读侧
    /// （`isCustomChoice()`）、写侧（`setChoiceCustom()`）和验证工程都走它们，
    /// 别在别处再写一遍 `0` / `1`。
    static int choiceFollowIndex() { return 0; }
    static int choiceCustomIndex() { return 1; }

    // ---------- 页面按钮那一套的规格（2026-09-28）----------
    //
    // 用户原话：「实例设置里面的所有按钮做成恒定宽度和高度的，每一行三个，间距适中且左右
    // 能充分利用」。排布在 .cpp 里那三个 `layout…` 函数里算，**量的这几个数就是它们**
    // —— 验证工程别抄第二份（抄一份的下场是改了这里、那边还在断言老数字）。
    //
    // NOTE: **宽度不在这儿** —— 它不是钉死的像素，是"三列各一份 + 铺满整幅"算出来的
    //       （见 .cpp 里 layoutActionCells() 那段）。这两个数只是"排几列"和"至少多宽"。
    static constexpr int actionButtonHeight() { return 32; }
    static constexpr int actionButtonsPerRow() { return 3; }
    static constexpr int actionButtonSpacing() { return 10; }
    static constexpr int actionButtonMinWidth() { return 110; }
    /// 某一行「跟随全局（当前 …）」那句话的 objectName。
    static QString followHintName(const QString &field) {
        return QStringLiteral("override_%1_hint").arg(field);
    }
    /// 某一行「档位那一行」的 objectName —— 里面是 [跟随全局][自定义] + 那一项的控件。
    /// 那一项的控件和它的档位是**同一个决定的两半**（选「自定义」+ 自定义成什么），
    /// 所以并排在同一行；这一整行下面才是那一句解释（followHintName）。
    static QString choiceRowName(const QString &field) {
        return QStringLiteral("override_%1_choice").arg(field);
    }
    static QString javaPathField() { return QStringLiteral("javaPath"); }
    static QString xmxField() { return QStringLiteral("xmx"); }
    static QString xmsField() { return QStringLiteral("xms"); }
    static QString extraJvmArgsField() { return QStringLiteral("extraJvmArgs"); }
    /// 「窗口模式」那两行（2026-09-28 加的）。
    ///
    /// NOTE: 这三个串**就是 wtfml.meta 里那三个键名**（`src/settings/GameWindowSettings.cpp`
    ///       里 gameWindowModeMetaField() 那一组，那边是读侧写侧的唯一出处）。这一份是
    ///       `ui/` 侧的那一份 —— ui/ 不许 include src/，所以两边各写一遍，两边各有断言
    ///       钉着（那条约定见 LauncherSettings::gameWindowMinWidth() 上面那段）。
    ///       对不上的症状是"在实例设置里改完窗口模式，启动时一点变化都没有"。
    static QString windowModeField() { return QStringLiteral("windowMode"); }
    static QString windowWidthField() { return QStringLiteral("windowWidth"); }
    static QString windowHeightField() { return QStringLiteral("windowHeight"); }

    static QString javaPathEditName() { return QStringLiteral("instanceJavaPathEdit"); }
    static QString browseJavaButtonName() {
        return QStringLiteral("instanceBrowseJavaButton");
    }
    static QString memoryMaxSpinName() { return QStringLiteral("instanceMemoryMaxSpin"); }
    /// 「最大内存」那一行左半边那根条（和设置页同一套，见 MemorySlider.hpp）。
    static QString memoryMaxSliderName() { return QStringLiteral("instanceMemoryMaxSlider"); }
    /// ★ 2026-09-26：对象还在、还在跟着盘上的值走，但**整行不显示了**（用户要求撤掉这一
    /// 项）。名字留着是因为验证工程还按它找这个控件 —— 找得到、但不该看得见（见 .cpp 里
    /// 「最小内存」那一行上面那段）。
    static QString memoryMinSpinName() { return QStringLiteral("instanceMemoryMinSpin"); }
    /// 「窗口模式」那个下拉框。**项由调用方喂**（见 WindowModeChoice），这里只搭框。
    static QString windowModeComboName() { return QStringLiteral("instanceWindowModeCombo"); }
    static QString windowWidthSpinName() { return QStringLiteral("instanceWindowWidthSpin"); }
    static QString windowHeightSpinName() { return QStringLiteral("instanceWindowHeightSpin"); }
    /// 「宽 × 高」中间那个乘号（和设置页那个同名不同物，各有各的父控件）。
    static QString windowSizeTimesName() { return QStringLiteral("instanceWindowSizeTimes"); }
    static QString extraJvmArgsEditName() {
        return QStringLiteral("instanceExtraJvmArgsEdit");
    }
    static QString appendJvmArgsEditName() {
        return QStringLiteral("instanceAppendJvmArgsEdit");
    }
    static QString warnJavaMismatchCheckName() {
        return QStringLiteral("instanceWarnJavaMismatchCheck");
    }
    /// 「实例名」那一格（改名）。**它在「概览」栏的第一行**（2026-09-26 分栏之前它在整页
    /// 最上面）：它改的是这个实例的身份，别的几项改的都是它的启动参数 —— 先看清改的是
    /// 哪一个，再谈别的。打开对话框默认停在「概览」，所以它还是第一眼看到的那一格。
    static QString instanceNameEditName() { return QStringLiteral("instanceNameEdit"); }
    static QString instanceNameText() { return QStringLiteral("实例名"); }

    // ---- 概览栏那四行（见上面 setIcon 那一段）----
    //
    // 排法（用户 2026-09-26 原话「第一行 图标 名称 / 第二行 更改图标 分组… / 第三行
    // 显示在置顶组中 / 第四行 导出」）：概览这一栏**不再有"标题在左、内容在右"的行名**，
    // 那三句话变成了第一行的控件自己（名称输入框）、第二行两颗按钮、第三行勾选框上的字。
    /// 第一行：图标预览（一张固定边长的图，当前选的是哪一张就画哪一张）+ 名称输入框
    /// （`instanceNameEditName()`，就是「实例名」那一格，2026-09-26 搬到它旁边）。
    static QString iconPreviewName() { return QStringLiteral("instanceIconPreview"); }
    /// 第二行左边那颗「更改图标」——点了在它底下开 @c iconMenuName() 那个浮层。
    static QString changeIconButtonName() { return QStringLiteral("instanceChangeIconButton"); }
    /// 那个浮层本身（**常驻**：构造函数里建好一次，以后每次点按钮只是再弹一遍 —— 见
    /// .cpp 里那段 NOTE。`isVisible()` 才表示"此刻开着没开着"）。
    static QString iconMenuName() { return QStringLiteral("instanceIconMenu"); }
    /// 浮层里第 @p index 张内置候选那颗按钮（下标见 VersionIcons::iconCandidateKeys()）。
    static QString iconCandidateButtonName(int index) {
        return QStringLiteral("instanceIconCandidate%1").arg(index);
    }
    /// 浮层里的「选一张图片…」（开的是 Windows 自己的文件选择框）。
    static QString iconPickButtonName() { return QStringLiteral("instanceIconPickButton"); }
    /// 浮层里的「恢复默认」。
    static QString iconResetButtonName() { return QStringLiteral("instanceIconResetButton"); }
    /// 第二行右边那颗「分组…」——点了在它底下开 @c groupMenuName() 那个浮层。
    static QString groupButtonName() { return QStringLiteral("instanceGroupButton"); }
    /// 「分组…」那个浮层本身（同样常驻）+ 里面第 @p index 行的 objectName。
    /// **下标 0 是「未分组」**，后面按 setGroupChoices() 递进来的顺序。
    static QString groupMenuName() { return QStringLiteral("instanceGroupMenu"); }
    static QString groupMenuRowName(int index) {
        return QStringLiteral("instanceGroupMenuRow%1").arg(index);
    }
    /// 第三行那个勾选框。
    static QString pinCheckName() { return QStringLiteral("instancePinCheck"); }
    /// 第四行那个「导出…」按钮（2026-09-26 从灰占位换成真的）。
    ///
    /// NOTE: 它**不叫** `exportScript*` 那一套 —— 「导出启动脚本」是同一个页面上另一件事
    ///       （导出的是 .bat，和整合包规范一点关系都没有）。两个名字长得像过一次，
    ///       断言认错过控件。
    static QString exportButtonName() { return QStringLiteral("instanceExportButton"); }
    /// 概览页那颗「检查更新」的 objectName（2026-09-28；验证工程拿它找那颗按钮）。
    static QString checkUpdatesButtonName() { return QStringLiteral("instanceCheckUpdatesButton"); }
    /// 那颗按钮上的字（**唯一出处**）。
    static QString checkUpdatesText() { return QStringLiteral("检查更新"); }
    /// 那一格下面那句解释。
    ///
    /// 用户 2026-09-28 要的是「更新检测」这四个字，而这句话得把**我们能说什么**讲清楚：
    /// 判据是"盘上那个文件的 sha1 等于这个项目最新那一版的哪个文件"（见
    /// `ModDependencies::judgeUpdate`），所以它认不出来的那些会如实说"说不清"——
    /// 那句话不是失败，是"这一条我没法判"。**别把这句话写成"已经全部检查过了"**：
    /// 用户会以为"说不清"的那几条也是"没问题"。
    static QString checkUpdatesHint() {
        return QStringLiteral("看看这个实例里的模组/资源包有没有新版本（要对一遍每个资源的"
                              "版本列表，要联网）。只报数，不动盘上的文件。");
    }
    /// 那颗按钮的 tooltip（**说全的那一份**）。
    static QString checkUpdatesButtonTooltip() {
        return QStringLiteral("逐个认一遍这个实例里的资源（模组 / 资源包 / 数据包 / 光影），"
                              "再按这个实例的游戏版本和加载器问一遍它们各自的版本列表，"
                              "比出哪些有新版本。\n\n"
                              "认不出来的（自己改过的、作者删了老版本的）会写「说不清」——"
                              "那不是「没有问题」，是这一条没法判。");
    }
    /// 概览页那颗「更新中心」的 objectName（2026-10-13，coding plan §7.1）。
    ///
    /// NOTE: 和「检查更新」是**两颗、不是一颗**：那一颗要联网重查一遍（几十个文件、好几发
    ///       请求），而这一颗只是把**上一次查出来的**那份结论摆开看（一个网络都不发）——
    ///       它也是「撤销上一次更新」唯一进得去的地方，所以那一颗按钮**不许**被"必须联网"
    ///       卡住（更新完了发现游戏起不来，正是要退回去的那一刻）。
    static QString updateCenterButtonName() { return QStringLiteral("instanceUpdateCenterButton"); }
    static QString updateCenterText() { return QStringLiteral("更新中心"); }
    /// 那一格下面那句解释。
    static QString updateCenterHint() {
        return QStringLiteral("看上一次检查更新的结果，逐条更新 / 忽略某一版 / 固定某一版，"
                              "也能把上一次更新退回去。不联网。");
    }
    /// 那颗按钮的 tooltip（**说全的那一份**）。
    static QString updateCenterButtonTooltip() {
        return QStringLiteral("把上一次「检查更新」查出来的那一份结论摊成能动手的一页：\n"
                              "· 勾几条（或只点一条）就把它们换成新版；\n"
                              "· 「忽略这一版」= 这一版别再提示我（作者再发新版本时它会回来）；\n"
                              "· 「固定这一版」= 我就停在这一版上；\n"
                              "· 「撤销上一次更新」= 把上一次换掉的文件退回原样。");
    }

    // ---- 「复制实例」（2026-09-28）----
    //
    // 用户原话：「实例设置的概览里面加一条复制实例，点击以后让用户选择复制到的文件夹，
    // 然后给定复制后的实例名称」。
    /// 概览页第五行那颗「复制实例」的 objectName。
    static QString copyButtonName() { return QStringLiteral("instanceCopyButton"); }
    /// 那颗按钮上的字（**唯一出处**）。
    static QString copyText() { return QStringLiteral("复制实例"); }

    /// 那一格下面那句解释。
    ///
    /// ★ 这句话要说清**落到哪儿**："复制到另一个游戏文件夹"不是随便一个目录 ——
    ///   拷贝的落点是 `<你挑的那个文件夹>/versions/<新名字>/`（见
    ///   VersionManager::copyInstance 那段）。不说清的话，用户会以为挑中的目录就是副本
    ///   本身的位置，然后在一个自己没料到的层级里找它。
    static QString copyHint() {
        return QStringLiteral("把这一份原样复制到另一个游戏文件夹里去（模组、存档、配置一起"
                              "带走），复制出来的那一份是独立的：改它不影响这一份。");
    }

    /// 那颗按钮的 tooltip（**说全的那一份**）。
    ///
    /// NOTE: 最后那句"先关掉游戏"是真的会拦（MainWindow::copyInstanceTo() 里第二道闸）。
    ///       v1 这段原来写的是"游戏正在跑也能复制，但拷出来是那一刻的样子" —— 那是照着
    ///       "没有闸"写的，两处一起改掉了：界面不该在明着教用户做一件会被拦住的事。
    static QString copyButtonTooltip() {
        return QStringLiteral("选一个游戏文件夹，再给副本起个名字，就会在"
                              "「<那个文件夹>\\versions\\<名字>」下多出一份。\n\n"
                              "源文件一个字节都不动；副本拿到一个全新的实例 id，所以它和"
                              "这一份在分组、图标这些地方互不影响。\n\n"
                              "游戏正在运行的时候复制不了（拷出来会是一份写到一半的存档），"
                              "先关掉游戏。");
    }

    // ---- 「删除实例」（2026-09-28）----
    //
    // 用户原话：「顺便把删除实例功能做了，按钮放在概览里」。**删除这件事其实早就有了**
    // （实例行悬停时行尾那颗垃圾桶），但从没在界面上"看得见"过 —— 它藏在一个要悬停才出现
    // 的格子里，而用户找它的时候找的是"这个实例的设置里面有没有"。所以这一颗不是新功能，
    // 是**同一个动作的第二个入口**：真去删的那条路一个字都没改
    // （`MainWindow::confirmDeleteInstance()`，两道闸 + 问一句都在那儿）。
    //
    // NOTE: 和「复制实例」同一个形状 —— 这一层只发一句信号。理由也一样：那两步（问一句
    //       "真的要删吗"、把整个目录删掉）都要覆盖层和 VersionManager，ui/ 两样都没有。
    /// 概览页那一颗「删除实例」的 objectName。
    static QString deleteButtonName() { return QStringLiteral("instanceDeleteButton"); }
    /// 那颗按钮上的字（**唯一出处**）。
    static QString deleteText() { return QStringLiteral("删除实例"); }
    /// 那一格下面那句解释。
    ///
    /// ★ 这句话里**必须带"删了找不回来"**。用户点名把这一颗归进"不可撤销"那一档（所以它是
    ///   红的）—— 颜色只是提醒，真正让人停下来的是字。上面那颗「复制实例」是"多出一份"，
    ///   这一颗是"少掉一份"，两颗摆在一起时这一句就是它们唯一的区别。
    static QString deleteHint() {
        return QStringLiteral("把这个实例整个删掉（jar、json、以及它自己的设置都在里面），"
                              "删掉之后找不回来。");
    }
    /// 那颗按钮的 tooltip（**说全的那一份**）。
    static QString deleteButtonTooltip() {
        return QStringLiteral("删掉这个实例的那整个目录。\n\n"
                              "游戏正在跑、或者这个实例正在装的时候删不了 —— "
                              "这两种情况会先告诉你，不会删到一半。\n\n"
                              "这一个是不可撤销的：删掉之后没有回收站，别的实例和游戏文件夹"
                              "都不受影响。");
    }

    // ---- 存档那一页顶上那两颗（2026-09-28）----
    //
    // 用户原话：「顶端加个导入世界按钮和批量编辑按钮（这个先做占位）」。
    // NOTE: 这两句和上面那些 `xxxText()` 一样是**唯一出处** —— 那一页照 setDownloadText()
    //       的老规矩按"文字空不空"决定露不露，所以字写在这儿、别的几页不接。
    /// 存档页顶上那颗「导入世界」。
    static QString importWorldText() { return QStringLiteral("导入世界"); }
    static QString importWorldTip() {
        return QStringLiteral("把一个已经在你盘上的世界文件夹拷进这个实例的 saves 里。");
    }
    /// 存档页顶上那颗「批量编辑」。**今天是个占位**（用户点头的）—— 点下去只会说一句
    /// 「还没做」。字和提示都照实写，不假装它能用。
    static QString batchEditWorldsText() { return QStringLiteral("批量编辑"); }
    static QString batchEditWorldsTip() {
        return QStringLiteral("一次改好几个世界（还没做，这个按钮先摆在这儿）。");
    }

    // ---- 启动栏那两颗「动作」按钮（导出启动脚本 / 补全版本文件，2026-09-26）----
    //
    // 它们是**动作**不是设置项（不改 wtfml.meta 里任何一格），所以没有「跟随全局 /
    // 自定义」那一对单选钮。用户 2026-09-26 追加的那句「按钮文本直接做成设置名称」说的就是
    // 它们：**不再有单独一行行名**，按钮上那几个字就是这一项的名字，两句话挪去 tooltip。
    /// 「导出启动脚本」（开的是 Windows 自己的保存框）。
    static QString exportScriptButtonName() { return QStringLiteral("instanceExportScriptButton"); }
    /// 「补全版本文件」（校验并补齐这个实例的文件）。
    static QString repairFilesButtonName() { return QStringLiteral("instanceRepairFilesButton"); }
    /// 两颗按钮上的字（= 它们的行名，也是这两件事的**唯一名字**）。
    static QString exportScriptText() { return QStringLiteral("导出启动脚本"); }
    static QString repairFilesText() { return QStringLiteral("补全版本文件"); }
    /// 两颗按钮的 tooltip，以及那一行下面那句合起来的说明。
    static QString exportScriptHint() {
        return QStringLiteral("导出一个 .bat：双击就按这套参数启动这个版本，不用启动器。"
                              "正版账号导出的那份里带着登录令牌，别发给别人。");
    }
    static QString repairFilesHint() {
        return QStringLiteral("校验这个版本的客户端 jar、库和资源，缺哪个补哪个；已经完好的"
                              "会跳过。要有下载或者安装任务在跑时用不了。");
    }
    /// 两颗按钮下面那一句。**两句都要留着**：上面那一颗带着"导出的脚本里有令牌"这句
    /// 安全警告，光放 tooltip 里会有人一辈子看不到。
    static QString actionsHint();

    // ---- 概览栏那颗「检查并修复」（coding plan §4.1，2026-10-12）----
    //
    // 和上面那颗「补全版本文件」是**同一件事的两半**：那一颗是"缺什么补什么"（直接开一批
    // 下载），这一颗先**体检**（只算不装）—— 扫完在内容区开一整页把结论摊给用户看，页上
    // 那颗「补齐缺失或损坏文件」再回到同一条补全路径。所以这一颗**只发信号**：清单、后台
    // 线程、任务账本、报告页全在 MainWindow 那边，这一层连读盘都没有。
    static QString integrityCheckButtonName() { return QStringLiteral("instanceIntegrityCheckButton"); }
    static QString integrityCheckText() { return QStringLiteral("检查并修复"); }
    static QString integrityCheckHint() {
        return QStringLiteral("扫一遍这个实例的客户端、库和资源，把缺的、坏的分组列出来给你看，"
                              "看完了再决定补不补。扫描只读盘，不动你的文件。");
    }
    static QString integrityCheckTooltip() {
        return QStringLiteral("扫描在后台跑，期间可以接着用启动器。扫完会开一页报告。");
    }

    // ---- 概览栏那两颗「快照」（coding plan §4.4，2026-10-13）----
    //
    // 和「检查并修复」一样**只发信号**：清单、后台线程、任务账本、挑哪一份、"还原之前先给
    // 现在这一份做安全快照"全在 MainWindow 那边（这一层连读盘都没有）。
    //
    // ★ 为什么是"快照"而不是"备份"：概览这一栏已经有一颗「导出…」（出的是**给别人用的**
    //   整合包）。同一个实例上再来一颗叫「备份」的，用户分不清哪一颗是"我自己要回去用的"。
    //   快照 = 给我自己还原用的那一份（默认排除能重新下回来的 assets / libraries）。
    static QString createSnapshotButtonName() { return QStringLiteral("instanceSnapshotButton"); }
    static QString createSnapshotText() { return QStringLiteral("创建快照…"); }
    /// 那一格下面那句解释。**要点名默认排除的是什么** —— 那是用户唯一需要知道的分岔。
    static QString createSnapshotHint() {
        return QStringLiteral("把这个实例（模组、配置、存档…）打成一份可以整体还原的快照。"
                              "默认不打包能重新下回来的资源文件和游戏库，要连着它们一起"
                              "打包的话按下去之后可以选。");
    }
    static QString createSnapshotTooltip() {
        return QStringLiteral("打在后台跑，期间可以接着用启动器。"
                              "快照放在启动器数据目录的 cache/backups/instances 底下。");
    }
    static QString restoreSnapshotButtonName() { return QStringLiteral("instanceRestoreSnapshotButton"); }
    static QString restoreSnapshotText() { return QStringLiteral("从快照恢复…"); }
    static QString restoreSnapshotHint() {
        return QStringLiteral("从以前留的快照里挑一份，把这个实例还原回那时候的样子。"
                              "还原之前会先自动给现在这一份留一份，还原之后逐文件核对。");
    }
    static QString restoreSnapshotTooltip() {
        return QStringLiteral("会先问一句，然后收掉这一页再干活（会覆盖这个实例现在的文件）。");
    }

    // ---- 文件管理那八页的文案与 objectName（2026-09-26）----

    /// 常用目录那一排按钮：**两张按下标对齐的表**（按钮上的字 / 路径键）。
    /// 路径键见 setFileDirs() 与 setInstanceDir()。
    ///
    /// NOTE: 2026-09-27 起它们长在**概览页**上（用户原话：「"快捷方式"放到概览的下方」），
    ///       「快捷方式」不再是一页 —— 键和 objectName 一个字没改，只是换了个父。
    ///
    /// NOTE: 顺序是排过的（用户原话「前面这些按照合理顺序排，我这个不合理」）：先是**这个
    ///       实例自己**（版本目录），再是**出事时去翻**的日志，然后是这一套东西所在的
    ///       **游戏目录**与崩溃报告，最后是配置。
    ///
    /// ★ 2026-10-13（coding plan §5.3「实例工具入口补齐」）：表从九条长到**十三条**，后
    ///   四条（游戏目录 / 录像 / 崩溃报告 / 配置）接在后面 —— **一个已有的下标都没挪**，
    ///   于是"第几颗是哪一路目录"这件事在验证工程里照旧。
    ///   同时这一排分成两段：**现在真有东西的摆在外面**、其余收在「更多目录…」里，见
    ///   setFolderPresentKeys() 那段。
    ///
    /// ★★ 2026-10-18：表缩到**五条**（版本目录 / 日志 / 游戏目录 / 崩溃报告 / 配置）。
    ///   用户原话：「实例设置 概览里面 快捷入口重复了」—— 被拿掉的八条（存档 / 截图 /
    ///   模组 / 资源包 / 数据包 / 光影包 / 结构/原理图 / 录像）在左栏那一列里都有同名的一页，
    ///   其中六条在紧挨着这排的「显示哪些页」勾选表里还又列了一遍。**留下来的这五条是
    ///   "左栏里没有入口"的那几路**（「日志」「崩溃报告」两页在堆叠页里、但不在左栏树上；
    ///   「配置」那页本来就没有入口）。加新的一条之前先问这一句。
    static QStringList shortcutTexts();
    static QStringList shortcutPathKeys();
    /// 快捷方式一排摆几颗（三条正好一排）。
    static int shortcutPerRow() { return 3; }
    /// 第 @p index 颗快捷方式按钮。
    static QString shortcutButtonName(int index) {
        return QStringLiteral("instanceShortcutButton%1").arg(index);
    }

    /// 「更多目录…」那颗按钮（收起时写「更多目录…」、展开后写「收起更多目录」）。
    ///
    /// ★ §5.3 原话：「仅在目录存在或相关模组可识别时**优先展示**，其他项收在"更多目录"」
    ///   —— 它是那两段之间的**唯一开关**。一条"现在没有的"都没有时，**它自己也不摆**
    ///   （不摆一颗按下去什么都不变的按钮）。
    static QString shortcutMoreText() { return QStringLiteral("更多目录…"); }
    static QString shortcutMoreLessText() { return QStringLiteral("收起更多目录"); }
    static QString shortcutMoreButtonName() { return QStringLiteral("instanceShortcutMore"); }

    /// 这一排里**现在真有东西**的那几个键（`shortcutPathKeys()` 里的值）。
    ///
    /// 判据全在调用方（MainWindow）：目录在不在盘上它是唯一知道"这个实例的根在哪儿、
    /// 装没装 ReplayMod"的地方。**默认是"全都在"**（和那四个 `set*Usable()` 同一个口径）：
    /// 直接 new 出来的对话框这五条全摆在外面、也没有「更多目录」—— 忘了调它只是"多摆了几颗
    /// 按下去会先把目录建出来的按钮"，不是把谁藏起来。
    void setFolderPresentKeys(const QStringList &keys);

    /// 那两段当前的形状（验证工程读它）：五颗按表序；「更多目录」是真有一截收着才摆。
    QPushButton *shortcutMoreButton() const { return m_shortcutMoreButton; }
    bool shortcutMoreExpanded() const { return m_shortcutMoreExpanded; }
    /// 这一刻**收在「更多目录」里**的那几颗（展开之后它们就在外面了）。
    QList<QWidget *> shortcutHiddenButtons() const;

    /// 六页目录列表控件的 objectName。**面板本身仍叫 `panelName(key)`**（那是堆叠页里
    /// 那一格的名字），这个是挂在他里面的列表控件。
    static QString filesPageName(const QString &key) {
        return QStringLiteral("instanceFilesPage-") + key;
    }
    /// 那一页底下那行说明（"数据包要自己搬进存档"这类）。
    static QString filesHintName(const QString &key) {
        return QStringLiteral("instanceFilesHint-") + key;
    }
    /// 第 @p key 那一页底下那行说明的正文。
    static QString filesHint(const QString &key);
    /// 第 @p key 那一页空着时那句话。
    static QString filesEmptyText(const QString &key);

    /// 第 @p key 那一页顶上那颗「下载新 X」上写的字（**空串 = 这一页没有那颗按钮**）。
    ///
    /// 用户 2026-09-27 原话：「在实例设置里模组 资源包 数据包 光影包里添加下载新光影，按了
    /// 以后导向下载页对应类别」。所以有它的就是**下载页里真有对应一栏的那四页**。
    ///
    /// NOTE: 「光影包」那一页上的字是「下载新光影」（不是「下载新光影包」）：这四个字要和
    ///       **落地那一栏**说同一句话（下载页那一栏叫「光影」），而左边那一列里这一页仍旧
    ///       叫「光影包」。
    static QString filesDownloadText(const QString &key);

    /// 那颗按钮的悬停提示（说清"会离开这一页、落到哪儿"）。没有那颗按钮的页回空串。
    static QString filesDownloadTip(const QString &key);

    /// 按了第 @p key 那一页顶上那颗按钮之后，下载页该翻到**哪一栏**
    /// （`MainWindowUI::downloadCategoryKeys()` 里的一个键；没有那颗按钮的页回空串）。
    ///
    /// ★ 两套键**现在**正好是同几个字（这四页的页键就是下载页那四栏的键），可它们是两码事：
    ///   这一张说的是"实例设置里这些页叫什么"，那一张说的是"下载页左栏有哪几栏"。所以这里
    ///   **显式写一遍**，而不是把 key 直接递出去 —— 将来哪一边改了名（或者这边加了一页而
    ///   下载页里没有对应的一栏），这里是唯一的改写点，而不是一个会静默指错的地方。
    ///   ui_check 钉着"回来的每一个键都在 downloadCategoryKeys() 里"。
    static QString filesDownloadCategoryKey(const QString &key);

    /// 「点一下**名字**就进它自己的那一页」这一条在这几页上开不开（2026-09-27）。
    ///
    /// ★ 用户原话（2026-09-27 上半句）：「除了模组以外的几个都是点击模组名称自动进入下载
    ///   界面（**前提是匹配上了，模组的待会再说**）」⇒ 先接的是**资源包 / 数据包 / 光影包**
    ///   三页。**同一天下半句到了**：「对于模组，点进去和资源包差不多…」⇒ **模组也接上了**，
    ///   现在这一张表是**四页**。要接别的页时改的是这**一处**（`setFileResource()` 那个循环
    ///   和构造里那条 connect 都读它）。
    ///
    /// NOTE: 模组和另外三页**走的是同一条路**（同一个页面对象、同一个返回去处）—— 唯一的
    ///       分别落在那一页**右上角那一格**上：只有"从实例的文件管理页点一个模组进来"才摆
    ///       那颗「模组设置」（见 `ResourceDetailPage::setModSettingsSlot` 和
    ///       `MainWindow::DetailSource`）。**别在这儿再加一个"模组要特殊对待"的分支**。
    ///
    /// NOTE: 和 `filesDownloadText()` 那张表**没关系**（原理图那页也没有这一条），别顺手
    ///       把两张表合成一张：那个说的是"顶上有没有那颗按钮"，这个说的是"名字点不点得动"，
    ///       而它们各自会变。
    static bool filesNameOpensResource(const QString &key);

    /// 名字那一格（认出来了的那几条）的悬停提示：说清**点下去会去哪儿**
    /// —— 这一页会关掉，所以必须说（见 InstanceFilesPage::setResourceForPath()）。
    /// @param title 那个资源在接口上的名字。空串（= 没认出来）回空串。
    static QString filesResourceTip(const QString &title);

    // ★★ `filesDetailHint()` 2026-10-10 **已删**（原来是"四页底下那行说明里多出来的那一句"，
    //    说的是「行尾那颗 ⓘ 能进详情页，单击选中一条，按住 Ctrl 能多选，Ctrl+A 全选」）。
    //    删它的直接原因：**模组 / 资源包 / 数据包 / 光影包这四页底下那段叙述整个不挂了**
    //    （用户："把这个页面下方那段叙述删了来让框拉到底充分利用空间"，四处都删）⇒ 没有第二个
    //    调用者。★ 它删之前就已经是一句错话：多选 2026-10-10 当天换成了 `MultiSelection`
    //    （**点一下就切换，一个修饰键都不用**），"按住 Ctrl"那句照着按只会把这一条翻个面
    //    —— `wtfml-comment-vs-code` 那个老毛病。要再挂这类文字，**照着
    //    `InstanceFilesPage::setBatchMode()` 里那段注释写**，别照抄记忆里的版本。
    //
    // NOTE: 一句也没搬去别处 —— 这些"要动手才发现"的事现在由**操作条自己**说（选中才露），
    //       不需要一段常驻小字。

    /// 「配置文件」那一页上面那颗「上级」（2026-09-27）。**只有这一页有** —— 别的几页
    /// 看的是一个平铺的目录，而 `config/` 底下还有层级（`config/ftbquests/` 这种），
    /// 得能进去也得出得来。
    static QString configUpButtonName() {
        return QStringLiteral("instanceConfigUpButton");
    }
    static QString configUpText() { return QStringLiteral("上一级"); }

    static QString serverTreeName() { return QStringLiteral("instanceServerTree"); }
    static QString serverNoticeName() { return QStringLiteral("instanceServerNotice"); }
    static QString serverRefreshButtonName() {
        return QStringLiteral("instanceServerRefreshButton");
    }
    static QString serverEditButtonName() {
        return QStringLiteral("instanceServerEditButton");
    }
    static QString serverAddButtonName() {
        return QStringLiteral("instanceServerAddButton");
    }
    static QString serverDeleteButtonName() {
        return QStringLiteral("instanceServerDeleteButton");
    }
    static QString serverJoinButtonName() {
        return QStringLiteral("instanceServerJoinButton");
    }
    static QString serverRefreshText() { return QStringLiteral("刷新"); }
    /// 「编辑」：改**选中的那一台**的名字和地址（2026-10-10，用户原话「在服务器列表那里
    /// 做一个编辑地址和名称的功能」）。
    ///
    /// NOTE: 没选中一条时它是**灰的**（见 updateServerSelectionActions()）—— "编辑哪一台"
    ///       这个问题只有一个答案，而那个答案就是选中那一行。
    static QString serverEditText() { return QStringLiteral("编辑"); }
    static QString serverEditTip() {
        return QStringLiteral("改选中的那一台的名字和地址。**名字和地址分开问两句**（这一层"
                              "一次只能问一样），两个都能留空。改完直接写进这个实例的 "
                              "servers.dat。");
    }
    /// 「添加」（2026-10-10，用户原话「服务器那个页面里放一个添加服务器的按钮，允许添加」）。
    ///
    /// NOTE: 和「编辑」不同，它**不跟着选中走** —— 要加的那一台还没出现在表里，没有
    ///       "先选中"这一步，所以它**常亮**（连一条都没有的时候尤其要能按）。
    static QString serverAddText() { return QStringLiteral("添加"); }
    static QString serverAddTip() {
        return QStringLiteral("加一台新的。名字可以留空，地址得填。加完直接写进这个实例的 "
                              "servers.dat。");
    }
    /// 「删除」：把选中那一台从列表里拿掉（2026-10-10，用户原话「编辑要能改名字改地址以及
    /// 删除」）。
    ///
    /// NOTE: 它是这一页上唯一一颗**真会从盘上拿掉东西**的键，所以
    ///       ① 没选中一条时灰着（同「编辑」）；② 按下去还有一道问话（`InlineMessage::question`
    ///       的 destructive 那一档，在 MainWindow 里）—— 这一层只发一条信号。
    static QString serverDeleteText() { return QStringLiteral("删除"); }
    static QString serverDeleteTip() {
        return QStringLiteral("把选中的那一台从列表里删掉。会先问一句，删完直接写进这个实例的 "
                              "servers.dat。");
    }
    /// 「加入服务器」（**临时占位**，见 setServerJoinText()）。
    static QString serverJoinText() { return QStringLiteral("加入服务器"); }
    static QString serverJoinTip() {
        return QStringLiteral("用游戏自己的 --quickPlayMultiplayer 参数直接连进选中的那一台"
                              "（**临时占位**：先试试看这条参数发出去游戏认不认）。");
    }
    /// 「编辑」「删除」「加入」三颗在**没选中一条**时按不动 —— 悬停提示里说清为什么。
    /// （「添加」也在这一组旁边，但它不跟着选中走，见 `serverAddText()` 那条 NOTE。）
    static QString serverSelectFirstHint() {
        return QStringLiteral("先在下面选中一台服务器。");
    }
    /// 「刷新」正在重读 + 重测的时候那颗按钮上的字。
    ///
    /// ★ 2026-10-10：「测速」并进「刷新」之后这一句从「测速中…」改成「刷新中…」——
    ///   屏幕上看不见「测速」两个字了，再写着"测速中"就是对着一个不存在的键说话。
    static QString serverPingingText() { return QStringLiteral("刷新中…"); }
    /// 还没测过的那一格写什么（**不是空着**：空着和"测了没通"在屏幕上是同一个样子）。
    static QString serverUntestedText() { return QStringLiteral("还没测"); }
    /// 超时那一档在界面上**就写这两个字**（用户 2026-09-27：「超时的只写超时」）。
    ///
    /// NOTE: 和 `Status::timedOut` 那一位配着用 —— 那一位才是判据，这一句只是写出来的字。
    ///       那一格窄（`ResizeToContents`），而「它没答话」和「网络不通」这两个理由对这个
    ///       格子的读者（想点进去玩的人）是同一件事：现在连不上。
    static QString serverTimeoutText() { return QStringLiteral("超时"); }
    /// 地址那一格悬停提示里那句**告诉用户右键能复制**的话（用户 2026-09-27：「ip地址右键
    /// 可以复制（这一点在悬停文本上也告知）」）。
    ///
    /// NOTE: 唯一出处。那一格显示的是**遮过的**地址（`m-.h-.c-`），所以这句话还得说清
    ///       "复制到的是完整的那个" —— 不然用户看到 `m-.h-.c-`、复制出来却是一长串，
    ///       会以为我们复制错了东西。
    static QString serverAddressCopyHint() {
        return QStringLiteral("右键复制完整的地址");
    }
    // NOTE: 这儿原来还有一颗 `serverCachedPrefix()`（「上次：」）。2026-09-27 用户原话
    //       「‘上次’两个字也不要」，那一句过去时的前缀整个删了 —— 状态那一格现在**缓存
    //       里那一份和当场测出来那一份写的是同一句字**。别再加回来：要在界面上给这一格
    //       加前缀，等于重新发明那句被点名删掉的话。
    static QString serverNameColumnText() { return QStringLiteral("名称"); }
    static QString serverAddressColumnText() { return QStringLiteral("地址"); }
    static QString serverStatusColumnText() { return QStringLiteral("状态"); }

    /// 地址那一栏显示的**遮过的**写法。**唯一出处**（完整地址只在悬停提示里给）。
    ///
    /// 用户 2026-09-27 原话：「域名就显示首个字母+一条横杠（如 m-.h-.c- 来表示
    /// mc.hyperlands.cloud），悬停在上方时才显示完整的，ipv4 的显示为 123.45.*.*，
    /// ipv6 显示为 [123:123:*:*:*:*:*:*]」。
    ///
    /// 规矩：
    ///   - **域名** —— 每一个点分开的段取**首字母 + 一条横杠**（`mc.hyperlands.cloud`
    ///     ⇒ `m-.h-.c-`）。域名要遮的是**那个名字**，留个首字母已经足够认人；
    ///   - **数字地址**（四段全数字的 IPv4 / 带冒号的 IPv6）—— 留头两段、后面一律 `*`
    ///     （`123.45.67.89` ⇒ `123.45.*.*`）。数字地址要遮的是**后面那几段**：头两段留着
    ///     还看得出是哪个网段 / 哪个机房，认不出的部分才该打掉；
    ///   - `[...]` 那种方括号**留着**，`:` 后面那个端口**照抄** —— 它们不是身份，而且
    ///     端口不写的话有些服务器根本分不出来（同一个域名上开好几个）。
    ///
    /// NOTE: 认不出来（空串、光一个方括号）就**原样回**，不猜也不报错：这一格只是个显示，
    ///       真正的"这个地址能不能用"由 ServerPing::splitAddress 说了算。
    static QString maskedServerAddress(const QString &address);
    static QString serverEmptyText() { return QStringLiteral("这个实例还没有加过服务器。"); }
    /// 服务器名字那一格是空的的时候顶上什么字。servers.dat 里的 name 真能是空串。
    static QString unnamedMark() { return QStringLiteral("（无名）"); }

    /// 「光影包」那一页用不上时那句话（唯一出处，验证工程也按它核对）。
    static QString shaderUnavailableText();
    /// 「结构/原理图」那一页用不上时那句话。
    static QString schematicUnavailableText();
    /// 「模组」那一页用不上时那句话（这个实例没有模组加载器）。
    static QString modUnavailableText();
    /// 「数据包」那一页用不上时那句话（这个游戏版本还没有数据包）。
    static QString datapackUnavailableText();
    /// 第 @p key 那一页**为什么用不上**（用得上 = 空串）。**唯一分发处** —— 页里那句
    /// 「用不上」和勾选表的悬停提示都走它，别再各写一遍 if 链。
    static QString pageUnavailableText(const QString &key);

    // ---- 「显示哪些页」那张勾选表（2026-09-27，概览页最下面）----
    //
    // 用户原话：「概览里面调整是否显示」「是否显示的调整放到"快捷方式"的下方」「如果没有
    // 对应的加载器，保持对应选项不可勾选的状态，悬停文本解释」。
    //
    // 七条一条一个勾选框（顺序见 optionalPageKeys()），**不可用的那一条不勾、灰着、
    // tooltip 说为什么**。勾/不勾直接决定左栏那一列里有没有这一条。
    /// 第 @p key 条勾选框的 objectName。
    static QString pageCheckName(const QString &key) {
        return QStringLiteral("instanceSettingsShowPage-") + key;
    }
    /// 那一小块的字（**它的行名**）。
    static QString showPagesLabelText() { return QStringLiteral("显示哪些页"); }
    /// 可用那几条的 tooltip（"勾上左边那一列里就有这一页…"）。
    static QString pageShownHint();

    /// 第二行右边那颗按钮上的字（**唯一出处**：它是打开那个浮层的按钮，也是这一项的
    /// 名字 —— 概览这一栏不再有单独的行名）。
    static QString groupButtonText() { return QStringLiteral("分组…"); }
    /// 第三行那个勾选框上的字。**用户 2026-09-26 点名的这一句**（原来叫「置顶（固定在最
    /// 前面）」，那是"置顶 = 搬进置顶组"那套旧语义留下的说法）。
    static QString pinCheckText() { return QStringLiteral("加入收藏"); }
    /// 「更改图标」/「导出…」两颗按钮上的字。
    static QString changeIconText() { return QStringLiteral("更改图标"); }
    /// 实例操作 → 导出页的主动作。
    static QString exportText() { return QStringLiteral("导出整合包…"); }
    /// 浮层里那两个动作按钮上的字。
    static QString iconPickText() { return QStringLiteral("选一张图片…"); }
    static QString iconResetText() { return QStringLiteral("恢复默认"); }
    /// 八个内置候选在界面上叫什么（顺序与 VersionIcons::iconCandidateKeys() 一一对应；
    /// 后四个故意同一个字：加载器名在设置页 / 安装页里就叫这个）。
    static QStringList iconCandidateTexts();
    /// 「分组…」那个浮层里第一项那两个字。**唯一出处是 MainWindowUI::ungroupedGroupText()**，
    /// 这里只是把它转出来（本文件不 include MainWindowUI.hpp，所以实现写在 .cpp 里）。
    static QString ungroupedGroupText();
    /// 「更改图标」那个浮层下面那句话。
    static QString iconHint() {
        return QStringLiteral("挑一张图当这个实例的标志。它会出现在实例树上，以及底栏"
                              "「正在运行」那一行里。");
    }
    /// 勾选框下面那句话（**新语义**：两边各摆一条，说明白"原组那份不动"）。
    ///
    /// NOTE: 用户 2026-09-26 特意点名的就是这一句（原话：「下面给到注释提醒」）。它得说清
    ///       三件事：勾上之后**哪儿多出一条**、**原来那份不动**、取消勾选之后哪一条没了 ——
    ///       少说中间那一件，用户就会以为勾上等于"搬家"（那是这一版之前的语义）。
    static QString pinHint() {
        return QStringLiteral("在收藏组中保留快捷入口，不改变原分组。主页收藏区按显示数量和"
                              "最近启动时间展示；取消收藏不会删除实例。");
    }
    /// 第四行那颗按钮下面那句话（**看得见的那一行**，短）。
    ///
    /// NOTE: 一句两用的写法在这里**不成立**：那行字要短（它在这儿是**窄栏里的折行标签**，
    ///       长了会把整页的 sizeHint 顶起来 —— 页面上那条 E5 断言钉的就是它），而 tooltip
    ///       该说全（.mrpack / .zip 的区别、认不出来的东西会怎样）。所以拆成两个函数。
    static QString exportHint() {
        return QStringLiteral("导出为 Modrinth 或 CurseForge 整合包，包含可识别的资源清单和配置。"
                              "保留完整用户数据请使用快照。");
    }
    /// 那颗按钮的 tooltip（**说全的那一份**）。
    static QString exportButtonTooltip() {
        return QStringLiteral("按整合包规范打包：保存框里挑 .mrpack 走 Modrinth 规范、"
                              "挑 .zip 走 CurseForge 规范，两个都能被「导入整合包…」装回来。"
                              "能认出来源的模组写进清单（装的人那边直接下），认不出来的"
                              "原样打进包里，一个都不会丢。存档和截图不进包。");
    }
    /// 那一格下面那句解释（**唯一一处**说清"改这里 = 改磁盘上的目录名"）。
    static QString instanceNameHint() {
        return QStringLiteral("改这里就是给这个实例改名：它会成为 versions 下那个目录的名字，"
                              "里面的 jar 和 json 会一起改。");
    }
    static QString advancedToggleName() { return QStringLiteral("instanceAdvancedToggle"); }
    static QString advancedBoxName() { return QStringLiteral("instanceAdvancedBox"); }
    static QString titleLabelName() { return QStringLiteral("instanceSettingsTitle"); }
    // NOTE: 2026-09-26 起这一页**没有「确定」也没有「取消」**（用户原话：「实例设置里面的
    //       那个取消和确定删了，修改了的设置实时写入」）⇒ 那两个 objectName 一起删了，
    //       覆盖层页眉上那颗返回箭头就是这一页唯一的出口（Esc 同它）。

    /// 这一页叫什么（`「实例设置：<实例名>」`）。页面型覆盖层要把这一句写在自己那条
    /// 页眉上，所以它得能问出来 —— 不然两边就各写各的了。
    QString titleText() const;

    /// 把上面那句里的**实例名**换成新的（@param displayName 是**显示名**，不是目录名 ——
    /// 标题上写的一直是用户认得的那一串）。
    ///
    /// ★ 为什么必须有它：这一页开出来之后**实例自己会改名**（「实例名」那一格）。改名那
    ///   一刻 MainWindow 会把覆盖层页眉上那行字跟着换（`OverlayHost::setPageTitle()`），
    ///   而它问的就是 `titleText()` —— 那行字是构造时按**当时那个名字**拼好就再没人管过
    ///   的死字符串，不在这儿重拼一个的话，改完名之后页眉上写的还是老名字（用户看到的是
    ///   "最高处写着那个已经不在了的名字，底下那一格却是新的"）。
    ///
    /// NOTE: 和 `setInstanceName()` 是两件事、也没互相调用：那个换的是**控件上那一格**和
    ///       `m_instanceId`（拿的是目录名），这个换的是那行标题（拿的是显示名）——
    ///       带自定义名的实例这两个值本来就不是一回事。
    void setInstanceDisplayName(const QString &displayName);

    /// ★ 关页之前先看一眼「配置文件」那一格里有没有没保存的改动（2026-09-27）。
    ///
    /// 走这条路的**不是**编辑器自己那颗「返回」（那颗发 `backRequested()`，由 MainWindow
    /// 接到 `leaveConfigEditor()` 上），而是**覆盖层页眉那颗返回箭头** —— 它接的是
    /// `dialog->reject()`（见 `OverlayHost::execPage` 里那段），点一下关掉的是**整个
    /// 「实例设置」**。而那一格里可能正压着改了一半的东西。
    ///
    /// 这一层没有覆盖层、弹不出"要丢掉吗"那一句，所以它只能**先不关**、把这一发交出去：
    /// 接的人问完，用户答"丢掉"之后拿 `done(QDialog::Rejected)` 关页（**不是** `reject()`
    /// —— 那会又走回这儿）。
    ///
    /// NOTE: 编辑器那一格没开着、或者它是干净的，这一条一个字都不做（照旧正常关页）。
    void reject() override;

    /// 嵌进覆盖层那一页的时候把这行标题收起来。
    ///
    /// NOTE: 覆盖层的页眉**就是这个页面的标题栏**（那行字和窗口的标题栏是同一件事）。
    ///       不收的话屏幕上会紧接着出现两行一模一样的字。收不收由调用方说了算 ——
    ///       单独拿出来用（验证工程那种）时它还是要有标题的。
    void setTitleVisible(bool visible);

    /// 把「实例名」那一格改成 @p name（**控件上那一格**，不碰盘）。
    ///
    /// 两个调用点都在 MainWindow 那条改名路上：改成功了要让它显示新名字（那一格从此
    /// 就是新目录名）、改失败了要**把它退回原样**（否则用户眼前那一格写着"改好了"，
    /// 而磁盘上还是老名字 —— 下一次动任何一个开关都会拿这一个不存在的名字去写 meta）。
    /// 它顺手把导出脚本那个默认文件名用的 `m_instanceId` 一起换掉。
    void setInstanceName(const QString &name);

    /// 那一对单选钮的文字。**这两句是产品文案**，验证工程按它们核对而不是自己拼。
    static QString followText() { return QStringLiteral("跟随全局"); }
    static QString customText() { return QStringLiteral("自定义"); }
    /// 「跟随全局（当前 %1）」。@param current 全局那一项的值的可读写法。
    static QString followHint(const QString &current) {
        return QStringLiteral("跟随全局（当前 %1）").arg(current);
    }
    /// 自动档下两行内存数字框的提示（也是它们被禁用的原因）。
    static QString memoryAutoDisabledHint() {
        return QStringLiteral("「内存分配」是自动档：这两个数按系统内存现算，覆盖不生效。");
    }
    /// 高级选项那个折叠按钮上的字。默认折叠 —— 里面两项都是"改错了会让游戏起不来"
    /// 的东西，不该一打开对话框就摆在眼前。
    static QString advancedToggleText() { return QStringLiteral("高级选项"); }

    /// 追加参数那行提示的正文（也是"一条一行"这条约定的唯一出处）。
    ///
    /// NOTE: 「之后」那两个字**不许写成 `**之后**`** —— 这一页上没有任何一处设过
    ///       `setTextFormat(Qt::MarkdownText)`，星号会原样画在用户眼前（全项目同一条，
    ///       2026-09-26 顺手清掉了几处）。
    static QString appendJvmArgsHint() {
        return QStringLiteral("一条一行。排在全局额外参数之后（同一条以后写的为准），"
                              "不覆盖全局那一份。");
    }

protected:
    /// 只为一件事装在这儿：**左栏那一列不许因为"拿到焦点"就自己挑一条**（见 .cpp 里
    /// 实现上面那一整段 —— 那是 Qt `focusInEvent()` 的一个兜底行为，会让用户按一下
    /// Tab、或者窗口重新激活就凭空翻一页）。
    bool eventFilter(QObject *watched, QEvent *event) override;

    /**
     * @brief 有东西被拖到这一页上（2026-09-29，用户原话「打开某个实例设置的时候如果拖入
     *        .jar/.zip之类的文件，检测是整合包还是资源包等」）。
     *
     * ★ 这一页**只报到"落了哪些文件"**，一个字的判断都不做：认类型要读压缩包的条目表
     *   （`DroppedResource`）、装进去要知道实例目录在哪（`AppPaths`）—— 两件都在
     *   `src/` 那一层，`ui/` 够不着（这一条是全仓的分层约定，见 .cpp 顶上那段）。
     *   所以它发 `filesDropped()`，接的人是 MainWindow。
     *
     * NOTE: 为什么在这一层接：拖放事件从光标底下那个控件沿着父链往上冒，**第一个**
     *       `acceptDrops()` 为真的控件接住它（见 .cpp 里 `dropEvent` 那段）。这一页里
     *       绝大多数的子控件（标签、勾选框、下拉框）都不接拖放，所以拖到它们身上照样
     *       冒到这儿来。例外是那几个输入框（`QLineEdit` 自己接拖放、会把路径当文字插进去）
     *       —— 拖到输入框正中间那一小块上时走的是它自己那条路，这是 Qt 本来的行为。
     */
    void dragEnterEvent(QDragEnterEvent *event) override;

    /// @copydoc dragEnterEvent
    void dropEvent(QDropEvent *event) override;

signals:
    void resourcePackPageEntered();
    /// 左栏底部的固定入口；真正启动仍统一交给 MainWindow 的启动链。
    void launchInstanceRequested();

    /**
     * @brief 页面上的值**改完了**，该写盘了（2026-09-26 起这一页没有「确定」）。
     *
     * 用户原话：「实例设置里面的那个取消和确定删了，修改了的设置实时写入」。于是这一页
     * 从"改一堆、按确定一次性提交"变成了"改哪一格就写哪一格"：
     *
     *   - 这一发**只是"值变了"**，不含任何"该写成什么" —— 写成什么由 MainWindow 拿
     *     `override()` / `appendJvmArgs()` / `iconChoice()` 那几问现读（和从前按确定
     *     那一刻读的是同一批函数，所以"实时写"和"按确定写"落到盘上的字节是一样的）。
     *   - **值没变就不发**（见 .cpp 里 notifyEdited() 那段）：点一下已经选中的单选钮、
     *     把一个数改回原样，都不该让磁盘动一次。
     *   - 同一发可能由"用户操作完成"以外的原因触发（回车 / 离开那一格），这条线只认值，
     *     不认手势。
     *
     * NOTE: 改名**不走这一发**（见 renameRequested()）：那一格动的是目录名，不能和
     *       别的值挤在同一个"写一次设置"里 —— 顺序错了会在盘上留下一个空壳实例
     *       （理由写在 MainWindow::openInstanceSettings() 那段）。
     */
    void settingsEdited();

    /**
     * @brief 「实例名」那一格改完了（用户按下回车，或者离开那一格）。
     *
     * @param name 那一格里的字（**原样**，不 trim、不改大小写 —— 合不合法由
     *        `InstanceFiles::validateRenameTarget()` 判，界面不抄第二份规则）。
     *
     * 这一发是**改名那条路**的入口（从前是"点确定"那一半）：接了的人负责改目录名、
     * 失败时把这一格退回去（setInstanceName()）。
     *
     * NOTE: 每一格按键都发的话，用户打字打到一半目录就被改一次（"我"→"我的"→
     *       "我的整合包"），中间那几次全是白改，而且失败一次就会打断打字。所以这一发
     *       挂在 `QLineEdit::editingFinished` 上（回车 / 失焦），不是 `textChanged`。
     */
    void renameRequested(const QString &name);

    /**
     * @brief 用户点了「导出…」，并且已经在 Windows 自己的保存框里挑好了一个路径。
     *
     * 这一层只**问路径**（它本来就在用 QFileDialog 问 java.exe 和图标），真正的
     * "这一次启动到底执行什么"在 MainWindow 里 —— 那边才有 authManager / versionManager
     * / launcherManager。这样切还有一条实在的好处：保存框是**驱动不了**的（见 .cpp 里
     * browseJavaButtonName 那段 NOTE），把"问"留在这一层之后，MainWindow 那一半就能被
     * 验证工程拿一条路径直接驱动。
     *
     * @param path 用户挑的完整路径（带 .bat；用户没写扩展名时保存框自己会补）。
     */
    void exportLaunchScriptRequested(const QString &path);

    /**
     * @brief 用户点了概览那一栏的「导出…」，并且已经在 Windows 自己的保存框里挑好了路径。
     *
     * 和上面 `exportLaunchScriptRequested` **同一个形状、同一套理由**（这一层只问路径，
     * 干活在 MainWindow）；区别是这一件要慢得多 —— 枚举实例目录、算哈希、联网反查、
     * 打包，都在那边，进度写底栏那一格。
     *
     * NOTE: **格式是从扩展名读出来的**，不另设一档：保存框里那两个筛选项各自带着扩展名
     *       （`.mrpack` / `.zip`），用户挑了哪个、或者自己敲了哪个，选的就是哪套规范。
     *       这一层保证交出去的路径**一定**带这两个之一（见 .cpp），所以那边不用兜底。
     *
     * @param path 用户挑的完整路径（带 `.mrpack` 或 `.zip`）。
     */
    void exportModpackRequested(const QString &path);

    /**
     * @brief 用户点了概览那一栏的「检查更新」（2026-09-28）。
     *
     * 和上面两颗一样：这一层**只发一句话**，一个请求都不发、一个文件都不读 ——
     * 真正的活（枚举目录 → 后台线程反查 → 每个项目问一发版本列表 → 判）全在
     * `MainWindow::checkInstanceUpdates()` 里，那边才有 VersionManager / ResourceStore。
     * 这样切还有一条实在的好处：这一颗按钮能在 ui_check 里被单独点，断言"它发了这一发"。
     *
     * NOTE: **不传实例 id** —— 这一页就是某个实例的设置页（`setInstanceDir()` / 那一份
     *       wtfml.meta 都是它的），接的人从自己那边认（MainWindow 那次 openInstanceSettings
     *       记着开的是哪一个）。
     */
    void checkUpdatesRequested();

    /**
     * @brief 用户点了概览那一栏的「更新中心」（2026-10-13，coding plan §7.1）。
     *
     * 和上面那一颗同一档（一颗按钮 + 底下那句解释，点了之后**会出现一个结果**），但**不联网**：
     * 只是把上一次「检查更新」查出来的那份结论摆成能动手的一页（`MainWindow::openUpdateCenter`）。
     * 那一页也是「撤销上一次更新」唯一进得去的地方 —— 所以这一发必须在"没有网络"的时候
     * 照样能用（更新完发现游戏起不来，正是要退回去的那一刻）。
     *
     * NOTE: **不传实例 id**，理由和 checkUpdatesRequested() 那条一样（这一页就是某个实例的
     *       设置页）。
     */
    void updateCenterRequested();

    /**
     * @brief 用户点了概览那一栏的「复制实例」（2026-09-28）。
     *
     * 和上面那颗一模一样：这一层**只发一句话** —— 挑目标游戏文件夹（一个 QFileDialog）、
     * 问副本叫什么（一句 InlineMessage，要覆盖层）、真去拷那一棵树（要 AppPaths）三步
     * 全在 MainWindow。这一层连"往哪儿拷"都够不着。
     *
     * NOTE: **不传实例 id、也不传目标** —— 理由和 checkUpdatesRequested() 那条一样
     *       （这一页就是某个实例的设置页），而且目标和名字都要用户下一步才给。
     */
    void copyInstanceRequested();

    /**
     * @brief 用户点了概览那一栏的「删除实例」（2026-09-28）。
     *
     * 同一个动作的**第二个入口**（实例行行尾那颗垃圾桶是第一个）。这一层只发一句话：
     * 「真的要删吗」那一问、两道闸（正在跑 / 正在装）、以及真去删目录，全在
     * `MainWindow::confirmDeleteInstance()` —— 一行都没有在这儿重写一遍。
     *
     * NOTE: **这一发之后页面上那个实例就没了** ⇒ 接的人删完要**顺手把这一页收掉**
     *       （`OverlayHost::dismiss()`），否则用户会停在一页写着"某个已经不存在的实例"的
     *       设置上，随便动一格都会去写一个不存在的目录。
     */
    void deleteInstanceRequested();

    /**
     * @brief 用户点了「开始检查」。
     *
     * **能不能开始、要不要先问一句，全由 MainWindow 说了算** —— 只有它知道有没有下载在跑、
     * 这个实例这会儿装没装。这一层只报"用户想干这件事"。
     */
    void repairFilesRequested();

    /**
     * @brief 用户点了概览栏那颗「检查并修复」（coding plan §4.1）。
     *
     * **能不能开始、扫完怎么办，全由 MainWindow 说了算** —— 只有它知道有没有下载在跑、
     * 这个实例这会儿装没装、任务账本怎么记。这一层只报"用户想做一次体检"。
     */
    void integrityCheckRequested();

    /**
     * @brief 用户点了概览栏那颗「创建快照…」（coding plan §4.4）。
     *
     * 按下去之后 MainWindow 先问一句"要不要连着能重新下回来的那部分一起打包"，然后起一趟
     * 后台打包、记进任务账本。**这一层不碰盘**（见 .hpp 里 createSnapshotText 那段）。
     */
    void createSnapshotRequested();

    /**
     * @brief 用户点了概览栏那颗「从快照恢复…」（coding plan §4.4）。
     *
     * 挑哪一份、确认、以及"还原之前先给现在这一份做安全快照"全在 MainWindow 那边。
     */
    void restoreSnapshotRequested();

    /**
     * @brief 用户在一个文件夹上按了「打开」（快捷方式那九颗按钮，或者某一页的「打开文件夹」）。
     *
     * **这一层不自己开**：开目录是 QDesktopServices 的事，开不了要弹一句，而弹话的
     * InlineMessage 每一个入口都要 OverlayHost —— 那个在 MainWindow 手里。
     * @param path 要打开的目录（绝对路径）。
     */
    void openInstanceFolderRequested(const QString &path);
    void openInstanceFileRequested(const QString &path);
    void backupSelectedWorldRequested(const QString &path);
    void restoreSelectedWorldRequested(const QString &path);

    /// 「Replay」/「结构」那一页里**选中的那一条换了**（2026-10-18，§9.5）。
    ///
    /// 接的人负责把 `metaData.json` / 结构 NBT 读成一句话，再拿
    /// `setLifecycleInfoText(key, path, …)` 递回来。**这一层不读盘**（它连
    /// `AppPaths` 都够不着），所以选不中 / 空目录时它一个字都不发。
    void lifecycleFileInfoRequested(const QString &key, const QString &path);

    /// 用户进了「启动诊断」那一页 / 按了「重新检查」（2026-10-18，§9.5）。
    /// 接的人算好一份报告，拿 `setDiagnosisText()` 摆回来。
    void diagnosisRequested();

    /// 用户在「迁移」/「升级」那一页换了一个目标（2026-10-18，§9.9）。
    /// @param key `migration` / `upgrade`
    /// @param targetId 目标实例 / 目标版本的 id（下拉框的 data）。
    ///
    /// 接的人按"源实例 + 这个目标"重算一份计划，拿 `setTransferPlan()` /
    /// `setTransferItems()` 摆回来。**这一层不读盘**，换目标时也不会自己发第二发。
    void transferTargetChosen(const QString &key, const QString &targetId);

    /// 用户按了「搬过去…」/「创建升级副本…」。**只发意图，不写盘**：路径闸、运不运
    /// 行、要不要问名字都在 controller 那一层。
    /// @param key `migration` / `upgrade`
    /// @param targetId 下拉框里选中的那个（空串 = 还没选目标）
    /// @param selectedKeys 用户勾着的那些范围键
    void transferApplyRequested(const QString &key, const QString &targetId,
                                const QStringList &selectedKeys);

    /**
     * @brief 用户按了某一条行尾的「禁用」/「启用」。
     *
     * 加后缀还是去后缀**由接的人判**（看路径是不是以 `.disabled` 结尾就行，不用另一格数据）。
     * 这一层连读都没读，所以它不知道那个文件现在是什么状态 —— 只有"用户点了这一条"。
     * @param path 这一条**现在**的绝对路径。
     */
    void toggleInstanceFileRequested(const QString &path);

    /**
     * @brief 用户按了某一条行尾的「删除」。
     *
     * **"真的要删吗"那句由接的人问** —— 理由同上（这一层没有 OverlayHost）。
     */
    void deleteInstanceFileRequested(const QString &path);

    /**
     * @brief 用户在「配置文件」那一页**打开了一个文件**（双击，或者选中之后按回车）。
     *
     * 接的人负责**读那个文件**，然后拿它调 `openConfigEditor()` 把编辑器那一格切出来
     * —— 这一层手上没有 OverlayHost，也没有盘（读、解码、失败要弹的那一句，全是
     * MainWindow 的）。
     *
     * NOTE: **这一发不是"开一整页覆盖层"**。点这一行的那一刻，「实例设置」自己正占着
     *       覆盖层那唯一的一层，开不出第二个整页（见 configFilePage() 上面那一段）。
     *
     * NOTE: **只有文件走这一发**。点一个目录是"进去看"，那件事这一层自己就办完了
     *       （换一个目录再列一遍，纯显示层的事，不碰盘不联网），不会绕到这儿来。
     * @param path 这一条的**绝对路径**。
     */
    void openConfigFileRequested(const QString &path);

    /**
     * @brief 用户想关掉整个「实例设置」，而**「配置文件」那一格里还有没保存的改动**。
     *
     * 见 `reject()` 上面那一段：这一层已经把那一关**挡下来**了（没关），接的人问完那一句
     * 之后自己决定 —— 用户说"丢掉"就 `done(QDialog::Rejected)`，说"留在这儿"就什么都不做
     * （页继续开着，编辑器那一格原样）。
     */
    void closeBlockedByUnsavedConfigFile();

    /**
     * @brief 用户想关掉整个「实例设置」，而**「世界设置」那一页里还有没保存的改动**
     *        （2026-09-28）。
     *
     * 和 `closeBlockedByUnsavedConfigFile()` 是同一件事的第二个实例（那一条的说明逐字
     * 适用）：那颗返回箭头走的是 `dialog->reject()`，点一下关掉的是**整个**「实例设置」，
     * 而这一格里可能正压着改了一半的游戏规则。这一层弹不出"要丢掉吗"，所以只能先挡住、
     * 把这一发交出去。
     */
    void closeBlockedByUnsavedWorldSettings();

    /**
     * @brief 用户按了「文件管理」某几页顶上那颗「下载新 X」⇒ 请切到下载页对应的那一栏
     *        （2026-09-27）。
     *
     * 用户原话：「在实例设置里模组 资源包 数据包 光影包里添加下载新光影，按了以后导向下载页
     * 对应类别」。
     *
     * @param categoryKey `MainWindowUI::downloadCategoryKeys()` 里的一个键 —— 就是那一页
     *        对应的一栏（写法见 filesDownloadCategoryKey()）。**是键不是行号**：下载页左栏
     *        的行序由界面自己摆，这一层不该知道它。
     *
     * NOTE: 这一层只发信号，自己什么都不做（不关这一页、也不碰选项卡）—— 它正卡在
     *       MainWindow 那次 `execPage()` 的嵌套事件循环里，"收页之后再切到哪儿"的顺序只有
     *       那边知道（见 OverlayHost::dismiss() 那段 ★）。
     */
    void browseDownloadsRequested(const QString &categoryKey);

    /**
     * @brief 用户**点了某一条行尾那颗「详情」**，而这一条认得出是哪个资源 ⇒ 请打开它自己的
     *        那一页（2026-09-27；2026-10-09 起触发点是那颗按钮，不再是被点了一下名字）。
     *
     * 用户原话（说"点名字"那一版）：「除了模组以外的几个都是点击模组名称自动进入下载界面
     * （前提是匹配上了）」；2026-10-09 改成「把点击条目进入对应详情页的功能变成一个详情
     * 按钮，放到开关和删除中间」。
     *
     * @param pageKey 哪一页发出来的（`filePageKeys()` 里的一个键 —— 也就是"这是个资源包
     *        还是光影包"，见 filesNameOpensResource()）。**是键不是下标**。
     * @param path 那一条的**绝对路径**。注意给的是路径不是资源身份：身份是 MainWindow 那张
     *        反查表里的东西（`InstanceFilesPage` 和这一层都不知道一个文件对应哪个项目）。
     *
     * NOTE: 这一层只发信号，自己什么都不做（不关这一页）—— 和 browseDownloadsRequested()
     *       同一个理由：它正卡在 MainWindow 那次 `execPage()` 的嵌套事件循环里。
     */
    void fileResourceRequested(const QString &pageKey, const QString &path);

    /**
     * @brief 用户在某一页的**悬浮操作条**上按了「更新」：把选中的这些升到最新版（2026-10-09）。
     *
     * @param pageKey 哪一页发出来的（和 fileResourceRequested() 同一套键）。
     * @param paths   选中的那些条目的**绝对路径**（现在盘上那个名字）。
     *
     * NOTE: 这一层不知道"最新版是哪一版" —— 那是 MainWindow 手里那张表的事（见
     *       InstanceFilesPage::batchUpdateRequested 那段）。
     */
    void fileBatchUpdateRequested(const QString &pageKey, const QStringList &paths);

    /**
     * @brief 用户在悬浮操作条上按了「禁用」/「启用」：把选中的这些整体改成那个状态。
     *
     * @param pageKey 同上。
     * @param paths   选中的那些条目的绝对路径（禁用着的那些带 `.disabled`）。
     * @param disable true = 禁用（挂后缀），false = 启用（去后缀）。
     */
    void fileBatchToggleRequested(const QString &pageKey, const QStringList &paths, bool disable);

    /**
     * @brief 用户在悬浮操作条上按了「删除」。
     *
     * **"真的要删吗"那句由接的人问** —— 而且对**整批**只问一次（理由同
     * deleteInstanceFileRequested：这一层没有 OverlayHost）。
     */
    void fileBatchDeleteRequested(const QString &pageKey, const QStringList &paths);

    /**
     * @brief 用户把几个文件**拖到这一页上**了（2026-09-29）。
     * @param paths 落下来的本地文件绝对路径（原样；至少一个 —— 一个都没有时根本不发这一发）。
     *
     * 见 `dragEnterEvent()` 那段：这一层只把"落了什么"交出去，认类型和落盘都在 MainWindow。
     */
    void filesDropped(const QStringList &paths);

    /**
     * @brief 用户按了服务器列表那一页的「刷新」（2026-09-27）。
     *
     * 那一页的内容是**解析好的结果**递进来的（setServerList()），重读一遍要碰 src/ 里的
     * NBT 解析，所以只能由 MainWindow 接。
     *
     * ★★ 2026-10-10：这一条现在是**这一页上唯一一条"重新看一眼"的信号** —— 「测速」
     *   并进来了（用户原话「服务器那一栏测速和刷新作用重复了」）。接的人该做满两件事：
     *   **重读一遍 servers.dat** + **把每一台重测一遍**。「刷新」的意思是"把这一页刷成
     *   现在的样子"，而这一页从 2026-09-27 起不只是那份文件的镜像（图标、MOTD、延迟都是
     *   刚问来的）。
     *
     * NOTE: 进这一页（`serverPageEntered()`）走的是**只重测**那一半 —— 刚打开的一页没有
     *       "重读"的道理（表本来就是刚递进来的），但延迟该当场问一次。
     */
    void refreshServerListRequested();

    /**
     * @brief 用户在地址那一格**按了右键** ⇒ 请把那一串**完整的**地址放进剪贴板（2026-09-27）。
     *
     * 用户原话：「ip地址右键可以复制（这一点在悬停文本上也告知）」。
     *
     * 给出去的是**没遮过**的那一串（`host:port`，就是 servers.dat 里写的那个）—— 屏幕上
     * 那一格显示的是遮过的写法（`maskedServerAddress()`），复制一份"屏幕上那样的"给用户
     * 没有任何用（粘到游戏里连不上）。
     *
     * NOTE: 这一层**不碰剪贴板**（也不写日志）：和 `copyNameRequested` 那边同一条路数 ——
     *       复制出去的东西进哪儿由调用方说了算，而且这一层够不着 MainWindow 的日志。
     */
    void copyServerAddressRequested(const QString &address);

    /**
     * @brief 用户按了服务器列表那一页的「编辑」（2026-10-10）。
     * @param name   选中那一行在 `servers.dat` 里**真正**的名字（可以是空串）。
     * @param address 选中那一行**完整**的地址（没遮过的，和 copyServerAddressRequested 同）。
     *
     * 问那两句（名字 / 地址）、以及**写盘**都不在这一层：前者要覆盖层、后者要
     * `src/manage/ServerListFile`（那一层不 include src/）。这一层只把"要改的是哪一台"
     * 交出去 —— 而且交出去的是**改之前**的样子：落盘那边要靠这两串在 `servers.dat` 里
     * 认出那一条（**按值匹配，不按下标** —— 见 ServerListFile 那段）。
     *
     * NOTE: 名字给的是 `serverNameRole()` 里那一份，**不是**屏幕上显示的那一格 —— 空的
     *       名字屏幕上写的是「（无名）」，那是我们编的话，不能拿去当数据。
     */
    void editServerRequested(const QString &name, const QString &address);

    /**
     * @brief 用户按了服务器列表那一页的「添加」（2026-10-10）。
     *
     * 用户原话：「服务器那个页面里放一个添加服务器的按钮，允许添加」。
     *
     * 这一发**不带参数** —— 要加的那一台还没出现在表里，这一层没有它的名字和地址可给；
     * 问那两句（名字可空 / 地址必填）和写盘都在 MainWindow（前者要覆盖层，后者要
     * `src/manage/ServerListFile`，这一层一样都够不着）。
     */
    void addServerRequested();

    /**
     * @brief 用户按了服务器列表那一页的「删除」（2026-10-10）。
     * @param name    选中那一行在 `servers.dat` 里**真正**的名字（可以是空串）。
     * @param address 选中那一行**完整**的地址（没遮过的）。
     *
     * 用户原话：「编辑要能改名字改地址以及删除」。
     *
     * 参数和 `editServerRequested` **逐字同义**（都是"改之前的样子"，落盘那边按值在
     * `servers.dat` 里认出那一条 —— 见 ServerListFile 那段）；差别只在接的人要做的事：
     * 这一条要先问一句（不可撤销的定夺），再去删。
     *
     * NOTE: 这一层**不问话也不删** —— 覆盖层和 `src/` 它都够不着。
     */
    void deleteServerRequested(const QString &name, const QString &address);

    /**
     * @brief 用户按了服务器列表那一页的「加入服务器」（**临时占位**，2026-10-10）。
     * @param name    选中那一台的名字（同上，可以是空串；回话里用得着）。
     * @param address 选中那一台**完整**的地址 —— 这一串就是 `--quickPlayMultiplayer` 的
     *                值（`host[:port]`，游戏自己认得这个写法）。
     *
     * 发给谁、发什么参数、游戏认不认，全在调用方（见 MainWindow::startQuickPlay()）。
     * 这一层连"这个版本有没有那个参数"都不知道（要读版本 json）。
     */
    void joinServerRequested(const QString &name, const QString &address);

    /**
     * @brief 用户**切到了服务器列表那一页**（2026-09-27）。
     *
     * 用户原话：「服务器列表点进去要自动测速」。所以这一发是"这一页露出来了"这件事本身
     * —— 接到的人该去测一遍。它和上面那两条**不是一回事**：那两条说的是"用户按了某颗
     * 按钮"，这一条说的是"这一页被打开了"。
     *
     * NOTE: 每一页都会发一次（切回「概览」再切回来还会再发）—— 那正是"点进去就测"该有的
     *       行为。"正在测的时候又切进来一次"不会真的并发出两趟：接的人那边有一道
     *       "已经在跑就不重开"的闸（ServerPing::Pinger::start 自己那条）。
     */
    void serverPageEntered();

private:
    ResourcePackPage *m_resourcePackPage = nullptr;
    /// 「编辑」「删除」「加入服务器」三颗按钮的**可用性**照"现在选没选中一行"重算一次
    /// （2026-10-10）。
    ///
    /// ★ 没选中任何一行时三颗都是**灰的**：这三件事都要一个明确的对象（"改哪一台"、"删哪
    ///   一台"、"连哪一台"），而那个对象只有选中那一行能给。灰着比"按下去什么也不发生"
    ///   诚实。
    /// ★ 「添加」**不在这儿**：要加的那一台还没出现在表里，它没有"先选中"这一步，所以
    ///   常亮（连一条都没有的时候尤其要能按）。
    ///
    /// NOTE: 三个地方要喊它 —— 建完那棵树（初值）、`setServerList()`（整张表换掉 ⇒ 选中
    ///       没了）、以及 `itemSelectionChanged`（用户点了一行）。
    void updateServerSelectionActions();

    /// 服务器列表里**现在选中的那一行**（没选中 = nullptr）。
    ///
    /// NOTE: 这一层只认"选中了哪一个条目"，**不认"选中的那个有没有地址"** —— 地址为空的
    ///       条目根本进不来（`NbtUtil::readServerList` 跳过没有可用 `ip` 的），所以条目
    ///       选得中就一定改得动、也一定连得上。那两颗按钮的灰/亮再多判一层空地址只是
    ///       把同一条规矩写两遍。
    QTreeWidgetItem *selectedServerItem() const;

    /// 造一行「跟随全局 / 自定义 + 控件」。返回值是**整格**（档位行 + 提示），控件要加进
    /// 里面的「档位那一行」（`choiceRowOf()` 取），内存那两行整格禁用靠的就是这个返回值。
    QWidget *addOverrideRow(class QGridLayout *grid, int row, const QString &field,
                            const QString &title, const QString &hint);

    /// 取某一行装控件的那条横排（`choiceRowName()`）。
    ///
    /// NOTE: 控件加进这里而不是加进整格，是因为整格的布局是**竖排**（档位行 / 提示）。
    ///       写成 `qobject_cast<QHBoxLayout *>(cell->layout())` 会拿到 nullptr，
    ///       再往下就是空指针解引用 —— 这张表里每一行都写着那一句，所以收口成一个函数。
    QHBoxLayout *choiceRowOf(const QString &field) const;

    /// 某一项选的是「自定义」还是「跟随全局」。**档位的读侧唯一出处**。
    ///
    /// ★ 2026-09-28：这一问从前读的是 `m_<field>Custom` 那颗单选钮的 `isChecked()`，
    ///   用户原话「所有单选框换成下拉的」之后改读下拉框的 `currentIndex()`。
    ///   认不出来的 field（或那一格没建出来）**回 false = 跟随全局** ——
    ///   和从前"单选钮指针为空就崩"相比，这是"没建出来 = 没有覆盖"那个方向，
    ///   正是 override() 想要的（少写一份覆盖比多写一份安全）。
    bool isCustomChoice(const QString &field) const;

    /// 摆某一项的档位（`custom` = true 就是「自定义」）。**不发任何信号**。
    ///
    /// NOTE: 灌值时用（`setInstanceValues()` 那一串）—— 那一下说的是"打开这一页时盘上
    ///       是这么选的"，不是一次编辑，所以后面必须自己 `QSignalBlocker` 挡掉
    ///       `currentIndexChanged`（挡的是 refreshHints 那一条；写盘那一条在
    ///       `watchEditors()` 里）。
    void setChoiceCustom(const QString &field, bool custom);

    /// 往堆叠页里加一页（objectName = `panelName(key)`），返回那一页自己。
    /// **加进去的顺序必须就是 pageCount() 那个顺序**（有一条断言钉着）。
    QWidget *addPage(const QString &key);

    /// 在 @p page 里加一张「左名右控件」的两列表格（3:7，和设置页栏内那一层同一套），
    /// 返回那张表格。控件加进它自己的 `formName(key)` 子控件里。
    ///
    /// NOTE: 中间夹一层 form（而不是直接把表格挂在页上）是为了以后要往页里加表格之外的
    ///       东西（一句话说明、一颗按钮）时不用把表格拆开 —— 设置页那一层就是这个形状。
    QGridLayout *addForm(const QString &key, QWidget *page);

    /// 左栏那两条导航列的宽度：按最长的一条条目算（`sizeHintForColumn`），不写死像素
    /// —— 换字体 / 换样式就错。再加一点左右余量，条目的字离边太近看着像被切了。
    static int navListWidth(const QListWidget *list);

    /// 左栏选中第 @p row 条之后要做的事：把右栏换到那一条对应的页上。
    /// 越界 = 什么都不做（见 .cpp 里那段 NOTE）。
    void applyEntry(int row);

    /// 把右栏摆到 @p key 那一页上（不管它在不在左栏里）。applyEntry() 与 showPage() 都落到
    /// 这儿，换页之后那两件事（重列目录、发 serverPageEntered）只写一遍。
    void applyPageKey(const QString &key);

    /// 按 `shownEntryKeys()` **整份重灌左栏那一列**：重算宽高、刷新勾选框、把选中还原到
    /// 堆叠页当前那一页（那一页要是掉出去了就回概览，**但「配置文件」不算掉出去**）。
    ///
    /// 可用性一变、用户勾掉/勾上一条，都要喊它 —— 左栏那一列的内容是**算出来的**，
    /// 不是构造时摆好就完事的。
    void rebuildEntryList();

    /// 把勾选框按"可用不可用 + 用户勾没勾"重摆一遍（**压着信号**：这是画，不是用户改的）。
    void refreshPageChecks();

    /// 四个 `setXxxUsable()` 的公共落点：记进 `m_pageAvailable` + 重建左栏那一列。
    /// @param markPage 要不要把那一页**整页换成一句「用不上」**。
    ///
    /// NOTE: 「光影包 / 原理图」要（那两页里的东西在没加载器时真的一点用都没有），
    ///       「模组 / 数据包」不要 —— 老版本上的 `datapacks/`、原版实例上的 `mods/` 里
    ///       照样可能有东西，列出来是句真话（用户真想看，把这一条勾出来就看得见）。
    ///       它们"用不上"这件事由勾选框那一格的 tooltip 说（见 pageUnavailableText()）。
    void setPageUsable(const QString &key, bool usable, bool markPage = true);

    /// 把图标那一行按 m_iconChoice 重画一遍：预览换图、八颗浮层里那颗按钮的勾选状态跟着换。
    /// 唯一出处是 m_iconChoice —— 按钮和预览都是它的画（见上面那段"影子状态"）。
    /// NOTE: 浮层**没弹出来的时候也要调到它**（预览和按钮的勾选状态在关着的时候也要对，
    ///       否则弹出来那一瞬间是上一次的样子）。
    void refreshIconRow();

    /// 在 @p anchor 那颗按钮底下弹出 @p menu 那个浮层。两颗按钮（更改图标 / 分组…）共用。
    ///
    /// NOTE: 浮层是**常驻**的（构造函数里建好，这里只负责弹），所以这一个函数没有"建"
    ///       那一半。理由见 .cpp 里那段 NOTE。
    static void popUpMenuBelow(QWidget *menu, const QWidget *anchor);

    /// 「分组…」那个浮层里第 @p index 行被点了：记住选的是哪一组（下标 0 = 未分组）。
    /// 只改这一层的影子状态 `m_chosenGroupIndex` —— 落盘是 MainWindow 的事。
    void chooseGroup(int index);

    /// 把「分组…」那个浮层里每一行的可用性、以及那颗按钮上的 tooltip，按此刻的
    /// `m_chosenGroupIndex` 重摆一遍（当前那一行灰掉 + 一句「已经在这一组里了」）。
    ///
    /// NOTE: setGroupChoices() 末尾也要调它一次 —— 重建完那几行是**全亮**的，不刷的话
    ///       弹开来会看到"当前那一组也能点"（点下去什么都没发生）。和 refreshIconRow()
    ///       同一条路数：控件的样子只有一个出处，改完影子状态就调一次。
    void refreshGroupMenuState();

    // ---------- 「改了就写」那套（见 signals 里 settingsEdited() 那段）----------

    /// 把这一页上**所有能改值的控件**接到 notifyEdited() 上。构造函数最后调一次。
    ///
    /// 按控件的**类型**接，不按对象一个个接：往这一页加控件时只要它属于这几类就自动
    /// 跟上（漏接一个的表现是"这一项改了不写盘"，界面上看不出来 —— 那正是最该防的
    /// 一种坏）。四类各自的信号在函数里写着。
    ///
    /// NOTE: 「实例名」那一格**故意排除在外**（它走 renameRequested()，见 signals 那段）。
    void watchEditors();

    /// 值变了吗：变了就把这一份记下来、发一发 settingsEdited()。
    ///
    /// **判据是"和上一次发出去的那一份比"，不是"有没有人动过控件"**：
    ///   - 点一下已经选中的那颗单选钮、把数字改回原样、在一个框里打一个字又删掉 ——
    ///     值没变就不该让磁盘动一次；
    ///   - 反过来，`setChecked()` 这类程序化的改动也照样算数（这一页上那些只发生在
    ///     构造期，构造函数最后会把当时的签名记下来当起点，所以打开页面不会立刻写一次）。
    ///
    /// NOTE: 它**不发 settingsEdited() 的"哪一项变了"**：写盘那边读的是整份覆盖
    ///       （`override()` 那一套），不关心是哪一格动的 —— 那两个问题分开问，将来
    ///       加一项就不用同时改两处。
    void notifyEdited();

    /// 这一页此刻的**值签名**（把每个会写进 wtfml.meta 的值拼成一串）。
    ///
    /// 用它而不是给那几个值类型写 `operator==`：签名的唯一用处就是"和上一次比一比"，
    /// 而这个比法**不依赖**那几个结构体将来加不加字段（加了字段、拼签名那一行忘记改的话，
    /// 漏掉的那一项会变成"改了不写盘"—— 所以它和 `override()` 摆在一起，改一处看得见另一处）。
    QStringList valueSignature() const;

    /// 上一次**发出去**（= 写盘那边看见过的）那一份签名。见 notifyEdited()。
    QStringList m_lastSignature;

    /// 全局那一层的值，用来写「跟随全局（当前 …）」。构造之后不再变。
    GlobalValues m_global;

    /// 这一页自己那行标题。页面型覆盖层用的是**它的字**（页眉 = 这个页面的标题栏），
    /// 所以它得留着给人问（见 titleText / setTitleVisible）。
    QLabel *m_titleLabel = nullptr;

    // ---------- 左栏那一列 + 右栏堆叠页（见 pinnedPageKeys() 上面那一段）----------
    /// 左栏**唯一**那一列。当前行 = 界面上当前摆着的那一页。
    QListWidget *m_pageList = nullptr;
    QTreeWidget *m_navigationTree = nullptr;
    QHash<QString, QListWidget *> m_lifecycleLists;
    /// 「Replay」/「结构」选中一行之后底下那一格（键 → 标签），以及它现在讲的是哪一条。
    /// @see lifecycleFileInfoLabelName() / setLifecycleInfoText()
    QHash<QString, QLabel *> m_lifecycleInfo;
    QHash<QString, QString> m_lifecycleInfoPath;
    QHash<QString, QTimer *> m_lifecycleInfoTimers;
    /// 「启动诊断」那一页的只读报告框（见 diagnosisReportName()）。
    QPlainTextEdit *m_diagnosisReport = nullptr;
    /// 「迁移」/「升级」那两页（键 → 控件；键就是 `migration` / `upgrade`）。
    QHash<QString, QComboBox *> m_transferTargets;
    QHash<QString, QPlainTextEdit *> m_transferPlans;
    QHash<QString, QListWidget *> m_transferItems;
    void updateTransferApplyState(const QString &key);
    void setupLifecyclePages();
    void rebuildNavigationTree();
    void refreshLifecycleFiles(const QString &key);
    void filterLifecycleFiles(const QString &key);
    void updateLifecycleInfo(const QString &key);
    /// 右栏那份堆叠页，下标见 pageCount() / filePageFirstIndex()。
    QStackedWidget *m_stack = nullptr;

    /// 哪几页在这个实例上**用得上**（键 → 可用；表里没有 = 可用）。
    ///
    /// 判据全在 MainWindow（读加载器名 / 扫 mods/ / 看游戏版本），这一层只收结论：
    /// `setModUsable()` / `setShaderUsable()` / `setSchematicUsable()` / `setDatapackUsable()`
    /// 四个 setter 各自改表里一格，再喊一次 rebuildEntryList()。
    QHash<QString, bool> m_pageAvailable;
    /// 用户**勾掉的**那几页（键 → false，见 pageChoices()）。只装显式选择，不写"默认值"。
    QJsonObject m_pageOverrides;
    /// 「显示哪些页」那七条（键 → 勾选框，顺序同 optionalPageKeys()）。
    QHash<QString, QCheckBox *> m_pageChecks;

    /// 「实例名」那一格（见 instanceName()）。现在长在「概览」栏里。
    QLineEdit *m_instanceNameEdit = nullptr;

    /// 这个实例现在的目录名（构造时那个 instanceId）。留着是为了"导出启动脚本"那条路
    /// 的默认文件名 —— 它一定是合法文件名（validateDirName 那一关挡过），而显示名不一定。
    QString m_instanceId;

    // ---- 文件管理那八页（见上面 filePageKeys() / setFileDirs() 那两段）----
    /// 这个实例的目录（setInstanceDir()）：概览页上那排快捷按钮的「版本目录」开的就是它。
    QString m_instanceDir;
    /// 「文件管理」那几页各自的目录（见 setFileDirs()）：**页键 → 绝对路径**。
    QHash<QString, QString> m_fileDirs;
    /// 六页目录列表控件（键见 filePageKeys()）。refreshFilePages() 走它。
    QHash<QString, InstanceFilesPage *> m_filesPages;
    /// 概览页上那一排常用目录按钮（**按表序**，见 shortcutTexts()）。
    QList<QWidget *> m_shortcutButtons;
    /// 那一排所在的容器与网格（relayoutShortcuts() 要拿它们重摆）。
    QWidget *m_shortcutCell = nullptr;
    QGridLayout *m_shortcutGrid = nullptr;
    /// 「更多目录…」那一颗（**没有收着的东西时是 nullptr** —— 见 .hpp 里那条 NOTE）。
    QPushButton *m_shortcutMoreButton = nullptr;
    /// 用户手点开的那一截现在开没开。
    bool m_shortcutMoreExpanded = false;
    /// 这一刻**在盘上**的那几路目录（setFolderPresentKeys()）。
    ///
    /// ★ 默认 = "全都在"（和那四个 `set*Usable()` 同一个口径）：直接 new 出来的对话框
    ///   这五条全摆在外面、也没有「更多目录」。
    QStringList m_shortcutPresentKeys = shortcutPathKeys();

    // ---- 「配置文件」那一页独有的两样（2026-09-27）----
    /// `config/` 的**根**。进去、出来都拿它当墙 —— 不许走到 `config/` 上面去（再往上是
    /// 这个实例自己、然后是整个 `.minecraft`，走到那儿"这是配置文件"这件事就走丢了）。
    ///
    /// NOTE: **不能在构造函数里从 m_fileDirs 抓** —— 那会儿 setFileDirs() 还没被调过，
    ///       抓到的永远是空串，于是"上级"那道墙形同虚设。和快捷方式那几颗按钮踩的是
    ///       同一个坑（见那边注释里那句"捕获快照的话拿到的永远是空串"）。所以它在
    ///       setFileDirs() 里赋值。
    QString m_configRoot;
    /// 那一页上面那颗「上级」。**到了根就整颗不画**（不是画成灰的 —— 和"组里挪不动的
    /// 那一侧不画"同一条规矩）。
    QPushButton *m_configUp = nullptr;
    /// 那一页的两格子（0 = 列表 1 = 编辑器，见 configFilePage() 那段）。
    ///
    /// NOTE: 「上一级」和那张列表都在第 0 格里，所以 `m_configUp` 的可见性只管第 0 格 ——
    ///       切到编辑器那一格时它整格都不在画面上，不必额外藏一遍。
    QStackedWidget *m_configStack = nullptr;
    /// 第 1 格那个编辑器（**建一次留着**，见 configFilePage() 的 NOTE）。
    ConfigFileEditorPage *m_configEditor = nullptr;
    /// 「存档」那一页的两格子（2026-09-28，见 openWorldSettings() 上面那一段）。
    QStackedWidget *m_worldStack = nullptr;
    /// 第 1 格那个世界设置（**建一次留着**，同上）。
    WorldSettingsPage *m_worldSettings = nullptr;
    QTreeWidget *m_serverTree = nullptr;
    QLabel *m_serverNotice = nullptr;
    /// 「刷新」（见 refreshServerListRequested()）。**正在重读 + 重测时**文字换成
    /// 「刷新中…」并按下不动（见 setServerPinging()）。
    QPushButton *m_serverRefresh = nullptr;
    /// 「编辑」（见 editServerRequested()）。
    QPushButton *m_serverEdit = nullptr;
    /// 「添加」（见 addServerRequested()）。**唯一一颗不跟着选中走的**（见 updateServerSelectionActions()）。
    QPushButton *m_serverAdd = nullptr;
    /// 「删除」（见 deleteServerRequested()）。
    QPushButton *m_serverDelete = nullptr;
    /// 「加入服务器」（见 joinServerRequested()）。**默认收着**：没有那个参数的版本上它
    /// 不该出现（见 setServerJoinText()）。
    QPushButton *m_serverJoin = nullptr;
    /// 「加入服务器」的悬停提示（setServerJoinText() 递进来的那一句）。**存在这儿**是因为
    /// 那颗按钮选中/没选中时提示要换（见 updateServerSelectionActions()）—— 换了之后还得
    /// 换得回**原来那一句**，而那句话是调用方给的，这一层编不出来。
    QString m_serverJoinTip;
    /// 每台服务器现在什么样（下标同 setServerList() 那一份）。**状态在这儿存着**：
    /// 「刷新」重读一遍 servers.dat 时那些条目是重建的，而这一份是"上一次问到的结果"，
    /// 由 setServerStatuses() 单独灌。行数对不上时以少的为准（见 setServerStatuses()）。
    QList<ServerRowStatus> m_serverStatuses;

    // ---- 概览栏那三行（见上面 setIcon 那一段）----
    /// 图标那一行**唯一的出处**：空串 / `builtin:<键>` / `local`。
    ///
    /// NOTE: 这是全页唯一一处影子状态，「本地图片」那一档没有控件能表示它（理由见上面
    ///       setIcon() 上面那一段）。八颗按钮和那张预览都由 refreshIconRow()
    ///       照着它画。
    QString m_iconChoice;
    /// 用户刚挑的那张图在盘上的原处（没挑过 = 空）。拷进实例目录是调用方的事。
    QString m_pickedIconPath;
    /// 「默认」和「本地那张」两档画什么。setIcon() 灌进来，之后不变。
    QPixmap m_defaultIconMark;
    QPixmap m_localIconMark;
    /// 那一张预览。边长固定（见 .cpp 里 kIconPreviewSize）—— 换图时整行不该跟着跳。
    QLabel *m_iconPreview = nullptr;
    /// 八颗候选按钮，下标与 VersionIcons::iconCandidateKeys() 一一对应。
    QList<QToolButton *> m_iconButtons;
    /// 「更改图标」那颗按钮，以及它底下那个**常驻**浮层（见 .cpp 里那段 NOTE）。
    QPushButton *m_changeIconButton = nullptr;
    QWidget *m_iconMenu = nullptr;
    /// 「分组…」那颗按钮 + 它底下那个常驻浮层。
    QPushButton *m_groupButton = nullptr;
    QWidget *m_groupMenu = nullptr;
    /// 「分组…」浮层里那几行按钮（下标 0 = 未分组，其余按 setGroupChoices() 的顺序）。
    QList<QPushButton *> m_groupRows;
    /// **选的是第几行**（下标，不是组 id）。这一格和图标那一格同一类：浮层里那几行是
    /// 现场按调用方递进来的清单建的，行数会变（用户能在另一个框里加组/删组），所以值的
    /// 出处只能是这一格。`-1` = 还没灌过（没弹过任何一次）—— 那时 chosenGroupId() 回空串。
    ///
    /// NOTE: 存的是**下标**而不是 id：id 那一份在 setGroupChoices() 进的 `m_groupIds` 里，
    ///       两个成员一起改，中间不会出现"下标指着另一行"的窗口。
    int m_chosenGroupIndex = -1;
    /// 「分组…」浮层里每一行的组 id（下标 0 是空串 = 未分组）。和 m_groupRows 一一对应。
    QStringList m_groupIds;
    QCheckBox *m_pinCheck = nullptr;
    /// 置顶组是哪一组（setPinned() 灌进来）。空串 = 这一份 .minecraft 里没有置顶组。
    ///
    /// NOTE: 它**只管那一句解释怎么写**（"会在「置顶」里也摆一条"），不再影响任何控件 ——
    ///       见 .hpp 里 setPinned() 那段。
    QString m_pinnedGroupId;
    QLineEdit *m_javaPathEdit = nullptr;
    QSpinBox *m_memoryMaxSpin = nullptr;
    MemorySlider *m_memoryMaxSlider = nullptr;
    QSpinBox *m_memoryMinSpin = nullptr;
    QLineEdit *m_extraJvmArgsEdit = nullptr;
    /// 「窗口模式」那一行（2026-09-28）。**项是调用方灌的**（构造参数 windowModes），
    /// 这里只有一个框；档位那一列以 itemData 存**代号字符串**，override() 直接把它读出来。
    QComboBox *m_windowModeCombo = nullptr;
    QSpinBox *m_windowWidthSpin = nullptr;
    QSpinBox *m_windowHeightSpin = nullptr;
    /// NOTE: 分辨率那一行虽然占**两个**键（windowWidth / windowHeight），但只有**一个**
    ///       档位下拉框 —— 它俩是同一个决定的两半（见 .hpp 里 override() 那段）。所以那一个
    ///       下拉框是按 `windowWidthField()` 那个 field 存进 m_choiceCombos 的。
    /// 「跟随全局」时那一句解释要说的窗口大小（`GlobalValues::windowWidth/Height`）。
    /// 存下来是给 refreshHints() 用的 —— 它只拿到一次构造参数，之后每次都要重写那句话。
    ///
    /// NOTE: 那个 `-1` 的默认值是"调用方没给"的哨兵：<= 0 时那句话里**不写**尺寸，
    ///       只写"跟随全局"（见 .hpp 里 GlobalValues 那一段）。
    int m_globalWindowWidth = -1;
    int m_globalWindowHeight = -1;
    /// 追加参数那一行是**多行**输入（一条一行）而不是单行：meta 里的 jvmArgs 是数组，
    /// 数组存在的理由就是"参数本身可能含空格"（见 VersionManager::versionJvmArgs()）。
    /// 用单行输入就得在"读出来拼成一行"和"存回去切成数组"之间来回切，而那个来回是
    /// **有损**的：`-Dfoo=bar baz` 会被切成两条。一条一行则原样进原样出。
    QPlainTextEdit *m_appendJvmArgsEdit = nullptr;
    QCheckBox *m_warnJavaMismatchCheck = nullptr;
    QToolButton *m_advancedToggle = nullptr;
    QWidget *m_advancedBox = nullptr;
    /// **展开**「高级选项」之前这个窗有多高。折回时按它还原 —— Qt 只在"窗口比内容还小"
    /// 时把窗口撑大，内容变少时一动不动，不记这一笔的话展开一次再收起，底下就永远留着
    /// 那一截空白（见 .cpp 里那段 NOTE）。0 = 还没展开过。
    int m_heightBeforeAdvanced = 0;

    /// 每一项的档位下拉框，键是那个 field（`javaPathField()` 那一组）。
    ///
    /// ★ 2026-09-28：**从前是七对单选钮**（`m_javaPathFollow` / `m_javaPathCustom` …
    ///   十四位成员），用户原话「所有单选框换成下拉的」之后收成这一个表 ——
    ///   一对成员换一个下拉框，七行长得一模一样，写成表就不必再维护那个 if 链。
    ///   存在成员里（而不是每次 findChild）是因为 refreshHints() 每次信号都要读它们算
    ///   "有效值"，而那是按键最频繁的一条路。
    QHash<QString, QComboBox *> m_choiceCombos;
    /// 「最大内存」「最小内存」两行那一格的容器 —— 自动档下整行禁用，
    /// 禁用的是这两个容器（连同里面的档位下拉框），不是只禁数字框。
    QWidget *m_xmxFollowCell = nullptr;
    QWidget *m_xmsFollowCell = nullptr;
    /// 每一项在「自定义」状态下那句话（选「跟随全局」时说的是"当前全局是哪个值"，
    /// 两种形态都由 refreshHints() 写进同一个 QLabel）。
    QHash<QString, QString> m_customHints;
};
