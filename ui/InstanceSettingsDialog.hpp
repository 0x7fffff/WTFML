/**
 * @file InstanceSettingsDialog.hpp
 * @brief 「实例设置…」对话框。**2026-09-26 起分栏**：左栏大类（概览 / 启动 / 文件管理）、
 *        选到「文件管理」时中间再长出一列小类、右边一块堆叠页（见下面 categoryKeys()）。
 *        分栏之前它是一张通到底的表：上面一行实例名，下面给单个实例覆盖启动五项
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
 * iconChoice() …），落盘在 MainWindow::applyInstanceSettings() 里。
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
#include <QList>
#include <QPair>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <optional>

class InstanceFilesPage;

class QCheckBox;
class QComboBox;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QVBoxLayout;
class QWidget;

class InstanceSettingsDialog : public QDialog {
    Q_OBJECT

public:
    /// 这个实例覆盖的那五项。没有值 = 跟随全局（空串也算有值，见 EffectiveJavaSettings.hpp）。
    struct Override {
        std::optional<QString> javaPath;
        std::optional<int> xmx;
        std::optional<int> xms;
        std::optional<bool> autoMemory;
        std::optional<QString> extraJvmArgs;
    };

    /// 全局那一层的值。只用来写「跟随全局（当前 …）」那句话 —— 用户要能看见自己
    /// 跟随的到底是什么数，否则"跟随全局"就是一格写不出内容的空白。
    struct GlobalValues {
        QString javaPath;
        int xmx = 0;
        int xms = 0;
        bool autoMemory = true;
        QString extraJvmArgs;
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
     * @param parent 父窗口。
     */
    InstanceSettingsDialog(const QString &instanceName, const QString &instanceId,
                           const Override &current, const GlobalValues &global,
                           const QStringList &appendJvmArgs, bool warnJavaMismatch,
                           QWidget *parent = nullptr);

    /// 用户摆好的覆盖。五项各自：那行选了「自定义」就有值，选了「跟随全局」就没有值。
    ///
    /// NOTE: 「内存分配」若是自动档（这一行选「自定义」且勾着自动，或者跟随全局而全局
    ///       是自动），最大/最小内存**一律当成跟随全局**（那两行这时是禁用的）。
    ///       自动档下这两个数由系统内存现算，存一份永远不会被读的覆盖就是骗人 ——
    ///       "让用户以为能改"正是设置页那条注释明确反对的事。
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

    // ---------- 分栏（2026-09-26）----------
    //
    // 用户原话：「实例设置里也这么改，概览（版本名、图标，分组，置顶/取消置顶）启动（Java
    // 选项，游戏窗口标题，内存分配，导出启动脚本，补全文件）文件管理（**再做一个类似的
    // 左侧菜单叠在已有的左边栏上**，…）」。
    //
    // 所以这一页长成三栏：**左栏大类**（概览 / 启动 / 文件管理）、**中栏小类**（只有选到
    // 「文件管理」时才出现）、**右栏内容**（一份堆叠页）。形状和设置页那一套是同一个套路
    // ——「左栏一列分类、右栏一份堆叠页」，只是这儿中间能再长出一列来。
    //
    // 键表与 objectName 的写法照抄设置页（settingsCategoryKeys / settingsPanelName）：
    // **两张按下标对齐的表**，加一栏要同时动两处；身份走 objectName，验证工程不靠位置认控件。

    /// 左栏那三条大类的键。**顺序就是界面上从上到下的顺序**。
    static QString overviewCategoryKey() { return QStringLiteral("overview"); }
    static QString launchCategoryKey() { return QStringLiteral("launch"); }
    static QString filesCategoryKey() { return QStringLiteral("files"); }
    static QStringList categoryKeys() {
        return {overviewCategoryKey(), launchCategoryKey(), filesCategoryKey()};
    }
    static QStringList categoryTexts() {
        return {QStringLiteral("概览"), QStringLiteral("启动"), QStringLiteral("文件管理")};
    }

    /// 「文件管理」下面那一列小类的键。**顺序就是界面上的顺序。**
    ///
    /// NOTE: 用户给的那一串（快捷方式 / 资源包·模组 / 数据包 / 光影包 / 原理图 / 存档 /
    ///       服务器列表）是他自己说了「我这个不合理」的，所以这儿是重排过的：把**跳板**
    ///       （快捷方式：开文件夹）摆在最前，然后是**往实例里加东西**的那几类（模组最常用
    ///       → 资源包 → 数据包 → 光影包 → 原理图），最后是**这个实例自己攒出来的**那两样
    ///       （存档、服务器列表）。前六类是"往里放"，后两类是"从里面看"。
    static QStringList filePageKeys() {
        return {QStringLiteral("shortcut"), QStringLiteral("mod"),    QStringLiteral("resourcepack"),
                QStringLiteral("datapack"), QStringLiteral("shader"), QStringLiteral("schematic"),
                QStringLiteral("save"),     QStringLiteral("server")};
    }
    static QStringList filePageTexts() {
        return {QStringLiteral("快捷方式"), QStringLiteral("模组"),   QStringLiteral("资源包"),
                QStringLiteral("数据包"),   QStringLiteral("光影包"), QStringLiteral("原理图"),
                QStringLiteral("存档"),     QStringLiteral("服务器列表")};
    }

    /// 堆叠页里「文件管理」那些页的起始下标（0/1 是概览与启动，排在前面）。
    /// 换言之 `stackIndex = filePageFirstIndex() + 小类下标`。
    static int filePageFirstIndex() { return 2; }
    /// 堆叠页一共有几页。
    static int pageCount() { return filePageFirstIndex() + filePageKeys().size(); }

    static QString categoryListName() { return QStringLiteral("instanceSettingsCategoryList"); }
    static QString subCategoryListName() {
        return QStringLiteral("instanceSettingsSubCategoryList");
    }
    static QString stackName() { return QStringLiteral("instanceSettingsStack"); }
    /// 堆叠页里某一页的 objectName（@param key 见 categoryKeys() / filePageKeys()）。
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

    /// 摆到 @p key 那一栏上（等价于点左栏里那一条）。越界 / 认不出来的键 = 什么都不做。
    /// 验证工程用它跳栏 —— 它比"数第几条"稳（键表将来插一条也不会错位）。
    void showCategory(const QString &key);

    /// 这三条给验证工程问几何用（认控件主要还是按 objectName）。
    QListWidget *categoryList() const { return m_categoryList; }
    QListWidget *subCategoryList() const { return m_subCategoryList; }
    QStackedWidget *stack() const { return m_stack; }

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

    /// 同上：**装了 Litematica 才谈得上原理图**（那是 Litematica 自己的目录）。
    void setSchematicUsable(bool usable);

    /// 服务器列表那一页的内容。
    /// @param servers 名字 → 地址，按文件里原来的顺序。
    /// @param problem 读不动的原因（空串 = 读到了）。
    ///
    /// NOTE: 「读到了但是一条都没有」和「没读到」要分开给 —— 前者那一页说"还没加过服务器"，
    ///       后者说清楚为什么没读到（像版本树那条"问到了就是没有"与"列表没取到"）。
    void setServerList(const QList<QPair<QString, QString>> &servers, const QString &problem);

    /// 把六页目录列表**重新列一遍**。MainWindow 动过盘（删了 / 改了名）之后喊它。
    void refreshFilePages();

    /// 给验证工程用的访问器：某一页那个文件列表控件（键见 filePageKeys()）。
    InstanceFilesPage *filesPage(const QString &key) const;
    QList<QWidget *> shortcutButtons() const { return m_shortcutButtons; }
    QTreeWidget *serverTree() const { return m_serverTree; }
    QLabel *serverNotice() const { return m_serverNotice; }
    QPushButton *serverRefreshButton() const { return m_serverRefresh; }

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
    ///       这一份是「文件管理」用的：快捷方式那一页的「版本目录」开的就是它。
    void setInstanceDir(const QString &dir);

    /// 「文件管理」那几页各看哪个目录：**键 → 绝对路径**。
    ///
    /// 一共**八条**：中间那六页各一条（`mod` / `resourcepack` / `datapack` / `shader` /
    /// `schematic` / `save`），外加两个**只有快捷方式那一页用得上**的（`screenshots` /
    /// `logs` —— 它们没有自己的页）。`shortcut` 和 `server` 两个键不用给：前者开的是
    /// setInstanceDir() 那一份（`dir`），后者给的是**解析好的一份列表**（见 setServerList()）。
    ///
    /// NOTE: 路径**由调用方算好递进来**，这一层不自己拼 `<实例目录>/mods` —— 那几个目录名
    ///       是 MC 的约定，仓库里已经有唯一一份出处（src/download/ResourceCatalog 的
    ///       folderForKind()，整合包那一路也照着它写），ui/ 抄第二份迟早会跟它对不上。
    void setFileDirs(const QHash<QString, QString> &dirs);

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
    /// 某一项的「跟随全局」/「自定义」单选钮。@param field 见下面那五个常量。
    static QString followRadioName(const QString &field) {
        return QStringLiteral("override_%1_follow").arg(field);
    }
    static QString customRadioName(const QString &field) {
        return QStringLiteral("override_%1_custom").arg(field);
    }
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
    static QString autoMemoryField() { return QStringLiteral("autoMemory"); }
    static QString extraJvmArgsField() { return QStringLiteral("extraJvmArgs"); }

    static QString javaPathEditName() { return QStringLiteral("instanceJavaPathEdit"); }
    static QString browseJavaButtonName() {
        return QStringLiteral("instanceBrowseJavaButton");
    }
    static QString memoryMaxSpinName() { return QStringLiteral("instanceMemoryMaxSpin"); }
    static QString memoryMinSpinName() { return QStringLiteral("instanceMemoryMinSpin"); }
    static QString memoryAutoCheckName() { return QStringLiteral("instanceMemoryAutoCheck"); }
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
    /// 第四行那个「导出…」**占位**按钮（灰的、点了什么都不发生）。
    static QString exportPlaceholderButtonName() {
        return QStringLiteral("instanceExportPlaceholderButton");
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

    // ---- 文件管理那八页的文案与 objectName（2026-09-26）----

    /// 快捷方式那一页的九颗按钮：**两张按下标对齐的表**（按钮上的字 / 路径键）。
    /// 路径键见 setFileDirs() 与 setInstanceDir()。
    ///
    /// NOTE: 顺序是排过的（用户原话「前面这些按照合理顺序排，我这个不合理」）：先是**这个
    ///       实例自己**（版本目录），再是**游戏玩出来的**那两样（存档、截图），然后是
    ///       **往里放东西**的那一串（模组 → 资源包 → 数据包 → 光影包 → 原理图，和左栏那列
    ///       小类同一个顺序），最后是**出事时去翻**的日志。
    static QStringList shortcutTexts();
    static QStringList shortcutPathKeys();
    /// 快捷方式一排摆几颗（九颗正好三排）。
    static int shortcutPerRow() { return 3; }
    /// 第 @p index 颗快捷方式按钮。
    static QString shortcutButtonName(int index) {
        return QStringLiteral("instanceShortcutButton%1").arg(index);
    }

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

    static QString serverTreeName() { return QStringLiteral("instanceServerTree"); }
    static QString serverNoticeName() { return QStringLiteral("instanceServerNotice"); }
    static QString serverRefreshButtonName() {
        return QStringLiteral("instanceServerRefreshButton");
    }
    static QString serverRefreshText() { return QStringLiteral("刷新"); }
    static QString serverNameColumnText() { return QStringLiteral("名称"); }
    static QString serverAddressColumnText() { return QStringLiteral("地址"); }
    static QString serverEmptyText() { return QStringLiteral("这个实例还没有加过服务器。"); }
    /// 服务器名字那一格是空的的时候顶上什么字。servers.dat 里的 name 真能是空串。
    static QString unnamedMark() { return QStringLiteral("（无名）"); }

    /// 「光影包」那一页用不上时那句话（唯一出处，验证工程也按它核对）。
    static QString shaderUnavailableText();
    /// 「原理图」那一页用不上时那句话。
    static QString schematicUnavailableText();

    /// 第二行右边那颗按钮上的字（**唯一出处**：它是打开那个浮层的按钮，也是这一项的
    /// 名字 —— 概览这一栏不再有单独的行名）。
    static QString groupButtonText() { return QStringLiteral("分组…"); }
    /// 第三行那个勾选框上的字。**用户 2026-09-26 点名的这一句**（原来叫「置顶（固定在最
    /// 前面）」，那是"置顶 = 搬进置顶组"那套旧语义留下的说法）。
    static QString pinCheckText() { return QStringLiteral("显示在置顶组中"); }
    /// 「更改图标」/「导出…」两颗按钮上的字。
    static QString changeIconText() { return QStringLiteral("更改图标"); }
    static QString exportPlaceholderText() { return QStringLiteral("导出…"); }
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
        return QStringLiteral("勾上之后这个实例会在两个地方各有一条：原来那一组里留一条，"
                              "置顶组里再摆一条同名的（置顶组永远排在最前面）。"
                              "取消勾选，置顶组里那一条就没了，原来那一组从头到尾不动。");
    }
    /// 第四行那个占位按钮下面那句话。
    static QString exportPlaceholderHint() {
        return QStringLiteral("还没做。以后会把整个实例（版本文件、模组、配置、存档）打包成"
                              "一个能直接导入的文件。");
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

signals:
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
     * @brief 用户点了「开始检查」。
     *
     * **能不能开始、要不要先问一句，全由 MainWindow 说了算** —— 只有它知道有没有下载在跑、
     * 这个实例这会儿装没装。这一层只报"用户想干这件事"。
     */
    void repairFilesRequested();

    /**
     * @brief 用户在一个文件夹上按了「打开」（快捷方式那九颗按钮，或者某一页的「打开文件夹」）。
     *
     * **这一层不自己开**：开目录是 QDesktopServices 的事，开不了要弹一句，而弹话的
     * InlineMessage 每一个入口都要 OverlayHost —— 那个在 MainWindow 手里。
     * @param path 要打开的目录（绝对路径）。
     */
    void openInstanceFolderRequested(const QString &path);

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
     * @brief 用户按了服务器列表那一页的「刷新」。
     *
     * 那一页的内容是**解析好的结果**递进来的（setServerList()），重读一遍要碰 src/ 里的
     * NBT 解析，所以只能由 MainWindow 接。
     */
    void refreshServerListRequested();

private:
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

    /// 左栏选中第 @p row 条之后要做的事：换页 + （只有「文件管理」才）露出中栏那一列。
    /// 越界 = 什么都不做（见 .cpp 里那段 NOTE）。
    void applyCategory(int row);
    /// 中栏选中第 @p row 条之后要做的事：把右栏换到那一页。
    void applyFilePage(int row);

    /// 在中栏那一条上挂（或者摘掉）「这一页为什么用不上」的 tooltip。
    /// @param reason 空串 = 用得上（把 tooltip 摘掉）。见 setShaderUsable()。
    void markPageUnusable(const QString &key, const QString &reason);

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

    // ---------- 三栏那几件（见 categoryKeys() 上面那一段）----------
    /// 左栏那三条大类。当前行 = 界面上的当前栏。
    QListWidget *m_categoryList = nullptr;
    /// 中栏那八条小类。**只在「文件管理」那一栏里显示** —— 其余两栏里它是隐藏的，
    /// 但 `currentRow` 留着（切走再切回来还停在刚才那一页）。
    QListWidget *m_subCategoryList = nullptr;
    /// 右栏那份堆叠页，下标见 pageCount() / filePageFirstIndex()。
    QStackedWidget *m_stack = nullptr;

    /// 「实例名」那一格（见 instanceName()）。现在长在「概览」栏里。
    QLineEdit *m_instanceNameEdit = nullptr;

    /// 这个实例现在的目录名（构造时那个 instanceId）。留着是为了"导出启动脚本"那条路
    /// 的默认文件名 —— 它一定是合法文件名（validateDirName 那一关挡过），而显示名不一定。
    QString m_instanceId;

    // ---- 文件管理那八页（见上面 filePageKeys() / setFileDirs() 那两段）----
    /// 这个实例的目录（setInstanceDir()）：快捷方式那一页的「版本目录」开的就是它。
    QString m_instanceDir;
    /// 「文件管理」那几页各自的目录（见 setFileDirs()）：**页键 → 绝对路径**。
    QHash<QString, QString> m_fileDirs;
    /// 六页目录列表控件（键见 filePageKeys()）。refreshFilePages() 走它。
    QHash<QString, InstanceFilesPage *> m_filesPages;
    /// 快捷方式那一页那九颗按钮（顺序同 shortcutTexts()）。
    QList<QWidget *> m_shortcutButtons;
    QTreeWidget *m_serverTree = nullptr;
    QLabel *m_serverNotice = nullptr;
    QPushButton *m_serverRefresh = nullptr;

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
    QSpinBox *m_memoryMinSpin = nullptr;
    QCheckBox *m_memoryAutoCheck = nullptr;
    QLineEdit *m_extraJvmArgsEdit = nullptr;
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

    // 五行各一对单选钮。存在成员里（而不是每次 findChild）是因为 refreshHints()
    // 每次信号都要读它们算"有效值"，而那是按键最频繁的一条路。
    QRadioButton *m_javaPathFollow = nullptr;
    QRadioButton *m_javaPathCustom = nullptr;
    QRadioButton *m_xmxFollow = nullptr;
    QRadioButton *m_xmxCustom = nullptr;
    QRadioButton *m_xmsFollow = nullptr;
    QRadioButton *m_xmsCustom = nullptr;
    QRadioButton *m_autoMemoryFollow = nullptr;
    QRadioButton *m_autoMemoryCustom = nullptr;
    QRadioButton *m_extraJvmArgsFollow = nullptr;
    QRadioButton *m_extraJvmArgsCustom = nullptr;
    /// 「最大内存」「最小内存」两行左边那两对单选钮的容器 —— 自动档下整行禁用，
    /// 禁用的是这两个容器（连同里面那对钮），不是只禁数字框。
    QWidget *m_xmxFollowCell = nullptr;
    QWidget *m_xmsFollowCell = nullptr;
    /// 每一项在「自定义」状态下那句话（选「跟随全局」时说的是"当前全局是哪个值"，
    /// 两种形态都由 refreshHints() 写进同一个 QLabel）。
    QHash<QString, QString> m_customHints;
};
