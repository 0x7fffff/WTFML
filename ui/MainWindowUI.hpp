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

#include <QButtonGroup>
#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
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

    // ---------- 下载页左栏那个「模组加载器」单选组 ----------
    //
    // 「勾了哪个」决定「安装」装什么（0..3 = 加载器，见 VersionManager::loaderKindFromIndex）
    // 而那一个「不装」选项是**默认勾中的**，它代表装纯原版那条路。
    //
    // NOTE: 2026-09-22 起这一组从"左栏孤零零五个钮"搬进了**安装页**：每个加载器
    //       各占一条，钮右边跟着一个选它版本的组合框（见 loaderVersionComboName()）。
    //       组、下标、文案一条都没变 —— 变的是它们现在长在哪一页上。
    //
    /// 选项的 objectName，**顺序就是界面上从上到下的顺序**。验证工程按这个顺序遍历，
    /// 界面上摆的顺序也由同一个表驱动，不各写一份。
    ///
    /// NOTE: 「不装」排在四个加载器**后面**（不是最前面）是刻意的：这样下标 0..3 和
    ///       VersionManager::LoaderKind 的枚举顺序一一对应，中间那层翻译
    ///       （loaderKindFromIndex）就少一个"顺序对不上、勾了 A 装成 B"的机会。
    static QStringList modLoaderOptionNames() {
        return {QStringLiteral("modLoaderFabric"), QStringLiteral("modLoaderForge"),
                QStringLiteral("modLoaderNeoForge"), QStringLiteral("modLoaderOptiFine"),
                QStringLiteral("modLoaderNone")};
    }
    /// 选项上写的字。和上面那张表**一一对应**，顺序不能各排各的。
    ///
    /// NOTE: 这是**显示内容**（将来要跟着语言/产品文案走），不是身份标识 —— 验证工程
    ///       认控件一律用上面的 objectName，这两个函数只在核对文案时用（和
    ///       renameInstanceText() 那几个一个道理）。
    static QStringList modLoaderOptionTexts() {
        return {QStringLiteral("Fabric"), QStringLiteral("Forge"), QStringLiteral("NeoForge"),
                QStringLiteral("OptiFine"), QStringLiteral("不装（纯原版）")};
    }
    /// 「不装（纯原版）」那一个在两张表里的下标。想"回到装纯原版"就得能勾回它 ——
    /// 单选钮一旦勾上就没法用鼠标取消（见下面 buildTwoColumnPage 里那段），这个选项
    /// 就是那条退路；写死数字的地方一律用这个函数。
    static int modLoaderNoneIndex() { return 4; }
    static QString modLoaderLabelText() { return QStringLiteral("模组加载器"); }
    /// 那一组（QButtonGroup）自己的 objectName。验证工程靠它在**真窗口**上把
    /// "这五个钮是一个互斥组"读回来 —— getModLoaderOptions() 只给得出五个钮，
    /// 看不出它们互不互斥（那正是"四选一"里唯一一条看代码看不出来的）。
    static QString modLoaderGroupName() { return QStringLiteral("modLoaderGroup"); }
    /// 某一个加载器那条上选**加载器版本**的组合框。下标就是上面的表（0..3 = 四个加载器，
    /// 「不装」那条没有组合框 —— 它没有版本可选）。
    ///
    /// 名字由 modLoaderOptionNames() 推出来（`modLoaderFabricVersionCombo` 这种），
    /// 不另写一张表：两张表迟早会有一处对不上，而"点了 Fabric 那条却填进 Forge 的版本号"
    /// 不会报错、只会装错东西。
    static QString loaderVersionComboName(int loaderIndex) {
        return modLoaderOptionNames().value(loaderIndex) + QStringLiteral("VersionCombo");
    }
    /// 那四个组合框里第 0 条写的字：**不指定版本** —— 让安装器按老规矩挑（Fabric 挑最新的
    /// 正式版、Forge/NeoForge 挑推荐的那个、OptiFine 挑官方页那一行主推的）。
    /// 它的 itemData 是**空串**，"没钉版本"这件事在数据上就靠这个空串表达。
    static QString loaderVersionAutoText() {
        return QCoreApplication::translate("MainWindowUI", "自动（推荐）");
    }
    /// 还没拿到版本列表时那个组合框上的占位话（一次请求 60~250 KB，等一会儿是常事）。
    /// NOTE: 短 —— 那个组合框也设了 AdjustToMinimumContentsLengthWithIcon，长的占位文案
    ///       会把它撑宽（完整原因见 MainWindow::onVersionsFetched 里那段）；而那条
    ///       minimumContentsLength 是**按 'x' 的宽度**算的，一个汉字差不多顶两个 'x'，
    ///       所以"字数不超 12"并不够 —— 放中文就得照着 5~6 个字来。
    static QString loaderVersionFetchingText() {
        return QCoreApplication::translate("MainWindowUI", "正在获取…");
    }
    /// 版本列表没拉到时那个组合框上的占位话。
    /// NOTE: 比"正在获取…"长一点点，但都短于那条 minimumContentsLength，所以三种文案
    ///       （这句 / 「自动（推荐）」/「正在获取…」）之间换来换去，框宽不会抽
    ///       （ui_check 的 H 节把这条钉住了）。具体原因不去占位文案里说 —— 框里那条
    ///       「自动（推荐）」本身就是退路，原因写进日志。
    static QString loaderVersionFailedText() {
        return QCoreApplication::translate("MainWindowUI", "列表没取到");
    }

    // ---------- 下载页「游戏」里那两层：版本树 / 安装页 ----------
    //
    // 用户原话：「下载版本那里改成四个分类（正式版 快照版 远古版 愚人节版），每一个占一条，
    // 点开后展开对应版本，再点击具体版本后进入一个新页面……顶上预留版本名称输入框……
    // 再下面放四个选项卡分别对应加载器（默认不选就是原版）也是可以点开选择加载器版本的」。
    //
    // 所以「游戏」那一页里再分两层（QStackedWidget）：
    //   [0] 版本树 —— 四条分类，点开是版本号；点一个版本号去 [1]
    //   [1] 安装页 —— 名称框 + 四个加载器（每个一条，右边选版本）+ 「安装」
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
    /// 安装页那一整页（名称框、四个加载器、安装按钮都在它里面）。
    static QString installPageName() { return QStringLiteral("versionInstallPage"); }
    /// 安装页顶上那句"要装的是基于哪个原版的什么"（点的是哪个版本，到这里必须还看得见）。
    static QString installTargetLabelName() { return QStringLiteral("installTargetLabel"); }
    /// 「版本名称」框：实例的目录名（`versions/<这个名字>`），默认名由 MainWindow 按
    /// "原版号 + 加载器"填（见 MainWindow::defaultInstanceName()）。
    static QString installNameEditName() { return QStringLiteral("installNameEdit"); }
    /// 名称框下面那行小字：重名提示 / 名字不合法的原因 / 平时的说明，三种取值一处
    /// 决定（MainWindow::updateInstallNameHint()）。
    static QString installNameHintName() { return QStringLiteral("installNameHint"); }
    static QString installButtonName() { return QStringLiteral("installButton"); }
    static QString installBackButtonName() { return QStringLiteral("installBackButton"); }
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
    // NOTE: 现在只有「游戏」那一页是真的 —— 它就是原来那一摊（挑加载器、挑版本、下载）。
    //       其余四个是**留着的入口**：点进去只有一句"还没做"，位置先占住，免得下次往里
    //       加东西时又要重排整页。那几页上的话由 downloadPlaceholderText() 单点决定。
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
    /// 每个分类**将来**放什么一句话（「游戏」那条是空的：它现在就有内容）。
    /// 这张表只给"还没做"那几页用，所以它和新加的页一一对应。
    static QStringList downloadCategoryNotes() {
        return {QString(),
                QStringLiteral("搜索 Modrinth / CurseForge 上的整合包，看简介和文件清单，一键导入"),
                QStringLiteral("按游戏版本和加载器搜模组，装进选中的那个实例"),
                QStringLiteral("搜资源包，装进选中的那个实例"),
                QStringLiteral("搜光影包，装进选中的那个实例")};
    }
    /// 还没做的那几页上写的那句话。**唯一出处** —— 页上那个标签读的就是它。
    /// @param index downloadCategoryKeys() 的下标；「游戏」那一条返回空串（它有内容）。
    static QString downloadPlaceholderText(int index) {
        const QStringList texts = downloadCategoryTexts();
        const QStringList notes = downloadCategoryNotes();
        if (index < 0 || index >= texts.size() || index >= notes.size() ||
            notes.at(index).isEmpty()) {
            return QString();
        }
        return QStringLiteral("「%1」还没做。\n\n这里的入口先留着，将来放：%2。")
            .arg(texts.at(index), notes.at(index));
    }

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
    /// 「导入整合包…」。它和上面那三个**不是一回事**：那三个作用于列表里选中的实例，
    /// 这个是造一个新实例出来。所以它单独一行、摆在它们上面（见 .cpp 里那一段）。
    static QString importModpackButtonName() { return QStringLiteral("importModpackButton"); }
    // 按钮上的字。验证工程**不要**拿它们当身份标识（那是显示内容），只用来核对文案。
    static QString renameInstanceText() { return QStringLiteral("重命名…"); }
    static QString groupInstanceText() { return QStringLiteral("分组…"); }
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
    /// 默认那一套份数：左 3 右 7。实例页/下载页，以及设置页那张两列表格都用它。
    static int leftColumnStretch() { return 3; }
    /// 右栏占的份数（默认那一套）。
    static int rightColumnStretch() { return 7; }
    /// 右栏在页面上是不是真的更宽（验证工程拿它当"默认的 3:7 生效了"的判据）。
    static bool rightColumnIsWider() { return rightColumnStretch() > leftColumnStretch(); }

    /// 这一页是不是"左宽右窄"。
    /// @param tab "home" / "instance" / "download"。
    ///
    /// 只有主页是。主页的右栏现在**空着**（原来那张「版本列表」已经去掉 —— 实例页右边
    /// 已经有一张同样数据的列表了，主页那张是把同一件事说两遍），所以宽度让给左边的
    /// "选版本 + 启动"那两样。另外两页的左栏仍是"只放选一个"的窄栏。
    static bool leftColumnIsWider(const QString &tab) {
        return tab == QStringLiteral("home");
    }
    /// 某一页左栏占的份数：主页把默认那一对**对调**过来用（7:3）。
    static int leftColumnStretch(const QString &tab) {
        return leftColumnIsWider(tab) ? rightColumnStretch() : leftColumnStretch();
    }
    /// 某一页右栏占的份数（见 leftColumnStretch(const QString&)）。
    static int rightColumnStretch(const QString &tab) {
        return leftColumnIsWider(tab) ? leftColumnStretch() : rightColumnStretch();
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
        return launching || downloading || blocking;
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
    /// 名称框下面那行小字（重名 / 名字不合法 / 平时说明，三种取值一处决定）。
    QLabel *getInstallNameHint() const { return installNameHint; }
    /// 安装页顶上那句"将基于原版 X 安装"。
    QLabel *getInstallTargetLabel() const { return installTargetLabel; }
    /// 安装页那个「安装」按钮。原来那个「下载版本」按钮的位子 —— 它现在只在这一页上，
    /// 而"装什么"由这一页上的名称框 + 加载器那几条决定。
    QPushButton *getInstallButton() const { return installButton; }
    /// 「← 返回版本列表」：回树那一层（不改任何选择，回去还是原来那棵树）。
    QPushButton *getInstallBackButton() const { return installBackButton; }
    /// 下载页安装页那个「模组加载器」单选组（见上面那段）。
    ///
    /// NOTE: 返回的是**那一组**（按钮组 + 每个钮），顺序就是 modLoaderOptionNames() 的
    ///       顺序。"勾了哪个"从 modLoaderGroup->checkedId() 读，id 就是下标。
    QButtonGroup *getModLoaderGroup() const { return modLoaderGroup; }
    QVector<QRadioButton *> getModLoaderOptions() const {
        return {modLoaderFabric, modLoaderForge, modLoaderNeoForge, modLoaderOptiFine,
                modLoaderNone};
    }
    /// 四个加载器各一条的那个"选版本"组合框，**下标就是加载器下标**（0..3，
    /// 和 modLoaderOptionNames() 前四个、以及 VersionManager::loaderKindFromIndex 同序）。
    /// 「不装（纯原版）」那条没有它 —— 原版没有加载器版本可选。
    /// 第 0 条是 loaderVersionAutoText()（itemData 是空串 = 不钉版本，让安装器自己挑）。
    QVector<QComboBox *> getLoaderVersionCombos() const { return loaderVersionCombos; }

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
    QPushButton *startButton;
    QPushButton *accountSkinSettingsButton;
    QCheckBox *offlineModeCheckBox;
    QPushButton *addFolderButton;
    QLabel *folderHintLabel;
    QPushButton *removeFolderButton;
    QListWidget *folderList;
    QTreeWidget *instanceList;
    QPushButton *renameInstanceButton;
    QPushButton *groupInstanceButton;
    QPushButton *instanceSettingsButton;
    QPushButton *importModpackButton;
    QLabel *instanceFolderLabel;
    QComboBox *installedVersionCombo;
    /// 下载页左栏那份「下载分类」（见头文件上面那段）与右栏那份 QStackedWidget。
    /// 只有「游戏」那一页是真的，其余四页各一句"还没做"。
    QListWidget *downloadCategoryList;
    QStackedWidget *downloadStack;
    /// 「游戏」那一页里的两层：版本树 / 安装页（见头文件上面那段）。
    QStackedWidget *gameStack;
    QTreeWidget *versionTree;
    QLabel *installTargetLabel;
    QLineEdit *installNameEdit;
    QLabel *installNameHint;
    QPushButton *installButton;
    QPushButton *installBackButton;
    /// 下载页安装页那个「模组加载器」单选组（见头文件上面那段）。
    ///
    /// NOTE: **互斥**由 QButtonGroup 保证（不是靠"同一个父控件下的 QRadioButton 自动
    ///       互斥"那条不成文的规矩 —— 哪天有人把它们挪进各自的容器，那条就悄悄失效了，
    ///       而失效的表现是"几个能同时勾上"，丑但不报错）。id 就是
    ///       modLoaderOptionNames() 里的下标。
    /// NOTE: modLoaderNone 是**初始勾中**的那一个（"装纯原版"）。
    QButtonGroup *modLoaderGroup;
    QRadioButton *modLoaderFabric;
    QRadioButton *modLoaderForge;
    QRadioButton *modLoaderNeoForge;
    QRadioButton *modLoaderOptiFine;
    QRadioButton *modLoaderNone;
    /// 四个加载器各一条的那个"选版本"组合框（下标 = 加载器下标，见
    /// loaderVersionComboName()）。「不装（纯原版）」那条没有 —— 原版没有加载器版本可选。
    ///
    /// NOTE: 用一张表而不是四个具名成员：这一组的身份**就是**下标（和加载器枚举同序），
    ///       接线时 `combos.at(loaderIndex)` 一个数就够；写成四个成员的话，"哪个成员是
    ///       哪个加载器"这件事得在每一处用它的人那儿重新对一遍。
    QVector<QComboBox *> loaderVersionCombos;
    QComboBox *javaVersionCombo;
    QLabel *javaPathLabel;
    QCheckBox *javaWarnMismatchCheck;
    QCheckBox *memoryAutoCheck;
    QLabel *memoryAutoHint;
    QSpinBox *memoryMaxSpin;
    QSpinBox *memoryMinSpin;
    QComboBox *launcherBehaviourCombo;
    QPlainTextEdit *extraJvmArgsEdit;
    QPushButton *applyExtraJvmArgsButton;
    QComboBox *downloadSourceCombo;
    QCheckBox *versionNameCustomCheck;
    QCheckBox *versionNameRealCheck;
    QCheckBox *versionNameLoaderCheck;
    QCheckBox *versionNameLoaderVersionCheck;
    SkinViewer *skinViewer;
    QLabel *skinHintLabel;
    QPushButton *accountSkinButton;
    QTabWidget *tabWidget;
    QFrame *bottomBar;
    QLabel *avatarLabel;
    QLabel *accountLabel;
    // 账户格那两行是**竖直排列**的（名字在上、登录方式在下），和左上角那格同构。
    QLabel *accountTypeLabel;
    QProgressBar *progressBar;
    QLabel *statusLabel;
    QLabel *runStateLabel;
    QLabel *runStateDetailLabel;
};
