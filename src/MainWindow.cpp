/**
 * @file MainWindow.cpp
 * @brief 实现主应用程序窗口及用户交互逻辑。
 * @author 0x7fffff
 * @date 2026-04-11
 * @note 本文件实现了 UI 事件处理函数并委托给各个管理器的方法。
 *
 * 本文件将 UI 控件连接到启动器，处理用户命令，
 * 并协调身份验证、下载和启动操作。
 */

#include "MainWindow.hpp"

#include "..\ui\ConfigFileEditorPage.hpp"
#include "..\ui\ConfigFileText.hpp"
#include "..\ui\GroupDialog.hpp"
#include "..\ui\InstanceGroupDelegate.hpp"
#include "..\ui\InstanceFilesPage.hpp"
#include "..\ui\InstanceSettingsDialog.hpp"
#include "..\ui\JavaMismatchDialog.hpp"
#include "..\ui\AccountSkinDialog.hpp"
#include "..\ui\ResourceDetailPage.hpp"
#include "..\ui\GameLogWindow.hpp"
#include "..\ui\MainWindowUI.hpp"
// 内容区上面那一层（2026-09-25）：设置界面和各种提示都开在它里面，不再另开独立窗口。
// 见它的文件头 —— ui/ 那边只管"开在哪儿、长什么样"，不认识业务。
#include "..\ui\OverlayHost.hpp"
// QMessageBox 的替身（2026-09-25 换过来的）：`InlineMessage::warning(overlay, 标题, 正文)`
// 顶掉原来的 `QMessageBox::warning(this, 标题, 正文)`。原来那些调用点**一行一行**对得上，
// 语义一个字没变 —— 变的是"这一句说在哪"。
#include "..\ui\InlineMessage.hpp"
// 主页右栏那个小人怎么画：几何、投影、默认外观、头像裁切全在它里面（纯函数 + 值）。
#include "..\ui\SkinModel.hpp"
// 顶上那条自己画的标题栏。这个头是 2026-09-25 加的：`MainWindowUI::titleBar()` 只前置
// 声明了 `class TitleBar`，而"设置页改名字 → 条上立刻跟着变"那两处要调它的 setBrandText()。
#include "..\ui\TitleBar.hpp"
// 加载器那棵树底下每一条版本号前面挂的**它自己那个加载器的标志**（见
// fillLoaderVersionItems()）：那几条是"Fabric 能装哪些版本"，前面顶个草方块会让人
// 以为是原版号。树的顶层那五条由 MainWindowUI 那边给图标，这里只管子条目。
#include "..\ui\VersionIcons.hpp"
#include "..\ui\VersionTreeDelegate.hpp"
#include "..\ui\WindowChrome.hpp"
// 树上"这一行被激活了"的唯一接线口（鼠标单击 + 键盘回车，一次手势只进来一次）。
// 三棵树（版本树 / 加载器树 / 下载页那几张资源表）都走它，见那个文件头。
#include "..\ui\RowActivation.hpp"

// 添加以下头文件（防止hpp变动导致文件失效）
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
// 「配置文件」那一页记盘上的"那一份"要一秒不差地带上修改时间（见 ConfigFileText::Stamp），
// 备份文件夹名也是拿它拼的。
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
// 用户挑的实例图标要**解码再编码**成实例目录下那份 icon.png（见 writeInstanceIcon()）。
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
// 底栏面板里那几行的图标要按"底栏那几行字的墨色"上色（图标是画的，颜色由调用方给）。
#include <QPalette>
// 下载页那几张资源表前面那个缩略图（onResourceIconReady 里还要造一个全透明的占位）。
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
// 写图标那张图走它（写临时文件 → 原子替换），见 writeInstanceIcon()。
#include <QSaveFile>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
// 整合包那一页下完 .mrpack 之后要"排到下一轮"再走导入（不能在那个信号栈里直接开新批次，
// 见 onResourceDownloadFinished 里那段 NOTE）。
#include <QThread>
// 「文件管理」那几页的图标反查跑在它上面（见 requestInstanceFileIcons 里那段 NOTE）。
#include <QTimer>
#include <QUrl>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
// 日志窗那两颗导出键的保存框默认落在桌面（见 exportDefaultDirectory()）。
#include <QStandardPaths>

// 底栏右面板那三行要按 saved_at 排序（std::stable_sort：同一秒里存的两个账号要保持
// 列表里原来的先后，不然每次点开顺序都可能不一样）。
#include <algorithm>
// 「配置文件」那一页挑最近那一份备份（`std::greater<QString>`：备份文件夹名倒序 == 时间
// 倒序，见 ConfigFileText::backupFolderName() 那段 NOTE）。
#include <functional>

#include "auth/AccountStore.hpp"
#include "auth/AuthManager.hpp"
#include "auth/SkinApi.hpp"
// 下载页那几类资源的取数（搜索 / 版本列表 / 图标）。头文件那边只前置声明了这个类
// （那里用不到它的定义），这里要用真的。
#include "download/ResourceStore.hpp"
#include "launcher/LauncherManager.hpp"
#include "manage/DefaultSkin.hpp"
#include "manage/FolderManager.hpp"
#include "manage/InstanceFiles.hpp"
#include "manage/InstanceGroupStore.hpp"
#include "manage/ModpackExport.hpp"
#include "manage/ModpackImport.hpp"
#include "manage/PlayerSkinStore.hpp"
#include "manage/RandomSkinStore.hpp"
#include "manage/VersionDisplay.hpp"
#include "manage/VersionManager.hpp"
#include "settings/EffectiveJavaSettings.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include "settings/LauncherSettings.hpp"
#include "util/AppPaths.hpp"
// 日志窗底下那颗「导出错误报告…」那一份文本怎么拼（头部那些事实 + 两段日志）。
#include "util/ErrorReport.hpp"
// 「服务器列表」那一页要读 servers.dat（NBT，老版本还是 gzip 过的）。
#include "util/NbtUtil.hpp"
// 同一页的「测速」：真的连上去问一遍每台现在什么样（图标 / MOTD / 人数 / 延迟）。
#include "util/ServerPing.hpp"
#include "util/SystemMemory.hpp"
// 「导入整合包…」要读压缩包根目录里的 modrinth.index.json（ZipUtil::readEntry）。
#include "util/ZipUtil.hpp"

namespace {

/**
 * @brief 把 MB 数说成人话：1 GB 以上按 GB 显示一位小数，否则按 MB。
 * @param mb 内存量（MB）；<= 0 表示"读不到"。
 * @return 给界面看的字符串；读不到时是空串（调用方自己决定怎么说这件事）。
 *
 * 只说数字和单位，不带句子 —— 句子在 MainWindow 里拼，那里有 tr()。这个文件里的
 * 自由函数没有翻译上下文，别在这儿硬塞中文句子。
 */
QString humanMemorySize(qint64 mb) {
    if (mb <= 0) {
        return QString();
    }
    if (mb >= 1024) {
        return QStringLiteral("%1 GB").arg(QString::number(double(mb) / 1024.0, 'f', 1));
    }
    return QStringLiteral("%1 MB").arg(mb);
}

/**
 * @brief 读下拉框里选中的版本号（**不是**显示文本）。
 * @param combo 版本下拉框。
 * @return 选中的版本号；没选中任何项时返回空串。
 *
 * NOTE: 取的是 Qt::UserRole 里存的裸 id，不是 currentText()。这两个值从"版本显示名"
 *       那次改动起就分家了：条目文本可能是 `我的整合包-1.20.1-Forge`，而真正进
 *       `--version` 和文件路径的永远是 id。拿文本当版本号用会一路静默地拼出错误路径。
 *
 * currentIndex() 为 -1 表示"当前什么都没选"（下拉框空着，或者刚好被 clear() 过），
 * 这时 currentData() 也要当没选。显式写 currentIndex() 是为了说清为什么空串可以当
 * 判据：任何版本号都不会是空串。
 */
QString selectedVersionId(const QComboBox *combo) {
    return combo->currentIndex() < 0 ? QString() : combo->currentData().toString();
}

/**
 * @brief 把一批版本灌进下拉框，并尽量保住用户原来选中的那一项。
 * @param combo 目标下拉框。
 * @param parts 每个版本的显示名组成部分。
 * @param options 全局「版本名格式」开关。
 *
 * 条目文本是 formatVersionName() 拼出来的显示名，版本号存在 Qt::UserRole 里，
 * 两者由 selectedVersionId() / currentText() 分开取。同一个版本在两个下拉框里
 * 可能显示得不一样（比如一个 `我的整合包-1.20.1`、一个 `1.20.1`），这是两个数据源
 * 各自独立设计的必然结果：已安装那边能从磁盘读到自定义名，在线清单 800+ 条只填 id。
 * 别为了"看着一致"给在线清单加逐条文件 I/O。
 *
 * 以前只有一个 QListWidget，clear() 会把用户选中的版本一起丢掉，所以下载完成时
 * 干脆不刷新列表。现在刷新必须自己把选中项捡回来。
 *
 * 语义是"只保住、不新增"：原来选的还在清单里就选回去；已经不在了（目录被删、
 * manifest 变了）就退回 currentIndex() == -1，让占位文案显出来，逼用户重新明确
 * 挑一个 —— 启动和下载都是重操作，不替用户猜。
 */
void fillVersionCombo(QComboBox *combo, const QList<VersionDisplayParts> &parts,
                      const VersionNameOptions &options) {
    const QString previous = selectedVersionId(combo);

    // 重灌过程会发 currentIndexChanged。现在接的这些槽都是幂等的，但先挡上，
    // 免得以后有人接槽时被中间态（先变成 -1 再选中）刷两遍。
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (const VersionDisplayParts &part : parts) {
        // NOTE: 必须逐条 addItem(显示名, id)。addItems(QStringList) 是**不设**
        // Qt::UserRole 的，用它就等于所有条目的 currentData() 都是无效 QVariant，
        // selectedVersionId() 会一路返回空串 —— 启动按钮就再也启不动任何版本了。
        combo->addItem(formatVersionName(part, options), part.id);
    }

    // NOTE: 别指望 QComboBox 自己选中第 0 项。只要下拉框设了非空 placeholderText，
    // 往空模型里插第一批数据时它不会自动选中任何一项。选中项只能自己恢复。
    // 也必须按 id（findData）找回来，不能按显示文本（findText）——显示名会随
    // 自定义名/开关变，拿它当身份标识一定找错。
    if (previous.isEmpty()) {
        return;
    }
    if (const int restored = combo->findData(previous); restored >= 0) {
        combo->setCurrentIndex(restored);
    }
}

// ======================================================================
// 实例页那张树（分组）
// ======================================================================
//
// 这一组自由函数和上面那些直筒子列表的写法（fillVersionCombo / syncListToCombo 那一类）
// **不能合并**：实例列表要两层（组 + 实例）。硬合并的写法是"到处判 item 是组还是实例"，
// 那样每多一个调用点就多一次判错的机会。
//
// NOTE: 2026-09-22 下载页那份"可安装版本"从下拉框 + 列表换成了版本树，原来和这一组
//       配对的那几个（fillVersionList / syncListToCombo / syncComboToList /
//       fillVersionViews / plainVersionParts）随之删掉了 —— 它们只服务于那一对。

/**
 * @brief 实例树里一个组节点，以及挂在它下面的那些实例。
 *
 * 「未分组」用 groupId 为空表示 —— 它是**虚拟**的，不进 wtfml.folders，而且只有存在
 * 真实分组时才会被画出来（一个组都没有时列表是平铺的，见 fillInstanceTree）。
 */
struct InstanceGroupBucket {
    QString groupId;
    QString name;
    /// 这一组在树里**画成展开的还是收着的**（用户 2026-09-25 原话里的「默认展开/关闭」）。
    ///
    /// NOTE: 它的出处是 wtfml.folders 里那一格（InstanceGroupStore::Group::expanded），
    ///       用户点一下组行（或者齿轮菜单里那一项）就翻过来并落盘 —— 于是"下次重灌还
    ///       照这样画"。虚拟的「未分组」没有记录，恒为 true（它收起来这件事不落盘）。
    bool expanded = true;
    QList<VersionDisplayParts> parts;
};

/**
 * @brief 一个树条目代表的实例 id；**组头返回空串**。
 * @param item 树条目；nullptr 也返回空串。
 *
 * 所有读这棵树的地方都必须过这一道，不许直接 `currentItem()->data(0, Qt::UserRole)`。
 * 组头天然能被选中（QTreeWidget::setCurrentItem() 不看 ItemIsSelectable），而组头上
 * 那个 role 是空的 —— 界面于是悄悄变成"没选中任何实例"，用户按启动得到"未选择版本"，
 * 而屏幕上明明有一行亮着。
 *
 * NOTE: 判据是**它有没有组 id 那个 role**，而不是"实例 id 是不是空的"。后者在
 *       "某个实例的 id 恰好是空串"时会误判，而这个判据不该依赖那个前提。
 */
QString instanceIdOf(const QTreeWidgetItem *item) {
    if (item == nullptr) {
        return QString();
    }
    if (item->data(0, MainWindowUI::groupIdRole()).isValid()) {
        return QString();
    }
    return item->data(0, MainWindowUI::instanceIdRole()).toString();
}

/**
 * @brief 按分组把实例灌进实例页那张树。
 * @param tree 目标树。
 * @param buckets 组（含虚拟的「未分组」，见 InstanceGroupBucket）。
 * @param options 全局「版本名格式」开关。
 * @param runningIds 此刻在跑的那些实例 id（显示名后面那句「（运行中）」）。
 * @param marks 每个实例行前面那个小图标（`id -> QIcon`，由 instanceRowMark() 算好）。
 *        缺一个不画图标，不影响别的（验证工程直接摆一张只有几个键的表也能灌）。
 * @param pinnedGroupId **置顶组**是哪一个（InstanceGroupStore::pinnedGroupId()）。空串 =
 *        这份 .minecraft 里没有置顶组 ⇒ 哪一行都不摆那格箭头（点下去没有去处）。
 *        非空时按 @p pinnedInstanceIds 给两个互斥的角色：不在里面 ⇒ `rowPinRole()`
 *        （悬停画 ↑ 置顶）、在里面 ⇒ `rowUnpinRole()`（悬停画 ↓ 取消置顶）。两个角色都由
 *        fillInstanceTree 写，代理/eventFilter 两边读。
 * @param pinnedInstanceIds 此刻**正在置顶**的那些实例 id（`InstanceGroupStore::isPinned()`）。
 *        它和 @p pinnedGroupId 是**两件事**：前者是"哪些实例置着顶"、后者只是"这一份
 *        .minecraft 里有没有置顶组"。有置顶组但一个实例都没置顶时前者是空的、后者非空。
 *
 * NOTE: 【那两个角色为什么按**实例**给，不再按"这一行画在哪一组"给】
 *       2026-09-26 之前置顶 = 把它挪进置顶组，所以"这一行的组 id 就是置顶组"和"这个实例
 *       置着顶"是同一句话，判据写成前者（`bucket.groupId == / != pinnedGroupId`）。
 *       两边都显示之后这两句**不再等值**：置着顶的那个实例在**两个组里各有一行**，两行
 *       说的是同一个实例的同一个状态。按"画在哪一组"给的话老家那一行会画成 ↑（= 置顶），
 *       而它其实已经置着顶了 —— 点下去是"再置一次"（空操作），在用户眼里就是"这颗按了
 *       没用"。所以判据换成**实例自己**置没置顶，两行拿到同一个答案（都画 ↓）。
 *
 * 两种形态，由调用方给进来的 buckets 决定（它才知道有没有真实分组）：
 *   - **只有一格、groupId 为空** → 平铺，不插组头。这是"还没分过组"的样子，
 *     和换树之前那张列表逐项一致。
 *   - 否则每格画一个组头，实例挂在它下面。**一个实例都没有的组不画**（用户 2026-09-25
 *     原话：「空白的分组隐藏」）—— 藏起来的只是那行标题，组本身还在 wtfml.folders 里、
 *     还在「分组设置…」里，也照样能被接收实例（那颗齿轮的第一项列的是**全部**组，
 *     含这些藏着的，见 MainWindowUI::openGroupPicker()）。
 *
 * NOTE: 灌完**不动选中项**（和 fillVersionList 一致），收尾由调用方 syncTreeToCombo()。
 * NOTE: 每个实例条目的 tooltip 是**目录的完整路径**。显示名和目录名可能不一样
 *       （自定义名、拼进去的加载器名、或者用户在资源管理器里改过目录名），而
 *       "这个实例到底在哪个目录"是排查问题时唯一要问的东西。
 */
/// 一个实例在树里那一行该写什么：显示名（后面缀上"（运行中）"当它真的在跑时）。
///
/// **唯一一处**拼这一行 —— 灌树（fillInstanceTree）和游戏起落时那次局部刷新
/// （MainWindow::updateInstanceTreeRunState）都走它。分开写的话，游戏起来那一刻
/// 后缀会跟灌树时那版慢慢漂开。
QString instanceTreeItemText(const VersionDisplayParts &part, const VersionNameOptions &options,
                             bool running) {
    const QString base = formatVersionName(part, options);
    return running ? base + MainWindowUI::instanceRunningSuffixText() : base;
}

void fillInstanceTree(QTreeWidget *tree, const QList<InstanceGroupBucket> &buckets,
                      const VersionNameOptions &options, const QSet<QString> &runningIds,
                      const QHash<QString, QIcon> &marks, const QString &pinnedGroupId,
                      const QSet<QString> &pinnedInstanceIds) {
    const QSignalBlocker blocker(tree);
    tree->clear();

    // 平铺还是插组头，按**调用方给的** buckets 判（它才知道那一份 .minecraft 里有没有
    // 真实分组），不看下面滤掉空组之后剩下什么：分组全空的那一档确实一行都不画，
    // 但那仍然是"分过组的"，形态不该因此翻过来。
    const bool flat = buckets.size() == 1 && buckets.first().groupId.isEmpty();
    for (const InstanceGroupBucket &bucket : buckets) {
        // 一个实例都没有的组**不画**（见函数头）。平铺那一格不参与：那不是一个组。
        if (!flat && bucket.parts.isEmpty()) {
            continue;
        }
        QTreeWidgetItem *parent = nullptr;
        if (!flat) {
            parent = new QTreeWidgetItem(tree);
            parent->setText(0, MainWindowUI::groupItemText(bucket.name, bucket.parts.size()));
            parent->setData(0, MainWindowUI::groupIdRole(), bucket.groupId);
            // 行尾那颗**齿轮**：几何的唯一出处是 InstanceGroupDelegate::gearRect()，
            // 认下那一下的是 MainWindow::eventFilter()。虚拟的「未分组」那一格也挂
            // （它是"把实例放进某个组"的最后一条路，见 MainWindowUI::rowGroupGearRole()）。
            parent->setData(0, MainWindowUI::rowGroupGearRole(), true);
            // 组头**不给选中**：它是"一堆东西的标题"，不是"一个能启动的实例"。
            // 清掉 ItemIsSelectable 之后，选择模型会拒绝把它放进"被选中"的集合，
            // 所以点它**不会亮**（ui_check 的 M 段真的发了一次鼠标事件钉住这条）。
            //
            // NOTE: 但**它仍然会变成 currentItem()** —— QAbstractItemView 在
            //       mousePressEvent 里只查 ItemIsEnabled 就把当前项挪过去了，选择命令
            //       是后一步的事。所以这一行是"别让它高亮"，**不是**"别让它被选中"；
            //       真正兜住"当前选中的是哪个实例"的是 instanceIdOf() 那道守卫，
            //       每一个读 currentItem() 的地方都绕不过去。
            parent->setFlags(parent->flags() & ~Qt::ItemIsSelectable);
            // 展开还是收着**由那一组自己记着**（bucket.expanded ← wtfml.folders 里那一格）：
            // 用户点一下组行就翻过来并落盘，于是"下次打开还照这样画"。默认那五个组都是
            // 展开的（新组的默认值也一样）—— 折起来的组里有什么，用户得点一下才知道。
            parent->setExpanded(bucket.expanded);
        }
        // 平铺时挂在**不可见的根**下面，那和"顶层条目"是一回事，而且省掉了
        // "parent 是空的就换一个构造函数"那条分叉（两个构造函数收的类型不一样）。
        QTreeWidgetItem *holder = parent != nullptr ? parent : tree->invisibleRootItem();
        for (const VersionDisplayParts &part : bucket.parts) {
            auto *item = new QTreeWidgetItem(holder);
            item->setText(0, instanceTreeItemText(part, options, runningIds.contains(part.id)));
            item->setData(0, MainWindowUI::instanceIdRole(), part.id);
            item->setToolTip(0, QDir::toNativeSeparators(AppPaths::versionDir(part.id)));
            // 行前面那个小图标（原版草方块 / Forge 铁砧…）。谁画的、怎么认出来的
            // 见 instanceRowMark() 那段 —— 这一层只负责把它挂上去。
            const auto mark = marks.constFind(part.id);
            if (mark != marks.constEnd()) {
                item->setIcon(0, mark.value());
            }
            // 中间那一格（**一对反义词共用的一格**，见 MainWindowUI::rowUnpinRole() 那段）
            // 的判据：画它的是 InstanceGroupDelegate::paint()、认下那一下的是
            // MainWindow::eventFilter()，三处读的都是**这两个角色**。
            //   - `rowPinRole()` = 还没置顶 ⇒ 悬停画 ↑「置顶」；
            //   - `rowUnpinRole()` = 已经置顶了 ⇒ 悬停画 ↓「取消置顶」（用户 2026-09-25
            //     原话：「置顶组里的实例悬停时右侧显示下箭头，即取消置顶」）；
            //   - 两个都假 = **这一份 .minecraft 里没有置顶组**（pinnedGroupId 是空串：
            //     那一格点下去没有去处，见 InstanceGroupStore::pinnedGroupId() 那三条出口）
            //     ⇒ 这一格空着、也不弹话。
            // 两个判据互斥（同一个 pinned，一正一反），所以这一格永远只有一个方向。
            //
            // NOTE: 判据是**这个实例**置没置顶（见函数头那条 NOTE）—— 所以置着顶的那个实例
            //       在老家和置顶组里的**那两行画的是同一颗 ↓**，不是一个 ↑ 一个 ↓。
            // NOTE: "已经在置顶组里 ⇒ 不画上箭头"这条老规矩换个说法活着 —— 它仍然是"不画 ↑"，
            //       只是改成画 ↓ 了（原来那档什么都不画：挪不动的不画，这条先例在
            //       ui/GroupListDelegate 里）。
            const bool pinned = pinnedInstanceIds.contains(part.id);
            item->setData(0, MainWindowUI::rowPinRole(),
                          !pinnedGroupId.isEmpty() && !pinned);
            item->setData(0, MainWindowUI::rowUnpinRole(),
                          !pinnedGroupId.isEmpty() && pinned);
        }
    }

    // NOTE: 这里原来还有一段收尾：给**画出来的**组行写一格位掩码，说明"行尾那两颗上/下
    //       箭头该画哪一颗"。2026-09-25 用户把两颗箭头整个搬进了「分组设置…」那张表
    //       （原话：「这个箭头不应该在这里，而应该在 分组设置 里面，这里的删了，去分组设置
    //       里面加上，设置那个框下方的上移下移的按钮就删了」），于是这一段连同
    //       `paintedGroupRows` 整个没了 —— 组行现在**只有两个动作**：点这一行=展开/收起、
    //       点齿轮=那一组的更多操作。那张表里"能不能挪"是算出来的（行序就是组序），
    //       不需要角色位，见 ui/GroupListDelegate。
}

/**
 * @brief 让实例树里的选中项跟上下拉框（两边都按 id 找，不按文本）。
 * @param combo 版本下拉框。
 * @param tree 实例页那张树。
 *
 * 找不到那个 id（清单刚刷新、实例被删了）时**清空选中**，和列表版一样。
 */
void syncTreeToCombo(const QComboBox *combo, QTreeWidget *tree) {
    const QString id = selectedVersionId(combo);
    // 挡信号：这里是"跟着别人变"，不该反过来再把下拉框动一遍。真正的用户操作才发信号。
    const QSignalBlocker blocker(tree);
    if (!id.isEmpty()) {
        QTreeWidgetItemIterator it(tree);
        while (*it != nullptr) {
            if (instanceIdOf(*it) == id) {
                tree->setCurrentItem(*it);
                return;
            }
            ++it;
        }
    }
    // 没找到（或者下拉框本来就没选）：留空，别硬选一个别的。
    tree->setCurrentItem(nullptr);
}

/**
 * @brief 让下拉框跟上实例树里选中的那一项。
 * @param combo 版本下拉框。
 * @param tree 实例页那张树。
 *
 * NOTE: 选中的是**组头**时什么都不做 —— 点一个组的标题不是"挑一个版本"。守卫走
 *       instanceIdOf()，理由见它的说明。
 */
void syncComboToTree(QComboBox *combo, const QTreeWidget *tree) {
    const QString id = instanceIdOf(tree->currentItem());
    if (id.isEmpty()) {
        return;
    }
    const int index = combo->findData(id);
    if (index >= 0 && combo->currentIndex() != index) {
        combo->setCurrentIndex(index);
    }
}

// 「一个组」的两份表示之间的转换。看着啰嗦，但它是 ui/ 和业务层解耦的**全部代价**：
// ui/ 一直不 include src/（ui_check 那个工程只编 ui/MainWindowUI.cpp 就能跑），所以
// 分组对话框有自己的 Group、存储层有自己的 Group，两边逐字段相同。把它们合成一个
// 类型就得让 ui/ 依赖业务层，或者反过来 —— 相比之下这两段循环是最便宜的。

QList<InstanceGroupStore::Group> toStoreGroups(const QList<GroupDialog::Group> &from) {
    QList<InstanceGroupStore::Group> to;
    to.reserve(from.size());
    for (const GroupDialog::Group &group : from) {
        InstanceGroupStore::Group item;
        item.id = group.id;
        item.name = group.name;
        // 「默认展开」也搬过来：它在两边是同一个字段（不是"界面上的显示状态"），
        // 丢了的话在对话框里摆好的展开状态按一次确定就被重置回默认。
        item.expanded = group.expanded;
        // 「设为置顶组」同样要搬（2026-09-25）。这一格漏掉的表现很具体：用户在对话框里
        // 勾了置顶组、按确定，实例行行尾那颗上箭头**还是往老的那一组送** —— 勾选框像是
        // 没生效，而两份清单比出来"没变"的话连盘都不写（见 Group::operator==）。
        item.pinned = group.pinned;
        to.append(item);
    }
    return to;
}

QList<GroupDialog::Group> toDialogGroups(const QList<InstanceGroupStore::Group> &from) {
    QList<GroupDialog::Group> to;
    to.reserve(from.size());
    for (const InstanceGroupStore::Group &group : from) {
        GroupDialog::Group item;
        item.id = group.id;
        item.name = group.name;
        item.expanded = group.expanded;  // 见 toStoreGroups() 那段
        item.pinned = group.pinned;      // 见 toStoreGroups() 那段
        to.append(item);
    }
    return to;
}

// 「一个实例的启动五项覆盖」的两份表示之间的转换。理由和上面那两段一样：ui/ 不 include
// src/，所以 InstanceSettingsDialog 有自己的 Override、settings/ 有自己的
// JavaSettingsOverride，逐字段相同、由这两段循环在两边搬。

/**
 * @brief 组齿轮菜单（以及它里面"移入分组…"弹出的挑目标框）那一刻的那份**快照**。
 * @param ui 主界面（组行都长在它的实例树里）。
 * @param store 分组存储（组的清单、顺序、展开状态从这儿来）。
 * @param selectedKey 此刻选中的那个实例的归属键（instanceKey()）；空串 = 没选中。
 * @param selectedHomeGroupId 那个实例的**老家**在哪一组（`homeGroupIdOf()`）；空串 = 未分组
 *        （也含"没选中"那一档，见下面的 NOTE）。挑目标那个框里被灰掉、写着「已经在这一组
 *        里了」的就是它。
 *
 * NOTE: 实例数是在**当前那张树**上数的（它就是用户此刻看到的东西）：藏起来的组不在树上
 *       ⇒ 它是 0 —— 那正好也是"这一组里有几个实例"在那一刻的实话。置顶着的那一条在两个
 *       组里各画一行，所以那两个数里各算它一次 —— 这正是"那一组里有几行"的实话。
 *
 * NOTE: 【为什么老家由调用方给，不在这张树上现看】2026-09-26 之前看的是"选中的那一条画在
 *       哪个组下面"，因为那时候一行只画在一个组里、那个组就是它的归属。两边都显示之后
 *       同一个实例有**两行**（老家一行 + 置顶组里一行），"画在哪一组"取决于用户点的是
 *       哪一行 —— 同一个实例会给出两个不同的答案：点老家那行时「置顶」那格亮着，点置顶
 *       组那行时又变成老家那格亮着。而这一格灰掉的意思是"已经在这一组里了、点它没用"，
 *       跟"你刚好点的是哪一行"没关系。所以判据换成老家 —— 它是实例自己的属性，两行拿到
 *       同一个答案。老家那个值 `homeGroupIdOf()` 只有 MainWindow 算得出来（要 VersionManager
 *       和版本类型），这个自由函数手上只有 ui/ 和 store，所以由调用方递进来。
 *
 * NOTE: 它是个自由函数（不是 MainWindow 的成员），因为 MainWindowUI::GroupRow 是 ui/ 里的
 *       嵌套类型，而 MainWindow.hpp **不 include ui/ 的头**（见那个文件头那几条前置声明）。
 *       摆在这儿的那几个私有成员函数只收窄（组 id / 一整行）就不必把它写进头文件。
 */
QList<MainWindowUI::GroupRow> groupRowsForMenu(const MainWindowUI *ui,
                                               const InstanceGroupStore *store,
                                               const QString &selectedKey,
                                               const QString &selectedHomeGroupId) {
    QHash<QString, int> counts;
    const QTreeWidget *tree = ui->getInstanceList();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *top = tree->topLevelItem(i);
        const QVariant groupId = top->data(0, MainWindowUI::groupIdRole());
        if (!groupId.isValid()) {
            // 平铺那一档（一个真实分组都没有时）：顶层条目**就是**实例本身，没有组头
            // 可数 —— 那一档连「未分组」那格都不摆（见下面 `!groups.isEmpty()` 那道闸），
            // 所以这儿一个数都不记，谁也不会来问。
            continue;
        }
        counts.insert(groupId.toString(), top->childCount());
    }

    const bool hasSelection = !selectedKey.isEmpty();
    QList<MainWindowUI::GroupRow> rows;
    const QList<InstanceGroupStore::Group> groups = store->groups();
    rows.reserve(groups.size() + 1);
    for (const InstanceGroupStore::Group &group : groups) {
        MainWindowUI::GroupRow row;
        row.groupId = group.id;
        row.name = group.name;
        row.memberCount = counts.value(group.id, 0);
        row.expanded = group.expanded;
        row.hasSelection = hasSelection;
        row.containsSelected = hasSelection && selectedHomeGroupId == group.id;
        rows.append(row);
    }
    // 虚拟的「未分组」排在最后（和树上同一个位置）。**只有真的分过组才摆它** ——
    // 一个真实分组都没有时列表是平铺的，那颗齿轮压根没地方挂，这张表也就没人看得见。
    if (!groups.isEmpty()) {
        MainWindowUI::GroupRow rest;
        rest.name = MainWindowUI::ungroupedGroupText();
        rest.memberCount = counts.value(QString(), 0);
        // 它没有记录（那一格不落盘），树上每次都是展开的 —— 快照照实说。
        rest.expanded = true;
        rest.hasSelection = hasSelection;
        rest.containsSelected = hasSelection && selectedHomeGroupId.isEmpty();
        rest.virtualGroup = true;
        rows.append(rest);
    }
    return rows;
}

/**
 * @brief 某一组此刻的那份快照（groupRowsForMenu() 里对应的那一条）。
 * @param row 拿到的那一行。@param selectedKey / @param selectedHomeGroupId 见
 *        groupRowsForMenu()（两个都缺省 = 没选中实例）。
 * @return 这一组不在（菜单开着的时候被别处删了）时返回 false，@p row 原样不动。
 *
 * NOTE: 虚拟的「未分组」的组 id 就是空串，所以传空串进来拿到的是**它**，不是"没找到"。
 *       两个调用点要的都是"这一刻的样子"，所以它每次都重新翻一遍快照，不复用菜单里
 *       那一份 —— 菜单摆出来之后用户可能看两眼才点，这中间别处一行代码就能把它删了。
 */
bool groupRowFor(const MainWindowUI *ui, const InstanceGroupStore *store, const QString &groupId,
                 MainWindowUI::GroupRow *row, const QString &selectedKey = QString(),
                 const QString &selectedHomeGroupId = QString()) {
    const QList<MainWindowUI::GroupRow> rows =
        groupRowsForMenu(ui, store, selectedKey, selectedHomeGroupId);
    for (const MainWindowUI::GroupRow &item : rows) {
        if (item.groupId == groupId) {
            if (row != nullptr) {
                *row = item;
            }
            return true;
        }
    }
    return false;
}

/**
 * @brief 某一组那一行行尾**齿轮**占的那一格，全局坐标（弹挑目标框时的锚点）。
 * @return 那一行不在树上（一个实例都没有的组是藏着的）时返回空矩形。
 *
 * 几何的唯一出处是 InstanceGroupDelegate::gearRect() —— 和代理画它、eventFilter 认下
 * 那一下用的是同一个式子（写歪了的症状就是"看着点中了齿轮、其实点的是那一行"）。
 */
QRect gearAnchorFor(const MainWindowUI *ui, const QString &groupId) {
    QTreeWidget *tree = ui->getInstanceList();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        if (item->data(0, MainWindowUI::groupIdRole()).toString() != groupId
            || !item->data(0, MainWindowUI::rowGroupGearRole()).toBool()) {
            continue;
        }
        const QRect gear = InstanceGroupDelegate::gearRect(tree->visualItemRect(item));
        return QRect(tree->viewport()->mapToGlobal(gear.topLeft()), gear.size());
    }
    return QRect();
}

JavaSettingsOverride toSettingsOverride(const InstanceSettingsDialog::Override &from) {
    JavaSettingsOverride to;
    to.javaPath = from.javaPath;
    to.xmx = from.xmx;
    to.xms = from.xms;
    to.autoMemory = from.autoMemory;
    to.extraJvmArgs = from.extraJvmArgs;
    return to;
}

InstanceSettingsDialog::Override toDialogOverride(const JavaSettingsOverride &from) {
    InstanceSettingsDialog::Override to;
    to.javaPath = from.javaPath;
    to.xmx = from.xmx;
    to.xms = from.xms;
    to.autoMemory = from.autoMemory;
    to.extraJvmArgs = from.extraJvmArgs;
    return to;
}

InstanceSettingsDialog::GlobalValues toDialogGlobal(const EffectiveJavaSettings &from) {
    InstanceSettingsDialog::GlobalValues to;
    to.javaPath = from.javaPath;
    to.xmx = from.xmx;
    to.xms = from.xms;
    to.autoMemory = from.autoMemory;
    to.extraJvmArgs = from.extraJvmArgs;
    return to;
}

/**
 * @brief 按 itemData 里的枚举值选中一项。
 * @return 找到并选中了返回 true；没有这一项返回 false（调用方决定要不要兜底）。
 *
 * NOTE: 枚举以 int 存进 itemData。界面这边只认"存进去的那个值"，不去 include
 *       src/settings 里的枚举 —— 界面层不该为了显示一个下拉框去依赖设置模块。
 */
template <typename Enum> bool selectByEnumData(QComboBox *combo, Enum value) {
    const int index = combo->findData(static_cast<int>(value));
    if (index < 0) {
        return false;
    }
    combo->setCurrentIndex(index);
    return true;
}

/**
 * @brief 往「Java 版本」下拉框里加一条运行时。
 * @param combo 目标下拉框。
 * @param majorVersion 主版本号；<= 0 表示不知道（钉死的那个路径就不一定知道）。
 * @param path java 可执行文件的完整路径。
 * @return 这条（新加的或本来就有的）条目的下标。
 *
 * NOTE: itemData 存的是**路径**，不是下标。这个下拉框里的条目会随扫描结果变多，
 *       拿下标当身份标识会在下次扫描后指到另一个 Java 上。
 *       空路径是保留给「自动」那一条的（它没有路径），所以这里拒绝空路径。
 */
int addJavaRuntimeItem(QComboBox *combo, int majorVersion, const QString &path) {
    if (path.isEmpty()) {
        return -1;
    }
    const int existing = combo->findData(path);
    if (existing >= 0) {
        return existing;
    }
    // 版本号不知道时只显示路径（钉死的那个路径不在扫描结果里就是这样）
    const QString label = majorVersion > 0
                              ? QStringLiteral("Java %1 — %2").arg(majorVersion).arg(path)
                              : path;
    combo->addItem(label, path);
    return combo->count() - 1;
}

/**
 * @brief 按当前设定把「Java 版本」下拉框的选中项摆正（调用方负责挡信号）。
 * @param combo 目标下拉框。
 * @param autoSelect 设定是不是"自动"。
 * @param path 设定的路径。
 *
 * 自动档选那一条 itemData 为**空串**的条目（只有它一条是空的）。
 * NOTE: 「手动档但路径是空的」这种配置只可能来自手改注册表（界面这条路上手动档
 *       一定带着一个真实路径）—— 那种情况下这里会落到「自动」那一条，而
 *       updateJavaPathLabel() 会照实说明它其实不会自动换 Java。不替用户改数据。
 */
void selectConfiguredJava(QComboBox *combo, bool autoSelect, const QString &path) {
    const QString wanted = autoSelect ? QString() : path;
    const int index = combo->findData(wanted);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

// ---------------------------------------------------------------------------
// 底栏那两块上拉面板（左：正在跑的实例 · 右：最近用过的账号）
// ---------------------------------------------------------------------------

/// 左面板行尾那两颗按钮的**编号**（onBottomBarPanelAction() 认的就是这两个数）。
///
/// NOTE: 编号是"这一行第几颗"，不是"这颗按钮上画着什么" —— 拿图形反认的话，哪天把电源
///       换成终止、或者两颗换个位置，那一下点下去会静悄悄地做错事（关实例变成开设置）。
constexpr int kRunningPanelStopAction = 0;
constexpr int kRunningPanelSettingsAction = 1;

/// 左面板里一行的那两颗按钮（电源 = 关掉这份会话，齿轮 = 进这个实例的设置）。
/// @param ink 图标那一档颜色（和底栏那几行字同一个墨色 —— 画的图标不带自己的颜色）。
QList<MainWindowUI::PanelAction> runningPanelActions(const QColor &ink) {
    const int iconSize = MainWindowUI::bottomBarPanelActionIconSize();
    MainWindowUI::PanelAction stop;
    stop.icon = VersionIcons::power(iconSize, ink);
    // 图标按钮**必须**有 tooltip：一个电源符号说明不了"点了会把游戏直接杀掉、不会存档"。
    stop.tooltip = QObject::tr("关闭这个实例（直接结束游戏进程，游戏不会存档）");
    MainWindowUI::PanelAction settings;
    settings.icon = VersionIcons::gear(iconSize, ink);
    settings.tooltip = QObject::tr("打开这个实例的设置");
    return {stop, settings};
}

/// 这个实例属于哪个分类（正式版 / 快照版 / 远古版 / 愚人节版）。
///
/// 单一出处是 VersionCategory::channelFor()（和下载页那棵树**同一个出处**，别在这儿另写
/// 一个 switch：写歪了的症状是"快照那一栏前面白草、远古那一栏还是绿草"，不报错、只是难看）。
/// 那条要一个 `type` 字段，两个地方能给：
///   - wtfml.meta 里手写的 `type`（**优先**：用户明确写下的东西盖过一切，他也可能是把
///     一个实例"装作"另一个分类）；
///   - 没有就回实例自己的 `versions/<id>/<id>.json` 里读 —— 那才是这个字段的出处。
/// 两个都读不到（meta 丢了、json 也被删了）就当**正式版**：大多数实例是正式版，而
/// "一个方块都不画"在界面上是个说不清的空白。
///
/// 【2026-09-25：第二段是补上的，在那之前这里是个真 bug】
/// `type` 原来**只从 meta 读，而全项目没有任何一处往 meta 里写过它**（写侧那半从来没做
/// 出来）。于是这个函数对每一个已装实例都退化成"正式版"，看得见的后果有三处：快照版实例
/// 自动归类跑进「原版」组、默认组「快照版 / 远古版」永远是空的、实例页那一行的图标
/// （见 instanceRowMark()）全是草方块。用户这一轮要的正是"给每个版本一个对的图标"，
/// 而底下的因是这一条。
///
/// 修法选的是**回 json 读**，不是"装的时候往 meta 里抄一份 type"：
///   - json 本来就是这个字段的出处。抄一份出去就有两个可能不一致的地方（用户在资源管理器
///     里换掉一份 json、或者把同一个版本号重装一遍，meta 里那份就过期了，而且看不出过期）；
///   - `getVersionJson()` 给的是**合并过**的那一份（顺 inheritsFrom 链），所以整合包实例
///     的 type 是从它继承的原版那一层来的 —— 那正是它该显示的分类。而装的时候手里只有
///     `pack.gameVersion` 这个版本号，想写 type 反而要再回清单里查一次。
/// 代价是每个"meta 里没写 type 的"实例第一次问它时要读+解析一次那份 json；结果在
/// VersionManager 里带缓存（mergedVersionJsons），所以一次会话里每个实例只读一次。
VersionCategory::Channel versionChannelOf(const VersionManager *versions, const QString &versionId) {
    QString type = versions == nullptr
                       ? QString()
                       : versions->versionMeta(versionId).value(QStringLiteral("type")).toString();
    // 先看一眼那份 json 在不在：不在就别问 getVersionJson()（它会 qWarning，而这个函数
    // 每次刷新实例列表都要对每个实例跑一遍，一句"版本 JSON 找不到"会把日志刷满）。
    //
    // NOTE: 这道闸只管"文件不在"那一种 —— **解析不了的**（用户手里是一份坏 json）照样会
    //       抱怨一句"Failed to parse ..."，而且每次刷新都会再说一遍（解析失败那条路不进
    //       缓存，见 VersionManager::getVersionJson）。这是**故意留着**的：那份 json 坏掉
    //       的实例本来就启动不了，日志里说一句比闷着强，而且它跟"缺 jar""缺 java"是同一档
    //       频率（一次刷新一句，不是每个像素一句）。见 mw_check 的 J 节 —— 那一节的样本
    //       文件因此必须是**能解析**的 json。
    if (type.isEmpty() && versions != nullptr
        && QFile::exists(InstanceFiles::jsonPath(versionId))) {
        type = versions->getVersionJson(versionId).value(QStringLiteral("type")).toString();
    }
    if (type.isEmpty()) {
        type = QStringLiteral("release");
    }
    return VersionCategory::channelFor(versionId, type);
}

/// 这个实例该画哪个分类的方块（草方块 / 覆雪草块 / 土块 / 幸运方块）。
int versionChannelIndexOf(const VersionManager *versions, const QString &versionId) {
    return VersionCategory::channelIndex(versionChannelOf(versions, versionId));
}

/// 这个实例**实际**基于哪个加载器 —— 给界面看的名字（空串 = 没认出来）。
/// @param versionOut 认出来时顺手带上加载器版本（只有"加载器对不上"那道闸要用它）。
///
/// 先看 @p parts 里**已经读过**的那一格（`displayParts()` 顺手带出来的，不额外读盘，
/// 那也是"显示名和图标是同一次读取的两半"那条 NOTE 的意思），为空时再回扣一次
/// `VersionManager::instanceModLoader()`。
///
/// ★ 为什么要回扣：`wtfml.meta` 里的 `loaderName` **只有我们自己的安装器会写**。PCL /
///   HMCL 那类把整份 json 合进实例目录的实例（用户盘上 70 个里 67 个）那一格是空的 ——
///   回扣那一趟照着版本 json 的 `libraries` 认，才是它们的加载器唯一的出处。四条路都读
///   同一个名字：实例行那个标志（defaultInstanceMark）、自动归类（defaultGroupIdFor）、
///   光影包那一页（instanceShaderUsable）、以及装模组时的"加载器对不上"那道闸。
QString instanceLoaderName(const VersionDisplayParts &parts, const VersionManager *versions,
                           QString *versionOut = nullptr) {
    if (versionOut != nullptr) {
        versionOut->clear();
    }
    if (!parts.loaderName.isEmpty()) {
        if (versionOut != nullptr) {
            *versionOut = parts.loaderVersion;
        }
        return parts.loaderName;
    }
    QString name;
    QString version;
    if (versions != nullptr && versions->instanceModLoader(parts.id, &name, &version)) {
        if (versionOut != nullptr) {
            *versionOut = version;
        }
        return name;
    }
    return QString();
}

/// 加载器名字 → **分组用的那个小写键**（`VersionGroup::loader` 的写法）。
///
/// ★ 两侧的词形**不是一套**，这是本仓库里最容易静默失灵的一处：`instanceLoaderName()`
///   回的是显示名（「Fabric」/「Forge」/「NeoForge」/「Quilt」/「OptiFine」），而
///   `ResourceCatalog::groupVersions()` 的 `preferredLoader` 与 `VersionGroup::loader`
///   收的都是小写那一套（「fabric」/「neoforge」…）。直接把显示名递进去 ⇒ 一个字都比不上
///   ⇒「把对应加载器排在前面」这件事**永久不生效，而屏幕上一点异常都没有**。
///
/// 认不出来（空串 / 别的加载器）回空串 = "没人在乎"，`groupVersions()` 就照旧排。
QString loaderGroupKey(const QString &loaderName) {
    return loaderName.trimmed().toLower();
}

/// 这个实例**没挑过图标**时该画哪一个方块（草方块 / 覆雪草块 / 土块 / 幸运方块，或者
/// 某个加载器的标志）—— 也叫它的「默认图标」。
///
/// 两条路，用的都是**另外两棵树上挂的同一份图标**（别在这儿另画一套）：
///   - **装了加载器的**（见 instanceLoaderName()：meta 里记着的，或者版本 json 自己说的）：
///     给那个加载器的标志（Fabric / Forge / NeoForge / OptiFine），走
///     `VersionIcons::loaderMark()` —— 安装页那棵加载器树上挂的就是它；
///   - **没装加载器的**（原版 / 快照版 / 远古版 / 愚人节版）：按版本分类给，走
///     `VersionIcons::versionChannelMark()` —— 下载页那棵版本树上挂的就是它。
///
/// 于是"下载时就带"这件事**不需要任何落盘**：图标是从实例自己那份 json 的 `type` 现算的
/// （见 versionChannelOf()），而那份 json 是下载的时候写下去的 —— 装完第一次刷新就有图标，
/// 老实例（一个 type 都没写过）也一样算得出来，不用回填任何东西。
///
/// NOTE: 加载器名 → 下标那一步用 `loaderKindFromIndex()` 反查，**不自己写一张"Forge 就是 1"
///       的表**：那张表的唯一出处是 MainWindowUI::modLoaderOptionNames()（图标那边的下标
///       就是照它排的），抄一遍就会在某次加加载器时慢慢漂开。
/// NOTE: 认不出来的加载器名（用户手写的别的名字、或者以后新加的加载器）也是草方块 ——
///       和 loaderMark() 越界那条一个规矩：认不出来就画原版。
///       **Quilt 就是这一档**：`instanceLoaderName()` 认得出来（版本 json 里
///       `org.quiltmc:quilt-loader` 那条），而 MainWindowUI 那四颗加载器钮里没有它 ⇒ 画草
///       方块。这是老行为（我们自己的安装器不装 Quilt，这一档只从 json 那一路进得来），
///       要给它一个标志就加一颗加载器钮 —— 别在这儿另画一张图。
QPixmap defaultInstanceMark(const VersionDisplayParts &part, const VersionManager *versions,
                            int size) {
    const QString loader = instanceLoaderName(part, versions);
    if (!loader.isEmpty()) {
        for (int index = 0; index < MainWindowUI::modLoaderNoneIndex(); ++index) {
            VersionManager::LoaderKind kind;
            if (VersionManager::loaderKindFromIndex(index, &kind)
                && VersionManager::loaderDisplayName(kind) == loader) {
                return VersionIcons::loaderMark(index, size);
            }
        }
        return VersionIcons::grassBlock(size);
    }
    return VersionIcons::versionChannelMark(versionChannelIndexOf(versions, part.id), size);
}

/// 实例页某一行前面那个小图标（用户 2026-09-25 原话：「为每个版本预设一个图标（默认下载时
/// 就带，原版草块，forge铁砧等，现有的没有就补上）」；2026-09-26 加上了"用户可以自己换一张"，
/// 那一条在「实例设置 → 概览 → 图标」里）。
///
/// **挑过的压过没挑过的**：wtfml.meta 里有 icon 就按它走，没有才落到上面那个默认图标。
/// 三种形态的判据与写法见 VersionManager::iconField()（那是唯一出处，这里不再抄一遍）。
///
/// NOTE: 收 @p size 而不是自己去问两个 size 常量里的哪一个 —— 同一个实例的图标在实例树上和
///       底栏「正在运行」那一行里**是同一张图**，只是边长不同（那两处各有各的常量）。
///       让调用方递边长，这里就只需要一处判断。
/// NOTE: `builtin:` 那个前缀认不出来（用户手写了个 `builtin:草方块`）时**落到默认**而不是
///       画一张空图 —— 和那几处"认不出来就当原版"同一个态度：屏幕上宁可显示一个不对的
///       方块，也不要一个空洞。
/// NOTE: 本地那张走 VersionIcons::fileMark()（按路径 + 修改时间缓存）。**不在这儿直接
///       QPixmap(path)**：这个函数实例树每一行都要问一次，而每刷新一次整棵树重建一遍，
///       用户挑的那张 1024×1024 的图会被解上成百遍。
QPixmap instanceRowMark(const VersionDisplayParts &part, const VersionManager *versions, int size) {
    const QString iconKey = VersionIcons::iconKeyFromValue(part.icon);
    if (!iconKey.isEmpty()) {
        const QPixmap mark = VersionIcons::iconMarkForKey(iconKey, size);
        if (!mark.isNull()) {
            return mark;
        }
    } else if (part.icon == InstanceSettingsDialog::localIconValue()) {
        // 「本地那张」= 实例目录下那份 icon.png（名字的唯一出处是 AppPaths::versionIconPath）。
        const QPixmap mark = VersionIcons::fileMark(AppPaths::versionIconPath(part.id), size);
        if (!mark.isNull()) {
            return mark;
        }
    }
    return defaultInstanceMark(part, versions, size);
}

/// 这一份 .minecraft 里**现在真的还在**的那些组 id。默认组被用户删掉之后它就不在里面了，
/// 于是一整组实例自动落到「未分组」—— 这正是用户要的「组被删掉后版本放到未分组里」。
QSet<QString> knownGroupIdsOf(const InstanceGroupStore *groups) {
    QSet<QString> known;
    for (const InstanceGroupStore::Group &group : groups->groups()) {
        known.insert(group.id);
    }
    return known;
}

/// 把用户挑的那张图落成实例目录下的 icon.png（用户 2026-09-26 原话：「选一张本地图片文件」）。
///
/// **解码再编码，不是原字节复制**：用户挑的可能是 jpg / webp / bmp，而读的那一侧只认
/// icon.png 这一个名字（见 AppPaths::versionIconPath()）。原字节拷过去的话，盘上就有一个
/// 扩展名写着 .png、内容却是 JPEG 的文件 —— 今天 Qt 按内容认、照样解得开，但那个名字从此
/// 是一句假话，谁照着它去写解析都会踩空。
///
/// NOTE: 走 QSaveFile（写临时文件 → 原子替换）：写到一半失败时盘上留着的要么是原来那张、
///       要么是新那张，不会是一张半截的图 —— 那东西下次启动会被当成"这张图坏了"，而用户
///       完全不知道自己哪里做错了。
///
/// @return 写成功了返回 true。失败时填 @p errorOut，调用方**必须把它说出来**：用户刚挑完
///         图、按了确定，图标没换上却一声不响，正是最难查的那种"没生效"。
bool writeInstanceIcon(const QString &versionId, const QString &sourcePath, QString *errorOut) {
    const QImage source(sourcePath);
    if (source.isNull()) {
        if (errorOut != nullptr) {
            *errorOut = QObject::tr("读不出这张图：%1\n\n"
                                    "（它可能不是图片格式，也可能已经不在那儿了。）")
                            .arg(QDir::toNativeSeparators(sourcePath));
        }
        return false;
    }
    QSaveFile file(AppPaths::versionIconPath(versionId));
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorOut != nullptr) {
            *errorOut = QObject::tr("图标写不进去：%1").arg(file.errorString());
        }
        return false;
    }
    // QSaveFile 的 commit() 失败时自己会把临时文件撤掉，所以这两句之间不用再清理一遍。
    if (!source.save(&file, "PNG") || !file.commit()) {
        if (errorOut != nullptr) {
            *errorOut = QObject::tr("图标没能放到 %1：%2")
                            .arg(QDir::toNativeSeparators(AppPaths::versionIconPath(versionId)),
                                 file.errorString());
        }
        return false;
    }
    return true;
}

/// 一个实例**默认**该归到哪个默认组 —— 只给"用户还从来没给它表过态"的实例用（见调用点）。
///
/// 判据就是用户 2026-09-24 定的那五个组名：
///   - 装了模组加载器 → 可安装mod（判据见 instanceLoaderName()：**不只** wtfml.meta 那一格，
///     版本 json 自己说的也算 —— PCL 那类合并实例 meta 里是空的，只按 meta 判会把 67 个
///     装了加载器的实例全扔进「原版」，而「可安装mod」永远是空的）；
///   - 否则按版本分类：正式版 → 原版、快照版 → 快照版、远古版 → 远古版；
///   - **愚人节版没有对应的默认组**，返回空串 = 未分组。用户给的那五个里没有它，而把
///     它塞进快照版是替用户做主（那九个版本的 type 全是 snapshot，正是这个"顺理成章"
///     的错法，见 VersionCategory.hpp 文件头）。
///
/// @param known 这一份 .minecraft 里**现在真的还在**的那些组 id。默认组被用户删掉之后
///        它就不在 known 里了，于是一整组实例自动落到「未分组」—— 这正是用户要的
///        「组被删掉后版本放到未分组里」，为它写任何清理代码都是多余的。
QString defaultGroupIdFor(const VersionDisplayParts &part, const VersionManager *versions,
                          const QSet<QString> &known) {
    const auto existing = [&known](InstanceGroupStore::DefaultKind kind) {
        const QString id = InstanceGroupStore::defaultGroupId(kind);
        return known.contains(id) ? id : QString();
    };
    // 加载器那一条先判：装了加载器的快照版/远古版也算"可安装mod"（那才是它现在的样子）。
    if (!instanceLoaderName(part, versions).isEmpty()) {
        return existing(InstanceGroupStore::DefaultKind::Moddable);
    }
    switch (versionChannelOf(versions, part.id)) {
    case VersionCategory::Channel::Release:
        return existing(InstanceGroupStore::DefaultKind::Vanilla);
    case VersionCategory::Channel::Snapshot:
        return existing(InstanceGroupStore::DefaultKind::Snapshot);
    case VersionCategory::Channel::Old:
        return existing(InstanceGroupStore::DefaultKind::Old);
    case VersionCategory::Channel::AprilFools:
        break;
    }
    return QString();
}

/// 一个实例**显示在**哪一组里 —— 也叫它的「归属」，树上摆在哪一组就是它。
///
/// 和 `InstanceGroupStore::groupIdOf()`（**存的是**哪一组）不是一回事：从来没表过态的实例
/// 由显示层按版本分类自动归类（defaultGroupIdFor）。凡是"要说清这个实例现在归在哪"的地方
/// 都得走这一个函数 —— 设置对话框那一格和实例树各说各的话，用户就会看到"对话框里写着未
/// 分组、而树上明明把它摆在「原版」里"。
///
/// NOTE: 判据是"文件里有没有这一条"（isAssigned）、**不是"groupId 是不是空"**：用户在对话框
///       里明确选了「未分组」的实例也算表过态（存的是那个保留值，读出来是空串），不该被自动
///       归类又拉回默认组去 —— 否则就是"点了确定没生效"。
QString displayGroupIdOf(const InstanceGroupStore *groups, const QString &key,
                         const VersionDisplayParts &part, const VersionManager *versions,
                         const QSet<QString> &knownGroupIds) {
    QString groupId = groups->groupIdOf(key);
    if (groupId.isEmpty() && !groups->isAssigned(key)) {
        groupId = defaultGroupIdFor(part, versions, knownGroupIds);
    }
    return groupId;
}

/// 一个实例的「老家」—— 它**本来**属于哪一组（置顶组里那一条同名的只是多出来的）。
///
/// 2026-09-26 用户把置顶改成**两边都显示**之前，这件事根本不需要回答：那时候"归属"只有
/// 一格（`groupIdOf()`），置顶就是把那一格改成置顶组。现在置顶着的那一条在盘上是
/// "members 指着置顶组 + pinOrigin 记着老家"，于是"它本来在哪一组"成了**显示层要问的问题**
/// （树上那一条留在老家、实例设置里「分组…」那一格显示的也是它）。
///
/// 判据就是 `pinOriginOf()` 那三种形态，**逐条**对应 `unpinInstance()` 的落点 —— 两处必须
/// 看到同一个答案，否则"取消置顶之后它跳去了一个和对话框里写着的不一样的组"。所以：
///   - 没在置顶 ⇒ 就是 displayGroupIdOf()（和以前一样，一个字没变）；
///   - 在置顶 + 记着一个组 ⇒ 那个组（**组已经没了就按"没有记录"处理**，见下）；
///   - 在置顶 + 记的是 null ⇒ 它置顶前没表过态 ⇒ 按版本类型现算一个（defaultGroupIdFor）；
///   - 在置顶 + 压根没有这一条 ⇒ 未分组（"用户自己塞进置顶组的"那一档）。
///
/// NOTE: 那个组被删掉之后**不写盘**（删组只清 members、pinOrigin 那一格要等下一次写盘的
///       自清理）⇒ 这里必须自己判一次"那一组还在不在"，不然会返回一个**树上没有的组 id**，
///       于是那一条同名的哪儿都不出现、看着像凭空少了一行。`unpinInstance()` 之所以不用
///       判，是因为它紧接着就要写盘、那一次写盘本身就会把悬空的记录清掉。
QString homeGroupIdOf(const InstanceGroupStore *groups, const QString &key,
                      const VersionDisplayParts &part, const VersionManager *versions,
                      const QSet<QString> &knownGroupIds) {
    if (!groups->isPinned(key)) {
        return displayGroupIdOf(groups, key, part, versions, knownGroupIds);
    }
    QString origin;
    switch (groups->pinOriginOf(key, &origin)) {
    case InstanceGroupStore::PinOrigin::None:
        return QString();
    case InstanceGroupStore::PinOrigin::Auto:
        return defaultGroupIdFor(part, versions, knownGroupIds);
    case InstanceGroupStore::PinOrigin::Group:
        break;
    }
    if (origin == InstanceGroupStore::ungroupedMarker()) {
        return QString();
    }
    return knownGroupIds.contains(origin) ? origin : QString();
}

/// 「本机现在跑着的这几份会话」翻成面板里的几行。
///
/// **一行 = 一份会话**（不是"一个实例"）：同一个实例多开时那两行的名字一样，靠第二行那个
/// "已运行 …"分开（用户 2026-09-24 定的："一行一个会话，有时长是好主意"）。行尾那颗电源
/// 关的也就是**这一份**会话。那一行是谁由 `PanelRow::key` 带着（= 会话号，见下面那一段）。
QList<MainWindowUI::PanelRow> runningGamePanelRows(const QList<LauncherManager::GameSession> &games,
                                                   const VersionManager *versions, const QColor &ink,
                                                   qint64 nowMs) {
    QList<MainWindowUI::PanelRow> rows;
    rows.reserve(games.size());
    // 一份都没在跑：摆一句说明（**不是**空白面板 —— 用户点开是想知道"有没有在跑"，
    // 空白答不了这个问题）。
    if (games.isEmpty()) {
        MainWindowUI::PanelRow empty;
        empty.title = MainWindowUI::bottomBarPanelNoRunningTitle();
        empty.subtitle = MainWindowUI::bottomBarPanelNoRunningNote();
        empty.placeholder = true;
        rows.append(empty);
        return rows;
    }
    for (const LauncherManager::GameSession &game : games) {
        MainWindowUI::PanelRow row;
        // 图标走**和实例树同一个函数**（instanceRowMark）：用户挑过就画他挑的那张，没挑过
        // 才按版本分类 / 加载器给。这里原来写的是「图标先占位，等实例自定义头像有了换的是
        // 这一处」—— 2026-09-26 头像有了，换的就是这一处。
        row.icon = instanceRowMark(versions->displayParts(game.versionId), versions,
                                   MainWindowUI::bottomBarPanelIconSize());
        row.title = game.displayName;
        // 那句"跑了多久"在产品侧唯一的一处算它（MainWindowUI::runningDurationText()）——
        // 放那边是为了让四档话术都能在验证工程里直接量（"已经跑了 3 天"那一档等不出来）。
        row.subtitle = MainWindowUI::runningDurationText(game.startedAtMs, nowMs);
        row.actions = runningPanelActions(ink);
        // 这一行是谁 = 那一份会话的编号（回来时按它认人，**不按"第几行"**：面板开着的时候
        // 哪个实例先结束是它自己的事，按行号认会关错游戏）。
        row.key = QString::number(game.sessionId);
        rows.append(row);
    }
    return rows;
}

/// 右面板里那一行右边的那颗按钮的下标（onBottomBarPanelAction() 认的就是它）。
constexpr int kAccountPanelUseAction = 0;

/// 一份**会话**的身份键：这个人是谁。
///
/// 和 `MainWindow::launchIdentityKey()` 是同一个规矩，**两处必须一致**（那边有整段说明）：
/// 离线会话的 `accountUuid` 是空的，拿 uuid 当键会把所有离线账号撞成同一个。
QString sessionIdentityKey(const LauncherManager::GameSession &session) {
    return session.accountUuid.isEmpty() ? session.accountName : session.accountUuid;
}

/// 「最后登录的三个账户」翻成面板里的几行。
///
/// 口径是用户 2026-09-24 定的：「最后登录的三个账户就拿缓存放（包含离线和正版，总共
/// 三个）」—— 读的就是**账号列表**那一条缓存（AccountStore::accounts()），不另外记账、
/// 不联网、不碰令牌。
///
/// 顺序按 `saved_at` **倒序**：那是这个列表里唯一一个"最近"（它写的是"这份登录凭据最后
/// 一次取到的时刻"，见 AccountStore::captureActive()）。NOTE: 切账号**不会**刷新它，所以
/// 准确的说法是"最近登录/添加过的三个"，而不是"最近用过的三个"—— 不新加一个字段是这一轮
/// 的取舍（新加键就得连带处理老数据、还有"登录取自"那句 tooltip 的语义）。
/// NOTE: `saved_at` 全是 `QDateTime::currentDateTimeUtc().toString(Qt::ISODate)` 写出来的
///       （唯一写方），形状一致，所以按文本比就是按时间比。
QList<MainWindowUI::PanelRow> recentAccountPanelRows(const QList<SavedAccount> &accounts,
                                                     const QString &activeUuid, int limit,
                                                     const QHash<QString, QPixmap> &faces) {
    QList<MainWindowUI::PanelRow> rows;
    if (accounts.isEmpty()) {
        MainWindowUI::PanelRow empty;
        empty.title = MainWindowUI::bottomBarPanelNoAccountTitle();
        empty.subtitle = MainWindowUI::bottomBarPanelNoAccountNote();
        empty.placeholder = true;
        rows.append(empty);
        return rows;
    }

    QList<SavedAccount> sorted = accounts;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const SavedAccount &a, const SavedAccount &b) {
                         return a.savedAt > b.savedAt;
                     });
    if (limit > 0 && sorted.size() > limit) {
        sorted = sorted.mid(0, limit);
    }

    rows.reserve(sorted.size());
    for (const SavedAccount &account : sorted) {
        MainWindowUI::PanelRow row;
        // 行首那个头像**由调用方给**（`faces`，按 uuid 认人）：这里是纯函数，不能自己去
        // 读缓存、更不该去联网（见 MainWindow::accountFacePixmap() 那段 —— 它只碰本地
        // 已经躺着的图，一张都不下）。认人用 uuid 而不是名字：名字是可以重复的。
        // 取不到就是一张空图，那一行的行首**什么都不画**；这一档不该出现（最差也有一个
        // 首字母方块），只是不留"这里必须有一张图"的隐含前提。
        row.icon = faces.value(account.uuid);
        row.title = account.name;
        row.subtitle = account.offline ? MainWindowUI::bottomBarPanelOfflineAccountText()
                                       : MainWindowUI::bottomBarPanelOnlineAccountText();
        row.key = account.uuid;

        MainWindowUI::PanelAction use;
        if (account.isCurrent) {
            // 当前账号：按钮写「登出」、**可点**（用户 2026-09-24 原话：「把那个账户管理里的
            // 当前改成登出」）。原先这儿是「当前」+ 灰掉 —— 灰掉那颗点不动，于是这一行除了
            // 告诉用户"你就是你"之外没有别的用处。
            use.text = MainWindowUI::bottomBarPanelLogoutText();
            use.tooltip = MainWindowUI::bottomBarPanelLogoutTip();
        } else {
            use.text = account.offline ? MainWindowUI::bottomBarPanelSelectText()
                                       : MainWindowUI::bottomBarPanelLoginText();
            use.tooltip = account.offline
                              ? QObject::tr("切到这个离线账号（用这个名字离线进游戏）")
                              : QObject::tr("切到这个正版账号；令牌快到期了会顺手换一张");
        }
        row.actions.append(use);
        rows.append(row);
    }
    return rows;
}

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_ui(new MainWindowUI(this)),
      m_settings(new LauncherSettings(this)),
      m_launcherManager(new LauncherManager(this)),
      m_authManager(new AuthManager(this)),
      m_folderManager(new FolderManager(m_settings, this)),
      m_versionManager(new VersionManager(this)),
      m_resourceStore(new ResourceStore(this)),
      m_serverPinger(new ServerPing::Pinger(this)),
      m_javaSettings(new GlobalJavaSettings(this)),
      m_groupStore(new InstanceGroupStore(this)),
      m_skinStore(new PlayerSkinStore(this)),
      m_randomSkinStore(new RandomSkinStore(this)),
      m_skinApi(new SkinApi(this)) {
    // 安装页那棵树默认停在「不装（纯原版）」那一条（打开安装页时也是它，见
    // openInstallPage()）—— 这一位构造完就得是个真值，免得任何早于安装页读到它的路
    // （selectedLoaderIndex()）拿到一个没初始化的下标。
    //
    // NOTE: 这里原来写着「updateLoaderRows() 在下面就会被调到」，不成立 —— 构造函数里
    //       没有任何一处调它，第一次是 openInstallPage()（:1744）。初值不写在头文件里
    //       是因为那边只前置声明了 MainWindowUI。
    m_selectedLoaderIndex = MainWindowUI::modLoaderNoneIndex();
    setupConnections();
    // 下载页那五页的筛子先灌上（来源 / 类型 / 加载器那三项这一刻就定得下来；「版本」那一栏
    // 等在线清单一到才有的灌，见 onVersionsFetched）。界面那边只搭空框 —— ui/ 不 include
    // src/，"每一项是什么"只能由这一层灌进去。
    fillDownloadFilters();
    // 设置页先按盘上的值摆好（不扫 Java，那一段等第一次切到设置页，见
    // refreshJavaList()）。必须在下面 fetchAvailableVersions() **之前**：
    // 清单请求要不要走镜像就是在这一步交给 VersionManager 的。
    applySettingsToUi();
    // 上次用的是个**离线账号**：把名字接回 AuthManager。**必须在下面 updateBottomBar()
    // 之前** —— AuthManager 构造函数只读活动槽，而离线账号在盘上**没有凭据**，它读出来的
    // 是占位的 "Player"：不接回来的话底栏和 --username 里是个假名字。
    //
    // NOTE: 2026-09-24 之前这里还要顺手勾一下主页那个「离线模式」勾选框（不勾的话启动那条
    //       路会去要凭据，而盘上一份都没有 —— 用户拿到的是"请先登录"，一个死胡同）。
    //       那个勾选框去掉了，现在"这一趟走离线"这件事**只由"当前账号是离线账号"决定**
    //       （见 offlineIdentity()），于是这一句就够了。
    {
        const QString active = AccountStore::activeUuid();
        if (AccountStore::isOffline(active)) {
            m_authManager->setOfflinePlayer(AccountStore::nameOf(active));
        }
    }
    // 「游戏文件夹」：先把当前那一份交给 AppPaths —— 从这一句起，扫版本、下资源、
    // 找 Java 用的都是它。**必须在 refreshInstalledVersions() 之前**，那个函数扫的
    // 就是这个根目录。左边那张列表此刻还没有条目可选，所以要挡着信号灌。
    applyCurrentFolderToPaths();
    refreshFolderList();
    // 本地已安装的先填上（同步扫盘）；在线清单是异步拉的，回来之后由
    // onVersionsFetched() 填"可安装"下拉框 —— 在那之前它显示占位文案。
    refreshInstalledVersions();
    // 顺序要紧：必须在 refreshInstalledVersions() **之后**（下拉框里得先有条目可选），
    // 并且在 fetchAvailableVersions() 之前（免得用户在等待网络期间就手选了一个，
    // 那样启动时反而会把用户的选择抢掉）。也**只在启动时**跑这一次 ——
    // 不能塞进 refreshInstalledVersions()，那个函数每次清单到达、每次下载完成都会跑，
    // 塞进去就破坏了它"只保住、不新增"的语义。
    restoreLastLaunchedVersion();
    // 底栏那三格的初始值：登录名可能已经从存下来的令牌里恢复了，进度两格先藏起来，
    // 运行状态也要等第一次真的启动才有内容。
    updateBottomBar();
    // 主页右栏那个小人 + 底栏头像。放在 updateBottomBar() 之后是因为它要问
    // AuthManager"登过没有"，而那份状态是在它自己的构造函数里从盘上读回来的。
    // **必须在 fetchAvailableVersions() 之前**：那一下要发网络请求，外观这条线不该
    // 排在它后面（离线那条路更是完全不让它走）。
    startPlayerSkinLoad();
    // 底栏那两块上拉面板里画什么、行尾的按钮点了做什么（面板本身在 MainWindowUI 里，
    // 它不查数据）。**每次点开现算一遍**：面板弹出来那一刻的一张快照，它自己不会跟着
    // 刷新（实例随时会结束、账号随时会多一个），所以这里不许缓存上一次那几行。
    //
    // NOTE: 行里带着的 key 是"**这一行是谁**"（左面板 = 会话号、右面板 = 账号 uuid），不是
    //       行号 —— 面板弹出来到某个按钮被点之间隔着用户看的那两眼，那中间列表是会变的
    //       （见 MainWindowUI::PanelRow::key 那段）。
    m_ui->setBottomBarPanelContent(
        [this](bool account) -> QList<MainWindowUI::PanelRow> {
            if (account) {
                // 「最后登录的三个账户」（用户 2026-09-24 原话）—— 3 这个数只在这一处。
                const QList<SavedAccount> saved = AccountStore::accounts();
                // 每个人的脸**一次算完**再交给 recentAccountPanelRows()：那个函数是纯函数
                // （它不认识缓存目录、也不该认识）。这里给**全部**账号各算一张、不是只算
                // 要显示的那三张 —— 挑哪三张是那个函数的事（它按 saved_at 倒序取前三个），
                // 在调它之前再排一遍序就是两处各算一遍。
                QHash<QString, QPixmap> faces;
                faces.reserve(saved.size());
                for (const SavedAccount &one : saved) {
                    faces.insert(one.uuid, accountFacePixmap(one));
                }
                return recentAccountPanelRows(saved, AccountStore::activeUuid(), 3, faces);
            }
            return runningGamePanelRows(
                m_launcherManager->runningGames(), m_versionManager,
                // 图标那一档颜色 = 底栏那几行字的墨色（画的图标不带自己的颜色）。
                m_ui->getBottomBar()->palette().color(QPalette::WindowText),
                QDateTime::currentMSecsSinceEpoch());
        },
        [this](bool account, const QString &key, int action) {
            onBottomBarPanelAction(account, key, action);
        });

    m_versionManager->fetchAvailableVersions();
}

MainWindow::~MainWindow() {
    // 日志窗**没有父窗口**（理由见 showGameLogLine 里那段），所以 Qt 不会顺手把它们带走 ——
    // 这里自己收，否则关掉启动器之后屏幕上会留着几扇无主的日志窗。
    //
    // NOTE: 先把表清空再逐个 delete。窗的 finished 处理函数会来动这张表
    //       （m_gameLogWindows.remove），一边删一边被删的容器是未定义行为。
    //       values() 拿的是一份拷贝，清表在这一刻，所以那几下 remove 落空而不出错。
    const QList<GameLogWindow *> logWindows = m_gameLogWindows.values();
    m_gameLogWindows.clear();
    for (GameLogWindow *window : logWindows) {
        delete window;
    }
    delete m_ui;
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    // 外框那两条消息（WM_NCCALCSIZE / WM_NCHITTEST）转给 ui/WindowChrome —— 那一份的
    // 文件头把"为什么走这条路、为什么不走 Qt 那条 ExpandedClientAreaHint"写全了。
    //
    // 时机上多说一句：Qt 是在**处理这两条消息之前**把消息交到这里来的
    // （qwindowscontext.cpp 那句 `filterNativeEvent(platformWindow->window(), &msg, result)`
    // 排在它自己那个大 switch 前面），所以这里回的"这一条到此为止"是算数的 —— Qt 那套
    // 非客户区处理和我们自己的外框不会打架。
    WindowChrome *chrome = m_ui != nullptr ? m_ui->chrome() : nullptr;
    if (chrome != nullptr && chrome->handleNativeEvent(message, result)) {
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!m_launcherManager->hasRunningGame()) {
        QMainWindow::closeEvent(event);
        return;
    }

    // 游戏还在跑。以前这里什么都不做 —— 而"什么都不做"的实际后果是**把游戏强杀**：
    // MainWindow 是 main() 里的栈对象，窗口关掉 → exec() 返回 → MainWindow 析构 →
    // LauncherManager（它的子对象）析构 → m_gameProcess（它的子对象）析构，而
    // QProcess 的析构是这么写的（Qt 6.10，qprocess.cpp）：
    //
    //     if (d->processState != NotRunning) { qWarning() << ...; kill(); waitForFinished(); }
    //
    // 也就是说，用户只是想关掉启动器，游戏却收到一个 TerminateProcess（Windows 上
    // kill() 就是它）—— 游戏来不及存档、没有"保存并退出"。PCL 那种启动器关掉之后游戏
    // 照跑，java.exe 本来也活得过启动器，这一步只是不让上面那条析构链碰到它。
    //
    // 所以这里问一句，而且**默认那一边是"别动游戏"**（回车 = 取消，不关窗口）：用户要
    // 是真想停游戏，在游戏里按"退出游戏"那条路才是干净的。
    // 同时开着好几份时，真名报出来（"还有 3 个游戏在跑：A、B、C"）比"游戏还在运行"
    // 有用得多 —— 用户要判断的正是"我还开着哪些"。
    const QList<LauncherManager::GameSession> games = m_launcherManager->runningGames();
    QStringList names;
    names.reserve(games.size());
    for (const LauncherManager::GameSession &game : games) {
        names.append(instanceDisplayName(game.versionId));
    }
    const QString text =
        games.size() == 1
            ? tr("游戏「%1」还在运行。\n\n关掉启动器**不会**关掉游戏 —— 它会继续跑，"
                 "存档、联机都照常。\n要现在关掉启动器吗？")
                  .arg(names.first())
            : tr("还有 %1 个游戏在运行：%2。\n\n关掉启动器**不会**关掉游戏 —— "
                 "它们会继续跑，存档、联机都照常。\n要现在关掉启动器吗？")
                  .arg(games.size())
                  .arg(names.join(QStringLiteral("、")));
    // 按钮文字自己写：启动器没装 Qt 的中文翻译，标准按钮在界面上是英文的 Yes/No。
    // NOTE: 关掉窗口这一发**本身**在跑 closeEvent，不能开独立窗（用户给的方向是最终
    //       只有内容区那一块覆盖层）—— 内嵌提示在 closeEvent 里同样作数：窗口这时候
    //       还没关掉，`execPrompt` 那个嵌套事件循环照转，"事件循环里再开事件循环"
    //       这件事和原来 `box.exec()` 一模一样。
    //       默认那一边照旧是**取消**（下标 1），回车 = 别动游戏。
    //       第三格是 destructiveIndex —— 两个都不是危险动作（关的是启动器，游戏照跑），
    //       所以给 -1。
    const int choice = InlineMessage::ask(
        m_ui->overlay(), MessageKind::Question, tr("游戏正在运行"), text,
        QStringList{tr("关掉启动器"), tr("取消")}, 1, -1);
    if (choice != 0) {
        event->ignore();
        return;
    }

    // 顺序要紧：**先**把进程交出去（脱离所有权 + 断信号），再让窗口关掉 —— 关窗口之后
    // 紧接着就是析构链，交出去那一步必须在那之前做完（见 LauncherManager::detachRunningGame）。
    m_launcherManager->detachRunningGame();
    appendLog(tr("已关掉启动器，游戏继续在跑。"));
    QMainWindow::closeEvent(event);
}

void MainWindow::setupConnections() {
    // UI 按钮信号
    connect(m_ui->getStartButton(), &QPushButton::clicked, this, &MainWindow::onStartButtonClicked);
    // NOTE: 这里原来接的是主页那个「离线模式」勾选框 —— 它一变就得重画底栏（名字下面那行
    //       写的是"这次启动用哪种身份"）。勾选框去掉了，现在让那个身份变的动作是
    //       **换账号 / 加一个离线账号**，而那两个槽自己就会调 updateBottomBar()，
    //       所以这里没有对应的接线了。
    // 「账号与外观…」那两个入口（主页右栏 3D 预览下面那个 + 设置页「个性化」栏里「账户」那格的）。
    // 同一件事、同一个窗 —— 所以接的是同一个槽。
    connect(m_ui->getAccountSkinButton(), &QPushButton::clicked, this,
            &MainWindow::onAccountSkinButtonClicked);
    connect(m_ui->getAccountSkinSettingsButton(), &QPushButton::clicked, this,
            &MainWindow::onAccountSkinButtonClicked);
    connect(m_ui->getAddFolderButton(), &QPushButton::clicked, this,
            &MainWindow::onAddFolderClicked);
    // NOTE: 「移除文件夹」那个按钮 2026-09-24 删掉了（用户原话：「下面那个移除文件夹
    //       也可以弃掉了」）—— 「移除」现在只有行尾齿轮菜单里那一项，走
    //       onFolderActionRequested() → removeFolderFromList()，和按钮原来那条是同一个函数。
    // NOTE: 实例页右栏那排按钮 2026-09-25 傍晚从三颗剩下一颗（用户原话：「删掉实例页面
    //       右侧三个按钮（因为都已经分别有入口了），顺便把下面的文本干掉」）：
    //         - 「实例设置…」那颗**没有接线了** —— 它的入口改成"点这一条实例行"（行里的
    //           任意处）和行尾那颗常显的齿轮，两条都落在 MainWindow::eventFilter 里；
    //         - 「重命名…」那颗也没有接线了 —— 改名搬进了「实例设置」那一页，走
    //           openInstanceSettings() 里那条路（用户原话：「重命名进实例设置」）；
    //         - 只有「分组设置…」这一颗还在（它挪到「实例列表」那个框外面的右上角），
    //           接线**一个字没改**。
    connect(m_ui->getGroupInstanceButton(), &QPushButton::clicked, this,
            &MainWindow::onGroupInstanceClicked);
    // NOTE: 实例页那颗「停止游戏…」2026-09-25 删掉了（用户原话），这里原来接
    //       onStopGameClicked() 的那条线跟着没了 —— 停游戏现在只有底栏左面板里那行电源
    //       那条路（onBottomBarPanelAction() → stopGameSession()）。
    // 「导入整合包…」这一颗 2026-09-25 从右栏挪到了左栏「游戏文件夹」那一组（在
    // 「添加文件夹…」下面），槽没变、接线也没变。
    connect(m_ui->getImportModpackButton(), &QPushButton::clicked, this,
            &MainWindow::onImportModpackClicked);
    // 左边列表选中哪一份 .minecraft，启动器就换到哪一份上（PCL 的卡片是同一个交互）。
    connect(m_ui->getFolderList(), &QListWidget::currentRowChanged, this,
            &MainWindow::onFolderSelectionChanged);
    // 行尾那颗齿轮弹出来的菜单：界面只报"哪一份文件夹、第几项"，做哪件事是这边的事。
    m_ui->setFolderActionHandler([this](const QString &path, int action) {
        onFolderActionRequested(path, action);
    });
    // FolderManager 才是改状态的那一方：加载/删除目录、当前目录不存在时的退回，
    // 都会从它这儿发信号出来，界面只管跟着重画。
    connect(m_folderManager, &FolderManager::foldersChanged, this,
            &MainWindow::refreshFolderList);
    connect(m_folderManager, &FolderManager::currentFolderChanged, this, [this] {
        applyCurrentFolderToPaths();
        // 换了一整份 .minecraft，三件跟着"上一份"走的事要重置：
        //   - "目录名和 jar 名对不上"的告警只报一次，新目录得重新能报；
        //   - 本机 Java 列表是**按根目录**扫的（自管 Java 在 <数据目录>/java 下），
        //     下次进设置页要重扫一遍。NOTE: 重扫是"往里加"，下拉框里已经摆着的旧条目
        //     （上一个根目录里那份自管 Java）不会因此消失 —— 它指着的是个真实存在的
        //     java.exe，选它也能跑，要重开启动器才会清干净。
        //   - ★ 2026-09-27 补的第三件：**版本 json 的两张内存缓存表**。它们的键只有版本
        //     id（= 目录名），而两份 .minecraft 里各有一个 `1.20.1` 是常态 —— 不丢的话，
        //     切过来再启动同名的那个实例，classpath / natives / javaVersion / assetIndex
        //     全按**上一份目录**里那份 json 算，而游戏自己去读新目录的 json。这条原来漏了
        //     （上面那句只数了两件），症状是"换文件夹之后启动的不是这个版本"。详见
        //     VersionManager::clearVersionJsonCache()。
        m_reportedUnrecognizedVersions = false;
        m_javaListScanned = false;
        m_versionManager->clearVersionJsonCache();
        // 版本列表、实例列表、按钮文字、底栏全都对着新根目录重来一遍。
        // 分组**不需要**在这里重置或重读：InstanceGroupStore 每个访问器都会比一下
        // "这次问的是哪一份 .minecraft"，不一样就自己重读（见它的文件头）。
        refreshInstalledVersions();
    });
    // 分组数据变了（对话框里确定之后、改名之后 rekey）。重灌实例页那张树。
    connect(m_groupStore, &InstanceGroupStore::groupsChanged, this,
            &MainWindow::refreshInstalledVersions);
    // 用户在"已安装版本"下拉框里换一个版本，按钮第二行立刻跟着变。
    // NOTE: 这条只管用户手选。fillVersionCombo() 重灌下拉框时信号是被挡住的，
    // 那条路径由 refreshInstalledVersions() 自己收尾，两条路合起来才不会漏。
    // NOTE: 接的是 currentIndexChanged 而不是 currentTextChanged。后者在"两个条目
    // 渲染成同一串文字"之间切换时**不发射** —— QComboBoxPrivate::updateCurrentText()
    // 发现文本没变就提前 return（qcombobox.cpp:2956-2963）。而按下标看就不会漏：
    // 关掉「版本名」档之后，`我的整合包-Forge` 和 `别的名字-Forge` 完全可能显示成
    // 一样，那时按钮第二行和 tooltip 会停在旧值上。
    connect(m_ui->getInstalledVersionCombo(), &QComboBox::currentIndexChanged, this,
            [this] {
                updateStartButtonText();
                // 底栏左下角那条状态要看"选中的是不是正在跑的那个实例"，
                // 所以换版本也得跟着刷一遍。
                updateBottomBar();
                // 实例页右边那张树是同一份数据的另一种看法，跟着一起亮。
                syncTreeToCombo(m_ui->getInstalledVersionCombo(), m_ui->getInstanceList());
                // 离线账号那张脸**跟着选中的版本走**：游戏按客户端 JAR 里那套规矩分配默认
                // 皮肤，两套规矩算出来的是两张不同的图（见 DefaultSkin.hpp 文件头），所以
                // 换版本就得重算一遍。**只有离线账号会做实事**：别的账号在
                // startPlayerSkinLoad() 里当场就返回了（那是网络那条线，和版本无关）。
                // 算过的那一份在盘上，重算是两次 QFile::exists，不会卡。
                if (offlineIdentity()) {
                    startPlayerSkinLoad();
                }
            });
    // 树 → 下拉框。点树里的一行 = 在上面把它选中（用户要求的行为）。
    // 选中组头时不动作（syncComboToTree 里的守卫）—— 点一个组的标题不是在挑版本。
    connect(m_ui->getInstanceList(), &QTreeWidget::currentItemChanged, this, [this] {
        syncComboToTree(m_ui->getInstalledVersionCombo(), m_ui->getInstanceList());
    });
    // 点**组头**那一下 = 展开/收起（用户原话：「每一个组和版本下载几乎一致，点一下
    // 打开/关闭」），而且当场记下来 —— 下次打开还照这样画（见 InstanceGroupStore::
    // Group::expanded）。点实例那一行仍旧只是"把它选中"（上面那条 currentItemChanged）。
    //
    // 整条行都是热区：那一行除了"展开/收起"没有别的事可做，而"点了没反应"是用户唯一会
    // 当成启动器坏了的情况（和版本树那几条分类同一条规矩）。
    //
    // NOTE: 先算再做（`!item->isExpanded()` 在 setExpanded() 之前求值）—— 和加载器树那
    //       一句同一个写法、同一个教训（见那里的 NOTE）。
    const auto onInstanceTreeActivated = [this](QTreeWidgetItem *item) {
        if (item == nullptr) {
            return;
        }
        if (!item->data(0, MainWindowUI::rowGroupGearRole()).toBool()) {
            // ★ **实例行**：点这一行（空白处、左边那个图标、版本名 —— 除了行尾那三颗按钮
            // 之外的任何地方）= 进这个实例的设置。用户 2026-09-25 原话：「直接点空白区域
            // （和左边图标版本名）进入实例设置」。行尾那三颗由 eventFilter 先吃掉，落不到
            // 这里来（见那一段；它们三个各自是"别的事"，不该顺带把设置页也开了）。
            //
            // NOTE: 这一下**同时**把它选中（视图在按下那一刻就把 currentItem 挪过来了），
            //       那条路照旧、它同步到主页那个下拉框。两件事不冲突：选中是给"启动"用的，
            //       进设置是这一下点名的另一件事 —— 用户明确要的是"点这一行就进设置"。
            openInstanceSettings(item->data(0, MainWindowUI::instanceIdRole()).toString());
            return;
        }
        const bool expand = !item->isExpanded();
        item->setExpanded(expand);
        rememberGroupExpanded(item->data(0, MainWindowUI::groupIdRole()).toString(), expand);
    };
    // 接线口和另外三棵树同一条（鼠标单击 + 键盘回车），另外交待**哪些行是"开/关"**：
    // 点得快的第二发（以及后面每一发）到的是双击事件，只有开关行才补那一发。
    // 组头就是开关行；实例行是"选中它"，算做事（那是 currentItemChanged 那条路的事，
    // 双击不该办两遍）。
    connectRowActivation(m_ui->getInstanceList(), onInstanceTreeActivated,
                         [](QTreeWidgetItem *item) {
                             return item != nullptr
                                    && item->data(0, MainWindowUI::rowGroupGearRole()).toBool();
                         });
    // 组行行尾那颗**齿轮**：它不是"点这一行"，所以不走上面那条路 —— 走**视口上的事件
    // 过滤器**，判据是 InstanceGroupDelegate::gearRect()（和代理画它的那一格是同一份
    // 几何）。过滤器是为了**吃掉**那一下：不然点齿轮会顺手把这一组展开/收起
    //（和加载器树上那个「取消」小叉、下载页那几个刷新圈完全同一个路数）。
    m_ui->getInstanceList()->viewport()->installEventFilter(this);
    // 组行行尾那颗齿轮里那几项点了做什么（界面只报"哪一组、第几项"）。
    m_ui->setGroupActionHandler([this](const QString &groupId, int action) {
        onGroupActionRequested(groupId, action);
    });
    // 齿轮里「把选中的实例移入分组…」弹出的**第二个**框（挑目标）点了哪一组。
    m_ui->setGroupPickHandler([this](const QString &groupId) {
        setSelectedInstanceHomeGroup(groupId);
    });
    // ---------- 下载页「游戏」那两层：版本树 → 安装页 ----------
    //
    // 点树里**一个版本号**（子条目）进安装页；点**分类**（顶层那四条）只展开/折叠 ——
    // 用户点的是那一行最右边那个 >/V，箭头就画在行上。判据是"有没有父条目"：分类存的是
    // 分类键、版本号存的是版本号，两者都不是空串，光看 userData 分不清这两种。
    //
    // 整个行都是热区（不要求用户精确点在那 12 像素的箭头上）：那一行除了"展开"没有
    // 别的事可做，而"点了没反应"是用户唯一会当成启动器坏了的情况。
    //
    // 共用一份 lambda 而不是一个槽函数：槽声明里带 QTreeWidgetItem* 就得让 moc 也认得
    // 这个类型（等于把这个头拖进 QtWidgets 的树控件），而这里类型本来就是完整的。
    const auto openFromTreeItem = [this](QTreeWidgetItem *item) {
        if (item == nullptr) {
            return;
        }
        if (item->parent() == nullptr) {
            item->setExpanded(!item->isExpanded());
            return;
        }
        const QString versionId =
            item->data(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole).toString();
        if (!versionId.isEmpty()) {
            openInstallPage(versionId);
        }
    };
    // 鼠标和键盘走**同一个**接线口（见 ui/RowActivation.hpp）：两条路都指向上面那份 lambda，
    // 而且一次手势只进来一次。以前这里是 itemClicked + itemActivated 各接一条 ——
    // Windows 上双击两发都到，于是 openInstallPage() 跑两遍（第二遍把四个在飞的加载器版本
    // 请求 abort 掉重发），分类那一行的展开/收起也会被翻两次（看上去就是"双击没反应"）。
    // 第三件事只有这一棵树要交待：**哪些行的激活是"开/关"**。分类那四条（顶层）是，
    // 点得快的时候第二发（以及后面每一发）到的是双击事件、itemClicked 不发它 —— 不补上
    // 用户看到的就是"一直点，有几次没反应"。版本号那一条是"进安装页"，算做事：双击仍然
    // 只办一次（不然安装页开两遍）。
    connectRowActivation(m_ui->getVersionTree(), openFromTreeItem,
                         [](QTreeWidgetItem *item) { return item->parent() == nullptr; });
    connect(m_ui->getInstallBackButton(), &QPushButton::clicked, this,
            &MainWindow::backToVersionTree);
    connect(m_ui->getInstallButton(), &QPushButton::clicked, this,
            &MainWindow::onInstallButtonClicked);
    // 名称框：每敲一个字都要重算那句小字（重名提示要"实时"——用户原话）。
    connect(m_ui->getInstallNameEdit(), &QLineEdit::textChanged, this,
            &MainWindow::onInstallNameEdited);
    // 加载器那棵树（安装页）：**点一下**那条里的两件事分开办 ——
    //   · 点顶层（一个加载器）：**只展开/收起**那一条（用户原话："点一下，展开，再点一下
    //     关闭"）。展开是"看看有哪些版本"，看一眼不等于选了它 —— **选中是点版本号那一下
    //     的事**（2026-09-23 用户原话："现在是展开以后就自动选择"，那正是要改掉的旧样子）；
    //   · 点「不装（纯原版）」：它底下没有版本可展开，点它就是选中它。它也是换加载器的
    //     唯一退路 —— 选中某一条之后其余几条会变灰（见 updateLoaderRows()），
    //     没有它就走不回去了；
    //   · 点子条目（一个版本号）：选中它爹那条加载器 + 把这一条钉在这个版本上，**收起**
    //     那条，并把版本号写回它的第二行小字（用户原话："选择版本以后收缩，显示版本"）。
    // 共用一份 lambda 而不是槽：槽签名里带 QTreeWidgetItem* 就得让 moc 也认得这个类型
    // （版本树那边同一个理由，见 openFromTreeItem 上面那段）。
    const auto onLoaderTreeClicked = [this](QTreeWidgetItem *item) {
        if (item == nullptr) {
            return;
        }
        // 灰的那几条（选中某个加载器之后其余几条、"正在获取…"/"列表没取到"那两个占位）
        // **不是能点的东西**：点它什么都不该发生。放在最前面是因为"点不动"这件事对
        // 顶层和子条目是同一个道理。
        if (item->isDisabled()) {
            return;
        }
        if (item->parent() == nullptr) {
            const int index =
                item->data(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole).toInt();
            if (index == MainWindowUI::modLoaderNoneIndex()) {
                selectLoader(index);
                return;
            }
            // NOTE: 这里**先算再做**（`!item->isExpanded()` 在动状态之前求值）是老账：
            //       2026-09-23 那次"选了「自动」以后列表就点不开了"就是这一句读到了**已被
            //       改过**的 isExpanded()（当时这一支里还夹着一次 selectLoader()，它顺手
            //       把这一条撑开了）。如今这一支除了 setExpanded 一行都不做，写法还是留着：
            //       它是这一句话唯一安全的写法。
            const bool expand = !item->isExpanded();
            item->setExpanded(expand);
            return;
        }
        QTreeWidgetItem *loader = item->parent();
        const int index =
            loader->data(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole).toInt();
        // userData 里可能是那两档的键、也可能是一个具体版本号（见
        // MainWindowUI::loaderVersionLatestText 上面那段）—— 这里原样存下来，认它的是
        // 数据层（LoaderMeta::loaderRequestFromChoice，安装那条路开头解码一次）。
        const QString version =
            item->data(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole).toString();
        // 走到这里就是"选中了这条加载器的这个版本"（上面那句 isDisabled() 已经把
        // "正在获取…"/"列表没取到"那两个灰占位挡在外面了 —— 不挡的话"列表没取到"会被
        // 当成一个选中的版本号填进默认名里）。
        m_chosenLoaderVersion.insert(index, version);
        selectLoader(index);
        loader->setExpanded(false);
        // 钉的版本号是默认名的一部分（`1.20.1-Forge_47.4.10`），换一条就得重算。
        updateInstallName();
    };
    // 接线口和版本树那一条完全一样（鼠标单击 + 键盘回车），另外交待**哪些行是"开/关"**：
    // 点得快的第二发到的是双击事件，只有开关行才补那一发。按上面那三条判据划：
    // 顶层那几条里除「不装」以外都是开关（「不装」点它是选中，算做事），
    // 灰的那几条点不动（handler 开头就 return 了，不算）；子条目（版本号）是选中，算做事。
    connectRowActivation(
        m_ui->getLoaderTree(), onLoaderTreeClicked, [](QTreeWidgetItem *item) {
            if (item->parent() != nullptr || item->isDisabled()) {
                return false;
            }
            return item->data(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole).toInt()
                   != MainWindowUI::modLoaderNoneIndex();
        });
    // 行里那个「取消」小叉（选中那条加载器右边那个 ×，见 updateLoaderRows()）：它不是
    // "点这一行"，所以不走上面两条路 —— 走**视口上的事件过滤器**，判据是
    // VersionTreeDelegate::clearRect()（和代理画它的那一格是同一份几何）。
    // NOTE: 过滤器是为了**吃掉**那一下：Qt 那边 itemClicked 是在鼠标松开时发的，而
    //       "点中了行里的一个按钮"和"点中了这一行"必须是两件事 —— 不然点叉会顺手把那一列
    //       展开/收起，用户看到的是"我按了取消，它却收起来了"。
    m_ui->getLoaderTree()->viewport()->installEventFilter(this);
    // 下载页左栏那份分类列表的行尾也有"行里的一个按钮"：每个条目右边那个刷新
    // （见 MainWindow::refreshDownloadCategory()）。同一个理由挂同一个过滤器 —— 那一下
    // 必须被**吃掉**，不然它会顺着 Qt 那条"点中了这一行"的路再走一遍（切页，以及空页
    // 自己搜的那一发），和我们自己要做的事叠在一起。"切不切、搜不搜"由
    // refreshDownloadCategory() 一处说了算。
    if (QListWidget *categories = m_ui->getDownloadCategoryList()) {
        categories->viewport()->installEventFilter(this);
    }
    // 「下载」页左栏那份分类（游戏 / 整合包 / 模组 / 资源包 / 光影）：点哪个，右栏那份
    // QStackedWidget 就翻到哪一页。**"第几个分类 = 第几页"这条对应关系是界面自己搭出来的**
    // （同一个表、同一个顺序一页一页加进去），这里只做一次越界保护：分类表和页数将来要是
    // 对不上，宁可停在原来那页，也不要 setCurrentIndex(-1) 把右栏整片清空 —— 那种"点一下
    // 界面就空了"的毛病，用户只会当成崩了。
    connect(m_ui->getDownloadCategoryList(), &QListWidget::currentRowChanged, this,
            [this](int row) {
                QStackedWidget *stack = m_ui->getDownloadStack();
                if (row >= 0 && row < stack->count()) {
                    stack->setCurrentIndex(row);
                }
                // 后面那几页（整合包/模组/数据包/资源包/光影）**第一次进页面时**才去搜一次
                //（空搜索词 = 这一类里最热门的）。理由有两条，第二条才是主要的：
                //   · 启动时一次发好几个请求是白花用户的等待（他多半只会进其中一页）；
                //   · 那个接口按分钟限流（实测响应头 X-Ratelimit-Limit: 300），
                //     每个用户每次开启动器都固定烧掉几发额度，换来的东西他一次都没看。
                // "第一次"的判据在 ensureResourcePageLoaded() 里（树上已经有东西就不动）。
                // 「游戏」那一页（第 0 页）不是这一类，kindFromCategoryKey 认不出来。
                const QStringList keys = MainWindowUI::downloadCategoryKeys();
                ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
                if (row >= 0 && row < keys.size()
                    && ResourceCatalog::kindFromCategoryKey(keys.at(row), &kind)) {
                    ensureResourcePageLoaded(kind);
                }
            });
    // 左栏顶上那颗「← 返回实例设置」（2026-09-28）。**整个下载页一颗**，摆在左栏顶上
    // （用户原话「返回按钮也是放在左上角（就是左边栏的顶上）」）—— 所以这里不再是一圈循环，
    // 就接一次。
    //
    // NOTE: 那颗键要回的是"当初从哪个实例点进来"，而那个实例只有一个出处
    //       （`m_downloadFromInstanceId`，由 openDownloadCategory 摆、由这个槽消费掉）。
    //       把实例号塞进信号里也能做，但那样"哪一位是活的"就有两份，而"按下时用的那一位"
    //       和"露出来的那一颗"必须永远是同一个。
    if (QPushButton *back = m_ui->getDownloadBackButton()) {
        connect(back, &QPushButton::clicked, this,
                &MainWindow::returnToInstanceSettingsFromDownload);
    }
    // 顶上那四颗导航（2026-09-28，用户原话「点击顶部下载按钮就刷新页面回到主页」）。
    //
    // **只管 MainWindowUI 管不着的那一件**：翻页和"点的是当前这一颗就把覆盖层收起来"那两条
    // 仍旧由 MainWindowUI 自己接的那一条负责（见那边搭导航那段）。这边补的一条只做
    // 下载页那一件 —— 那一层够不着"下载页的首页是哪一栏、筛子该怎么清"。
    //
    // NOTE: 两条连接都在 `navRequested` 上，**MainWindowUI 那条先接**（setupUi 早于
    //       setupConnections），所以覆盖层那一发先收完，这边再动手。
    connect(m_ui->titleBar(), &TitleBar::navRequested, this, &MainWindow::onNavRequested);
    // ---------- 下载页后面那几页：搜索框 + 三层资源表 ----------
    //
    // 这几页走的是同一条路，所以这里是**一圈循环**：每一页接上"回车就搜"和"点一下就动"，
    // 三个回复信号接的全是同一个槽（槽里按请求钥匙认回来的是哪一页）。
    //
    // NOTE: 那两个 lambda 收的是 Kind 而不是 res 的键 —— 用值捕获，每页一份，
    //       不会在循环结束后全都指向最后一页。
    for (ResourceCatalog::Kind kind : ResourceCatalog::allKinds()) {
        const QString key = ResourceCatalog::kindKey(kind);
        QLineEdit *edit = m_ui->getDownloadSearchEdit(key);
        QTreeWidget *tree = m_ui->getDownloadResourceTree(key);
        if (edit == nullptr || tree == nullptr) {
            // MainWindowUI 那边这几页是一段循环搭出来的，这里少一个说明"分类键"这套约定
            // 两边对不上了。**说出来**，不然表现是"某一页点了没反应"，没人查得到。
            qWarning() << "下载页缺少控件，分类键：" << key;
            continue;
        }
        // 回车（returnPressed）才搜，不是边打字边搜：一次搜索是一条请求，而这个接口按分钟
        // 限流；边打边搜等于把额度花在"用户打到一半的那些词"上。
        // NOTE: 信号带一个 QString（编辑框里的文字），这个槽不收参数，多余的那个会被丢掉
        //       （Qt 的新式 connect 允许槽少收参数）。
        connect(edit, &QLineEdit::returnPressed, this, [this, kind] { startResourceSearch(kind); });

        // ---------- 旁边那一行筛子（2026-09-25）----------
        //
        // 「搜索」那颗键：和名称框里按回车是**同一件事**（用户原话「一个搜索键」）。
        if (QPushButton *searchButton = m_ui->getDownloadFilterSearchButton(key)) {
            connect(searchButton, &QPushButton::clicked, this,
                    [this, kind] { startResourceSearch(kind); });
        }
        // 「清空条件」：把名称框和四个筛子退回"不筛"，然后**重搜一发**（2026-09-26 用户原话
        // 「修改任何内容（不论是来源网站还是…）都刷新一下」；从前这里是"清完不动"，于是
        // 清完的界面和列表对不上 —— 六个框全空着，底下还摆着上一批按老条件筛出来的结果）。
        if (QPushButton *resetButton = m_ui->getDownloadFilterResetButton(key)) {
            connect(resetButton, &QPushButton::clicked, this, [this, kind] {
                resetDownloadFilters(kind);
                startResourceSearch(kind);
            });
        }
        // 「版本」那一格也可输入，在里面按回车**同样等同于按搜索键**（用户原话
        // 「名字/版本可自行输入，这两个框选中时按回车等同于按搜索键」）。
        //
        // NOTE: 必须接 `lineEdit()->returnPressed`，不能接下拉框自己的 `returnPressed` ——
        //       后者只在"框里那串字正好是列表里选中的那一项"时才发（Qt 的
        //       QComboBox::returnPressed 就是 `lineEdit()->returnPressed` 加了一道
        //       "有没有选中项"的闸，见它文档里那句）。而这一格存在的理由正是**用户自己
        //       打一个清单里没有的版本号**，那一发会被那道闸吃掉：回车下去什么都没发生。
        // NOTE: `lineEdit()` 只有在 `setEditable(true)` 之后才不是空的（界面那边已经设了，
        //       见 MainWindowUI 里 makeFilterCombo 那一段），所以这里要判一下。
        if (QComboBox *versionCombo = m_ui->getDownloadFilterVersionCombo(key)) {
            if (QLineEdit *versionEdit = versionCombo->lineEdit()) {
                connect(versionEdit, &QLineEdit::returnPressed, this, [this, kind, key] {
                    // 先记一笔，理由见下面那个 activated 里那段（同一按回车会来两发）。
                    m_versionEnterSearches.insert(key);
                    QTimer::singleShot(0, this,
                                       [this, key] { m_versionEnterSearches.remove(key); });
                    startResourceSearch(kind);
                });
            }
            // 「版本」是从下拉里挑的（而不是手打的字）⇒ 挑完就搜。
            //
            // NOTE: 这一发**和上面那一发会撞**：这一格是**可输入的**，在框里按回车时 Qt
            //       先发 `QLineEdit::returnPressed`（上面那条路），紧接着发
            //       `editingFinished`，而 QComboBox 自己接了后者 —— 框里那串字正好是清单里
            //       某一项时它补一次 setCurrentIndex + activated（见 Qt 6.10 的
            //       QComboBoxPrivate::editingFinished 与 QLineEditPrivate::controlEditingFinished
            //       那两句的先后）。两发都通到"搜一发"，不拦的话同一按回车会搜两次、
            //       烧两发限流额度。判据就是上面 `returnPressed` 记的那一笔 —— **同一回合里
            //       只认第一发**（那一笔由 0 毫秒的单次定时器抹掉）。
            connect(versionCombo, &QComboBox::activated, this, [this, kind, key](int) {
                if (m_versionEnterSearches.contains(key)) {
                    return;
                }
                startResourceSearch(kind);
            });
        }
        // ---------- 四个筛子：在下拉里挑了一项 ⇒ 立刻重搜（2026-09-26）----------
        //
        // 用户原话：「修改任何内容（不论是来源网站还是…）都刷新一下」+「下拉即搜，文字回车才搜」。
        // 于是分工是：**筛子（下拉）挑一项 = 立刻搜；框里打的字 = 只有回车那一下搜**。
        //
        // NOTE: 这四格接的是 `activated` 而不是 `currentIndexChanged`。后者在**程序**改
        //       的时候也发（灌清单、换来源时把加载器/类型退回「全部」、清空条件），而"用户
        //       在下拉里挑了一项"只有 activated 说得清（Qt 只在 itemSelected /
        //       editingFinished 里发它）。两个好处：灌数据不会自己搜自己，而
        //       `setCurrentIndex()` 这种"改到一半"的中间态也不会漏出去一发。
        //
        // 「来源」换了一项：这一页的加载器 / 类型跟着换一份选项（两边的键不是一套），
        // 换完**就按新来源搜一发**。
        if (QComboBox *sourceCombo = m_ui->getDownloadFilterSourceCombo(key)) {
            // 顺序是**对的**：用户挑那一项时 Qt 先 setCurrentIndex（⇒ 下面这条先把选项
            // 换掉）再发 activated（⇒ 这一发读到的已经是新选项），见 QComboBoxPrivate::
            // itemSelected 那两句的先后。
            connect(sourceCombo, &QComboBox::currentIndexChanged, this,
                    [this, kind] { refreshDownloadFilterOptions(kind); });
            connect(sourceCombo, &QComboBox::activated, this,
                    [this, kind](int) { startResourceSearch(kind); });
        }
        // 「模组加载器」（**只有模组那一页有这个控件**，别的页回空指针）和「类型」。
        if (QComboBox *loaderCombo = m_ui->getDownloadFilterLoaderCombo(key)) {
            connect(loaderCombo, &QComboBox::activated, this,
                    [this, kind](int) { startResourceSearch(kind); });
        }
        if (QComboBox *typeCombo = m_ui->getDownloadFilterTypeCombo(key)) {
            connect(typeCombo, &QComboBox::activated, this,
                    [this, kind](int) { startResourceSearch(kind); });
        }
        // 点一行：三种行的行为不一样，判据在 onResourceItemActivated() 里（那里是唯一出处）。
        // 鼠标和键盘走同一个接线口（见 ui/RowActivation.hpp）—— 这里**必须**只进来一次：
        // 第三层那一行"点它就是下它"，而双击的两发里第二发会撞上"已有下载任务正在进行"，
        // 界面于是先弹一句假的「下载失败」，顺手把待导入记录也吃掉了（真的整合包下完就
        // 再也不会走导入）。
        // NOTE: 2026-09-25 起那笔记录只在"这一批真的开起来了"之后才登记（见
        //       downloadResourceVersion 里那句），假失败那一发带的是同一个 label，
        //       但它根本没开出批次，登记这一步压根没走到 —— 吃不到它了。
        //       这里仍然只该进来一次：假失败本身是假的，双击仍只该下一次。
        // 哪几行是"开/关"：**上面两层**（资源、游戏版本）是，第三层那一行（版本号）
        // 是"下它"，算做事 —— 双击仍然只下一次。这个判据和 onResourceItemActivated()
        // 里那个 resourceRowDepth() 说的是同一件事（那边 2 才是下），两处必须一起看。
        connectRowActivation(tree, [this](QTreeWidgetItem *item) { onResourceItemActivated(item); },
                             [](QTreeWidgetItem *item) {
                                 // 写"0 或 1"而不是"<= 1"：树上认不出来的行回 -1，
                                 // 那个也**不是**开关行。
                                 const int depth = resourceRowDepth(item);
                                 return depth == 0 || depth == 1;
                             });
        // 点行身 = 打开详情页，点箭头 = 展开/收起（2026-09-26）。这一发走**事件过滤器**
        // 而不是上面那个接线口：接线的那个只给"点中了哪个条目"、不给"点在条目里的哪儿"
        // （箭头是行里的一格），和加载器树上那个「取消」小叉是同一个形状的判据
        // —— 见 MainWindow::eventFilter 里"下载页那四张资源表"那一段。
        //
        // NOTE: 装的是**视口**（鼠标事件按光标位置投递给视口）—— 和实例树、加载器树、
        //       左栏那份分类列表三处一模一样。
        // ★ 这一句**必须排在上面那句 connectRowActivation 之后**：Qt 的事件过滤器是
        //   "后装的先跑"，而上面那个接线口会给**开关行**的每一发双击补一次激活
        //   （见 RowActivation.hpp 里那段"点得快的时候每一下都要算"）。排在它前面的话，
        //   "双击行身"那一发会先被它当成展开/收起办掉、再被我们吃掉 —— 症状是"点开这一页
        //   之前，底下那棵树自己展开又收起了一下"。位置反过来就没有这个窗口。
        //   （那个接线口自己也挂在视口上，所以这两句的先后**就是**那两条过滤器的先后。）
        tree->viewport()->installEventFilter(this);
        // 图标是**懒要**的：条目建出来之后才 requestIcon()，不然图标回来时没有行可以放
        //（而且 store 命中缓存时是同步发的，见 requestIcon 的 NOTE）。
    }
    // 下载页「整合包」那一页那颗「导入整合包…」（用户原话「整合包那一页带一个导入功能」）
    // 走的**就是**实例页那颗同一个槽：同一条路、同一套"选文件 → 问实例名 → 挑 Java"，
    // 没有理由抄两份。两颗按钮的区别只在它们在哪儿，做的事是同一件。
    if (QPushButton *downloadImport = m_ui->getDownloadFilterImportModpackButton()) {
        connect(downloadImport, &QPushButton::clicked, this, &MainWindow::onImportModpackClicked);
    }
    connect(m_resourceStore, &ResourceStore::searchFinished, this,
            &MainWindow::onResourceSearchFinished);
    connect(m_resourceStore, &ResourceStore::versionsFinished, this,
            &MainWindow::onResourceVersionsFinished);
    connect(m_resourceStore, &ResourceStore::iconReady, this,
            &MainWindow::onResourceIconReady);
    // ★ 2026-09-28：同一个 `versionsFinished` 上**再挂两个接的人**（前置模组那一趟、
    //   更新检测那一趟）。Qt 允许多个槽接同一个信号，而**那三处各自按自己的钥匙认人**
    //   （详情页那把、`ModDependencies::Lookup::requestKey`、更新检测那把）—— 所以
    //   谁的回复谁接走，不会互相顶掉。**不要**为了"省两个连接"把它们并成一个槽：
    //   那样这个函数会变成三段 if，而三件事的生命周期根本不一样（详情页那一页关掉就没了、
    //   前置那一趟跟着一次点击、更新检测那一趟跟着一次检查）。
    connect(m_resourceStore, &ResourceStore::versionsFinished, this,
            &MainWindow::onDependencyVersionsFinished);
    connect(m_resourceStore, &ResourceStore::versionsFinished, this,
            &MainWindow::onUpdateCheckVersionsFinished);
    connect(m_resourceStore, &ResourceStore::projectsFinished, this,
            &MainWindow::onDependencyProjectsFinished);
    // 详情页那一行前置要的名字（2026-09-28）。**和上面那一趟分开两个槽**，理由同 `versionsFinished`
    // 那三条 —— 两件事各有各的钥匙，各自认人（见 :: 后面那段 ★）。
    connect(m_resourceStore, &ResourceStore::projectsFinished, this,
            &MainWindow::onResourceProjectsFinished);
    // 服务器那一趟测速（2026-09-27）：**两个信号都是从工作线程发出来的**，接的人在这儿
    // （主线程），Qt 按"接收方所在线程"自动排队投递 —— 所以这两个槽里可以随便碰界面。
    // NOTE: 这条按**下标**回来（下标 = servers.dat 里那一行的行号），所以槽里要拿
    //       m_serverPing.addresses 才能翻回是哪一台（见 onServerPingOne）。
    connect(m_serverPinger, &ServerPing::Pinger::oneFinished, this,
            &MainWindow::onServerPingOne);
    connect(m_serverPinger, &ServerPing::Pinger::allFinished, this,
            &MainWindow::onServerPingAllFinished);

    // ---------- 设置页 ----------
    // 「Java 版本」：选自动，或者钉死一个具体运行时。
    connect(m_ui->getJavaVersionCombo(), &QComboBox::currentIndexChanged, this,
            &MainWindow::onJavaSelectionChanged);
    // 「实例指定的 Java 版本不符时提醒」这个总闸。管的是**启动时那个三岔口**
    // （onStartButtonClicked 里问的那一次），不是这一页的显示。
    connect(m_ui->getJavaWarnMismatchCheck(), &QCheckBox::toggled, this,
            &MainWindow::onJavaWarnMismatchChanged);
    // 内存：先接「自动 / 手动」总开关，再接「最大内存」那一行的**一根条 + 一个数字框**。
    // 自动档下那两件都是禁用的（见 refreshMemoryUi），所以"用户改数字"这件事只可能发生在
    // 手动档，onMemoryChanged 里那句 autoMemory() 守卫是给程序内部 setValue 兜底的。
    connect(m_ui->getMemoryAutoCheck(), &QCheckBox::toggled, this,
            &MainWindow::onMemoryAutoChanged);
    // 条和数字框说的是同一件事 ⇒ 走同一个槽，谁改的在那儿看当前值就知道（见 onMemoryChanged：
    // "另一个还停在旧值上"的那一侧会被挪过去）。条多走一道 onMemorySliderMoved 只是为了把
    // **拖动中**（还没松手）那一串 valueChanged 和"松手了、该落盘了"分开。
    connect(m_ui->getMemoryMaxSpin(), &QSpinBox::valueChanged, this,
            &MainWindow::onMemoryChanged);
    connect(m_ui->getMemoryMaxSlider(), &QSlider::valueChanged, this,
            &MainWindow::onMemorySliderMoved);
    // 松手这一发**必须**单独接：拖动中 valueChanged 已经响过了，松手时位置没再变就不会
    // 再来一发 —— 只接 valueChanged 的话，拖到哪儿、界面上数字也变了，盘上却一个字节
    // 都没写（下一次启动用的还是老值）。这一发就是"拖完了、可以落盘了"。
    connect(m_ui->getMemoryMaxSlider(), &QSlider::sliderReleased, this,
            &MainWindow::onMemoryChanged);
    connect(m_ui->getLauncherBehaviourCombo(), &QComboBox::currentIndexChanged, this,
            &MainWindow::onLauncherBehaviourChanged);
    connect(m_ui->getDownloadSourceCombo(), &QComboBox::currentIndexChanged, this,
            &MainWindow::onDownloadSourceChanged);
    // 「下载新模组时直接放进当前实例」（2026-09-28）：和上面那一项同一条路 —— 设置里改完
    // 立刻生效（下载那一刻现读），不用重启启动器。
    connect(m_ui->getDownloadAutoIntoInstanceCheck(), &QCheckBox::toggled, this,
            &MainWindow::onDownloadAutoIntoInstanceChanged);
    // 「启动选项」那三项。三项都是"下一次启动才读"的设置（日志那个例外：见
    // showGameLogLine 的说明），所以槽里只落盘，不去碰任何正在跑的东西。
    connect(m_ui->getShowGameLogCheck(), &QCheckBox::toggled, this,
            &MainWindow::onShowGameLogChanged);
    connect(m_ui->getProcessPriorityCombo(), &QComboBox::currentIndexChanged, this,
            &MainWindow::onProcessPriorityChanged);
    // 两个数字框共用一个槽（和内存那两个 spin 同一套写法：谁改的在那里区分不了，
    // 也不该区分 —— 两个值一起存）。
    connect(m_ui->getGameWindowWidthSpin(), &QSpinBox::valueChanged, this,
            &MainWindow::onGameWindowSizeChanged);
    connect(m_ui->getGameWindowHeightSpin(), &QSpinBox::valueChanged, this,
            &MainWindow::onGameWindowSizeChanged);
    // 「启动提醒」那两条。和上面三项一样是"下一次启动才读"，槽里只落盘。
    connect(m_ui->getWarnDuplicateLaunchCheck(), &QCheckBox::toggled, this,
            &MainWindow::onWarnDuplicateLaunchChanged);
    connect(m_ui->getWarnWorldLossCheck(), &QCheckBox::toggled, this,
            &MainWindow::onWarnWorldLossChanged);
    // JVM 参数刻意**不**接 textChanged：边打字边存会把打到一半的半条参数写进配置，
    // 而它是直接塞进命令行的东西，半条参数会让游戏起不来。改完点「应用」。
    connect(m_ui->getApplyExtraJvmArgsButton(), &QPushButton::clicked, this,
            &MainWindow::onApplyExtraJvmArgsClicked);
    // 「外观」那一格（条上那行字）：**反着来** —— 边打字边生效、也边落盘。
    // NOTE: 为什么不跟上面那条 JVM 参数走"点应用才存"：那边怕的是"半条参数进命令行"，
    //       半条 JVM 参数会让游戏起不来；这里存的只是一行**显示用的**名字，半路的名字
    //       没有任何副作用，而"打着字就能看见条上变了"正是这一项该有的手感。
    //       代价是每个字符一次 QSettings 写（最多 titleTextMaxLength() 次），可以接受。
    connect(m_ui->getTitleTextEdit(), &QLineEdit::textChanged, this,
            &MainWindow::onTitleTextChanged);
    // CurseForge 的 API key：**编辑完才存**（editingFinished = 回车或者焦点离开）。
    // 理由写在 onCurseForgeApiKeyEdited() 里。
    connect(m_ui->getCurseForgeApiKeyEdit(), &QLineEdit::editingFinished, this,
            &MainWindow::onCurseForgeApiKeyEdited);
    // 四个「版本名格式」勾选框共用一个槽：归一化要同时看四个值。
    const auto nameOptionChanged = [this] { onVersionNameOptionChanged(); };
    connect(m_ui->getVersionNameCustomCheck(), &QCheckBox::toggled, this, nameOptionChanged);
    connect(m_ui->getVersionNameRealCheck(), &QCheckBox::toggled, this, nameOptionChanged);
    connect(m_ui->getVersionNameLoaderCheck(), &QCheckBox::toggled, this, nameOptionChanged);
    connect(m_ui->getVersionNameLoaderVersionCheck(), &QCheckBox::toggled, this,
            nameOptionChanged);

    // 选项卡切换：底栏左下角的显示规则依赖"当前是不是在主页"，切页就得重算；
    // 第一次切到设置页还要顺带扫一遍 Java。
    connect(m_ui->getTabWidget(), &QTabWidget::currentChanged, this,
            &MainWindow::onMainTabChanged);

    // 登录三态都要在界面上有反应。以前只接了 loginSucceeded，而且它刷的还是同一个占位名
    // （AuthManager 那会儿没有真名），于是"登录成功"这件事在启动器上一点痕迹都没有：
    // 用户点完登录、在浏览器里看到「登录成功」，回到启动器还是原样，像什么都没发生。
    connect(m_authManager, &AuthManager::loginStarted, this, &MainWindow::onLoginStarted);
    connect(m_authManager, &AuthManager::loginSucceeded, this, &MainWindow::onLoginSucceeded);
    connect(m_authManager, &AuthManager::loginFailed, this, &MainWindow::onLoginFailed);

    // 玩家外观（主页右栏那个小人 + 底栏头像）。三条都只走这一处 —— 数据层不知道界面
    // 长什么样，界面不知道贴图从哪来。
    connect(m_skinStore, &PlayerSkinStore::profileReady, this, &MainWindow::onSkinProfileReady);
    connect(m_skinStore, &PlayerSkinStore::textureReady, this, &MainWindow::onSkinTextureReady);
    connect(m_skinStore, &PlayerSkinStore::failed, this, &MainWindow::onSkinFailed);
    // 离线账号那张随机皮肤。**同一个来源、两条降级路**：skinReady 之后走的是上面那三个
    // 信号一样的界面路径（挑出来的地址就是官方 CDN 上的地址）；failed 也照样只记日志 ——
    // 但**不能直接接 onSkinFailed**，那句话的前缀是"取玩家皮肤失败"，而这一条失败的是
    // "挑一张"这件事，跟某个人的皮肤没关系。
    connect(m_randomSkinStore, &RandomSkinStore::skinReady, this, &MainWindow::onRandomSkinReady);
    connect(m_randomSkinStore, &RandomSkinStore::failed, this, [this](const QString &message) {
        appendLog(tr("取随机皮肤失败：%1").arg(message));
        m_skinFetchInFlight = false;
        updateSkinHint();
    });

    // 游戏进程信号。三条都带 sessionId（哪一份会话说的）—— 同时开着好几份时，日志里
    // 那句"哪来的"和底栏那格"还有谁在跑"全靠它归属。
    connect(m_launcherManager, &LauncherManager::processOutput, this, &MainWindow::onProcessOutput);
    connect(m_launcherManager, &LauncherManager::processError, this, &MainWindow::onProcessError);
    connect(m_launcherManager, &LauncherManager::gameFinished, this, &MainWindow::onGameFinished);

    // 版本管理器信号
    // NOTE: 这里以前接的是 MainWindow 自己 new 出来的一个 DownloadManager，
    // 但从来没有任何地方在它上面发起过下载 —— 真正的下载全走 VersionManager
    // 内部的 DownloadManager。于是 "下载完成" 那个弹窗从来没有被触发过，
    // 用户看着刷屏的日志以为还没下完就去点启动。现在只接真正在下载的那条链路。
    connect(m_versionManager, &VersionManager::versionsFetched, this,
            &MainWindow::onVersionsFetched);
    connect(m_versionManager, &VersionManager::versionDetailsFetched, this,
            &MainWindow::onVersionDetailsFetched);
    connect(m_versionManager, &VersionManager::versionDownloadStats, this,
            &MainWindow::onVersionDownloadStats);
    connect(m_versionManager, &VersionManager::versionDownloadFinished, this,
            &MainWindow::onVersionDownloadFinished);
    // 下载页那几类资源（模组/数据包/资源包/光影）里挑中的那个版本下完了。**和上面那条分开**
    // （理由见 VersionManager 里那个信号的 NOTE）：这条路上的 label 不是"哪个版本"，
    // 接进来的是另一个槽，那个槽不碰任何实例的安装状态。
    connect(m_versionManager, &VersionManager::resourceDownloadFinished, this,
            &MainWindow::onResourceDownloadFinished);
    // 加载器安装中间那几步（拉列表、挑版本、先补原版）只写日志、不弹窗：那几件事都在
    // "一次点击"里面，每步弹一个框等于点着玩。真正要用户做决定的（还没做、失败了）
    // 才走 versionDownloadFinished 那条弹窗的路。
    connect(m_versionManager, &VersionManager::loaderInstallMessage, this,
            [this](const QString &text) { appendLog(text); });
    // 整合包导入的中间几句（装加载器、下清单那批文件、解覆盖文件）同上：只写日志、不弹窗。
    // 真正要用户做决定的（装完了、失败了）走 versionDownloadFinished 那条弹窗的路。
    connect(m_versionManager, &VersionManager::modpackImportMessage, this,
            [this](const QString &text) { appendLog(text); });
    // 装"一个带名字的实例"中间那几句（正在装原版 / 正在装加载器）同上：只写日志、不弹窗。
    connect(m_versionManager, &VersionManager::instanceInstallMessage, this,
            [this](const QString &text) { appendLog(text); });
    // 某个加载器在那个游戏版本上有哪些版本可选（安装页上点开一条加载器 → 拉列表 → 填
    // 它右边那个组合框）。**每次请求都会回一次**，失败也回 —— 失败时那句原因要写进
    // 组合框的占位文案里（不能让用户对着一个永远空的框等下去）。
    // 这里那次"枚举 → 下标"的翻译也在这条链路上做一次：槽收的是下标（见头里的说明），
    // 而信号带过来的是枚举，中间只有 VersionManager::loaderIndexForKind 这一处。
    connect(m_versionManager, &VersionManager::loaderVersionsFetched, this,
            [this](const QString &gameVersion, VersionManager::LoaderKind kind, bool success,
                   const LoaderMeta::LoaderVersionList &versions, const QString &errorMessage) {
                const int loaderIndex = VersionManager::loaderIndexForKind(kind);
                if (loaderIndex < 0) {
                    return;  // 枚举里新来的那条界面还没跟上（表没改全），当没收到
                }
                onLoaderVersionsFetched(gameVersion, loaderIndex, success, versions, errorMessage);
            });
}

void MainWindow::enterSyncWait(const QString &statusText) {
    // 这一段里 UI 线程在等网络或等进程（见 .hpp 里那三种情况），Qt 不重绘、不响应输入，
    // Windows 过几秒还会给标题挂上"(无响应)"——用户看到的就是"点了启动就卡死"。
    // 这里能做的只有"进去之前先把话说出来"：底栏那格写上在等什么，再套一个忙指针。
    m_syncWaitStatus = statusText;
    updateBottomBar();
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    // 不刷这一次，上面那些改动要等窗口重新绘制时才看得见 —— 而那要等到这段路走完。
    // 排掉用户输入是**必须的**：不排的话这次 processEvents() 会把用户在这期间的点击
    // （比如"再点一次启动"）送回来，于是同一段路走两遍，比卡着更糟。
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void MainWindow::leaveSyncWait() {
    m_syncWaitStatus.clear();
    QGuiApplication::restoreOverrideCursor();
    updateBottomBar();
}

void MainWindow::setDownloadUiBusy(bool busy) {
    m_downloadInFlight = busy;
    // ---------- 这几项**故意不灰**（2026-09-23 多账户多实例那批改的） ----------
    // 启动按钮、「已安装版本」下拉框、实例页那张树，以前是跟着 busy 一起灰的。放开它们的
    // 理由是同一条：**"在下载"和"能启动什么"是两件事**，而且现在按版本分开了 ——
    // 正在装的那个从 launchable 里滤掉（见 installingVersionIds()），列表里剩下的
    // 都是文件齐的。所以"界面显示选的 A、实际在下的却是 B"这种误判在做完过滤之后
    // 不可能发生，而"装 1.20.1 的时候开一个早就装好的 1.18.2"是用户明确要的。
    //
    // 真正危险的那一种由 onStartButtonClicked() 里那道**按版本**的闸口兜住。
    //
    // 安装页那一整层（名称框、四个加载器、安装按钮、返回）**照样灰**：在装什么是点击
    // 那一刻定下的，这时候改名字/换加载器并不会把手上这次安装换掉（真正干活的
    // VersionManager 已经拿着那个枚举在跑了），留着能改只会让人以为改生效了。
    m_ui->getInstallButton()->setEnabled(!busy);
    m_ui->getInstallNameEdit()->setEnabled(!busy);
    m_ui->getVersionTree()->setEnabled(!busy);
    // 从安装页上退回去也不给（退回去那棵树也是灰的）：那会让人以为"这次安装取消了"。
    m_ui->getInstallBackButton()->setEnabled(!busy);
    // 「导入整合包…」也一起灰：导入期间再点一次，选文件和问名字那两个框走完，
    // 数据层才会用一句"已有任务正在进行"把它顶回来 —— 白走一趟对话框。
    m_ui->getImportModpackButton()->setEnabled(!busy);
    // 下载页「整合包」那一页上那颗是**同一件事的另一个入口**（同一个槽，见
    // setupConnections 里那条），所以它得跟着一样灰 —— 一个灰着、另一个亮着，用户点的
    // 正好是亮着那颗，走的是同一条被顶回来的路。
    if (QPushButton *downloadImport = m_ui->getDownloadFilterImportModpackButton()) {
        downloadImport->setEnabled(!busy);
    }
    // 实例页左边那列也一起灰：换游戏文件夹 = 换一整套 .minecraft，下载中途换过去
    // 会让这一批文件一半写进 A、一半写进 B。两个按钮的可用性由 updateFolderButtons()
    // 一处说了算（「移除文件夹」还要看选中的是不是自带那份）。
    m_ui->getFolderList()->setEnabled(!busy);
    // 「模组加载器」那棵树一起灰，理由同上。忙完解禁走 updateLoaderRows() 一处重算
    // （那棵树的样子全由它一处决定）；「安装」那个按钮还要看名称合法不合法，走
    // updateInstallNameHint() 一处。
    m_ui->getLoaderTree()->setEnabled(!busy);
    updateLoaderRows();
    updateInstallNameHint();
    // 重灌一遍"能启动的实例"：上面那条过滤（在装的那个先不出现）要立刻生效 ——
    // 置忙那一刻它该从列表里消失，置闲那一刻它该回来（成功的话还在 onVersionDownloadFinished
    // 里被选中，那句也在这个函数之后跑）。
    refreshInstalledVersions();
    updateFolderButtons();

    QProgressBar *progressBar = m_ui->getProgressBar();
    if (busy) {
        // 还不知道总任务数：先转成不确定态的滚动条，拿到 stats 再换成确定态。
        // 顺手把上一批留下的数字清掉 —— 不清的话"置忙"到"第一次 stats"之间那一小段，
        // 进度条会是新批次的不确定态、而记的数还是上一批的，两者对不上。
        m_downloadDone = 0;
        m_downloadTotal = 0;
        m_downloadFailed = 0;
        progressBar->setRange(0, 0);
        m_ui->getStatusLabel()->setText(tr("正在准备下载..."));
    } else {
        // 这一批结束了，记下的进度一起作废：再留着的话，下一批还没报数时那一支会拿
        // 上一批的数去还原（"已经下了 3000/3580"出现在一个刚点下去的下载上）。
        m_downloadDone = 0;
        m_downloadTotal = 0;
        m_downloadFailed = 0;
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
    }
    // 这一格的显隐**不在这里定**：底栏中间那格要同时受"在下载"和"在启动"两条影响，
    // 只有 updateBottomBar() 一处说了算，免得两处各改一次互相盖掉。
    updateBottomBar();
}

void MainWindow::updateBottomBar() {
    // ---------- 左：头像 + 用户名 ----------
    // 登录过就是档案里的真名（登录那一刻拿到的，或者开窗时从盘上读回来的那份）；
    // 没登录是占位的 "Player" —— 这里不自己造假名字，也不去猜。
    const QString name = m_authManager->playerName();
    m_ui->getAccountLabel()->setText(name);
    // 名字下面那行：这次启动会用哪种身份。**判据只看一件事**（当前账号是不是离线账号），
    // 不自己推"应该用哪个" —— 真正的判据在 onStartButtonClicked() 里，两处一旦各写一套
    // 就会出现"底栏说正版、启动时却说请先登录"。现在两处读的是**同一个函数**
    // （offlineIdentity()），连"各写一套"的可能都没有了。
    // NOTE: loggedIn() 是"盘上有账户"的意思（AuthManager::loggedIn() = uuid 非空），
    //       令牌过期了它照样是 true —— 那没关系：这一行说的是**身份**，令牌过没过期是
    //       启动那一刻刷新才知道的事（那时失败会有弹窗说清原因）。
    m_ui->getAccountTypeLabel()->setText(
        offlineIdentity()
            ? MainWindowUI::accountTypeOfflineText()
            : (m_authManager->loggedIn() ? MainWindowUI::accountTypeOnlineText()
                                         : MainWindowUI::accountTypeNotLoggedInText()));
    // 头像优先用**真皮肤**抠出来的那一块（m_avatarPixmap，在 applyPlayerSkin() 里算好
    // 存着 —— 这里每次进度刷新都会被调到，不能现算）。没有它的时候（未登录、还没取到、
    // 取不到、贴图载不进来）退回首字母方块：退路必须留着，一块空白的头像比一个首字母
    // 更像"界面坏了"。
    // NOTE: setPixmap()/setText() 都会把另一份内容清掉（QLabel 只显示最后设的那一个），
    //       所以这里不用自己清。
    if (m_avatarPixmap.isNull()) {
        m_ui->getAvatarLabel()->setText(name.isEmpty() ? QStringLiteral("?")
                                                       : name.left(1).toUpper());
    } else {
        m_ui->getAvatarLabel()->setPixmap(m_avatarPixmap);
    }

    // ---------- 中：进度（启动优先于下载） ----------
    // 两件事都会占这一格，启动优先 —— 下载可以慢慢来，用户刚点完启动那几秒需要反馈。
    QProgressBar *progressBar = m_ui->getProgressBar();
    QLabel *statusLabel = m_ui->getStatusLabel();
    // 三种"在忙"都占这一格：启动中、下载中、以及在同步等待段里（见 enterSyncWait()）。
    // **归谁**这条优先级放在 MainWindowUI::bottomBarProgressOwner() 里，那儿是它唯一的
    // 出处 —— 下载进度那条回调也拿它判"现在该不该我说话"（见 updateDownloadProgressUi）。
    const bool launching = !m_launchingSessions.isEmpty();
    const auto owner = MainWindowUI::bottomBarProgressOwner(
        launching, m_downloadInFlight, !m_syncWaitStatus.isEmpty());
    const bool showProgress = owner != MainWindowUI::BottomBarProgressOwner::None;
    progressBar->setVisible(showProgress);
    statusLabel->setVisible(showProgress);
    if (owner == MainWindowUI::BottomBarProgressOwner::Launch) {
        // 启动进度没有可测量的百分比（JVM 和游戏自己在初始化，没有任何进度回调），
        // 所以是不确定态的滚动条。等游戏吐出第一行输出（或进程结束）就收起来。
        progressBar->setRange(0, 0);
        statusLabel->setText(MainWindowUI::launchingStatusText(launchingInstanceName()));
    } else if (owner == MainWindowUI::BottomBarProgressOwner::Blocking) {
        // 同步等待那一段：不知道要等多久（网络 / 下 Java / 进程启动），所以也是不确定态。
        // 文字就是进那一段之前写下的"在等什么"。
        progressBar->setRange(0, 0);
        statusLabel->setText(m_syncWaitStatus);
    } else if (owner == MainWindowUI::BottomBarProgressOwner::Download) {
        // 下载还在跑，这一格还回给它 —— 照**上次记下的进度**还原，不是按回 0。
        //
        // NOTE: 以前这里只写"没在下载就复位"，因为"在启动"和"在下载"是互斥的：启动
        //       那一段开始时下载一定已经停了。解耦之后启动是从下载态里**借走**这一格，
        //       借完必须原样还回去（游戏吐出第一行输出时走到这一支，见 onProcessOutput）。
        //       还成一根 setRange(0,100) / setValue(0) 的空条，用户看到的就是"下载没了"。
        if (m_downloadTotal > 0) {
            progressBar->setRange(0, m_downloadTotal);
            progressBar->setValue(m_downloadDone);
            QString text = tr("已下载 %1 / %2").arg(m_downloadDone).arg(m_downloadTotal);
            if (m_downloadFailed > 0) {
                text += tr("，失败 %1").arg(m_downloadFailed);
            }
            statusLabel->setText(text);
        } else {
            // 这一批还没报过数（刚点下载、版本 json 还在路上）：那一段本来就是不确定态。
            progressBar->setRange(0, 0);
            statusLabel->setText(tr("正在准备下载..."));
        }
    } else {
        // 真的没在下载了：把进度条复位，下一次下载开始前它会先显示上次的旧值，闪一下。
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
        statusLabel->clear();
    }
    // 下载态下的字**文件名那一截**由 updateDownloadProgressUi() 维护（它每 200 ms 来一次，
    // 这里不拼那一截，免得两处各说各的）；上面这一支只把"已下载 d / t"这个数还原回来。

    // ---------- 右：当前实例运行状态 ----------
    QLabel *runStateLabel = m_ui->getRunStateLabel();
    QLabel *runStateDetailLabel = m_ui->getRunStateDetailLabel();
    QLabel *runStateMarkLabel = m_ui->getRunStateMarkLabel();
    const QList<LauncherManager::GameSession> games = m_launcherManager->runningGames();

    // 三态的顺序是**唯一的判据**（放这里当唯一出处，下面三支只负责画）：
    //   · 正在启动优先 —— 那一段里进程已经起来了（所以 runningGames() 里已经有它），
    //     但用户等的是"游戏窗口出来"，这时候该说的是"启动中"（黄），不是"运行中"（绿）；
    //   · 然后是"一个都没有"（红）；
    //   · 剩下的就是真的在跑（绿）。
    //
    // NOTE: 判据里**没有**"选中的是哪个实例"——用户 2026-09-24 要的是"这台机器上有没有
    //       实例在跑"，那件事跟下拉框选着谁没关系（第二行的显示规则才看那个，见
    //       runStateShowsDetail()）。
    const MainWindowUI::RunStateMark mark =
        launching ? MainWindowUI::RunStateMark::Launching
                  : (games.isEmpty() ? MainWindowUI::RunStateMark::NotRunning
                                     : MainWindowUI::RunStateMark::Running);
    // 那颗方块的图**每次重画都重新要一张**（VersionIcons 里按"边长+颜色"缓存着，
    // 所以这一步不贵）：状态变了颜色就得跟着变，而这里正是"状态唯一定下来"的地方。
    //
    // 调色板取**底栏**的，不取那颗按钮的：按钮上挂着一份 QSS（applyBottomBarButtonStyle），
    // 而带样式表的控件解析出来的 QPalette 里 Window 那个角色是**黑**的（实测亮度 0），
    // 拿它判深浅就会在浅色主题下也退回深色那一档的取色。方块画在底栏那块底上，
    // 底栏的调色板才是"它背后那块布"的颜色 —— 深色主题下它自己会跟着变。
    runStateMarkLabel->setPixmap(
        MainWindowUI::runStateMarkPixmap(mark, m_ui->getBottomBar()->palette()));
    runStateMarkLabel->setVisible(true);

    // 左下那一格**现在点不点得开**。判据就是"左面板里会不会有行"—— 那几行是拿
    // runningGames() 翻出来的（见 setBottomBarPanelContent() 里那个 provider），所以
    // 空不空和这里**同源**。用户 2026-09-25 原话：「没有运行中的实例时让那个实例栏点不开，
    // 箭头隐藏」。
    // NOTE: 黄灯那一档（"正在启动"）在这里也是**点得开**的：那一段里会话已经在
    //       runningGames() 里了（见上面三态那段注释），面板里列得出它。
    m_ui->setRunPanelAvailable(!games.isEmpty());

    if (mark == MainWindowUI::RunStateMark::Launching) {
        // 黄：正在启动。名字的写法**不看「版本名格式」那四个开关**（见 launchingStatusName()）。
        runStateLabel->setText(MainWindowUI::runStateLaunchingText(launchingStatusName()));
        runStateLabel->setVisible(true);
        // 第二行在这一档**一律不显示**：第一行那句话里已经带着实例名了（判据在
        // runStateShowsDetail() 里，和别的那几种情况同一处）。
        runStateDetailLabel->setText(QString());
        runStateDetailLabel->setVisible(false);
        return;
    }

    if (games.isEmpty()) {
        // 没有实例在跑：写「当前没有运行中的实例」。**不是藏起来** —— 用户 2026-09-23 要的
        // 就是这句话，而且那一格现在还兼着"点开看实例列表"那颗按钮，藏起来连按钮一起没了。
        // NOTE: 连带的好处是那一格的文字**任何时候都非空**，布局上不再有"从无到有"这一步
        //       （那一步正是"中间那根条突然缩短一截"的来源）。
        runStateLabel->setText(MainWindowUI::runStateNotRunningText());
        runStateLabel->setVisible(true);
        runStateDetailLabel->setText(QString());
        runStateDetailLabel->setVisible(false);
        return;
    }

    // 只有一份在跑时用老那句话（"实例运行中"），多份时报个数。同一句话在两个地方
    // 各拼一半，就会出现"跑了三个却说实例运行中"这种没人看得出错的说法。
    runStateLabel->setText(games.size() == 1
                               ? MainWindowUI::runStateRunningText()
                               : MainWindowUI::runStateRunningCountText(games.size()));
    runStateLabel->setVisible(true);

    if (games.size() == 1) {
        // 第二行要不要出，判据在 runStateShowsDetail() 里（那里也是它唯一的出处）。
        const bool showDetail = MainWindowUI::runStateShowsDetail(
            m_ui->isHomeTabCurrent(), selectedVersionId(m_ui->getInstalledVersionCombo()),
            games.first().versionId, launching);
        runStateDetailLabel->setVisible(showDetail);
        runStateDetailLabel->setText(showDetail ? instanceDisplayName(games.first().versionId)
                                                : QString());
        return;
    }

    // 两份以上：这一行**一定**要出，而且**必须带上身份**（"哪个实例是哪个账号"正是
    // 多开这件事本身要回答的问题 —— 三个同版本实例跑在三个账号上时，光列名字分不出谁是谁）。
    // NOTE: 只在这一支里带账号，是想让"只开了一个"那种绝大多数情况下的界面一个字都不变。
    QStringList lines;
    lines.reserve(games.size());
    for (const LauncherManager::GameSession &game : games) {
        const QString name = instanceDisplayName(game.versionId);
        lines.append(game.accountName.isEmpty()
                         ? name
                         : MainWindowUI::runStateDetailWithAccount(name, game.accountName));
    }
    runStateDetailLabel->setVisible(true);
    runStateDetailLabel->setText(lines.join(QStringLiteral("、")));
}

void MainWindow::startPlayerSkinLoad() {
    // 先摆默认外观。**这一步之后，右栏那个小人任何时候都不是空白的** —— 未登录、
    // 还没取到、取不到，看到的都是它。默认外观是程序画的经典配色（见
    // SkinModel::defaultSteveSkin()），不是把官方那张 Steve 贴图打进仓库。
    applyPlayerSkin(QString(), QString(), false);
    // 上一次那个账号的档案要清掉：换账号登录时它已经没用了，留着会让"贴图回来时
    // 认领"那一步把上一套外观当成这一套。
    m_skinProfile = PlayerSkinProfile();
    // 同理：下面重新算出来之前，"这张脸是按哪个版本算的"这件事不能留着上一轮的值 ——
    // 留着会让右栏那句话拿旧版本的显示名去说新的一张图。
    m_offlineDefaultSkinVersion.clear();

    // ---------- 离线账号那一档：从库里的随机挑一张 ----------
    //
    // 这一档必须在下面 !loggedIn() **之前**：离线账号必然同时是"没有登录态"
    // （AuthManager::setOfflinePlayer() 把 uuid 和令牌一起清了，见 AuthManager.cpp），
    // 摆在那句后面就等于永远走不到 —— 以前就是这样，离线账号的预览永远是那张写死的
    // 内置 Steve。
    //
    // 判据是 offlineIdentity()（当前账号本身是不是离线账号），不是"有没有勾某个框"。
    // **未登录（压根没有当前账号）不走这一档**：没有身份就没有可记的地方（那张图要按
    // 账号记在盘上），也没人可挑。
    if (offlineIdentity()) {
        // ① 先算"游戏会给这个名字分配哪一张官方默认皮肤"（见 DefaultSkin）：那个"随机"其实
        //    是确定的，图就在**本地那个版本的客户端 JAR** 里，一个字节都不用下。同一个
        //    (账号, 版本) 算过一次之后连 JAR 都不碰（缓存命中 = 两次 QFile::exists）。
        //
        //    名字取的是**进 --username 的那一个**（m_authManager->playerName()）而不是
        //    AccountStore 里那份存档：游戏是拿前者算 uuid 的，差一个字符就是另一张脸。
        //    版本取的是当前选中的那个实例 —— 换版本这张脸真的会变（两套规矩算出来的不是
        //    同一张，见 DefaultSkin.hpp 文件头），所以换版本时下面有一处会重跑这一整段。
        const QString versionId = selectedVersionId(m_ui->getInstalledVersionCombo());
        QString defaultPath;
        bool defaultSlim = false;
        QString defaultReason;
        const DefaultSkin::Result resolveResult = DefaultSkin::resolve(
            m_authManager->playerName(), versionId, &defaultPath, &defaultSlim, &defaultReason);
        if (resolveResult == DefaultSkin::Result::Resolved) {
            m_offlineDefaultSkinVersion = versionId;
            // 这张图**不是一份档案**（它没有地址、离线账号也没有披风），所以 m_skinProfile
            // 保持空着：上一轮若有别人的贴图回来，地址对不上就丢掉了（见 onSkinTextureReady）。
            // NOTE: 这条路上 m_skinFetchInFlight 一直是 false —— 图是从盘上（或 JAR 里）
            //       直接拿的，不是"在路上"，那句话不该说"正在获取"。
            applyPlayerSkin(defaultPath, QString(), defaultSlim);
            return;
        }
        // Unavailable 是**常态**：没装任何版本、或者那个目录里没有客户端 JAR（不是原版
        // 客户端，第三方安装器做的薄壳 JAR 就长那样）—— 都不值得报。Failed 才是真出错
        // （读 JAR 失败、写不出去），那种留一行日志，不然"怎么老是随机那张"无从查起。
        if (resolveResult == DefaultSkin::Result::Failed) {
            appendLog(tr("算离线默认皮肤失败：%1").arg(defaultReason));
        }

        // ② 算不出来才走"随机挑一张"。
        //
        // 身份用的是 AccountStore 里那个 uuid，**不是** m_authManager->playerUuid() ——
        // 后者对离线账号返回空串（它的 m_uuid 被清掉了）。离线账号的 uuid 是
        // md5("OfflinePlayer:" + 名字)，同名就是同一个 uuid，所以同一个人换不掉那张脸。
        const QString accountUuid = AccountStore::activeUuid();
        const QString chosen = RandomSkinStore::loadChosenSkinUrl(accountUuid);
        if (chosen.isEmpty()) {
            // 还没挑过（第一次用这个账号）→ 要一页去。回来之后 onRandomSkinReady()
            // 接着走下面这段。这一格点亮是为了让那句话说"正在获取"。
            m_skinFetchInFlight = true;
            // ★ 2026-09-27：把**发请求时这位是谁**记下来。回来的时候用户可能已经切到另一个
            //   离线账号了，而那两个都是"离线身份" —— 只判 offlineIdentity() 分不出来，
            //   详见 onRandomSkinReady() 里那段。
            //   NOTE: 只在**真的发出去了**的时候才记（fetchRandomSkin() 那个返回值）。已经有
            //         一趟在飞时这一趟什么都没开始，改掉这一位等于把上一趟的结果认到这一位
            //         名下 —— 那正是要防的那个错（切账号时这条路必被走到）。
            if (m_randomSkinStore->fetchRandomSkin()) {
                m_randomSkinRequestUuid = accountUuid;
            }
        } else {
            // 挑过了，还是那一张。**当成一份档案用** —— 于是下不下得来、存哪、怎么画
            // 全是现成那条路（requestSkinTexture → fetchTexture → onSkinTextureReady）。
            // 离线账号没有披风，所以 capeUrl 留空。
            m_skinProfile =
                PlayerSkinProfile{chosen, QString(), QStringLiteral("classic")};
            requestSkinTexture(chosen, /*allowDownload=*/true);
        }
        updateSkinHint();
        return;
    }

    if (!m_authManager->loggedIn()) {
        // 没登录就没有 uuid，连问都不用问（问也问不出东西来）。那句话现在是
        // "未登录，显示的是默认外观"，由上面那次 updateSkinHint() 写好。
        return;
    }
    const QString uuid = m_authManager->playerUuid();
    // 当前账号是离线账号的意思是"别联网"（启动时走的就是离线登录），这时候还发一个外观
    // 请求是自相矛盾的 —— 所以整条联网路径都跳过，只认已经躺在缓存里的那一份。
    //
    // NOTE: 判据从"勾选框勾没勾"换成了 offlineIdentity()。而且这一档现在**必然**同时是
    //       "没有登录态"（setOfflinePlayer() 把 uuid 和令牌一起清了），上面那句 !loggedIn()
    //       其实已经先返回了 —— 留着它是为了这条路自己读起来是完整的，不靠上面的早退。
    const bool offline = offlineIdentity();

    // 1) 盘上那份档案：开窗那一刻零网络把**上次**的样子画出来。贴图是内容寻址的，
    //    命中缓存时 fetchTexture() 是**同步**发信号回来的，所以这一句之后就画好了。
    PlayerSkinProfile cached;
    if (PlayerSkinStore::loadCachedProfile(uuid, &cached)) {
        m_skinProfile = cached;
        requestSkinTexture(cached.skinUrl, /*allowDownload=*/!offline);
        requestSkinTexture(cached.capeUrl, /*allowDownload=*/!offline);
    }

    // 2) 后台刷一次档案：用户在游戏里换了皮肤，重开启动器就能看到。回来那份和手上
    //    这份一样时 onSkinProfileReady() 什么都不做。
    if (!offline) {
        m_skinStore->refreshForUuid(uuid);
    }
    updateSkinHint();
}

bool MainWindow::requestSkinTexture(const QString &url, bool allowDownload) {
    if (url.isEmpty()) {
        // 这个人没有披风（多数账号都没有）。**常态**，不是错误，也不是失败。
        return false;
    }
    if (QFile::exists(PlayerSkinStore::cachePathForUrl(url))) {
        // 命中缓存：fetchTexture() 会同步发 textureReady 回来，所以这一句之后那张
        // 贴图已经在屏幕上了。
        m_skinStore->fetchTexture(url);
        return true;
    }
    if (allowDownload) {
        // 这一张要真的去下载了 —— 那句话得说"正在获取"，而不是"取不到"。
        m_skinFetchInFlight = true;
        m_skinStore->fetchTexture(url);
    }
    return false;
}

void MainWindow::onSkinProfileReady(const QString &uuid, const PlayerSkinProfile &profile) {
    // 回来的时候身份可能已经变了：用户在请求飞着的时候换了账号、删掉了那个账号，或者
    // 登出了。**不是同一个身份就整份丢掉** —— 这份档案是发请求时那个人的，照着它落盘
    // 会把 A 的脸写进 profile-<B>.json（那份文件跨重启生效：下次开窗先按它画出 A 的
    // 外观，直到下一次刷新成功才纠正），照着它画就是"切了账号，脸还是上一个人的"。
    //
    // NOTE: 和 onRandomSkinReady() 那道判据是同一件事 —— 都是"发请求时那位 == 现在这位"。
    //       区别在判据从哪来：那条路是信号自己带着身份回来的（uuid 就在形参里），随机皮肤
    //       那条没有，只好在发请求时自己记一份（m_randomSkinRequestUuid）。
    //       判据取 uuidKey()：它和 refreshForUuid() 里用的是同一个规整函数，所以"带不带横线"
    //       不会让这一比较失败。
    if (uuid.isEmpty() || uuid != PlayerSkinStore::uuidKey(m_authManager->playerUuid())) {
        return;
    }
    const bool sameAsBefore = profile.sameAs(m_skinProfile);
    const QString previousCapeUrl = m_skinProfile.capeUrl;
    m_skinProfile = profile;
    if (sameAsBefore && !m_skinPath.isEmpty()) {
        // 手上的就是这一份，而且已经在屏幕上 → 这次刷新**完全看不见**：不重下贴图、
        // 不重画、那句话也不动。用户在游戏里没换皮肤时，绝大多数刷新都走这一条。
        m_skinFetchInFlight = false;
        updateSkinHint();
        return;
    }

    QString error;
    // 落盘用**信号带回来的那个身份**，不用 m_authManager 里当下那个（上面那道闸之后
    // 它俩本来就相等了 —— 写 uuid 是让"这份是谁的"在读者眼里只有一处出处）。
    if (!PlayerSkinStore::saveProfile(uuid, profile, &error)) {
        // 存不下来不影响这一次显示（贴图照样下、照样画），只是下次开窗要先重取一遍。
        appendLog(tr("存玩家外观档案失败：%1").arg(error));
    }
    // 披风那一格只在**地址真的换了**的时候才作废。
    //
    // 作废是必须的：新档案里没有披风时（用户在游戏里把披风摘了），留着的那条旧路径会让
    // 下一趟 applyPlayerSkin(skin, m_capePath) 把上一套的披风又画回去，而且再没有哪一趟
    // 会来纠正它 —— 新的披风那一张根本不会到达。
    //
    // 但**不能一律清掉**：披风先到、皮肤还在路上的时候（见 onSkinTextureReady）那一格是
    // "记忆"，不是"现在画着的东西"，清了就等于把刚记住的那张丢了 —— 于是同一个 bug 换个
    // 位置又出现一次。所以判据是"地址换没换"，而不是"档案换没换"。
    //
    // 皮肤那一格不用管：它下一趟必被新贴图覆盖，留着还能让"新披风先到"那一趟有皮肤可配。
    if (profile.capeUrl != previousCapeUrl) {
        m_capePath.clear();
    }
    requestSkinTexture(profile.skinUrl, /*allowDownload=*/true);
    requestSkinTexture(profile.capeUrl, /*allowDownload=*/true);
    updateSkinHint();
}

void MainWindow::onSkinTextureReady(const QString &url, const QString &path) {
    // 两张贴图是**分开**回来的（皮肤一张、披风一张），所以这里做的是"把这一张记到它
    // 该去的那一格，再用手上这两格重画一遍" —— 谁先到都能画。
    const bool slim = m_skinProfile.variant == QStringLiteral("slim");
    if (!url.isEmpty() && url == m_skinProfile.skinUrl) {
        // 皮肤到手了：不再是"正在获取"。就算这张图后面载不进来，也该由那句
        // "取不到…"来说，而不是一直停在"正在获取"。
        m_skinFetchInFlight = false;
        // 随机挑的那一张**到这里才记账**（2026-09-25 修）：能走到这一句就说明字节已经在
        // 盘上了（这个信号的契约就是"本地就绪"，fetchTexture() 里那几条失败路一条都不发它），
        // 而那条记录是永不失效的 —— 记一条"图上没有"的地址，等于把这个账号钉死在重下同
        // 一个失败地址上。挑的那一刻只把它放进了 m_pendingRandomSkin*，见 onRandomSkinReady()。
        // NOTE: 记的是**挑的时候那个账号**（不是当下这个）：用户在这几百毫秒里换了账号时，
        //       这张图仍然是那个人挑的那一张，记在他名下才对（记错人就没用了 —— 下次
        //       没人会去读它）。两个都对得上才记，所以中途切过账号的情况这里天然放过。
        if (!m_pendingRandomSkinUrl.isEmpty() && url == m_pendingRandomSkinUrl) {
            QString error;
            if (!RandomSkinStore::saveChosenSkinUrl(m_pendingRandomSkinUuid, url, &error)) {
                // 记不下来不影响这一次显示（这一趟照样画得出来），只是下次开启动器会重挑一张。
                appendLog(tr("记下随机皮肤失败：%1").arg(error));
            }
            m_pendingRandomSkinUrl.clear();
            m_pendingRandomSkinUuid.clear();
        }
        applyPlayerSkin(path, m_capePath, slim);
        return;
    }
    if (!url.isEmpty() && url == m_skinProfile.capeUrl) {
        if (m_skinPath.isEmpty()) {
            // **披风先到了，皮肤还在路上**（披风那张小得多，经常先下完）。
            //
            // 这时候千万不能顺手 applyPlayerSkin(m_skinPath, path, slim) —— 皮肤那个参数
            // 是空的，而 applyPlayerSkin() 里"皮肤载不进来就退回默认外观"那条路会**把
            // 披风一起清掉**（默认外观本来就没有披风）。等皮肤那一趟回来时 m_capePath
            // 已经是空的，披风再也补不回来了：表现是**皮肤有了、披风永远没有**，而且
            // 每一趟都停在默认外观那句话上。2026-09-21 真机上就是这个现象。
            //
            // 所以这里只**记住**，不画：皮肤那一趟（上面那个分支）会带上 m_capePath
            // 一起画出来。这一小段时间里屏幕上是上一套外观（或者默认外观），不闪。
            m_capePath = path;
            // ★ 2026-09-27：皮肤那一趟**根本不会来**的时候（这个人的档案里没有皮肤那一格 ——
            //    Mojang 的档案里"只有披风、没有 SKIN"是可能的），这一位必须在这儿落回 false：
            //    它的意思是"还有图在下"，而下完的就是手上这一张、皮肤那一张从没被请求过。
            //    不落的话那句「正在获取…」会一直挂着 —— 没有任何一条路会再来纠正它（皮肤
            //    那个分支永远不会执行、也不会失败，因为没有请求发出去）。
            //    NOTE: 皮肤在路上的时候**不能**落（那才是正常的一小段），所以判据是
            //          "档案里根本没有皮肤地址"，而不是"m_skinPath 还空着"。
            if (m_skinProfile.skinUrl.isEmpty()) {
                m_skinFetchInFlight = false;
            }
            return;
        }
        applyPlayerSkin(m_skinPath, path, slim);
        return;
    }
    // 两个都对不上：这是一次**过期**的回调（档案已经换成别人了）。丢掉 —— 照着它画
    // 等于把上一个账号的皮肤画回来。
}

void MainWindow::onRandomSkinReady(const QString &skinUrl) {
    // 回来的时候身份可能已经变了：用户在这几百毫秒里换了账号、删掉了那个离线账号，
    // 或者干脆登录成正式账号了。**不是同一个离线身份就丢掉** —— 记下去等于把 A 的脸
    // 记到 B 名下，而且再也不会有人来纠正它（那份记录是"永不失效"的）。
    //
    // ★★ 2026-09-27 修：这里原来只有 `if (!offlineIdentity()) return;` 这一道，而它回答的
    //    是"现在这位**是不是**离线账号"，**不是**"是不是**刚才那一位**" —— 从离线 A 切到
    //    离线 B 之后（两个都满足那一句，而且这是最常见的切法）A 那一趟的结果照样走得下去：
    //      · m_pendingRandomSkinUuid 被写成 **B**（下面那句读的是"当下"），
    //      · 贴图到了以后 onSkinTextureReady() 就把 A 挑的那张记进 `random-<B>.txt`；
    //      · 而 A 名下永远不会有记录 ⇒ A 每次开启动器都重挑一张、脸一直变（那份记录存在的
    //        全部意义正是"挑一次就定住"）。
    //    判据换成"发请求时那位 == 现在这位"：`m_randomSkinRequestUuid` 是发请求那一刻写的
    //    （见 startPlayerSkinLoad() 里那一句），两个都是 AccountStore 给的同一个规整 uuid，
    //    直接比字符串就行。
    //
    //    NOTE: **没记过账的那一趟（这一位跑到现在一个请求都没发过）按当下这位收下**。
    //          两种收法是"当下这位"和"发请求时那位"的差别，而没有记账就没有"发请求时那位"
    //          可言 —— 那一趟的来路只能是"盘上已经记着一行，于是这一位从没挑过"（见
    //          startPlayerSkinLoad()：有记录就走 applyPlayerSkin，一个请求都不发），这时候
    //          它跟"当下这位"本来就是同一位。生产里回不来"没记账却有结果"这种组合（信号
    //          只有 fetchRandomSkin() 会发，而它一发就把那一位记上了），所以这一条不放松
    //          任何东西 —— 它挡的只是"把空串当成一个身份去比"。mw_check 的 AF5/AF6/AF7 就是
    //          直接踢这个槽函数来演"挑好的那张回来了"，那几趟正落在这个组合上。
    const bool requesterKnown = !m_randomSkinRequestUuid.isEmpty();
    if (!offlineIdentity() ||
        (requesterKnown && AccountStore::activeUuid() != m_randomSkinRequestUuid)) {
        // ★★ 2026-09-27：丢掉之后**再问一次**。这一趟是替上一位挑的，而当下这一位（离线）
        //    可能正等着自己那一张 —— 切账号时 startPlayerSkinLoad() 那一趟确实发出去了，
        //    但再切一次就会被 RandomSkinStore::fetchRandomSkin() 的 m_inFlight 挡回来
        //    （那一趟返回 false，所以 m_randomSkinRequestUuid 没被改写，这里才认得出"过期"）。
        //    此刻 m_inFlight 已经落回 false（reply 的回调第一句就清掉了，而这里是它直接
        //    发信号叫进来的），所以这一句能真的发出去。
        //    NOTE: 只在**现在这位是离线账号**时才补 —— 正版账号的皮肤走官方那条路，
        //          切过去的时候已经有另一条路在刷了，不该由这里再捅一次。
        if (offlineIdentity()) {
            startPlayerSkinLoad();
        }
        return;
    }
    // 挑中的这一张**先记在内存里**，等贴图真的落在盘上再写那行记录（见
    // onSkinTextureReady 里那一段）。原来是在这里就写的，而这一句之前图一个字节都还没到：
    // requestSkinTexture() 会因为超时、非 200、或者"不是一张 64 宽的皮肤 PNG"
    // （PlayerSkinStore::fetchTexture）**一个字节都不落盘**，而记录已经写下了 —— 那条记录
    // 是永不失效的，于是这个账号从此每次都去重下同一条永远失败的地址、永远停在默认外观，
    // 而且"换一张"没有界面入口（要手动删 skins/random-<uuid>.txt）。记账的语义本来是
    // "这个人挑过哪一张、那张是好的"，那就该等它好。
    m_pendingRandomSkinUrl = skinUrl;
    m_pendingRandomSkinUuid = AccountStore::activeUuid();
    m_skinProfile = PlayerSkinProfile{skinUrl, QString(), QStringLiteral("classic")};
    // 走那条公用的路。**缓存命中时它是同步的**（fetchTexture 命中就同步发 textureReady），
    // 所以挑第二张的时候这一句之后屏幕上就已经换好了；真要下的时候它会自己把
    // m_skinFetchInFlight 点起来。
    requestSkinTexture(skinUrl, /*allowDownload=*/true);
    updateSkinHint();
}

void MainWindow::onSkinFailed(const QString &message) {
    // 外观是**装饰**：取不到就静默退回默认外观。这是全仓唯一一处不弹窗的错误，
    // 理由（别的错误都让用户做不成他想做的事，而这个只是不好看）写在
    // PlayerSkinStore.hpp 文件头。但日志得留一行，否则真出问题无从查起。
    appendLog(tr("取玩家皮肤失败：%1").arg(message));
    m_skinFetchInFlight = false;
    if (m_skinPath.isEmpty()) {
        // 已经有真皮肤在屏幕上时**不改那句话**：多数失败是**披风**那一张（皮肤早就画
        // 好了），这时候说"取不到皮肤"是错的。
        updateSkinHint();
    }
}

QPixmap MainWindow::accountFacePixmap(const SavedAccount &account) const {
    // ★ 这条路**只读本地已经躺着的东西**：不联网、不换令牌、不读客户端 JAR。
    //   面板是"点一下就弹出来"的，用户原话的前提也是「（因为选择/登录过，所以肯定会有缓存，
    //   应该不会遇到丢失的情况）」—— 所以这里按"缓存里有就用、没有就自己画一个"来收尾，
    //   而不是去把缺的那张拉下来（拉皮肤要网络、要令牌，还要为每一行各等一次）。
    const int size = MainWindowUI::bottomBarPanelIconSize();
    // 缓存图 → 头像。图上取不到（文件不在 / 不是一张皮肤）就是一张空图，让调用方接着退。
    const auto faceFromTexture = [size](const QString &path) -> QPixmap {
        if (path.isEmpty() || !QFile::exists(path)) {
            return {};
        }
        const QImage texture(path);
        if (texture.isNull()) {
            return {};
        }
        // headAvatar() 自己认 64×32 的老格式（那一档只取头和帽子，是同一块区域）。
        const QImage head = SkinModel::headAvatar(texture, size);
        return head.isNull() ? QPixmap() : QPixmap::fromImage(head);
    };

    // ① 当前账号：底栏那颗头像是**现成的**（m_avatarPixmap，applyPlayerSkin() 里算好的），
    //    缩一下就是这一行的图。
    //
    //    这一条必须排在最前面。它是"面板上这一行"和"底栏那颗"是同一张脸的**唯一保证** ——
    //    走下面那几条缓存路的话，当前账号正在"随机皮肤还没下回来"那一档时，面板会给出一张
    //    缓存里的旧图（或者干脆一个首字母方块），同一个窗口里同一个人两种脸。
    //    NOTE: 缩放的插值方式跟着 headAvatar() 那套走（最近邻）—— 那张图本来就是 8×8
    //          放大出来的像素画，平滑插值只会把脸糊掉。
    if (account.isCurrent && !m_avatarPixmap.isNull()) {
        return m_avatarPixmap.scaled(size, size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    if (!account.offline) {
        // ② 正版账号：缓存里那份档案写着他的皮肤地址，图按地址的 sha1 躺在同一个缓存目录里
        //    （PlayerSkinStore 的两级缓存，见那边的文件头）。地址是内容哈希，所以"本地有这张
        //    图"就等于"这是他当前的皮肤"。
        PlayerSkinProfile profile;
        if (PlayerSkinStore::loadCachedProfile(account.uuid, &profile) &&
            !profile.skinUrl.isEmpty()) {
            const QPixmap face = faceFromTexture(PlayerSkinStore::cachePathForUrl(profile.skinUrl));
            if (!face.isNull()) {
                return face;
            }
        }
    } else {
        // ③ 离线账号优先用**他自己挑的那张**（RandomSkinStore 记在盘上的地址）。挑过就有
        //    地址，图和正版走同一个缓存 —— 同一个账号在底栏、面板、主页看到的是同一张脸。
        //    NOTE: 认人用 account.uuid（离线账号的 uuid 是 md5("OfflinePlayer:"+名字)，
        //          和 startPlayerSkinLoad() 里读的是同一个值），不是 m_authManager 那个
        //          —— 后者对离线账号是空串。
        const QString chosen = RandomSkinStore::loadChosenSkinUrl(account.uuid);
        if (!chosen.isEmpty()) {
            const QPixmap face = faceFromTexture(PlayerSkinStore::cachePathForUrl(chosen));
            if (!face.isNull()) {
                return face;
            }
        }
        // ④ 再退一步：**算出来的**那张官方默认皮肤的缓存图（见 DefaultSkin）。
        //
        //    它的缓存名带三段（账号 / 版本 / 条目），所以这里得先猜"当初是按哪个实例算的"。
        //    猜错只是 QFile::exists 落空，**绝不会触发读 JAR** —— 那一条路要起一次
        //    PowerShell（几百毫秒），挂在"点开面板"这一下上是不行的。所以这里**直接拼
        //    cachePathFor()**，一步都不走 resolve()。
        const QStringList versions =
            offlineDefaultSkinCacheVersions(selectedVersionId(m_ui->getInstalledVersionCombo()));
        for (const QString &versionId : versions) {
            for (const DefaultSkin::Rule rule :
                 {DefaultSkin::Rule::Eighteen, DefaultSkin::Rule::Two}) {
                const DefaultSkin::Prediction prediction = DefaultSkin::predict(account.uuid, rule);
                if (!prediction.valid) {
                    continue;
                }
                const QPixmap face = faceFromTexture(
                    DefaultSkin::cachePathFor(account.uuid, versionId, prediction.entryPath));
                if (!face.isNull()) {
                    return face;
                }
            }
        }
    }

    // ⑤ 一张图都没有 —— 自己画一个首字母方块。**这是常态，不是出错**：从没登录过的正版
    //    账号（列表里那条只是个凭据副本）、刚加进来还没挑过皮肤的离线账号，本来就没有图。
    //    面板不能因为某一项没图就空着那一格（三行的行首一高一低比一个方块更难认）。
    return QPixmap::fromImage(SkinModel::letterAvatar(account.name, size));
}

QStringList MainWindow::offlineDefaultSkinCacheVersions(const QString &currentVersionId) const {
    // 猜"当初按哪个实例算的那张官方默认皮肤"，**按可能性从高到低**：
    //   · 上一轮真的算过的那一个（m_offlineDefaultSkinVersion）—— 最准的一个；
    //   · 现在选中的那个实例 —— 用户切到谁身上就按谁算；
    //   · 上次启动过的版本 —— 换过实例、或者那会儿没选中任何版本时的兜底。
    // 三档都空就是空表（那时候这条路直接落空，退回首字母方块）。
    // NOTE: 一个账号只要**当过**当前账号，它的那张图就躺在盘上，而这三种猜测里总有一个
    //       能命中 —— 这正是"面板里三个人的脸都能画出来"的来源。
    QStringList versions;
    const auto add = [&versions](const QString &versionId) {
        if (!versionId.isEmpty() && !versions.contains(versionId)) {
            versions.append(versionId);
        }
    };
    add(m_offlineDefaultSkinVersion);
    add(currentVersionId);
    add(m_settings->lastLaunchedVersion());
    return versions;
}

void MainWindow::applyPlayerSkin(const QString &skinPath, const QString &capePath, bool slim) {
    QImage skin;
    if (!skinPath.isEmpty()) {
        skin = QImage(skinPath);
    }
    QImage cape;
    if (!capePath.isEmpty()) {
        cape = QImage(capePath);
    }
    if (skin.isNull()) {
        // 载不进来（文件被删了、下了一半、或者路径本来就是空的）→ 退回默认外观。
        // 这是**正常的一条路**，不是异常：这条路上 UI 什么都不用说，右栏那句话会由
        // updateSkinHint() 按"手上有没有真皮肤"重新挑一档。
        skin = SkinModel::defaultSteveSkin();
        cape = QImage();
        m_skinPath.clear();
        m_capePath.clear();
    } else {
        // 只记**真的载入成功了的**路径：所以 m_skinPath 为空就等于"现在显示的是默认外观"。
        m_skinPath = skinPath;
        m_capePath = cape.isNull() ? QString() : capePath;
    }

    m_ui->getSkinViewer()->setSkin(skin, cape, slim);
    // 头像在这里**算一次存起来**：updateBottomBar() 每次进度刷新（几百毫秒一次）都会
    // 被调，在它里面现算等于一直在重渲染同一张 8×8 的皮肤。裁切和放大都在
    // SkinModel::headAvatar() 里（最近邻放大，保持像素画的那个方块感）。
    m_avatarPixmap = QPixmap::fromImage(SkinModel::headAvatar(skin, MainWindowUI::avatarSize()));
    // 那个窗里的 3D 预览跟着**同一份贴图**走：它和这里是同一个东西的两个窗口，
    // 各下各的、各画各的就会出现"主页换了、那个窗还是旧外观"。挂在 applyPlayerSkin()
    // 里是因为这是全仓**唯一**一处决定"现在显示的是哪一套外观"的地方（贴图从哪来、
    // 档案怎么换都不影响这条）。
    if (m_accountSkinDialog != nullptr) {
        m_accountSkinDialog->setLooks(skin, cape, slim);
    }
    updateSkinHint();
    updateBottomBar();
}

void MainWindow::updateSkinHint() {
    const QString name = m_authManager->playerName();
    QLabel *hint = m_ui->getSkinHintLabel();
    // 离线账号有一套自己的话（"正版外观"那句对它是错的，"未登录"那句也不对 ——
    // 它是**故意**不登录的，读起来像出错了）。判据只有一个：当前账号本身是不是离线账号。
    const bool offline = offlineIdentity();
    if (!m_skinPath.isEmpty()) {
        if (!offline) {
            hint->setText(MainWindowUI::skinHintLoadedText(name, !m_capePath.isEmpty()));
        } else if (!m_offlineDefaultSkinVersion.isEmpty()) {
            // 算出来的那张官方默认皮肤（见 DefaultSkin）：说得比"随机挑的"准一点 —— 这是
            // 那个版本的客户端本来就会给他分配的那一张。版本号**不是废话**：换版本这张脸
            // 真的会变（两套规矩算出来的不是同一张），那句话要能解释这件事。
            hint->setText(MainWindowUI::skinHintOfflineDefaultSkinText(
                name, instanceDisplayName(m_offlineDefaultSkinVersion)));
        } else {
            hint->setText(MainWindowUI::skinHintOfflineLoadedText(name));
        }
    } else if (offline) {
        // 那张随机外观还没画上：不是"还在要"就是"没要到"。这两句都不弹窗（理由同
        // PlayerSkinStore.hpp 文件头：外观是装饰，取不到也照样能启动游戏）。
        hint->setText(m_skinFetchInFlight ? MainWindowUI::skinHintLoadingText(name)
                                          : MainWindowUI::skinHintFailedText(name));
    } else if (!m_authManager->loggedIn()) {
        hint->setText(MainWindowUI::skinHintNotLoggedInText());
    } else if (m_skinFetchInFlight) {
        hint->setText(MainWindowUI::skinHintLoadingText(name));
    } else {
        hint->setText(MainWindowUI::skinHintFailedText(name));
    }
}

QString MainWindow::instanceDisplayName(const QString &versionId) const {
    // 用下拉框里那条文本，也就是用户看惯了的显示名（可能是"我的整合包-1.20.1-Forge"），
    // 而不是裸目录名。找不到（版本刚被删、或者压根不在本地）时退回版本号本身 ——
    // 底栏宁可显示一个用户没见过的目录名，也不能什么都不显示。
    const QComboBox *combo = m_ui->getInstalledVersionCombo();
    const int index = combo->findData(versionId);
    return index >= 0 ? combo->itemText(index) : versionId;
}

void MainWindow::updateDownloadProgressUi(int done, int total, int failed,
                                          const QString &currentFile) {
    QProgressBar *progressBar = m_ui->getProgressBar();
    QLabel *statusLabel = m_ui->getStatusLabel();

    // 记下来：启动那一段结束、这一格还回下载态时，updateBottomBar() 靠它还原，
    // 而不是把进度条按回 0（那会显示成一根没有内容的空条）。见 m_downloadDone 的说明。
    m_downloadDone = done;
    m_downloadTotal = total;
    m_downloadFailed = failed;

    // 这一格**现在不归下载**（启动中 / 在同步等待段里）就一个像素都不许动 —— 上面那几个
    // 数已经记下了，等那两档让开时 updateBottomBar() 会照它还原。
    //
    // NOTE: 2026-09-24 补的。以前这里无条件写进度条和文字，于是"边下边开"时
    //       enterSyncWait() 写的「正在校验登录状态…」「正在准备 Java 运行时…」和进程起来
    //       之后的「正在启动 X …」会在 200 ms 内被这一句顶掉（这一格的字只有一个标签，
    //       谁后写谁算数）—— 而那几句话正是 2026-09-22 专为"点了启动就像卡死"做的。
    //       判据和 updateBottomBar() 用的是**同一个**（bottomBarProgressOwner()）。
    if (MainWindowUI::bottomBarProgressOwner(!m_launchingSessions.isEmpty(), m_downloadInFlight,
                                             !m_syncWaitStatus.isEmpty())
        != MainWindowUI::BottomBarProgressOwner::Download) {
        return;
    }

    if (total <= 0) {
        progressBar->setRange(0, 0);
        statusLabel->setText(tr("正在准备下载..."));
        return;
    }

    progressBar->setRange(0, total);
    progressBar->setValue(done);

    QString text = tr("已下载 %1 / %2").arg(done).arg(total);
    if (failed > 0) {
        text += tr("，失败 %1").arg(failed);
    }
    if (!currentFile.isEmpty()) {
        // 路径可能很长（asset 目录层级深），只显示文件名
        text += QStringLiteral("  ") + QFileInfo(currentFile).fileName();
    }
    statusLabel->setText(text);
}

void MainWindow::onVersionDownloadStats(int done, int total, int failed,
                                        const QString &currentFile) {
    // 前缘节流：200 ms 内的后续更新直接丢掉。不这么做的话，5000 多个 asset 会疯狂重排
    // 状态栏，界面反而卡住。
    // NOTE: 丢掉的那些**没有**"结束时补刷"这回事 —— 进度条和状态栏停在上一次越过阈值的
    //       那一帧，最多比真实进度旧 200 ms。整批结束由 setDownloadUiBusy(false) 把进度条
    //       归零收尾，所以停在哪一帧不影响收尾。
    if (m_progressThrottle.isValid() && m_progressThrottle.elapsed() < 200) {
        return;
    }
    m_progressThrottle.start();
    updateDownloadProgressUi(done, total, failed, currentFile);
}

void MainWindow::appendLog(const QString &text) {
    // 界面里**不再有**日志面板（2026-09 分选项卡时去掉的）：命令行里本来就有
    // LauncherManager 的全套输出，多加一块面板只是把同一批东西抄第二遍。
    //
    // NOTE: 所以这条通道是"给人事后看"的，不是"必须让人看见"的。**必须让用户看到**
    //       的消息（缺 Java、下载失败、未选版本、找不到 version.json）一律走
    //       `InlineMessage`（`ui/InlineMessage.hpp`，全仓已无一处 `QMessageBox`）挡住
    //       操作，绝不能只写这里。★ 2026-09-27 更新：这里原来写的是"走 QMessageBox"，
    //       而那句话早就和代码相反了（内嵌化那一批把 90 来处全换掉了）—— 正是
    //       "注释逐字声称的那一侧才是洞"那个形状，所以顺手改掉。
    qDebug().noquote() << QStringLiteral("[WTFML]") << text;
}

QSet<QString> MainWindow::installingVersionIds() const {
    // 三个来源合起来才是"现在**不能启动**的那几个实例"。分开记是因为它们覆盖的时段
    // 不一样：VersionManager 那个只在一批文件真的在写的时候非空，而"点了导入、还在拉
    // 版本表"那几段是界面自己记的（数据层那会儿连批都还没开）。
    QSet<QString> ids;
    const QString busy = m_versionManager->busyVersionId();
    if (!busy.isEmpty()) {
        ids.insert(busy);
    }
    // 这两个只在"用户点的是它"那一刻置上、终态时清掉（见那两个成员的说明），覆盖面比
    // VersionManager 那个宽 —— 正好补上前奏那几段。
    if (!m_importingInstanceId.isEmpty()) {
        ids.insert(m_importingInstanceId);
    }
    if (!m_installingInstanceId.isEmpty()) {
        ids.insert(m_installingInstanceId);
    }
    // 「补全文件」那颗按钮（2026-09-26）：它**是**一批下载在跑，所以上面那个 busy 那一份
    // 通常已经盖住它了；记这一笔是给"点下去、批次还没起来"那一小段兜底（见头文件）。
    if (!m_repairingInstanceId.isEmpty()) {
        ids.insert(m_repairingInstanceId);
    }
    return ids;
}

void MainWindow::refreshInstalledVersions() {
    // 只扫本地磁盘（.minecraft/versions/<id>/<id>.jar），同步返回，随叫随到。
    //
    // 识别不出来的版本只在第一次上报：本函数一次会话会被调很多次（构造、版本清单到达、
    // 下载完成、每勾一下「版本名格式」都会走一遍），每回都报就是刷屏。第一次报过之后
    // 连扫都不扫了 —— 传 nullptr 时
    // installedVersions() 走的是和加这个参数之前完全一样的那条路径。
    QStringList warnings;
    const QStringList installed = m_versionManager->installedVersions(
        m_reportedUnrecognizedVersions ? nullptr : &warnings);
    if (!warnings.isEmpty()) {
        m_reportedUnrecognizedVersions = true;
        for (const QString &warning : warnings) {
            appendLog(warning);
        }
    }

    QComboBox *combo = m_ui->getInstalledVersionCombo();
    // 还在下载/安装的那几个**先不出现在启动选项里**：一份文件没下全的实例列在下拉框里，
    // 用户点它就是启动一个文件不全的版本（游戏崩在缺库上，而且看不出原因）。
    //
    // NOTE: 这一步同时把"启动"和"下载"两件事**按版本**分开了：装 1.20.1 的这十来分钟里，
    //       早就装好的 1.18.2 照样在列表里、照样能启动（见 onStartButtonClicked 里的闸口）。
    //       以前是"有任何一批下载在跑，启动按钮就整片灰掉+下拉框禁用"，那既拦不住真正
    //       危险的那一种，又把完全无关的另一种一起堵死了。
    const QSet<QString> installing = installingVersionIds();
    QStringList launchable;
    launchable.reserve(installed.size());
    for (const QString &id : installed) {
        if (!installing.contains(id)) {
            launchable.append(id);
        }
    }

    QList<VersionDisplayParts> parts;
    parts.reserve(launchable.size());
    for (const QString &id : launchable) {
        // 每个版本读一次 <id>/wtfml.meta（几百字节的可选文件，绝大多数版本没有）。
        // 刻意不缓存：这个文件会被"重命名"功能改写，缓存了就会变成"改了名不生效"。
        // 开销上不必担心 —— 真机上实例数可以到 61 个，而这个函数一次会话没有调用上界。
        parts.append(m_versionManager->displayParts(id));
    }
    // 第一次用这一份 .minecraft：把五个默认组（置顶 / 可安装mod / 原版 / 快照版 /
    // 远古版）种进去。
    //
    // 时机就选在这里：**有实例可摆**的时候才种（一个实例都没有时种下去，树里就是五个
    // 空组头加一个「未分组」，纯噪声）。这个函数每次刷实例都走一遍，但种只在文件还不
    // 存在的那一次真的发生（见 seedDefaultGroups()），代价是一次 QFile::exists。
    //
    // NOTE: 挡住信号。这一趟本来就要把树重画一遍（下面那一段），而 groupsChanged 接的
    //       正是本函数 —— 不挡的话会把自己再叫一遍，白扫一次盘。
    // NOTE: 只在文件不存在时种，所以这是**一次**的事：用户把那五个删光之后不会再长回来
    //       （"默认可以删"那条要求的实现就在 seedDefaultGroups() 里）。
    // NOTE: 种不下去（盘写不了）只说一句日志 —— 默认组是锦上添花，原来那种"没分过组就
    //       平铺"的样子照样能用，为它弹一个框反而吓人。
    if (!parts.isEmpty()) {
        const QSignalBlocker blocker(m_groupStore);
        QString seedError;
        if (!m_groupStore->seedDefaultGroups(&seedError)) {
            appendLog(seedError);
        }
    }

    // 主页上"已安装版本"只有下拉框这一个控件了（右栏那张列表 2026-09 去掉，见
    // MainWindowUI 里主页那一段），所以这里只灌下拉框，同步选中项也只剩实例页那张列表。
    fillVersionCombo(combo, parts, m_settings->versionNameOptions());

    // 实例页：顶上写清这是从哪一份 .minecraft 里扫出来的（左边换个文件夹它就跟着变），
    // 右边那张树灌同一批数据（按分组摆）。它本身不是"又一份数据源"，只是同一个东西
    // 摆在文件夹旁边更好看 —— 点击行为是"点一下等于在主页把它选中"（见 setupConnections）。
    const QString root = AppPaths::minecraftDir();
    QString folderText = tr("当前文件夹：%1").arg(QDir::toNativeSeparators(root));
    // 那个数字数的是**能启动的**（= 已经从 installed 里滤掉在装的那几个）：这一行是
    // 实例页顶上那句"这里有几个实例"，和树里数得出来的行数必须是同一个数 —— 报
    // installed.size() 的话，正在装的那一个会让这句话比树上多一个，看着像漏画了。
    folderText += QDir(root).exists() ? tr("（%1 个实例）").arg(launchable.size())
                                      : tr("（这个目录不在盘上了）");
    m_ui->getInstanceFolderLabel()->setText(folderText);

    // 按分组摆那张树。归属键走 instanceKey()（稳定 id，没有就退回目录名）。
    //
    // NOTE: 每个实例这里要读两次 wtfml.meta（上面 displayParts() 一次、这里 instanceId()
    //       一次）。看着浪费，但这个文件是几百字节的可选文件、真机上最多 61 个实例，
    //       而"把两者合并成一次读"要把 instanceId 塞进 VersionDisplayParts ——那是个
    //       纯显示用的结构，塞一个身份字段进去会让"显示名怎么拼"和"归属怎么认"纠缠在
    //       一起。分开读，代价是一次小文件打开。
    const QList<InstanceGroupStore::Group> groups = m_groupStore->groups();
    QList<InstanceGroupBucket> buckets;
    QHash<QString, int> bucketOfGroup;
    const QSet<QString> knownGroupIds = knownGroupIdsOf(m_groupStore);
    for (const InstanceGroupStore::Group &group : groups) {
        InstanceGroupBucket bucket;
        bucket.groupId = group.id;
        bucket.name = group.name;
        bucket.expanded = group.expanded;
        bucketOfGroup.insert(group.id, buckets.size());
        buckets.append(bucket);
    }
    // 置顶组是哪一个（这一份 .minecraft 里没有就空串）。**这里读一次就够**：下面那圈
    // 循环里每一行都要问两遍，而且它保证指向一个**还在清单里**的组（见它的三条出口），
    // 所以 bucketOfGroup 里一定找得到。
    const QString pinnedGroupId = m_groupStore->pinnedGroupId();
    // 此刻**正在置顶**的那些实例（判据是 isPinned()，不是"这一行画在哪一组"）。它和上面
    // 那个字段是两件事：有置顶组但一个实例都没置顶时它是空的。灌树那一步要它 ——
    // 理由见 fillInstanceTree() 文件头那条 NOTE。
    QSet<QString> pinnedInstanceIds;
    QList<VersionDisplayParts> ungrouped;
    for (const VersionDisplayParts &part : parts) {
        // 摆在哪一组由 homeGroupIdOf() 回答 —— 也就是它的「老家」（置顶着的那一条老家
        // 不动，多出来的那一条才摆进置顶组，见下面那段）。没置顶时它就是 displayGroupIdOf()
        // 逐字的结果：改这一处**不会**动到任何一个没置顶的实例（那一段 NOTE 就在它上面）。
        const QString key = instanceKey(part.id);
        const QString homeId = homeGroupIdOf(m_groupStore, key, part, m_versionManager,
                                             knownGroupIds);
        const auto found = bucketOfGroup.constFind(homeId);
        if (found != bucketOfGroup.constEnd()) {
            buckets[found.value()].parts.append(part);
        } else {
            ungrouped.append(part);
        }

        // ★ 两边都显示（用户 2026-09-26 定的）：置顶着的那一个在置顶组里**再摆一条同名
        // 的**，老家那一条（上面刚摆完）从头到尾不动。取消置顶就是把这一条去掉。
        if (pinnedGroupId.isEmpty() || !m_groupStore->isPinned(key)) {
            continue;
        }
        pinnedInstanceIds.insert(part.id);
        if (homeId == pinnedGroupId) {
            // 老家**本来就在置顶组里**（用户自己把它塞进去的，pinOrigin 那一格是"压根
            // 没有记录"）⇒ 上面那条已经摆过了。不判这一下的话同一行会出现两次。
            continue;
        }
        // 置顶组一定在 bucketOfGroup 里（见上面那段），这里其实找不到 —— 但写出来是
        // 为了让"找不到就什么都不做"这条规矩在纸面上也成立（真找不到时宁可少一条，
        // 也不能往一个不存在的桶里写）。
        const auto pinnedBucket = bucketOfGroup.constFind(pinnedGroupId);
        if (pinnedBucket != bucketOfGroup.constEnd()) {
            buckets[pinnedBucket.value()].parts.append(part);
        }
    }
    if (groups.isEmpty()) {
        // 一个真实分组都没有：平铺，一个组头都不插。没分过组的用户看到的和换树之前
        // 逐项一致（这也正是 mw_check 里"实例数 == 3"那条断言还成立的原因）。
        buckets.clear();
        InstanceGroupBucket all;
        all.parts = parts;
        buckets.append(all);
    } else {
        // 「未分组」排在有归属的后面：用户建了组，说明那些组是他主要在看的东西。
        //
        // NOTE: 它和真实分组一样**空着就不画**（fillInstanceTree 那道滤）。2026-09-25
        //       之前它是"总是画出来"，那条理由（"少一格又出现比一直留着更难理解"）在
        //       用户要求「空白的分组隐藏」之后就不成立了：现在藏起来的是**所有**空组，
        //       一视同仁，而一格都没有的树本来就空着（没什么可画的）。
        InstanceGroupBucket rest;
        rest.name = MainWindowUI::ungroupedGroupText();
        rest.parts = ungrouped;
        buckets.append(rest);
    }
    // 每一行前面那个图标：**在这里一次算好**（而不是让灌树那个自由函数自己去问
    // VersionManager）—— 算它要读版本 json（见 instanceRowMark() 那段），那是业务层的事，
    // 而 fillInstanceTree 是只认树和值的自由函数。
    QHash<QString, QIcon> marks;
    marks.reserve(parts.size());
    for (const VersionDisplayParts &part : parts) {
        marks.insert(part.id, QIcon(instanceRowMark(part, m_versionManager,
                                                    MainWindowUI::versionTreeIconSize())));
    }
    fillInstanceTree(m_ui->getInstanceList(), buckets, m_settings->versionNameOptions(),
                     runningVersionIds(), marks, pinnedGroupId, pinnedInstanceIds);
    syncTreeToCombo(combo, m_ui->getInstanceList());

    // 占位文案随上下文变。**两个串都必须非空**：空占位文案会让 QComboBox 在往空
    // 模型里插第一批数据时静默选中第 0 项（qcombobox.cpp:1753-1759），而这里必须
    // 停在"没选"，否则用户会在毫无察觉的情况下启动一个自己没挑过的版本。
    //
    // 这一段同时治掉一个真实的用户报告：「打开启动器时显示没有已安装版本」——
    // 真因就是这句以前写死成"暂无已安装版本"，灌完数据后没人改它，于是明明装了
    // 好几个版本，画面上却说着一个都没有。
    // 判据同样用 launchable："一个能启动的都没有"才是"暂无已安装版本"（唯一那个正在
    // 装的时候，下拉框里确实一个都挑不了，那时候说"请选择一个版本"是句空话）。
    combo->setPlaceholderText(launchable.isEmpty() ? MainWindowUI::noInstalledVersionPlaceholder()
                                                   : MainWindowUI::chooseVersionPlaceholder());

    // 刷完必须手动同步按钮文字和 tooltip：上面 fillVersionCombo() 里套了
    // QSignalBlocker，重灌过程不会发信号，光靠 connect 那条接不到。
    updateStartButtonText();
}

QSet<QString> MainWindow::runningVersionIds() const {
    QSet<QString> ids;
    const QList<LauncherManager::GameSession> games = m_launcherManager->runningGames();
    for (const LauncherManager::GameSession &game : games) {
        // 同一个实例多开：这里天然去重（QSet），而"它在跑"这件事本来就只要一个是。
        ids.insert(game.versionId);
    }
    return ids;
}

void MainWindow::updateInstanceTreeRunState() {
    QTreeWidget *tree = m_ui->getInstanceList();
    // 挡信号：改的是文字，不该被当成"用户在树里点了什么"（树上的点击会同步到下拉框）。
    const QSignalBlocker blocker(tree);
    const VersionNameOptions options = m_settings->versionNameOptions();
    QTreeWidgetItemIterator it(tree);
    while (*it != nullptr) {
        const QString id = instanceIdOf(*it);
        if (!id.isEmpty()) {
            // 重新拼一遍而不是"在旧文本上加减后缀"：加减法要认得旧文本长什么样，
            // 而显示名本身会随"版本名格式"那一项变（那时 refreshInstalledVersions()
            // 会整棵重灌，不会走到这里）—— 每回从头拼，两条路的结果就一定一样。
            (*it)->setText(0, instanceTreeItemText(m_versionManager->displayParts(id), options,
                                                   m_launcherManager->isVersionRunning(id)));
        }
        ++it;
    }
}

void MainWindow::restoreLastLaunchedVersion() {
    const QString remembered = m_settings->lastLaunchedVersion();
    if (remembered.isEmpty()) {
        // 从没成功启动过任何版本：留空，让用户自己挑。这里不是错误路径，不打日志。
        return;
    }

    QComboBox *combo = m_ui->getInstalledVersionCombo();
    if (combo->currentIndex() >= 0) {
        // 已经有选中项了（理论上不会发生：构造函数里刚刷完、placeholderText 非空所以
        // 灌数据时不会自动选中第 0 项）。真发生了也不抢 —— 当前显示出来的那个才是
        // 用户看得见的选择，偷偷改掉比不恢复更糟。
        return;
    }

    const int index = combo->findData(remembered);
    if (index < 0) {
        // 记着的那个版本已经不在磁盘上了（被删了、目录改过名）。此时**刻意留空**：
        // 退回"再选一个"，而不是替用户改选一个别的版本 —— 启动是重操作，不替他猜。
        // 占位文案是"请选择一个版本"，开头那句"暂无已安装版本"只在真一个都没装时出现。
        qDebug() << "Last launched version is no longer installed:" << remembered;
        return;
    }
    // 显式收尾，**不依赖** currentIndexChanged 那条连接。那条连接是活的（这里没有
    // QSignalBlocker），但依赖它就等于依赖"setupConnections() 已经跑过了"这个隐藏
    // 前提 —— 而这里少刷一次的后果是按钮第二行停在"未选择版本"、和上面选中的版本
    // 对不上，正是 mw_check 那一整摊断言在防的事。这一句是幂等的，重复跑不花钱。
    combo->setCurrentIndex(index);
    updateStartButtonText();
}

void MainWindow::updateStartButtonText() {
    // 按钮文字分两行，第二行显示当前选中的版本，"点下去会启动哪个"不用再回头
    // 看上面的下拉框。名字可能很长（26.2-snapshot-1，或"我的整合包-1.20.1-Forge"），
    // 交给 QPushButton 自己按宽度决定换不换行，这里只保证第一行永远是"启动游戏"。
    QComboBox *combo = m_ui->getInstalledVersionCombo();
    const QString shown =
        combo->currentIndex() < 0 ? tr("未选择版本") : combo->currentText();
    m_ui->getStartButton()->setText(tr("启动游戏\n%1").arg(shown));

    // 下拉框的 tooltip 挂**裸 id**，不是显示名。两个理由：
    //   1. 下拉框设了 minimumContentsLength(12)，长显示名会被省略号截断；
    //   2. 更要紧的是，id 在界面上**任何开关组合下都不会出现** —— 显示名那边参与
    //      拼装的"实际版本"是 parts.realVersion（为空才退化成 id），所以只要
    //      wtfml.meta 里写了 realVersion，目录名就彻底从界面上消失了。而它才是真正
    //      进 --version 和文件路径的那个值。用户要排查"怎么启动错了版本"时，
    //      悬停是唯一能看到它的地方。
    // tooltip 必须走这条函数（而不是在 refreshInstalledVersions 里直接设），因为
    // fillVersionCombo() 套了 QSignalBlocker，刷新路径只能靠这一处收尾。
    const QString id = selectedVersionId(combo);
    combo->setToolTip(id.isEmpty() ? QString() : tr("版本目录名：%1").arg(id));
}

void MainWindow::refreshAvailableVersions() {
    // 只填树里那几条（2026-09-25 起是「最新版本」那一组 + 那四条分类）：它们唯一的数据源
    // 就是在线的版本清单（和已安装那一摊完全不相干 —— 以前清单为空时这里会退回显示本地
    // 已安装版本，两个数据源混在一张列表里，用户分不清点下去到底是启动还是下载）。
    // 四堆怎么分是 VersionManager 的事（它那边只有 VersionCategory 一处判据，含那张写死的
    // 愚人节版名单），"最新的两条是哪两条"同样是它的事（latestVersions()）。
    //
    // 条目文本就是**版本号本身**：在线清单 900+ 条逐条去读 wtfml.meta 是不可接受的
    // I/O，而且那些版本压根还没下载、磁盘上没有元数据可读。
    const QList<VersionManager::VersionChannel> channels =
        m_versionManager->availableVersionChannels();

    // 重灌之前记住"哪些分类是展开的"、"当前选中的是哪一条" —— 清单刷新（每次启动
    // 一次，拉回来还会再刷一次）不该把用户正在看的那一摊收起来。
    QTreeWidget *tree = m_ui->getVersionTree();
    QSet<QString> expandedKeys;
    QString selectedVersion;
    // 展开状态**逐个顶层条目读**，不区分"有没有选中项"。
    // NOTE: 以前这里是个 if/else：有选中项就只记"选中项所在的那个分类"、其它分类
    //       一律不管，只有**没有**选中项时才逐个扫一遍。于是同时展开着"正式版"和
    //       "快照"、并且在正式版里点过一个版本号之后，下次重灌会把快照那条收起来
    //       —— 而清单刷新（启动时那一次、以及拉回来之后那一次）本来就不该动用户
    //       看的那一摊。展开状态和选中项是两件互不相干的事，得分别记。
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = tree->topLevelItem(i);
        if (top->isExpanded()) {
            expandedKeys.insert(
                top->data(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole).toString());
        }
    }
    if (QTreeWidgetItem *current = tree->currentItem()) {
        if (QTreeWidgetItem *parent = current->parent()) {
            // 选中的是某个分类下的版本号。这个分类必须保住展开 —— 否则重灌之后选中
            // 的那条藏在收起来的分类里，用户看到的像是"选中的版本丢了"。
            expandedKeys.insert(
                parent->data(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole).toString());
            selectedVersion =
                current->data(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole).toString();
        }
    }

    // 「最新版本」那一组是**默认展开**的（用户原话：「下载游戏那个界面最上方放一个 最新版本 类，
    // 默认展开，里面放最新的正式版和快照」）。这一格只**摆一次**：用户把它收起来之后，
    // 清单刷新（每次启动那一次、以及拉回来那一次）不该又把它撑开 —— 而"记住哪些分类是展开的"
    // 这笔账上面刚记完，靠的就是这份集合，所以这里直接往里面塞一格，走的是**和用户自己展开
    // 完全一样**的那条路（重灌之后照旧按 isExpanded() 记回来），不是"每一发都强行展开"。
    //
    // NOTE: 为什么需要一个成员来记"摆过一次"：头一发时树是空的 ⇒ expandedKeys 也是空的
    //       ⇒ 不加这一格它从一开始就是收着的。而"树是不是空的"这个判据不能当替身 ——
    //       refreshAvailableVersions() 之外还有别人能清这棵树（那样用户收起来的东西会被
    //       又撑开一次），用成员就与"谁清了树"无关。
    if (!mLatestGroupExpandSeeded) {
        expandedKeys.insert(VersionManager::latestGroupKey());
        mLatestGroupExpandSeeded = true;
    }

    const QSignalBlocker blocker(tree);
    tree->clear();

    // ★ 2026-09-25 新加的第一条：**「最新版本」**（用户原话见上面那段）。
    //
    // 摆在**最上面**：它讲的是"现在最值得装的这两条"，放在四个分类后面等于没摆（那四栏各自
    // 几百条，谁也不会滚到底去看）。它不是第五个分类，所以有几件事刻意和那四条不一样：
    //   · 那四条是清单的**全集划分**（一条版本不重不漏地落进某一栏），这一组是**从里面挑
    //     两条出来** —— 同一个版本号在这两处各有一份（它自己那一栏里那条还在）；
    //   · 身份不来自 VersionCategory::channelKey()，走 VersionManager::latestGroupKey()
    //     （那个键保证和四个分类键两两不同，展开状态那笔账才串不了）；
    //   · 里面那两条**只认版本号**（和别处的版本号一样：点一下就是"基于它装"），
    //     所以它前面那个小图标按它**自己属于哪一类**给（正式版草方块 / 快照版覆雪草块）。
    //
    // NOTE: 清单没拉回来时（离线、或者第一发就失败）这一组照旧摆着，只是里面空的 ——
    //       和那四条空分类是同一个规矩（"这一条压根不出现"和"点开是空的"在界面上是两件
    //       很不一样的事，见 availableVersionChannels() 那段）。
    auto *latestTop = new QTreeWidgetItem(tree);
    latestTop->setData(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole,
                       VersionManager::latestGroupKey());
    latestTop->setText(MainWindowUI::versionTreeKeyColumn(), VersionManager::latestGroupText());
    for (const VersionManager::LatestVersion &entry : m_versionManager->availableLatestVersions()) {
        auto *child = new QTreeWidgetItem(latestTop);
        child->setData(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole, entry.version.id);
        child->setText(MainWindowUI::versionTreeKeyColumn(), entry.version.id);
        child->setData(MainWindowUI::versionTreeKeyColumn(), MainWindowUI::rowSubTextRole(),
                       releaseDateText(entry.version.releaseTime));
        const QPixmap mark =
            VersionIcons::versionChannelMark(VersionCategory::channelIndex(entry.channel),
                                             MainWindowUI::versionTreeIconSize());
        if (!mark.isNull()) {
            child->setData(MainWindowUI::versionTreeKeyColumn(), MainWindowUI::rowIconRole(), mark);
        }
    }
    if (expandedKeys.contains(VersionManager::latestGroupKey())) {
        latestTop->setExpanded(true);
    }

    for (const VersionManager::VersionChannel &channel : channels) {
        auto *top = new QTreeWidgetItem(tree);
        // 身份在 userData（= 分类键），文字只是给人看的 —— 和左栏那列分类同一个约定。
        top->setData(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole, channel.key);
        top->setText(MainWindowUI::versionTreeKeyColumn(),
                     channel.text.isEmpty() ? channel.key : channel.text);
        // NOTE: 这里**不要**去动 setChildIndicatorPolicy —— 想关掉 Qt 自己那个"左边
        //       一个小三角"的话，那是关错了地方：QTreeModel::hasChildren() 是按这个
        //       policy 直接 return false 的（QTreeWidget 源码里那条 switch），于是
        //       **整个分类的版本号一条都排不进布局**，树上只剩四条光秃秃的分类、
        //       点开也是空的。真要把左边那个三角去掉，走 rootIsDecorated(false)
        //       （见 MainWindowUI 里建树那一段）：它只影响装饰格的绘制，不动
        //       "有没有孩子"这件事。
        //       右边那个箭头仍然由 VersionTreeDelegate 画在行最右边（用户要的位置），
        //       一个版本都没有的分类它连箭头都不画。
        // 版本号前面那个小图标**按分类**给（用户 2026-09-23：「快照版的草用白色（覆雪草块），
        // 远古版就土块，愚人节版用幸运方块，原版不变」）。分类 → 图标那张表在
        // VersionIcons::versionChannelMark()，这里只把分类的**下标**递过去 —— ui/ 那边
        // 不认 VersionCategory（见 VersionIcons.hpp 那段"纯函数"的说明），所以分类 → 下标
        // 这一步必须走 VersionCategory::channelIndex()，别在这儿另写一个 switch：
        // 写歪了的症状是"快照那一栏前面白草、远古那一栏还是绿草"，不报错、只是难看。
        const QPixmap channelMark =
            VersionIcons::versionChannelMark(VersionCategory::channelIndex(channel.channel),
                                             MainWindowUI::versionTreeIconSize());
        for (const MinecraftVersion &version : channel.versions) {
            auto *child = new QTreeWidgetItem(top);
            child->setData(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole, version.id);
            child->setText(MainWindowUI::versionTreeKeyColumn(), version.id);
            // 版本号下面那行小字：发布时间（精确到日）。清单里没有 / 认不出来的，那行就不画。
            child->setData(MainWindowUI::versionTreeKeyColumn(), MainWindowUI::rowSubTextRole(),
                           releaseDateText(version.releaseTime));
            if (!channelMark.isNull()) {
                child->setData(MainWindowUI::versionTreeKeyColumn(), MainWindowUI::rowIconRole(),
                               channelMark);
            }
        }
        if (expandedKeys.contains(channel.key)) {
            top->setExpanded(true);
        }
        // 把选中那条找回来：**只在四条分类里找，不找「最新版本」那一组**。
        // Why: 同一个版本号在树上有两份（它自己那一栏里一条、最新那一组里可能还有一条），
        //      两个都是真的、点哪个都是"基于它装"。这一搜要是从头开始（「最新版本」现在在
        //      第一条），用户本来在"正式版"里选着的那条**每次刷新都会悄悄跳到最上面那一组**
        //      去 —— 他没动过手，高亮自己跑了。只在分类里找 = 和这次改动之前**一模一样**的
        //      行为（以前树上只有那四条）。代价：要是用户当初是从「最新版本」那一组点进去的，
        //      刷新之后高亮会落到它自己那一栏里 —— 那也仍在屏幕上、仍是同一条版本。
        if (!selectedVersion.isEmpty()) {
            for (int i = 0; i < top->childCount(); ++i) {
                if (top->child(i)->data(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole)
                        .toString() == selectedVersion) {
                    tree->setCurrentItem(top->child(i));
                    break;
                }
            }
        }
    }
}

void MainWindow::openInstallPage(const QString &gameVersion) {
    if (gameVersion.isEmpty()) {
        return;
    }
    m_installGameVersion = gameVersion;
    m_installNameTouched = false;

    // 这一层只管界面：要装的是哪个版本、名称框里默认写什么、那五条加载器现在该是什么
    // 状态。真正的安装请求在 onInstallButtonClicked() 里发出去。
    m_ui->getInstallTargetLabel()->setText(MainWindowUI::installTargetText(gameVersion));
    // 默认名跟着"选中的加载器"走（没选就是原版号本身）—— updateLoaderRows() 会顺手
    // 把它算出来，所以这里先摆状态、再让它收尾。
    // 注意每次进安装页都**回到「不装（纯原版）」**：上一个实例选的是哪条加载器跟这一个
    // 没关系，留着的话下一个版本会顶着一个"上次顺手选的 Forge"进来。
    m_selectedLoaderIndex = MainWindowUI::modLoaderNoneIndex();
    // 钉住的版本号同理：换了一个游戏版本，上一份能装的版本就完全不是这一份了
    // （Forge 每个游戏版本能装的版本完全不同）。
    m_chosenLoaderVersion.clear();
    // 收起来 + 清掉上次灌进去的子条目（**不能**只 clear 整棵树：那样顶层那五条也一起
    // 没了）。takeChildren() 交出来的那些条目已经不带爹了，得自己删 —— 不删就是每进一次
    // 安装页漏一批（几十上百个 QTreeWidgetItem）。requestAllLoaderVersions() 里对每个
    // 加载器再来一遍，顺手把"正在获取…"占位摆上。
    for (int i = 0; i < MainWindowUI::modLoaderOptionTexts().size(); ++i) {
        QTreeWidgetItem *item = loaderItem(i);
        if (item == nullptr) {
            continue;
        }
        item->setExpanded(false);
        qDeleteAll(item->takeChildren());
    }
    updateLoaderRows();
    // 四个加载器能装哪些版本：**进这一页就全问出去**（用户原话："获取列表不能点开了再
    // 获取，得全获取完"）。以前是"点开哪条现拉哪条"，那次实现里还藏着一个"点不开了"
    // 的 bug（见 fillLoaderVersionItems() 里那段 NOTE）。
    requestAllLoaderVersions();
    // 名称框里的默认值：勾的是「不装」时就是原版号本身（那正是"装原版"这条老路）。
    updateInstallName();

    // 树那一层留着不动（用户按返回时回的就是它，连展开状态都还在）。
    m_ui->getGameStack()->setCurrentIndex(MainWindowUI::downloadGameInstallIndex());
    m_ui->getInstallNameEdit()->setFocus();
    appendLog(tr("准备安装，基于原版 %1。").arg(gameVersion));
}

void MainWindow::backToVersionTree() {
    // 只翻页，什么都不清：树里那份展开状态、名称框里敲过的字都留着 —— 用户按返回
    // 是想"再看一眼别的版本"，不是想"放弃刚才那些输入"。真要放弃，重进一次就重算了。
    m_ui->getGameStack()->setCurrentIndex(MainWindowUI::downloadGameTreeIndex());
}

// ---------------------------------------------------------------------------
// 安装页：名称框 + 四个加载器（各自选版本）
// ---------------------------------------------------------------------------

QString MainWindow::defaultInstanceName() const {
    // 目录名的形状照抄真机上那些实例：`1.18.2-Forge_40.3.12`、`1.21.1-NeoForge_21.1.210`。
    // 没选加载器时就是原版号本身 —— 那一份**不写薄 json**（id == 父版本），装出来的就是
    // 原版那个实例，和改动前「下载版本」的行为一个字不差。
    //
    // NOTE: 刻意**不**走 VersionDisplay::formatVersionName()：那是**显示名**的拼法，跟着
    //       用户在设置页选的"版本名格式"开关走（关掉"实际版本"那一档就会拼出一个不含
    //       版本号的目录名），而目录名要的是确定、可预期、跟显示开关无关。
    if (m_installGameVersion.isEmpty()) {
        return QString();
    }
    VersionManager::LoaderKind kind = VersionManager::LoaderKind::Fabric;
    const int index = selectedLoaderIndex();
    if (!VersionManager::loaderKindFromIndex(index, &kind)) {
        return m_installGameVersion;
    }
    const QString loaderName = VersionManager::loaderDisplayName(kind);
    const QString pinned = pinnedLoaderVersion(index);
    // 钉的是一个**具体版本号**时原样写进目录名（`1.18.2-Forge_40.3.12`）。
    // 钉的是**那两档**（`@latest` / `@stable`）时**不带后缀**（`1.21.4-Forge`）—— 和改动前
    // 那条「自动（推荐）」写出来的名字一个字不差。两个理由：
    //   · 那两档最终装到哪个版本号，要等安装那条路联网挑完才知道（见
    //     VersionManager::installLoaderVersion），这一刻算不出来 —— 写 `_stable` 是拿
    //     "哪一档"当版本号用，而真实版本号在装完那份 wtfml.meta 的 loaderVersion 里；
    //   · 名字一样才走得上「已经有叫这个的实例了 → 再装一次是校验并补齐」那条路。带后缀
    //     的话，用户"同一个游戏同一个加载器"再装一次会多出一个并行实例，而他手上的老实例
    //     还叫 `1.21.4-Forge`。同一档换个版本装进同一个实例，本来就是这条路支持的用法。
    LoaderMeta::LoaderChannel channel;
    const bool byChannel = LoaderMeta::loaderChannelFromKey(pinned, &channel);
    const QString suffix = byChannel ? QString() : pinned;
    return suffix.isEmpty() ? QStringLiteral("%1-%2").arg(m_installGameVersion, loaderName)
                            : QStringLiteral("%1-%2_%3").arg(m_installGameVersion, loaderName,
                                                             suffix);
}

int MainWindow::selectedLoaderIndex() const {
    return m_selectedLoaderIndex;
}

QTreeWidgetItem *MainWindow::loaderItem(int loaderIndex) const {
    QTreeWidget *tree = m_ui->getLoaderTree();
    if (tree == nullptr) {
        return nullptr;
    }
    // 顺序是界面自己按 modLoaderOptionNames() 摆的（第 i 条就是第 i 个加载器），
    // 但**身份**仍然从 userData 读 —— 摆的顺序哪天变了，这里不会跟着错。
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        if (item->data(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole).toInt() == loaderIndex) {
            return item;
        }
    }
    return nullptr;
}

QString MainWindow::pinnedLoaderVersion(int loaderIndex) const {
    // 这一格里有三种东西（见 MainWindowUI::loaderVersionStableText 上面那段）：
    //   · 一个**具体版本号** —— 用户在那两档下面点的那一条；
    //   · `@latest` / `@stable` —— 用户点的是上面那两档（LoaderMeta::loaderChannelKey）；
    //   · **空串** —— 没挑过（没进过安装页、或者版本列表没取到）。空串在数据层那边按
    //     稳定档算（LoaderMeta::loaderRequestFromChoice），也就是改动前"让安装器按老规矩挑"。
    // 没进过 map 的下标也是空串。
    return m_chosenLoaderVersion.value(loaderIndex);
}

void MainWindow::updateLoaderRows() {
    // 那棵树的样子**全部**在这一处决定（别处一个 setText/setData 都不许写）：
    //   · 选中的那一条：第二行小字写它选的那一格（版本号 / 「最新版」/「稳定版」）；
    //   · 其余几条加载器：第二行小字写「与 X 不兼容」（用户要的那句话）；
    //   · 「不装（纯原版）」那条：没有第二行（它不是加载器，不跟谁冲突）。
    // NOTE: 选中某一条之后，**其余几条加载器变灰、也点不开**（用户原话："选择以后让其他
    //       选项变成不可用（不能点开）的模式"）。改选别的加载器要先点「不装（纯原版）」
    //       —— 那条**故意不灰**，它既是"回纯原版"，也是唯一那条还点得动的退路；
    //       换版本则点当前这条底下的版本号。本启动器一次只装一个加载器，所以"不兼容"
    //       说的是"两个不能同时装"，而灰掉说的是"这一下点了也不会变成装它"。
    const int selected = selectedLoaderIndex();
    VersionManager::LoaderKind selectedKind = VersionManager::LoaderKind::Fabric;
    const bool hasLoader = VersionManager::loaderKindFromIndex(selected, &selectedKind);
    const QString selectedName =
        hasLoader ? VersionManager::loaderDisplayName(selectedKind) : QString();

    const int count = MainWindowUI::modLoaderOptionTexts().size();
    for (int i = 0; i < count; ++i) {
        QTreeWidgetItem *item = loaderItem(i);
        if (item == nullptr) {
            continue;
        }
        VersionManager::LoaderKind kind = VersionManager::LoaderKind::Fabric;
        const bool isLoader = VersionManager::loaderKindFromIndex(i, &kind);
        QString subText;
        if (isLoader && hasLoader) {
            if (i == selected) {
                const QString pinned = pinnedLoaderVersion(i);
                LoaderMeta::LoaderChannel channel;
                if (LoaderMeta::loaderChannelFromKey(pinned, &channel)) {
                    // 挑的是那一档：小字写档名（版本号要等装的时候才定下来）。
                    subText = MainWindowUI::loaderVersionChannelShortText(
                        channel == LoaderMeta::LoaderChannel::Newest);
                } else if (pinned.isEmpty()) {
                    // 没挑过（版本列表没取到）：按默认那一档说 —— 稳定档。
                    subText = MainWindowUI::loaderVersionChannelShortText(false);
                } else {
                    subText = pinned;
                }
            } else {
                subText = MainWindowUI::loaderConflictText(selectedName);
            }
        }
        // 角色是 rowSubTextRole()：空串 = 那一行不画第二行小字（代理那边统一处理）。
        item->setData(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::rowSubTextRole(), subText);
        // 选中的那条加载器：行尾（箭头左边）画一个「取消」小叉 —— 点它 = 不装这个加载器了、
        // 退回「不装（纯原版）」（用户原话："选中以后在那个 > 左侧加一个 x 来取消安装
        // 对应版本"）。**只有这一条**有这个叉：其余几条（包括「不装」）没有"要取消的东西"。
        // 点它的那一下由 MainWindow::eventFilter 接（几何是 VersionTreeDelegate::clearRect()）。
        item->setData(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::rowClearRole(),
                      isLoader && hasLoader && i == selected);
        // 灰那几条：选中了某个加载器之后**其余几条加载器**不可用（见上面那段 NOTE）。
        // 顺手把灰掉的那条**收起来** —— 收着的时候它底下的版本号看不见，而"展开着一份
        // 版本列表、整条却是灰的"看着像"点得动、只是不让点"（点一下才知道不是）。
        // 「不装（纯原版）」不参与：它不是加载器（isLoader 为假），而且是那条退路。
        if (isLoader) {
            const bool locked = hasLoader && i != selected;
            if (locked && item->isExpanded()) {
                item->setExpanded(false);
            }
            item->setDisabled(locked);
        }
    }
}

void MainWindow::selectLoader(int loaderIndex) {
    // 「不装（纯原版）」和四个加载器都是合法值；表外的值（界面表和枚举哪天对不上）
    // 一律当作"没选加载器"—— 宁可装成原版，也不要拿着一个不认识的下标去猜加载器。
    VersionManager::LoaderKind kind = VersionManager::LoaderKind::Fabric;
    m_selectedLoaderIndex = VersionManager::loaderKindFromIndex(loaderIndex, &kind)
                                ? loaderIndex
                                : MainWindowUI::modLoaderNoneIndex();
    updateLoaderRows();
    // 默认名里带着加载器名（和它钉的版本号），换一条就得重算一次。
    updateInstallName();
    // NOTE: **这里不发任何请求**。"有哪些版本"是进安装页时一次性全问好的
    //       （见 requestAllLoaderVersions()，用户原话："获取列表不能点开了再获取，
    //       得全获取完"）。走这一句的只有两条路：点一条**版本号**（选中它爹那条加载器）、
    //       点「不装（纯原版）」那一条。点一条加载器**不再**选中它（那一下只展开/收起，
    //       见 onLoaderTreeClicked() 上面那段）—— 点一下就去碰那棵树是 2026-09-23 那个
    //       "点不开了"的成因之一，别再走回去。
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    // 安装页那棵树上**唯一一个"行里的按钮"**：选中那条加载器右边的「取消」小叉
    // （谁画的、画在哪一格，见 VersionTreeDelegate::clearRect() 那段 NOTE）。
    //
    // 为什么不走 itemClicked：它只给"点中了哪个条目"、不给"点在条目里的哪儿"，而这一下
    // 点的是行里的一个按钮、**不是这一行** —— 点中叉的时候那一行不该跟着展开/收起。
    // 过滤器能把事件吃掉（返回 true 之后 Qt 那套点击处理整条不走了），两件事就都干净。
    QTreeWidget *tree = m_ui->getLoaderTree();
    if (tree != nullptr && watched == tree->viewport() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QTreeWidgetItem *item = nullptr;
        if (mouse->button() == Qt::LeftButton) {
            item = tree->itemAt(mouse->position().toPoint());
        }
        if (item != nullptr &&
            item->data(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::rowClearRole())
                .toBool() &&
            VersionTreeDelegate::clearRect(tree->visualItemRect(item))
                .contains(mouse->position().toPoint())) {
            if (event->type() == QEvent::MouseButtonPress) {
                // 按下就动手（松开那一下只负责被吃掉）：跟普通按钮一样，按下去就该有反应。
                // 这一下取消的是"装这个加载器"这件事 —— 退回「不装（纯原版）」，也就是
                // 装纯原版（用户原话："来取消安装对应版本"）。
                // NOTE: **钉住的版本号留着**（不清 m_chosenLoaderVersion）：那是用户自己挑的
                //       一版，按一下叉就把它忘掉，下次再选中这条加载器还得从头找一遍。
                //       它只在"这一条被选中"的时候才显示，留着不会让人看到不该看的东西。
                selectLoader(MainWindowUI::modLoaderNoneIndex());
            }
            return true;
        }
    }
    // 实例页那张树上**行里的按钮**，头一个：组行行尾那颗齿轮（谁画的、画在哪一格，
    // 见 InstanceGroupDelegate::gearRect()）。判据和上面那个叉一模一样 —— 包括"吃掉
    // 那一下"：不吃的话点齿轮会顺手把这一组展开/收起（那正是"点组行"要干的事）。
    // 另外两颗（上移 / 下移）在下面那一段，判据同族，只有"按得快要不要补那一发"不同。
    //
    // NOTE: 这里**也接双击**。点得快的时候第二发及以后到的是 MouseButtonDblClick
    //       （见 ui/RowActivation.hpp 那段），只接 Press/Release 的话"连点两下齿轮"
    //       的第二下会漏给下面那条"点中了这一行"的路。
    QTreeWidget *instanceTree = m_ui->getInstanceList();
    if (instanceTree != nullptr && watched == instanceTree->viewport()
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::MouseButtonDblClick)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QTreeWidgetItem *item =
            mouse->button() == Qt::LeftButton ? instanceTree->itemAt(mouse->position().toPoint())
                                              : nullptr;
        if (item != nullptr && item->data(0, MainWindowUI::rowGroupGearRole()).toBool()
            && InstanceGroupDelegate::gearRect(instanceTree->visualItemRect(item))
                   .contains(mouse->position().toPoint())) {
            // 按下就动手（松开、以及那几发双击只负责被吃掉）：跟普通按钮一样，按下去就该
            // 有反应。双击那几发**不能**再弹一遍 —— 弹第二遍时上面那块框已经开着，
            // openGroupMenu() 认到"还是同一组"就把它收回去了，用户看到的是"连点两下齿轮
            // 反而没了"（那一处正是"再点同一颗齿轮 = 收回"，别在门口把它挡掉）。
            if (event->type() == QEvent::MouseButtonPress) {
                openGroupMenuFor(item);
            }
            return true;
        }
    }
    // 同一个实例树上**实例行**行尾那一排（用户 2026-09-25 原话：「实例设置也是放在每一条的
    // 右侧，鼠标悬停时右侧显示上箭头和垃圾桶，分别对应 置顶和删除实例，删除前需要弹窗提示」）。
    // 行内三颗：**常显的齿轮**（= 实例设置）+ 悬停才出现的**上箭头**（= 置顶）和**垃圾桶**
    // （= 删除这个实例）。谁画的、画在哪一格，见 InstanceGroupDelegate::instanceGearRect()
    // 那几个函数 —— 这里读的是**同一个函数**，差一个像素就是"看着点中了垃圾桶、其实点的是
    // 那一行"（那一行现在也有一件事要做：进实例设置，见 onInstanceTreeActivated）。
    //
    // 判据"这是不是实例行"用**没有** groupIdRole()：和代理那边 paint()/sizeHint() 是同一条
    // （组行有那个角色、实例行没有）。
    //
    // NOTE: 三格的命中判定顺序无所谓（探针量过：三格之间零重叠、零空隙），但中间那一格
    //       要先问**这一行到底摆没摆它**（rowPinRole() / rowUnpinRole()）—— 这份
    //       .minecraft 里没有置顶组时那一格是空的：指着一块空白弹出「置顶」、点下去还
    //       真的挪了一次，是最难解释的一种。
    // NOTE: 中间那一格是**两个动作共用的一格**（不在置顶组 ⇒ ↑ 置顶 / 已经在了 ⇒ ↓
    //       取消置顶，见 MainWindowUI::rowUnpinRole()）—— 所以这里比的是"在里面不在里面"，
    //       不是"角色号是哪一个"。
    // NOTE: 也接双击，理由和上面组行齿轮那一段逐字相同（点得快时第二发及以后到的是
    //       双击事件，不接的话会漏给"点中了这一行"那条路，于是连点垃圾桶的第二下变成
    //       进实例设置）。
    if (instanceTree != nullptr && watched == instanceTree->viewport()
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::MouseButtonDblClick)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QTreeWidgetItem *item =
            mouse->button() == Qt::LeftButton ? instanceTree->itemAt(mouse->position().toPoint())
                                              : nullptr;
        // 三格的顺序照代理**画它们的顺序**（齿轮 / 上箭头 / 垃圾桶），一粒一粒判。
        if (item != nullptr && !item->data(0, MainWindowUI::groupIdRole()).isValid()) {
            const QRect row = instanceTree->visualItemRect(item);
            const QPoint pos = mouse->position().toPoint();
            const QString instanceId = item->data(0, MainWindowUI::instanceIdRole()).toString();
            const bool pressed = event->type() == QEvent::MouseButtonPress;
            if (InstanceGroupDelegate::instanceGearRect(row).contains(pos)) {
                // 按下就动手（松开、以及那几发双击只负责被吃掉）：跟普通按钮一样，按下去
                // 就该有反应。齿轮 = 进这个实例的设置（用户原话：「直接点空白区域（和左边
                // 图标版本名）进入实例设置」—— 那一行别的地方也是同一件事）。
                if (pressed) {
                    openInstanceSettings(instanceId);
                }
                return true;
            }
            // 中间那一格：↑「置顶」和 ↓「取消置顶」**共用的一格**（见 MainWindowUI::
            // rowUnpinRole()）。判据是"这一行摆没摆它"那两个角色里**有一个为真**，
            // 按下去调谁看的是**在里面还是不在**（canUnpin 优先，两个都真时不可能，
            // 代理那边同一个约定）。
            const bool canPin = item->data(0, MainWindowUI::rowPinRole()).toBool();
            const bool canUnpin = item->data(0, MainWindowUI::rowUnpinRole()).toBool();
            if ((canPin || canUnpin) && InstanceGroupDelegate::instancePinRect(row).contains(pos)) {
                if (pressed) {
                    if (canUnpin) {
                        unpinInstance(instanceId);
                    } else {
                        pinInstance(instanceId);
                    }
                }
                return true;
            }
            if (InstanceGroupDelegate::instanceTrashRect(row).contains(pos)) {
                if (pressed) {
                    confirmDeleteInstance(instanceId);
                }
                return true;
            }
        }
    }
    // NOTE: 这里原来还有一段：同一个实例树上组行行尾那两颗「上移 / 下移」（齿轮左边那两颗
    //       实心三角）的接线。2026-09-25 用户把它们整个搬进了「分组设置…」那张表（原话：
    //       「这个点击一下就同时展开和调整了，其次这个箭头不应该在这里，而应该在 分组设置
    //       里面，这里的删了，去分组设置里面加上」），于是这一段和它调的
    //       moveGroupInOrder() 一起删了 —— 那张表里认下那一下的是
    //       `ui/GroupDialog::eventFilter()`（挂在那张表的视口上），"每一发都算"和
    //       "Shift = 置顶/置底"那两条手势原样跟着搬了过去。
    //       ★ 2026-09-25 第二轮：实例行行尾那一排的接线加在**上面**（就在组行齿轮那段
    //         后面）—— 它和这里删掉的那套是同一个位置、同一族判据，别再往下找。
    // 下载页左栏那份分类列表：每一行最右边那个刷新按钮（谁画的、画在哪一格，见
    // VersionTreeDelegate::refreshRect()）。判据和上面那个叉一模一样，只是宿主是个
    // QListWidget（它的 itemAt/visualItemRect 和 QTreeWidget 那对同名同义）。
    QListWidget *categories = m_ui->getDownloadCategoryList();
    if (categories != nullptr && watched == categories->viewport() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QListWidgetItem *item =
            mouse->button() == Qt::LeftButton ? categories->itemAt(mouse->position().toPoint())
                                              : nullptr;
        if (item != nullptr &&
            item->data(MainWindowUI::rowRefreshRole()).toBool() &&
            VersionTreeDelegate::refreshRect(categories->visualItemRect(item))
                .contains(mouse->position().toPoint())) {
            if (event->type() == QEvent::MouseButtonPress) {
                // 按下就动手（松开那一下只负责被吃掉）—— 和那个叉一个规矩。
                refreshDownloadCategory(categories->row(item));
            }
            return true;
        }
    }
    // 下载页那四张资源表：**点行身 = 打开这一条资源的详情页**（用户原话：「下载页选择了
    // 模组以后进入一个独立界面…和 pcl 类似」）；点行尾那个箭头仍然是老规矩（展开 / 收起，
    // 判据是同一个 arrowRect()）—— 于是"展开这一条"这件事从此只挂在箭头上。
    //
    // NOTE: 这一块**只认第一层**（一个资源）。第二层（游戏版本）点开是"问这个资源在这个
    //       游戏版本上有哪些版本可下"、第三层（版本号）点它是"下它"，两条老路原样留着
    //       （用户要的详情页是**另一条**看整份清单的路，不是把树上那条拆掉）。
    // NOTE: 三发都吃掉（按下 / 松开 / 双击）：`connectRowActivation` 那边给"开关行"补的
    //       那一发双击也会落到"展开"上 —— 而这一下已经算"进这一页"了，不该再翻一次树。
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
        || event->type() == QEvent::MouseButtonDblClick) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        for (ResourceCatalog::Kind treeKind : ResourceCatalog::allKinds()) {
            const QString treeKey = ResourceCatalog::kindKey(treeKind);
            QTreeWidget *tree = m_ui->getDownloadResourceTree(treeKey);
            if (tree == nullptr || watched != tree->viewport()) {
                continue;
            }
            const QPoint pos = mouse->position().toPoint();
            QTreeWidgetItem *item =
                mouse->button() == Qt::LeftButton ? tree->itemAt(pos) : nullptr;
            if (item == nullptr || resourceRowDepth(item) != 0) {
                continue;
            }
            // "这一行是不是我们建的"：占位行（「正在搜索…」那几行）没有这一位。
            const int column = MainWindowUI::versionTreeKeyColumn();
            const QString kindKey =
                item->data(column, MainWindowUI::resourceKindRole()).toString();
            if (kindKey.isEmpty()) {
                continue;
            }
            // 箭头那一格**交回去**（展开 / 收起还是它）：判据和代理画它用的是同一个函数，
            // 两边各算一份的话迟早错开，而错开的形状是"看着点的是箭头、其实进了一页"。
            if (VersionTreeDelegate::arrowRect(tree->visualItemRect(item)).contains(pos)) {
                continue;
            }
            if (event->type() == QEvent::MouseButtonPress) {
                // 按下就动手（松开和那几发双击只负责被吃掉）—— 和实例行上那三颗一个规矩。
                const QString iconKey =
                    item->data(column, MainWindowUI::resourceIconIdRole()).toString();
                // **排到下一轮事件循环**再开页：`execPage` 里面要转起一个嵌套事件循环，
                // 而这一发还在视图的事件处理里（列表那一侧的状态机悬在半路）。这里传的是
                // 两个字符串**值**，不是条目指针 —— 下一轮里树可能已经被重搜清空了。
                //
                // ★ 来路钉死是 `Download`（2026-09-27）：这条路上这一页**永远**摆「显示
                //   快照版」，哪怕点的是一个模组（用户原话「不要不小心把下载页改了」）。
                //
                // ★ 2026-09-28：把"当前实例"一起带过去（空 = 不是从某个实例进的下载页）——
                //   从实例设置点「下载新模组」进来的那一次，用户是在给**那个实例**找模组，
                //   开页时按它的版本预选、把它的加载器排前面（见 openResourceDetailPage）。
                //   抓的是**此刻**这一位的值（下一轮里它可能已经变了）。
                const QString instanceId = m_downloadFromInstanceId;
                QTimer::singleShot(0, this, [this, iconKey, kindKey, instanceId] {
                    openResourceDetailPage(iconKey, kindKey, DetailSource::Download, instanceId);
                });
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::requestAllLoaderVersions() {
    // 进安装页时把**四个加载器**的版本列表一次性全问出去（用户原话："获取列表不能点开了
    // 再获取，得全获取完"）。四条各占一格请求（见 VersionManager 里 m_loaderVersionRequests
    // 那段），所以四条真能同时在飞。
    //
    // 代价是进一次安装页约 500 KB（Forge 那份 maven 清单自己就 211 KB）—— 换来的是
    // "点开哪一条都立刻有东西看"，不再有"点开 → 等一下 → 才出列表"那一下。
    if (m_installGameVersion.isEmpty()) {
        return;  // 没定下来装哪个游戏版本，问不出"这个版本上有哪些加载器版本"
    }
    for (int i = 0; i < MainWindowUI::modLoaderOptionTexts().size(); ++i) {
        VersionManager::LoaderKind kind = VersionManager::LoaderKind::Fabric;
        if (!VersionManager::loaderKindFromIndex(i, &kind)) {
            continue;  // 「不装（纯原版）」那一条：原版没有加载器版本可问
        }
        QTreeWidgetItem *item = loaderItem(i);
        if (item == nullptr) {
            continue;
        }
        // 先摆一条灰的"正在获取…"占住那一条底下的位置：空着一条看着像"这个加载器一个
        // 版本都没有"。回复在 onLoaderVersionsFetched() 里收，按"加载器 + 游戏版本"认领。
        item->setExpanded(false);
        qDeleteAll(item->takeChildren());
        auto *waiting = new QTreeWidgetItem(item);
        waiting->setText(MainWindowUI::loaderTreeKeyColumn(),
                         MainWindowUI::loaderVersionFetchingText());
        waiting->setDisabled(true);  // 灰的、点不动：它不是能选的东西（选了也不该有反应）
        m_versionManager->requestLoaderVersions(m_installGameVersion, kind);
    }
}

void MainWindow::fillLoaderVersionItems(int loaderIndex,
                                        const LoaderMeta::LoaderVersionList &versions,
                                        bool includeChannels) {
    QTreeWidgetItem *item = loaderItem(loaderIndex);
    if (item == nullptr) {
        return;
    }
    // 别动用户已经选中的那一条：列表可能刚好是"同一条"（缓存命中的那次会走到这里），
    // 重灌一次把它顶回第 0 条就是"我选的 47.4.10 自己变回最新版了"。选中的版本号在
    // m_chosenLoaderVersion 里，所以重灌只是把条目重建一遍、再把光标放回去。
    const QString chosen = pinnedLoaderVersion(loaderIndex);
    const bool wasExpanded = item->isExpanded();
    // 连"正在获取…"那条一起清掉：它就是等这一份列表用的。
    qDeleteAll(item->takeChildren());
    // 这一版上有没有正式版 —— 稳定那一行灰不灰就看它。**列表为空时一律当作"不知道"**：
    // 那时候这份列表根本没取到（下面 includeChannels 为假的那一支是"问到了、就是没有"，
    // 连这两行都不摆），而"让安装器自己挑"那条路不依赖这份列表，所以不该把用户堵死。
    bool hasStable = false;
    for (const LoaderMeta::LoaderVersionEntry &entry : versions) {
        hasStable = hasStable || entry.stable;
    }
    // 头两条 = 那两档（见 MainWindowUI::loaderVersionLatestText 上面那段）。它们的 userData
    // 是 LoaderMeta::loaderChannelKey() 那两个键，再往下才是具体版本号。
    //
    // includeChannels 为假 = 这一版**一个能装的都没有**（versions 也是空的）：连这两档
    // 都不留 —— 理由写在 onLoaderVersionsFetched() 那段（留着等于让用户挑一条必然装不
    // 出来的路）。调用方保证"includeChannels 假"只在这种情况出现。
    QTreeWidgetItem *latestItem = nullptr;
    QTreeWidgetItem *stableItem = nullptr;
    if (includeChannels) {
        const auto mark = [loaderIndex](QTreeWidgetItem *row) {
            row->setIcon(MainWindowUI::loaderTreeKeyColumn(),
                         QIcon(VersionIcons::loaderMark(
                             loaderIndex, MainWindowUI::versionTreeIconSize())));
        };
        latestItem = new QTreeWidgetItem(item);
        latestItem->setText(MainWindowUI::loaderTreeKeyColumn(),
                            MainWindowUI::loaderVersionLatestText());
        latestItem->setData(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole,
                            LoaderMeta::loaderChannelKey(LoaderMeta::LoaderChannel::Newest));
        mark(latestItem);

        stableItem = new QTreeWidgetItem(item);
        stableItem->setText(MainWindowUI::loaderTreeKeyColumn(),
                            MainWindowUI::loaderVersionStableText());
        stableItem->setData(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole,
                            LoaderMeta::loaderChannelKey(LoaderMeta::LoaderChannel::Stable));
        mark(stableItem);
        // **问到了、可一条正式版都没有**（NeoForge + 26.3 就是：11 个构建全是 `-beta`）：
        // 这一行灰掉、第二行小字把原因写出来。留着能点的它等于让用户挑一条必然装不出来的
        // 路 —— 装的时候会以"还没有支持 X 的版本"收场。
        if (!versions.isEmpty() && !hasStable) {
            stableItem->setDisabled(true);
            stableItem->setData(MainWindowUI::loaderTreeKeyColumn(),
                                MainWindowUI::rowSubTextRole(),
                                MainWindowUI::loaderVersionStableMissingText());
        }
    }
    QTreeWidgetItem *chosenItem = nullptr;
    for (const LoaderMeta::LoaderVersionEntry &entry : versions) {
        const QString &version = entry.version;
        auto *child = new QTreeWidgetItem(item);
        child->setText(MainWindowUI::loaderTreeKeyColumn(), version);
        child->setData(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole, version);
        // 子条目前面挂**它自己那个加载器**的标志（那一列列出来的是"Fabric 能装哪些版本"，
        // 前面顶个草方块会让人以为是原版号）。
        child->setIcon(MainWindowUI::loaderTreeKeyColumn(),
                       QIcon(VersionIcons::loaderMark(loaderIndex,
                                                      MainWindowUI::versionTreeIconSize())));
        // 不是正式版的那几条：第二行小字标出「测试版」。**照列、也能装**（用户原话：
        // "beta 版虽然不稳定，但是也能使用"）—— 这一行小字是"它是什么"，不是"它不行"。
        if (!entry.stable) {
            child->setData(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::rowSubTextRole(),
                           MainWindowUI::loaderVersionBetaSubText());
        }
        if (version == chosen) {
            chosenItem = child;
        }
    }
    // 光标放回"现在钉着的那一条"—— 展开之后一眼看到的就是它。没挑过（空串）时摆在
    // **默认会走的那一档**上：稳定档（它灰了就是最新档），和安装那条路的默认值一致。
    // **这不改选中状态**：选中是 m_selectedLoaderIndex 的事，这里只是画光标。
    if (chosenItem == nullptr) {
        LoaderMeta::LoaderChannel channel;
        if (LoaderMeta::loaderChannelFromKey(chosen, &channel)) {
            chosenItem = channel == LoaderMeta::LoaderChannel::Newest ? latestItem : stableItem;
        } else {
            // 没挑过、或者钉的那条这次没列出来（换了游戏版本那种）：退回默认那一档。
            chosenItem = (stableItem != nullptr && !stableItem->isDisabled()) ? stableItem
                                                                             : latestItem;
        }
    }
    //
    // ★ 这一下**只能在那一行开着的时候做**（2026-09-23 用户报的 bug 就在这儿）。
    //   链路：`QTreeWidget::setCurrentItem()` → currentChanged → QAbstractItemView 的
    //   `if (d->autoScroll) scrollTo(newCurrent)` → `QTreeView::scrollTo()` 头一句就是
    //   "Expand all parents if the parent(s) of the node are not expanded"（qtreeview.cpp
    //   里那几行，实测过：把 setCurrentItem 摘掉，那个"点不开了"当场就好了）。
    //   于是"把光标放到一条收着的加载器的子条目上" = **把那一条撑开**。用户看到的是：
    //   点开 Fabric → 选一条版本号 → 再点 Fabric，它闪一下就不动了（撑开它的那一下
    //   正好把点击处理里那句"取反"吃掉了）。
    //   收着的时候不放也没损失 —— 这几个版本号就写在那一行自己的第二行小字上
    //   （"选择版本以后收缩，显示版本"），不靠光标指。
    QTreeWidget *tree = m_ui->getLoaderTree();
    // chosenItem 可能是空指针（"这一版一个能装的都没有"那次：连那两档都没有，底下只剩一条
    // 灰的）。那种情况下这一列本来就没有"现在钉着的那一条"可指 —— 什么都不做。
    if (tree != nullptr && wasExpanded && chosenItem != nullptr) {
        tree->setCurrentItem(chosenItem);
    }
    // 列表是灌在**已经展开**的那一条底下的：条目换了一批，那棵树的高度得跟着重算
    // （MainWindowUI 那边挂在 expanded/collapsed 上，可这一批是原地换的、信号不会响）。
    // 收起来的那条不用管：它在展开的那一刻会自己算一次。
    if (wasExpanded) {
        item->setExpanded(false);
        item->setExpanded(true);
    }
}

void MainWindow::onLoaderVersionsFetched(const QString &gameVersion, int loaderIndex, bool success,
                                         const LoaderMeta::LoaderVersionList &versions,
                                         const QString &errorMessage) {
    // 这一发回复可能已经过时了（用户换了游戏版本）。按"这条回复讲的是不是现在这一页"来判，
    // 过时的直接丢 —— 填错地方的下场是"点了 Fabric 却看到 Forge 的版本号"，那不报错、
    // 只会装错东西。
    if (gameVersion != m_installGameVersion) {
        return;
    }
    // 那棵树是按**下标**摆的，日志里那句"是哪个加载器"要的是名字，所以这里翻译回枚举；
    // 反函数在这边失手（表没改全）就当没收到 —— 宁可那一条底下空着，也不要填错。
    VersionManager::LoaderKind kind;
    if (!VersionManager::loaderKindFromIndex(loaderIndex, &kind)) {
        return;
    }
    QTreeWidgetItem *item = loaderItem(loaderIndex);
    if (item == nullptr) {
        return;
    }
    if (!success) {
        // 拉不到：那一条底下**只留那两档**，外加一条灰的「列表没取到」把话说到 ——
        // 不钉版本这条路是不需要这份列表的（安装器自己会挑，问的是另一份东西：
        // promotions / maven 清单，见 installLoaderVersion 那条路），所以"列不出来"不该把
        // 安装整件事堵死，但也不能一声不吭。
        // 两档都**不灰**：这一刻我们根本不知道有没有正式版（那份列表没到手），而"让安装器
        // 自己挑"这条路由数据层去挑、挑不出来会在安装那条路上说清楚。
        // 具体原因（超时？404？）写进日志 —— 那是给排查用的，不是给用户读的。
        fillLoaderVersionItems(loaderIndex, LoaderMeta::LoaderVersionList(),
                               /*includeChannels=*/true);
        auto *failed = new QTreeWidgetItem(item);
        failed->setText(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::loaderVersionFailedText());
        failed->setDisabled(true);
        appendLog(tr("%1 在 %2 上有哪些版本没取到：%3")
                      .arg(VersionManager::loaderDisplayName(kind), gameVersion, errorMessage));
        return;
    }
    if (versions.isEmpty()) {
        // **问到了、就是没有**（四种加载器上都可能：Forge 还没支持某个刚发的快照）。
        // 这时候底下**不留那两档**（用户原话："如果对应版本不支持，就不要保留自动了"）：
        // 留着它等于让用户挑一条必然装不出来的路 —— 安装那条路会以"X 还没有支持 Y 的版本"
        // 收场，那一下本来在这一眼就该说完（用户原话："这个情况下的 列表没取到 换成无可用
        // 版本"）。
        //
        // NOTE: 2026-09-23 起"**有预览版、没有正式版**"的那种版本**不算**这一支 ——
        //       用户原话："beta 版虽然不稳定，但是也能使用"，所以那一份列表非空、照列，
        //       只是稳定那一行是灰的。
        // 于是这条加载器**一个能点的子条目都没有**，也就选不中它 —— 这正是"这一条我装不了"
        // 该有的样子；底下那条灰的「无可用版本」只是把原因写出来。
        fillLoaderVersionItems(loaderIndex, LoaderMeta::LoaderVersionList(),
                               /*includeChannels=*/false);
        auto *none = new QTreeWidgetItem(item);
        none->setText(MainWindowUI::loaderTreeKeyColumn(),
                      MainWindowUI::loaderVersionUnavailableText());
        none->setDisabled(true);
        appendLog(tr("%1 还没有支持 %2 的版本。")
                      .arg(VersionManager::loaderDisplayName(kind), gameVersion));
        return;
    }
    // 这一版上一条正式版都没有（真实例子：NeoForge + 26.3，11 个构建全是 `-beta`）：
    // 用户还什么都没挑的时候，把默认那一档**挪到「最新版」**上 —— 不然什么都没点、点安装，
    // 走的是稳定档那条必然装不出来的路（那一行同时也是灰的，见 fillLoaderVersionItems）。
    //
    // NOTE: 只在他**没挑过**的时候改（空串）。他已经挑过的那一格不许动 —— 那是他自己的
    //       选择，哪怕挑的正是一条现在没列出来的版本号。
    if (pinnedLoaderVersion(loaderIndex).isEmpty()) {
        bool hasStable = false;
        for (const LoaderMeta::LoaderVersionEntry &entry : versions) {
            hasStable = hasStable || entry.stable;
        }
        if (!hasStable) {
            m_chosenLoaderVersion.insert(
                loaderIndex, LoaderMeta::loaderChannelKey(LoaderMeta::LoaderChannel::Newest));
        }
    }
    // 灌进去就完了 —— 不存缓存：这一批是进安装页时一次性问全的（requestAllLoaderVersions），
    // 每一条在这次安装页里只会被灌这一次。
    fillLoaderVersionItems(loaderIndex, versions, /*includeChannels=*/true);
}

// ---------------------------------------------------------------------------
// 下载页后面那几页：搜索 → 挑游戏版本 → 挑版本号 → 装进实例
// ---------------------------------------------------------------------------
//
// 这几页（整合包 / 模组 / 数据包 / 资源包 / 光影）走的是**同一条路**，区别只有"搜哪一类"
// 和"下到哪儿去"。
// 三层树里每一层都是用户点开才动：
//   第一层 资源   —— 进页面时空搜索词问一次（= 这一类里最热门的），回车重搜
//   第二层 游戏版本 —— 搜索响应里本来就带着这个资源支持哪些游戏版本，**不用再问接口**
//   第三层 版本号  —— 点开第二层那一下才问（一个资源 × 一个游戏版本 = 一条请求）
// 点第三层那一下 = 下它（下到实例的 mods/ 、datapacks/ 之类；整合包那条走缓存 + 导入）。
//
// 三层都是**平条目**（不是自绘控件），用的是版本树/加载器树那同一个代理，所以长得一样
// （见 VersionTreeDelegate 第三个用户那段）。

/// 「这一条在第几层」。0 = 资源 / 1 = 游戏版本 / 2 = 版本号；不在树上回 -1。
///
/// NOTE: 代理那边认层用的是"有没有爹"（isChannel），这边认层用的是"往上数几层" ——
///       两处必须说的是同一件事，而"往上数几层"这句话在这里写一次（见头文件那段）。
int MainWindow::resourceRowDepth(const QTreeWidgetItem *item) {
    if (item == nullptr) {
        return -1;
    }
    int depth = 0;
    const QTreeWidgetItem *walk = item;
    while (walk->parent() != nullptr) {
        walk = walk->parent();
        ++depth;
    }
    return depth;
}

QTreeWidgetItem *MainWindow::addResourcePlaceholderRow(QTreeWidget *tree, const QString &text) {
    if (tree == nullptr) {
        return nullptr;
    }
    auto *item = new QTreeWidgetItem(tree);
    item->setText(MainWindowUI::versionTreeKeyColumn(), text);
    // 灰的（禁用）—— 和加载器树里「正在获取…」「列表没取到」那两行是同一种样子、
    // 也是同一种意思："这一行不是能点的东西"。判据见 onResourceItemActivated 开头。
    // NOTE: 不用 setFlags(Qt::ItemIsEnabled) 那一套"能看不能选"：代理是看 enabled 位
    //       整行淡 50% 的（见 VersionTreeDelegate 里 rowOpacity），禁用位才是"灰"的唯一出处。
    item->setDisabled(true);
    return item;
}

QTreeWidgetItem *MainWindow::addResourcePlaceholderChild(QTreeWidgetItem *parent,
                                                        const QString &text) {
    if (parent == nullptr) {
        return nullptr;
    }
    auto *item = new QTreeWidgetItem(parent);
    item->setText(MainWindowUI::versionTreeKeyColumn(), text);
    item->setDisabled(true);
    return item;
}

bool MainWindow::kindForSearchRequestKey(const QString &key, ResourceCatalog::Kind *out,
                                         ResourceCatalog::Source *outSource) const {
    // 拼钥匙和认钥匙读的是同一个函数（searchRequestKey）—— 所以这里不解析字符串，
    // 一条一条比。有几类，比几次。
    //
    // NOTE: **来源也要比一圈**（2026-09-25）。钥匙里带来源（见 searchRequestKey 那段），
    //       只比"默认那个来源"的话，CurseForge 那一发回来时一把都对不上 —— 症状是
    //       "切到 CurseForge 按了搜索，那一页什么都没有、也不报错"（结果被当成"不是我们
    //       发的"整发丢掉）。
    for (ResourceCatalog::Kind kind : ResourceCatalog::allKinds()) {
        for (ResourceCatalog::Source source : ResourceCatalog::allSources()) {
            if (ResourceCatalog::searchRequestKey(kind, source) == key) {
                if (out != nullptr) {
                    *out = kind;
                }
                if (outSource != nullptr) {
                    *outSource = source;
                }
                return true;
            }
        }
    }
    return false;
}

void MainWindow::ensureResourcePageLoaded(ResourceCatalog::Kind kind) {
    QTreeWidget *tree = m_ui->getDownloadResourceTree(ResourceCatalog::kindKey(kind));
    if (tree == nullptr) {
        return;
    }
    if (tree->topLevelItemCount() > 0) {
        // 这一页已经有东西了（用户搜过、或者刚才进来时搜了一次）。**不重搜**：来回换分类
        // 每次都问一遍，既慢、又是在拿限流额度换同一份东西。
        return;
    }
    startResourceSearch(kind);
}

void MainWindow::refreshDownloadCategory(int row) {
    const QStringList keys = MainWindowUI::downloadCategoryKeys();
    if (row < 0 || row >= keys.size()) {
        // 过滤器交上来的行号理论上一定在表里（那一行是 MainWindowUI 按这张表建出来的），
        // 越界只可能是"表和列表对不上了"那种时候 —— 什么都不做比按错一页去做强。
        return;
    }
    // 点的那一类要是现在没选中的，先切过去 —— 不切的话请求飞出去了、结果落在看不见的
    // 那一页上，用户看到的就是"点了没反应"。这一条对**五条一视同仁**（「游戏」也是：
    // 它刷新的是版本树，那棵树就长在它自己那一页上）。
    //
    // NOTE: 切过去那一下会触发 currentRowChanged → ensureResourcePageLoaded()，而那一页
    //       要是还空着，那儿**自己就会搜一次**。所以下面分两半：切过去的路上那一发已经
    //       发出去了就直接完事（再发一发是同一份东西问两遍，白烧一次限流额度）；页面上
    //       已经有东西了才轮到我们按现在的搜索词重搜一次 —— 那才是用户按这个按钮要的
    //       那件事：**重新问一遍**，而不是"第一次问"。
    QListWidget *categories = m_ui->getDownloadCategoryList();
    const bool wasCurrent = categories == nullptr || categories->currentRow() == row;
    if (!wasCurrent) {
        categories->setCurrentRow(row);
    }
    if (row == MainWindowUI::downloadCategoryGameIndex()) {
        // 「游戏」那一页是**在线版本清单**，不是资源搜索（那张表上根本没有"游戏"这个键，
        // kindFromCategoryKey 也认不出来，所以它得自己一条分支）。
        //
        // 摆不摆"正在…"那一行：**不摆**。版本树那边和资源表不是一回事 ——
        // refreshAvailableVersions() 每一发都重建**恰好那几条顶层**（「最新版本」那一组 +
        // 四条分类，那是它的活），中途插一行灰的会被它整个冲掉；而且版本树本来就不会因为
        // "正在等"而空着：老那棵一直挂在
        // 那儿（fetchAvailableVersions 失败时也保留上一份好清单，见 onVersionsFetched）。
        // 用户看到的是"点了一下、树没消失、过一会儿多的版本冒出来"，正是要的样子。
        appendLog(tr("正在重新获取游戏版本列表…"));
        m_versionManager->fetchAvailableVersions();
        return;
    }
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    if (!ResourceCatalog::kindFromCategoryKey(keys.at(row), &kind)) {
        return;  // 认不出来 = 这一行不是我们建的那几类之一，没有"重问一遍"这回事
    }
    if (!wasCurrent) {
        QTreeWidget *tree = m_ui->getDownloadResourceTree(ResourceCatalog::kindKey(kind));
        if (tree != nullptr && tree->topLevelItemCount() > 0) {
            startResourceSearch(kind);
        }
        return;
    }
    // 已经在这一页上：按搜索框里现在那个词重搜（空词 = 这一类里最热门的，和第一次进来同一个规矩）。
    // NOTE: **图标缓存一个字节都不动**。这一发重新走的是 search()，回来的那批 hit 里
    //       project_id 和上次多半是同一批，requestIcon() 在内存/磁盘缓存里就命中了
    //       （见 ResourceStore 里 requestIcon 那段"两级缓存"）—— 用户要的"图标留缓存"正是
    //       这个：重新问一遍**不会**把已经存下来的图标丢掉、也不会重新下一遍。
    //       这一下**也不重新核对**（核对的闸是"启动器这一趟运行"一道，重搜不重置它，
    //       见 m_iconRevalidated）—— 核对要跟着"每次打开启动器"，不能跟着"每点一下"。
    //       唯一的例外是"这一批刚才放弃了"：那阵子没核成的会在这里得到第二次机会
    //       （search() 会把那道闸放回去，见它开头那两句）。
    startResourceSearch(kind);
}

// ---------- 每一页顶上那一行筛子（2026-09-25）----------
//
// 这一段的整段理由在头文件里那段：界面只搭空框，**内容全在这儿灌**，读也只从下面这几个
// 访问器读。谁要是绕开它们自己去 `combo->currentText()`，症状是"界面上筛了、请求里没筛"
// —— 不报错、也看不出哪儿不对。

ResourceCatalog::Source MainWindow::downloadFilterSource(ResourceCatalog::Kind kind) const {
    ResourceCatalog::Source source = ResourceCatalog::Source::Modrinth;
    QComboBox *combo = m_ui->getDownloadFilterSourceCombo(ResourceCatalog::kindKey(kind));
    if (combo != nullptr) {
        // 认不出来（框是空的 / 里面不是我们灌的那个键）就停在 Modrinth —— 和"这一页
        // 没有来源框"是同一个结果，也正是加这一栏之前的行为。
        ResourceCatalog::sourceFromKey(combo->currentData().toString(), &source);
    }
    return source;
}

QString MainWindow::downloadFilterGameVersion(ResourceCatalog::Kind kind) const {
    // **读框里的字**，不是 currentData()。这一格是可输入的（用户原话「版本可自行输入」），
    // 手打一个版本号时列表里没有那一项、currentData() 是空的 —— 而框里明明写着那个版本号。
    QComboBox *combo = m_ui->getDownloadFilterVersionCombo(ResourceCatalog::kindKey(kind));
    return combo == nullptr ? QString() : combo->currentText().trimmed();
}

QString MainWindow::downloadFilterLoader(ResourceCatalog::Kind kind) const {
    // 别的页上这个控件**不存在**（MainWindowUI 里是"只有模组页才建"，不是建了再藏），
    // 所以空指针就是"这一页没有这一栏"。回空串 = 不筛，正是这一栏没有时该有的行为。
    QComboBox *combo = m_ui->getDownloadFilterLoaderCombo(ResourceCatalog::kindKey(kind));
    return combo == nullptr ? QString() : combo->currentData().toString();
}

QString MainWindow::downloadFilterCategory(ResourceCatalog::Kind kind) const {
    QComboBox *combo = m_ui->getDownloadFilterTypeCombo(ResourceCatalog::kindKey(kind));
    return combo == nullptr ? QString() : combo->currentData().toString();
}

void MainWindow::fillDownloadFilterVersionOptions() {
    // 版本那一格：**一整份在线清单**，没有「全部」那一项 —— 这一格是可输入的，
    // "不筛"就是框里空着（那时框里显示灰字「全部版本」，见 downloadFilterVersionPlaceholder）。
    const QStringList versions = m_versionManager->availableVersions();
    for (ResourceCatalog::Kind kind : ResourceCatalog::allKinds()) {
        QComboBox *combo = m_ui->getDownloadFilterVersionCombo(ResourceCatalog::kindKey(kind));
        if (combo == nullptr) {
            continue;
        }
        // 灌之前把用户手里那串字记下来、灌完放回去。清单是**异步**回来的（开机一次、
        // 用户按左栏那个刷新按钮又一次），而 clear() 会把框里编辑着的东西一起清掉：
        // 他刚打完版本号、清单正好回来，那串字就没了，可筛子还在生效 —— 界面和请求对不上。
        const QString current = combo->currentText();
        const QSignalBlocker blocker(combo);
        combo->clear();
        combo->addItems(versions);
        if (!current.isEmpty()) {
            combo->setCurrentText(current);
        }
    }
}

void MainWindow::refreshDownloadFilterOptions(ResourceCatalog::Kind kind) {
    const QString key = ResourceCatalog::kindKey(kind);
    const ResourceCatalog::Source source = downloadFilterSource(kind);

    // 「模组加载器」那一栏（**只有模组页有**，别的页 getDownloadFilterLoaderCombo 回空指针）。
    if (QComboBox *loaderCombo = m_ui->getDownloadFilterLoaderCombo(key)) {
        const QSignalBlocker blocker(loaderCombo);
        loaderCombo->clear();
        for (const ResourceCatalog::FilterOption &option : ResourceCatalog::loaderOptions(source)) {
            loaderCombo->addItem(option.label, option.key);
        }
        // 换来源之后退回「全部」（= 第一项）。两边的键根本不是一套（Modrinth 的 `fabric`
        // 对 CurseForge 的 `4`），留着上一个来源选中那一项，就是拿 A 家的号去问 B 家 ——
        // 而且它不报错，只会搜出来一片空。
        loaderCombo->setCurrentIndex(0);
    }

    // 「类型」那一栏。**回空表就把整个小组藏起来**（连标签一起，见 MainWindowUI 里那段）：
    // 摆一个只有「全部」的框等于骗人。现在只有 Modrinth 的数据包会空。
    QComboBox *typeCombo = m_ui->getDownloadFilterTypeCombo(key);
    QWidget *typeBox = m_ui->getDownloadFilterTypeBox(key);
    if (typeCombo != nullptr) {
        const QSignalBlocker blocker(typeCombo);
        typeCombo->clear();
        // 「全部」那一项是 categoryOptions() 自己带的第一项，这里不补也不动顺序。
        for (const ResourceCatalog::FilterOption &option :
             ResourceCatalog::categoryOptions(source, kind)) {
            typeCombo->addItem(option.label, option.key);
        }
        typeCombo->setCurrentIndex(typeCombo->count() > 0 ? 0 : -1);
    }
    if (typeBox != nullptr) {
        typeBox->setVisible(typeCombo != nullptr && typeCombo->count() > 0);
    }
}

void MainWindow::fillDownloadFilters() {
    for (ResourceCatalog::Kind kind : ResourceCatalog::allKinds()) {
        const QString key = ResourceCatalog::kindKey(kind);
        // 来源那一格：两项，顺序照 allSources()（Modrinth 在前：它是不要 key 的那个、
        // 也是"原来就能用"的那个）。**没有「全部」这一项** —— 一个页面上同时只搜一个
        // 来源，那是这一格的语义。
        if (QComboBox *sourceCombo = m_ui->getDownloadFilterSourceCombo(key)) {
            const QSignalBlocker blocker(sourceCombo);
            sourceCombo->clear();
            for (ResourceCatalog::Source source : ResourceCatalog::allSources()) {
                sourceCombo->addItem(ResourceCatalog::sourceLabel(source),
                                     ResourceCatalog::sourceKey(source));
            }
            sourceCombo->setCurrentIndex(0);
        }
        // 加载器 / 类型那两项跟着来源走，所以灌法是"按当前来源算一遍"。
        refreshDownloadFilterOptions(kind);
    }
    fillDownloadFilterVersionOptions();
}

void MainWindow::resetDownloadFilters(ResourceCatalog::Kind kind) {
    const QString key = ResourceCatalog::kindKey(kind);
    // 名称那一格：就是原来那个搜索框。
    if (QLineEdit *edit = m_ui->getDownloadSearchEdit(key)) {
        edit->clear();
    }
    // 版本那一格是可输入的，`setCurrentIndex(-1)` 不一定把框里的字清掉（值是从
    // currentText() 读的），所以**显式**再清一遍那串字。
    if (QComboBox *versionCombo = m_ui->getDownloadFilterVersionCombo(key)) {
        const QSignalBlocker blocker(versionCombo);
        versionCombo->setCurrentIndex(-1);
        versionCombo->clearEditText();
    }
    // 加载器和类型那两格：第一项就是「全部」（键空串），回到它 = 不筛。
    if (QComboBox *loaderCombo = m_ui->getDownloadFilterLoaderCombo(key)) {
        const QSignalBlocker blocker(loaderCombo);
        loaderCombo->setCurrentIndex(loaderCombo->count() > 0 ? 0 : -1);
    }
    if (QComboBox *typeCombo = m_ui->getDownloadFilterTypeCombo(key)) {
        const QSignalBlocker blocker(typeCombo);
        typeCombo->setCurrentIndex(typeCombo->count() > 0 ? 0 : -1);
    }
    // 「来源」**不清**：它不是"条件"，是"去哪儿问"。清成"没有来源"这件事不存在
    // （这一格只有两项、总有一项是选中的），而且用户刚在设置里配好的 CurseForge key
    // 就是给这一格用的 —— 清一下把他踢回 Modrinth，那是替他做了个他没说的决定。
    //
    // NOTE: 这一层**只清**，搜不搜是调用方的事 —— 那颗键上接着"清完重搜一发"（2026-09-26
    //       用户原话「修改任何内容…都刷新一下」，接线见构造函数里那段）。
    appendLog(tr("已清空%1那一页的搜索条件。").arg(ResourceCatalog::kindLabel(kind)));
}

void MainWindow::startResourceSearch(ResourceCatalog::Kind kind) {
    const QString key = ResourceCatalog::kindKey(kind);
    QTreeWidget *tree = m_ui->getDownloadResourceTree(key);
    if (tree == nullptr) {
        return;
    }
    const QLineEdit *edit = m_ui->getDownloadSearchEdit(key);
    const QString query = edit == nullptr ? QString() : edit->text().trimmed();

    // 上一批结果连同展开状态一起清掉，换上一行灰的「正在…」。不摆这一行的话，用户按回车
    // 之后界面**完全没有反应**（请求一秒上下才回来），而"点了没反应"是用户唯一会当成
    // 启动器坏了的情况。
    tree->clear();
    addResourcePlaceholderRow(tree,
                             query.isEmpty()
                                 ? tr("正在获取最热门的%1…").arg(ResourceCatalog::kindLabel(kind))
                                 : tr("正在搜索「%1」…").arg(query));
    // 记下**这一发真的问了什么**：回来时那句「没有搜到「xx」」里的 xx 要是那一次的词。
    // 读编辑框里现在的字是错的 —— 用户完全可能在等结果的时候又敲了几个字（那几下还没发出去，
    // 回车才算一发）。
    m_resourceQueries.insert(key, query);

    // 另外四个筛子（版本 / 加载器 / 类型 / 来源）在这儿一次读完。读法全在 downloadFilterXxx()
    // 那四个访问器里（**唯一出处**），这里只负责把它们递给 store —— 空串一律 = 不筛，
    // 所以"没选"和"没有这一栏"在这里是同一件事，不用分岔。
    const ResourceCatalog::Source source = downloadFilterSource(kind);
    const QString gameVersion = downloadFilterGameVersion(kind);
    const QString loader = downloadFilterLoader(kind);
    const QString category = downloadFilterCategory(kind);

    appendLog(query.isEmpty() ? tr("正在获取最热门的%1…").arg(ResourceCatalog::kindLabel(kind))
                              : tr("正在搜索%1「%2」…")
                                    .arg(ResourceCatalog::kindLabel(kind), query));
    // 日志里把这一发**带了哪些条件**说全（那两格的值写的是框里显示的字，不是键 ——
    // CurseForge 的类型键是 `6814` 这种号，写进日志没人看得懂）。搜出来"怎么是这么几条"，
    // 十有八九就是某个筛子在起作用；不写下来的话只能对着结果猜。
    QStringList conditions;
    conditions.append(tr("来源 %1").arg(ResourceCatalog::sourceLabel(source)));
    if (!gameVersion.isEmpty()) {
        conditions.append(tr("版本 %1").arg(gameVersion));
    }
    const auto shownText = [this, &key](QComboBox *combo) {
        return combo == nullptr ? QString() : combo->currentText();
    };
    if (!loader.isEmpty()) {
        conditions.append(
            tr("模组加载器 %1").arg(shownText(m_ui->getDownloadFilterLoaderCombo(key))));
    }
    if (!category.isEmpty()) {
        conditions.append(tr("类型 %1").arg(shownText(m_ui->getDownloadFilterTypeCombo(key))));
    }
    appendLog(tr("搜索条件：%1").arg(conditions.join(QStringLiteral(" · "))));

    // 钥匙里要带来源（见 ResourceCatalog::searchRequestKey 那段）：用户刚点完 Modrinth 又
    // 切成 CurseForge 时，前一趟回来的结果不该被当成"我现在等的那一发"填进树里。
    m_resourceStore->search(ResourceCatalog::searchRequestKey(kind, source), kind, source, query,
                            gameVersion, loader, category);
}

void MainWindow::fillResourceHitRows(QTreeWidget *tree, ResourceCatalog::Kind kind,
                                     const ResourceCatalog::SearchResult &result) {
    if (tree == nullptr) {
        return;
    }
    const int column = MainWindowUI::versionTreeKeyColumn();
    for (const ResourceCatalog::Hit &hit : result.hits) {
        auto *item = new QTreeWidgetItem(tree);
        // 身份 = project_id：图标回来时按它在树里找回这一行、点开游戏版本时按它拼版本请求。
        item->setData(column, Qt::UserRole, hit.projectId);
        // 这一条是哪一类。点最底下那一层要按它算"下到哪儿去"（见 resourceKindRole()）——
        // 存的是**分类键**（不是枚举的整数值），回来时走 kindFromCategoryKey 认，
        // 认不出来就是"这一行不是我们建的"。
        item->setData(column, MainWindowUI::resourceKindRole(), ResourceCatalog::kindKey(kind));
        // 这一条是从哪个来源搜出来的（点第二层要问对那一侧，见 resourceSourceRole()）。
        item->setData(column, MainWindowUI::resourceSourceRole(),
                      ResourceCatalog::sourceKey(hit.source));
        // 图标那一格的**身份**（带来源前缀，见 resourceIconIdRole()）：图标回来时按它认行。
        item->setData(column, MainWindowUI::resourceIconIdRole(), hit.iconCacheId());
        item->setText(column, hit.title);

        // 第二行小字：「作者 · 下载量」。下载量那串字只有一处出处（formatDownloads），
        // 界面上别处显示下载量也要读它。作者缺了就少一段。
        QStringList bits;
        if (!hit.author.isEmpty()) {
            bits.append(hit.author);
        }
        bits.append(ResourceCatalog::formatDownloads(hit.downloads));
        item->setData(column, MainWindowUI::rowSubTextRole(), bits.join(QStringLiteral(" · ")));

        // 简介一行放不下（代理只画一行、超了截断），鼠标停上去看全文。
        // 空简介不挂 tooltip —— 挂一个空的等于鼠标一停就冒一个空框。
        if (!hit.description.isEmpty()) {
            item->setToolTip(column, hit.description);
        }

        // 第二层：游戏版本。**不用再问接口** —— 搜索响应里本来就带着"这个资源支持哪些
        // 游戏版本"（Hit::gameVersions）。顺序由 gameVersionChoices() 统一成"新的在前"。
        // 第二层**一律折**（`false`）：那枚「显示快照版」勾选框在详情页顶上，这一棵树
        // 没有它 —— 用户要的是"快照版归到对应的大版本里"，这一层就是那句话的落点。
        const QStringList choices = ResourceCatalog::gameVersionChoices(hit.gameVersions, false);
        if (choices.isEmpty()) {
            // 一个游戏版本都没列（数据包那类）。摆一行灰的说清楚 —— 不然这一行看着能展开、
            // 点开却是空的。
            addResourcePlaceholderChild(item, tr("这个%1没有列出支持的游戏版本。")
                                                  .arg(ResourceCatalog::kindLabel(kind)));
        }
        for (const QString &gameVersion : choices) {
            auto *gameItem = new QTreeWidgetItem(item);
            // 身份 = 游戏版本号。第三层还不建（点开这一条才去问，见 onResourceItemActivated）。
            gameItem->setData(column, Qt::UserRole, gameVersion);
            gameItem->setText(column, gameVersion);
        }

        // 整条 hit 存一份（键就是上面那个图标身份，也是"这一行是哪个资源"的钥匙）。
        // 条目上那四个角色装不下简介 / slug / 更新时间，而点开详情页三样都要
        // （见 m_resourceHits 那段 NOTE）。
        m_resourceHits.insert(hit.iconCacheId(), hit);
        // 图标**最后**要：ResourceStore 命中内存缓存时是同步发 iconReady 的，那一行得先在
        // 那儿（见 requestIcon 的 NOTE）—— 所以这一句必须排在最后。
        // 收的是整条 hit 不是 id：要挑地址（能用 webp 就走那条小 7 倍的，见 requestIcon）。
        m_resourceStore->requestIcon(hit);
    }
}

void MainWindow::onResourceSearchFinished(const QString &requestKey,
                                         const ResourceCatalog::SearchResult &result) {
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    ResourceCatalog::Source fromSource = ResourceCatalog::Source::Modrinth;
    if (!kindForSearchRequestKey(requestKey, &kind, &fromSource)) {
        // 不是我们认得的那几页发的（别的东西也用这个 store 时才会走到）。
        return;
    }
    // 这一发回来的时候，用户可能已经把来源换成另一个了 —— 两个来源的请求是**各自飞的**
    // （钥匙不同，store 不会互相掐掉）。不是这一页**现在**选的那个来源就丢掉：留着会把
    // 上一家那批结果摆在一个写着另一家的页面上，而页面上那句"来源 CurseForge"就成了假话。
    if (fromSource != downloadFilterSource(kind)) {
        return;
    }
    const QString key = ResourceCatalog::kindKey(kind);
    QTreeWidget *tree = m_ui->getDownloadResourceTree(key);
    if (tree == nullptr) {
        return;
    }
    // 「正在搜索…」那一行先撤掉（下面每一条分支都会摆上自己那一行/那一批）。
    tree->clear();

    if (!result.ok) {
        // 「没搜成」和「没搜到」是**两句不同的话**（加载器树那边「无可用版本」和
        // 「列表没取到」踩过同一个坑，见记忆里那条）。这里说的是前一句。
        //
        // NOTE: 这里**不弹窗**。产品那条"错误要弹窗"管的是"必须让用户看见的东西不能只写
        //       日志"（日志面板早就没有了，写进去等于没写）；这一句**不是**那种：那一行就
        //       摆在用户刚点的那一页上、就是他正在看的地方，而且话是接口/网络自己说的
        //       （超时 / 连不上 / 还是"你问得太勤了，过一分钟再试" —— 最后那种正是最该
        //       看清、又最不该用一个模态框打断他的）。回车再按一次就是重试。
        //       这一条和加载器树那条「列表没取到」是同一种形状，所以做法也一样。
        //       真要弹窗的是那些**没有别的地方能显示**的错（见 onResourceDownloadFinished）。
        appendLog(tr("✗ 搜索%1失败：%2").arg(ResourceCatalog::kindLabel(kind), result.problem));
        addResourcePlaceholderRow(tree, result.problem);
        return;
    }
    if (result.hits.isEmpty()) {
        // **问到了、就是没有** —— 不算错，也**不弹窗**（一个"什么都没发生"的弹窗比没有
        // 更烦人）。用记下来的那个搜索词，别读编辑框里现在的字（理由见 startResourceSearch）。
        const QString query = m_resourceQueries.value(key);
        appendLog(tr("%1 一条也没有搜到（%2）。")
                      .arg(ResourceCatalog::kindLabel(kind), query.isEmpty() ? tr("空搜索词") : query));
        addResourcePlaceholderRow(tree, query.isEmpty()
                                            ? tr("这一类里一条都没有。")
                                            : tr("没有搜到「%1」。").arg(query));
        return;
    }
    fillResourceHitRows(tree, kind, result);
}

void MainWindow::onResourceIconReady(const QString &iconId, const QPixmap &icon) {
    // 没有图标（作者没传过 / 这次没取到 / 被限流）也**要**给这一行挂上东西：一个全透明的
    // 方块。为什么：代理是"条目给了图标，文字才让出前面那一格"的（见 VersionTreeDelegate
    // 里 iconRect 那段），什么都不挂的话这一行的字会顶到最左边，和上下那些有图标的行
    // 参差不齐 —— 而"这一列没对齐"在用户眼里是"这软件做得很糙"，不是"这个模组没图标"。
    // 全透明是为了不再多画一个"没有图标"的形状（那个形状本身也是要设计的东西）。
    // NOTE: **不是错误**、不弹窗：项目没上传过图标是完全正常的一件事。
    QPixmap mark = icon;
    if (mark.isNull()) {
        mark = QPixmap(MainWindowUI::resourceTreeIconSize(), MainWindowUI::resourceTreeIconSize());
        mark.fill(Qt::transparent);
    }
    const int column = MainWindowUI::versionTreeKeyColumn();
    // 按**图标身份**（`Hit::iconCacheId()`，见 resourceIconIdRole）找回**现在**树上那一行 ——
    // 不按裸 project_id：两个来源的 id 空间会撞，撞上就是把另一个来源同号那个资源的图标
    // 画到这一行上（而且不报错）。也不存条目指针：重搜会把整棵树清掉，存下来的指针就是
    // 悬空的 —— 按身份找，找得到才画，找不到就什么都不做。
    for (ResourceCatalog::Kind kind : ResourceCatalog::allKinds()) {
        QTreeWidget *tree = m_ui->getDownloadResourceTree(ResourceCatalog::kindKey(kind));
        if (tree == nullptr) {
            continue;
        }
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = tree->topLevelItem(i);
            if (item->data(column, MainWindowUI::resourceIconIdRole()).toString() != iconId) {
                continue;
            }
            item->setData(column, MainWindowUI::rowIconRole(), mark);
        }
    }
    // 详情页正开着、回来的又正好是它那一张 → 换上头卡那一格。
    //
    // NOTE: 判据是**图标身份**而不是"页开着"就换：图标是一条一条回来的，页上那一格只该
    //       认自己这一条（换个说法：这是同一套"按身份认行"的规矩，只不过这儿认的是那一页）。
    //       头卡那一格比列表大（64 vs 32），缩放由那一页自己做（见 setIcon）。
    if (m_resourcePage.open && iconId == m_resourcePage.iconKey
        && m_resourceDetailPage != nullptr) {
        m_resourceDetailPage->setIcon(icon);
    }

    // ---------- 「文件管理」那几页（2026-09-27）----------
    //
    // 同一个身份也可能是**实例里某个文件**换来的那一个（见 requestInstanceFileIcons）——
    // 这一格和上面那棵树是两回事（那边是"搜出来的资源"，这边是"盘上躺着的文件"），
    // 所以两条路都要走一遍，谁都不抢谁。
    //
    // ★ 这一句**必须排在 m_resourceStore->requestIcon() 之后**（也就是"图标回来时"）：
    //   盘上已经有缓存的话 requestIcon() 是**同步**发 iconReady 的（见它那条 NOTE），
    //   所以这里是"记了账、立刻就用上"，而记账那一步在 requestIcon 之前就写完了。
    const QStringList filePaths = m_instanceIcons.pathsByIconKey.value(iconId);
    if (!filePaths.isEmpty()) {
        for (const QString &path : filePaths) {
            // 空的也存 —— 它记的是"这一条问过了、没有"（见 m_instanceIconMissing 那段）。
            m_instanceFileIcons.insert(instanceIconCacheKey(path), icon);
        }
        pushInstanceFileIcons();
    }
}

void MainWindow::onResourceItemActivated(QTreeWidgetItem *item) {
    if (item == nullptr) {
        return;
    }
    // 灰的那些（「正在搜索…」「没有搜到…」「正在获取…」「这个游戏版本上没有…」）不是能点的
    // 东西：点它什么都不该发生。放在最前面 —— "点不动"这件事对三层是同一个道理。
    if (item->isDisabled()) {
        return;
    }
    const int depth = resourceRowDepth(item);
    if (depth == 0 || depth == 1) {
        // 第一层（一个资源）和第二层（一个游戏版本）都是**只展开 / 收起** ——
        // 用户原话：「点开模组后是选择游戏版本」。"点开"是"看看有什么"，看一眼不等于选了它。
        const bool expand = !item->isExpanded();
        item->setExpanded(expand);
        if (depth != 1 || !expand || item->childCount() > 0) {
            return;
        }
        // 第二层**第一次**被点开：这时才去问"这个资源在这个游戏版本上，哪些版本可下"。
        // 判据是"底下一条都没有" —— 这一层点开之前一定是空的，问过之后无论有没有结果都会
        // 留下至少一行（版本号、或者一句灰的"没有"），所以"空"就等于"还没问过"。
        // 这也顺带成了"再点一次重试"那条路：没取到时底下留着一行灰的，把那一条删掉
        // （重搜/重新点开）就会再问一次。
        const QTreeWidgetItem *resourceItem = item->parent();
        const QString projectId = resourceItem == nullptr
                                      ? QString()
                                      : resourceItem->data(MainWindowUI::versionTreeKeyColumn(),
                                                           Qt::UserRole).toString();
        // 这一条是从哪个来源搜出来的（见 resourceSourceRole()）：**必须问对那一侧** ——
        // 两个来源的 id 空间会撞，问错了不报错，只会显示另一个资源的版本列表。
        // 认不出来（那一行不是我们建的）就当 Modrinth，和"没选来源"的默认一致。
        ResourceCatalog::Source source = ResourceCatalog::Source::Modrinth;
        if (resourceItem != nullptr) {
            ResourceCatalog::sourceFromKey(
                resourceItem->data(MainWindowUI::versionTreeKeyColumn(),
                                   MainWindowUI::resourceSourceRole()).toString(),
                &source);
        }
        const QString gameVersion =
            item->data(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole).toString();
        if (projectId.isEmpty() || gameVersion.isEmpty()) {
            return;
        }
        addResourcePlaceholderChild(item, tr("正在获取…"));
        // NOTE: **不按加载器筛**（loader 传空）。为什么：这一层的游戏版本是从**搜索响应**
        //       来的，而那份响应里没有"这个资源的哪个游戏版本属于哪个加载器"这份对应关系
        //       （categories 是整个资源的标签，不是某个版本的）。要按加载器筛就得先知道
        //       用户在给哪个实例装 —— 那件事在点版本号那一下才定（见
        //       downloadResourceVersion 里那道加载器对不上的闸口）。
        m_resourceStore->fetchVersions(
            ResourceCatalog::versionsRequestKey(projectId, gameVersion, QString(), source),
            projectId, source, gameVersion);
        return;
    }
    if (depth == 2) {
        // 第三层（一个版本号）：**这一下就是"下它"**。
        downloadResourceVersion(item);
    }
}

void MainWindow::fillResourceVersionRows(QTreeWidgetItem *gameItem,
                                        const QList<ResourceCatalog::VersionEntry> &versions,
                                        const QString &problem) {
    if (gameItem == nullptr) {
        return;
    }
    // 先撤掉「正在获取…」。这一层点开之前一定是空的，所以这里清掉的只有那一行占位。
    while (gameItem->childCount() > 0) {
        delete gameItem->takeChild(0);
    }
    const int column = MainWindowUI::versionTreeKeyColumn();

    if (!problem.isEmpty()) {
        // 「没取到」（超时 / 限流 / 那条资源被作者删了）。**不弹窗**：用户要的是"展开看看"，
        // 进来已经有话说了；而且这一层再点一次（收起再展开）就是重新问一次。
        addResourcePlaceholderChild(gameItem, problem);
        return;
    }
    if (versions.isEmpty()) {
        addResourcePlaceholderChild(gameItem, tr("这个游戏版本上没有可下载的版本。"));
        return;
    }

    // 三层行前面那个图标：跟着它所属的那个资源（同一列里三个草方块会让人以为是三个
    // Minecraft 版本）。资源那条的图标是异步回来的，这里读到的可能还是空的 —— 那就
    // 按老规矩留给代理（空的等于没给）。
    const QVariant resourceIcon =
        gameItem->parent() == nullptr
            ? QVariant()
            : gameItem->parent()->data(column, MainWindowUI::rowIconRole());

    for (const ResourceCatalog::VersionEntry &version : versions) {
        auto *item = new QTreeWidgetItem(gameItem);
        // 身份 = 版本 **id**（不是版本号）：点它那一下要拿它回 m_resourceVersions 里换
        // "这一版的文件是哪几个"，而文件清单是按 id 找的。
        item->setData(column, Qt::UserRole, version.id);
        // 正文写**文件名**：接下来要落到 mods/ 里的就是这个名字（作者给这一版起的名字
        // 常常已经在文件名里写全了，两个都写一行放不下）。
        const ResourceCatalog::FileEntry *file = version.primaryFile();
        item->setText(column, file == nullptr ? version.versionNumber : file->filename);
        // 第二行小字：release/beta/alpha · 加载器 · 发布日期（各缺各的就不补）。
        QStringList bits;
        if (!version.versionType.isEmpty()) {
            bits.append(version.versionType);
        }
        if (!version.loaders.isEmpty()) {
            bits.append(version.loaders.join(QLatin1Char('/')));
        }
        if (!version.datePublished.isEmpty()) {
            // ISO 8601（`2026-06-13T…`）只取日期那一段。
            bits.append(version.datePublished.left(10));
        }
        item->setData(column, MainWindowUI::rowSubTextRole(), bits.join(QStringLiteral(" · ")));
        if (resourceIcon.isValid()) {
            item->setData(column, MainWindowUI::rowIconRole(), resourceIcon);
        }
    }
}

void MainWindow::onResourceVersionsFinished(const QString &requestKey,
                                           const QList<ResourceCatalog::VersionEntry> &versions,
                                           const QString &problem) {
    const int column = MainWindowUI::versionTreeKeyColumn();
    // 先看是不是**详情页**那一发（钥匙里游戏版本、加载器都是空的，和树上那些一定带一个
    // 游戏版本的钥匙不会撞，见 openResourceDetailPage）。
    if (m_resourcePage.open && requestKey == m_resourcePage.requestKey) {
        if (!problem.isEmpty()) {
            // 「没取到」（超时 / 限流 / 那条资源被作者删了）。**不弹窗**：用户是点进这一页
            // 来看东西的，页上已经有话说了；再点一次这个资源就是重新问一次。
            m_resourcePage.versions.clear();
            m_resourcePage.loaded = true;   // 回来过了（回的是"没取到"）
            m_resourceDetailPage->setPlaceholder(problem);
            return;
        }
        m_resourcePage.versions = versions;
        m_resourcePage.loaded = true;
        // 摆哪一份由 chip 决定（这一步同时也处理"筛完一条都不剩"和"一条都没发过"两种空）。
        fillResourcePageGroups();
        return;
    }
    // 不解析那把钥匙（钥匙的格式只有 ResourceCatalog 知道）。改成**按内容重算**：把树上
    // 每一个"资源 × 游戏版本"重算一次钥匙，和回来的这一把比。几页加起来也就几十对，
    // 一次线性扫描。这样钥匙的拼法只有一处（versionsRequestKey），而且不必在条目上存
    // "这一发是谁发的"（那个指针在重搜时会被整个清掉，留着就是悬空的）。
    for (ResourceCatalog::Kind kind : ResourceCatalog::allKinds()) {
        QTreeWidget *tree = m_ui->getDownloadResourceTree(ResourceCatalog::kindKey(kind));
        if (tree == nullptr) {
            continue;
        }
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *resourceItem = tree->topLevelItem(i);
            const QString projectId = resourceItem->data(column, Qt::UserRole).toString();
            if (projectId.isEmpty()) {
                continue;
            }
            // 这一条是从哪个来源搜出来的：**必须跟着它算钥匙**。钥匙里带来源（见
            // versionsRequestKey 那段），拿默认那一档去比的话，CurseForge 那条路上一把都
            // 对不上 —— 症状是"展开游戏版本，一直停在「正在获取…」"，而请求其实已经回来了。
            ResourceCatalog::Source source = ResourceCatalog::Source::Modrinth;
            ResourceCatalog::sourceFromKey(
                resourceItem->data(column, MainWindowUI::resourceSourceRole()).toString(), &source);
            for (int j = 0; j < resourceItem->childCount(); ++j) {
                QTreeWidgetItem *gameItem = resourceItem->child(j);
                const QString gameVersion = gameItem->data(column, Qt::UserRole).toString();
                if (gameVersion.isEmpty()
                    || ResourceCatalog::versionsRequestKey(projectId, gameVersion, QString(), source)
                           != requestKey) {
                    continue;
                }
                // 就是它。清单存下来（点第三层那一下要用 URL / sha1 / 大小），再摆第三层。
                // NOTE: 这张表只往里放、不清理（键是"资源 × 游戏版本"，值是一份纯数据的
                //       小清单）。不清理不会读到旧数据：第三层那些行只在问过之后才存在，
                //       而问过就一定覆盖过同一个键。
                m_resourceVersions.insert(requestKey, versions);
                fillResourceVersionRows(gameItem, versions, problem);
                return;
            }
        }
    }
    // 树里找不到对应的行 = 用户重搜过了，这一发的结果已经不是现在这棵树上的东西了。
    // 丢掉是对的 —— 把旧结果填进新列表比丢掉糟得多。
}

void MainWindow::downloadResourceVersion(QTreeWidgetItem *item) {
    if (item == nullptr) {
        return;
    }
    const int column = MainWindowUI::versionTreeKeyColumn();
    QTreeWidgetItem *gameItem = item->parent();
    QTreeWidgetItem *resourceItem = gameItem == nullptr ? nullptr : gameItem->parent();
    if (gameItem == nullptr || resourceItem == nullptr) {
        return;
    }
    const QString versionId = item->data(column, Qt::UserRole).toString();
    const QString gameVersion = gameItem->data(column, Qt::UserRole).toString();
    const QString projectId = resourceItem->data(column, Qt::UserRole).toString();
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    if (!ResourceCatalog::kindFromCategoryKey(
            resourceItem->data(column, MainWindowUI::resourceKindRole()).toString(), &kind)) {
        return;  // 这一行不是我们建的（正常走不到）
    }
    // 这一版的文件清单：随"点开游戏版本"那一发回来的。没有它就下不了（正常走不到 ——
    // 有第三层这些行就说明那一发回来过）。说清楚，别静静地什么都不做。
    //
    // 钥匙里的来源要**从这一行自己读**（和点开时那把一模一样，见 onResourceItemActivated）：
    // 少带来源的话，CurseForge 那一页上每一版都查不到自己的清单 —— 用户按下去拿到的是
    // 一句「这一版的下载信息还没取到」，可它明明就摆在眼前。
    ResourceCatalog::Source source = ResourceCatalog::Source::Modrinth;
    ResourceCatalog::sourceFromKey(
        resourceItem->data(column, MainWindowUI::resourceSourceRole()).toString(), &source);
    const QString requestKey =
        ResourceCatalog::versionsRequestKey(projectId, gameVersion, QString(), source);
    const auto cached = m_resourceVersions.constFind(requestKey);
    if (cached == m_resourceVersions.constEnd()) {
        InlineMessage::warning(m_ui->overlay(), tr("下载失败"),
            tr("这一版的下载信息还没取到。把上面那个游戏版本收起再点开一次，然后重试。"));
        return;
    }
    const ResourceCatalog::VersionEntry *chosen = nullptr;
    for (const ResourceCatalog::VersionEntry &entry : *cached) {
        if (entry.id == versionId) {
            chosen = &entry;
            break;
        }
    }
    if (chosen == nullptr) {
        // 行在、清单里没有它 = 这一行是上一次搜索留下的（重搜过）。用户没做错什么，不弹窗。
        return;
    }
    // 剩下的（选哪个实例、落到哪个目录、文件清单、加载器那道闸）详情页那条路也要走一遍，
    // 全在 installResourceVersion 里 —— **这一条路不自己再算一份**。
    installResourceVersion(kind, resourceItem->text(column), *chosen, source);
}

void MainWindow::installResourceVersion(ResourceCatalog::Kind kind, const QString &displayTitle,
                                        const ResourceCatalog::VersionEntry &version,
                                        ResourceCatalog::Source source) {
    // ★ 2026-09-28：模组这一路**先看一眼前置**（用户原话：「在模组下载页，选择模组后显示
    //   其依赖，点击下载后弹出窗口，让用户可以点击一键下载所有缺少的依赖」）。
    //
    // 判据：这一版声明的必装前置里，**目标实例还缺哪几个**（`ModDependencies` 那边算，
    // 纯函数）。一条不缺就走下面那一句 —— 那时候**一个请求都不多发**，和从前逐字一样。
    //
    // NOTE: 只在**模组**这一路看。资源包 / 数据包 / 光影那一版列的是 `minecraft` /
    //       `datapack` / `iris`，它们和"前置模组"没有关系（同一个理由见下面那道加载器闸）；
    //       整合包更不用 —— 它的依赖在清单里，那条路本来就是整个包一起装。
    if (kind == ResourceCatalog::Kind::Mod && !m_dependencyCheck.active) {
        const QString instanceId = selectedVersionId(m_ui->getInstalledVersionCombo());
        if (!instanceId.isEmpty()) {
            const ModDependencies::Target target = dependencyTargetFor(instanceId, source);
            bool installedKnown = false;
            const QSet<QString> installed = installedModKeys(instanceId, &installedKnown);
            const QList<ModDependencies::Lookup> missing =
                ModDependencies::missingRequiredDependencies(version, installed, target);
            if (!missing.isEmpty()) {
                startDependencyCheck(kind, displayTitle, version, target, missing, installedKnown);
                return;
            }
        }
        // 没有选实例 / 没有缺的：交给下面那一句，它的判断和话术一个字都不变。
    }
    installResourceVersionNow(kind, displayTitle, version, {});
}

ModDependencies::Target MainWindow::dependencyTargetFor(const QString &instanceId,
                                                       ResourceCatalog::Source source) {
    ModDependencies::Target target;
    target.instanceId = instanceId;
    if (instanceId.isEmpty()) {
        return target;
    }
    // 游戏版本和加载器都从**这个实例自己**读（不是从页面上那颗 chip）：前置要装进的是
    // 这个实例，而 chip 是用户在挑"看哪个版本"，两者可以不一样。
    const VersionDisplayParts parts = m_versionManager->displayParts(instanceId);
    // `realVersion` = 这个实例**基于哪个原版**（PCL 那类合并实例只有它能说得出口，
    // 见 VersionDisplayParts 那段）；它就是"这个实例的游戏版本"。
    target.gameVersion = parts.realVersion;
    // ★ 加载器名走 instanceLoaderName()（理由同下面那道加载器闸）：PCL 那类合并实例
    //   （用户盘上 70 个里 67 个）从 parts.loaderName 读出来是空的，而那两个筛参数空着
    //   等于"不筛" —— 问回来的可能是任意加载器的前置。
    target.loader = instanceLoaderName(parts, m_versionManager, nullptr);
    // 来源跟着**这一条资源是从哪一家搜出来的**走（调用方从条目 / 页面状态里读出来递进来）：
    // 前置也去同一家问 —— 两家的项目号不是一套，拿 Modrinth 的号去 CurseForge 问，
    // 问出来的是**另一个项目**（或者什么都没有），而且不报错。
    target.source = source;
    if (target.source == ResourceCatalog::Source::CurseForge) {
        // 根地址那一处判走 ModpackImport::curseForgeApiBase（**唯一出处**，和
        // ResourceStore::fetchProjects 里面用的是同一条 —— 两边不一致的话，问出去的是
        // 一条地址、解析回来的是另一条的服务，症状是"名字永远取不到"）。
        target.apiBase = ModpackImport::curseForgeApiBase(m_settings->curseForgeApiKey(),
                                                         m_settings->curseForgeApiBaseOverride());
    }
    return target;
}

QSet<QString> MainWindow::installedModKeys(const QString &instanceId, bool *known) const {
    QSet<QString> keys;
    const QString modsDir = instanceFileDirs(instanceId).value(QStringLiteral("mod"));
    if (modsDir.isEmpty()) {
        if (known != nullptr) {
            *known = false;
        }
        return keys;
    }
    const QString prefix = modsDir + QLatin1Char('/');
    for (auto it = m_instanceFileIdentities.constBegin();
         it != m_instanceFileIdentities.constEnd(); ++it) {
        // 键是 `路径\n大小\n时间`（见 instanceIconCacheKey）⇒ 前缀比得上就说明这个文件在
        // 这个实例的 mods 里。**必须带那个 `/`**：不带的话 `…/mods2/x.jar` 也会算进来。
        if (it.key().startsWith(prefix)) {
            keys.insert(it.value());
        }
    }
    // ★ `known` 说的是"这个实例的 mods 到底认过没有"，而不是"认出来几个" ——
    //   一个模组都没认出（新实例、全是自己改过的 jar）和"没进过文件管理页"都表现为空表，
    //   而界面上那两句话不一样（见 startDependencyCheck）。
    if (known != nullptr) {
        *known = !m_instanceIcons.versionId.isEmpty() &&
                 m_instanceIcons.versionId == instanceId;
    }
    return keys;
}

void MainWindow::startDependencyCheck(ResourceCatalog::Kind kind, const QString &displayTitle,
                                      const ResourceCatalog::VersionEntry &version,
                                      const ModDependencies::Target &target,
                                      const QList<ModDependencies::Lookup> &missing,
                                      bool installedKnown) {
    m_dependencyCheck = DependencyCheck{};
    m_dependencyCheck.active = true;
    m_dependencyCheck.kind = kind;
    m_dependencyCheck.title = displayTitle;
    m_dependencyCheck.version = version;
    m_dependencyCheck.instanceId = target.instanceId;
    m_dependencyCheck.target = target;
    m_dependencyCheck.missing = missing;
    // "这个实例的 mods 到底认过没有"（见 installedModKeys 的 known 出参）—— 它只决定
    // 弹框那句话怎么写（"这几个还没装" vs "无法确认你装了没有"），所以跟着这一趟存着。
    m_dependencyCheck.installedKnown = installedKnown;

    QStringList ids;
    ids.reserve(missing.size());
    for (const ModDependencies::Lookup &lookup : missing) {
        if (!ids.contains(lookup.dependency.projectId)) {
            ids.append(lookup.dependency.projectId);
        }
    }
    // ★ 消费方名字（`dependency`）**必须跟详情页那一趟（`detail`）分开**：两处问的完全可能
    //   是同一批号，而 `fetchProjects()` 是按钥匙去重的（同一把钥匙再来一发把前一发掐掉）。
    //   撞上的症状是"先发的那一趟永远等不到回信"，屏幕上一点异常都没有。
    m_dependencyCheck.titleRequestKey = ModDependencies::projectsKeyFor(
        QStringLiteral("dependency"), ids, m_dependencyCheck.target.source);
    appendLog(ModDependencies::dependencyLookupLogText(missing.size()));
    m_resourceStore->fetchProjects(m_dependencyCheck.titleRequestKey, ids,
                                   m_dependencyCheck.target.source);
}

void MainWindow::onDependencyProjectsFinished(const QString &requestKey,
                                             const QList<ResourceCatalog::Hit> &hits,
                                             const QString &problem) {
    if (!m_dependencyCheck.active || requestKey != m_dependencyCheck.titleRequestKey) {
        return; // 不是这一趟等的（用户中途又点了别的下载）
    }
    // 名字取不到**不算错**：那一条退成项目号显示（见 ModDependencies::buildPlan 那段），
    // 而"没有名字"挡不住"要不要一起下"这个决定。所以这里只记一句日志。
    if (!problem.isEmpty()) {
        appendLog(tr("前置模组的名字没取到：%1").arg(problem));
    }
    // 名字进**会话级**那个缓存（详情页那一行读的是同一份）。取不到名字的那几条**不写**：
    // 写一个空串进去等于把"还没问过"说成"问过了、它没名字"。
    rememberProjectTitles(hits, m_dependencyCheck.target.source);

    // 逐条去问"它有哪些版本"。**同一批发出去**（一把钥匙一个项目，互相不撞）——
    // 串起来问的话，N 个前置就是 N 个往返，而它们本来就互不相干。
    m_dependencyCheck.answers.clear();
    m_dependencyCheck.pending = 0;
    for (const ModDependencies::Lookup &lookup : m_dependencyCheck.missing) {
        ++m_dependencyCheck.pending;
        m_resourceStore->fetchVersions(lookup.requestKey, lookup.dependency.projectId,
                                       m_dependencyCheck.target.source,
                                       m_dependencyCheck.target.gameVersion,
                                       m_dependencyCheck.target.loader);
    }
    if (m_dependencyCheck.pending == 0) {
        finishDependencyCheck(); // 一条都不缺（走不到：missing 非空才进这一趟）
    }
}

void MainWindow::onDependencyVersionsFinished(const QString &requestKey,
                                             const QList<ResourceCatalog::VersionEntry> &versions,
                                             const QString &problem) {
    if (!m_dependencyCheck.active) {
        return;
    }
    bool ours = false;
    for (const ModDependencies::Lookup &lookup : m_dependencyCheck.missing) {
        if (lookup.requestKey == requestKey) {
            ours = true;
            break;
        }
    }
    if (!ours) {
        return; // 详情页那一发（或者别的页面）的回复，不关这一趟的事
    }
    // ★ 失败时**不往 answers 里放空表**：`buildPlan` 那边"钥匙缺席"和"回来了但是空的"
    //   是两句不同的话（"没问到"和"它就是没有版本"）—— 放空表等于把前者说成后者。
    if (problem.isEmpty()) {
        m_dependencyCheck.answers.insert(requestKey, versions);
    }
    if (m_dependencyCheck.pending > 0) {
        --m_dependencyCheck.pending;
    }
    if (m_dependencyCheck.pending == 0) {
        finishDependencyCheck();
    }
}

void MainWindow::finishDependencyCheck() {
    DependencyCheck check = m_dependencyCheck;
    m_dependencyCheck.active = false;

    const bool installedKnown = check.installedKnown;

    // 名字从会话缓存里查（详情页那一行读的是同一份）。缺的那几条**退成项目号**——
    // 列出来总比"有 2 个前置没找到"强（见 ModDependencies::buildPlan 那段），
    // 而"为什么没名字"已经写进日志了（见 onDependencyProjectsFinished）。
    QHash<QString, QString> titles;
    for (const ModDependencies::Lookup &lookup : check.missing) {
        const QString name =
            m_projectTitles.value(projectTitleKey(check.target.source, lookup.dependency.projectId));
        if (!name.isEmpty()) {
            titles.insert(lookup.dependency.projectId, name);
        }
    }

    const ModDependencies::Plan plan =
        ModDependencies::buildPlan(check.missing, check.answers, titles);

    QStringList names;
    names.reserve(check.missing.size());
    for (const ModDependencies::Lookup &lookup : check.missing) {
        const QString name = titles.value(lookup.dependency.projectId);
        names.append(name.isEmpty() ? lookup.dependency.projectId : name);
    }

    const QString note = ModDependencies::unresolvedNoteText(plan.unresolved);
    QList<ModDependencies::Resolved> extras;
    if (plan.downloads.isEmpty()) {
        // 一条都没解开：**照样把这个模组装上**，但得如实说一句"前置一条都没找到"。
        // 不说的话，用户点完下载看到的就是"下好了"，而进游戏那一下就崩。
        QString text = ModDependencies::dependencyPromptText(names);
        if (!note.isEmpty()) {
            text += QStringLiteral("\n\n") + note;
        }
        InlineMessage::warning(m_ui->overlay(), ModDependencies::dependencyPromptTitle(), text);
    } else {
        QString text = installedKnown
                           ? ModDependencies::dependencyPromptText(names)
                           : ModDependencies::dependencyPromptUnknownText(names);
        if (!note.isEmpty()) {
            text += QStringLiteral("\n\n") + note;
        }
        const bool downloadAll = InlineMessage::question(
            m_ui->overlay(), ModDependencies::dependencyPromptTitle(), text,
            ModDependencies::dependencyAllButtonText(),
            ModDependencies::dependencyOnlyThisButtonText(), false);
        if (downloadAll) {
            extras = plan.downloads;
        }
    }

    installResourceVersionNow(check.kind, check.title, check.version, extras);
}

// ============================ 实例更新检测 ============================
//
// 用户 2026-09-28：「实例设置那里做一个更新检测」。
//
// 【这一趟在做什么（顺序不能换）】
//
//   ① 把这个实例那四个能下载的目录**枚举**一遍（模组 / 资源包 / 数据包 / 光影 ——
//      和文件管理页那几页同一份目录表）；
//   ② 后台线程里**反查一次**（`InstanceIconLookup`：哈希 → 项目号 → 项目对象）。
//      顺手带出来的是每个文件的 **sha1** —— 判"盘上这一份是第几版"就靠它
//      （见 `Found::sha1` 那段：这一位本来就是为它加的）；
//   ③ 每个项目**问一发版本列表**（按这个实例的游戏版本 + 加载器筛！不筛的话，"最新版"
//      会是另一个游戏版本的版本，于是报告说"有新版本"、点进去装不上）；
//   ④ `judgeUpdate()` 逐条判，攒成一份报告。
//
// 【为什么别的事都不做】
//
// 不自动下载、不自动改盘、不问"要不要更新" —— 用户要的是"检测"。报告里只说事实
//（哪几个有新版本、最新的那一版叫什么），后面那一步是他自己的事。

void MainWindow::checkInstanceUpdates(const QString &instanceId) {
    if (instanceId.isEmpty()) {
        return;
    }
    // 已经在跑：**到此为止**（不排队、不重开）。重开那一趟的代价是把几十兆 jar 再读一遍、
    // 把刚才那几发再问一遍，而屏幕上那一份报告正在弹着 —— 用户看不到任何差别。
    if (m_updateCheck.thread != nullptr) {
        appendLog(tr("更新检测已经在跑了，等它出结果。"));
        return;
    }

    // ① 候选文件（四个"能下载的资源"目录，不含存档/原理图 —— 那两样没有"更新"这回事）。
    QStringList paths;
    const QHash<QString, QString> dirs = instanceFileDirs(instanceId);
    const QStringList wantedDirs = {QStringLiteral("mod"), QStringLiteral("resourcepack"),
                                    QStringLiteral("datapack"), QStringLiteral("shader")};
    for (const QString &kindKey : wantedDirs) {
        const QString dir = dirs.value(kindKey);
        if (dir.isEmpty()) {
            continue;
        }
        QDir folder(dir);
        // 不过滤"启用/禁用"：`X.jar.disabled` 也是那个文件，它的 sha1 照样能判。
        const QFileInfoList entries =
            folder.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name);
        for (const QFileInfo &info : entries) {
            paths.append(info.absoluteFilePath());
        }
    }
    if (paths.isEmpty()) {
        InlineMessage::information(m_ui->overlay(), tr("更新检测"),
                                 tr("这个实例里没有可以查更新的文件。"));
        return;
    }

    m_updateCheck = UpdateCheckRun{};
    m_updateCheck.instanceId = instanceId;
    appendLog(tr("正在检查「%1」里的资源有没有新版本（%2 个文件）...")
                  .arg(instanceDisplayName(instanceId))
                  .arg(paths.size()));

    InstanceIconLookup::Options options;
    // 三个口子和 ResourceStore / VersionManager / 文件管理页那三处**同一份设置**。
    options.curseForgeApiKey = m_settings->curseForgeApiKey();
    options.modrinthApiBaseOverride = m_settings->modrinthApiBaseOverride();
    options.curseForgeApiBaseOverride = m_settings->curseForgeApiBaseOverride();

    // 上一趟那个线程对象还挂着就先放它走（同 startInstanceFileIconLookup 那段 ——
    // 每查一次都在父对象底下攒一个僵尸 QThread 是要还的）。
    if (m_updateCheck.thread != nullptr) {
        m_updateCheck.thread->deleteLater();
        m_updateCheck.thread = nullptr;
    }
    m_updateCheck.thread = QThread::create([this, paths, options, instanceId]() {
        const InstanceIconLookup::Result result = InstanceIconLookup::lookup(paths, options);
        QMetaObject::invokeMethod(
            this,
            [this, result, instanceId]() { onUpdateCheckLookupFinished(result, instanceId); },
            Qt::QueuedConnection);
    });
    m_updateCheck.thread->setParent(this);
    m_updateCheck.thread->start();
}

void MainWindow::onUpdateCheckLookupFinished(const InstanceIconLookup::Result &result,
                                            const QString &instanceId) {
    // 回来看的这一趟是不是**现在**要的那一趟（用户完全可能又点了一次检查、或者换了实例）。
    if (m_updateCheck.instanceId != instanceId) {
        return;
    }
    // 反查那一趟自己的毛病（读不动几个文件 / 整趟没跑完）——报告末尾照实说一句。
    // **不算失败**：认出来的那些照样判（那是真的）。
    if (!result.problem.isEmpty()) {
        m_updateCheck.lookupNote = result.problem;
    } else if (result.unreadable > 0) {
        m_updateCheck.lookupNote =
            tr("有 %1 个文件读不动（多半被别的程序占着），这一批里没有它们。")
                .arg(result.unreadable);
    }

    const ModDependencies::Target target =
        dependencyTargetFor(instanceId, ResourceCatalog::Source::Modrinth);
    for (const InstanceIconLookup::Found &found : result.found) {
        if (found.hit.projectId.isEmpty()) {
            continue;
        }
        ModDependencies::InstalledItem item;
        item.path = found.path;
        item.iconKey = found.hit.iconCacheId();
        item.title = found.hit.title;
        item.sha1 = found.sha1;
        // ★ 钥匙跟着**这一条自己的来源**走（两家的 id 空间不是一套，问错边问出来的是
        //   另一个项目）。来源在 hit 上，不在 target 上。
        const QString key = ModDependencies::versionsKeyFor(
            QStringLiteral("update"), found.hit.projectId, target.gameVersion, target.loader,
            found.hit.source);
        m_updateCheck.itemsByKey[key].append(item);
    }

    m_updateCheck.waitingKeys.clear();
    for (auto it = m_updateCheck.itemsByKey.constBegin();
         it != m_updateCheck.itemsByKey.constEnd(); ++it) {
        m_updateCheck.waitingKeys.insert(it.key());
    }
    if (m_updateCheck.waitingKeys.isEmpty()) {
        // 一个都没认出来（新实例、或者全是自己改过的 jar）——**照样出一份报告**：
        // "一个都认不出来"正是用户需要知道的事，闷着不说是最坏的。
        finishUpdateCheck(instanceId);
        return;
    }

    // 逐个项目问版本列表。**一把钥匙一发**，互相不撞。项目号从钥匙里读不出来（钥匙是拼
    // 出来的一串），所以从这一组里那一条自己的 `iconKey`（`<来源>-<项目号>`）上取。
    for (auto it = m_updateCheck.itemsByKey.constBegin();
         it != m_updateCheck.itemsByKey.constEnd(); ++it) {
        const ModDependencies::InstalledItem &any = it.value().first();
        ResourceCatalog::Source source = ResourceCatalog::Source::Modrinth;
        const int dash = any.iconKey.indexOf(QLatin1Char('-'));
        if (dash > 0) {
            ResourceCatalog::sourceFromKey(any.iconKey.left(dash), &source);
        }
        const QString projectId = dash > 0 ? any.iconKey.mid(dash + 1) : any.iconKey;
        m_resourceStore->fetchVersions(it.key(), projectId, source, target.gameVersion,
                                       target.loader);
    }
}

void MainWindow::onUpdateCheckVersionsFinished(const QString &requestKey,
                                              const QList<ResourceCatalog::VersionEntry> &versions,
                                              const QString &problem) {
    if (!m_updateCheck.waitingKeys.contains(requestKey)) {
        return; // 不是这一趟等的（详情页那一发 / 前置那一趟 / 已经判过的那把）
    }
    m_updateCheck.waitingKeys.remove(requestKey);

    // 那一发没成 → **空表**进 judgeUpdate：那几条判"说不清"（**不是**"已是最新"）。
    const QList<ResourceCatalog::VersionEntry> &answer =
        problem.isEmpty() ? versions : QList<ResourceCatalog::VersionEntry>();
    for (const ModDependencies::InstalledItem &item : m_updateCheck.itemsByKey.value(requestKey)) {
        m_updateCheck.verdicts.append(ModDependencies::judgeUpdate(item, answer));
    }
    if (m_updateCheck.waitingKeys.isEmpty()) {
        finishUpdateCheck(m_updateCheck.instanceId);
    }
}

void MainWindow::finishUpdateCheck(const QString &instanceId) {
    const UpdateCheckRun run = m_updateCheck;
    m_updateCheck = UpdateCheckRun{};

    // 报告：**有新版本的排最前面**（那才是用户点这一下的目的），然后是"说不清"，
    // 最后才是"已是最新"。同一个档里按名字排 —— 不排的话每条的位置都随哈希表顺序变，
    // 同一个实例连查两次的名单顺序都不一样。
    QList<ModDependencies::UpdateVerdict> outdated;
    QList<ModDependencies::UpdateVerdict> unknown;
    QList<ModDependencies::UpdateVerdict> current;
    for (const ModDependencies::UpdateVerdict &verdict : run.verdicts) {
        switch (verdict.state) {
        case ModDependencies::UpdateState::Outdated:
            outdated.append(verdict);
            break;
        case ModDependencies::UpdateState::UpToDate:
            current.append(verdict);
            break;
        case ModDependencies::UpdateState::Unknown:
            unknown.append(verdict);
            break;
        }
    }
    const auto byName = [](const ModDependencies::UpdateVerdict &a,
                           const ModDependencies::UpdateVerdict &b) {
        return a.item.title < b.item.title;
    };
    std::sort(outdated.begin(), outdated.end(), byName);
    std::sort(unknown.begin(), unknown.end(), byName);
    std::sort(current.begin(), current.end(), byName);

    const auto displayName = [](const ModDependencies::UpdateVerdict &verdict) {
        if (!verdict.item.title.isEmpty()) {
            return verdict.item.title;
        }
        // 名字取不到就用文件名 —— 那是**盘上那一条**的名字，用户能照它找到文件。
        return QFileInfo(verdict.item.path).fileName();
    };

    QStringList lines;
    for (const ModDependencies::UpdateVerdict &verdict : outdated) {
        lines.append(QStringLiteral("· %1 —— %2 %3")
                         .arg(displayName(verdict), ModDependencies::updateOutdatedText(),
                              verdict.latestVersionNumber));
    }
    for (const ModDependencies::UpdateVerdict &verdict : unknown) {
        lines.append(QStringLiteral("· %1 —— %2：%3")
                         .arg(displayName(verdict), ModDependencies::updateUnknownText(),
                              verdict.reason));
    }
    for (const ModDependencies::UpdateVerdict &verdict : current) {
        lines.append(QStringLiteral("· %1 —— %2")
                         .arg(displayName(verdict), ModDependencies::updateUpToDateText()));
    }

    const int checked = run.verdicts.size();
    const QString summary = ModDependencies::updateSummaryText(checked, outdated.size());
    QString text = summary;
    if (!lines.isEmpty()) {
        text += QStringLiteral("\n\n") + lines.join(QLatin1Char('\n'));
    }
    if (!run.lookupNote.isEmpty()) {
        text += QStringLiteral("\n\n") + run.lookupNote;
    }

    appendLog(tr("更新检测（%1）：%2").arg(instanceDisplayName(instanceId), summary));
    for (const QString &line : lines) {
        appendLog(QStringLiteral("  ") + line);
    }
    InlineMessage::information(m_ui->overlay(), tr("更新检测"), text);
}

void MainWindow::installResourceVersionNow(ResourceCatalog::Kind kind,
                                           const QString &displayTitle,
                                           const ResourceCatalog::VersionEntry &version,
                                           const QList<ModDependencies::Resolved> &extras) {
    const bool isModpack = kind == ResourceCatalog::Kind::Modpack;

    // 目标实例：**以下拉框为准**（树和下拉框是同一份数据的两种看法，见 setupConnections 里
    // 那条 currentIndexChanged）。整合包那一路不用它 —— 下回来的是 .mrpack，接着走导入，
    // 实例名是在那条路上问出来的。
    const QString instanceId =
        isModpack ? QString() : selectedVersionId(m_ui->getInstalledVersionCombo());
    if (!isModpack && instanceId.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("没有选中实例"),
                             tr("先在「已安装版本」里选一个实例，再挑要装进它的版本。"));
        return;
    }

    // 下到哪儿去。实例那几类是 `<实例目录>/<子目录>`（mods / datapacks / resourcepacks /
    // shaderpacks）；整合包走缓存目录 —— 它不能进 versions/（那底下每一个子目录都会被当成
    // 一个实例）。
    const QString folder = ResourceCatalog::folderForKind(kind);
    const QString defaultDir = isModpack
                                   ? AppPaths::modpackCacheDir()
                                   : AppPaths::versionDir(instanceId) +
                                         QLatin1Char('/') + folder;
    if (!isModpack && folder.isEmpty()) {
        return;  // folderForKind 说了"这一类不走这条安装路"，而它又不是整合包
    }

    // ★★ 2026-09-28：**要不要问一句"存哪儿"**（用户原话见 LauncherSettings 里那一节，
    //    那里是这一支判据唯一的语义出处）。判据就一条：
    //
    //        弹框 = 设置里没勾  或者  这一栏不是从实例设置里点进来的
    //
    //    "是不是从实例设置里点进来的"读的是 `m_downloadFromInstanceId` —— 和那颗
    //    「← 返回实例设置」用的是**同一个值**，因为它俩说的本来就是同一件事（这一栏是从哪个
    //    实例来的）。不另开一个布尔：两个值一旦分开，"露着返回键、却弹了保存框"这种事迟早
    //    会出现。
    //
    // NOTE: 这一支是**在哪里下载**那一层唯一的岔路，所以它写在"算落点"这一句上，而不是
    //       写在按钮那一路：详情页里点一行、下载页里双击一行、文件管理里点名字进详情页再
    //       点一行 —— 三条路都会走到这儿，分流只有一处才不会有哪一条路漏掉。
    // NOTE: 用户在下载页里待久了之后（比如从实例 A 点进来、又自己把「已安装版本」换成 B），
    //       这一位还是非空、于是不再问。这是**刻意的**：那时候落点仍然是"当前实例"（下拉框
    //       里那一个），而这一项勾选框上写的就是"直接放进当前实例"。
    QString destinationDir = defaultDir;
    // 这一批算"谁在动盘"。默认是那个实例；用户挑了别处的目录时它必须是空的（那底下没有任何
    // 实例在被写，锁着它等于白挡别人启动）。
    QString busyInstanceId = instanceId;
    if (!m_settings->downloadAutoIntoInstance() || m_downloadFromInstanceId.isEmpty()) {
        const QString chosen = QFileDialog::getExistingDirectory(
            this, tr("下载到哪个文件夹"), defaultDir);
        if (chosen.isEmpty()) {
            return;  // 用户取消 = 这一版不下（一个字都不写盘）
        }
        destinationDir = chosen;
        if (QDir(destinationDir) != QDir(defaultDir)) {
            busyInstanceId.clear();
        }
    }

    const ResourceCatalog::FileEntry *file = version.primaryFile();
    if (file == nullptr || file->url.isEmpty()) {
        // 「接口没给地址」有两种，**不是同一句话**：一种是这一版真的没有文件；
        // 另一种是作者在项目设置里关掉了第三方分发（CurseForge 那条路，`downloadUrl`
        // 给的就是 null，见 FileEntry::unavailableNote）。后者要是也说成"没有可下载的文件"，
        // 用户会当成启动器坏了，然后一遍遍重试 —— 那句话在那儿就写好了，用它。
        const QString note = file == nullptr ? QString() : file->unavailableNote;
        InlineMessage::warning(m_ui->overlay(), tr("下载失败"),
                               note.isEmpty() ? tr("这一版没有可下载的文件。") : note);
        return;
    }

    // 加载器对不对得上。**这一层没有按加载器筛过**（见 onResourceItemActivated 里那段
    // NOTE），所以在这儿补一道闸：给一个 Fabric 实例装一个 Forge 的模组，症状是"游戏进去就
    // 崩"，而那一刻没人会想到是刚才那个模组。
    //
    // NOTE: 这道闸**只管模组**（2026-09-25 加数据包时收的）。理由：`VersionEntry::loaders`
    //       不是只有模组加载器 —— 资源包那一版列的是 `minecraft`、数据包列的是 `datapack`、
    //       光影列的是 `iris/optifine`，它们跟"这个实例装的是什么模组加载器"根本没有关系。
    //       不收的话，给一个 Forge 实例装资源包/数据包会**每次都弹一次**「加载器对不上」，
    //       而那句话（"这一版是给 minecraft 用的"）本身就说不通 —— 那种框弹多了，用户就
    //       再也不看这类提醒了，真正该拦的那一次也跟着一起被忽略。
    const bool loaderMatters = kind == ResourceCatalog::Kind::Mod;
    // 判据刻意做得**窄**（只拦"知道实例是什么加载器、且这一版只列了另一种"这一种）：
    //   · 不是模组（资源包 / 数据包 / 光影 / 整合包）→ 放行；
    //   · 认不出实例的加载器（纯原版，或者 json 里没写）→ 放行；
    //   · 这一版列了不止一种加载器（Quilt 之类兼容好几种）→ 放行；
    //   · 纯原版实例 + 一个带加载器的模组 → 也放行（认不出来的和纯原版在这里分不开，
    //     宁可漏拦也不要拦住一个本来装得上的）。
    // 拦下来的是**问一句**，不是禁止 —— 用户说"就要这么装"就照装。
    //
    // ★ 2026-09-27：比的这一下从前是 `contains()` —— 子串比较。于是这一个闸**恰好漏掉了它
    //   最该拦的那一对**：实例是 Forge、这一版是 NeoForge 的模组时，
    //   `"neoforge".contains("forge")` 为真 ⇒ 不报，直接装下去；而反过来（NeoForge 实例装
    //   只列了 forge 的模组）也一样漏。两个名字是**互为子串**的，这一对照的是"同一族里的
    //   另一个"，恰恰是症状最像"装上了、进去就崩"的那一种。
    //   改成一字不差地比（大小写不敏感：实例那边是显示名 `NeoForge`，模组那边是小写）。
    const VersionDisplayParts parts = m_versionManager->displayParts(instanceId);
    // ★ 加载器名走 instanceLoaderName()，**不是** `parts.loaderName`：后者只有我们自己的
    //   安装器会写，PCL 那类合并实例（用户盘上 70 个里 67 个）读出来是空的 —— 于是这道闸
    //   对它们**恒不放行**，"加载器对不上"那句话一次都弹不出来（它最该拦的那一对恰好就
    //   在这些实例上）。理由同 instanceModsUsable() 那段。
    QString instanceLoaderVersion;
    const QString instanceLoader =
        instanceLoaderName(parts, m_versionManager, &instanceLoaderVersion);
    if (loaderMatters && !instanceLoader.isEmpty() && version.loaders.size() == 1
        && version.loaders.first().compare(instanceLoader, Qt::CaseInsensitive) != 0) {
        const auto answer = InlineMessage::question(m_ui->overlay(), tr("加载器对不上"),
            tr("这个实例是 %1 %2，而这一版是给 %3 用的。\n\n"
               "装进去多半不会生效（游戏进去可能直接崩）。还要装吗？")
                .arg(instanceLoader, instanceLoaderVersion, version.loaders.first()), tr("是"), tr("否"), false);
        if (!answer) {
            return;
        }
    }

    QList<VersionManager::ResourceFile> files;
    VersionManager::ResourceFile resourceFile;
    resourceFile.url = file->url;
    resourceFile.filename = file->filename;
    resourceFile.sha1 = file->sha1;
    resourceFile.size = file->size;
    files.append(resourceFile);

    // ★ 2026-09-28：一起下的前置**并进同一批**（用户在框里按了「一起下载」）。
    //   并成一批而不是"接着再下一批"是有缘故的：`downloadResourceFiles()` 一批一个进度、
    //   一条"下好了"的日志、一次 `resourceDownloadFinished`。分两批的话，用户会先看到
    //   "下好了"、然后前置才开始下 —— 而那一刻他多半已经点进游戏了（那时候缺的正是前置）。
    //
    // NOTE: 前置的落点就是这个实例的 mods（`destinationDir` 是按 kind=模组算出来的）——
    //       前置本来就是模组，和主文件同一个目录，这一点不用另判。
    for (const ModDependencies::Resolved &extra : extras) {
        if (extra.file.url.isEmpty() || extra.file.filename.isEmpty()) {
            continue; // 走不到（resolveDependency 已经挡过一道），挡第二道不费什么
        }
        VersionManager::ResourceFile extraFile;
        extraFile.url = extra.file.url;
        extraFile.filename = extra.file.filename;
        extraFile.sha1 = extra.file.sha1;
        extraFile.size = extra.file.size;
        files.append(extraFile);
    }

    QString label = QStringLiteral("%1 %2").arg(displayTitle, version.versionNumber);
    if (!extras.isEmpty()) {
        // 日志和"下好了"那句话上写清楚这一批里**还有前置**（不然用户看到下了好几个文件、
        // 而屏幕上只说了一个模组的名字，会以为下错了）。
        label += tr("（含 %1 个前置）").arg(extras.size());
    }
    // 下完接着走导入那条路（那边会问实例名、挑 Java，然后解包、下清单里那几百个文件）。
    // 路径按**接口给的文件名**算 —— 落盘用的就是它（见 downloadResourceFiles），这里
    // 不另编一套命名。
    const QString modpackPath =
        isModpack ? destinationDir + QLatin1Char('/') + file->filename : QString();
    appendLog(tr("正在下载 %1（%2）。").arg(label, QDir::toNativeSeparators(destinationDir)));
    // busyInstanceId 传空的有两路：整合包下到缓存里（盘上没有任何实例在被写），以及用户
    // 自己挑了实例之外的目录那一路 —— 两路都不该锁住谁（见上面算落点那一段）。
    const bool started =
        m_versionManager->downloadResourceFiles(destinationDir, busyInstanceId, label, files);
    // ★ 登记**必须**在这一批真的开起来之后。被拒绝时上面那次调用**照样会发**一个
    //   `resourceDownloadFinished(label, false, …)`，label 还是同一个字符串 —— 先登记的话，
    //   那一下就会把记录 take() 走。这不是假想的：双击那一行"点它就是下它"时，第二发
    //   撞的正是这道闸，于是真的整合包下完时记录已经没了 → 界面弹「已经装进实例里了」，
    //   而实际**一个字节都没导入**。先问"开起来没有"就没有这个窗口。
    if (started && !modpackPath.isEmpty()) {
        m_pendingModpackImports.insert(label, modpackPath);
    }
}

// ============================ 资源详情页 ============================
//
// 用户原话：「下载页选择了模组以后进入一个独立界面，上方写模组图像 名字 来源，然后有转到
// modrinth/curseforge/mcmod.cn 的选项，中间一排版本，下方排列版本，和 pcl 类似」。
// "独立界面"按那条总目标落成**整页覆盖层**（`OverlayHost::execPage`，顶上四个选项卡和
// 底栏照旧露着，不另开窗）。
//
// 三件事的分工（写清楚免得下次改错地方）：
//   - **界面的样子**全在 ui/ResourceDetailPage.{hpp,cpp}（它不 include 任何 src/ 下的头）；
//   - **数据**全在 src/download/ResourceCatalog（拼网址、分组、时间那句、渠道那一档）；
//   - **下面这一片**是接线：谁去问网络、点哪颗按钮干什么、哪一行是"下它"。
//     所以这里没有一行"这一页长什么样"的代码，那一页也没有一处"要不要联网"的决定。

void MainWindow::setupResourceDetailPage() {
    if (m_resourceDetailPage != nullptr) {
        return;
    }
    m_resourceDetailPage = new ResourceDetailPage(this);
    // NOTE: 这一页只发"用户想做什么"，一件事都不自己干 —— 收到信号之后干什么全在下面
    //       这几个槽里。所以它能在 ui_check 里被单独构造、单独驱动（不联网、不读盘）。
    connect(m_resourceDetailPage, &ResourceDetailPage::chipChanged, this,
            &MainWindow::onResourcePageChipChanged);
    connect(m_resourceDetailPage, &ResourceDetailPage::showSnapshotsChanged, this,
            &MainWindow::onResourcePageShowSnapshotsChanged);
    connect(m_resourceDetailPage, &ResourceDetailPage::rowActivated, this,
            &MainWindow::downloadResourcePageVersion);
    // 开浏览器 / 复制名字**由这一层动手**（那一页只发信号）：开浏览器要走
    // QDesktopServices（验证工程把 url handler 换掉就能拦，见那一页的文件头），
    // 复制要能进日志。
    connect(m_resourceDetailPage, &ResourceDetailPage::openExternalRequested, this,
            [this](const QString &url) {
                if (url.isEmpty()) {
                    return;
                }
                appendLog(tr("正在打开 %1。").arg(url));
                // 和"打开实例文件夹"那条走的是同一个东西（都是"把这件事交给系统"）。
                // 开不了要说一句：那颗按钮看着像是我们自己开的页面，静静地什么都不发生，
                // 用户只会一遍遍点它。
                if (!QDesktopServices::openUrl(QUrl(url))) {
                    InlineMessage::warning(m_ui->overlay(), tr("打不开浏览器"),
                                           tr("没能打开这一条网址：\n%1").arg(url));
                }
            });
    connect(m_resourceDetailPage, &ResourceDetailPage::copyNameRequested, this,
            [this](const QString &text) {
                if (text.isEmpty()) {
                    return;
                }
                // 复制完**不弹话**：这一下是用户自己按的，而剪贴板里那串东西他自己
                // 粘一下就看得见。进日志留个痕就够（排查"我复制的怎么不对"时有用）。
                if (QClipboard *clipboard = QApplication::clipboard()) {
                    clipboard->setText(text);
                    appendLog(tr("已复制名称：%1").arg(text));
                }
            });
}

void MainWindow::applyResourcePageChips(const QStringList &declaredGameVersions,
                                        bool showSnapshots, const QString &wanted) {
    if (m_resourceDetailPage == nullptr) {
        return;
    }
    // 第一颗是「全部」（key 空串 = 不筛）。其余每一颗的 key **和文字是同一串** ——
    // 这一排上没有"显示名"这回事（游戏版本号本身就是用户在界面上要找的那个东西）。
    QList<ResourceDetail::Chip> chips;
    ResourceDetail::Chip all;
    all.text = tr("全部");
    chips.append(all);
    for (const QString &gameVersion :
         ResourceCatalog::gameVersionChoices(declaredGameVersions, showSnapshots)) {
        ResourceDetail::Chip chip;
        chip.key = gameVersion;
        chip.text = gameVersion;
        chips.append(chip);
    }
    m_resourceDetailPage->setChips(chips, wanted);
}

int MainWindow::openResourceDetailPage(const QString &iconKey, const QString &kindKey,
                                       DetailSource from, const QString &instanceId) {
    OverlayHost *overlay = m_ui->overlay();
    if (overlay == nullptr || overlay->isOpen()) {
        // 已经有覆盖层开着（另一页、或者一条提示）。这一层不做页面栈，这一次就不开了 ——
        // 和 `openPage()` 返回 nullptr 是同一件事（见 OverlayHost.hpp 那段）。
        return kResourcePageNotOpened;
    }
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    if (!ResourceCatalog::kindFromCategoryKey(kindKey, &kind)) {
        return kResourcePageNotOpened;  // 这一行不是我们建的（正常走不到）
    }
    const auto hit = m_resourceHits.constFind(iconKey);
    if (iconKey.isEmpty() || hit == m_resourceHits.constEnd()) {
        // 条目上那把钥匙在表里找不到 = 这一行是上一批搜索留下的（重搜过）。用户没做错
        // 什么，不弹窗（和点第三层时那句"清单里没它"同一个规矩）。
        return kResourcePageNotOpened;
    }
    setupResourceDetailPage();
    ResourceDetailPage *page = m_resourceDetailPage;
    if (page == nullptr) {
        return kResourcePageNotOpened;
    }

    // 这一次说的是哪个资源、列的是哪一份。**开页之前就要摆好**：下面 execPage 一进去
    // 就卡住了，那之后写的每一句都只有等用户点返回才执行得到。
    m_resourcePage.open = true;
    m_resourcePage.kind = kind;
    m_resourcePage.source = hit->source;
    m_resourcePage.projectId = hit->projectId;
    m_resourcePage.iconKey = iconKey;
    m_resourcePage.title = hit->title;
    m_resourcePage.chip.clear();
    m_resourcePage.showSnapshots = false;   // 开页一律从"不显示快照版"开始（勾选框的默认值）
    m_resourcePage.versions.clear();
    m_resourcePage.loaded = false;
    // 钥匙里游戏版本和加载器都空 = **不筛**（"这个资源一共发过哪些版本"）。
    // 这也正是它和树那条路的钥匙不会撞的原因：树上那把一定带着一个游戏版本号。
    m_resourcePage.requestKey = ResourceCatalog::versionsRequestKey(
        hit->projectId, QString(), QString(), hit->source);

    ResourceDetail::Header header;
    header.title = hit->title;
    header.description = hit->description;
    header.metaLine = ResourceCatalog::hitMetaLine(*hit);
    // 「转到 Modrinth / CurseForge」那两颗：网址是**那家自己网页的形状**（见
    // projectPageUrl，类名拼错了回 404 而不报错）。拼不出来（slug 空）那一页就不画那颗
    // 按钮 —— 摆一颗点了没反应的键比不摆糟。
    header.openLabel = ResourceCatalog::sourceLabel(hit->source);
    header.openUrl = ResourceCatalog::projectPageUrl(hit->source, kind, hit->slug);
    // MC 百科那边**没有按 id 直接跳的路**（它是另一套号），只能拿名字去搜 —— 这是
    // "跳到那儿附近"，不是"跳到那一页"。
    header.mcmodUrl = ResourceCatalog::mcmodSearchUrl(hit->title);
    header.nameToCopy = hit->title;
    page->setHeader(header);

    // 中间那一排游戏版本 chip：**搜索响应里本来就带着**"这个资源支持哪些游戏版本"
    // （`Hit::gameVersions`，顺序由 gameVersionChoices 统一成新的在前），所以开页这一下
    // 不用为它多发一条请求 —— 勾「显示快照版」那一下也一样，重算用的是存下来的这一份。
    //
    // NOTE: 这一份**声明表要留着**（不止是拿来摆一次 chip）：勾选框一拨，chip 那一排整个
    //       换一份（折 / 不折是两份不一样的表），那时候手上只有开页时存下来的它。
    m_resourcePage.declaredGameVersions = hit->gameVersions;
    // 勾选框一律从不勾开始（那一页自己的默认值，见 ResourcePageState::showSnapshots）。
    page->setShowSnapshots(false);

    // ---------- 从某个实例进来的那一次：预选到**那个实例的**版本 / 加载器（2026-09-28）----------
    //
    // 用户原话：「点开某个模组等的时候将版本自动选到当前实例对应的那个，对应的模组加载器
    // 的版本放在前面，资源包等同理，展示为对应版本适用的」。三件事一起落到这儿：
    //   ① chip 预选到那个实例的游戏版本 ⇒ 底下那一份版本列表**只列适用于那个版本的**
    //      （筛就是 `versionsForGameVersion()`，本来就是每按一颗 chip 做一次的事）；
    //   ② 分组时把那个实例的加载器的组排到前面（`groupVersions()` 的 `preferredLoader`）；
    //   ③ 预选**存进 `m_resourcePage`**：勾「显示快照版」那一下要把 chip 那一排整个重摆，
    //      那时还得知道"按着的是哪一个"，不然用户拨一下勾选框就被甩回「全部」了。
    //
    // NOTE: 那两份值是**现在**读的（`instanceGameVersion` 读盘）。开页这一下只读一次，
    //       一点问题没有；**别**把它挪进 `fillResourcePageGroups()` 那种每按一颗 chip 就
    //       跑一遍的路径（那是每按一下读一次盘）。
    // NOTE: 加载器要从**显示名**（「Fabric」/「NeoForge」）换算成**小写那一套**
    //       （`VersionGroup::loader` 的写法），换算在下面那个 helper 里 —— 直接递显示名进去
    //       会一个字都比不上，症状是"排在前面"永久不生效而屏幕上一点异常都没有。
    m_resourcePage.instanceId = instanceId;
    m_resourcePage.preferredLoader.clear();
    QString wantedChip;
    if (!instanceId.isEmpty()) {
        m_resourcePage.preferredLoader = loaderGroupKey(
            instanceLoaderName(m_versionManager->displayParts(instanceId), m_versionManager, nullptr));
        // 换算走 `gameVersionAfterToggle()`（那一份规则只有那一处，别自己比大版本）：实例报
        // 的是**原样**那个版本号（可能是 26.2-rc1），而 chip 那一排这会儿是**折过**的那一份
        // （不勾「显示快照版」）—— 换算不出来（这个资源压根没声明过那个版本）回空串，
        // 于是停在「全部」那颗上，和用户自己点了一颗表里没有的 chip 是同一个结果。
        wantedChip = ResourceCatalog::gameVersionAfterToggle(
            hit->gameVersions, m_resourcePage.showSnapshots,
            m_versionManager->instanceGameVersion(instanceId));
    }
    applyResourcePageChips(hit->gameVersions, m_resourcePage.showSnapshots, wantedChip);
    // ★ 按着的是哪一颗**以页面为准**（和 onResourcePageShowSnapshotsChanged 里那一句逐字
    //   同一个理由）：上一步要是把"表里没有的那个 key"纠正成了「全部」，两位得说的是同一
    //   件事。**这一句不能省** —— `m_resourcePage.chip` 就是 `fillResourcePageGroups()`
    //   拿来筛的那一份，光把 chip 按下去而这一位还空着，症状是"那一颗亮着、底下却列着
    //   全部版本"（看着像筛没生效）。
    m_resourcePage.chip = m_resourceDetailPage->currentChip();

    // ★ 右上角那一格**每次开页都显式摆一次**（2026-09-27）：这一页是**同一个对象、建一次
    //   留着的**，两个入口（下载页 / 实例的文件管理页）共用它 —— 不摆的话就留着上一次那
    //   一格（先下载页点个模组、再从实例点个模组进来，看见的还是「显示快照版」）。
    //
    //   判据是**两样都要**：来路是实例的文件管理页，**而且**这一条是个模组。少一样都不摆
    //   —— 从实例点一个**资源包**进来照旧摆「显示快照版」（那一颗在那一页上是有用的：那一
    //   排 chip 折的正是"这个资源支持哪些游戏版本"）。
    //   ★ 上面那一发 `applyResourcePageChips(…, wantedChip)` **就是**这一段原来那一发
    //     （原来写死 `QString()` = 一律从「全部」开始，2026-09-28 起改成预选到实例的版本）。
    //     别再在这儿补一次 `QString()` 的那一发 —— 那会把上面刚按下去的那一颗整个抹回
    //     「全部」，而 `m_resourcePage.chip` 还留着实例那个值：屏幕上一颗亮的都没有、底下
    //     列的却是按实例版本筛过的（正是 fillResourcePageGroups 那段点名的"两位各说各的"）。
    page->setModSettingsSlot(from == DetailSource::InstanceFiles && kind == ResourceCatalog::Kind::Mod);
    // 图标是异步来的：先清掉上一次那一张（不然会留着上一个资源的图标，比空着糟），
    // 那一格的位置照旧留着，图回来时整张卡不会往左跳一下。
    page->setIcon(QPixmap());
    page->setPlaceholder(tr("正在读版本列表…"));
    // 中间那一行也**每次开页清一次**（同上：这一页是建一次留着的，不清就留着上一个
    // 资源的"要装的前置"，而那一行看着像是这个资源的）。
    m_resourcePage.dependencies.clear();
    m_resourcePage.dependencyRequestKey.clear();
    page->setDependencies(QString());

    // 图标：列表那几行早就为这一条要过了（见 fillResourceHitRows 最后那句），内存/盘上
    // 命中就是**同步**回来的 —— 于是这一句常常在下面 execPage 之前就把图摆好了。
    m_resourceStore->requestIcon(*hit);
    // 版本列表（整份，不筛）。答复落在 onResourceVersionsFinished 里，判据是
    // `m_resourcePage.requestKey` —— 和树那条路按行重算钥匙的做法不同：那一页没有
    // 条目可查（它自己那份清单是空的），所以这儿直接比钥匙。
    m_resourceStore->fetchVersions(m_resourcePage.requestKey, hit->projectId, hit->source,
                                   QString());

    // **最后**才开页：这一句会一直卡在这儿（内部那个嵌套事件循环）转到用户点返回。
    // NOTE: 上面发出去的两条请求都是异步的，答复会在那个循环里落地 —— 所以"先发请求、
    //       再开页"和"先开页、再发请求"对用户是同一件事；写成这样只是因为这一句之后
    //       的控制流要等用户。
    const int result = overlay->execPage(page, page->titleText());
    // 点了返回（或者 Esc）：状态落下去。**页面本身留着**（它是懒建的那一份），下次点开
    // 还是它，setHeader/setChips 会把上一次那些字全换掉。
    m_resourcePage.open = false;
    return result;
}

void MainWindow::onResourcePageChipChanged(const QString &gameVersion) {
    if (!m_resourcePage.open) {
        return;
    }
    m_resourcePage.chip = gameVersion;
    fillResourcePageGroups();
}

void MainWindow::onResourcePageShowSnapshotsChanged(bool showSnapshots) {
    if (!m_resourcePage.open || m_resourceDetailPage == nullptr) {
        return;
    }
    if (m_resourcePage.showSnapshots == showSnapshots) {
        // 值没变（比如开页那一下把勾选框摆回去）。**不往下走**：下面那一段会把 chip 那一排
        // 整个重建，而重建会清掉用户正按着的那一颗的按压状态（虽然换算之后还是它）——
        // 白白闪一下不值当。
        return;
    }
    m_resourcePage.showSnapshots = showSnapshots;
    // chip 那一排整个换一份（折与不折是两份不一样的表），现在按着的那一颗**换算成同一个
    // 大版本在另一档里的说法**（26.2-rc1 ↔ 26.2）—— 勾这一下不该把用户从他正在看的
    // 那条线上甩回「全部」。换算的规则只有一处，见 ResourceCatalog::gameVersionAfterToggle。
    const QString wanted = ResourceCatalog::gameVersionAfterToggle(
        m_resourcePage.declaredGameVersions, showSnapshots, m_resourcePage.chip);
    applyResourcePageChips(m_resourcePage.declaredGameVersions, showSnapshots, wanted);
    // 按着的是哪一颗**以页面为准**：换算出来的那一颗要是表里没有（对不上），`setChips`
    // 会把它纠正成「全部」，这儿跟着抄回来 —— 两个状态各说各的才是真出问题的地方。
    m_resourcePage.chip = m_resourceDetailPage->currentChip();
    // setChips **不发** chipChanged（它不是"用户点了"那一发）—— 所以这一句得自己来，
    // 它就是用户要的「更改以后自动刷新版本列表」。
    fillResourcePageGroups();
}

void MainWindow::fillResourcePageGroups() {
    if (!m_resourcePage.open || m_resourceDetailPage == nullptr) {
        return;
    }
    // 还没回来：chip 是在这一页上点的，而版本列表可能还在路上（用户手快）。
    // **不能**在这儿说"这一档没有版本" —— 那是在替一份还没到的数据回答。
    if (!m_resourcePage.loaded) {
        m_resourceDetailPage->setPlaceholder(tr("正在读版本列表…"));
        return;
    }
    // ★ 这一页当下摆在**最上面那一版**（= 用户一眼看到、最可能点的那一条）。下面那一段
    //   版本列表中间那一行说的是"这一版要装的前置"（用户 2026-09-28 要的），而"这一版"
    //   指的就是它。取的是**分组之后**的最前一条（`groupVersions` 把新游戏版本那一组摆在
    //   最前、组内也是新的在前），不是 filtered 的第一条 —— 两条在"筛了某一档"时能不一样。
    //   NOTE: 存的是**一份拷贝**不是指针：`groupVersions()` 那一串是这一句里的临时表，
    //         指着它出了这个 for 就是悬空的（而这一页的状态要活到下一次刷）。
    ResourceCatalog::VersionEntry topmost;
    bool hasTopmost = false;

    // 筛（看的是这一版**声明过**的每一个游戏版本，不是它落在哪个组里 —— 那条规矩和它的
    // 坑都在 versionsForGameVersion 的注释里），再按「游戏版本 + 加载器」分组。
    // 两步**分开**：合并了就是上面那条坑（筛 1.20.1 会漏掉一半支持它的版本）。
    const QList<ResourceCatalog::VersionEntry> filtered = ResourceCatalog::versionsForGameVersion(
        m_resourcePage.versions, m_resourcePage.chip, m_resourcePage.showSnapshots);
    QList<ResourceDetail::Group> groups;
    // 第三个参数 = 从实例进来时那个实例的加载器（**小写**，见 loaderGroupKey）：同一个游戏
    // 版本里把它的组排到前面（用户原话「对应的模组加载器的版本放在前面」）。不是从实例
    // 进来的那一次是空串 = 照旧按固定的 fabric/forge/neoforge/quilt 次序。
    for (const ResourceCatalog::VersionGroup &group :
         ResourceCatalog::groupVersions(filtered, m_resourcePage.showSnapshots,
                                        m_resourcePage.preferredLoader)) {
        ResourceDetail::Group out;
        // 组头写的是**这一条落在哪个游戏版本下**（分组规则见 groupVersions）。和上面选的
        // 那一档不一致是正常的（一条支持两档的版本落在新的那一档下），**不加**「（所选
        // 版本）」那种后缀：每一组都挂一句一样的话，那一句就不是信息了。
        out.title = group.title();
        for (const ResourceCatalog::VersionEntry &entry : group.versions) {
            if (!hasTopmost) {
                topmost = entry;
                hasTopmost = true;
            }
            ResourceDetail::Row row;
            row.key = entry.id;
            // 正文写**版本号**（不是文件名）：这一页一屏几十条版本，认人的是版本号；
            // 文件名在小字那一截里（versionRowSubText 的第二截）。树那条路正相反
            // （那一层是"挑一个文件下来"，写的就是落盘那个名字）—— 两处不一样是有意的。
            row.title = entry.versionNumber;
            row.subText = ResourceCatalog::versionRowSubText(entry);
            row.channel = ResourceCatalog::releaseChannelRank(entry.versionType);
            const ResourceCatalog::FileEntry *file = entry.primaryFile();
            if (file != nullptr && !file->filename.isEmpty()) {
                // 鼠标停上去看**要下哪个文件**（正文放不下、也不该放）。
                row.tooltip = file->filename;
            }
            out.rows.append(row);
        }
        groups.append(out);
    }
    // 中间那一行（"这一版要装的前置"）跟着**这一次摆上去的那一份**走 —— 列表空的（下面
    // 两种空）就整个收起来：那时候屏幕上一条版本都没有，"这一版"指的是哪一版就说不清了。
    m_resourcePage.dependencies =
        hasTopmost ? ModDependencies::requiredDependenciesOf(topmost)
                   : QList<ResourceCatalog::Dependency>();
    updateResourcePageDependencies();

    if (groups.isEmpty()) {
        // 两种"空"**不是同一句话**：选了某一档之后一条都没有（这一档上没有），和这个资源
        // 一条版本都没发过（那一页自己那句，`setGroups()` 收到空表时摆的就是它）。
        if (m_resourcePage.chip.isEmpty()) {
            m_resourceDetailPage->setGroups({});
        } else {
            m_resourceDetailPage->setPlaceholder(tr("这个游戏版本上没有可下载的版本。"));
        }
        return;
    }
    m_resourceDetailPage->setGroups(groups);
}

void MainWindow::updateResourcePageDependencies() {
    if (!m_resourcePage.open || m_resourceDetailPage == nullptr) {
        return;
    }
    const QList<ResourceCatalog::Dependency> &dependencies = m_resourcePage.dependencies;
    if (dependencies.isEmpty()) {
        m_resourceDetailPage->setDependencies(QString());   // 没有前置 = 这一行不画
        m_resourcePage.dependencyRequestKey.clear();
        return;
    }

    // 名字：先查会话缓存，缺的那几个凑成一批去问（**只问缺的**——已经知道的不该再问一遍，
    // 而这一行每换一次 chip 就要重算一次）。
    QStringList names;
    names.reserve(dependencies.size());
    QStringList unknownIds;
    for (const ResourceCatalog::Dependency &dependency : dependencies) {
        const QString name =
            m_projectTitles.value(projectTitleKey(m_resourcePage.source, dependency.projectId));
        if (name.isEmpty()) {
            if (!unknownIds.contains(dependency.projectId)) {
                unknownIds.append(dependency.projectId);
            }
        }
        names.append(name);   // 空名字也占一格（下面按"有没有空串"判整个名单全不全）
    }

    if (unknownIds.isEmpty()) {
        m_resourceDetailPage->setDependencies(ModDependencies::detailPrerequisiteText(names));
        m_resourcePage.dependencyRequestKey.clear();
        return;
    }

    // 名字还没到：**先摆"有几条"那句**，别空着 —— 空着的症状是"这一版到底要不要前置"
    // 看不出来，而那正是用户要这一行的理由。名字回来之后再换成那一串。
    m_resourceDetailPage->setDependencies(
        ModDependencies::detailPrerequisiteCountText(dependencies.size()));
    m_resourcePage.dependencyRequestKey = ModDependencies::projectsKeyFor(
        QStringLiteral("detail"), unknownIds, m_resourcePage.source);
    m_resourceStore->fetchProjects(m_resourcePage.dependencyRequestKey, unknownIds,
                                   m_resourcePage.source);
}

void MainWindow::onResourceProjectsFinished(const QString &requestKey,
                                            const QList<ResourceCatalog::Hit> &hits,
                                            const QString &problem) {
    // 不是这一页等的那一发就丢。**别在这里顺手替"前置那一趟"收名字**：两边的来源可能
    // 不一样（详情页那个资源是 Modrinth 的、要装进去的那个实例里那个模组是 CurseForge
    // 的），而来源是那把键的一半 —— 拿错来源写进缓存的名字会挂在**错误的那把键**上，
    // 于是另一个人永远查不到它（症状和"名字没取到"一模一样）。各自收各自的。
    if (!m_resourcePage.open || requestKey.isEmpty()
        || requestKey != m_resourcePage.dependencyRequestKey) {
        return;
    }
    rememberProjectTitles(hits, m_resourcePage.source);
    // 没取到**不是错误**（那一页上还有"有几条"那句），所以不弹窗、只记一句日志 ——
    // 和前置那一趟同一条规矩（见 onDependencyProjectsFinished）。
    if (!problem.isEmpty()) {
        appendLog(tr("前置模组的名字没取到：%1").arg(problem));
    }
    // 名字已经进缓存了：再走一次那一段就会走到"全都有了"那条路上去（**不会**再发一发 ——
    // 缺的那几个现在是空的，见上面 unknownIds 那段）。
    updateResourcePageDependencies();
}

QString MainWindow::projectTitleKey(ResourceCatalog::Source source, const QString &projectId) {
    // `Hit::iconCacheId()` 是那个拼法（`<来源>-<项目号>`）的**唯一出处** —— 别在这儿再拼
    // 一遍字符串：拼法一改，图标缓存、实例里那份身份表、这个缓存三处就各认各的了。
    return ResourceCatalog::Hit{.projectId = projectId, .source = source}.iconCacheId();
}

void MainWindow::rememberProjectTitles(const QList<ResourceCatalog::Hit> &hits,
                                       ResourceCatalog::Source source) {
    for (const ResourceCatalog::Hit &hit : hits) {
        if (hit.projectId.isEmpty() || hit.title.isEmpty()) {
            continue;
        }
        m_projectTitles.insert(projectTitleKey(source, hit.projectId), hit.title);
    }
}

void MainWindow::downloadResourcePageVersion(const QString &versionId) {
    if (!m_resourcePage.open || versionId.isEmpty()) {
        return;
    }
    for (const ResourceCatalog::VersionEntry &entry : m_resourcePage.versions) {
        if (entry.id == versionId) {
            // 和树上点第三层那一下**是同一件事**（选哪个实例、落到哪个目录、加载器那道闸、
            // 整合包下完接导入，全在那边那一个函数里）。
            installResourceVersion(m_resourcePage.kind, m_resourcePage.title, entry,
                                   m_resourcePage.source);
            return;
        }
    }
    // 行在、这份清单里没有它：正常走不到（那些行就是从这份清单摆出来的）。
}

void MainWindow::onResourceDownloadFinished(const QString &label, bool success,
                                            const QString &errorMessage) {
    // 整合包那一页下回来的 .mrpack：下完要接着走导入那条路。这一单**只活一次**，所以先
    // 取走再判成功与否 —— 不清的话，用户手动导入过一个包之后，下一个下完的模组也会顺手
    // 再触发一次导入。
    //
    // NOTE: 按 `label` **只取自己那一单**（不是整根成员一刀切）。以前不分单，于是任何一次
    //       下载完成都会把这笔记录读走：整合包还在下、用户又点了个模组，模组先下完就把
    //       记录吃了 —— 拿半截 .mrpack 去导入，而且整合包真下完时再没人接它。
    //       用 take() 而不是 value()+remove()：这一单**只活一次**，取走本身就是"用掉了"。
    const QString pendingModpack = m_pendingModpackImports.take(label);

    if (!success) {
        appendLog(tr("✗ %1 没有下下来：%2").arg(label, errorMessage));
        InlineMessage::warning(m_ui->overlay(), tr("下载失败"),
                             tr("%1 有文件没能下下来。\n\n%2").arg(label, errorMessage));
        return;
    }

    appendLog(tr("✓ %1 下好了：%2").arg(label, errorMessage));
    if (!pendingModpack.isEmpty()) {
        // 不弹"下载完成"：整合包下回来只是第一步，紧接着那个导入框就是用户的下一步，
        // 中间夹一个"下好了"等于让他白点一次。
        //
        // NOTE: **排到下一轮**再走导入，不能在这里直接调。这个函数是在
        //       DownloadManager 的 allDownloadsFinished 信号栈里被调到的，而导入那条路
        //       马上就会 beginBatch() 开新的一批 —— 在别人的信号栈里开批次正是 2026-09-22
        //       那次崩溃的形状（取证见 build/tools/crashdump/，VersionManager 里那几处
        //       deferXxxStep 是同一个理由）。
        QTimer::singleShot(0, this, [this, pendingModpack] { importModpackFromFile(pendingModpack); });
        return;
    }
    InlineMessage::information(m_ui->overlay(), tr("下载完成"),
                             tr("%1 已经装进实例里了。\n\n%2").arg(label, errorMessage));
}

void MainWindow::updateInstallName() {
    QLineEdit *edit = m_ui->getInstallNameEdit();
    if (!m_installNameTouched) {
        // 用户还没自己改过名字：默认名跟着"勾中的加载器 + 它选的那个版本"走。
        // 挡信号是必须的 —— 不挡的话这一句会触发 textChanged，于是"程序填的名字"
        // 被当成"用户改过"，从此默认名再也不跟着加载器变了。
        const QSignalBlocker blocker(edit);
        edit->setText(defaultInstanceName());
    }
    updateInstallNameHint();
}

void MainWindow::updateInstallNameHint() {
    QLineEdit *edit = m_ui->getInstallNameEdit();
    const QString name = edit->text().trimmed();

    // 名字合不合法：判据在数据层（InstanceFiles::validateDirName()，和"重命名实例"、
    // "整合包导入"用的是同一套规矩）。这里只用它的结论，不抄一份规则过来。
    const QString invalid = InstanceFiles::validateDirName(name);
    const bool canInstall = invalid.isEmpty();

    // 上面那行**灰字**：说"装下去会怎样"。名字能不能用**都照说** —— 2026-09-23 起名字
    // 不合法的原因不再顶掉它，而是另起一行红字（用户原话："如果版本名字存在非法字符，
    // 不要替换掉那个「装好后在实例...」，而是在下面添加一行红色字"）。
    // 名字空着时这行没话可说（没有名字就没有目录），就让它空着。
    QString hint;
    if (!name.isEmpty()) {
        // 重名：**只提示、不拦**（用户原话："如果文件夹里有重名则在下方小字提示"）。
        // 同名就是同一个实例 —— 装下去不是新建一个，而是校验并补齐那个目录里的文件
        // （「重下 = 校验跳过」）。这句话必须说清这一点，否则用户会以为会多出一个。
        // NOTE: 名字不合法时**不查**重名 —— 那个名字当不了目录名，AppPaths::versionDir()
        //       拼出来的路径没有意义（`..` 那种会拼到数据目录外面去），查了也只是白查。
        const QString dir = canInstall ? AppPaths::versionDir(name) : QString();
        if (!dir.isEmpty() && QDir(dir).exists()) {
            // 「已经装过了」和「这个目录是别的东西」在用户眼里是两回事，分开说。
            const bool installed = QFile::exists(AppPaths::versionJsonPath(name));
            hint = installed ? tr("已经有叫「%1」的实例了：同一个名字就是同一个实例，再装一次"
                                  "会校验并补齐它的文件（已经下好的跳过）。")
                                   .arg(name)
                             : tr("%1 已经存在了，但它不像一个装好的实例。装下去会往这个目录"
                                  "里写文件。")
                                   .arg(dir);
        } else {
            hint = tr("装好之后在「实例」页里能看到它，目录是 versions/%1。").arg(name);
        }
    }
    QLabel *hintLabel = m_ui->getInstallNameHint();
    hintLabel->setText(hint);
    // 没话可说的时候连那一**行**一起收掉，别只把字清空。理由：这是 wordWrap 的标签，
    // 空着照样占一整行（实测 693x14），于是名字清空那一刻底下那行红字上面会凭空多出
    // 一个空行 —— 用户 2026-09-23 报的正是这个（"把名字弄空了以后上面那行消失了但是
    // 下面那一行空提示会换行怪怪的"）。空着的时候这一行没内容，收掉不影响别的。
    hintLabel->setVisible(!hint.isEmpty());

    // 下面那行**红字**：名字不能用的原因（就是 validateDirName() 那句话的字面意思）。
    // 能用的时候空着 + 藏起来 —— 藏起来（而不是留一个空行）是为了正常情况下名称框底下
    // 那几行的位置和以前一模一样（这一页上加载器树和安装按钮的位置都跟着它走）。
    QLabel *error = m_ui->getInstallNameError();
    error->setText(invalid);
    error->setVisible(!canInstall);
    // 「安装」按钮：名字不合法就点不动（省掉一次"点了才弹一句名字不对"的往返）。
    // 忙的时候也不给点（那是 setDownloadUiBusy 的活，这里只是别把它顶回来）。
    m_ui->getInstallButton()->setEnabled(canInstall && !m_downloadInFlight);
}

void MainWindow::onInstallNameEdited() {
    // 用户动过这个名字了：此后默认名不再跟着加载器变（不然他敲到一半的名字会被
    // 一次"勾了 Forge"顶掉）。
    m_installNameTouched = true;
    updateInstallNameHint();
}

void MainWindow::onInstallButtonClicked() {
    if (m_downloadInFlight) {
        appendLog(tr("已有下载任务正在进行，请等它结束。"));
        return;
    }
    const QString gameVersion = m_installGameVersion;
    if (gameVersion.isEmpty()) {
        // 正常走不到（安装页只能从树里一个具体版本进来）。真走到了就是界面状态错了，
        // 宁可不做 —— 猜一个版本装下去会装到用户没点过的东西上。
        InlineMessage::warning(m_ui->overlay(), tr("没有选中版本"),
                             tr("不知道要基于哪个原版安装，请回到版本列表重新挑一个。"));
        return;
    }

    const QString name = m_ui->getInstallNameEdit()->text().trimmed();
    const QString invalid = InstanceFiles::validateDirName(name);
    if (!invalid.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("名字不能用"), invalid);
        return;
    }

    const int loaderIndex = selectedLoaderIndex();
    VersionManager::LoaderKind kind = VersionManager::LoaderKind::Fabric;
    const bool withLoader = VersionManager::loaderKindFromIndex(loaderIndex, &kind);

    // 已经下过就不给"直接重下"（几 GB 白跑一趟），改成校验并补齐缺失/损坏的文件 ——
    // 这是本启动器一贯的规矩。名字和原版号一样时问的是"这个版本"，不一样时问的是
    // "这个实例"，两句话都得像人说的。
    if (QFile::exists(AppPaths::versionJsonPath(name))) {
        const bool sameAsVanilla = name == gameVersion;
        const auto answer = InlineMessage::question(m_ui->overlay(), sameAsVanilla ? tr("版本已存在") : tr("实例已存在"),
            sameAsVanilla
                ? tr("版本 %1 已经下载过了。\n是否校验并修复缺失或损坏的文件？").arg(name)
                : tr("实例「%1」已经装过了。\n是否校验并修复缺失或损坏的文件？").arg(name), tr("是"), tr("否"), true);
        if (!answer) {
            appendLog(tr("已取消对 %1 的校验。").arg(name));
            return;
        }
        appendLog(tr("开始校验 %1 的文件（已存在的会跳过）...").arg(name));
    }

    QString installerJava = m_javaSettings->javaPath();
    if (withLoader && !pickInstallerJava(gameVersion, loaderIndex, &installerJava)) {
        return;  // 里面已经弹过窗说清原因了，界面还没进忙态，直接退出就是干净的状态
    }

    // 忙要在点击这一刻就置上：版本 json / 加载器列表都是异步拉回来的，中间这段时间
    // 按钮还能点的话，用户会重复触发（和别处同一个理由）。
    //
    // NOTE: **顺序不能反**（m_installingInstanceId 必须在 setDownloadUiBusy(true) 之前）。
    //       setDownloadUiBusy() 内部会调 refreshInstalledVersions()，而那份"能启动的实例"
    //       列表是按 m_installingInstanceId 过滤出来的（installingVersionIds()）。反过来的
    //       话，置忙那一刻读到的还是**上一次**的值：对一个已经装好的实例点「安装」（校验并
    //       修复）时，它整段安装期间都留在可启动列表里 —— 用户点得动它，点下去却被
    //       onStartButtonClicked() 那道按版本的闸口弹一句"文件还没有下载完成"。
    //       那正是 MainWindow.hpp 写明不许出现的"列表里能选、点下去被拦"。
    //       （applyModpackImport() 那边本来就是"先记 id 再置忙"，这里对齐它。）
    m_installingInstanceId = name;
    setDownloadUiBusy(true);
    m_progressThrottle.invalidate();
    if (withLoader) {
        m_versionManager->installNamedLoader(name, gameVersion, kind, pinnedLoaderVersion(loaderIndex),
                                            installerJava);
    } else {
        m_versionManager->installNamedVanilla(name, gameVersion);
    }
}

bool MainWindow::pickInstallerJava(const QString &gameVersion, int loaderIndex, QString *out) {
    // 安装器自己就是个 Java 程序（Forge/NeoForge 是 `java -jar <安装器>.jar --installClient
    // <目录>`，OptiFine 那个是无界面入口 + 换 APPDATA，见 VersionManager::runInstaller），
    // 所以要的运行环境和"这个版本要的 Java"是同一件事：版本 json 里的 javaVersion。
    // Fabric 那条路只拉 json + 下 maven 库，一个进程都不起，所以这一段只管要跑安装器的那些。
    //
    // 拿不到版本 json（原版还没装）就不知道要求，此时原样用配的那份 —— 不在这一步拦人：装
    // 加载器本来就会把原版补齐，而"要哪个 Java"从那份 json 里读最准，猜一个主版本比不猜更
    // 危险（见启动那条路上"拿不到 version.json 就不能猜"那段注释）。
    // 参数收下标、在这里翻一次枚举（和界面别处同一处翻译，见 VersionManager 里那段注释）。
    // 翻不出来 = 没勾加载器 / 勾的是「不装」：一个外部安装器都不跑，直接放行。
    VersionManager::LoaderKind kind = VersionManager::LoaderKind::Fabric;
    if (!VersionManager::loaderKindFromIndex(loaderIndex, &kind)) {
        return true;
    }
    if (kind == VersionManager::LoaderKind::Fabric) {
        return true;
    }
    const QString configuredJava = m_javaSettings->javaPath();
    QString installerJava = configuredJava;
    int requiredJavaMajor = 0;
    const QJsonObject gameJson = m_versionManager->getVersionJson(gameVersion);
    if (!gameJson.isEmpty()) {
        requiredJavaMajor = gameJson["javaVersion"].toObject()["majorVersion"].toInt();
    }
    QString javaReason;
    if (!m_javaSettings->installerJavaFor(requiredJavaMajor, configuredJava, &installerJava,
                                          &javaReason)) {
        appendLog(tr("这个版本要的 Java 拿不到：%1").arg(javaReason));
        InlineMessage::warning(m_ui->overlay(), tr("Java 版本不够"), javaReason);
        return false;
    }
    if (installerJava != configuredJava) {
        appendLog(tr("这个版本要 Java %1，装的时候改用 %2").arg(requiredJavaMajor).arg(installerJava));
    }
    *out = installerJava;
    return true;
}

// ======================================================================
// 「设置」选项卡
// ======================================================================
//
// 分工：MainWindowUI 只把控件搭出来（含"选项名称在左、控件在右"的两列布局），值和
// 持久化全在这一层。枚举 → 文案的映射也在这里 —— 界面层不该为了画一个下拉框去
// include src/settings，而 tr() 的文案本来就该和业务逻辑待在一起。
//
// 落盘时机逐项不同，这是这块最容易做错的地方，所以每一项都在自己的函数里说清楚：
//   Java / 内存 / 启动后 / 下载源 / 版本名格式 —— 改完立刻存
//   JVM 参数 —— 点「应用」才存（那是直接进命令行的东西，不能存半条）

void MainWindow::applySettingsToUi() {
    // 灌值时把信号全挡上：下面这些控件现在反映的是**盘上的值**，不是用户改的。
    // 不挡的话每条 currentIndexChanged/valueChanged 都会触发一遍"保存"，白写几次盘，
    // 更要紧的是那几条槽里还夹着真动作（改「版本名格式」会重刷实例列表）。
    const QSignalBlocker blockJava(m_ui->getJavaVersionCombo());
    const QSignalBlocker blockBehaviour(m_ui->getLauncherBehaviourCombo());
    const QSignalBlocker blockSource(m_ui->getDownloadSourceCombo());
    const QSignalBlocker blockShowLog(m_ui->getShowGameLogCheck());
    const QSignalBlocker blockWarnDuplicate(m_ui->getWarnDuplicateLaunchCheck());
    const QSignalBlocker blockWarnWorldLoss(m_ui->getWarnWorldLossCheck());
    const QSignalBlocker blockDownloadInto(
        m_ui->getDownloadAutoIntoInstanceCheck());
    const QSignalBlocker blockPriority(m_ui->getProcessPriorityCombo());
    const QSignalBlocker blockWindowWidth(m_ui->getGameWindowWidthSpin());
    const QSignalBlocker blockWindowHeight(m_ui->getGameWindowHeightSpin());
    const QSignalBlocker blockTitleText(m_ui->getTitleTextEdit());
    const QSignalBlocker blockCustom(m_ui->getVersionNameCustomCheck());
    const QSignalBlocker blockReal(m_ui->getVersionNameRealCheck());
    const QSignalBlocker blockLoader(m_ui->getVersionNameLoaderCheck());
    const QSignalBlocker blockLoaderVersion(m_ui->getVersionNameLoaderVersionCheck());

    // ---------- Java 版本 ----------
    // 本地扫到的那些运行时**不在这里加**：扫描要逐个跑 `java -version`，
    // 放到第一次切到设置页时再做（refreshJavaList()）。这里只放「自动」那条
    // （itemData 是空串，只有它是空的，见 selectConfiguredJava()）。
    m_ui->getJavaVersionCombo()->addItem(tr("自动（按版本挑选）"), QString());
    if (!m_javaSettings->autoSelectJava()) {
        // 手动档：把一个条目摆出来，好让下面这句选中它。路径来自盘上，
        // 可能指向一个早就删掉的 Java —— 那也是这个设定当前的真实状态，照实显示。
        addJavaRuntimeItem(m_ui->getJavaVersionCombo(), 0, m_javaSettings->javaPath());
    }
    selectConfiguredJava(m_ui->getJavaVersionCombo(), m_javaSettings->autoSelectJava(),
                         m_javaSettings->javaPath());
    updateJavaPathLabel();
    // 提醒总闸。挡信号：这是"把值画出来"，不挡的话 setChecked 会触发
    // onJavaWarnMismatchChanged() 又回头落一次盘。
    {
        const QSignalBlocker blockWarn(m_ui->getJavaWarnMismatchCheck());
        m_ui->getJavaWarnMismatchCheck()->setChecked(m_javaSettings->warnVersionMismatch());
    }

    // ---------- 内存分配 ----------
    // 交给 refreshMemoryUi()：自动档下它要按当前系统内存算一遍值、禁用两个框、写说明
    // 文字，还要落盘。两个 spin 的信号在那边自己挡（这里的局部 blocker 就不再重复了）。
    refreshMemoryUi();

    // ---------- 启动后 ----------
    // 枚举值存在 itemData 里，文案是这里的 tr()。两边的对应关系只在这四行里。
    QComboBox *behaviourCombo = m_ui->getLauncherBehaviourCombo();
    behaviourCombo->addItem(tr("不变"), int(LauncherBehaviour::Unchanged));
    behaviourCombo->addItem(tr("最小化启动器"), int(LauncherBehaviour::Minimize));
    behaviourCombo->addItem(tr("收起启动器"), int(LauncherBehaviour::Close));
    behaviourCombo->addItem(tr("收起启动器，游戏退出后自动回来"),
                            int(LauncherBehaviour::CloseAndRestore));
    selectByEnumData(behaviourCombo, m_settings->launcherBehaviour());

    // ---------- JVM 参数 ----------
    m_ui->getExtraJvmArgsEdit()->setPlainText(m_javaSettings->extraJvmArgs());

    // ---------- 外观 ----------
    // 条上那行字有**两个**去处：设置页那一格、还有条自己。同一次调用里一起灌 ——
    // 分成两处写的话，以后改一处就会出现"框里是用户改过的名字、条上还是默认那五个字母"。
    // 两边喂的都是盘上那份（空串 = 没改过，条自己会退回 defaultBrandText()）。
    m_ui->getTitleTextEdit()->setText(m_settings->titleText());
    m_ui->titleBar()->setBrandText(m_settings->titleText());

    // ---------- 启动选项 ----------
    m_ui->getShowGameLogCheck()->setChecked(m_settings->showGameLog());
    // 两条启动提醒（上面那两个 blocker 已经挡上了）。这里**不能**顺手把值抄成
    // "界面 = 盘上"以外的任何默认：默认开这件事只有 LauncherSettings 一处出处。
    m_ui->getWarnDuplicateLaunchCheck()->setChecked(m_settings->warnDuplicateLaunch());
    m_ui->getWarnWorldLossCheck()->setChecked(m_settings->warnWorldLoss());
    // 优先级：枚举值存在 itemData 里，文案是这里的 tr()。五档一一对应 ProcessPriority，
    // 顺序也按枚举来（Low → High）—— 界面从上往下就是"越来越抢 CPU"。
    QComboBox *priorityCombo = m_ui->getProcessPriorityCombo();
    priorityCombo->addItem(tr("低（空闲时才算）"), int(ProcessPriority::Low));
    priorityCombo->addItem(tr("低于正常"), int(ProcessPriority::BelowNormal));
    priorityCombo->addItem(tr("正常（默认）"), int(ProcessPriority::Normal));
    priorityCombo->addItem(tr("高于正常"), int(ProcessPriority::AboveNormal));
    priorityCombo->addItem(tr("高"), int(ProcessPriority::High));
    selectByEnumData(priorityCombo, m_settings->processPriority());
    // 窗口大小：两个框各自 setValue（信号已经被上面那两个 blocker 挡上了）。
    m_ui->getGameWindowWidthSpin()->setValue(m_settings->gameWindowWidth());
    m_ui->getGameWindowHeightSpin()->setValue(m_settings->gameWindowHeight());

    // ---------- 下载源 ----------
    QComboBox *sourceCombo = m_ui->getDownloadSourceCombo();
    sourceCombo->addItem(tr("官方源（Mojang）"), int(DownloadSource::Official));
    sourceCombo->addItem(tr("镜像源（BMCLAPI）"), int(DownloadSource::Mirror));
    selectByEnumData(sourceCombo, m_settings->downloadSource());

    // CurseForge 的 API key（同一个「下载源」组里）。信号已经被上面那几个 blocker 挡上了。
    m_ui->getCurseForgeApiKeyEdit()->setText(m_settings->curseForgeApiKey());

    // 「下载新模组时直接放进当前实例」（2026-09-28）。默认值同样只有 LauncherSettings
    // 那一处出处（默认勾上）—— 这里不写任何默认，灌的就是盘上那份。
    m_ui->getDownloadAutoIntoInstanceCheck()->setChecked(m_settings->downloadAutoIntoInstance());

    // ---------- 版本名格式 ----------
    pushVersionNameOptionsToUi(m_settings->versionNameOptions());

    // 下载源得立刻交给发请求的那一层：构造函数里紧接着的
    // fetchAvailableVersions() 就会走它。
    syncDownloadSource();
    // CurseForge 那两项同理（虽然要等到导入整合包才用得上）。
    syncCurseForgeSettings();
}

void MainWindow::pushVersionNameOptionsToUi(const VersionNameOptions &options) {
    // 挡信号：这是"把结果画出来"，不是用户操作。不挡的话，下面第一个 setChecked
    // 会触发 onVersionNameOptionChanged()，它又回头去归一化、又回头调这个函数。
    const QSignalBlocker blockCustom(m_ui->getVersionNameCustomCheck());
    const QSignalBlocker blockReal(m_ui->getVersionNameRealCheck());
    const QSignalBlocker blockLoader(m_ui->getVersionNameLoaderCheck());
    const QSignalBlocker blockLoaderVersion(m_ui->getVersionNameLoaderVersionCheck());
    m_ui->getVersionNameCustomCheck()->setChecked(options.showCustomName);
    m_ui->getVersionNameRealCheck()->setChecked(options.showRealVersion);
    m_ui->getVersionNameLoaderCheck()->setChecked(options.showLoaderName);
    m_ui->getVersionNameLoaderVersionCheck()->setChecked(options.showLoaderVersion);
}

void MainWindow::updateJavaPathLabel() {
    QLabel *label = m_ui->getJavaPathLabel();
    if (!m_javaSettings->autoSelectJava()) {
        const QString path = m_javaSettings->javaPath();
        label->setText(path.isEmpty()
                           ? tr("手动指定：还没有路径，启动会直接失败。")
                           : tr("手动指定：%1").arg(path));
        return;
    }
    const QString cached = m_javaSettings->javaPath();
    label->setText(cached.isEmpty()
                       ? tr("自动：还没挑过，启动时按版本要求找。")
                       : tr("自动：上次用的是 %1").arg(cached));
}

void MainWindow::refreshJavaList() {
    if (m_javaListScanned) {
        return;
    }
    m_javaListScanned = true;

    QComboBox *combo = m_ui->getJavaVersionCombo();
    const QSignalBlocker blocker(combo);

    const QMap<int, QString> local = m_javaSettings->scanLocalJavaVersions();
    for (auto it = local.cbegin(); it != local.cend(); ++it) {
        addJavaRuntimeItem(combo, it.key(), it.value());
    }
    // 钉死的那个路径也要在列表里（它可能不在扫描结果里：用户在别的盘上装的 Java、
    // 或者那个路径已经失效）。没有它的话，用户一进设置页看到的是「自动」亮着，
    // 而设定其实是手动 —— 界面在说谎。
    addJavaRuntimeItem(combo, 0, m_javaSettings->javaPath());
    selectConfiguredJava(combo, m_javaSettings->autoSelectJava(), m_javaSettings->javaPath());
}

void MainWindow::onMainTabChanged() {
    // 底栏左下角的显示规则依赖"当前是不是在主页"，切页就得重算。
    updateBottomBar();
    if (m_ui->getTabWidget()->currentIndex() == MainWindowUI::settingsTabIndex()) {
        // 进设置页才做的两件事：扫 Java（要跑几次 `java -version`）、重算自动档内存
        // （可用内存是个一直在变的数，用户看到的那句说明得是**现在**的）。
        refreshJavaList();
        refreshMemoryUi();
    }
}

void MainWindow::onJavaSelectionChanged() {
    const QString path = m_ui->getJavaVersionCombo()->currentData().toString();
    if (path.isEmpty()) {
        // 「自动」那一条。**刻意不清空 m_javaPath**：那个字段同时是"上次挑中的
        // 路径"的缓存，清掉的话每次启动都要重新扫一遍本地 Java。
        m_javaSettings->setAutoSelectJava(true);
    } else {
        m_javaSettings->setAutoSelectJava(false);
        m_javaSettings->setJavaPath(path);
    }
    // 这两个 setter 只改内存，落盘要显式调（GlobalJavaSettings 的约定）。
    m_javaSettings->save();
    updateJavaPathLabel();
}

void MainWindow::refreshMemoryUi() {
    // 自动档下这里会按**现在**的系统内存重算一次 Xmx/Xms。手动档下 refreshSuggestedMemory()
    // 只读不动 —— 用户定好的两个数一个都不许改。
    const SystemMemory::Usage usage = m_javaSettings->refreshSuggestedMemory();
    const bool autoMemory = m_javaSettings->autoMemory();

    // 上限先按这台机器收口，**再**灌值：盘上可能存着一个比物理内存还大的 Xmx（换过机器、
    // 或者在界面上限是 65536 的那些版本里手动填过），先把上限压下来，下面 setValue 才是
    // 个合法值（数字框 setRange 时自己会夹一次，但那是副作用，不是这里的意思）。
    applyMemoryLimits(usage.totalMb);
    const int capMb = m_ui->getMemoryMaxSpin()->maximum();
    int xmxMb = m_javaSettings->xmx();
    bool clamped = false;
    if (xmxMb > capMb) {
        // **静默**改用户的配置是不行的：他下次会在游戏日志里看到 JVM 实际拿到的内存和
        // 自己填的不是一个数。落盘 + 记一行日志，至少有一处说得出这件事发生过。
        appendLog(tr("内存：盘上记的最大内存 %1 MB 超过这台机器的物理内存，已降到 %2 MB。")
                      .arg(xmxMb)
                      .arg(capMb));
        m_javaSettings->setXmx(capMb);
        xmxMb = capMb;
        clamped = true;
    }
    if (clampXmsToXmx(xmxMb)) {
        // 这一句过去是接在"用户改数字框"那一路上的（那一格现在没了）。拖动条的机器上
        // 它照样会响：把最大内存拖到最小内存以下时，最小内存跟着降。
        appendLog(tr("内存：最小内存不能超过最大内存，已跟着降到 %1 MB。").arg(xmxMb));
        clamped = true;
    }
    if (autoMemory || clamped) {
        // 自动算出来的值也落盘：盘上那份就是"上次按系统内存算出来的值"，用户从自动切到
        // 手动时正好拿它当起点，也让"界面显示的就是盘上存的"这条一直成立。
        // 手动档下走到这儿只有一种可能：上面那两条夹过值。
        m_javaSettings->save();
    }

    const QSignalBlocker blockCheck(m_ui->getMemoryAutoCheck());
    const QSignalBlocker blockMax(m_ui->getMemoryMaxSpin());
    const QSignalBlocker blockSlider(m_ui->getMemoryMaxSlider());
    m_ui->getMemoryAutoCheck()->setChecked(autoMemory);
    m_ui->getMemoryMaxSpin()->setValue(xmxMb);
    m_ui->getMemoryMaxSlider()->setValueMb(xmxMb);
    // 自动档下把这一整行禁掉：里面是刚算出来的建议值，让人以为能改就是"改了不生效"。
    m_ui->getMemoryMaxSpin()->setEnabled(!autoMemory);
    m_ui->getMemoryMaxSlider()->setEnabled(!autoMemory);

    // 说明文字：把"这台机器有多少内存、现在还剩多少、最后给了多少"三件事说全 ——
    // 自动档下用户看不到（也改不了）上面那个数，只能靠这句话知道为什么是这个值。
    // ★ 2026-09-26：最小内存那一格从界面上撤掉了（用户要求），但它**仍然发给 JVM**，
    //   所以这句话要把它现在的值说全 —— 否则"启动器到底给了多少 -Xms"在整个界面上
    //   一个字都找不到，只能去翻注册表。
    const QString totalText = humanMemorySize(usage.totalMb);
    const QString availableText = humanMemorySize(usage.availableMb);
    if (usage.totalMb <= 0) {
        // 读不到系统内存（非 Windows 平台或系统调用失败）：照实说，别让用户以为是"按你的
        // 内存算的"。实际用的是内置默认值。
        // NOTE: 这一支**不看 autoMemory**，是照着自动档写的：手动档下会对着用户自己刚
        //       定的那个数说"自动分配用的是内置默认值"。手动档下那句话两头都不对，
        //       只是数值本身在上面那一行里能直接看到，就先这么写着。
        m_ui->getMemoryAutoHint()->setText(
            tr("读不到系统内存，自动分配用的是内置默认值（最大 %1 MB）。"
               "最小内存由启动器定（%2 MB），不用管它。")
                .arg(m_javaSettings->xmx())
                .arg(m_javaSettings->xms()));
        return;
    }
    if (autoMemory) {
        m_ui->getMemoryAutoHint()->setText(
            tr("系统总内存 %1、当前可用 %2，自动给到最大 %3 MB。"
               "最小内存由启动器定（%4 MB），不用管它。"
               "每次启动前都会按当时的可用内存重算。")
                .arg(totalText, availableText)
                .arg(m_javaSettings->xmx())
                .arg(m_javaSettings->xms()));
        return;
    }
    m_ui->getMemoryAutoHint()->setText(
        tr("系统总内存 %1、当前可用 %2；现在是手动指定（最大 %3 MB）。"
           "最小内存由启动器定（%4 MB），不用管它。"
           "上面那条拖到哪儿就是哪儿，也可以直接填数字。")
            .arg(totalText, availableText)
            .arg(m_javaSettings->xmx())
            .arg(m_javaSettings->xms()));
}

void MainWindow::applyMemoryLimits(qint64 totalMb) {
    // 算法是 MainWindowUI::memoryCapForTotalMb() —— **不是**这里现算一遍：实例设置那一页
    // 也要按同一个上限收口（见 InstanceSettingsDialog 里那段），两处各写一份的话迟早会
    // 长得不一样，而"设置页能填 4096、实例页能填 16384"这种不一致用户一眼就能看出来。
    int capMb = MainWindowUI::memoryCapForTotalMb(totalMb);
    const int minMb = MainWindowUI::memoryMaxMinMb();

    // 只改上下限、不动值：调用方紧接着就灌值了（见 refreshMemoryUi）。挡信号是因为
    // 改上限时数字框会自己夹一下当前值、条也会跟着重排档位 —— 那两发都不该被当成
    // "用户改了内存"（真按用户改的处理，会顺手把这次夹的值落盘）。
    const QSignalBlocker blockMax(m_ui->getMemoryMaxSpin());
    const QSignalBlocker blockSlider(m_ui->getMemoryMaxSlider());
    m_ui->getMemoryMaxSpin()->setRange(minMb, capMb);
    m_ui->getMemoryMaxSlider()->setRangeMb(minMb, capMb);
}

bool MainWindow::clampXmsToXmx(int xmxMb) {
    if (m_javaSettings->xms() <= xmxMb) {
        return false;
    }
    // 永远拉最小、不去顶最大。两根都是"用户刚改的那一根"，猜错的代价是把用户正在输入的
    // 那个数改掉 —— 而现在界面上只剩一根了（最大内存），更没得猜。
    m_javaSettings->setXms(xmxMb);
    return true;
}

void MainWindow::onJavaWarnMismatchChanged() {
    const bool warn = m_ui->getJavaWarnMismatchCheck()->isChecked();
    m_javaSettings->setWarnVersionMismatch(warn);  // 内部落盘
    // 这个开关只管**下次启动**问不问，当前这一次不受影响（已经在启动路径上了），
    // 所以这里不碰任何和"正在跑的实例"有关的状态。
    appendLog(warn ? tr("实例指定的 Java 版本不符时：会提醒。")
                   : tr("实例指定的 Java 版本不符时：不再提醒，直接按它指定的 Java 启动。"));
}

void MainWindow::onMemoryAutoChanged() {
    const bool autoMemory = m_ui->getMemoryAutoCheck()->isChecked();
    m_javaSettings->setAutoMemory(autoMemory);  // 内部落盘
    // 切到自动：按当前系统内存算一次并禁掉两个框。切到手动：**留着**刚才那两个建议值，
    // 用户是想微调，不是想从内置默认值重来（refreshMemoryUi 里那条 autoMemory() 守卫
    // 保证手动档下不会去改值）。
    refreshMemoryUi();
    appendLog(autoMemory ? tr("内存分配：已改为按系统内存自动分配。")
                         : tr("内存分配：已改为手动指定。"));
}

void MainWindow::onMemoryChanged() {
    if (m_javaSettings->autoMemory()) {
        // 自动档下这一行是禁用的，用户改不到这里。真走到这一句（程序内部 setValue）
        // 也不能把自动算出来的值当成"用户手动调的值"存下去 —— 那样下次进设置页就会看到
        // 手动档亮着，而用户从没动过它。
        return;
    }
    const int maxMb = m_ui->getMemoryMaxSpin()->value();

    // 数字框和条说的是同一件事：用户直接打了 6144 时条还停在刚才那一格上，得挪过去
    // （反过来那条路走 onMemorySliderMoved，它也会把数字框带过来）。挡信号：这一发是
    // "跟着走"，不是"又改了一次"。
    if (m_ui->getMemoryMaxSlider()->valueMb() != maxMb) {
        const QSignalBlocker blockSlider(m_ui->getMemoryMaxSlider());
        m_ui->getMemoryMaxSlider()->setValueMb(maxMb);
    }

    m_javaSettings->setXmx(maxMb);
    // Xms ≤ Xmx 这条不许破（见 .hpp 里 clampXmsToXmx 那段）。把最大内存拖到 256 时，
    // 盘上那个 1024 的最小内存得跟着降 —— 不然后面每次启动 JVM 都直接报错退出。
    clampXmsToXmx(maxMb);
    m_javaSettings->save();
    // 说明文字里写着"最大 %3 MB、最小 %4 MB"，改了数不重画它就一直是旧值。手动档下
    // refreshMemoryUi() 只读不动（见它开头那段），不会把刚改出来的数冲掉。
    refreshMemoryUi();
}

void MainWindow::onMemorySliderMoved() {
    const int maxMb = m_ui->getMemoryMaxSlider()->valueMb();
    if (m_ui->getMemoryMaxSpin()->value() != maxMb) {
        const QSignalBlocker blockSpin(m_ui->getMemoryMaxSpin());
        m_ui->getMemoryMaxSpin()->setValue(maxMb);
    }
    if (m_ui->getMemoryMaxSlider()->isSliderDown()) {
        // 还在拖：数字框跟着走就够了。落盘留给松手那一下（见 ctor 里 sliderReleased 那条线）
        // —— 一次拖动会发几十上百发 valueChanged，每一发都写一遍注册表、还顺带重算一次
        // 系统内存，是不行的。
        return;
    }
    // 不是拖动中：键盘左右键、滚轮、PgUp/PgDn 都走这里 —— 那几种一次就是一格，
    // 本来就该立刻落盘（也正好是"松手了"的等价物）。
    onMemoryChanged();
}

void MainWindow::onLauncherBehaviourChanged() {
    const int value = m_ui->getLauncherBehaviourCombo()->currentData().toInt();
    // setter 内部会比较，值没变就不写盘（构造时灌值那条路有 QSignalBlocker，走不到这里）。
    m_settings->setLauncherBehaviour(static_cast<LauncherBehaviour>(value));
}

void MainWindow::onDownloadSourceChanged() {
    const int value = m_ui->getDownloadSourceCombo()->currentData().toInt();
    m_settings->setDownloadSource(static_cast<DownloadSource>(value));
    syncDownloadSource();
}

void MainWindow::onDownloadAutoIntoInstanceChanged() {
    const bool on = m_ui->getDownloadAutoIntoInstanceCheck()->isChecked();
    m_settings->setDownloadAutoIntoInstance(on);
    // 说一句话（和「显示游戏日志」那条同一个做法）：这一项管的是"以后还问不问"，而用户
    // 改完在界面上看不出任何立刻的变化 —— 不写一句的话，他只能等到下一次下载才知道生效没有。
    appendLog(on ? tr("下载：从实例设置里点「下载新模组」时不再问存哪儿，直接放进该实例。")
                 : tr("下载：不管从哪儿点进来的，每次都会问存哪儿。"));
}

void MainWindow::onShowGameLogChanged() {
    const bool show = m_ui->getShowGameLogCheck()->isChecked();
    m_settings->setShowGameLog(show);
    // NOTE: 这里**不去关已经开着的那些窗**。用户可能正对着一个崩溃日志在看，而他在设置页
    //       把开关关掉的意思是"以后别再自动开了"，不是"把我正在看的那份关掉"。
    appendLog(show ? tr("游戏日志：启动游戏时会开一个日志窗。")
                   : tr("游戏日志：不再自动开日志窗。"));
}

void MainWindow::onWarnDuplicateLaunchChanged() {
    m_settings->setWarnDuplicateLaunch(m_ui->getWarnDuplicateLaunchCheck()->isChecked());
}

void MainWindow::onWarnWorldLossChanged() {
    m_settings->setWarnWorldLoss(m_ui->getWarnWorldLossCheck()->isChecked());
}

void MainWindow::onProcessPriorityChanged() {
    const int value = m_ui->getProcessPriorityCombo()->currentData().toInt();
    // setter 内部会比较，值没变就不写盘（构造时灌值那条路有 QSignalBlocker，走不到这里）。
    m_settings->setProcessPriority(static_cast<ProcessPriority>(value));
}

void MainWindow::onGameWindowSizeChanged() {
    // 两个框一起存（setGameWindowSize 收一对）：只改一个也得能一次落盘，
    // 而且要落的是一个**自洽的**宽高对。
    m_settings->setGameWindowSize(m_ui->getGameWindowWidthSpin()->value(),
                                  m_ui->getGameWindowHeightSpin()->value());
}

void MainWindow::onTitleTextChanged() {
    // 存的是**框里那串原文**：去空白、截长度都由 setTitleText() 自己做（归一化只有那
    // 一处，界面这一层不改用户的字）。取回来的才是"盘上那一份"，条上画的就是它 ——
    // 于是条上显示的字和盘上存着的字永远是同一个（不然"打了 17 个字、条上是前 16 个、
    // 框里是 17 个"这种事迟早会发生）。
    m_settings->setTitleText(m_ui->getTitleTextEdit()->text());
    m_ui->titleBar()->setBrandText(m_settings->titleText());
}

void MainWindow::syncDownloadSource() {
    // 改完立刻生效，不用重启启动器 —— 设置是**发请求那一刻**读的（见 DownloadManager）。
    // 正在下着的那一批不会被中途改道，这是对的：半批官方半批镜像更难查。机制上是"一个
    // 批次的 URL 在入队那一刻就定死了"（downloadFile 收到的是一整个字符串），所以代价是
    // 这次下载开始之后再改设置，要等下一次下载才见效。
    m_versionManager->setUseMirror(m_settings->downloadSource() == DownloadSource::Mirror);
}

void MainWindow::syncCurseForgeSettings() {
    // 和 syncDownloadSource() 同一个形状：数据层看不见 LauncherSettings（那是这一层的），
    // 所以两项设置由这里推过去。**两个键都推**：`Launcher/CurseForgeApiBase` 是验证工程
    // 的口子（产品从不写它，只把它读出来转给数据层 —— 离线端到端那条路就靠它把接口指到
    // 本机的假服务器上）。
    m_versionManager->setCurseForgeApiKey(m_settings->curseForgeApiKey());
    m_versionManager->setCurseForgeApiBaseOverride(m_settings->curseForgeApiBaseOverride());
    // 下载页那条搜索/版本列表的路**也要这两条**（CurseForge 是很久以后才加进那边的第二个
    // 来源）：同一份设置、同一条接口根地址，没有理由推给一个、不推给另一个。
    m_resourceStore->setCurseForgeApiKey(m_settings->curseForgeApiKey());
    m_resourceStore->setCurseForgeApiBaseOverride(m_settings->curseForgeApiBaseOverride());
    // Modrinth 那一侧那个口子。**三处都要它**（2026-09-28 更正了这一条注释 —— 原来写的是
    // "只有 VersionManager 要它"，而图标反查从 2026-09-27 起就在读了，下载页那几页从
    // 2026-09-28 起也读）：
    //   ① 导出整合包的反查（VersionManager）；
    //   ② 实例文件那一页的图标/名字反查（InstanceIconLookup）；
    //   ③ 下载页的搜索、版本列表、前置名字（ResourceStore）。
    // 放在这个函数里纯粹是"和上面那两条一起推"：同一个时机、同一份设置对象。
    m_versionManager->setModrinthApiBaseOverride(m_settings->modrinthApiBaseOverride());
    m_resourceStore->setModrinthApiBaseOverride(m_settings->modrinthApiBaseOverride());
}

void MainWindow::onCurseForgeApiKeyEdited() {
    // **不边打字边存**（和上面 JVM 参数那条同一个理由的反面）：半串 key 存进去的后果是
    // 一次 403，而用户看到的现象是"导入整合包失败"，那句话里说的是"key 可能不对" ——
    // 他自己刚打的字，会以为启动器坏了。所以等这一格编辑完（回车或者焦点离开）。
    m_settings->setCurseForgeApiKey(m_ui->getCurseForgeApiKeyEdit()->text());
    syncCurseForgeSettings();
}

void MainWindow::onVersionNameOptionChanged() {
    VersionNameOptions options;
    options.showCustomName = m_ui->getVersionNameCustomCheck()->isChecked();
    options.showRealVersion = m_ui->getVersionNameRealCheck()->isChecked();
    options.showLoaderName = m_ui->getVersionNameLoaderCheck()->isChecked();
    options.showLoaderVersion = m_ui->getVersionNameLoaderVersionCheck()->isChecked();
    // setter 内部会 normalize 并落盘。
    m_settings->setVersionNameOptions(options);

    // 归一化可能把从属的那一项关掉（"加载器版本"的前提是"加载器名"）。必须把结果
    // 回灌到勾选框上：不回灌的话，用户取消"加载器名"之后界面上的"加载器版本"还亮着，
    // 而拼名字时它已经不参与 —— 界面在说谎，而且下次打开设置时它又自己灭了。
    pushVersionNameOptionsToUi(m_settings->versionNameOptions());

    // 显示名变了，界面上所有显示版本名的地方都得重拼。两处都不读磁盘以外的东西，
    // 重来一遍很便宜：已安装的那份本来就要读每个版本的 wtfml.meta。
    refreshInstalledVersions();
    refreshAvailableVersions();
}

void MainWindow::onApplyExtraJvmArgsClicked() {
    // 去首尾空白：末尾多一个空格会让命令行里多出一个空参数。
    // 空串是合法的（就是"不加额外参数"）。
    const QString args = m_ui->getExtraJvmArgsEdit()->toPlainText().trimmed();
    m_javaSettings->setExtraJvmArgs(args);
    m_javaSettings->save();
    appendLog(tr("已保存全局 JVM 参数：%1").arg(args.isEmpty() ? tr("（空）") : args));
}

void MainWindow::applyLauncherBehaviourAfterLaunch() {
    switch (m_settings->launcherBehaviour()) {
    case LauncherBehaviour::Minimize:
        showMinimized();
        break;
    case LauncherBehaviour::Close:
        // hide() 而不是 close()：close() 是"关窗口"，启动器是最后一个窗口时那等于
        // 退出进程，而游戏是它的子进程 —— 用户以为只是把启动器收起来，游戏跟着没了。
        hide();
        break;
    case LauncherBehaviour::CloseAndRestore:
        m_hiddenForGame = true;
        hide();
        break;
    case LauncherBehaviour::Unchanged:
        break;
    }
}

void MainWindow::onVersionsFetched(const QStringList &versions) {
    refreshAvailableVersions();
    // 下载页那五页的「版本」筛子也灌一遍：它的选项就是这份在线清单（用户要"按版本搜"，
    // 而"有哪些版本"这件事只有这一份是一手来源）。放在**失败也照灌**的位置 —— 空清单时
    // 灌出来是空框，而空框仍然能自己打字（这一格本来就是可输入的）。
    fillDownloadFilterVersionOptions();

    // 顺带把已安装的也刷一遍：一次网络往返之后两个下拉框都是最新的，这条不变式
    // 很便宜（就是一次 QDir 扫描），但能兜住"清单在拉的这段时间用户往
    // .minecraft/versions 里拷了个版本目录"这种情况。
    refreshInstalledVersions();

    if (versions.isEmpty()) {
        // 拉清单失败时 DownloadManager 也会发这个信号，发的是**上一次成功的结果**
        // （首次拉取就失败才是空的），所以这一支实际拦的是"从来没拉到过"。别让用户
        // 对着一棵永远空的树等下去 —— 日志里说清楚，树上那四条照样在（它们本来就是空的）。
        appendLog(tr("在线版本列表获取失败（网络异常？）。"));
        return;
    }
    appendLog(tr("已更新在线版本列表 (%1 个版本)。").arg(versions.size()));
}

QString MainWindow::launchIdentityKey() const {
    // 规矩的整段理由在头文件里。这里只强调一处：**会话那一侧走的是同一份算法**
    // （sessionIdentityKey()，就在本文件顶上那个匿名 namespace 里）—— 两处不一致的症状
    // 是"我自己刚启动的那一份被当成别人开的"，于是「重复启动」那条永远不响。
    if (offlineIdentity()) {
        return m_authManager->playerName();
    }
    const QString uuid = m_authManager->playerUuid();
    return uuid.isEmpty() ? m_authManager->playerName() : uuid;
}

bool MainWindow::confirmLaunchWhileRunning(const QString &versionId) {
    const QString identity = launchIdentityKey();

    // 这个实例现在在跑的都有哪几份，**一次取完**：下面两条问话读的是同一份快照。
    // 卡片是嵌套事件循环里跑的（`execPrompt`），但它回来之前外面这一层不会往下走 ——
    // 所以不存在"第一问的时候还好好的、第二问的时候那一份已经退了"这种时序缝。
    bool sameInstanceRunning = false;
    bool sameIdentityRunning = false;
    for (const LauncherManager::GameSession &session : m_launcherManager->runningGames()) {
        if (session.versionId != versionId) {
            continue;
        }
        sameInstanceRunning = true;
        if (sessionIdentityKey(session) == identity) {
            sameIdentityRunning = true;
        }
    }

    const QString displayName = instanceDisplayName(versionId);

    // ① 「重复启动」：同一个身份、同一个实例，而且**我上一次点启动就在十秒之内**。
    //
    // NOTE: 要判"确实有那么一份在跑"（sameIdentityRunning）而不是只看时间戳。只看时间戳的话
    //       上一次启动失败（登录缺失 / 版本不完整那些早退路径都在这两问**后面**）时，
    //       用户十秒内再点一下就会被问"已经有一个相同的运行中游戏进程了" —— 而那句话是假的，
    //       界面上一个游戏进程都没有。
    if (m_settings->warnDuplicateLaunch() && sameIdentityRunning) {
        const qint64 last = m_lastLaunchAttempts.value(identity).value(versionId, 0);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        // `now >= last` 挡的是时钟被往回拨（对时）那一档：负的间隔不该被当成"刚刚点过"，
        // 否则一次对时就能让这个框冒出来。
        const bool withinWindow =
            last > 0 && now >= last && now - last <= duplicateLaunchWindowMs();
        if (withinWindow) {
            const int choice = InlineMessage::ask(
                m_ui->overlay(), MessageKind::Question,
                tr("已经有一个相同的运行中游戏进程了！"),
                tr("「%1」刚刚才启动过一次，那一份现在还在跑。\n\n"
                   "再启动一份会开出第二个一模一样的游戏进程：同一个账号、同一个实例，"
                   "两份游戏开着同一份存档。")
                    .arg(displayName),
                QStringList{tr("取消"), tr("仍要启动")}, -1, 1, false,
                tr("可以在设置中关闭此提示（设置页「提示」→「重复启动」）。"));
            if (choice != 1) {
                return false;
            }
        }
    }

    // ② 「进度提醒」：这个实例**已经有任何一份在跑** —— 我开的、别人开的、上一问刚放行的
    //    都算。用户原话要的就是"上一问选了『仍要启动』之后接着问这一句"
    //    （「在前面选择仍要启动的情况下/不同账户」），所以这不是"二选一"：连点两下、两个
    //    开关都开着时，框是前后两个。
    if (m_settings->warnWorldLoss() && sameInstanceRunning) {
        const int choice = InlineMessage::ask(
            m_ui->overlay(), MessageKind::Warning, tr("进入单人世界可能会丢失进度"),
            tr("「%1」现在还有一份在跑。\n\n"
               "两份游戏同时打开同一份存档时，后保存（或者后退出）的那一份会盖掉另一份的"
               "进度 —— 存档可能回退，也可能坏掉。")
                .arg(displayName),
            QStringList{tr("取消"), tr("仍要启动")}, -1, 1, false,
            tr("可以在设置中关闭此提示（设置页「提示」→「进度提醒」）。"));
        if (choice != 1) {
            return false;
        }
    }

    // 记一笔：① 那个十秒窗口下一次读的就是它。记在**两问都过了之后**，所以"用户在这一问上
    // 选了取消"不会被当成一次"想启动"—— 他在那一问上的意思显然就是不想再开一份。
    m_lastLaunchAttempts[identity][versionId] = QDateTime::currentMSecsSinceEpoch();
    return true;
}

/// 从 version.json 里取这次启动要用的 assetIndexId 与 javaMajorVersion，各自带兜底值。
///
/// **唯一出处**：真的启动（onStartButtonClicked）和「导出启动脚本」都走它 —— 两条路各写一份
/// 兜底的下场是导出来的 .bat 和启动器真正执行的那条命令行悄悄分家，而那种分家只在**没写
/// 这两个字段的版本**上才看得出来（写了就都一样，改错了也测不出来）。
///
/// NOTE: 兜底值（assetIndex "29" / Java 21）只在 json 里没写那两级字段时才用得上。
/// NOTE: 两个出参都可以给 nullptr（调用方只关心其中一个时）。
static void versionRuntimeIds(const QJsonObject &versionJson, QString *assetIndexId,
                              int *javaMajorVersion) {
    QString index = QStringLiteral("29");
    int major = 21;
    const QJsonObject assetIndexObj = versionJson["assetIndex"].toObject();
    const QString actualAssetIndexId = assetIndexObj["id"].toString();
    if (!actualAssetIndexId.isEmpty()) {
        index = actualAssetIndexId;
    }
    const QJsonObject javaVersionObj = versionJson["javaVersion"].toObject();
    const int declaredMajor = javaVersionObj["majorVersion"].toInt();
    if (declaredMajor > 0) {
        major = declaredMajor;
    }
    if (assetIndexId != nullptr) {
        *assetIndexId = index;
    }
    if (javaMajorVersion != nullptr) {
        *javaMajorVersion = major;
    }
}

QStringList MainWindow::launchArguments(const QString &versionId, const QJsonObject &versionJson,
                                        const EffectiveJavaSettings &javaSettings,
                                        const QString &token, bool offlineMode) const {
    // 兜底值只在这一份 json 没写那两个字段时才用得上（唯一出处在 versionRuntimeIds）。
    QString assetIndexId;
    int javaMajorVersion = 0;
    versionRuntimeIds(versionJson, &assetIndexId, &javaMajorVersion);

    // 按版本的自定义参数来自 <id>/wtfml.meta 的 "jvmArgs"（PCL 那半边"自定义启动参数"的
    // 对应物）；全局那半边是 EffectiveJavaSettings::extraJvmArgs，在
    // generateJvmArguments() 里比它先入列，所以单个实例能盖掉全局默认值。
    // 玩家名和底栏左下角显示的是**同一个**（AuthManager::playerName()），离线与正版
    // 两条分支都用它 —— 以前离线分支自己拼 "Player<时间戳>"、正版分支写死 "Player"，
    // 于是底栏上的名字和游戏里看到的名字可以不一样。
    // UUID 也一样：正版分支用它当 --uuid（没登录时 AuthManager 给空串，那边退回全零）。
    // 「启动选项 → 窗口大小」：原样传下去，由 generateJvmArguments() 决定发成哪两个参数
    // （原版那条 has_custom_resolution 规则**用不上**，理由写在那边的 NOTE 里）。
    return m_launcherManager->generateJvmArguments(
        token, versionId, assetIndexId, versionJson, javaSettings, offlineMode, javaMajorVersion,
        m_versionManager->versionJvmArgs(versionId), m_authManager->playerName(),
        m_authManager->playerUuid(), m_settings->gameWindowWidth(), m_settings->gameWindowHeight());
}

void MainWindow::onStartButtonClicked() {
    // 先认"启动哪一个"：下面那道闸口是按版本判的（见那段说明），所以版本号要在它前面拿到。
    // 只认"已安装版本"下拉框：启动只可能发生在本地已经装好的版本上。
    const QString versionId = selectedVersionId(m_ui->getInstalledVersionCombo());
    if (versionId.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("未选择版本"),
                             tr("请先在「已安装版本」下拉框中选择一个版本。"));
        return;
    }

    // 闸口：**正在下载/安装的那一个**不能启动 —— 文件还没齐，点了也是崩。
    //
    // NOTE: 这条判据 2026-09-23 从"有任何一批下载在跑就拦"改成了**按版本**判。旧那条把
    //       启动和下载彻底串起来了：装 1.20.1 的那十来分钟里，用户想开一个早就装好的
    //       1.18.2 是点不动的，而两者之间一个字节的关系都没有。现在只有"你选中的那个
    //       正好在装"才拦 —— 也就只有那一种情况真的会启动一个文件不全的实例。
    //       那几个 isXxx() 判据（装加载器 / 导入整合包 / 装带名字的实例的前奏）没有丢，
    //       它们被收进了 installingVersionIds()：那几条路在"拉版本表、拉 profile"的前奏里
    //       还没开批次，靠的就是界面自己记的那个实例名。
    //
    // NOTE: 正常情况下用户根本选不到"正在装的那一个" —— 它已经被
    //       refreshInstalledVersions() 从列表里滤掉了（"下载完成前该实例不在启动选项中"）。
    //       这一道是兜底：滤掉那份列表和这道闸口读的是**同一个**
    //       installingVersionIds()，两边不可能各说各的。
    if (installingVersionIds().contains(versionId)) {
        InlineMessage::warning(m_ui->overlay(), tr("下载进行中"),
                             tr("「%1」的文件还没有下载完成（或者还在安装加载器 / "
                                "导入整合包），请等它结束后再启动。\n\n"
                                "别的实例不受影响，可以直接启动。")
                                 .arg(instanceDisplayName(versionId)));
        return;
    }

    // 启动提醒（设置页「提示」栏）：同一份存档被两个进程同时打开这件事**事后救不
    // 回来**（后保存的那一份盖掉先保存的），所以只有"点在启动上那一下"挡得住才算数。
    //
    // NOTE: 摆在**换令牌之前**是刻意的。那一段是同步的（可能要发请求、单次超时 15 秒），
    //       让用户先等十几秒再被告知"你已经开过了"是最差的顺序 —— 而且这两问的答案不受
    //       令牌影响（取消了就什么都不用做）。
    if (!confirmLaunchWhileRunning(versionId)) {
        return;
    }

    // 这一趟走不走离线：**只看当前账号是不是离线账号**（唯一出处，见 offlineIdentity()）。
    const bool offlineMode = offlineIdentity();

    QString token;
    if (!offlineMode) {
        // 拿不到令牌的原因有好几种（从没登录过 / 登录过期了换不出来 / 换的时候被 XSTS 挡了），
        // 对应的话完全不同。要一句"请先登录"，用户照做也还是登不上 —— 所以这里把
        // AuthManager 给的原因一起弹出来。
        QString tokenError;
        // 这一段**是同步的**：令牌需要换的时候它要在 UI 线程上发请求（单次超时 15 秒），
        // 期间界面完全不重绘。先给用户一句话再进去，别让他以为点了没反应。
        enterSyncWait(tr("正在校验登录状态..."));
        token = m_authManager->loadSavedToken(&tokenError);
        leaveSyncWait();
        if (token.isEmpty()) {
            // NOTE: 这两句原来写的是"或使用离线模式" —— 那个开关去掉了（2026-09-24），
            //       照旧文案去界面上找是找不到东西的。现在指的是**那一个动作**：在
            //       「账号与外观…」那个窗里填个名字、点「添加离线账号」。
            InlineMessage::warning(m_ui->overlay(), tr("登录缺失"),
                                 tokenError.isEmpty()
                                     ? tr("请先登录 Microsoft 账户，或者在「账号与外观…」"
                                          "里添加一个离线账号。")
                                     : tr("%1\n\n请重新登录 Microsoft 账户，或者在"
                                          "「账号与外观…」里添加一个离线账号。")
                                           .arg(tokenError));
            return;
        }
    }

    // version.json 要提前取：natives 的准备和启动前检查都依赖它
    QJsonObject versionJson = m_versionManager->getVersionJson(versionId);

    // 拿不到 version.json 就不能猜。以前这里会静默用 javaMajorVersion = 21 和
    // assetIndex = "29" 继续启动，26.1（需要 Java 25）就会崩在
    // UnsupportedClassVersionError，且看不出原因。
    if (versionJson.isEmpty()) {
        appendLog(tr("找不到版本 %1 的 version.json").arg(versionId));
        InlineMessage::warning(m_ui->overlay(), tr("版本信息缺失"),
            tr("找不到版本 %1 的 version.json，无法确定所需的 Java 版本。\n"
               "请先选中该版本并点击\"下载\"重新获取版本信息。")
                .arg(versionId));
        return;
    }

    // 继承链不完整（带模组加载器的版本最常见：它基于的原版没装）时**现在就说清楚**。
    //
    // 必须单列这一步，不能靠上面那句"版本信息缺失"：加载器版本自己那份 json 是**在**的，
    // 缺的是原版那份。用户照着"请重新下载该版本"做，下回来的还是同一个加载器版本，
    // 问题原样复现。
    const QString inheritanceProblem = m_versionManager->inheritanceProblem(versionId);
    if (!inheritanceProblem.isEmpty()) {
        appendLog(tr("版本继承链不完整：%1").arg(versionId));
        InlineMessage::warning(m_ui->overlay(), tr("版本不完整"), inheritanceProblem);
        return;
    }

    // 旧格式（1.18.2 及以下）的 natives 必须先落地，否则会被 checkPreLaunch 拦下。
    // 该方法自带守卫，已经解压过的版本直接返回，不会重复解压。
    QStringList nativeErrors;
    const QString nativesDir =
        m_launcherManager->prepareNatives(versionJson, versionId, &nativeErrors);
    if (nativesDir.isEmpty() && !nativeErrors.isEmpty()) {
        appendLog(tr("准备 natives 失败："));
        for (const QString &error : nativeErrors) {
            appendLog(error);
        }
        // NOTE: 这条以前只写日志。日志面板在主页是收起来的，用户看到的就是"点了启动、什么都没发生"。
        //       同文件 appendLog 上面那段 NOTE 的规矩：拦下用户操作的消息一律要弹窗。
        InlineMessage::warning(m_ui->overlay(), tr("无法启动"),
                             tr("准备 natives 失败，游戏起不来：\n\n%1")
                                 .arg(nativeErrors.join(QStringLiteral("\n"))));
        return;
    }
    if (!nativesDir.isEmpty()) {
        appendLog(tr("natives 已就绪：%1").arg(nativesDir));
    }

    QStringList errors;
    if (!m_launcherManager->checkPreLaunch(versionId, &errors, versionJson)) {
        appendLog(tr("启动前检查失败："));
        for (const QString &error : errors) {
            appendLog(error);
        }
        // 同上：缺 Java、游戏目录写不进去这一类都在这里，必须让用户看见。
        InlineMessage::warning(m_ui->overlay(), tr("无法启动"),
                             tr("启动前检查没通过，游戏起不来：\n\n%1")
                                 .arg(errors.join(QStringLiteral("\n"))));
        return;
    }

    // 兜底值只在 version.json 没写这个字段时才用得上（两处兜底的唯一出处是
    // versionRuntimeIds()）—— 这里要**提前**拿到 javaMajorVersion，下面挑 Java 就得用它。
    // assetIndex 那一半这里不取：它只进命令行，而那串参数是 launchArguments() 拼的。
    int javaMajorVersion = 0;
    versionRuntimeIds(versionJson, nullptr, &javaMajorVersion);
    // version.json 里的 javaVersion.component 指明了 Mojang 为该版本准备的运行时，
    // 例如 26.1 是 java-runtime-epsilon（Java 25）。
    const QString javaComponent = versionJson["javaVersion"].toObject()["component"].toString();

    // ---------- 每实例设置：先把"这次到底用哪一套值"合成出来 ----------
    //
    // 这个实例的覆盖来自它自己的 wtfml.meta（「实例设置…」写的那些键）。一个实例
    // 一个键都不用覆盖时 override.isEmpty()，下面这条路走出来的值和加覆盖层之前
    // 逐字节一致 —— 那是绝大多数实例的处境，也是这条改动必须保住的性质。
    const JavaSettingsOverride override =
        javaOverrideFromJson(m_versionManager->versionMeta(versionId));
    EffectiveJavaSettings javaSettings =
        resolveJavaSettings(m_javaSettings->effective(), override);

    // 实例覆盖了 Java 路径 = 这个实例是**手动档**：全局那个「自动」开关不许盖掉它。
    // 不这么判的话，用户给某个实例钉死的 Java 会在下一次启动时被"按版本要求自动挑"
    // 悄悄换掉，而那一项覆盖也就白设了。
    const bool instanceOwnsJavaPath = override.javaPath.has_value();
    bool allowAutoSelect = m_javaSettings->autoSelectJava() && !instanceOwnsJavaPath;

    // 「问」和「做」分开：这一段只负责拿到用户的答案，值、meta、全局设置一概不动 ——
    // 那些由下面按答案各走各的。问在合成值之后、挑 Java 之前。
    const JavaMismatchAnswer answer = askAboutJavaMismatch(versionId, override, javaMajorVersion);
    if (answer == JavaMismatchAnswer::AbortLaunch) {
        return;
    }
    // 「仍然用这个 Java 启动」只有一个后果：把"允许不匹配"那个口子打开。判在
    // javaMatchesMajor() 之前（见 ensureJavaForVersion 的值版），所以哪怕那条路径
    // 根本起不来，用户也已经明说了"就用它"。
    const bool allowMismatchedPath = (answer == JavaMismatchAnswer::KeepInstanceJava);
    // 「改用匹配的 Java 启动」也是只有一个后果：把实例钉的那条路径**丢掉**，
    // 让这次走"按版本要求自动挑"。丢掉的只是这次要用的那份值 —— 实例的 meta 要等
    // 挑出来之后才写回去（那才是用户要的"在实例设置中恢复为匹配的 Java"）。
    if (answer == JavaMismatchAnswer::UseMatchingJava) {
        javaSettings.javaPath.clear();
        allowAutoSelect = true;
    }

    QString javaError;
    // 这一段也可能是同步的、而且可能是**最久**的一段：本机没有匹配的 Java 时它会直接在
    // UI 线程上下一个完整的运行时（component 那份，或者退到 Adoptium 的那份），几百 MB。
    // 就算不用下，挑的过程里也要逐个 `java -version` 冷启 JVM、扫盘。
    enterSyncWait(tr("正在准备 Java 运行时..."));
    const bool javaReady = m_javaSettings->ensureJavaForVersion(
        javaSettings, javaMajorVersion, javaComponent, allowAutoSelect, &javaError,
        allowMismatchedPath);
    leaveSyncWait();
    if (!javaReady) {
        appendLog(tr("无法获取所需的 Java 运行时环境：版本 %1").arg(javaMajorVersion));
        InlineMessage::warning(m_ui->overlay(), tr("Java 运行时缺失"),
            instanceOwnsJavaPath
                ? tr("「%1」指定的 Java 用不了（路径不存在，或者主版本不是 %2）。\n"
                     "可以在「实例设置…」里改这个实例的 Java 路径，或者把它切回「跟随全局」。")
                      .arg(instanceDisplayName(versionId))
                      .arg(javaMajorVersion)
                : tr("找不到或无法下载 Java %1，请手动指定 Java 路径。").arg(javaMajorVersion));
        return;
    }

    // 「改用匹配的 Java」：把刚挑中的那条路径写回**这个实例**。
    //
    // 用户原话是"在实例设置中恢复为匹配的 Java"，所以写的是那个实例的 wtfml.meta，
    // 不是全局 —— 下面那段 adoptJavaPath 的注释说的就是为什么不能写全局。
    //
    // NOTE: 这一步失败**要弹窗而且不启动**。这次启动其实能成（javaSettings 里已经有
    //       路径了），但下次启动开头又会读到原来的覆盖、又问一遍同一个问题 ——
    //       看起来就像"他的选择没被记住"。宁可现在说清楚，也别让用户对着一个反复
    //       出现的框去猜。
    if (answer == JavaMismatchAnswer::UseMatchingJava) {
        QString writeError;
        if (!m_versionManager->updateVersionMeta(
                versionId, {{javaPathMetaField(), QJsonValue(javaSettings.javaPath)}},
                &writeError)) {
            appendLog(tr("把匹配的 Java 写进实例设置失败：%1").arg(writeError));
            InlineMessage::warning(m_ui->overlay(), tr("实例设置写入失败"),
                tr("找到了匹配的 Java（%1），但没能把它记到实例「%2」的设置里：\n%3\n\n"
                   "没有启动。你可以在「实例设置…」里手动指定这条路径。")
                    .arg(javaSettings.javaPath, instanceDisplayName(versionId), writeError));
            return;
        }
        appendLog(tr("已把「%1」指定的 Java 换成 %2")
                      .arg(instanceDisplayName(versionId), javaSettings.javaPath));
    }

    // 挑中的 Java 路径落在**哪一层** —— 这就是"跨作用域污染"那道坎。
    //
    // 实例覆盖了 javaPath 的实例**不需要写回任何地方**（所以没有"实例档写回 meta"
    // 那一步）：那种情况上面刚判成手动档，而手动档下 ensureJavaForVersion() 只有两种
    // 结局 —— 原样认可那个路径，或者直接失败返回。**它没有能力换一个路径出来**，
    // 也就是说只要实例钉了路径，这次启动就不可能改到全局配置，污染在结构上不存在。
    //
    // 剩下的就只有全局档：老行为 —— 自动挑中的路径写回全局配置
    // （adoptJavaPath() 里那句"值没变就不写盘"也是老行为的一部分）。
    if (!instanceOwnsJavaPath) {
        m_javaSettings->adoptJavaPath(javaSettings.javaPath);
    }

    // 自动档内存要**在这里**再算一次（不是开窗时算的那次）：从打开启动器到点下"启动"
    // 可能过了很久，中间可能开了浏览器、IDE，可用内存差好几 GB，而 JVM 只认这一刻的机器
    // 状态。手动档下这一句什么都不动。
    //
    // NOTE: 这里**不刷设置页**。老代码在这一句之后会把设置页那两个框硬刷成"这次实际
    //       用的值"，有了实例覆盖之后那句话不再成立：实例覆盖了 2048 而设置页显示
    //       2048，用户会以为全局被改了。设置页显示的是**全局那一层**，它只由用户自己
    //       在设置页的改动（和切到设置页时的那次重算）决定。
    resolveMemoryForLaunch(javaSettings, override);

    // 这一串就是"这一次启动到底执行什么"的参数那半边。它和「导出启动脚本」导出的那份
    // 是**同一个函数**给的（见 launchArguments 的说明）—— 那边还要程序名，程序名在这里
    // 由 LauncherManager 自己从 javaSettings.javaPath 取（取不到退 PATH 上的 java）。
    const QStringList jvmArgs =
        launchArguments(versionId, versionJson, javaSettings, token, offlineMode);
    // 记住"上次启动的版本"的时机是**进程真的起来了**，不是"表达式算完了"。
    // startGame() 有三条早退路径（找不到 Java / waitForStarted 超时 / 会话数到顶），
    // 以前它们都会先骗一句"已开始启动游戏"再显示错误。以"起来了"为准也是对的：
    // 因为库缺失之类的原因崩掉，恰恰是最需要下次打开还选着这个版本的时候。
    //
    // startGame() 返回之前那一段（准备 natives、探 Java、等进程起来）是**同步阻塞**的，
    // 界面根本没机会重画 —— 所以那一段报不出百分比，只能报"在等什么"。
    // NOTE: 这里那句和下面 updateBottomBar() 里那句是**同一句话**（都出自
    //       MainWindowUI::launchingStatusText()），所以进程起来的那一刻文字不会跳。
    enterSyncWait(MainWindowUI::launchingStatusText(instanceDisplayName(versionId)));
    // 身份和实例名都在**启动这一刻**钉进会话里（LauncherManager 的 GameSession 快照）：
    // 之后切账号、改实例名都不会影响"这一份当初是谁跑的"。多开这件事最要紧的就是这条 ——
    // 同一个实例挂两个账号各开一个时，两份会话的 accountName 必须是各自的，不能都读
    // "当前登录的那个"。
    const int sessionId = m_launcherManager->startGame(
        jvmArgs, javaSettings, versionId, instanceDisplayName(versionId),
        m_authManager->playerName(), m_authManager->playerUuid(),
        m_settings->processPriority());
    leaveSyncWait();
    if (sessionId > 0) {
        m_settings->setLastLaunchedVersion(versionId);
        appendLog(tr("已开始启动游戏，正在执行 JVM 参数..."));
        // 启动进度从这里开始走到"这一份自己吐出第一行输出"为止。startGame() 返回之前
        // 那段只能报"在等什么"（见上面那次 enterSyncWait()）—— 这里能覆盖的是随后的
        // JVM/游戏初始化。
        // 顺序上安全：startGame() 内部那条 "游戏已启动" 的 processOutput 是在它
        // return 之前同步发出的，那时这一份还不在集合里，所以不会被当成"游戏已经出话了"。
        m_launchingSessions.insert(sessionId);
        // ★ 2026-09-27：名字在这一刻记一份。**这里记的是"这一份当初是谁"**，和 startGame()
        //   收到的那个名字是同一个值（都是 instanceDisplayName(versionId)）—— 之后用户改名、
        //   切账号都不影响它，理由同 startGame() 那段（多开最要紧的就是这条）。
        //   用途只有一处：onGameFinished() 里那行日志的前缀（那时候会话已经从表里摘掉了，
        //   查不到名字，见那边的 NOTE）。
        m_sessionDisplayNames.insert(sessionId, instanceDisplayName(versionId));
        // 实例树上"（运行中）"要当场出来：这一份已经从"开始启动"走到"进程真的起来了"。
        updateInstanceTreeRunState();
        // 这一档只对"真的起来了"生效 —— startGame() 那三条早退路径下面根本走不到这里。
        applyLauncherBehaviourAfterLaunch();
    }
    updateBottomBar();
}

// ============================ 「账号与外观」那个模块 ============================
//
// 三件事在这个模块里的分工（写清楚免得下次改错地方）：
//   - **界面的样子**全在 ui/AccountSkinDialog.{hpp,cpp}（它不 include 任何 src/ 下的头）；
//   - **盘上的账**在 src/auth/AccountStore（多账号列表）+ TokenStore（凭据字节格式）；
//   - **下面这一片**是接线：谁来读谁、失败怎么报、哪个动作之后要刷新什么。
// 所以这里没有任何"界面布局"的代码，那边也没有任何"读注册表"的代码。

void MainWindow::onAccountSkinButtonClicked() {
    setupAccountSkinDialog();
    OverlayHost *overlay = m_ui->overlay();
    if (overlay == nullptr || overlay->isOpen()) {
        // 已经有覆盖层开着（另一页、或者一条提示）。这一层不做页面栈，这一次就不开了
        // —— 和 `openPage()` 返回 nullptr 是同一件事，见 OverlayHost.hpp 那段。
        return;
    }
    // 从"另开一个窗"改成"把内容区整块盖掉"（2026-09-25 用户给的方向：最终不出现独立窗口）。
    // 原来是**非模态** show()，理由是登录那一步会把浏览器推到前台、用户要在两边来回切；
    // 嵌进内容区之后这一点照旧成立 —— execPage 内部那个嵌套事件循环一直在转，界面是活的，
    // 浏览器本来也是外面的独立窗，谁都拦不着它。
    //
    // NOTE: 顺序照旧是**先摆出来、再刷新**。refreshAccountSkin() 最后那一段会去拉披风列表
    //       （真联网，最长能等十几秒），倒过来做的话头一次点这个入口就是"什么也没发生"
    //       —— 页面还没画出来，那个请求已经在跑了，用户只会以为按钮点坏了。
    //       这里排一发队：execPage 一进它自己的嵌套循环就先跑这一发，那时页面已经在屏幕上了。
    QMetaObject::invokeMethod(this, [this] { refreshAccountSkin(); }, Qt::QueuedConnection);
    overlay->execPage(m_accountSkinDialog, AccountSkinDialog::windowTitle());
}

void MainWindow::setupAccountSkinDialog() {
    if (m_accountSkinDialog != nullptr) {
        return;
    }
    m_accountSkinDialog = new AccountSkinDialog(this);
    // NOTE: 这个窗只发"用户想做什么"，一件事都不自己干 —— 收到信号之后干什么全在下面
    //       这几个槽里。所以它能在 ui_check 里被单独构造、单独驱动（不登录、不联网）。
    connect(m_accountSkinDialog, &AccountSkinDialog::loginRequested, this,
            &MainWindow::onAccountLoginRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::offlineAccountRequested, this,
            &MainWindow::onOfflineAccountRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::switchRequested, this,
            &MainWindow::onAccountSwitchRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::removeRequested, this,
            &MainWindow::onAccountRemoveRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::refreshRequested, this,
            [this] { refreshAccountSkin(); });
    connect(m_accountSkinDialog, &AccountSkinDialog::skinUploadRequested, this,
            &MainWindow::onSkinUploadRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::capeSelectRequested, this,
            &MainWindow::onCapeSelectRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::hideCapeRequested, this,
            &MainWindow::onCapeHideRequested);
}

bool MainWindow::offlineIdentity() const {
    // 读的是**盘上的活动槽**（AccountStore 那几个函数都是静态的、直接读写盘），不是
    // AuthManager 里那份内存副本 —— 两者在"切账号"那一刻会短暂不一致（activate() 先写盘，
    // 紧接着才调 setOfflinePlayer() / reloadStoredAccount()），而盘上那份才是权威。
    // 没有当前账号（从没登录、或者刚把当前那个删掉）时 activeUuid() 是空串，isOffline()
    // 对空串回 false —— 那种状态是"未登录"，不是"离线"。
    return AccountStore::isOffline(AccountStore::activeUuid());
}

QString MainWindow::accountSkinStatusText() const {
    // 当前账号是离线账号时先说这一句：名字就在上面那一列里躺着，比任何泛泛的说法都具体。
    //
    // NOTE: 2026-09-24 之前这里有两档"离线" —— 这一档，和"账号是正版的、但主页那个
    //       「离线模式」勾着"那一档。勾选框去掉之后只剩这一档了：**离线 = 当前账号本身
    //       是离线账号**，没有别的意思。
    const QString active = AccountStore::activeUuid();
    if (AccountStore::isOffline(active)) {
        return AccountSkinDialog::offlineIdentityText(AccountStore::nameOf(active));
    }
    if (!m_authManager->loggedIn()) {
        return AccountSkinDialog::notLoggedInNote();
    }
    return AccountSkinDialog::identityText(m_authManager->playerName());
}

void MainWindow::refreshAccountSkin() {
    // NOTE: **这一句只挡窗里那些东西**（账号列表 / 状态行 / 披风），预览那一份照刷 ——
    //       2026-09-24 用户报的 bug 就是它挡多了：「现在从未登录切换到选择离线账号不会随机
    //       给皮肤了」。主页那个小人、底栏头像、那个窗里的 3D 预览**都只在
    //       startPlayerSkinLoad() → applyPlayerSkin() 里更新**，所以原来这句 return 一挡，
    //       没点开过「账号与外观…」的会话里切账号根本不换脸，一直停在上一个人的脸上
    //       （从"未登录"切过去时停的就是那张兜底占位图）。
    //
    //       以前没人发现是因为切账号只能从「账号与外观…」里点 —— 那个窗开着，这句 return
    //       进不去；09-23 把切换搬到右面板之后，这条路才第一次被走到。
    if (m_accountSkinDialog == nullptr) {
        startPlayerSkinLoad();  // 窗没建 → 没有列表/状态行/披风要刷，但脸得换
        return;
    }

    // 1) 账号列表：**纯本地**（读 SavedAccounts 那个键），不联网、不碰令牌。
    QList<AccountSkinDialog::AccountRow> rows;
    const QList<SavedAccount> accounts = AccountStore::accounts();
    rows.reserve(accounts.size());
    for (const SavedAccount &account : accounts) {
        AccountSkinDialog::AccountRow row;
        row.uuid = account.uuid;
        row.name = account.name;
        row.savedAt = account.savedAt;
        row.current = account.isCurrent;
        row.offline = account.offline;
        rows.append(row);
    }
    m_accountSkinDialog->setAccounts(rows);

    // 2) 状态行回到"当前身份"那一档，忙态也在这里松开 —— 能走到这一句就说明上一步那个
    //    同步请求已经回来了（下面第 4 步会再离开这个方法去联网）。
    m_accountSkinDialog->setBusy(false);
    m_accountSkinDialog->setStatusText(accountSkinStatusText());

    // 3) 3D 预览走**和主页同一条路**（同一份缓存、同一次后台刷新）：applyPlayerSkin()
    //    会把同一份贴图同时喂给主页那个小人和这个窗里的预览。
    //
    //    NOTE: 这一句**不保证不联网**（2026-09-24 之前这里是那么写的，现在不成立了）：
    //          离线账号第一次进来时它要 MineSkin 那一页随机皮肤（见 startPlayerSkinLoad()
    //          里 offlineIdentity() 那一档）。挑过一次之后就只在盘上读那一行。
    //          这也正是这个窗**只在离线账号那一档开**的原因（见 onAccountSkinClicked()
    //          里那段）：正版那一档开窗会顺带拉披风，那才是真联网的那一下。
    startPlayerSkinLoad();

    // 4) 披风列表要令牌，是这一整块里唯一会真的去联网的地方（过期令牌也是在这里顺带刷新的）。
    if (offlineIdentity()) {
        // 当前是离线账号 = "别联网"，而拉披风列表正是联网。跳过。
        m_accountSkinDialog->setCapes({});
        return;
    }
    // ★ 2026-09-27：这一页**现在不在屏幕上**的时候，这一整段（下面那两发同步联网）跳过。
    //
    // 为什么非得另立一条判据：上面那句 `m_accountSkinDialog == nullptr` 分不出"关掉了" ——
    // 这个对话框是**建一次就留着**的（setupAccountSkinDialog() 见非空就返回，父对象是本
    // 窗口），页面关掉只是把它藏起来，指针永远不为空。于是"开过一次账号页、之后从右面板
    // 切账号"这一条路上，每一次切换都会在**没有人看着的那个页面上**点起忙态、然后同步
    // 等一次网络往返（loadSavedToken 还可能真去换一次令牌）：用户看到的是"切个账号整个
    // 启动器卡几秒"，而卡住的理由（「正在获取披风列表…」）摆在一个藏起来的页面里。
    //
    // NOTE: 判据是"**现在真摆在眼前**"（isVisible），和 OverlayHost 里 retargetCoveredPage()
    //       那条 NOTE 同一条规矩：页面收在别的选项卡上时它不在屏幕上，一样跳过。
    // NOTE: 上半截（账号列表 / 状态行 / 3D 预览）**照样刷了** —— 那些不联网。披风列表会在
    //       用户真的打开这一页时补上：开页那条路必走一次本函数（见 onAccountSkinClicked），
    //       而那时页面已经在屏幕上了。
    if (!m_accountSkinDialog->isVisible()) {
        return;
    }
    // 下面这两下是**同步**的（2026-09-25 补忙态）：loadSavedToken() 可能要真去换一次令牌、
    // fetchOwnedCapes() 一定发一次 GET —— 两个函数都是"调完才返回"，于是点「刷新」之后的
    // 这几秒钟里 UI 线程一直在这两行里面出不来：窗不重绘、什么都按不动，Windows 过一会儿
    // 还会给标题挂上"(无响应)"。
    //
    // NOTE: 上面 onAccountSkinClicked() 那段写着"先 show 出来，那一趟就变成『窗开着、
    //       状态行写着在忙』"—— 那句在改动前是**不成立**的：第 2 步的 setBusy(false)
    //       之后到这里谁也没把忙态点起来，用户看到的是一个画着旧内容、点什么都没反应的窗。
    //       忙态 + 一次 processEvents（排掉用户输入）和 enterSyncWait() 是同一个手法：
    //       进去之前先把话说出来。下面每条返回路上都要松开它。
    m_accountSkinDialog->setBusy(true, tr("正在获取披风列表…"));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QString error;
    const QString token = m_authManager->loadSavedToken(&error);
    if (token.isEmpty()) {
        // 没登录（或者令牌换不出来）。原因写进状态行 —— 它比"当前身份"那句更要紧，
        // 而"当前是哪个账号"在列表里那个（当前）标记上照样看得到。
        m_accountSkinDialog->setCapes({});
        if (!error.isEmpty()) {
            m_accountSkinDialog->setStatusText(error);
        }
        m_accountSkinDialog->setBusy(false);
        return;
    }

    QList<OwnedCape> capes;
    if (!m_skinApi->fetchOwnedCapes(token, &capes, &error)) {
        // 背景刷新失败**不弹窗**（这是这个窗里唯一一条不弹窗的错误路）：账号列表和预览
        // 都已经正常出来了，弹一个窗拦在中间会让用户以为整个模块坏了。正文写在状态行里，
        // 日志留一行备查。
        appendLog(tr("拉取披风列表失败：%1").arg(error));
        m_accountSkinDialog->setCapes({});
        m_accountSkinDialog->setStatusText(error);
        m_accountSkinDialog->setBusy(false);
        return;
    }

    QList<AccountSkinDialog::CapeRow> capeRows;
    capeRows.reserve(capes.size());
    for (const OwnedCape &cape : capes) {
        AccountSkinDialog::CapeRow row;
        row.id = cape.id;
        row.alias = cape.alias;
        row.active = cape.active;
        capeRows.append(row);
    }
    m_accountSkinDialog->setCapes(capeRows);
    // 到这里那一趟联网才算走完（这一条是上面那个忙态唯一的"成功"出口）。
    m_accountSkinDialog->setBusy(false);
}

void MainWindow::onAccountLoginRequested() {
    // 和设置页那个登录按钮**同一条路**：AuthManager 那三个信号是共用的，所以登录成功
    // 之后 onLoginSucceeded() 会把凭据收进列表、把这个窗刷新一遍，一个字都不用在这里补。
    // 返回值 true = 流程开了头；两条 false 的路径 AuthManager 会先发一次 loginFailed()，
    // 弹窗由 onLoginFailed() 统一负责。
    // 返回值刻意不管：失败那两条路径 AuthManager 会先发一次 loginFailed()，弹窗由
    // onLoginFailed() 统一负责（直接连接，所以这里返回之前那个窗已经弹出来了）。
    // 忙态也不在这里摆 —— loginMicrosoft() 一起头就会发 loginStarted()，由
    // onLoginStarted() 统一处理（见那段说明：一个来源，不给界面留第二份状态）。
    m_authManager->loginMicrosoft();
}

void MainWindow::onOfflineAccountRequested(const QString &name) {
    QString error;
    // 名字合不合法在这里判（其实是 AccountStore 判，唯一出处）：空名字、名字里有空格、
    // 超过 16 个字符。失败按产品约定弹窗 —— 这是用户主动发起、而且被挡住了的操作。
    if (!AccountStore::addOffline(name, &error)) {
        InlineMessage::warning(m_ui->overlay(), tr("添加离线账号失败"), error);
        return;
    }
    const QString trimmed = name.trimmed();
    // 离线账号 = "用这个名字离线进游戏"。**"这一趟走离线"这件事不需要另外打开** ——
    // addOffline() 已经把它设成当前账号了，而启动那条路读的正是"当前账号是不是离线账号"
    // （offlineIdentity()）。2026-09-24 之前这里还要顺手勾一下主页那个「离线模式」。
    //
    // 名字进 AuthManager：底栏左下那行和启动参数里的 --username 取的都是它。
    m_authManager->setOfflinePlayer(trimmed);
    if (m_accountSkinDialog != nullptr) {
        // 添加成功之后才清输入框：失败时留着让用户改一下再点（重打一遍是白费）。
        m_accountSkinDialog->clearOfflineNameDraft();
    }
    updateBottomBar();
    appendLog(tr("已添加离线账号「%1」并切换到它。").arg(trimmed));
    // 列表、状态行、预览、披风这一整套刷新 —— 和切换账号之后做的事情一样。
    //
    // NOTE: 离线账号那一档**不是"整条路都不联网"**（2026-09-24 之前这句注释是那么写的，
    //       现在不成立了）：披风确实不拉（离线账号没有披风可拉），但这个账号**还没挑过**
    //       随机皮肤时，startPlayerSkinLoad() 会去 MineSkin 要一页列表（见 RandomSkinStore）。
    //       挑过一次之后就只在盘上读那一行 —— 本地命中，一个包都不发。
    refreshAccountSkin();
}

void MainWindow::onBottomBarPanelAction(bool account, const QString &key, int action) {
    if (account) {
        if (action != kAccountPanelUseAction) {
            return;  // 右面板那一行只有一颗按钮；别的编号是接线错了，什么都不做
        }
        // 这一行那颗按钮**是哪种**由行的状态决定（当前账号那颗是「登出」，别人是「选择」/
        // 「登录」），而行的状态是 recentAccountPanelRows() 一家算的 —— 这里用**同一个判据**
        // 分流，绝不看按钮上画着什么。key = 账号 uuid。
        if (key == AccountStore::activeUuid()) {
            logoutCurrentAccount();
            return;
        }
        useAccountFromPanel(key);
        return;
    }

    // 左面板（key = 会话号，见 runningGamePanelRows()）。两个编号各是一颗按钮，
    // "关"和"改设置"是两件完全不同的事 —— 认编号，不认按钮上画着什么。
    switch (action) {
        case kRunningPanelStopAction:
            stopGameSession(key.toInt());
            return;
        case kRunningPanelSettingsAction: {
            // 进的是**这一行那个实例**的设置 —— 面板那一行自己带着版本号，和"当前选中的
            // 那个实例"没关系（用户点的就是这一行）。
            const LauncherManager::GameSession game = m_launcherManager->session(key.toInt());
            if (game.sessionId == 0) {
                appendLog(tr("这个实例刚刚已经结束了。"));
                return;
            }
            openInstanceSettings(game.versionId);
            return;
        }
        default:
            return;
    }
}

void MainWindow::logoutCurrentAccount() {
    // 认人用 activeUuid()，不用面板那一行的 key —— 面板弹出来到用户点下这一颗之间隔着
    // 他看那两眼的时间，这中间账号是可能被换掉的（recentAccountPanelRows() 那段注释里
    // 说的是同一件事）。眼下这两个值必然相等（onBottomBarPanelAction() 就是拿它分流的），
    // 但"登出谁"这件事只该有一个出处。
    const QString uuid = AccountStore::activeUuid();
    if (uuid.isEmpty()) {
        return;  // 本来就没登录（那一行不该长成「登出」）
    }
    const QString name = AccountStore::nameOf(uuid);
    if (!AccountStore::logout()) {
        InlineMessage::warning(m_ui->overlay(), tr("退出登录失败"),
                             tr("盘上的登录状态没能清掉，现在还是登录着「%1」。").arg(name));
        return;
    }
    // 内存里那一份也得跟着清 —— AuthManager 只在构造和换账号那两处读过盘，不重读的话
    // 底栏、启动参数、外观那几条路拿到的还是**上一个人的名字**（等于"看着登出了，
    // 手里还攥着他的状态"）。清干净之后 loggedIn() 是 false，界面自己退回未登录那一档。
    m_authManager->reloadStoredAccount();
    updateBottomBar();
    appendLog(tr("已退出账号「%1」（账号还留在列表里，点那一行的「选择」/「登录」就能回来）。")
                  .arg(name));
    // 外观也得跟着回默认：预览、账号列表里那个「当前」标记、披风列表都要重画一遍。
    // 没登录这一档走上半截就返回了（loadSavedToken() 取不到令牌），不会去联网。
    refreshAccountSkin();
}

void MainWindow::useAccountFromPanel(const QString &uuid) {
    if (uuid.isEmpty() || uuid == AccountStore::activeUuid()) {
        // 已经是它了。那一行的按钮本来就是灰的（见 recentAccountPanelRows()），走到这儿
        // 只是防一手。
        return;
    }
    const bool offline = AccountStore::isOffline(uuid);
    // 切换本身走**现成那一条**：onAccountSwitchRequested() 已经把"离线走 setOfflinePlayer、
    // 正版走 reloadStoredAccount"和"重画底栏 + 刷外观"都做完了。这里只补正版那半句。
    onAccountSwitchRequested(uuid);
    // 没切过去就到此为止。**这一句不是为了兜底而兜底**：onAccountSwitchRequested() 失败时
    // （列表里那份副本不完整会走那条）没有换人，活动槽里还是**上一个**账号的凭据 ——
    // 再往下走就会拿上一个人的令牌去做"够不够 6 小时"的判断，甚至去刷新他的令牌，
    // 而弹出来的话会把那个账号的名字写成这一行的名字。失败的框它自己已经弹过了。
    if (AccountStore::activeUuid() != uuid) {
        return;
    }
    if (offline) {
        return;  // 离线身份没有令牌这回事
    }

    // 正版：看一眼**盘上属于这个账号的那张**令牌还剩多少（onAccountSwitchRequested() 刚
    // 把这一份读进内存）。还够用（剩 ≥6 小时）就到此为止 —— 这一下不联网。
    //
    // NOTE: 这里刻意**不用 loadSavedToken()**：它只在"已经过期"时才去换，而用户要的是
    //       "剩不到 6 小时就换"（见 AuthManager::tokenRefreshWindowSecs 那段）。
    if (m_authManager->tokenSecondsLeft() >= AuthManager::tokenRefreshWindowSecs) {
        return;
    }
    QString error;
    // 这一段**是同步的**（15 秒超时的那种请求），先给用户一句话再进去。
    enterSyncWait(tr("正在刷新登录令牌..."));
    const bool refreshed = m_authManager->refreshMinecraftToken(&error);
    leaveSyncWait();
    if (refreshed) {
        appendLog(tr("「%1」的登录令牌剩余不足 6 小时，已经顺手换了一张新的。")
                      .arg(AccountStore::nameOf(uuid)));
        return;
    }
    // 失败：**账号已经切过去了**，只是令牌没换到新的 —— 这两件事必须分开说，不然用户会
    // 以为切换失败了（那他就会再点一次，而再点一次还是这个结果）。
    //
    // NOTE: 已知的两种失败原因（Azure 应用没被 Mojang 批准 / 盘上凭据属于另一个应用）
    //       都不是这里能修的，只能重新登录一次 —— 所以那句话得指出**下一步**，不能只说
    //       "刷新失败"。
    appendLog(tr("「%1」的登录令牌没能刷新：%2").arg(AccountStore::nameOf(uuid), error));
    InlineMessage::warning(m_ui->overlay(), tr("登录令牌没能刷新"),
                         tr("已经切到账号「%1」，但它保存的登录凭据换不出新令牌：\n\n%2\n\n"
                            "这个账号在「账号与外观…」里重新登录一次就好了。")
                             .arg(AccountStore::nameOf(uuid), error));
}

void MainWindow::onAccountSwitchRequested(const QString &uuid) {
    if (uuid.isEmpty() || uuid == AccountStore::activeUuid()) {
        return;  // 已经是它了：写盘一遍再读回来，白折腾
    }
    // 先问清是哪一种：activate() 之后盘上的状态已经变了，那时候再问容易问错。
    const bool offline = AccountStore::isOffline(uuid);
    QString error;
    if (!AccountStore::activate(uuid, &error)) {
        InlineMessage::warning(m_ui->overlay(), tr("切换账号失败"), error);
        return;
    }
    // 盘上换人了，内存里那一份也得跟着换 —— 不换的话底栏和启动参数里还是上一个人的名字
    // （AuthManager 只在构造时读过盘，见 reloadStoredAccount()）。
    if (offline) {
        // 离线账号没有凭据可读（活动槽刚被它清空），名字从账号列表里来。
        // "这一趟走离线"不用另外记一笔：activate() 已经把当前账号换成它了，
        // 启动那条路读的就是这个（offlineIdentity()）。
        m_authManager->setOfflinePlayer(AccountStore::nameOf(uuid));
    } else {
        m_authManager->reloadStoredAccount();
    }
    updateBottomBar();
    appendLog(tr("已切换到账号「%1」。").arg(AccountStore::nameOf(uuid)));
    // 外观也得跟着换：新账号是另外一套皮肤和披风。这一步会把预览、披风列表、账号列表
    // 一起刷一遍。
    refreshAccountSkin();
}

void MainWindow::onAccountRemoveRequested(const QString &uuid) {
    if (uuid.isEmpty()) {
        return;
    }
    const QString name = AccountStore::nameOf(uuid);
    // 删掉的正好是**当前**账号时，活动槽也一起清空（等于退出登录）—— 这件事必须在确认框
    // 里说清楚，用户点下去才知道自己会退出登录（见 AccountStore.hpp 文件头）。
    const bool wasCurrent = AccountStore::activeUuid() == uuid;
    const QString question =
        wasCurrent ? tr("删掉当前账号「%1」？\n\n它保存的登录凭据会一并删除，删完之后就是"
                        "未登录状态（可以重新登录）。")
                         .arg(name)
                   : tr("删掉账号「%1」？\n\n它保存的登录凭据会一并删除。").arg(name);
    if (!InlineMessage::question(m_ui->overlay(), tr("删除账号"), question, tr("是"), tr("否"), true)) {
        return;
    }
    if (!AccountStore::remove(uuid)) {
        InlineMessage::warning(m_ui->overlay(), tr("删除账号失败"), tr("这个账号不在已保存的列表里。"));
        return;
    }
    if (wasCurrent) {
        m_authManager->reloadStoredAccount();
        updateBottomBar();
    }
    appendLog(tr("已删除账号「%1」。").arg(name));
    refreshAccountSkin();
}

void MainWindow::onSkinUploadRequested(const QString &path, const QString &variant) {
    // 先把文件读出来。**尺寸那条规矩只在 SkinApi::isAcceptableSkinSize() 里**（唯一出处），
    // 这里不自己再判一遍。但"读不出来"得在这里说 —— 它连尺寸都没有。
    QFile file(path);
    const QString fileName = QFileInfo(path).fileName();
    if (!file.open(QIODevice::ReadOnly)) {
        InlineMessage::warning(m_ui->overlay(), tr("上传皮肤失败"),
                             tr("读不到「%1」这个文件。").arg(fileName));
        return;
    }
    const QByteArray png = file.readAll();
    file.close();
    const QImage image(path);  // 用 QImage 再读一遍只为拿宽高（下面换的是原始字节）
    if (image.isNull()) {
        InlineMessage::warning(m_ui->overlay(), tr("上传皮肤失败"),
                             tr("「%1」不是一张能读出来的图片。").arg(fileName));
        return;
    }
    if (!SkinApi::isAcceptableSkinSize(image.width(), image.height())) {
        InlineMessage::warning(m_ui->overlay(), tr("上传皮肤失败"),
                             tr("皮肤图必须是 64×64（新格式）或者 64×32（老格式），这张是 "
                                "%1×%2。")
                                 .arg(image.width())
                                 .arg(image.height()));
        return;
    }

    QString error;
    const QString token = m_authManager->loadSavedToken(&error);
    if (token.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("上传皮肤失败"), error);
        return;
    }
    if (!m_skinApi->uploadSkin(token, png, variant, &error)) {
        InlineMessage::warning(m_ui->overlay(), tr("上传皮肤失败"), error);
        return;
    }
    appendLog(tr("皮肤已上传（模型 %1）。").arg(variant));
    // 换完立刻重拉一遍：预览要显示**刚传上去的**那一张，而不是等下次开窗。
    refreshAccountSkin();
}

void MainWindow::onCapeSelectRequested(const QString &capeId) {
    if (capeId.isEmpty()) {
        return;
    }
    QString error;
    const QString token = m_authManager->loadSavedToken(&error);
    if (token.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("更换披风失败"), error);
        return;
    }
    if (!m_skinApi->setActiveCape(token, capeId, &error)) {
        InlineMessage::warning(m_ui->overlay(), tr("更换披风失败"), error);
        return;
    }
    appendLog(tr("已更换披风。"));
    refreshAccountSkin();
}

void MainWindow::onCapeHideRequested() {
    QString error;
    const QString token = m_authManager->loadSavedToken(&error);
    if (token.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("取消披风失败"), error);
        return;
    }
    if (!m_skinApi->hideCape(token, &error)) {
        InlineMessage::warning(m_ui->overlay(), tr("取消披风失败"), error);
        return;
    }
    appendLog(tr("已取消披风（不显示）。"));
    refreshAccountSkin();
}

void MainWindow::onLoginStarted() {
    // 从这一刻到浏览器回调回来可能有几十秒（用户要输账号密码、可能还要过二次验证）。
    // 界面上要说明自己在等什么的地方是**那个窗**：按钮一起变灰、状态行写清在等什么。
    //
    // NOTE: 写在这里（信号）而不是点下去那一下（槽）：「在登录中」就只有一个来源
    //       （AuthManager 自己），以后再多一个入口起这个流程，界面照样跟得上。
    //       超时兜底也在那边 —— 浏览器再也不回来时它会发一次 loginFailed()，
    //       忙态由 onLoginFailed() 松开。
    if (m_accountSkinDialog != nullptr) {
        m_accountSkinDialog->setBusy(true, tr("正在等浏览器完成登录…"));
    }
}

void MainWindow::onLoginSucceeded(const QString &playerName) {
    // 底栏左下的用户名和头像都换成真名 —— 这是"启动器有反应"里最该看见的那一下
    updateBottomBar();
    // 外观也要跟着换成**这个账号**的：底下那行名字已经变了，右栏还画着上一个人的
    // 皮肤，是这一屏里最容易看出来"没跟上"的一处。这里走的是和开窗同一条路
    // （先默认外观兜底，再拿缓存，最后后台刷），所以离线勾着时也一样不联网。
    startPlayerSkinLoad();
    // 这次登录的凭据要**收进多账号列表**（列表里就多一条，或者更新已有的那一条）：
    // 不收的话，用户切到别的账号之后就再也回不到这一个了 —— 那是这一整块功能最要紧的
    // 一步，而不是可选的记账。
    QString accountError;
    if (!AccountStore::captureActive(&accountError)) {
        // 收不进列表**不影响这一次登录**（凭据已经在活动槽里，启动游戏照旧），所以不弹窗；
        // 但要说一声，否则用户下次开那个窗会发现"我刚登录的账号怎么不在列表里"。
        appendLog(tr("保存账号到列表失败：%1").arg(accountError));
    }
    if (m_accountSkinDialog != nullptr) {
        refreshAccountSkin();
    }
    // 用户刚在浏览器里看到过「登录成功」，这里再说一声是为了把**登录到的是哪个账户**
    // 摆出来：拿错微软账户登录（家里共用的机器上很常见）只有这句话能看出来。
    InlineMessage::information(m_ui->overlay(), tr("登录成功"), tr("已登录为「%1」。").arg(playerName));
}

void MainWindow::onLoginFailed(const QString &errorMessage) {
    if (m_accountSkinDialog != nullptr) {
        // 那个窗上也可能有一个人在等（它是从「登录新账号…」起的流程）。忙态必须在这里
        // 松开，否则弹窗关掉之后它那几个按钮一直灰着，看着像卡死了。
        m_accountSkinDialog->setBusy(false);
        m_accountSkinDialog->setStatusText(accountSkinStatusText());
    }
    // ★ 2026-09-26：登录失败还可能是"**上一个在线账号已经不在盘上了**"。
    //   登录那一趟开头会把活动槽里的 Minecraft 凭据清掉（防"旧 uuid 配新刷新令牌"的错配，
    //   见 AuthManager::exchangeAuthorizationCode），而 AuthManager 在失败那条路上已经把
    //   内存里那份身份跟着盘清了（见 handleNewConnection 里那段）。**界面得跟着画一遍**：
    //   底栏那个名字是上一次 updateBottomBar() 摆上去的，不重画就会一直停在上一个人的名字
    //   上，而底栏第二行"未登录"的判断（offlineIdentity()，读盘）与启动那句判据都说"没人"。
    //
    //   条件写全是为了**别动离线账号那一档**：那时候盘上什么都没变（离线账号没有凭据），
    //   底栏该继续写离线那个名字。而"从来没登录过 + 登录失败"也会走进来一次，那一趟是
    //   幂等的：画出来还是原来那张默认脸、占位名字。
    if (!offlineIdentity() && !m_authManager->loggedIn()) {
        updateBottomBar();
        // 外观同理：脸还是上一个人的（m_avatarPixmap 只在 applyPlayerSkin() 里换），
        // 这里走的是"未登录 ⇒ 默认外观"那一支，**不联网**（见 startPlayerSkinLoad() 里
        // !loggedIn() 那个早退）。刻意**不调** refreshAccountSkin()：那一支最后会去
        // loadSavedToken() 拉披风，而盘上此刻正躺着**新账号**那半份微软凭据 —— 一次同步的
        // 令牌刷新会把"登录失败"当场变成"其实登上了另一个人"，用户看到的弹窗就成了假话。
        startPlayerSkinLoad();
    }
    // 登录失败是**挡住启动**的错误（没有令牌就点不动「启动游戏」），按产品约定必须弹窗。
    // 正文是 AuthManager 那边给的、能直接给用户看的话（"这个微软账户还没注册过 Xbox 账户，
    // 到 xbox.com 登录一次"这种），不是一句"登录失败"。
    InlineMessage::warning(m_ui->overlay(), tr("登录失败"), errorMessage);
}

void MainWindow::onAddFolderClicked() {
    // 用目录选择对话框而不是让用户手打路径：路径里有中文、空格、全角括号（真机上的
    // 实例目录就是这样），打错一个字符得到的是一个"加进去了但一个实例都没有"的空文件夹，
    // 而界面上看不出区别。
    //
    // ★ 这一处是 2026-09-25「不出现独立窗口」那一轮之后留下来的独立窗之一，**全项目一共
    //   七处**（2026-09-27 数的；那一份带清单的说明在
    //   ui/InstanceSettingsDialog.cpp 里选 java.exe 那一处，要改数字就改那儿，别各写一份 ——
    //   这一行原来是"四处（另外三处：选整合包 / 选皮肤图 / 选 java.exe）"，数丢了一半）。
    //   它不是我们的窗 ——
    //   `QFileDialog` 不指定 `DontUseNativeDialog` 时开的是 **Windows 自己那个选择框**，
    //   和点「启动游戏」时弹出来的浏览器是同一类东西（用户没抱怨过那一个）。
    //   要它也进内容区，办法是有的：`QFileDialog` 本身就是 QDialog，`execPage()` 那一套
    //   能收，只要把 `DontUseNativeDialog` 打开（`QFileDialog` 的头文件里写着这个属性
    //   必须在构造后、设完 model 之前设才有效）。但那是拿一个**更好用的系统选择框**去换
    //   "界面上再少一个窗"—— 没做，留这段说明在这儿，将来想收就从这一处开始。
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("选择 Minecraft 游戏文件夹"), QDir::homePath());
    if (folder.isEmpty()) {
        return;  // 用户取消了，不是错误
    }
    if (!m_folderManager->addFolder(folder)) {
        InlineMessage::warning(m_ui->overlay(), tr("添加文件夹失败"),
                             tr("「%1」不是一个存在的目录。").arg(folder));
        return;
    }
    appendLog(tr("已添加文件夹：%1").arg(folder));
    // 加完直接切过去：点「添加文件夹」的意图就是要用那一份。已经在列表里时会切到它，
    // 已经是当前的则什么都不做（FolderManager 自己判）。
    m_folderManager->setCurrentFolder(folder);
}

void MainWindow::removeFolderFromList(const QString &folder) {
    if (m_folderManager->isDefaultFolder(folder)) {
        // 只有"自带的那一份"会走到这儿（正常路径下齿轮里那一项本来就是灰的），照实说明
        // 原因，不是"移除失败"这种没法处理的消息。
        InlineMessage::information(m_ui->overlay(), tr("移除文件夹"),
            tr("「%1」是启动器自带的游戏文件夹，移不掉。\n它是别的目录都被移走时唯一的退路。")
                .arg(QDir::toNativeSeparators(folder)));
        return;
    }
    // 确认框。这一步**不动盘上的任何东西**，但它正是用户最容易误会的一步（以前这个按钮
    // 叫「删除文件夹」，看到那个词的人会以为点下去几个 GB 的存档就没了）—— 所以正文里把
    // "只动列表、盘上文件不动"写死，而不是指望用户知道。
    // NOTE: 默认按钮是「是」：这一步不删东西，弹窗在这儿的作用是**说清楚**，不是拦一道。
    QString question = tr("「%1」会从上面这个列表里移除。\n\n"
                          "文件夹本身和里面的文件都不会被删掉 —— 这一步只动启动器的列表，"
                          "盘上一个字节都不动。")
                           .arg(QDir::toNativeSeparators(folder));
    if (m_folderManager->currentFolder() == folder) {
        question += tr("\n\n它是启动器当前在用的那一份，移除之后改用「%1」。")
                        .arg(QDir::toNativeSeparators(FolderManager::defaultFolder()));
    }
    const auto answer = InlineMessage::question(m_ui->overlay(), tr("从列表里移除文件夹"), question, tr("是"), tr("否"), true);
    if (!answer) {
        return;  // 取消不是错误，不写日志（这里也没有"取消了一次删除"可言）
    }
    if (!m_folderManager->removeFolder(folder)) {
        // 正常路径下走不到这儿（上面那道 isDefaultFolder 已经挡住了唯一的失败原因）。
        // 留着是防"列表变了但一个提示都没有"。
        InlineMessage::warning(m_ui->overlay(), tr("移除文件夹"),
            tr("没能把「%1」从列表里移除。").arg(QDir::toNativeSeparators(folder)));
        return;
    }
    appendLog(tr("已从列表里移除文件夹：%1").arg(folder));
}

void MainWindow::onFolderActionRequested(const QString &path, int action) {
    // 齿轮菜单里"第几项"翻译成"做哪件事"。摆在这儿（而不是让界面直接把五项各接一个
    // 回调）是因为界面只知道位置，而"第几项是刷新"这件事是和 folderActionText() 那一份
    // 顺序绑在一起的 —— 两处顺序一旦漂开，症状是"点刷新结果把文件夹删了"。
    switch (action) {
    case MainWindowUI::FolderActionRefresh:
        onFolderRefreshRequested(path);
        return;
    case MainWindowUI::FolderActionRename:
        onFolderRenameRequested(path);
        return;
    case MainWindowUI::FolderActionOpen:
        onFolderOpenRequested(path);
        return;
    case MainWindowUI::FolderActionRemove:
        removeFolderFromList(path);
        return;
    case MainWindowUI::FolderActionDelete:
        onFolderDeleteRequested(path);
        return;
    }
    // 越界到不了这儿（菜单是照 folderActionCount() 摆的）。真到了只写一行日志：
    // 这一下本来就不该有动作，为它弹个框反而像"用户做错了什么"。
    appendLog(tr("不认识的文件夹动作：%1").arg(action));
}

void MainWindow::onFolderRefreshRequested(const QString &path) {
    // 这一份现在还在不在盘上 —— 「刷新」最要紧的那件事就是它（目录被挪走、U 盘被拔掉
    // 之后列表里那一条还在，但从此扫不出任何实例）。
    const bool onDisk = QFileInfo(path).isDir();
    // 先重画列表：下面那句「（目录不在了）」就是这一下加上去的。行里的名字/路径都可能
    // 被别的地方改过（比如名字是另一处设的），一并重读。
    refreshFolderList();
    // 正好是当前在用的那一份 → 右边那张实例树也重扫一遍。用户点「刷新」要的通常正是
    // 这一下：他在别处往这个文件夹里丢了个版本，想在这儿看到它。
    if (path == m_folderManager->currentFolder()) {
        refreshInstalledVersions();
    }
    appendLog(onDisk ? tr("已刷新游戏文件夹：%1").arg(QDir::toNativeSeparators(path))
                     : tr("游戏文件夹不在盘上：%1").arg(QDir::toNativeSeparators(path)));
}

void MainWindow::onFolderRenameRequested(const QString &path) {
    // 预填的是**现在真正显示的那个名字**：没起过名字时那一格是空的，而对话框正文里
    // 会写清"现在显示的是「xxx」"—— 用户看得见自己要改的是哪个名字，也知道清空
    // 这一格就是恢复默认。
    const QString custom = m_folderManager->displayName(path);
    bool ok = false;
    const QString input = InlineMessage::getText(
        m_ui->overlay(), tr("重命名游戏文件夹"),
        tr("新名字（现在显示的是「%1」）。\n"
           "它只用在启动器的这个列表上，磁盘上的目录名一个字都不动；\n"
           "清空这一行就恢复默认的名字。")
            .arg(folderTitle(path)),
        custom, &ok);
    if (!ok) {
        return;  // 用户取消了，不是错误
    }
    const QString name = input.trimmed();
    if (name == custom) {
        // 名字没动就悄悄退出，不弹窗：用户点开、看看、点确定，这不是错误
        //（和实例改名那条路同一个取法）。
        return;
    }
    // 校验和数据层是**同一份**（FolderManager::validateDisplayName），界面绝不自己抄
    // 一遍规则 —— 抄一遍的下场是两边慢慢漂开，出现"界面放行、数据层拒绝"。
    const QString problem = FolderManager::validateDisplayName(name);
    if (!problem.isEmpty()) {
        // 不循环重问：用户看到原因后可以自己再点一次，那样他能顺便改别的决定。
        InlineMessage::warning(m_ui->overlay(), tr("重命名游戏文件夹"), problem);
        return;
    }
    if (!m_folderManager->setDisplayName(path, name)) {
        // 走得到这儿只有一种情况：菜单弹出来之后这一份被别处移掉了（列表已经不是刚才
        // 那份快照）。照实说，别静默什么都不做。
        InlineMessage::warning(m_ui->overlay(), tr("重命名游戏文件夹"),
                             tr("「%1」已经不在列表里了。").arg(QDir::toNativeSeparators(path)));
        return;
    }
    appendLog(name.isEmpty()
                  ? tr("游戏文件夹恢复默认名字：%1").arg(QDir::toNativeSeparators(path))
                  : tr("游戏文件夹改名为「%1」：%2").arg(name, QDir::toNativeSeparators(path)));
}

void MainWindow::onFolderOpenRequested(const QString &path) {
    if (!QFileInfo(path).isDir()) {
        InlineMessage::warning(m_ui->overlay(), tr("打开游戏文件夹"),
                             tr("「%1」现在不在盘上（U 盘没插、或者目录被挪走了）。")
                                 .arg(QDir::toNativeSeparators(path)));
        return;
    }
    // 走 QDesktopServices 而不是自己 QProcess 起 explorer.exe：它就是这个用途
    // （Windows 上底下就是 ShellExecute），路径里的中文、空格、全角括号一律不用管 ——
    // 自己拼命令行的话，那段引号迟早会写错一个地方，而且错法随路径变。
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        InlineMessage::warning(m_ui->overlay(), tr("打开游戏文件夹"),
                             tr("没能打开「%1」。").arg(QDir::toNativeSeparators(path)));
    }
}

void MainWindow::onFolderDeleteRequested(const QString &path) {
    if (m_folderManager->isDefaultFolder(path)) {
        // 正常路径下走不到这儿（菜单里那一项对自带那份是灰的），照实说明原因。
        InlineMessage::information(m_ui->overlay(), tr("删除游戏文件夹"),
            tr("「%1」是启动器自带的游戏文件夹，删不掉。\n它是别的目录都被移走时唯一的退路。")
                .arg(QDir::toNativeSeparators(path)));
        return;
    }
    // 闸口：有游戏在跑就不给删。删的这一刻 JVM 正拿着里面那些 jar 和 natives，删到一半
    // 失败会留下一个**半截**的目录（前面那些文件已经没了）—— 那种状态比"这次删不了"
    // 难收拾得多。等游戏关掉再删，代价只是等一会儿。
    if (m_launcherManager->hasRunningGame()) {
        InlineMessage::warning(m_ui->overlay(), tr("删除游戏文件夹"),
                             tr("现在有游戏正在运行，先关掉游戏再删。\n"
                                "删的时候游戏正开着，里面的文件会删不干净。"));
        return;
    }
    // 这一步是**不可恢复**的，所以确认框的默认按钮是「否」（和「移除」那一步正好相反：
    // 那一步不删东西，默认给「是」是为了少点一下）。
    QString question =
        tr("「%1」这个文件夹和**里面的全部文件**都会被删掉，删了恢复不了。\n\n"
           "存档、截图、自己放的模组都在这份目录里。\n"
           "只想把它从列表里拿掉、盘上的东西留着的话，用「移除」。")
            .arg(QDir::toNativeSeparators(path));
    if (m_folderManager->currentFolder() == path) {
        question += tr("\n\n它是启动器当前在用的那一份，删掉之后改用「%1」。")
                        .arg(QDir::toNativeSeparators(FolderManager::defaultFolder()));
    }
    const auto answer = InlineMessage::question(m_ui->overlay(), tr("删除游戏文件夹"), question, tr("是"), tr("否"), false);
    if (!answer) {
        return;  // 取消不是错误，不写日志
    }
    QString error;
    if (!m_folderManager->deleteFolder(path, &error)) {
        // 失败原因是由数据层给的**一整句**（可能带"删到一半"那种状态说明），原样弹出来。
        InlineMessage::critical(m_ui->overlay(), tr("删除游戏文件夹失败"), error);
        return;
    }
    appendLog(tr("已删除游戏文件夹：%1").arg(QDir::toNativeSeparators(path)));
}

// NOTE: 这里原来有一个 `onRenameInstanceClicked()` 槽（2026-09-25 删）：它接的是实例页
//       右栏那颗「重命名…」按钮，而那颗按钮没了 —— 改名现在在**实例设置那一页**里
//       （用户在对话框里改「实例名」那一格、按确定，落盘走 openInstanceSettings() 里
//       那一段：先 applyRename() 再写设置）。
//
//       这个槽里原来那两件事的去向：
//         - 「先选中一个实例」那句提示：进设置页的入口（点实例行 / 点行尾齿轮）本来就是
//           带着一个**具体的** instanceId 进来的，不存在"没选中"这一档；
//         - 「正在运行的不能改名」那道闸口：**搬进了 applyRename() 前面那一句**（见
//           openInstanceSettings() 里的注记）—— 那道守卫必须在真正动手的那条路上，
//           而不是在某个界面的入口上。

bool MainWindow::applyRename(const QString &oldId, const QString &newId) {
    // 闸口一：**正在跑的不能改名**，而且必须在调 renameInstance() **之前** —— 运行中的
    // 实例在 Windows 上 jar 被 JVM 锁着，改到一半才失败的话用户看到的是一句"拒绝访问"，
    // 而真正该说的是这一句。
    //
    // NOTE: 这一道原来在 onRenameInstanceClicked() 里（界面的入口上）。改名搬进实例设置
    //       那一页之后，入口变成了"任何一条点进设置页的路"，守卫放在入口上迟早会漏一条
    //       —— 放在这里，所有调用点（含验证工程）都绕不过去。
    if (m_launcherManager->isVersionRunning(oldId)) {
        InlineMessage::warning(m_ui->overlay(), tr("重命名实例"),
                             tr("「%1」正在运行，先关掉游戏再改名。").arg(instanceDisplayName(oldId)));
        return false;
    }

    const QString name = newId.trimmed();

    // 名字合不合法。判据和数据层是**同一份**（InstanceFiles::validateRenameTarget），
    // 界面绝不自己抄一遍规则 —— 抄一遍的下场是两边慢慢漂开，出现"界面放行、数据层拒绝"
    // 或者更糟的"界面拒绝、数据层其实能接受"。
    const QString problem = InstanceFiles::validateRenameTarget(name, oldId);
    if (!problem.isEmpty()) {
        // 不循环重问：用户看到原因后可以自己再点一次，那样他能顺便改别的决定。
        InlineMessage::warning(m_ui->overlay(), tr("重命名实例"), problem);
        return false;
    }

    // 分组归属的**老键必须在改名之前取**：改名会把老目录名连同"按目录名认人"那条
    // 退路一起改掉，改完再问它，问的是磁盘上一个已经不存在的目录。
    const QString oldKey = instanceKey(oldId);

    // 执行：改磁盘、含失败回滚，都在数据层。失败原因是给用户看的一整句
    // （可能带"回滚到什么程度"），界面原样弹出来，不再自己加工。
    QString error;
    if (!m_versionManager->renameInstance(oldId, name, &error)) {
        InlineMessage::critical(m_ui->overlay(), tr("重命名失败"), error);
        // NOTE: 失败可能是"回滚干净了"也可能是"停在半路上"。两种都不在这里重刷列表 ——
        //       刷新只会把界面对着磁盘重画一遍，而用户需要的是先看清弹窗里那句话。
        return false;
    }

    // 改完名字要跟着动的状态。漏掉下面这一条的后果很具体：用户下次打开启动器，
    // restoreLastLaunchedVersion() 拿老 id 去找人找不到，选中项是空的，他会以为
    // 刚改名的那个实例丢了。
    if (m_settings->lastLaunchedVersion() == oldId) {
        m_settings->setLastLaunchedVersion(name);
    }

    // 分组归属也要跟着挪。两件事叠在一起，所以这里必须按顺序取两次键：
    //   - 改名**之前**那个键（上面取的 oldKey）：老实例可能还没有稳定 id，
    //     那时它的键是**老目录名**；
    //   - 改名**之后**那个键：renameInstance() 刚好在这一次把稳定 id 生成出来了
    //     （见它的第 2 步），所以新键多半是那个 uuid，而不是新目录名。
    // 只取一次（无论是哪一次）都会让归属在改名的同时丢掉 —— 而丢归属的表现是
    // "改完名它自己从组里出来了"，用户会以为分组没保存上。
    const QString newKey = instanceKey(name);
    QString groupError;
    if (!m_groupStore->rekey(oldKey, newKey, &groupError)) {
        // 实例本身已经改好名了，只是分组没搬过去。**不能当成改名失败** ——
        // 那会让用户以为名字没改成功，然后再点一次。说清楚哪一半成了、哪一半没成。
        InlineMessage::warning(m_ui->overlay(), tr("分组归属没跟着搬"),
                             tr("实例已经改名为「%1」，但它原来的分组归属没能搬过去：\n%2\n\n"
                                "可以在「分组…」里重新归一次。")
                                 .arg(name, groupError));
    }

    refreshInstalledVersions();
    // 刷新时下拉框那边是"保住原来选中的那个 id"，而那个 id 已经不在了 —— 于是它
    // 会停在空选。显式选中新名字：用户刚改完，看到的应该是刚改完的那个实例。
    QComboBox *combo = m_ui->getInstalledVersionCombo();
    const int index = combo->findData(name);
    if (index >= 0) {
        combo->setCurrentIndex(index);  // 这条连接是活的，按钮文字会跟着刷新
    }
    updateStartButtonText();
    appendLog(tr("实例已重命名：%1 → %2").arg(oldId, name));
    return true;
}

void MainWindow::pinInstance(const QString &versionId) {
    // 「置顶」= 把这个实例挪进**置顶组**（用户 2026-09-25 原话：「置顶以后进选了这个的组」），
    // 同时记下"它置顶之前待在哪一组"（2026-09-26 原话：「加一个记录置顶前这玩意属于哪一组
    // 的」）。两件事一起做是 store 那一层的实现（InstanceGroupStore::pinInstance()）——
    // 分两次写的话，中间那一份文件自相矛盾，取消置顶就会给出一个不一样的答案。
    // 哪一组是置顶组由 store 回答（清单里勾了「设为置顶组」的那一组；一个都没勾就退回名字
    // 叫「置顶」的那个默认组，那是老用户的迁移路；都没有则是空串）。
    //
    // 没有置顶组 ⇒ 那一格本来就**没画**（fillInstanceTree 里那一格判的就是它）。走到这儿
    // 只可能是"树画出来之后、点下去之前"用户把那一组删了。什么都不做、也不弹窗：这一下
    // 本来就没有去处，而屏幕上那颗箭头此刻也已经不在了，屏幕底下那条"已置顶"的日志更不该
    // 写出来（store 那一层同样不写盘，见 InstanceGroupStore::pinInstance()）。
    if (m_groupStore->pinnedGroupId().isEmpty()) {
        return;
    }

    // 归属键走 instanceKey()（有稳定 id 用 id、没有用目录名）—— 读和写必须同一个键，
    // 否则存下去的那一刻就对不上了（见 InstanceGroupStore 文件头）。
    QString error;
    if (!m_groupStore->pinInstance(instanceKey(versionId), &error)) {
        InlineMessage::critical(m_ui->overlay(), tr("置顶失败"), error);
        return;
    }
    // 写成功会发 groupsChanged ⇒ 整棵树重灌（那个实例挪进置顶组、它那一行那颗上箭头消失，
    // 因为已经在里面了）。**不再手动刷一遍**：手动刷只会和信号那条路撞在一起。
    appendLog(tr("已置顶：%1").arg(instanceDisplayName(versionId)));
}

void MainWindow::unpinInstance(const QString &versionId) {
    // 「取消置顶」= 把这个实例**送回它置顶之前待的那一组**（用户 2026-09-25 原话：
    // 「置顶组里的实例悬停时右侧显示下箭头，即取消置顶」；落点由 2026-09-26 那句定死：
    // 「加一个记录置顶前这玩意属于哪一组的，用户直接塞的取消置顶才归进未分类」）。
    // 三种记录三种落点都写在 InstanceGroupStore::unpinInstance() / 那个文件头那一段里：
    // 记着组 id ⇒ 回那个组；记着"明确选了未分组"⇒ 回未分组；记着 null（置顶前压根没表过
    // 态）⇒ 把那一条归属**删掉**、让显示层按版本类型重新归类。
    //
    // NOTE: **没有记录**（用户自己用「移入分组」把它塞进置顶组的，或者这一版之前就在里面的
    //       老数据）⇒ 落未分组，和「移出分组」逐字同一个落点。这一档**不猜**它本来该归哪
    //       一组：那条信息真的不存在（从来没被记下来过），猜错的表现是"取消置顶之后它跳去
    //       了一个我没选过的组"，比"落在未分组里、用户自己再挪一次"糟得多 —— 而"用户直接
    //       塞的才归未分类"正是用户点名要的那一档。
    //
    // NOTE: 不判"它到底在不在置顶组里" —— 那一格只在 rowUnpinRole() 为真的行上画得出来，
    //       走到这儿就说明在。真要在外面，这一下也只是写一条"未分组"，无害。
    QString error;
    if (!m_groupStore->unpinInstance(instanceKey(versionId), &error)) {
        InlineMessage::critical(m_ui->overlay(), tr("取消置顶失败"), error);
        return;
    }
    // 写成功会发 groupsChanged ⇒ 整棵树重灌（那个实例回到记录里那一组、它那一行那颗箭头
    // 换回朝上的）。**不再手动刷一遍**，理由同 pinInstance()。
    appendLog(tr("已取消置顶：%1").arg(instanceDisplayName(versionId)));
}

void MainWindow::confirmDeleteInstance(const QString &versionId) {
    if (versionId.isEmpty()) {
        return;
    }
    const QString name = instanceDisplayName(versionId);

    // 闸口一：**正在跑的不能删**。放在最前面、并且必须排在真正动手之前 —— 运行中的实例在
    // Windows 上 jar 被 JVM 锁着，删到一半才失败的话用户看到的是一句"拒绝访问"，而真正
    // 该说的是这一句（和「重命名」那一条逐字同款）。
    if (m_launcherManager->isVersionRunning(versionId)) {
        InlineMessage::warning(m_ui->overlay(), tr("删除实例"),
                              tr("「%1」正在运行，先关掉游戏再删。").arg(name));
        return;
    }
    // 闸口二：**正在装 / 正在导入的也不能删**。判据用的是 `installingVersionIds()` ——
    // 「现在谁动不得」的唯一出处（底栏那颗启动按钮、下拉框都是问它）。在这里删掉的后果
    // 很具体：那一路还在往这个目录里写，写到收尾那一步会**把目录重新建出来**（json /
    // meta 那两笔），用户看到的是"删掉了、过一会儿它自己又回来了，而且是半截的"。
    if (installingVersionIds().contains(versionId)) {
        InlineMessage::warning(m_ui->overlay(), tr("删除实例"),
                              tr("「%1」正在安装，等它装完再删。").arg(name));
        return;
    }

    // 这一趟**不可逆**，所以先问一句（用户原话：「删除前需要弹窗提示」）。正文里把目录的
    // 完整路径写出来：用户要能看清删的是哪一个目录、它长在哪，而不是只知道"某个实例"。
    const auto answer = InlineMessage::question(
        m_ui->overlay(), tr("删除实例"),
        tr("要删除实例「%1」吗？\n\n"
           "会把 %2 这整个目录删掉（jar、json、以及这个实例自己的设置都在里面），"
           "删掉之后找不回来。\n\n"
           "它所在的游戏文件夹、以及别的实例都不受影响。")
            .arg(name, QDir::toNativeSeparators(AppPaths::versionDir(versionId))),
        tr("删除"), tr("取消"), false);
    if (!answer) {
        return;  // 取消不是错误，不写日志
    }
    deleteInstance(versionId);
}

bool MainWindow::deleteInstance(const QString &versionId) {
    // 真正动手的那一半（问的那一半在 confirmDeleteInstance() 里）。和「重命名」是同一个
    // 切法：验证工程直接驱动这一半，弹窗那一半它驱动不了。切在这里的另一个好处是
    // **守卫和动作在同一个函数里** —— 任何调用点（含以后新加的）都绕不过去。
    if (versionId.isEmpty()) {
        return false;
    }
    const QString name = instanceDisplayName(versionId);
    if (m_launcherManager->isVersionRunning(versionId)) {
        InlineMessage::warning(m_ui->overlay(), tr("删除实例"),
                              tr("「%1」正在运行，先关掉游戏再删。").arg(name));
        return false;
    }
    // 闸口二（和上面那一半逐字同款，理由也逐字相同）：守卫和动作留在同一个函数里，
    // 任何调用点都绕不过去 —— 包括以后直接从别处调 deleteInstance() 的那些。
    if (installingVersionIds().contains(versionId)) {
        InlineMessage::warning(m_ui->overlay(), tr("删除实例"),
                              tr("「%1」正在安装，等它装完再删。").arg(name));
        return false;
    }

    QString error;
    if (!m_versionManager->deleteInstance(versionId, &error)) {
        // 失败原因是数据层给的**一整句**（可能带"删到一半"那种状态说明），原样弹出来
        // —— 和「删除游戏文件夹」那条路一个规矩。
        InlineMessage::critical(m_ui->overlay(), tr("删除实例失败"), error);
        return false;
    }

    // 分组归属**故意不动**：wtfml.folders 里可能还留着这个实例的一条归属（它指向的组还在，
    // 所以不会被 store 的清成员规则扫掉）。留着是有意的，理由是**它的代价是零**：
    //   - 它对界面没有任何影响：归属只有在"这个实例还在"的时候才会被问到，而那张树是
    //     按**盘上的实例**建的（遍历 installedVersions()，每条再拿 instanceKey() 去问归属）
    //     ⇒ 指向已删实例的那些条目从来不会被遍历到。「组名 (N)」里的 N 也是从树上数的，
    //     不是从归属表里数的。换句话说：既没有幽灵行，也没有虚高的计数。
    //   - 为它写一条"删实例时顺手清归属"要新加一个 store 的 API（现在只有整份
    //     replaceGroups()），换来的只是少几个字节的死数据。
    //
    // ★★ 2026-09-27 修：这一段原来还写着"用户把同一个版本号重装一遍时它会回到原来那一组"
    //    ——**那是假的**（只有一半情况成立）。归属的键是 instanceKey() = `wtfml.meta` 里的
    //   稳定 id（没有才退回目录名），而 deleteInstance() 把整个目录连 `wtfml.meta` 一起删了
    //    ⇒ 重装出来的是一个**没有 id 的新目录**，键退回目录名，而那条残留的键是旧 UUID，
    //    永远不可能再对上。而那个 id 是**惰性生成**的：用户只要开过一次实例设置按过确定
    //    （ensureInstanceId()）或者改过名就会落一个 —— 所以"重装回原组"只对**从来没过
    //    拿过 id 的老实例**（键就是目录名）成立，其余的残留就是永远的死数据。
    //   ⇒ 上面那两条"代价为零"的理由照样成立，但别再拿"重装能回原组"当理由用。
    //
    // 真嫌它脏的话，下一次在「分组设置」里按确定会整份重写这个文件（那时成员表按现有的
    // 组重建，指向已删实例的那些条目照旧保留）。
    refreshInstalledVersions();
    appendLog(tr("已删除实例：%1").arg(versionId));
    return true;
}

QString MainWindow::instanceKey(const QString &versionId) const {
    // 有稳定 id 就用它，没有就退回目录名 —— 见 InstanceGroupStore 文件头那段。
    // NOTE: 退回目录名不是"临时凑合"，它是老实例（2026-09 之前建的，一个都没有稳定 id）
    //       唯一的现实身份。更要紧的是**读和写必须走同一个函数**：读的时候按目录名找、
    //       写的时候按 id 存，归属会在存下去的那一刻就对不上，而那个错不报任何东西。
    const QString id = m_versionManager->instanceId(versionId);
    return id.isEmpty() ? versionId : id;
}

void MainWindow::onGroupInstanceClicked() {
    if (!overlayAcceptsNewPage()) {
        return;
    }
    // 每个组里有多少个实例：从**当前那张树**上数，不重新扫盘。它就是用户此刻看到的
    // 东西，而对话框拿它显示「组名 (N)」和"删这个组会有多少实例变成未分组"——
    // 两处都该和屏幕上的数字一致。
    //
    // NOTE: 一个实例都没有的组**不在树上**（用户要的"空白的分组隐藏"）⇒ 这里数出来是 0，
    //       那也正是实话；它在对话框里照样列着、照样能改名排序，只是括号里写着 0。
    QHash<QString, int> counts;
    const QTreeWidget *tree = m_ui->getInstanceList();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *item = tree->topLevelItem(i);
        const QVariant groupId = item->data(0, MainWindowUI::groupIdRole());
        if (groupId.isValid()) {
            counts.insert(groupId.toString(), item->childCount());
        }
    }

    // 「恢复默认分组设置」之后每个组里会有几个实例：恢复 = 用户自己建的组全没了 ⇒ 那些
    // 实例回到"没表过态"，于是按版本类型自动归类（defaultGroupIdFor()）—— 落点只有这里
    // 算得出来，而对话框要在那一句话里说清"是不是真的一个实例都不删"。
    //
    // 取的实例集合和上面那张树是同一份（在装的那几个不算）：两处数的要是同一批东西，
    // 不然"恢复之后原版有 3 个"和屏幕上看到的数字对不上。
    const QList<InstanceGroupStore::Group> defaults = InstanceGroupStore::defaultGroups();
    QSet<QString> defaultIds;
    for (const InstanceGroupStore::Group &group : defaults) {
        defaultIds.insert(group.id);
    }
    const QSet<QString> installing = installingVersionIds();
    QHash<QString, int> defaultCounts;
    for (const QString &id : m_versionManager->installedVersions()) {
        if (installing.contains(id)) {
            continue;
        }
        const QString groupId =
            defaultGroupIdFor(m_versionManager->displayParts(id), m_versionManager, defaultIds);
        defaultCounts[groupId] = defaultCounts.value(groupId) + 1;
    }

    // NOTE: 这张窗**不读选中的实例**（用户原话：「不再是针对单个实例的设置」）——
    //       一个实例都没选也开得出来，那是"先建几个组、再往里放东西"的正常顺序。
    //       把实例放进某个组是组行行尾那颗齿轮的事（见 onGroupActionRequested()）。
    GroupDialog dialog(toDialogGroups(m_groupStore->groups()), counts, toDialogGroups(defaults),
                       defaultCounts, this);
    // 整页盖在内容区上（见 OverlayHost::execPage）。这一页自己没有标题行，页眉那句
    // 就是它唯一一处标题（和它自己的窗口标题同一个出处）。
    if (m_ui->overlay()->execPage(&dialog, GroupDialog::windowTitle()) != QDialog::Accepted) {
        // 取消 / 返回箭头：磁盘一个字节都没动（对话框只改自己手里那份工作副本）。
        return;
    }
    applyGroupList(&dialog);
}

bool MainWindow::applyGroupList(const GroupDialog *dialog) {
    if (dialog == nullptr) {
        return false;
    }
    const QList<InstanceGroupStore::Group> wanted = toStoreGroups(dialog->groups());

    // 变了才写：每点一次「确定」都重写一遍文件（连带刷新 mtime）而其实什么都没改，是
    // 最常见的一次操作。比较是**逐字段**的（连"展开还是收着"那一格一起比）—— 用户完全
    // 可能只动了那一格，而它一样要留住。
    if (wanted == m_groupStore->groups()) {
        return true;
    }
    QString error;
    if (!m_groupStore->replaceGroups(wanted, &error)) {
        InlineMessage::critical(m_ui->overlay(), tr("分组保存失败"), error);
        return false;
    }
    // NOTE: 这里**不**手动重灌那张树。这条写路径成功之后数据层会发 groupsChanged，
    //       界面接的是同一个刷新函数（见 setupConnections）—— 在这里再来一次就是
    //       同一件事做两遍，而且"哪些路径会刷新"会变成两处各自维护的约定。
    return true;
}

void MainWindow::onGroupActionRequested(const QString &groupId, int action) {
    // 这一组此刻的样子（名字、实例数、展开状态）。**动作菜单里每一格都用的同一份**：
    // 菜单是照快照摆的，而这里要的是"现在"—— 菜单开着的时候别处可能已经把它删了，
    // 那几种情况一律当"什么都没发生"（点了个过期的菜单项不该弹一个用户看不懂的框）。
    MainWindowUI::GroupRow row;
    if (!groupRowFor(m_ui, m_groupStore, groupId, &row)) {
        return;
    }

    switch (action) {
    case MainWindowUI::GroupActionMoveInto: {
        // "把选中的实例移进哪一组"：列的是**全部**分组（含树上一个实例都没有、藏起来的
        // 那些）—— 那是把实例放进一个空组的唯一入口（见 MainWindowUI::openGroupPicker()）。
        const QString selectedId = selectedVersionId(m_ui->getInstalledVersionCombo());
        if (selectedId.isEmpty()) {
            InlineMessage::information(m_ui->overlay(), tr("移入分组"), tr("先选中一个实例。"));
            return;
        }
        // 弹在**原来那颗齿轮**下面（用户看到的是"刚才那块框换了个内容"）。那一行理论上
        // 一定还在（菜单就是从它那儿弹出来的）；万一不在了就退回光标那里，框照样出得来，
        // 不会神秘消失。
        QRect anchor = gearAnchorFor(m_ui, groupId);
        if (anchor.isEmpty()) {
            anchor = QRect(QCursor::pos(), QSize(1, 1));
        }
        // 灰哪一格按那个实例的**老家**判（不是"它那一条画在哪个组下面"—— 置顶着的有两行，
        // 点哪一行会给出不同答案，理由见 groupRowsForMenu() 那条 NOTE）。
        const QString selectedKey = instanceKey(selectedId);
        m_ui->openGroupPicker(
            groupRowsForMenu(m_ui, m_groupStore, selectedKey,
                             homeGroupIdOf(m_groupStore, selectedKey,
                                           m_versionManager->displayParts(selectedId),
                                           m_versionManager, knownGroupIdsOf(m_groupStore))),
            anchor);
        return;
    }
    case MainWindowUI::GroupActionClearMembers:
        clearGroupMembers(groupId, row.name);
        return;
    case MainWindowUI::GroupActionRename: {
        // 「问」和「做」分开（和实例页那颗「重命名…」同一个拆法）：做的那一半
        // renameGroupTo() 能在验证工程里直接驱动。
        //
        // NOTE: 原来这半句还写着"因为 QInputDialog 在 offscreen 下驱动不了" —— 2026-09-25
        //       那一档换成 InlineMessage::getText() 之后，**这个理由不成立了**：问名字的那一格
        //       现在就是一个能按 objectName 找得到的 QLineEdit（见 InlineMessageBox::
        //       setLineEdit），验证工程可以真的把字打进去再点确定。拆成两步照旧有它的道理
        //       （"做"那一半不用开界面就能验），但不再是"因为没法驱动"。
        bool ok = false;
        const QString name =
            InlineMessage::getText(m_ui->overlay(), tr("重命名分组"), tr("组名："), row.name, &ok);
        if (ok) {
            renameGroupTo(groupId, name);
        }
        return;
    }
    case MainWindowUI::GroupActionToggleExpand: {
        // 先摆树里那一行，再记 —— rememberGroupExpanded() 只管写盘（见它的 NOTE），
        // 所以"这一下要让用户看见什么"是调用方的事。两处管的是同一格，摆完就是它。
        //
        // NOTE: 状态从**那一行自己**读（`!item->isExpanded()`）而不是拿菜单那份快照里的
        //       `row.expanded`：菜单是弹出来的那一刻拍的，中间用户完全可能又点了一下组头
        //       （那一下也会落盘）。和版本树/加载器树那几处"先算再做"同一个教训。
        QTreeWidget *tree = m_ui->getInstanceList();
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = tree->topLevelItem(i);
            if (item->data(0, MainWindowUI::groupIdRole()).toString() != groupId) {
                continue;
            }
            const bool expand = !item->isExpanded();
            item->setExpanded(expand);
            rememberGroupExpanded(groupId, expand);
            return;
        }
        // 那一行不在树上（这一组空了 —— 空组是藏着的 —— 或者刚被别处删了）：没有"看得见的
        // 那一行"要摆，把状态记下来就好。
        rememberGroupExpanded(groupId, !row.expanded);
        return;
    }
    case MainWindowUI::GroupActionDelete: {
        // 先问再删，而且说清**实例不会被删**：用户看到"删除"两个字时的第一反应就是怕
        // 连里面的东西一起没了，而这里确实不会（和对话框里那句一模一样）。
        const bool answer = InlineMessage::question(m_ui->overlay(), tr("删除分组"),
            tr("「%1」会被删除，组里的 %2 个实例不会被删掉，它们只是变成未分组。\n\n"
               "确定要删除吗？")
                .arg(row.name)
                .arg(row.memberCount), tr("是"), tr("否"), false);
        if (answer) {
            deleteGroupById(groupId);
        }
        return;
    }
    }
    // 越界（理论上到不了：菜单是照 MainWindowUI::groupActionCount() 循环摆的）。
}

void MainWindow::clearGroupMembers(const QString &groupId, const QString &groupName) {
    QTreeWidget *tree = m_ui->getInstanceList();
    QTreeWidgetItem *groupItem = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        if (item->data(0, MainWindowUI::groupIdRole()).toString() == groupId) {
            groupItem = item;
            break;
        }
    }
    if (groupItem == nullptr) {
        return;  // 藏起来的空组（或者刚被删了）：没有可移的东西
    }
    QStringList keys;
    for (int i = 0; i < groupItem->childCount(); ++i) {
        const QString versionId = instanceIdOf(groupItem->child(i));
        if (!versionId.isEmpty()) {
            keys.append(instanceKey(versionId));
        }
    }
    if (keys.isEmpty()) {
        return;
    }

    // 「移出分组」对**没表过态**的那些实例也得写一条：不写的话它们下一次刷新又被自动归类
    // 拉回这一组来（写下去的是"明确选了未分组"那个保留值，见 InstanceGroupStore::
    // ungroupedMarker()）—— 表现就是"点了移出，可它们还在里面"。
    QString error;
    int moved = 0;
    {
        // 挡信号：下面每写一条都会发一次 groupsChanged，而界面接的那条路是**重灌整棵树**
        // （连带重扫一遍盘）。不挡的话 N 个实例就是 N 次全盘重扫，而真机上实例可以有 61 个。
        // 写完自己刷一次就够 —— 和 seedDefaultGroups() 那一段同一个路数。
        const QSignalBlocker blocker(m_groupStore);
        for (const QString &key : keys) {
            if (!m_groupStore->assign(key, QString(), &error)) {
                break;
            }
            ++moved;
        }
    }
    refreshInstalledVersions();
    if (moved < keys.size()) {
        // 半路写失败了。**已经写下去的那些是落盘了的**（说不说它都变了），所以只说清
        // 哪几个没成，别让用户以为整件事都没发生。
        InlineMessage::critical(m_ui->overlay(), tr("移出分组失败"),
            tr("「%1」里还有 %2 个实例没能移出来：\n%3").arg(groupName).arg(keys.size() - moved).arg(error));
        return;
    }
    appendLog(tr("已把「%1」里的 %2 个实例移出分组").arg(groupName).arg(moved));
}

bool MainWindow::setSelectedInstanceHomeGroup(const QString &groupId) {
    // 取值口径和另外两颗实例级按钮一样：**下拉框**说了算（它就是"当前选中的那个实例"），
    // 不是树的 currentItem()（点一下组头它也会变）。
    return setInstanceHomeGroup(selectedVersionId(m_ui->getInstalledVersionCombo()), groupId);
}

bool MainWindow::setInstanceHomeGroup(const QString &versionId, const QString &groupId) {
    if (versionId.isEmpty()) {
        // 挑目标那个框只在"有选中的实例"时才点得动（groupActionEnabled 那一条），
        // 而菜单开着的时候那个实例可能被删掉了 —— 真走到这儿就说一句，别静默。
        InlineMessage::information(m_ui->overlay(), tr("移入分组"), tr("先选中一个实例。"));
        return false;
    }
    const QString key = instanceKey(versionId);
    // 已经在这一组里了（挑目标那个框里那一格本来就灰着，这儿是给别的入口的保险）。
    //
    // ★ 置顶着的那一档**不在这里返回**：它的老家和 members 指着的是两个组，而"盘上已经是
    //   什么样"这件事得由 store 那一层按老家判（下面那句 setInstanceHomeGroup() 自己做）。
    //   在这里拿 groupIdOf() 当代替的话，"把它移进它老家那一组"会被判成"已经做好了"而
    //   直接返回 —— 看着像没事，其实是要写的那一格没写。
    //
    // NOTE: 目标非空时"没表过态"和"明确选了它"要分开看 —— 光比"存的是不是它"会把
    //       "明确选了它"当成已经做好了；目标为空（未分组）时**一律写**：那条保留值
    //       才是"别再自动归类"的意思，而一个从来没表过态的实例正需要它（不写的话它
    //       下一次刷新又被拉回默认组去，表现就是"选了未分组但没生效"）。
    if (!groupId.isEmpty() && !m_groupStore->isPinned(key) &&
        m_groupStore->groupIdOf(key) == groupId) {
        return true;
    }

    QString error;
    // ★ 走 setInstanceHomeGroup() 而不是 assign()：置顶着的实例这一档**只改老家那一格**，
    //   members 继续指着置顶组（那是在不在置顶组里的判据）。理由（以及"走 assign 会把老家
    //   一起丢掉"那两条）写在 InstanceGroupStore::setInstanceHomeGroup() 上面。没置顶的
    //   实例它内部就是原样委托给 assign()，一个字都没变。
    if (!m_groupStore->setInstanceHomeGroup(key, groupId, &error)) {
        InlineMessage::critical(m_ui->overlay(), tr("移入分组失败"), error);
        return false;
    }
    QString destination = MainWindowUI::ungroupedGroupText();
    for (const InstanceGroupStore::Group &group : m_groupStore->groups()) {
        if (group.id == groupId) {
            destination = group.name;
            break;
        }
    }
    appendLog(tr("实例已分组：%1 → %2").arg(key, destination));
    // NOTE: 这里**不**手动重灌那张树 —— 写成功之后数据层会发 groupsChanged（见上面那段）。
    return true;
}

bool MainWindow::renameGroupTo(const QString &groupId, const QString &newName) {
    QList<InstanceGroupStore::Group> groups = m_groupStore->groups();
    int row = -1;
    for (int i = 0; i < groups.size(); ++i) {
        if (groups.at(i).id == groupId) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        return false;  // 这一组已经被别处删掉了：什么都不做，也不弹窗（不是用户此刻做的事）
    }

    const QString name = newName.trimmed();
    // 两条规矩和 GroupDialog 里那一份**逐字一样**（名字是用户认这个组的东西，两个入口
    // 得用同一把尺子）：空名字和重名都不收。那份校验在 ui/ 里，而 ui/ 不 include src/
    // 是反过来也成立的（那边给的是 main 这一侧拿不到的类型），所以这两句是各写一份的
    // —— 改了这边记得连那边一起改。
    if (name.isEmpty()) {
        InlineMessage::warning(m_ui->overlay(), tr("重命名分组失败"), tr("组名不能是空的。"));
        return false;
    }
    if (name.size() > GroupDialog::maxGroupNameLength()) {
        InlineMessage::warning(m_ui->overlay(), tr("重命名分组失败"),
                             tr("组名最多 %1 个字，现在有 %2 个。")
                                 .arg(GroupDialog::maxGroupNameLength())
                                 .arg(name.size()));
        return false;
    }
    for (const InstanceGroupStore::Group &group : groups) {
        if (group.id != groupId && group.name.compare(name, Qt::CaseInsensitive) == 0) {
            InlineMessage::warning(m_ui->overlay(), tr("重命名分组失败"),
                                 tr("已经有一个叫「%1」的组了。").arg(group.name));
            return false;
        }
    }
    if (groups.at(row).name == name) {
        return true;  // 改成一模一样的名字：不写盘
    }

    // 组改名**不动归属** —— 归属挂的是组的 id，不是名字（见 InstanceGroupStore::Group）。
    groups[row].name = name;
    QString error;
    if (!m_groupStore->replaceGroups(groups, &error)) {
        InlineMessage::critical(m_ui->overlay(), tr("重命名分组失败"), error);
        return false;
    }
    return true;
}

bool MainWindow::deleteGroupById(const QString &groupId) {
    QList<InstanceGroupStore::Group> groups = m_groupStore->groups();
    QString name;
    for (int i = 0; i < groups.size(); ++i) {
        if (groups.at(i).id == groupId) {
            name = groups.at(i).name;
            groups.removeAt(i);
            break;
        }
    }
    if (name.isEmpty()) {
        return false;  // 找不到（已经被别处删了）
    }

    QString error;
    if (!m_groupStore->replaceGroups(groups, &error)) {
        InlineMessage::critical(m_ui->overlay(), tr("删除分组失败"), error);
        return false;
    }
    // 组里的实例**一个都不删**：它们指向的组没了，那些归属按 replaceGroups() 的规矩一起
    // 被清掉 —— 于是回到"没表过态"，下一次刷新按版本类型自动归类。和对话框里删一个组
    // 走的是同一条路、同一个说法。
    appendLog(tr("分组已删除：%1").arg(name));
    return true;
}

void MainWindow::rememberGroupExpanded(const QString &groupId, bool expanded) {
    QString error;
    // NOTE: 写盘但**不发 groupsChanged**（理由见 InstanceGroupStore::setGroupExpanded()）——
    //       所以那一行的展开状态由调用方自己摆好，这里只管记。
    if (!m_groupStore->setGroupExpanded(groupId, expanded, &error)) {
        // 只记一句日志、不弹窗：这一下**已经生效了**（树上那一格就是刚摆的），没留住的
        // 只是"下次打开还照这样"——它挡不住用户做任何事，而每点一下弹一个框反倒是把用户
        // 从手上的事里打断（产品约束管的是"会挡住操作"的那一类错误）。
        appendLog(error);
    }
}

void MainWindow::openGroupMenuFor(QTreeWidgetItem *item) {
    if (item == nullptr || !item->data(0, MainWindowUI::rowGroupGearRole()).toBool()) {
        return;
    }
    const QString groupId = item->data(0, MainWindowUI::groupIdRole()).toString();
    // 快照里带上"此刻有没有选中的实例、它在不在这组里"：菜单第一项能不能点、挑目标那个
    // 框里哪一格灰着，都由它决定。
    MainWindowUI::GroupRow row;
    const QString selectedId = selectedVersionId(m_ui->getInstalledVersionCombo());
    const QString selectedKey = instanceKey(selectedId);
    if (!groupRowFor(m_ui, m_groupStore, groupId, &row, selectedKey,
                     homeGroupIdOf(m_groupStore, selectedKey,
                                   m_versionManager->displayParts(selectedId),
                                   m_versionManager, knownGroupIdsOf(m_groupStore)))) {
        return;  // 那一组已经不在了（菜单还没来得及弹它就被删了）
    }
    // 锚点 = 那一行行尾那颗齿轮占的格子（几何的唯一出处见 InstanceGroupDelegate::gearRect()）。
    m_ui->openGroupMenu(row, gearAnchorFor(m_ui, groupId));
}

void MainWindow::openInstanceSettings(const QString &versionId, const QString &pageKey) {
    if (versionId.isEmpty()) {
        InlineMessage::information(m_ui->overlay(), tr("实例设置"), tr("先选中一个实例。"));
        return;
    }
    if (!overlayAcceptsNewPage()) {
        return;
    }

    // 已经写了的那部分覆盖从 meta 里读回来当起点，「当前全局是哪个值」由 global 提供。
    // 读归读，**这里一个字节都不写** —— 写盘只发生在下面那两条接线里（2026-09-26 起这一页
    // 没有「确定」，改一格就写一格）。
    const JavaSettingsOverride current =
        javaOverrideFromJson(m_versionManager->versionMeta(versionId));
    // 第二个参数是**目录名**（改名要改的就是它），第一个是显示名（页眉上那行字）——
    // 两个值可以不一样（自定义名、拼进去的加载器名），页面上是分开的两行。
    InstanceSettingsDialog::GlobalValues global = toDialogGlobal(m_javaSettings->effective());
    // 「最大内存」那一格能拖到哪儿 = **设置页那一格能拖到哪儿**，取的是设置页那个数字框现在的
    // 上限（开窗时 applyMemoryLimits 已经按这台机器的物理内存收过口，见 refreshMemoryUi）。
    // 不在这儿另读一次系统内存，是为了两页**逐字同一个数**：用户刚在设置页看见"最多 16256"，
    // 切到实例页又是另一个上限的话，两页各说各的。
    global.memoryCapMb = m_ui->getMemoryMaxSpin()->maximum();
    InstanceSettingsDialog dialog(instanceDisplayName(versionId), versionId,
                                  toDialogOverride(current), global,
                                  m_versionManager->versionJvmArgs(versionId),
                                  !m_versionManager->skipJavaVersionWarning(versionId), this);
    // ---------- 概览栏那四行的初值（2026-09-26）----------
    //
    // 三张图标**由这里解好递进去**：ui/ 不做"这个实例默认该长什么样"那个判断（它要读版本
    // json 的 type 和加载器名，是业务层的事），也够不着实例目录下那份 icon.png 的名字
    // （AppPaths 在 src/ 里）。这一层解好、那一层只管画。
    const VersionDisplayParts parts = m_versionManager->displayParts(versionId);
    const int previewSize = InstanceSettingsDialog::iconPreviewSize();
    QPixmap localIcon;
    if (parts.icon == InstanceSettingsDialog::localIconValue()) {
        localIcon = VersionIcons::fileMark(AppPaths::versionIconPath(versionId), previewSize);
    }
    dialog.setIcon(parts.icon, defaultInstanceMark(parts, m_versionManager, previewSize), localIcon);

    // 「分组…」那一格：候选项是**现在所有的组**（含空组 —— 「往空组里放第一个实例」正是用户
    // 会干的事，那是它唯一的入口之一），当前值是那个实例的**老家**。
    //
    // NOTE: 走 homeGroupIdOf() 而**不是** displayGroupIdOf()。对没置顶的实例两者逐字相同；
    //       对置顶着的那一个，displayGroupIdOf() 答的是置顶组（盘上 members 指着它），而
    //       用户在这一格里说的永远是"它本来属于哪儿" —— 摆上置顶组的话，用户点开看见
    //       「置顶」被灰着、还以为是这一格出了问题，同时"想给它换个老家"这件事**做不了**
    //       （选了别的组又会被 applyInstanceSettings 当成"老家改了"，而它本来就是"老家改了"，
    //       只是起点显示错了）。
    const QSet<QString> knownGroupIds = knownGroupIdsOf(m_groupStore);
    QList<QPair<QString, QString>> groupChoices;
    for (const InstanceGroupStore::Group &group : m_groupStore->groups()) {
        groupChoices.append(qMakePair(group.name, group.id));
    }
    const QString key = instanceKey(versionId);
    const QString groupAtOpen = homeGroupIdOf(m_groupStore, key, parts, m_versionManager,
                                              knownGroupIds);
    dialog.setGroupChoices(groupChoices, groupAtOpen);

    // 置顶那一行：判据是"它此刻在不在置顶组里"（isPinned()，不是"老家等于置顶组"——
    // 两边都显示之后那两句话不再等值：老家的的确确可以和置顶组是同一个组，而那时它
    // 也照样是置顶着的）。没有置顶组（用户把它删了）时恒假，而且那一格会灰掉
    // （见 setPinned()）。
    const QString pinnedGroupId = m_groupStore->pinnedGroupId();
    dialog.setPinned(m_groupStore->isPinned(key), pinnedGroupId);

    // ---------- 「文件管理」那八页的目录与可用性（2026-09-26）----------
    //
    // **所有路径都由这一层算好递进去**：那几个目录名是 MC 的约定，出处是
    // ResourceCatalog::folderForKind()（见 instanceFileDirs()），ui/ 不抄第二份。
    //
    // ★ 收成一个 lambda 而不是直接写四句：**改完名要整份重算**（下面那条改名线里调它）。
    //   改名会把整个实例换到另一个目录，那八条路径、那两页的可用性、"这九颗按钮开的是
    //   哪儿"全都跟着换 —— 不重算的话，用户改完名点「模组」，看的是那个**已经不在了**
    //   的老目录；再点一下「打开文件夹」，`mkpath()` 会把老目录重新建出来（一个只有几个
    //   空文件夹的空壳实例）。这是改名和"实时写"凑到一起才会露出来的一类坑。
    //
    // NOTE: 它按**引用**收 targetId（而不是收一个 QString）：改名之后那个值变了，重算
    //       必须看新值。下面那些 lambda 也是同一个理由全都按引用捕获它。
    QString targetId = versionId;
    const auto refreshInstanceDirs = [this, &dialog](const QString &id) {
        dialog.setInstanceDir(AppPaths::versionDir(id));
        dialog.setFileDirs(instanceFileDirs(id));
        // 左栏那一列是**按实例现算**的（2026-09-27）：这几页要"用得上"才列出来。四个判据
        // 全在这一层 —— loadername / mods 目录 / 装没装那个 mod / 游戏版本够不够新，ui/
        // 一样都够不着。
        //
        // ★ 四个 setter 的**默认都是"可用"**（直接 new 出来的对话框七条全列）：忘了调哪一个，
        //   那个实例上就会多列出一页来 —— 而多一页是"用户点进去看见一句用不上"，不是静默
        //   的错。所以宁可这样默认，也不要反过来（默认不可用 = 忘了调就把一整页藏了）。
        //
        // NOTE: 「模组」的判据是 instanceModsUsable()（有模组加载器 **或** mods/ 里有 jar）
        //       ⇒ **OptiFine 也算**，装 OptiFine 的实例会多一条「模组」。这是有意的：
        //       OptiFine 从 1.13 起就是塞进 mods/ 的，那一页在那样的实例上真的用得上。
        //       ★ 2026-09-27：原来这里写的是 `displayParts().loaderName` 非空，而那一格
        //       只有我们自己的安装器会写 ⇒ 用户盘上 70 个实例里 67 个（全是 PCL 装的）
        //       被误判成"没有模组加载器"。判据与理由见 instanceModsUsable() 那段说明。
        dialog.setModUsable(instanceModsUsable(id));
        dialog.setShaderUsable(instanceShaderUsable(id));
        dialog.setSchematicUsable(instanceHasMod(id, QStringLiteral("litematica")));
        dialog.setDatapackUsable(m_versionManager->instanceSupportsDatapacks(id));
        loadServerList(&dialog, id);
        // 那几页里那些资源文件（模组 / 资源包 / 数据包 / 光影包）的图标（2026-09-27）：
        // 按文件内容反查，**在后台线程上跑**，回来了一个一个换上去（见那一支）。
        // 摆在这儿而不是摆在开窗那一刻：改名会让整个实例换一个目录，那批路径跟着全换，
        // 图标也得按新路径重认一遍（旧的那一页那时还画着老目录那些条的图）。
        requestInstanceFileIcons(&dialog, id);
    };
    refreshInstanceDirs(targetId);

    // 用户自己勾掉的那几页（2026-09-27）——**必须排在 refreshInstanceDirs() 之后**：
    // 灌进来的那份表是"藏起来的页"，而"这一条到底露不露"要拿"可不可用"一起算（见
    // InstanceSettingsDialog::shownEntryKeys()）。反过来的话，第一遍重建会先把不可用
    // 那几条全列出来、再被这一发推回去 —— 中间那一下会顺手把右栏换到某页上
    // （rebuildEntryList() 里那个"当前页没了就回第一页"）。
    dialog.setPageChoices(
        m_versionManager->versionMeta(targetId).value(VersionManager::settingsPagesField())
            .toObject());

    // 两条**动作**信号（2026-09-26）：这一页里那两颗按钮（导出启动脚本 / 补全文件）不自己
    // 干活 —— ui/ 那边不 include src/、也不碰磁盘，"这一次到底执行什么"只有这一层知道
    // （authManager / versionManager / launcherManager 都在这儿）。
    //
    // 接在 execPage **之前**：那一下会阻塞在一个嵌套事件循环里（用户在页面上点按钮时，
    // 这一句还没返回），接晚了就一辈子收不到。
    //
    // NOTE: dialog 是**栈上的局部对象**，所以这几条 connect 用 lambda 捕获它和 targetId ——
    //       这里没有 m_instanceSettingsDialog 那种成员（也刻意不加：加了就得管生命周期，
    //       而这一页每次都是新开一个）。
    connect(&dialog, &InstanceSettingsDialog::exportLaunchScriptRequested, this,
            [this, &targetId](const QString &path) { exportLaunchScript(targetId, path); });
    connect(&dialog, &InstanceSettingsDialog::repairFilesRequested, this,
            [this, &targetId]() { repairInstanceFiles(targetId); });
    // 第三条同类：概览那一栏的「导出…」（2026-09-26 从灰占位换成真的）。**和上面第一条
    // 不是一件事** —— 那个出的是 .bat，这个出的是整合包。
    connect(&dialog, &InstanceSettingsDialog::exportModpackRequested, this,
            [this, &targetId](const QString &path) { exportModpack(targetId, path); });

    // 第四条同类：概览那一栏的「检查更新」（2026-09-28，用户原话「实例设置那里做一个更新
    // 检测」）。**结果是一个弹框**（Prompt 那一档），所以这一发是**异步**的：反查要读几十兆
    // jar（后台线程），每个项目还要各问一发版本列表 —— 那几发回来的时候这一页可能还开着
    // （正常），也可能已经被用户收掉了（那时候 `m_ui->overlay()` 上摆着的是别的页面）。
    //
    // NOTE: 收页了也**照样弹**（不判断这一页还在不在）：用户按下这一颗按钮之后，那个框
    //       就是他等的那个结果 —— 把它吞掉等于"按了没反应"。这一条和文件管理那几处
    //       "动完盘得刷新那一页"不一样：那些是**改这一页的样子**，这一件是把结果**告诉人**。
    connect(&dialog, &InstanceSettingsDialog::checkUpdatesRequested, this,
            [this, &targetId]() { checkInstanceUpdates(targetId); });

    // 「文件管理」那三发（2026-09-26）：开文件夹 / 启用·禁用一条 / 删一条。
    //
    // NOTE: 这两条**要多捕获一个 &dialog** —— 动完盘得让那几页重列一遍（不然删掉的那一行
    //       还在屏幕上，用户会以为没删掉）。dialog 是栈上的局部对象、这一句又写在
    //       execPage 之前，所以引用一直活着；那几发也只在 execPage 期间可能发生。
    connect(&dialog, &InstanceSettingsDialog::openInstanceFolderRequested, this,
            [this](const QString &path) {
                if (path.isEmpty()) {
                    return;
                }
                // 先建出来再开：新实例的 saves/ screenshots/ 多半还不存在，而这九颗按钮
                // 就是给"我要往里面放东西"用的 —— 在这儿弹一句"这个文件夹不在盘上"
                // 正好挡住用户要做的事。建一个空目录也正是游戏第一次跑起来会做的事。
                QDir().mkpath(path);
                onFolderOpenRequested(path);
            });
    connect(&dialog, &InstanceSettingsDialog::toggleInstanceFileRequested, this,
            [this, &dialog](const QString &path) {
                if (toggleInstanceFile(path)) {
                    dialog.refreshFilePages();
                }
            });
    connect(&dialog, &InstanceSettingsDialog::deleteInstanceFileRequested, this,
            [this, &dialog](const QString &path) {
                if (deleteInstanceFile(path)) {
                    dialog.refreshFilePages();
                }
            });

    // ---------- 「配置文件」那一页（2026-09-27）----------
    //
    // ★ 这一页和别的几页不是一回事：点一行**不是"重列一遍"**，而是"读那个文件、把它摆进
    //   编辑器"。编辑器和那张列表是**同一页里的两格**（开不出第二个整页覆盖层 —— 见
    //   InstanceSettingsDialog::configFilePage() 上面那一段），所以这一趟没有"开页 / 关页"，
    //   只有"读进来灌进去"和"切回第一格"。
    //
    // NOTE: `configFilePage()` 是**构造时就建好、一直留着**的那一个（那把 NOTE 说了不要
    //       把它当一次性的）—— 所以这三条 connect 只连这一次，之后每开一个文件都复用。
    connect(&dialog, &InstanceSettingsDialog::openConfigFileRequested, this,
            [this, &dialog](const QString &path) { openConfigFile(&dialog, path); });
    if (ConfigFileEditorPage *editor = dialog.configFilePage()) {
        connect(editor, &ConfigFileEditorPage::saveRequested, this,
                [this, &dialog](const QString &path, const QByteArray &bytes) {
                    saveConfigFile(&dialog, path, bytes);
                });
        // 编辑器给的是**文件**路径，开文件夹要的是它所在的那个目录。
        //
        // NOTE: 这儿**不 mkpath**（不像上面那九颗快捷方式按钮）：那个目录一定是存在的
        //       ——这一格能开着，说明刚从这个目录底下读出来过一个文件。
        connect(editor, &ConfigFileEditorPage::openFolderRequested, this,
                [this](const QString &path) {
                    const QString dir = QFileInfo(path).absolutePath();
                    if (!dir.isEmpty()) {
                        onFolderOpenRequested(dir);
                    }
                });
        // ★ 「还原上一次」：读备份是读盘（见 ConfigFileEditorPage.hpp 那段"这一层不碰盘"），
        //   所以那一格只发"用户要这个文件的上一次"，取和摆在这儿做。
        connect(editor, &ConfigFileEditorPage::restoreRequested, this,
                [this, &dialog](const QString &path) { restoreConfigFile(&dialog, path); });
        connect(editor, &ConfigFileEditorPage::backRequested, this,
                [this, &dialog]() { leaveConfigEditor(&dialog); });
    }
    // ★ 顶上那颗返回箭头（覆盖层页眉上那一颗）走的是 `dialog->reject()`，点一下关掉的是
    //   **整个「实例设置」** —— 而编辑器那一格里可能正压着改了一半的东西。对话框把那一关
    //   挡下来了（见它那条 reject()）：这一发就是"挡下来了、你说怎么办"。
    //
    // NOTE: 用户说"丢掉"之后要用 `done(QDialog::Rejected)` 而不是 `reject()` —— 后者会
    //       又走回对话框那条 override 上，于是又发一次这一发（问不完的问）。
    connect(&dialog, &InstanceSettingsDialog::closeBlockedByUnsavedConfigFile, this,
            [this, &dialog]() {
                ConfigFileEditorPage *editor = dialog.configFilePage();
                if (editor == nullptr || !confirmDiscardConfigEdits()) {
                    return;  // 留在这儿：页照旧开着，编辑器那一格原样。
                }
                editor->discardChanges();
                dialog.done(QDialog::Rejected);
            });
    // 服务器列表那一页的「刷新」：重读一遍 servers.dat 再递进去，**顺手测一遍**
    // （2026-09-27）—— 「刷新」的意思是"把这一页刷新成现在的样子"，而这一页现在不只是
    // 那份文件的镜像（图标 / MOTD / 延迟都是刚问来的）。只重测不重读走「测速」。
    connect(&dialog, &InstanceSettingsDialog::refreshServerListRequested, this,
            [this, &dialog, &targetId]() {
                loadServerList(&dialog, targetId);
                pingServers(&dialog, targetId);
            });
    // 「测速」：只重测。两条都排在 execPage 之前（见上面那段 NOTE）。
    connect(&dialog, &InstanceSettingsDialog::pingServersRequested, this,
            [this, &dialog, &targetId]() { pingServers(&dialog, targetId); });
    // ★ 「点进去就自动测速」（用户 2026-09-27 原话）。这一发是**切到那一页**发出来的
    //   （见 InstanceSettingsDialog::applyPageKey），和上面那两条"用户按了某颗按钮"不是
    //   一回事。它每一页都会发一次 —— 用户切走再切回来就又测一遍，而"已经在跑"那一趟
    //   不会被重开（见 pingServers 里那道闸）。
    connect(&dialog, &InstanceSettingsDialog::serverPageEntered, this,
            [this, &dialog, &targetId]() { pingServers(&dialog, targetId); });

    // 「下载新模组 / 资源包 / 数据包 / 光影」那四颗（2026-09-27）：**切到下载页对应的那一栏**。
    // 那一页和下载页是两块地方，路只有这一条 —— 收页 + 摆选项卡 + 挑那一行全在
    // openDownloadCategory() 一处（内含"为什么要排到下一轮事件循环"那句）。
    //
    // NOTE: 接在 execPage **之前**，理由同上：那一下会阻塞在一个嵌套事件循环里，
    //       用户在页面上点那颗按钮时，这一句还没返回，接晚了就一辈子收不到。
    // ★ 2026-09-28：把**是哪个实例**也带过去（`targetId` 就是这一页说的那个实例）。那一栏
    //   靠它做两件事：左上角露出「← 返回实例设置」（回得来）、以及把「已安装版本」选到它
    //   身上（下来的东西落进"当前实例"）。不带的话用户点完那颗键就回不去了。
    connect(&dialog, &InstanceSettingsDialog::browseDownloadsRequested, this,
            [this, &targetId](const QString &categoryKey) {
                openDownloadCategory(categoryKey, targetId);
            });

    // ★ 行上的名字点得动（2026-09-27，用户原话「除了模组以外的几个都是点击模组名称自动
    //   进入下载界面（前提是匹配上了，模组的待会再说）」）：**进的是那个资源自己的那一页**
    //   （和下载页里点一行开的是同一页）—— 「前提是匹配上了」那句话只有这样才说得通，
    //   换成"切到下载页"就跟匹配不匹配没关系了。
    //   路径 → 它是哪个资源，是上面那趟反查认出来的（见 m_instanceFileIdentities）；页面
    //   那边只会为**记过名字**的那几条发这一发（没认出来的名字压根点不动）。
    // ★★ `targetId` 按**引用**收（和上面那批 lambda 同一个理由，见 refreshInstanceDirs 的
    //   NOTE）：它必须取到**改名之后**的值 —— 用户改完名再点名字，回来时那一页要开在
    //   新目录名的实例上，收一份旧名字就是"实例设置打不开"。
    // NOTE: 同样接在 execPage **之前**，理由和上面那条逐字相同。
    connect(&dialog, &InstanceSettingsDialog::fileResourceRequested, this,
            [this, &targetId](const QString &pageKey, const QString &path) {
                openInstanceFileResource(targetId, pageKey, path);
            });

    // 地址那一格右键 ⇒ 把**完整的**地址放进剪贴板（2026-09-27，用户原话「ip地址右键可以
    // 复制」）。复制完**不弹话**：这一下是用户自己按的，粘一下就看得见 —— 和资源详情页
    // 那条「复制名称」逐字同一个路数（那边进日志留个痕就够）。
    connect(&dialog, &InstanceSettingsDialog::copyServerAddressRequested, this,
            [this](const QString &address) {
                if (address.isEmpty()) {
                    return;
                }
                if (QClipboard *clipboard = QApplication::clipboard()) {
                    clipboard->setText(address);
                    appendLog(tr("已复制服务器地址：%1").arg(address));
                }
            });

    // ---------- 「改了就写」那两条（2026-09-26）----------
    //
    // ★ 用户原话：「实例设置里面的那个取消和确定删了，修改了的设置实时写入」。从前收尾那
    //   一段（`execPage` 返回之后读对话框、改名、写设置）**整个搬到这两条 lambda 里** ——
    //   写什么、按什么顺序写，一个字没变，只是时机从"点确定"变成了"值真的变了"
    //   （对话框那边保证：值没变不发，见 notifyEdited()）。
    //
    // NOTE: `targetId` 是**活到 execPage 结束**的那个局部变量（上面算目录时就摆好了；
    //       下面 execPage 会阻塞在嵌套事件循环里，这一层的栈一直站着），改名成功之后它
    //       跟着换 —— 不换的话，改完名之后动任何一个开关都会写到**老目录名**上，而
    //       updateVersionMeta() 会照着老名字把目录重新建出来，用户手上就多一个只有一份
    //       wtfml.meta 的空壳实例。
    connect(&dialog, &InstanceSettingsDialog::settingsEdited, this,
            [this, &dialog, &targetId]() { applyInstanceSettings(targetId, dialog); });

    // 改名：**排在写设置之外自己一条**（用户 2026-09-25 原话「重命名进实例设置」）。
    // 两件事不能合成一条 —— 改名的副作用是**整个实例换了个目录**，写设置必须发生在它
    // 之后（顺序反了就是上面那个空壳实例）。对话框那边也把这两件事分成了两条信号：
    // 那一格走 `editingFinished`（回车 / 失焦），正好是"这次改名想清楚了"的那一刻。
    connect(&dialog, &InstanceSettingsDialog::renameRequested, this,
            [this, &dialog, &targetId, &refreshInstanceDirs](const QString &name) {
                const QString wanted = name.trimmed();
                // 空 = 用户把那一格清空了（不是"想改成没有名字"），等于没改。
                if (wanted.isEmpty() || wanted == targetId) {
                    dialog.setInstanceName(targetId);
                    return;
                }
                if (!applyRename(targetId, wanted)) {
                    // ★ 失败要把那一格退回原样。不退回的话，用户眼前那一格写着新名字、
                    //   而磁盘上还是老名字 —— 他接着动任何一个开关，那一次写入就落到一个
                    //   **不存在的目录**上（updateVersionMeta() 会照着它把目录建出来）。
                    //   applyRename() 已经把原因弹出来了，这里只负责让界面别撒谎。
                    dialog.setInstanceName(targetId);
                    return;
                }
                // 改完名之后写的是**新目录**（applyRename() 里那句 refreshInstalledVersions()
                // 已经把列表和下拉框都换到新名字上了）。
                targetId = wanted;
                dialog.setInstanceName(targetId);
                // 「文件管理」那八页说的也是**这个实例的目录**，整份跟着重算 ——
                // 重算之前它们还指着那个已经不在了的老目录（见上面那个 lambda 的 NOTE）。
                refreshInstanceDirs(targetId);
                // 页眉上那行字也是"这个实例叫什么"，跟着换 —— 不换的话页面上最高处还写着
                // 老名字，而它底下那一格已经是新的了。
                //
                // NOTE: 先让对话框把**它自己那行标题**重拼一遍（`titleText()` 读的就是它），
                //       再拿它去问覆盖层要那行字。`titleText()` 在改名前是**构造时按当时那个
                //       名字拼好的死字符串** —— 不重拼就直接递给 setPageTitle，页眉上写的
                //       还是老名字（而且看不出来哪里错了：两边调的是同一个函数）。
                dialog.setInstanceDisplayName(instanceDisplayName(targetId));
                m_ui->overlay()->setPageTitle(dialog.titleText());
            });

    // ---------- 停在哪一页（2026-09-28，用户原话「从模组点进去，返回也在模组而非概览」）----------
    //
    // 摆在这一段**最后、execPage 之前**有两条理由：
    //   ① `showPage()` 换不了"左栏里没有的那几页"（被用户勾掉的、以及「配置文件」那种没
    //      入口的），而"哪几页在左栏里"是上面 `setPageChoices()` 定的 ⇒ 必须在它**之后**；
    //   ② `execPage()` 是**阻塞**的（下面那一句会卡在嵌套事件循环里转到用户点返回），
    //      再往后的代码这一趟根本执行不到。
    // 认不出来的键 `showPage()` 自己判 -1 什么都不做（停「概览」）—— 不静默出错。
    if (!pageKey.isEmpty()) {
        dialog.showPage(pageKey);
    }

    // ---------- 这一页归「实例」那一颗选项卡（2026-09-28）----------
    //
    // ★ 见 .hpp 那段：不钉的话，从下载页那颗「← 返回实例设置」进来时这一页会被记成
    //   **下载页的**页面，而用户点「实例」看见的是实例列表（那一页收着看不见）、又因为它
    //   占着覆盖层那一位而从此开不了新的实例设置，每次还被拽回下载页。
    //   钉住 + 先把选项卡切过去（**切完再开**：`ownerTab` 不是当前这一颗的话，这一页一开
    //   出来就是"收着"的，用户按了那颗返回键屏幕上什么都不出现）。
    //
    // NOTE: 切选项卡要排在 `overlayAcceptsNewPage()`（上面那个闸）**之后**：那一句在
    //       "已经有一页开着"时会弹话，而弹话不该顺手把用户翻到别的选项卡上。
    const int instancesTab = MainWindowUI::instancesTabIndex();
    if (QTabWidget *tabs = m_ui->getTabWidget()) {
        if (instancesTab >= 0 && instancesTab < tabs->count()) {
            tabs->setCurrentIndex(instancesTab);
        }
    }

    // 这一页盖在内容区上（标签栏和底栏都留着），左上角那颗返回箭头 = 关掉这一页（见
    // OverlayHost::execPage）。标题写在页眉上，所以这一页自己那行标题要收起来 ——
    // 两行一模一样的字挨着是重复，不是强调。
    //
    // NOTE: 这一页**不再 accept**（没有「确定」了），所以这里**不看返回值** —— 关页
    //       不等于放弃（该写的早就在上面那些 lambda 里写完了），"关掉"就是"关掉"。
    dialog.setTitleVisible(false);
    m_ui->overlay()->execPage(&dialog, dialog.titleText(), instancesTab);
}

// ---------- 实例设置「文件管理」那八页（见 .hpp 里那一段）----------

QHash<QString, QString> MainWindow::instanceFileDirs(const QString &versionId) const {
    const QString root = AppPaths::versionDir(versionId);
    QHash<QString, QString> dirs;

    // 能下载的那四类走 ResourceCatalog 那张表 —— 那是目录名的**唯一出处**
    // （整合包那一路 curseForgeFilePath() 也照它写，注释里点名要求一致）。
    const QList<QPair<QString, ResourceCatalog::Kind>> downloadable = {
        {QStringLiteral("mod"), ResourceCatalog::Kind::Mod},
        {QStringLiteral("resourcepack"), ResourceCatalog::Kind::ResourcePack},
        {QStringLiteral("datapack"), ResourceCatalog::Kind::DataPack},
        {QStringLiteral("shader"), ResourceCatalog::Kind::Shader},
    };
    for (const QPair<QString, ResourceCatalog::Kind> &entry : downloadable) {
        dirs.insert(entry.first,
                    root + QLatin1Char('/') + ResourceCatalog::folderForKind(entry.second));
    }

    // 这五样下载页里没有对应的"类"（没有"存档"/"配置文件"这一类资源），目录名直接写在这儿。
    // 「原理图」用的是 Litematica 的默认目录名；另外四个是游戏自己建的。
    //
    // ★ `config` 是 2026-09-27 加的（实例设置里那一页「配置文件」）。**它不是游戏建的** ——
    //   是模组自己建的（原版客户端根本没有这个目录，那时候那一页会说"进一次游戏"）。
    const QList<QPair<QString, QString>> plain = {
        {QStringLiteral("schematic"), QStringLiteral("schematics")},
        {QStringLiteral("config"), QStringLiteral("config")},
        {QStringLiteral("save"), QStringLiteral("saves")},
        {QStringLiteral("screenshots"), QStringLiteral("screenshots")},
        {QStringLiteral("logs"), QStringLiteral("logs")},
    };
    for (const QPair<QString, QString> &entry : plain) {
        dirs.insert(entry.first, root + QLatin1Char('/') + entry.second);
    }
    return dirs;
}

void MainWindow::openDownloadCategory(const QString &categoryKey, const QString &returnInstanceId) {
    // 认行用**条目自己的 UserRole**（= 那一栏的键），不是行号：左栏那一列的行序是界面
    // 自己按 downloadCategoryKeys() 摆出来的，这一层不去假设"第几个就是哪一个"
    // （和 MainWindow::refreshDownloadCategory 那边认身份的做法一致）。
    QListWidget *categories = m_ui->getDownloadCategoryList();
    const QStringList keys = MainWindowUI::downloadCategoryKeys();
    int row = -1;
    for (int i = 0; categories != nullptr && i < categories->count(); ++i) {
        if (categories->item(i)->data(Qt::UserRole).toString() == categoryKey) {
            row = i;
            break;
        }
    }
    if (row < 0 || !keys.contains(categoryKey)) {
        // 认不出来就**说出来**：悄悄什么都不做的症状是"按了那颗按钮没反应"，
        // 而这一层是唯一知道"下载页到底有哪几栏"的地方，出错时不说就没人查得到。
        qWarning() << "下载页没有这一栏：" << categoryKey;
        return;
    }

    // ② 这一栏是不是**从某个实例的设置页里点「下载新模组」进来的**（2026-09-28）。
    //
    //    是的话记下那个实例，并且把「← 返回实例设置」露出来 —— 用户原话：「点击下载新模组/
    //    资源包/...时左上角保留一个返回键，返回到实例设置里面，和资源包详情啥的一致」。
    //    上面那一下 `dismiss()` 会把那一页收掉（`OverlayHost` 不做页面栈），所以"来路"只能
    //    由这一层自己记着、自己放回去（和 openFileResourcePage 那边同一个手法）。
    //
    //    ★ 2026-09-28：那颗键现在**整个下载页共用一颗**、摆在左栏顶上（用户原话「返回按钮
    //      也是放在左上角（就是左边栏的顶上）」），所以不再"只露这一栏那一颗"，翻到别的栏
    //      它照旧在那儿 —— 这一点比原来对：能不能回去和用户此刻在逛哪一栏无关。
    //
    //    ★ `m_downloadFromPageKey` 和这一位**同生共死**：它就是"来路那一页"。也正因为
    //      「下载新模组」那一栏**就是**实例设置里那一页（`filesDownloadCategoryKey()` 那张
    //      表是"这两处是一回事"的唯一出处），它不用另要一个参数，写的就是 `categoryKey`。
    m_downloadFromInstanceId = returnInstanceId;
    m_downloadFromPageKey = returnInstanceId.isEmpty() ? QString() : categoryKey;
    m_ui->setDownloadBackVisible(!returnInstanceId.isEmpty());

    // ① 先收掉当前这一页。**只能走 dismiss()**：它发的就是顶上那颗返回箭头的同一发信号
    //    （见 OverlayHost::dismiss() 里那段 ★）—— 这一页正卡在 `execPage()` 的嵌套事件
    //    循环里，只 `closeOverlay()` 的话一个信号都不发，那个循环永远不返回（用户看到的
    //    是"按了没反应"，测试是直接挂在那儿）。
    //
    //    NOTE: 这一发只是**尽力**先收（常见情况下它收完就完了）；收干净了没有由下一轮的
    //          `continueOpenDownloadCategory()` 判 —— 上面一层压着一条提示时 `dismiss()`
    //          收掉的是**提示**、那一页还在原处。
    m_ui->overlay()->dismiss();

    // ② 摆选项卡与那一栏**排到下一轮事件循环**：这一句这会儿还在上面那个嵌套循环的调用栈
    //    里（按钮的 clicked 处理函数），`execPage()` 还没返回、覆盖层也还盖在内容区上 ——
    //    现在就把选项卡翻过去，翻的是它**底下**那一层，而覆盖层收干净的那一下还没发生。
    //    排到下一轮，那时这一页已经关好了（和 openResourceDetailPage 那边同一个手法）。
    QTimer::singleShot(0, this, [this, row, categoryKey, returnInstanceId] {
        continueOpenDownloadCategory(row, categoryKey, returnInstanceId, kDownloadOpenRounds);
    });
}

void MainWindow::continueOpenDownloadCategory(int row, const QString &categoryKey,
                                             const QString &returnInstanceId, int roundsLeft) {
    // ---------- 收页：收到干净为止，收不干净就**什么都不做**（2026-09-28）----------
    //
    // 为什么还要再收一次：`openDownloadCategory()` 里那一发 `dismiss()` 收的是**最上面
    // 那一层** —— 提示压在实例设置上面时收掉的是提示，那一页还在原处占着覆盖层那一位。
    //
    // ★ 收不干净时**绝不能切选项卡**：切了就是把那一页孤零零地丢在别的选项卡上（它收着、
    //   看不见），而它占着覆盖层那一位 ⇒ 用户此后既回不去那一页、也开不了新的一页，还会
    //   被一次一次拽到下载页 —— 用户报的"点来点去点多了会卡住进不去实例设置（而且会变到
    //   下载页去了）"就是这个形状。宁可这一次什么都不做，也不要把他丢进那个状态。
    //   （正常路径上收不掉是走不到的：那一页归「实例」那一颗，而它的按钮只在它露着的时候
    //     点得着 —— 见 openInstanceSettings() 里那段。留着这一条是因为"走不到"和"保证了
    //     不会走不到"是两件事。）
    OverlayHost *overlay = m_ui->overlay();
    if (overlay != nullptr && overlay->pageOpen()) {
        if (roundsLeft > 0 && overlay->dismiss()) {
            // 收到一半（这一发收回来的那一层要等 `execEmbedded` 收尾才真正落下去）—— 再排一轮。
            QTimer::singleShot(0, this, [this, row, categoryKey, returnInstanceId, roundsLeft] {
                continueOpenDownloadCategory(row, categoryKey, returnInstanceId, roundsLeft - 1);
            });
            return;
        }
        qWarning() << "下载页：实例设置那一页没能收掉，这一次不切选项卡";
        return;
    }

    m_ui->getTabWidget()->setCurrentIndex(MainWindowUI::downloadTabIndex());
    // 从实例设置进来的那一次，顺手做三件事 —— 都是"让屏幕上说的和用户以为的是同一件事"：
    //   ① 「已安装版本」那一格选到**那个实例**上：这一栏下来的东西是落进"当前选中的实例"
    //      的（见 installResourceVersionNow），不替用户选上的话，"从实例 A 的页面点进来、
    //      东西却下进了实例 B"——而屏幕上没有任何东西说过会这样（用户那句「下载新模组就
    //      自动放进当前实例文件夹」说的正是这一件事）；
    //   ② 版本筛子填成那个实例的游戏版本；
    //   ③ 模组加载器筛子填成那个实例的加载器。
    // ②③ 是用户 2026-09-28 那条：「点击下载新模组/...后自动在搜索框里填入对应的版本和
    // 加载器」。
    if (!returnInstanceId.isEmpty()) {
        QComboBox *combo = m_ui->getInstalledVersionCombo();
        const int index = combo->findData(returnInstanceId);
        if (index >= 0) {
            combo->setCurrentIndex(index);
            updateStartButtonText();
        }
    }
    QListWidget *categories = m_ui->getDownloadCategoryList();
    if (categories == nullptr) {
        return;
    }
    categories->setCurrentRow(row);
    // ★ 已经在那一行上时 `setCurrentRow()` **不发** currentRowChanged（Qt 只在"变了"
    //   的时候才发），而"第一次进这一页才去搜一次"那一发正挂在它身上 ⇒ 这里补一次。
    //   同一条路、同一个函数，重复调用是安全的（它自己判"树上已经有东西就不动"）。
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    const bool known = ResourceCatalog::kindFromCategoryKey(categoryKey, &kind);
    if (!returnInstanceId.isEmpty() && known) {
        fillDownloadFiltersFromInstance(kind, returnInstanceId);
    }
    if (known) {
        ensureResourcePageLoaded(kind);
    }
}

void MainWindow::fillDownloadFiltersFromInstance(ResourceCatalog::Kind kind,
                                                const QString &instanceId) {
    const QString key = ResourceCatalog::kindKey(kind);
    // 版本那一格**读的是框里的字**（`downloadFilterGameVersion()` 就是这么读的，那一格是
    // 可输入的），所以写也要写进"字"里：`setCurrentText()` 两样都写了（有那一项就选中它、
    // 没有就把字填进编辑框）—— 那个实例的版本号在在线清单里通常有，可万一没有（离线装
    // 的自定义版本），填进去照样是对的，而那正是用户要的那一格该显示的东西。
    const QString gameVersion = m_versionManager->instanceGameVersion(instanceId);
    if (!gameVersion.isEmpty()) {
        if (QComboBox *versionCombo = m_ui->getDownloadFilterVersionCombo(key)) {
            versionCombo->setCurrentText(gameVersion);
        }
    }
    // 加载器那一格**只有模组页有**（别的页 getDownloadFilterLoaderCombo 回空指针）。
    //
    // ★ 按**文字**认（「Fabric」/「Forge」/「NeoForge」/「Quilt」）而不是按 `data()`：
    //   两个来源的键**不是一套**（Modrinth 是词、CurseForge 是数字，见 loaderOptions），
    //   而文字两边一模一样 —— 按 data 认的话，用户把来源换成 CurseForge 之后这一格就再也
    //   填不上了（而且不报错、看不出来）。
    QString loaderName;
    if (m_versionManager->instanceModLoader(instanceId, &loaderName) && !loaderName.isEmpty()) {
        if (QComboBox *loaderCombo = m_ui->getDownloadFilterLoaderCombo(key)) {
            const int index = loaderCombo->findText(loaderName);
            if (index >= 0) {
                loaderCombo->setCurrentIndex(index);
            }
        }
    }
}

void MainWindow::returnToInstanceSettingsFromDownload() {
    const QString instanceId = m_downloadFromInstanceId;
    // ★ 回来时开在**来路那一页**上（用户原话「从模组点进去，返回也在模组而非概览，
    //   其余同理」）。这一位和上面那一位同生共死，所以下面两句话并排写。
    const QString pageKey = m_downloadFromPageKey;
    // 先收干净再开那一页：`openInstanceSettings` 里第一件事就是问覆盖层开没开，
    // 而这一颗键通常是在"没有覆盖层"的时候点的（不冲突），可万一撞上一条提示，
    // 那一位留着就会让下一次进来时这颗键莫名地还露着。
    m_downloadFromInstanceId.clear();
    m_downloadFromPageKey.clear();
    m_ui->setDownloadBackVisible(false);
    if (instanceId.isEmpty()) {
        return;  // 正常走不到：那颗键只在这一位非空时才露面
    }
    openInstanceSettings(instanceId, pageKey);
}

void MainWindow::onNavRequested(int index) {
    // 顶上那四颗导航，这一层只管**下载页那两件**（用户 2026-09-28 原话「在这个情况下点击
    // 顶部下载按钮就刷新页面回到主页」+「如果点击了下载新模组按钮后再点击实例按钮，功能
    // 就和返回一样，如果点两下那就是先返回再回到实例主页」）。翻页、以及"点的是当前这一
    // 颗就把上面那层收掉"那两件，仍旧是 MainWindowUI 自己接的那一条在管（那边**先接**，
    // 所以它先跑完，见 setupConnections 里那条连接的注释）—— 这里不重复做，也不去改它。
    //
    // ---------- 「实例」那一颗 = 按了「← 返回实例设置」（用户原话见上） ----------
    //
    // ★ 这**不是**那一颗返回键的第二条接线（那颗键仍旧只有一处出口，见 setupConnections
    //   里那段）—— 这里只是"同一件事从另一颗键上够得着"。两条都走
    //   `returnToInstanceSettingsFromDownload()`，所以"清哪两位、开在哪一页"一个字都不
    //   用写两遍。
    //
    // ★ 判据四条：
    //   ① 点的是「实例」那一颗；
    //   ② **刚才是从下载页按的**（`lastNavSourceTab()`）。少了这一条，"从实例进来 →
    //      切去主页逛了一圈 → 点实例"也会被当成"回实例设置"，可那一下用户要的是切到实例
    //      列表（他手上那一页早就切走了）。这一位**只有 MainWindowUI 说得出口**：翻页那一下
    //      是它先跑的，这边醒过来时 `currentIndex()` 已经是新的一颗了（见那个访问器的注释）。
    //   ③ 这一趟**确实是从某个实例点进来的** —— 也就是那颗返回键正露着。
    //   ④ 这一层**没有别的页面占着**（典型：用户先从下载页点开了一个资源的详情页）。
    //      `openInstanceSettings()` 头一句就是 `overlayAcceptsNewPage()`，占着的话它会弹一句
    //      "先关掉现在这一页"然后**什么都不开** —— 而上面那一发已经把来路清了 ⇒ 用户拿到的是
    //      "返回键莫名没了 + 一句让他去关别的页的提示"，正是这一整段想避免的那件事。
    //      占着的时候按「实例」就照常翻页（和按「主页」「设置」一样：那一页收着、还在）。
    //
    // ★ 第二下为什么自动就成了"回到实例主页"：上面那一发把这一页开起来之后，当前选项卡
    //   **就是**「实例」，于是再按一下正中 MainWindowUI 那条的 ② 档（点的是当前这一颗
    //   ⇒ `overlayHost->dismiss()` = 收掉这一页，露出底下的实例列表）。那一下这一位已经
    //   被上面清空了 ⇒ 这里不接手，两下没人打架。
    const bool overlayFree = m_ui->overlay() == nullptr || !m_ui->overlay()->isOpen();
    if (index == MainWindowUI::instancesTabIndex()
        && m_ui->lastNavSourceTab() == MainWindowUI::downloadTabIndex()
        && !m_downloadFromInstanceId.isEmpty() && overlayFree) {
        returnToInstanceSettingsFromDownload();
        return;
    }

    //
    // ★ 判据三条，缺一不可：
    //   ① 点的**就是**「下载」那一颗；
    //   ② **刚才就是在下载页上按的**（`lastNavSourceTab()`）—— 也就是"这一按没有翻页"。
    //      少了这一条，"从实例进来、切去主页逛了一圈、再点下载"也会被当成"回主页" —— 可那
    //      一下用户要的是**切回来**（原来那一栏、连筛子带实例都在，见 MainWindowUI 搭导航
    //      那段①），顺手清掉就成了"我明明没点返回，回实例设置那颗键却没了"；
    //   ③ 这一趟**确实是从某个实例点进来的**（那一位非空 = 「← 返回实例设置」正露着）。
    //      用户原话里这一条是跟着"点了下载新模组"来的；平白从顶部点进下载页时，左栏挑的
    //      哪一栏、框里填的什么都是他自己摆的，那一发不该替他重置。
    //
    // ★★ 2026-09-28 更正 ② 的**量法**：它原来写的是 `tabs->currentIndex() == index`，那一格
    //    **量不出东西来** —— 上面那条 MainWindowUI 的槽先跑，而它自己就会把选项卡翻过来，
    //    于是"切去了主页再点下载"走到这里时 `currentIndex()` 早就是下载页了 ⇒ 判据永远为真，
    //    等于没有这一条（注释说的那个症状照样发生，只有 `mw_check` T25.8 量得出来）。
    if (index != MainWindowUI::downloadTabIndex()
        || m_ui->lastNavSourceTab() != MainWindowUI::downloadTabIndex()
        || m_downloadFromInstanceId.isEmpty()) {
        return;
    }

    // 来路那一栏先认下来：下面头一件事就是把这几位清掉。
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    const bool known = ResourceCatalog::kindFromCategoryKey(m_downloadFromPageKey, &kind);

    // ① 把"从实例进来"这件事整个收起来。返回键露不露只看这一位（见 setDownloadBackVisible
    //    的调用点），所以两位一起清，再把它收起来。
    m_downloadFromInstanceId.clear();
    m_downloadFromPageKey.clear();
    m_ui->setDownloadBackVisible(false);

    // ② 那一栏的筛子是**我们**灌进去的（fillDownloadFiltersFromInstance），回主页就该一并
    //    还回去 —— 留着的话，用户下一次点进那一栏看到的是"我没填过"的条件。
    //
    //    ★ 清完**还得把那一页的搜索结果扔掉**：筛子是新条件、树上那批是按旧条件搜出来的，
    //      两下里对不上 —— 正是上面筛子那一段点名的"界面上筛了、请求里没筛"。这里**不发
    //      新请求**：清空之后，下一次进那一页时 ensureResourcePageLoaded() 自然会按新的
    //      筛子搜一次（既不替用户白等一下，也不白烧一次限流额度）。
    if (known) {
        resetDownloadFilters(kind);
        if (QTreeWidget *tree = m_ui->getDownloadResourceTree(ResourceCatalog::kindKey(kind))) {
            tree->clear();
        }
    }

    // ③ 左栏回到第一栏 —— 那**就是**下载页的首页（「游戏」：在线版本清单，见
    //    downloadCategoryGameIndex()）。
    //
    //    ★ 摆的是**下标**，不是"现在选中的那一栏"：这件事跟用户此刻在哪一栏无关，起点是
    //      确定的。（切过去会触发 currentRowChanged → 右栏翻页；第一栏是「游戏」，那一页
    //      不走资源搜索，所以这一下不会多发任何请求。）
    if (QListWidget *categories = m_ui->getDownloadCategoryList()) {
        categories->setCurrentRow(MainWindowUI::downloadCategoryGameIndex());
    }
}

void MainWindow::openFileResourcePage(const QString &versionId, const QString &iconKey,
                                      const QString &kindKey) {
    // ★★ 2026-09-27 用户原话：「点击下载界面返回后应该退回到实例设置而不是实例列表」。
    //    这一页是**从实例设置里点出来的**，用户按返回要回的是**那一页** —— 不接的话他落在
    //    实例列表上，原来那一页（连页码带勾选）整个没了。`OverlayHost` 不做页面栈
    //    （那一层的规矩，见它的 .hpp），所以"来路"得由这一层自己放回去。
    //
    // ★ 判据是 `QDialog::Rejected`（= 用户把这一页**收掉**了）：返回箭头和 Esc 走的都是
    //   它。这一页上没有别的出路会让 `execPage()` 返回（三颗按钮和版本行都只发信号、
    //   不 accept）—— 哪一天真加了一条 accept 的出路，它自然就不会被实例设置盖住。
    //
    // ★★ 必须和 `kResourcePageNotOpened` 分开认：那一档是"这一页压根没开出来"（上面第
    //   ① ② 步认完之后、`m_resourceHits` 里那条已经不在了之类）。不分开的话，用户点一下
    //   名字 = 实例设置被收掉、详情页没出来、实例设置又被摆回去，看着就是白闪一下。
    //
    // ★ 来路是 `InstanceFiles`：只有这一条路上那一页才可能摆「模组设置」（见
    //   openResourceDetailPage 里那一句 —— 还要**同时**是个模组）。
    //
    // ★ 2026-09-28：`versionId` 就是"当前实例"，一并传进去（开页时按它的版本预选、把它的
    //   加载器排前面）。原来它只用来"返回时把实例设置放回去"，现在两件事都用它。
    const int result = openResourceDetailPage(iconKey, kindKey, DetailSource::InstanceFiles, versionId);
    if (result != QDialog::Rejected || versionId.isEmpty()) {
        return;
    }
    // ★ 回来时开在**刚才那一页**上（`kindKey` 就是那一页的键 —— 「这一页是哪一类资源」
    //   和「实例设置里哪一页」现在恰好同名，那个等式在 filesDownloadCategoryKey() 一处）。
    //   用户原话：「从模组点进去，返回也在模组而非概览，其余同理」。
    openInstanceSettings(versionId, kindKey);
}

void MainWindow::openInstanceFileResource(const QString &versionId, const QString &pageKey,
                                          const QString &path) {
    // ★ 2026-09-27 补，用户原话：「除了模组以外的几个都是点击模组名称自动进入下载界面
    //   （前提是匹配上了，模组的待会再说）」。进的是**那个资源自己的那一页**（下载页里点
    //   一行开的就是它，见 openResourceDetailPage）——「前提是匹配上了」那句话只有这样
    //   才说得通：换成"切到下载页那一栏"就跟匹配不匹配没关系了。
    //
    // ① 这一页是"哪一类资源"：判据就是 filesDownloadCategoryKey() 那张表 —— 页键和下载页
    //    分类键现在恰好同名，可那一处才是"这两件事是一回事"的唯一出处。认不出来就回去，
    //    连覆盖层都不碰：悄悄地少收一页比收错了强。
    ResourceCatalog::Kind kind = ResourceCatalog::Kind::Mod;
    const QString kindKey = InstanceSettingsDialog::filesDownloadCategoryKey(pageKey);
    if (kindKey.isEmpty() || !ResourceCatalog::kindFromCategoryKey(kindKey, &kind)) {
        return;
    }
    // ② 这一条是哪个资源。认不出来 = 没匹配上 ⇒ **什么都不做**（那一行的名字本来也没
    //    变成链接，正常走不到这儿；测试里点一下量得到的就是"没反应"）。
    const auto identity = m_instanceFileIdentities.constFind(instanceIconCacheKey(path));
    if (identity == m_instanceFileIdentities.constEnd()) {
        return;
    }
    const QString iconKey = identity.value();
    // ③ 先收掉实例设置那一页 —— **只能走 dismiss()**，理由和 openDownloadCategory() 里
    //    逐字相同：那一页正卡在 `execPage()` 的嵌套事件循环里，只 `closeOverlay()` 的话
    //    一个信号都不发，那个循环永远不返回。
    if (m_ui->overlay() == nullptr || !m_ui->overlay()->isOpen()) {
        // 页已经关掉了（连着点两下、或者返回键和它撞上）。没有那一层嵌套循环要收，
        // 直接开新的就完了。
        openFileResourcePage(versionId, iconKey, kindKey);
        return;
    }
    m_ui->overlay()->dismiss();
    // ④ 摆到下一轮：这一句这会儿还在那个嵌套循环的调用栈里（名字上那个鼠标松开事件），
    //    execPage() 还没返回、覆盖层也还盖着 —— 现在就去开新页的话，openResourceDetailPage
    //    开头那条 `overlay->isOpen()` 会当场把这次点击吃掉。
    //
    //   NOTE: 键与键都在这一层认完了，排到下一轮带过去的只有那三个**字符串**：下一轮
    //         里这一页早就没了，那时候再去查"这一条是哪个资源"就等于查一份已经被清掉的
    //         上下文（和 openDownloadCategory 那边带 `row` 过去是同一个道理）。
    //         `versionId` 也在这一层就绑好：它是"回来时把哪一页放回去"的唯一依据，而实例
    //         设置那一页这会儿已经在往下撤了。
    QTimer::singleShot(0, this, [this, versionId, iconKey, kindKey] {
        openFileResourcePage(versionId, iconKey, kindKey);
    });
}

// ---------- 「世代备份」那三件事（见 .hpp 那一段）----------

namespace {

/// 每个实例的备份留最近多少份。**不按文件算、按"存了几次"算**：备份是"某一刻整个 config/
/// 的一批"，用不着按单个文件各留二十份（那会长出二十个文件夹、每个里面一个文件）。
///
/// NOTE: 20 这个数是拍的，不是量出来的 —— 它只影响"最多能回退几步"，而每一步都比上一步
///       更不像用户想要的。真要按量出的数来定，得先知道用户平均多久存一次（今天没有这个
///       数）。这里宁可留够：20 份 × 一份配置那几十 KB 是几个 MB。
constexpr int kConfigBackupGenerations = 20;

}  // namespace

QString MainWindow::instanceIdOfPath(const QString &path) const {
    // cleanPath 顺手把 `\` 换成 `/`（Windows 上真有可能递进来一份原生分隔符的）—— 后面
    // 每一步都只认一种写法，省得两个方向的字符串各比一遍。
    const QString clean = QDir::cleanPath(path);
    const QString prefix = AppPaths::versionsDir() + QLatin1Char('/');
    if (!clean.startsWith(prefix)) {
        return QString();  // 不在任何实例底下（理论上到不了这儿：这一格只列 config/ 里的文件）
    }
    const QString rest = clean.mid(prefix.size());
    const int slash = rest.indexOf(QLatin1Char('/'));
    // `slash <= 0` 一并把"versions/ 底下直接一个文件"那种挡掉了（那不是实例）。
    return slash <= 0 ? QString() : rest.left(slash);
}

QString MainWindow::latestConfigBackup(const QString &instanceId, const QString &path) const {
    if (instanceId.isEmpty()) {
        return QString();
    }
    const QString root = AppPaths::configBackupDir();
    const QStringList names =
        QDir(root + QLatin1Char('/') + instanceId).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QStringList mine;
    for (const QString &name : names) {
        // ★ 只认我们自己生成的那个形状（见 ConfigFileText::isBackupFolderName()）：用户往这个
        //   目录里放了个同名文件夹的话，那不是"这个文件的上一次"。
        if (ConfigFileText::isBackupFolderName(name)) {
            mine.append(name);
        }
    }
    // 名字倒序 == 时间倒序（定长的数字段，见 ConfigFileText::backupFolderName() 那段 NOTE）
    // —— 命中的第一个就是最近那一份。
    std::sort(mine.begin(), mine.end(), std::greater<QString>());
    for (const QString &name : mine) {
        const QString candidate =
            ConfigFileText::backupPathFor(root, instanceId,
                                          instanceFileDirs(instanceId).value(QStringLiteral("config")),
                                          path, name);
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

void MainWindow::pruneConfigBackups(const QString &instanceId, int keep) {
    if (instanceId.isEmpty()) {
        return;
    }
    const QString dir = AppPaths::configBackupDir() + QLatin1Char('/') + instanceId;
    const QStringList names = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    // 挑该删的这一步在 ConfigFileText 里（纯函数、验证工程直接喂名字进去量）：认不出的
    // 名字一个都不碰。
    const QStringList doomed = ConfigFileText::backupFoldersToRemove(names, keep);
    for (const QString &name : doomed) {
        QDir(dir + QLatin1Char('/') + name).removeRecursively();
    }
}

void MainWindow::openConfigFile(InstanceSettingsDialog *dialog, const QString &path) {
    if (dialog == nullptr) {
        return;
    }
    // ★ **打开之前**先量一眼盘上那一份（见 ConfigFileText::Stamp）：保存前拿它比对，对不上
    //   就说明这中间有别人动过（游戏退出时写回 / 另一个编辑器 / 模组自己的迁移），那一趟
    //   不许静静盖掉。
    //
    // ★ 顺序要紧：量在**读之前**。反过来的话，万一有人正好在"我们读完"和"我们量"之间写了
    //   一次，量到的就是**新**那一份 —— 保存时会判成"没动过"，正好把那次改动盖掉。这个方向
    //   的错更贵，所以宁可把"读完就被改"判成"改过"（多问一句，用户点一下就过去了）。
    const QFileInfo onDisk(path);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        // 会挡住用户要做的事 ⇒ 必须弹出来（见 .hpp 那条硬约束）。多半是文件在那一下被
        // 别的东西独占着（游戏正开着、杀毒在扫），或者是权限。
        InlineMessage::warning(m_ui->overlay(), tr("打不开这个文件"),
                               tr("%1\n\n%2").arg(QDir::toNativeSeparators(path),
                                                  file.errorString()));
        return;
    }
    const QByteArray raw = file.readAll();
    const QString readError = file.errorString();
    const bool readFailed = file.error() != QFileDevice::NoError;
    file.close();
    if (readFailed) {
        InlineMessage::warning(m_ui->overlay(), tr("读不完这个文件"),
                               tr("%1\n\n%2").arg(QDir::toNativeSeparators(path), readError));
        return;
    }
    ConfigFileText::Stamp stamp;
    if (onDisk.exists()) {
        stamp.size = onDisk.size();
        stamp.mtimeMs = onDisk.lastModified().toMSecsSinceEpoch();
    }
    // 解码**不是解析**（这一页压根不认格式，见 ConfigFileText 那一段）：它只认 BOM、认
    // 行尾、认这一份是不是 UTF-8 —— 前两样要原样存着，改完写回去时一个字都不能变。
    dialog->openConfigEditor(path, ConfigFileText::decode(raw), stamp);
    // 这个文件留过备份没有 —— 那一格那颗「还原上一次」画不画就看这一位（**必须排在
    // openConfigEditor 之后**：setFile() 会把这一位清成 false）。
    const QString instanceId = instanceIdOfPath(path);
    dialog->setConfigBackupAvailable(!latestConfigBackup(instanceId, path).isEmpty());
}

void MainWindow::saveConfigFile(InstanceSettingsDialog *dialog, const QString &path,
                                const QByteArray &bytes) {
    if (dialog == nullptr) {
        return;
    }
    ConfigFileEditorPage *editor = dialog->configFilePage();

    // ---------- 闸 ①：这个实例正开着游戏 ⇒ 不写 ----------
    //
    // ★ 这不是"劝一句"，是**不写**：游戏手里有一份配置的内存副本（模组启动时读进去的），
    //   它退出（或者用户在游戏里改一次设置）的时候会**整份写回**盘上 —— 现在写下去的东西
    //   会被静默盖掉，而用户看到的是「我改了没用」，回头只会以为这个启动器有毛病。判据和
    //   「游戏还在跑、窗口不许关」那一趟用的是同一份（runningVersionIds()，一个字都没另起）。
    const QString instanceId = instanceIdOfPath(path);
    if (!instanceId.isEmpty() && runningVersionIds().contains(instanceId)) {
        InlineMessage::warning(
            m_ui->overlay(), tr("这个实例正开着游戏"),
            tr("%1\n\n游戏里那份配置是它自己读进去的，它退出（或者你在游戏里改一次设置）的"
               "时候会整份写回来，现在存下去的东西会被它盖掉。\n\n先把游戏关掉，再回来改。")
                .arg(QDir::toNativeSeparators(path)));
        return;
    }

    // ---------- 闸 ②：盘上那份还是我打开时那一份吗 ----------
    //
    // ★ 这里比的是 **大小 + 修改时间**（见 ConfigFileText::Stamp 那段，两样一起才够用），
    //   比的是"我打开它的时候"和"现在"—— 中间别人动过就必须问一句，**不许静静盖掉**。
    const QFileInfo now(path);
    ConfigFileText::Stamp onDisk;
    if (now.exists()) {
        onDisk.size = now.size();
        onDisk.mtimeMs = now.lastModified().toMSecsSinceEpoch();
    }
    // 打开那一刻**量到过**才比：两边都得是"量过的"才有意义（没量到的那一份是合成的、
    // 或者当时就量不动）。对不上 = 中间有人动过，或者文件干脆没了。
    if (editor != nullptr && editor->stamp().valid() && editor->stamp() != onDisk) {
        if (!now.exists()) {
            // 文件被删了（或者整个目录被挪走）。框里这一份是它最后的样子 —— 写下去等于把它
            // 重新建出来，所以也得问一句（用户可能正是为了"让它按默认值重生成"才删的）。
            const int picked = InlineMessage::ask(
                m_ui->overlay(), MessageKind::Question, tr("盘上那个文件没有了"),
                tr("%1\n\n打开它的时候它还在，现在盘上已经没有了（被删掉、或者你把它挪走了）。"
                   "\n\n现在存下去，它会照你框里这一份重新建出来。")
                    .arg(QDir::toNativeSeparators(path)),
                {tr("重新写出来")}, -1, -1, false);
            if (picked != 0) {
                return;  // 关掉 / Esc：盘上、框里都原样
            }
        } else {
            // ★ 三颗出口，而且**默认那颗是"什么都不做"**（`defaultIndex = -1`）：这是一件
            //   会毁掉别人改动的事，误按一个回车不该把它办了（和那几处危险动作同一个规矩）。
            const int picked = InlineMessage::ask(
                m_ui->overlay(), MessageKind::Question, tr("盘上那份被人改过了"),
                tr("%1\n\n这份文件在你打开之后被别的东西改过（多半是游戏退出时写回，也可能"
                   "是另一个编辑器）。现在存下去，改它的那一次就没有了。")
                    .arg(QDir::toNativeSeparators(path)),
                {tr("盖掉它"), tr("读盘上那一份")}, -1, 0, false,
                tr("「读盘上那一份」= 你在这儿改的那些丢掉，框里换成盘上现在的内容。"));
            if (picked == 1) {
                // 用户要的是盘上那一份：重读一遍（**不写盘**）。走的是和点开一个文件同一条路
                // —— 它顺手把 stamp 和"留过备份没有"都重新认一遍。
                openConfigFile(dialog, path);
                return;
            }
            if (picked != 0) {
                return;  // 关掉 / Esc：什么都不做
            }
            // picked == 0（盖掉它）：往下走。**这一趟照样给盘上那一份留备份** —— 所以"盖掉"
            // 也不是不可逆的，这正是这一整套东西的用处。
        }
    }

    // ---------- 闸 ③：写之前把盘上那一份原样留一份 ----------
    //
    // ★ 备份的是**盘上那一份**（不是框里这一份）：这一页说的"上一次"是"我上一次按下保存
    //   之前的那一份"，所以留的就是马上要被盖掉的那个样子。于是「还原上一次」有一个确定
    //   的含义：取回来的永远是"我上一次保存之前的内容"。
    //
    // ★ 留不下就不写（不是"警告一句然后照写"）：这一整件事的前提是"改坏了能回去"，而这一页
    //   自己没有撤销（那一行字是页面上写着的）。备份写不进去还照写，就是把用户手上唯一一份
    //   配置丢进一个没有回退按钮的操作里 —— 而它挡住的是"这一次保存"，用户还能把框里的
    //   内容自己拷出来、或者去把那个目录的权限修好再来。
    bool backedUp = false;
    if (!instanceId.isEmpty() && now.exists()) {
        QFile source(path);
        QByteArray previous;
        if (source.open(QIODevice::ReadOnly)) {
            previous = source.readAll();
        }
        if (source.error() != QFileDevice::NoError) {
            InlineMessage::warning(
                m_ui->overlay(), tr("保存之前没能读到原来那一份"),
                tr("%1\n\n读它读不完（%2），所以这一次没有存 —— 直接写下去的话，它原来是什么"
                   "样就再也找不回来了。")
                    .arg(QDir::toNativeSeparators(path), source.errorString()));
            return;
        }
        source.close();

        const QString root = AppPaths::configBackupDir();
        const QStringList existing =
            QDir(root + QLatin1Char('/') + instanceId).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        // 文件夹名 = 现在这一刻（见 ConfigFileText::backupFolderName()）：同一秒里存两次会挂
        // 一个 `-2`，**不覆盖**上一次那一份（覆盖的话"上一步"就悄悄变成了"这一步"）。
        const QString folder =
            ConfigFileText::backupFolderName(QDateTime::currentDateTime(), existing);
        const QString target = ConfigFileText::backupPathFor(
            root, instanceId, instanceFileDirs(instanceId).value(QStringLiteral("config")), path,
            folder);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QSaveFile backup(target);
        bool ok = backup.open(QIODevice::WriteOnly);
        if (ok && backup.write(previous) != previous.size()) {
            ok = false;
        }
        if (ok && !backup.commit()) {
            ok = false;
        }
        if (!ok) {
            backup.cancelWriting();
            InlineMessage::warning(
                m_ui->overlay(), tr("保存之前没能留下原来那一份"),
                tr("%1\n\n没能往这里写一份备份（%2）：\n%3\n\n所以这一次没有存 —— 这一页改错了"
                   "没有撤销，留着能回去的那一份比存下去要紧。")
                    .arg(QDir::toNativeSeparators(path), backup.errorString(),
                         QDir::toNativeSeparators(target)));
            return;
        }
        backedUp = true;
    }

    // ★ `QSaveFile` 而不是 `QFile`：它先往同一个目录下写一个临时文件，最后**原子地**改名
    //   过去。直接写的话，写到一半失败（盘满 / 被杀毒拦下 / 断电）会把用户手上那份配置
    //   毁成半截 —— 而这一页没有"恢复默认"（模组的默认值是它下次启动时自己重写一份，
    //   那要等它启动，也不保证每个模组都这么干）。
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        InlineMessage::warning(m_ui->overlay(), tr("保存失败"),
                               tr("%1\n\n%2").arg(QDir::toNativeSeparators(path),
                                                  file.errorString()));
        return;
    }
    if (file.write(bytes) != bytes.size()) {
        const QString reason = file.errorString();
        file.cancelWriting();
        InlineMessage::warning(m_ui->overlay(), tr("保存失败"),
                               tr("%1\n\n%2").arg(QDir::toNativeSeparators(path), reason));
        return;
    }
    if (!file.commit()) {
        InlineMessage::warning(m_ui->overlay(), tr("保存失败"),
                               tr("%1\n\n%2").arg(QDir::toNativeSeparators(path),
                                                  file.errorString()));
        return;
    }
    // 写成了才告诉那一格 —— 它那一句"已保存（时:分:秒）"就是**盘上真的变了**的意思，
    // 写失败的时候不许出现（那一档由上面那几句话负责）。
    if (editor != nullptr) {
        editor->markSaved();
        // ★ 把刚写下去那一份的样子记回去：不记的话，**用户连着存两次**，第二次会被自己
        //   上一次写下去的那一份判成"盘上被人改过了"（mtime 变了）—— 于是每一次保存都要
        //   多问一句"要盖掉吗"。它要的是刚写下去那个 Stat。
        const QFileInfo after(path);
        ConfigFileText::Stamp fresh;
        if (after.exists()) {
            fresh.size = after.size();
            fresh.mtimeMs = after.lastModified().toMSecsSinceEpoch();
        }
        editor->setStamp(fresh);
        // 这一颗按钮画不画**只有一条判据**（和打开文件那一刻问的是同一个函数）：刚刚留的
        // 那一份就在那儿，所以这一次存完它一定找得到；而"这一次没留成"（文件本来就不在盘上
        // 了）也不会把更早那些备份一起抹掉 —— 那些备份照样能取回来。
        //
        // NOTE: 用 `backedUp` 这一位是不行的：它只说"这一趟留了没有"，不说"有没有别的可回"。
        editor->setBackupAvailable(!latestConfigBackup(instanceId, path).isEmpty());
    }
    if (backedUp) {
        pruneConfigBackups(instanceId, kConfigBackupGenerations);
    }
}

void MainWindow::restoreConfigFile(InstanceSettingsDialog *dialog, const QString &path) {
    if (dialog == nullptr) {
        return;
    }
    ConfigFileEditorPage *editor = dialog->configFilePage();
    if (editor == nullptr) {
        return;
    }
    const QString instanceId = instanceIdOfPath(path);
    const QString latest = latestConfigBackup(instanceId, path);
    if (latest.isEmpty()) {
        // 走到这儿说明"刚刚还有、点下去就没了"（用户自己把那个目录删了）。**不许什么都不说**
        // —— 用户按了按钮，总得有个交代；顺手把那一颗收回去。
        editor->setBackupAvailable(false);
        InlineMessage::warning(m_ui->overlay(), tr("找不到原来那一份了"),
                               tr("这个文件的备份不在了（%1）。").arg(QDir::toNativeSeparators(
                                   AppPaths::configBackupDir())));
        return;
    }
    QFile file(latest);
    if (!file.open(QIODevice::ReadOnly)) {
        InlineMessage::warning(m_ui->overlay(), tr("打不开备份里的那一份"),
                               tr("%1\n\n%2").arg(QDir::toNativeSeparators(latest),
                                                  file.errorString()));
        return;
    }
    const QByteArray raw = file.readAll();
    const QString readError = file.errorString();
    const bool readFailed = file.error() != QFileDevice::NoError;
    file.close();
    if (readFailed) {
        InlineMessage::warning(m_ui->overlay(), tr("读不完备份里的那一份"),
                               tr("%1\n\n%2").arg(QDir::toNativeSeparators(latest), readError));
        return;
    }
    const ConfigFileText::Loaded loaded = ConfigFileText::decode(raw);
    if (!loaded.utf8Ok) {
        // 那一份不是 UTF-8（原件当时就是）。**不摆进去**：框里这一份是 UTF-8，摆进去会变成
        // 一堆 U+FFFD，再一存就把那份备份的内容毁成问号了 —— 而它是用户唯一的退路。
        InlineMessage::warning(
            m_ui->overlay(), tr("那一份不是 UTF-8"),
            tr("%1\n\n它里面有几个字节不是 UTF-8，摆进这个编辑框（只认 UTF-8）会把那些字弄坏。"
               "要它的话，直接拿这个文件替换过去：\n%2")
                .arg(QDir::toNativeSeparators(latest), QDir::toNativeSeparators(path)));
        return;
    }
    // "那份是什么时候的"取**备份文件自己的时间**（= 我们写下这一份的那一刻 = 那一次保存的
    // 时候），不是文件夹名里那一串 —— 两者本来一样，但这一份是盘上真有的那个时间。
    const QString when =
        QFileInfo(latest).lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    editor->restoreContent(loaded.text, when);
}

void MainWindow::leaveConfigEditor(InstanceSettingsDialog *dialog) {
    if (dialog == nullptr) {
        return;
    }
    ConfigFileEditorPage *editor = dialog->configFilePage();
    if (editor == nullptr) {
        return;
    }
    // ★ 改过的东西**不许悄悄没掉**（那一问的字见 confirmDiscardConfigEdits()）。
    if (editor->isDirty()) {
        if (!confirmDiscardConfigEdits()) {
            return;
        }
        // 退回读进来那一份 —— 不退回的话，isDirty() 还是 true，用户下一次点「返回」又会被
        // 问一遍（"我不是刚说了丢掉吗"）。
        editor->discardChanges();
    }
    dialog->closeConfigEditor();
}

bool MainWindow::confirmDiscardConfigEdits() {
    return InlineMessage::question(
        m_ui->overlay(), tr("改过还没保存"),
        tr("这个文件刚才改的地方还没写到盘上，离开就会丢掉这些改动。\n\n要丢掉吗？"),
        tr("丢掉改动"), tr("留在这儿"), false);
}

namespace {

/// 文件名切出来的"词"里有没有 @p token（忽略大小写）。
///
/// ★ 2026-09-27 新加的。原来 `instanceHasMod()` 是拿文件名跟 token 做 `startsWith()` ——
///   在一个"jar 可以随便改名"的世界里不够：**中文整合包和 PCL 都会给 jar 前面挂一个
///   中文名**，用户盘上真有的两例就是 `[投影] litematica-fabric-26.2-0.28.7.jar` 和
///   `[虹膜] iris-fabric-1.11.4+mc26.2.jar`。按前缀认的话这两样都算"没装"，而它们明明
///   装着 —— 于是「光影包」「原理图」那两页谎报"用不上"（用户 2026-09-27 报的
///   「原理图和光影包没正常显示」）。
///
/// 切词规则：**只有 ASCII 的字母数字算"词里的字"**，别的一律当分隔符 —— `-` `_` `+` `.`
/// 空格、方括号，以及**中日韩字**（那一档是刻意的：`虹膜iris-….jar` 这种一个分隔符都不
/// 带的写法也切得出来，中文不会粘住后面的词）。
/// 命中判据两条：词**等于**它，或者词**以它开头**（`irisfly.jar` / `litematicafoo.jar`
/// 这种粘连写法照旧算 —— 和从前那个前缀语义对齐，不多不少）。
bool fileNameHasToken(const QString &fileName, const QString &token) {
    if (token.isEmpty()) {
        return false;
    }
    const QString wanted = token.toLower();
    const auto hit = [&wanted](const QString &word) {
        return word == wanted || word.startsWith(wanted);
    };
    QString word;
    for (const QChar character : fileName.toLower()) {
        if (character.isLetterOrNumber() && character.unicode() < 128) {
            word += character;
            continue;
        }
        if (hit(word)) {
            return true;
        }
        word.clear();
    }
    return hit(word);
}

}  // namespace

bool MainWindow::instanceHasMod(const QString &versionId, const QString &nameToken) const {
    const QString modsDir =
        AppPaths::versionDir(versionId) + QLatin1Char('/') +
        ResourceCatalog::folderForKind(ResourceCatalog::Kind::Mod);
    // 只认 `.jar`：被禁用的那些现在叫 `.jar.disabled`，扫不到 —— 那正是禁用的意思。
    const QStringList jars = QDir(modsDir).entryList({QStringLiteral("*.jar")}, QDir::Files);
    for (const QString &jar : jars) {
        if (fileNameHasToken(jar, nameToken)) {
            return true;
        }
    }
    return false;
}

bool MainWindow::instanceShaderUsable(const QString &versionId) const {
    // ① 加载器**就是** OptiFine（它自己就认 shaderpacks/）。加载器名走 instanceLoaderName()
    //    而不是 `displayParts().loaderName`：后者只有我们自己的安装器会写，而用户那两个
    //    OptiFine 实例（`1.20.1-OptiFine_I6` / `1.20.2-OptiFine_I7_pre1`）是 PCL 装的、
    //    meta 里没有那一格 —— 它们又没有 mods/ 目录，于是①和②两条同时落空，光影包那一页
    //    在**真的装了 OptiFine** 的实例上说"用不上"（2026-09-27 连同「模组」一起修的）。
    //    比对基准走 VersionManager::loaderDisplayName()，不写一遍字面量。
    const QString loader = instanceLoaderName(m_versionManager->displayParts(versionId),
                                             m_versionManager);
    if (loader == VersionManager::loaderDisplayName(VersionManager::LoaderKind::OptiFine)) {
        return true;
    }
    // ② mods/ 里装了一个"加载光影的模组"。这三个名字**都是用户盘上真见过的**：
    //    `iris`（Fabric / NeoForge / Quilt 上那个）、`oculus`（Forge 上那个 Iris 移植 ——
    //    「落幕曲」「香草纪元」「魔改版ATT2」三个整合包用的都是它）、`optifine`
    //    （1.12.2 及以前 OptiFine 是**当普通模组装进 mods/** 的，
    //    `OptiFine_1.12.2_HD_U_G5.jar`）。三条原来一条都不认。
    // NOTE: 没有 `optifabric`（Fabric 上那个把 OptiFine 塞进来的桥）：它要求 OptiFine
    //       的 jar 也在 mods/ 里，所以上面那条 `optifine` 本来就盖住了它。
    static const QStringList loaders = {
        QStringLiteral("iris"),
        QStringLiteral("oculus"),
        QStringLiteral("optifine"),
    };
    for (const QString &token : loaders) {
        if (instanceHasMod(versionId, token)) {
            return true;
        }
    }
    return false;
}

bool MainWindow::instanceModsUsable(const QString &versionId) const {
    // ① 基于一个模组加载器。判据走 instanceLoaderName()（**不是** displayParts().loaderName
    //    —— 那一格只有我们自己的安装器会写，理由写在那个函数的说明里）。
    if (!instanceLoaderName(m_versionManager->displayParts(versionId), m_versionManager)
             .isEmpty()) {
        return true;
    }
    // ② mods/ 里已经有 jar。和 instanceHasMod() 一样只认 `.jar`：被禁用的那些现在叫
    //    `.jar.disabled`，扫不到 —— 那正是禁用的意思。
    const QDir modsDir(AppPaths::versionDir(versionId) + QLatin1Char('/') +
                       ResourceCatalog::folderForKind(ResourceCatalog::Kind::Mod));
    return !modsDir.entryList({QStringLiteral("*.jar")}, QDir::Files).isEmpty();
}

void MainWindow::loadServerList(InstanceSettingsDialog *dialog, const QString &versionId) const {
    if (dialog == nullptr) {
        return;
    }
    // 「文件不在盘上」（Missing）不算 problem：那一页会说「这个实例还没有加过服务器。」
    // —— 那正是实话。只有"文件在但是读不动"才把原因递过去。
    dialog->setServerList(serverListOf(versionId), serverListProblem(versionId));
}

QList<QPair<QString, QString>> MainWindow::serverListOf(const QString &versionId) const {
    QList<NbtUtil::ServerEntry> entries;
    QString problem;
    NbtUtil::readServerList(AppPaths::versionDir(versionId) + QStringLiteral("/servers.dat"),
                            &entries, &problem);
    QList<QPair<QString, QString>> servers;
    servers.reserve(entries.size());
    for (const NbtUtil::ServerEntry &entry : entries) {
        servers.append(qMakePair(entry.name, entry.address));
    }
    return servers;
}

QString MainWindow::serverListProblem(const QString &versionId) const {
    QList<NbtUtil::ServerEntry> entries;
    QString problem;
    NbtUtil::readServerList(AppPaths::versionDir(versionId) + QStringLiteral("/servers.dat"),
                            &entries, &problem);
    return problem;
}

namespace {
/// 盘上缓存里那一份（「上次问到」的样子）→ 一行该显示成什么样。
///
/// ★ 两条路都走它：真结果还没回来的 prefill（pushServerPingRows）和"这一页现在没有趟在
///   管、先把缓存摆上"（serverCachedRows）。两处各写一遍的话，改了一处另一处就漂了 ——
///   而这两处摆的本来就是**同一件东西**（上一次问到的样子）。
InstanceSettingsDialog::ServerRowStatus serverRowFromCache(const ServerStatusCache::Entry &entry) {
    InstanceSettingsDialog::ServerRowStatus row;
    row.tested = true;
    row.fromCache = true;
    row.online = entry.online;
    // 没连上那一档里**是不是超时**（见 Entry::timedOut）：界面上按它决定写「超时」还是
    // 「没连上」—— 和当场测出来那一档走的是同一个判据、同一句字（2026-09-27 用户
    // 「‘上次’两个字也不要」之后，那两处连前缀都没有分别了）。
    row.timedOut = entry.timedOut;
    row.latencyMs = entry.latencyMs;
    row.playersOnline = entry.playersOnline;
    row.playersMax = entry.playersMax;
    row.motdRaw = entry.motdRaw;
    // 悬停提示用的是**洗过**那一份，缓存里只存了原串 ⇒ 这儿现洗一遍（纯函数，
    // 见 ServerPing::cleanMotd）。不洗的话提示里会带一串 `§a` 码，读起来是乱码。
    row.motd = ServerPing::cleanMotd(entry.motdRaw);
    if (!entry.iconPng.isEmpty()) {
        QPixmap icon;
        // 解不出来就**不画图标**（而不是画一张破图）：理由同 pushServerPingRows 里那段。
        if (icon.loadFromData(entry.iconPng, "PNG")) {
            row.icon = QIcon(icon);
        }
    }
    return row;
}

/// 这份地址表在盘上缓存里那一份（键是下标，只含 **valid** 的那些）。**一次一趟读盘**：
/// 下面那个函数和 beginServerPingRun() 共用它，各读一遍的话，"摆到屏幕上的"和"垫在底下的"
/// 就是两次读盘的结果（中间用户或者游戏往盘上写了一条，两处就对不上了）。
///
/// NOTE: 不是 MainWindow 的成员：它只用 ServerStatusCache，而返回类型
///       `InstanceSettingsDialog::ServerRowStatus` 是嵌套类型，MainWindow.hpp 只前向声明了
///       那个类（在头里写出这个返回类型就得把 ui/ 的头拉进去）。
QHash<int, ServerStatusCache::Entry> serverCachedEntries(const QStringList &addresses) {
    // 只读盘上那份缓存，**不问网络**（见 ServerStatusCache：一台一个文件、读侧不写）。
    QHash<int, ServerStatusCache::Entry> cached;
    for (int i = 0; i < addresses.size(); ++i) {
        const ServerStatusCache::Entry entry = ServerStatusCache::load(addresses.at(i));
        if (entry.valid) {
            cached.insert(i, entry);
        }
    }
    return cached;
}

/// 这份地址表现在该显示成什么样（**只读缓存、不问网络**）。换页那一支用它先把缓存里
/// 那一份摆上，免得新那一页在"上一趟收摊"那个空档里是一片"还没测"。
/// 摆法和 pushServerPingRows() 里那一档同源（都走 serverRowFromCache()）。
QList<InstanceSettingsDialog::ServerRowStatus>
serverCachedRows(const QStringList &addresses) {
    const QHash<int, ServerStatusCache::Entry> cached = serverCachedEntries(addresses);
    QList<InstanceSettingsDialog::ServerRowStatus> rows;
    rows.reserve(addresses.size());
    for (int i = 0; i < addresses.size(); ++i) {
        InstanceSettingsDialog::ServerRowStatus row;
        const auto it = cached.constFind(i);
        if (it != cached.constEnd()) {
            row = serverRowFromCache(it.value());
        }
        rows.append(row);
    }
    return rows;
}
} // namespace

void MainWindow::pingServers(InstanceSettingsDialog *dialog, const QString &versionId) {
    if (dialog == nullptr) {
        return;
    }
    const QList<QPair<QString, QString>> servers = serverListOf(versionId);
    if (servers.isEmpty()) {
        // 一台都没有（或者那份文件读不动）：那一页本来就摆着一句"还没有加过服务器"，
        // 没有什么可问的。**不弹话** —— 点「测速」不是错误操作。
        return;
    }

    QStringList addresses;
    addresses.reserve(servers.size());
    for (const QPair<QString, QString> &server : servers) {
        addresses.append(server.second);
    }

    // ---------- 已经有一趟在跑 ----------
    //
    // 两种时候会走到这儿：①「测速」那颗按钮点得快（它自己也防了一手，但自动测速那条路上
    // 没有那颗按钮挡着）；②用户切到别的页又切回服务器列表（每一趟"点进去"都发一次
    // serverPageEntered()）。两件事都不该让这一趟重开 —— 重开的话每一台都要重新等一次
    // 超时，而且**已经问回来的那几台会被打回"还没测"**（那一张表看着像倒退了）。
    //
    // ★★ 2026-09-27：但**只有地址表一模一样时**才能这样接回来。结果是**按下标**摆的
    //    （第 i 个结果 → 第 i 行），份数一样、内容不一样的两份表（换了实例、改了
    //    servers.dat 再按「刷新」、按「测速」…）按下标摆就是**张冠李戴**：B 那一页的
    //    第 3 行会显示 A 的第 3 台服务器的延迟和 MOTD，而且看着跟真的一样。
    if (m_serverPing.active) {
        if (m_serverPing.addresses == addresses) {
            // 这一次的 dialog 可能**不是**上一趟那一个（用户把设置页关掉、又重开了一个）：
            // 换过去，然后把已经攒下的结果摆上这一页。
            m_serverPing.dialog = dialog;
            pushServerPingRows();
            dialog->setServerPinging(true);
            return;
        }
        // 不一样 ⇒ **这一趟的结果一台都别往这一页上摆**（上面那种张冠李戴）。但也不能现在
        // 就换账：这一趟的结果还可能在队列里（Pinger 的 `m_running` 那时候已经是假了，
        // 见 ServerPingRun::active 那段），回来时按的还是**这一份**地址表的下标。
        // 所以只把"新那一页要测什么"记下来，等这一趟收摊（见 onServerPingAllFinished）。
        // NOTE: dialog 也一起换成空的：这一趟剩下的结果只落盘上那份缓存、不碰界面。
        m_serverPing.dialog = nullptr;
        m_serverPing.pendingAddresses = addresses;
        m_serverPing.pendingDialog = dialog;
        m_serverPing.pendingVersionId = versionId;
        // 新那一页先把盘上缓存那一档摆上 —— 那份东西和哪一趟在跑无关，让这一页在这个
        // 空档里也不是一片"还没测"。
        dialog->setServerStatuses(serverCachedRows(addresses));
        dialog->setServerPinging(true);
        return;
    }

    beginServerPingRun(dialog, versionId, addresses);
}

void MainWindow::beginServerPingRun(InstanceSettingsDialog *dialog, const QString &versionId,
                                    const QStringList &addresses) {
    // ---------- 开一趟新的 ----------
    //
    // ★ 顺序：**先把地址和盘上缓存读好**（这一段是纯读盘，几 KB，不碰网络），再把表摆上，
    //   最后才让线程跑起来。反过来（先 start 再摆表）的话，第一台几乎必然在"表还没摆好"
    //   的时候就回来了 —— 那一发是按下标写某一行的，行还不在，结果就丢了。
    ServerPingRun run;
    run.dialog = dialog;
    run.versionId = versionId;
    run.addresses = addresses;
    // 上一次问到的那份（可能没有）。它是**垫底**的：真结果回来之前先让那一格有东西，
    // 回来之后整条盖掉（用户 2026-09-27 原话：「测出来的motd和图案保留在缓存里」）。
    run.cached = serverCachedEntries(addresses);
    run.active = true;
    m_serverPing = run;

    pushServerPingRows();
    if (dialog != nullptr) {
        dialog->setServerPinging(true);
    }
    // 整趟交给后台线程（一台问完回来一台）。**这一句立刻返回** —— 用户在这几秒里翻页、
    // 点别的按钮、关掉这一页，都不受它影响（见 ServerPing::Pinger 文件头那段）。
    m_serverPinger->start(run.addresses);
}

void MainWindow::pushServerPingRows() {
    InstanceSettingsDialog *dialog = m_serverPing.dialog;
    if (dialog == nullptr) {
        // 那一页已经关掉了（用户在这几秒里关的）。结果照旧落缓存（见 onServerPingOne），
        // 界面上一格都不碰 —— 这正是 ServerPingRun::dialog 是弱引用的那件事。
        return;
    }
    // 三样东西摆在一张表上，**一台一台决定**：
    //   fresh   —— 这一趟真的问回来了，用它（最权威）；
    //   cached  —— 还没问到，拿上一次那份垫着（2026-09-27 起那一格**不再标出**这是过去时
    //              —— 用户要「‘上次’两个字也不要」，见 serverStatusLines()）；
    //   都没有  —— 「还没测」。
    QList<InstanceSettingsDialog::ServerRowStatus> rows;
    rows.reserve(m_serverPing.addresses.size());
    for (int i = 0; i < m_serverPing.addresses.size(); ++i) {
        InstanceSettingsDialog::ServerRowStatus row;
        if (m_serverPing.fresh.contains(i)) {
            const ServerPing::Status status = m_serverPing.fresh.value(i);
            row.tested = true;
            row.online = status.ok;
            row.error = status.error;
            // 超时那一档界面上只写「超时」两个字（见 ServerRowStatus::timedOut）。
            row.timedOut = status.timedOut;
            row.latencyMs = status.latencyMs;
            row.playersOnline = status.playersOnline;
            row.playersMax = status.playersMax;
            row.motd = status.motd;
            row.motdRaw = status.motdRaw;
            if (!status.iconPng.isEmpty()) {
                QPixmap icon;
                // 解不出来就**不画图标**（而不是画一张破图）：favicon 是服务器给的字节，
                // 我们只是把它画出来，它坏掉不是我们的错，但把它画成一团乱码会让人以为
                // 是我们画错了。`loadFromData` 失败时 pixmap 是空的，正好走这一档。
                if (icon.loadFromData(status.iconPng, "PNG")) {
                    row.icon = QIcon(icon);
                }
            }
        } else if (m_serverPing.cached.contains(i)) {
            row = serverRowFromCache(m_serverPing.cached.value(i));
        }
        rows.append(row);
    }
    dialog->setServerStatuses(rows);
}

void MainWindow::onServerPingOne(int index, const ServerPing::Status &status) {
    if (!m_serverPing.active) {
        // 这一趟已经收摊了（用户把设置页关掉之后我们清过状态）。照收，不写。
        return;
    }
    const QString address = m_serverPing.addresses.value(index);
    if (address.isEmpty()) {
        return;
    }

    // ---------- 先落缓存，再上屏 ----------
    //
    // 这是个**后台线程发过来的结果**：用户随时可能把这一页关掉、把这个窗口关掉。先写盘，
    // 那几个字节就是"下一次打开这一页先摆什么"的全部依据（见 ServerStatusCache）。
    // 写盘失败是静默的（那是个锦上添花的东西，见它的声明）。
    ServerStatusCache::Entry entry;
    entry.valid = true;
    entry.motdRaw = status.motdRaw;
    entry.iconPng = status.iconPng;
    entry.online = status.ok;
    // 「是不是超时」也要落盘：界面上那一格对超时只写两个字，而缓存那一份要能说出**同一句**
    // 话（用户 2026-09-27：「服务器那个会显示上次:没连上，也改成超时」；稍后又说「‘上次’
    // 两个字也不要」⇒ 这两处现在**连前缀的分别都没有了**，全靠这一位对齐）。
    entry.timedOut = status.timedOut;
    entry.latencyMs = status.latencyMs;
    entry.playersOnline = status.playersOnline;
    entry.playersMax = status.playersMax;
    ServerStatusCache::save(address, entry);

    m_serverPing.fresh.insert(index, status);
    pushServerPingRows();
}

void MainWindow::onServerPingAllFinished() {
    if (m_serverPing.dialog != nullptr) {
        m_serverPing.dialog->setServerPinging(false);
    }
    // ★ `fresh` / `cached` **留着不清**：这一趟问到的那些（以及垫底的那些）正是"这一页
    //   现在该显示的样子"，下一次 pushServerPingRows() 还要靠它们。清掉的只是"在跑"这
    //   一位 —— 下一次 pingServers() 会整份重来（那时候缓存里已经是刚写下去的那份了）。
    m_serverPing.active = false;

    // ★★ 2026-09-27：这一趟跑着的时候用户**换了一份地址表**（换了实例 / 改了 servers.dat
    //    再按「刷新」）。那时候没法立刻改账（结果还在队列里、按下标写的是这一份表），
    //    所以那件事记在这儿等现在办（见 pingServers 里那段）。
    // NOTE: `pendingAddresses` 空 = 没有欠着的活，绝大多数趟都是这么收尾的。
    if (!m_serverPing.pendingAddresses.isEmpty()) {
        const QStringList addresses = m_serverPing.pendingAddresses;
        InstanceSettingsDialog *dialog = m_serverPing.pendingDialog;
        const QString versionId = m_serverPing.pendingVersionId;
        m_serverPing.pendingAddresses.clear();
        m_serverPing.pendingDialog = nullptr;
        m_serverPing.pendingVersionId.clear();
        // 那一页已经关掉了（QPointer）就什么都不做 —— 没人看，等用户再进去时会照常重问
        // 一遍（那时候没有趟在跑了，走的就是"开一趟新的"那条路）。
        if (dialog != nullptr) {
            // 「上一趟的账」到这一刻整份作废：地址表、真结果、垫底的缓存全都是**别人**的。
            // beginServerPingRun() 会整份新建（含 active = true），不必在这儿先清。
            beginServerPingRun(dialog, versionId, addresses);
        }
    }
}


bool MainWindow::toggleInstanceFile(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        // 正常路径下走不到（那一行是刚列出来的），照实说明原因。
        InlineMessage::warning(m_ui->overlay(), tr("改动失败"),
                               tr("「%1」现在不在盘上。").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    const QString suffix = InstanceFilesPage::disabledSuffix();
    const QString target = info.fileName().endsWith(suffix, Qt::CaseInsensitive)
                               ? path.left(path.size() - suffix.size())
                               : path + suffix;
    if (QFileInfo::exists(target)) {
        InlineMessage::warning(
            m_ui->overlay(), tr("改动失败"),
            tr("「%1」已经有一个了 —— 先把那一份删掉或者改个名，再动这一份。")
                .arg(QDir::toNativeSeparators(target)));
        return false;
    }
    // 走 QFile::rename 而不是 QDir 那套：文件、目录它都能搬，而且同一个卷里改名不搬数据。
    if (!QFile::rename(path, target)) {
        InlineMessage::warning(m_ui->overlay(), tr("改动失败"),
                               tr("没能改「%1」的名字：多半是它正被游戏占着。先把游戏关掉再试。")
                                   .arg(QDir::toNativeSeparators(path)));
        return false;
    }
    appendLog(target.endsWith(suffix, Qt::CaseInsensitive)
                  ? tr("禁用文件：%1").arg(QDir::toNativeSeparators(path))
                  : tr("启用文件：%1").arg(QDir::toNativeSeparators(target)));
    return true;
}

bool MainWindow::overlayAcceptsNewPage() {
    OverlayHost *overlay = m_ui->overlay();
    if (overlay == nullptr || !overlay->isOpen()) {
        return true;
    }
    // 挡在这儿的是**已经开着的那一页**（一层只放得下一个页面，见 OverlayHost.hpp 那段）。
    // 它可能是"正铺在眼前"（用户点的是底栏那些没被盖住的按钮），也可能是"收着"——用户在
    // 别的选项卡上逛的时候，顶上那四颗导航按钮会把页面收起来，而**收着也照样占着这一层**。
    //
    // NOTE: **这一条不能不出声。** 在这之前这两个入口一个判据都没有：`execPage()` 拿到
    //       nullptr 直接返回 Rejected，对话框建了、一个字节没写、原地销毁 —— 用户点了实例
    //       行（或者组行那颗齿轮），屏幕上**什么都没发生**，也没有任何提示。挡住用户操作的
    //       事必须说清楚，而且得说清"怎么办"：收着的页面连返回箭头都不认
    //       （见 OverlayHost::dismiss()：那一发落在它头上 = "我在主页点了一下主页，下载页
    //       那个详情页自己没了"），所以唯一的出路是回它自己那颗选项卡上去关。
    const QString title = overlay->pageTitle();
    // ★ 2026-09-27：标题**可能是空的** —— 这一层开着的若是**提示**（`promptOpen()`）而不是
    //   整页，pageTitle() 返回空串，照老写法那句正文会变成「「」那一页还开着」。
    //   NOTE: 这一档（提示开着、又从某个页面入口要开新页）原本恰恰是**点了没反应**的那条路
    //         之一：底栏那两颗按钮不在覆盖层底下、提示按不住它们。现在由
    //         OverlayHost::setPromptBlockedWidget() 把底栏跟着禁掉了，所以到不了这儿；
    //         真到了（将来多一个入口）也得说句人话，而不是一对空引号。
    if (title.isEmpty()) {
        InlineMessage::warning(overlay, tr("先关掉现在这一条提示"),
                               tr("现在有一条提示开着，先说清它或者按 Esc 关掉它，再开这个。"));
        return false;
    }
    InlineMessage::warning(overlay, tr("先关掉现在这一页"),
                           tr("「%1」那一页还开着。切到别的选项卡时它会收起来，但它一直占着"
                              "这个位置 —— 顶上那四颗导航按钮一次只放得下一个页面。\n\n"
                              "回到「%1」那一颗选项卡，点左上角的「←」把它关掉，再回来开"
                              "这个。")
                               .arg(title));
    return false;
}

bool MainWindow::deleteInstanceFile(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        InlineMessage::warning(m_ui->overlay(), tr("删除失败"),
                               tr("「%1」现在不在盘上。").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    const bool isDir = info.isDir();
    const QString question = isDir
                                 ? tr("把文件夹「%1」连同里面的东西一起删掉？删了就没了。")
                                       .arg(info.fileName())
                                 : tr("把「%1」从盘上删掉？删了就没了。").arg(info.fileName());
    // 默认**不**落在「删除」上：误按一下回车不该把用户的东西删掉（和"从列表里移除文件夹"
    // 那两处一个方向）。
    if (!InlineMessage::question(m_ui->overlay(), tr("删除文件"), question, tr("删除"),
                                 tr("取消"), false)) {
        return false;
    }
    // **不能**用 `QDir::removeRecursively()`：实测（见 FolderManager.cpp 里那段）它会跟着
    // junction / 符号链接走进目标目录，把**实例目录外面**的东西删掉。把 `saves` 挂到网盘、
    // 把 `mods` 挂到另一块盘都是常见做法，而这一页的目录行（模组 / 资源包 / 数据包 / 光影包
    // / 原理图那几页）都挂着删除按钮 ⇒ 用户点的是"删掉这个文件夹"，落点却跨出了实例。
    // 另外两条销毁路径（删游戏文件夹、删版本）走的也是这个函数。
    const bool removed =
        isDir ? removeTreeWithoutFollowingLinks(path) : QFile::remove(path);
    if (!removed) {
        InlineMessage::warning(m_ui->overlay(), tr("删除失败"),
                               tr("没能删掉「%1」：多半是它正被游戏占着。先把游戏关掉再试。")
                                   .arg(QDir::toNativeSeparators(path)));
        return false;
    }
    appendLog(tr("删除文件：%1").arg(QDir::toNativeSeparators(path)));
    return true;
}

// ---------- 「文件管理」那几页的图标（2026-09-27）----------

QString MainWindow::instanceIconCacheKey(const QString &path) {
    // 路径 + 大小 + 修改时间。**三样都要**（理由见声明处那条 NOTE）：只按路径键住的话，
    // "删掉一个模组、往同一个名字上放另一个"这一形态会让那一格一直画着上一个资源的图标。
    // 分隔符用 `\n`（一个文件名里不可能有它，而且它不会和路径里那些 `|` 打架）。
    const QFileInfo info(path);
    return path + QLatin1Char('\n') + QString::number(info.size()) + QLatin1Char('\n') +
           QString::number(info.lastModified().toMSecsSinceEpoch());
}

QStringList MainWindow::instanceIconCandidatePaths(const QString &versionId) const {
    const QHash<QString, QString> dirs = instanceFileDirs(versionId);
    // 五个目录：四个能下载的（模组 / 资源包 / 数据包 / 光影包）+ 原理图。**不含存档**
    // （那一页全是目录，而一个存档不是一个能反查的资源）。原理图那一页大多是
    // `.litematic`（`isLookupCandidate()` 会把它们筛掉），但用户往里放 `.zip` 也是常事。
    const QStringList keys = {QStringLiteral("mod"), QStringLiteral("resourcepack"),
                              QStringLiteral("datapack"), QStringLiteral("shader"),
                              QStringLiteral("schematic")};
    QStringList paths;
    for (const QString &key : keys) {
        const QDir dir(dirs.value(key));
        if (!dir.exists()) {
            continue;
        }
        const QFileInfoList entries =
            dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &info : entries) {
            // 只收 `.jar` / `.zip`（禁用着的那些也算，见 isLookupCandidate）。
            if (InstanceIconLookup::isLookupCandidate(info.absoluteFilePath())) {
                paths.append(info.absoluteFilePath());
            }
        }
    }
    return paths;
}

void MainWindow::requestInstanceFileIcons(InstanceSettingsDialog *dialog, const QString &versionId) {
    if (dialog == nullptr) {
        return;
    }
    // 这一页现在是谁 —— 结果回来时按它认人（见 InstanceIconRun::dialog 那段）。
    m_instanceIcons.dialog = dialog;
    m_instanceIcons.versionId = versionId;
    m_instanceIcons.paths = instanceIconCandidatePaths(versionId);
    // 已经知道的先摆上：这一趟要问的那几个还在路上，但上一趟问到的、以及这一趟里
    // "已经问过、没有"的那些，现在就该是对的（不是等整个后台线程跑完才一起摆）。
    pushInstanceFileIcons();

    startInstanceFileIconLookup();
}

void MainWindow::startInstanceFileIconLookup() {
    // ★ 已经有一趟在跑就**到此为止**：同一个实例的目录列表前后差不了多少，重开一趟等于
    //   把已经在读的那几十兆重读一遍、把已经在问的那几发重问一遍。
    //   NOTE: 这一条早退**不再丢下新开的那一页** —— 那一趟收尾时会拿"这一趟是给谁问的"
    //         和"现在这一页是谁"比一比，不一样就回头替新那页再走一遍这里（见
    //         onInstanceIconLookupFinished 末尾那段）。
    if (m_instanceIcons.active) {
        return;
    }

    // 还没问过的那些：会话缓存里两样都没有的。
    QStringList missing;
    for (const QString &path : m_instanceIcons.paths) {
        const QString key = instanceIconCacheKey(path);
        if (m_instanceFileIcons.contains(key) || m_instanceIconMissing.contains(key)) {
            continue;
        }
        missing.append(path);
    }
    if (missing.isEmpty()) {
        // 一个都不缺（多半是刚关掉又打开、而且上一趟已经把这一批问完了）。
        return;
    }

    InstanceIconLookup::Options options;
    // 三个口子和 ResourceStore / VersionManager 那两处**同一份设置**（CurseForge 的 key
    // 是用户自己填的那一份；两个 override 只有验证工程会写）。
    options.curseForgeApiKey = m_settings->curseForgeApiKey();
    options.modrinthApiBaseOverride = m_settings->modrinthApiBaseOverride();
    options.curseForgeApiBaseOverride = m_settings->curseForgeApiBaseOverride();

    // 上一趟那个线程对象还挂着（跑完了，但 deletion 还排着队）：先放它走 —— 不收拾的话
    // 每问一次就在父对象底下攒一个僵尸 QThread（同 ServerPing::Pinger::start() 那段）。
    if (m_instanceIconThread != nullptr) {
        m_instanceIconThread->deleteLater();
        m_instanceIconThread = nullptr;
    }

    m_instanceIcons.active = true;
    // ★★ 2026-09-27：**这一趟问的是哪几个文件**记一份，回调里只认这一份（理由见
    //    m_instanceIconRunPaths 声明处那段）。上面那道 `active` 闸排在"换 paths"之后，
    //    所以这一句必须放在这里、而不是跟着 paths 一起在最上面写 —— 它要写的是**这一趟**
    //    的事实，不是"最后一次请求"的事实。
    m_instanceIconRunPaths = missing;
    m_instanceIconRunVersionId = m_instanceIcons.versionId;
    appendLog(tr("正在认「%1」里的资源图标（%2 个文件）...")
                  .arg(instanceDisplayName(m_instanceIcons.versionId))
                  .arg(missing.size()));
    // `QThread::create` 那个重载收一个函数对象（Qt 5.10 起），省掉"再写一个 QThread 子类
    // 并在 .cpp 里 moc"。结果**回主线程那一句走 invokeMethod 的 functor 重载**（而不是
    // 一个带自定义类型参数的信号）：那样连元类型都不用登记 —— 那个 functor 是被拷进事件
    // 里带过去的，中间不过 QVariant（`InstanceIconLookup::Result` 里那两层容器要按
    // Q_DECLARE_METATYPE 一层层登记才对，漏一层就是**静默**收不到，见 ServerPing 里那条）。
    m_instanceIconThread = QThread::create([this, missing, options]() {
        const InstanceIconLookup::Result result = InstanceIconLookup::lookup(missing, options);
        QMetaObject::invokeMethod(
            this, [this, result]() { onInstanceIconLookupFinished(result); },
            Qt::QueuedConnection);
    });
    m_instanceIconThread->setParent(this);
    m_instanceIconThread->start();
}

void MainWindow::onInstanceIconLookupFinished(const InstanceIconLookup::Result &result) {
    m_instanceIcons.active = false;
    // ★★ 2026-09-27：这一趟**问过的那批文件**，以及"结果回来时这一页还是不是它"。
    //    下面②那个循环**只许读这一份**：m_instanceIcons.paths 在这一趟跑着的时候可能
    //    已经被"A 跑着、用户又开了 B 那一页"换掉了，照它写负缓存等于把 A 的"没找到"记到
    //    B 的文件头上（写进去就不清，B 这一整个会话都没有图标）。
    const QStringList runPaths = m_instanceIconRunPaths;
    const QString runVersionId = m_instanceIconRunVersionId;
    m_instanceIconRunPaths.clear();
    m_instanceIconRunVersionId.clear();

    // ---------- ① 认出来的：换成一个"资源身份"，交给 ResourceStore 去取那张图 ----------
    //
    // 到这里为止拿到的是"这个文件的哈希属于哪个项目"，**图还没有**。取图的活交给
    // ResourceStore（它有一套现成的两级缓存 + 条件请求 + 挑 webp/PNG 那套判断），
    // 这一层只是把身份递给它。
    QSet<QString> foundPaths;
    for (const InstanceIconLookup::Found &found : result.found) {
        // ★ 2026-09-27：反查回来的是**整个项目对象**（`Found::hit`），不再只是项目号 +
        //   两条图标地址 —— 名字 / slug / 简介 / 支持的游戏版本都在同一份字节里，
        //   也就是"点名字进它自己的那一页"要的那几样（见 setFileResource() 那段）。
        //   多读几栏，**没有多发一个请求**。
        const ResourceCatalog::Hit hit = found.hit;
        // 这一条文件属于哪一格（`<来源>-<项目号>`）—— 图标回来时按它找回**行**（见
        // onResourceIconReady）。同一个资源被反查出来两次（同一个 jar 摆了两份）时
        // 两条路径都记上：两张图本来就该是同一张。
        const QString iconKey = hit.iconCacheId();
        QStringList &paths = m_instanceIcons.pathsByIconKey[iconKey];
        if (!paths.contains(found.path)) {
            paths.append(found.path);
        }
        foundPaths.insert(found.path);
        // 这一条是哪个资源（会话缓存，按"路径 + 大小 + 修改时间"记 —— 同一个名字上换了
        // 一个文件就自然作废，见 instanceIconCacheKey）。整份 Hit 存进 m_resourceHits：
        // 点名字开那一页走的就是**下载页点一行**那条路（openResourceDetailPage 按这把
        // 钥匙取标题 / 简介 / slug / 游戏版本；两处的分别只有"来路"那一位，见 DetailSource），
        // 这一层不需要再为它多存一份。
        m_instanceFileIdentities.insert(instanceIconCacheKey(found.path), iconKey);
        m_resourceHits.insert(iconKey, hit);
        // 已经在问 / 已经在缓存里的话 requestIcon() 自己会处理（可能**同步**就把 iconReady
        // 发出来了 —— 那时候上面那一行必须先写完，见它那条 NOTE）。
        m_resourceStore->requestIcon(hit);
    }

    // ---------- ② 没认出来的那一批 ----------
    //
    // ★ 判据是 `result.problem`：只有**整趟完整跑完**了才把"没有"记下来。断网那一趟
    //   要是也记，用户这一整个会话都再也看不到图标了 —— 而那本来是"网好了就会有"
    //   （见 m_instanceIconMissing 那段）。problem 非空时还有一个后果：①里那些**认出来了
    //   的**照旧记账（那是真的），只是不再问第二遍。
    const bool complete = result.problem.isEmpty();
    if (!complete) {
        // 只记一句日志、**不弹窗**：图标取不到是常态（自己改过的 jar、作者没传过的、
        // 断网的），画个占位拼图就是正确的界面 —— 它挡不住用户做任何事。
        appendLog(tr("资源图标没查完：%1").arg(result.problem));
    }
    for (const QString &path : runPaths) {
        if (foundPaths.contains(path)) {
            continue;
        }
        if (complete) {
            m_instanceIconMissing.insert(instanceIconCacheKey(path));
        }
        // 记不上也要摆：这一条现在还是一张占位图（见 InstanceFilesPage::iconForPath），
        // 而"这一条问过了、没有"这件事在这一页活着的时候得记住 —— 不然用户点一下
        // 「刷新」，那一格又变回"还没问"的样子。
    }
    // 图标地址那一发没成 —— **和上面那句分开说**：那一刻身份都已经认出来了，缺的只是图，
    // 而那几格退回占位拼图就是正确的界面（不弹窗，理由同上）。这句话得说，是因为"认出来了
    // 但没图"和"压根没认出来"在屏幕上是一个样子，而这两件事的下一步不一样。
    if (!result.iconProblem.isEmpty()) {
        appendLog(tr("有几个资源的图标地址没取到：%1").arg(result.iconProblem));
    }
    if (result.unreadable > 0) {
        appendLog(tr("有 %1 个文件读不动（多半被别的程序占着），这一批里没有它们的图标。")
                      .arg(result.unreadable));
    }
    pushInstanceFileIcons();
    // ★★ 2026-09-27：这一趟跑着的时候用户**换了页**的话，替新那一页补一趟。
    //
    // 为什么非补不可：换页那次请求撞在 `active` 闸上直接 return 了（见
    // startInstanceFileIconLookup 那条早退），而这里刚从回来的这一趟是**上一页**的 ——
    // 没有人替新那页问的话，它的图标要等用户再进一次文件管理页才出现。
    // 判据看的是**趟**（runVersionId）而不是 `m_instanceIcons.paths` 变没变：同一个实例
    // 反复进出这一页不该重跑一趟（那正是那条 early-return 想省的）。
    // NOTE: 这一页已经关掉了（dialog 是 QPointer，关了就是空）就什么都不做 —— 没人看。
    // NOTE: 不会再套一层循环：补跑那一趟的 runVersionId 就是**当时**的 versionId，它收尾时
    //       这一条判据必然相等（同一页期间路径表也不会自己变）。
    if (m_instanceIcons.dialog != nullptr && m_instanceIcons.versionId != runVersionId) {
        startInstanceFileIconLookup();
    }
}

void MainWindow::pushInstanceFileIcons() {
    InstanceSettingsDialog *dialog = m_instanceIcons.dialog;
    if (dialog == nullptr) {
        return;
    }
    for (const QString &path : m_instanceIcons.paths) {
        const QString key = instanceIconCacheKey(path);
        const auto cached = m_instanceFileIcons.constFind(key);
        if (cached != m_instanceFileIcons.constEnd()) {
            dialog->setFileIcon(path, cached.value());
        } else if (m_instanceIconMissing.contains(key)) {
            // 空 pixmap = "问过了、没有" ⇒ 那一格摆占位拼图（见 setIconForPath）。
            dialog->setFileIcon(path, QPixmap());
        }
        // 两样都没有 = 还没问回来 ⇒ **什么都不做**，那一格现在摆的就是占位图。

        // ★ 行上的名字点不点得动（2026-09-27）：认得出来（`m_instanceFileIdentities` 里有
        //   它、而且那份 Hit 带着名字）才给那一行带上链接的样子，否则给**空串**。
        //   空串这一支不是"没话说"，而是"把上一趟可能戴上的摘掉"：同一个路径换了内容
        //   （大小/时间变了）时键也换了，这一趟的负结果必须覆盖上一趟的正结果，不然
        //   那一行会顶着一个已经不对的资源名字、点进去是另一个东西。
        const auto identity = m_instanceFileIdentities.constFind(key);
        dialog->setFileResource(
            path, identity == m_instanceFileIdentities.constEnd()
                      ? QString()
                      : m_resourceHits.value(identity.value()).title);
    }
}

bool MainWindow::applyInstanceSettings(const QString &versionId,
                                      const InstanceSettingsDialog &dialog) {
    // ★ 分组归属的**老键必须在写 meta 之前取**。下面那一句 `ensureInstanceId()` 会给
    //   "还没有稳定 id"的老实例补一个，而归属是按 `instanceKey()`（有 id 用 id、没有就
    //   退回目录名）存的 ⇒ 补上 id 之后同一个实例的键**换了一个**，不跟着搬它就会
    //   **静默掉出原来那一组**。用户看到的是"我只改了个内存设置，它自己从组里出来了"，
    //   而且没有任何日志、没有任何弹窗 —— 事后才发现。
    //
    //   NOTE: 已经走过改名那条路的实例到这里时 id 早就有了（`renameInstance()` 那一步
    //         生成的），oldKey == newKey —— `rekey()` 自己认得出这是空操作，一个字节
    //         都不写盘（见它开头那一句）。
    const QString oldGroupKey = instanceKey(versionId);

    // 先把稳定 id 落下去。**必须排在最前**：这一项覆盖要"跟着这个实例走"，而实例的
    // 身份就是那个 id（目录名会被改，改名之后按目录名记的东西全丢）。失败就不写 ——
    // 写成一次"靠目录名认出来的覆盖"比这次没写成糟得多。
    if (m_versionManager->ensureInstanceId(versionId).isEmpty()) {
        InlineMessage::critical(m_ui->overlay(), tr("实例设置没能保存"),
                              tr("「%1」的 wtfml.meta 写不进去，这次的设置没有保存。\n\n"
                                 "（这份文件可能被别的程序占着，或者它所在的目录不可写。）")
                                  .arg(instanceDisplayName(versionId)));
        return false;
    }

    // 键换过了（老实例刚拿到 id）就把归属搬过去 —— 这一步**必须在这里**，不能挪到
    // 下面那次写 meta 之后：`ensureInstanceId()` 已经把 id 落盘了，也就是说"键已经换了"
    // 是既成事实，再往下走无论成败，归属都只剩这一条路能保住。
    //
    // NOTE: 搬失败**不当成"设置没保存"**（照 applyRename 的先例）：那会让用户以为要重来
    //       一次，而设置那半边马上就写好了。说清楚哪一半没成，让他自己去「分组…」再归一次。
    const QString newGroupKey = instanceKey(versionId);
    QString groupError;
    if (!m_groupStore->rekey(oldGroupKey, newGroupKey, &groupError)) {
        InlineMessage::warning(m_ui->overlay(), tr("分组归属没跟着搬"),
                             tr("「%1」的分组归属没能搬过去：\n%2\n\n"
                                "可以在「分组…」里重新归一次。")
                                 .arg(instanceDisplayName(versionId), groupError));
    }

    // "值 → 要写进 meta 的字段"整批交给 javaOverrideMetaFields()：五项里"跟随全局"
    // 的要**删键**而不是写一个等于全局的值（写了的话用户之后改全局，这个实例就不跟着
    // 走了，而他明明选的是「跟随全局」），追加参数为空时那个键也要删。这套判断放在
    // settings/ 那边是有意的 —— 在这里就没法被断言盯上（这个函数收的是对话框引用，
    // 验证工程驱动不了它）。
    QJsonObject fields =
        javaOverrideMetaFields(toSettingsOverride(dialog.override()), dialog.appendJvmArgs(),
                               VersionManager::jvmArgsField());

    // 提醒开关：勾着（要提醒）就**删掉**那个键，取消勾选才写 true。
    // 和那五项同一个约定（Null = 删键）—— 写一个"等于默认值"的 false 进 meta 会让这个
    // 文件里多出一堆没信息量的键，而且"没写过"和"写过 false"从此分不开。
    fields.insert(VersionManager::skipJavaVersionWarningField(),
                  dialog.warnJavaMismatch() ? QJsonValue(QJsonValue::Null) : QJsonValue(true));

    // ---------- 图标（2026-09-26）----------
    //
    // 用户刚挑了一张图 ⇒ **先把它拷进实例目录**，再往 meta 里写"用本地那张"。顺序反过来的话，
    // 写成功、拷失败会在盘上留下一句指向一个不存在的文件的 meta（树那一格从此永远是默认图标，
    // 而文件里明明写着 local）。
    //
    // NOTE: 拷失败**不把整次保存赔进去**（只报一声、meta 里那一格保持原样）：用户改的内存 /
    //       Java 路径和图标是两件事，为前者把后者一起丢掉是替用户做主。写回去的那个值是
    //       **磁盘上现在是什么**、不是"这次没改"，所以这一次写入不会动它 —— 而要是直接写
    //       dialog.iconChoice()，用户清掉图标就会把盘上那张删了却还以为换成了默认的。
    //
    // ★ `iconBefore` 是**这一次写之前** disk 上那个值，用来回答"这次改动里包含图标吗"
    //   （见函数末尾那一发 refreshInstalledVersions()）。
    const QString iconBefore = m_versionManager->versionIcon(versionId);
    QString iconValue = dialog.iconChoice();
    const QString pickedIcon = dialog.pickedIconPath();
    if (!pickedIcon.isEmpty()) {
        QString iconError;
        if (writeInstanceIcon(versionId, pickedIcon, &iconError)) {
            iconValue = InstanceSettingsDialog::localIconValue();
        } else {
            InlineMessage::warning(m_ui->overlay(), tr("图标没能保存"), iconError);
            iconValue = m_versionManager->versionIcon(versionId);
        }
    }
    // 空串 = 「默认」= **删掉那个键**，不写一个"等于默认"的值 —— 和上面两项同一个约定。
    fields.insert(VersionManager::iconField(),
                  iconValue.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(iconValue));

    // ---------- 左栏显示哪几页（2026-09-27）----------
    //
    // 一个**顶层对象**（`{"save": false}`）—— updateVersionMeta 的合并是"这一格的整块替换"，
    // 所以不用（也不能）逐条删键：整块交上去，那一格就以这份为准。
    // 空对象（七条全显示 = 没勾掉任何一条）写 Null ⇒ **删键**，和上面三项同一个约定。
    //
    // NOTE: 这里**不校验**那几页此刻可不可用 —— 值是从对话框现读的（pageChoices()），而它
    //       本来就只写"可用且没勾"的那几条（见它那段说明）。可用性是每次现算的，不写盘。
    const QJsonObject pageChoices = dialog.pageChoices();
    fields.insert(VersionManager::settingsPagesField(),
                  pageChoices.isEmpty() ? QJsonValue(QJsonValue::Null)
                                        : QJsonValue(pageChoices));

    QString error;
    if (!m_versionManager->updateVersionMeta(versionId, fields, &error)) {
        // 数据层不弹窗是约定，所以弹在这一层。error 里已经写清了"是哪个文件、为什么"。
        InlineMessage::critical(m_ui->overlay(), tr("实例设置没能保存"), error);
        return false;
    }
    appendLog(tr("实例设置已保存：%1").arg(versionId));

    // 图标换了，就得让实例树上那一格跟着换。`updateVersionMeta()` **没有任何"meta 变了"
    // 的信号**（它是数据层，不认得上层谁在看），而写入 Meta 之后树上那一格的图标是从
    // meta 现读的 —— 不重扫的话用户回到实例页看到的还是老图标，得等下一次下载或者重启。
    //
    // 判据是"写之前那个值和现在盘上那个值一样不一样"，**不是**"这次有没有碰图标那一格"：
    // 实时写盘之后 applyInstanceSettings() 每次值一变就会被调一次（改个内存数字也算），
    // 无条件重扫等于每敲一下就全盘扫一遍目录。
    //
    // NOTE: 只有图标这一项在这儿重扫。名字那一项走 renameRequested → applyRename，
    //       那边自己会刷（`applyRename()` 里那句）。分组/置顶在下面，它们各自走
    //       setInstanceHomeGroup / pin / unpin，那三个会发 groupsChanged 把树重灌。
    if (m_versionManager->versionIcon(versionId) != iconBefore) {
        refreshInstalledVersions();
    }

    // ---------- 分组 / 置顶（2026-09-26，当天晚些时候改成"两边都显示"）----------
    //
    // 排在写 meta **之后**、`rekey()` **之后**（那一步在上面）：归属的键刚刚才定下来，在它
    // 之前写等于写到旧键上。
    //
    // 三条规则，合起来就是"用户在「分组…」和那颗勾选框上摆成什么样，结果就是什么样"：
    //   ① 老家被**用户改过** → 改老家（setInstanceHomeGroup：置顶着的那一档只改"老家"那
    //      一格，members 继续指着置顶组）；
    //   ② 原来没置顶、现在勾上了 → 置顶（记下"置顶前老家在哪一组"，老家那一条不动）；
    //   ③ 原来置顶、现在取消了   → 取消置顶（回到记着的那一组）。
    //
    // NOTE: 判"改没改"是拿对话框里那一格的值和**打开对话框时**那一格的值比，不是拿它和
    //       "现在存在哪一组"比 —— 和验证工程驱动这颗按钮时同一个口径，见 .hpp 里
    //       setGroupChoices() 那段。
    //
    // NOTE: ②③ 和 ① 现在**互不相干**了。旧语义（置顶 = 把它挪进置顶组）里这两件事抢同一格：
    //       勾上置顶会把「分组」那一格当场挪到置顶组上去（旧代码里那个 applyPinState()），
    //       于是"分组被改过"这一问必须有额外的规矩才分得清是用户改的还是置顶顺手摆的。
    //       现在那一格是老家、勾选框只管多摆一条，两者不再打架，那段规矩连同 hasPinGroup
    //       那道闸一起删了 —— 没有置顶组时勾选框本来就是灰的（setPinned()），勾不上。
    //
    // NOTE: 三条路都走**现有那几个入口**（setInstanceHomeGroup / pinInstance / unpinInstance），
    //       不在这里另写一遍 wtfml.folders 的读写：它们各自会发 groupsChanged、自己把树重灌，
    //       而且"没有置顶组时什么都不做""认不出来就落未分组"这些判断都已经在那儿了。
    //
    // NOTE: 顺序是**先改老家、再摆置顶**。这一版两种顺序算出来的答案是同一个（理由写在
    //       InstanceGroupStore::setInstanceHomeGroup() 上面那段 NOTE 里），钉死它只是为了让
    //       读代码的人不用先在脑子里把那四种组合各跑一遍。
    const QString key = instanceKey(versionId);
    const QSet<QString> knownGroupIds = knownGroupIdsOf(m_groupStore);
    const QString groupAtOpen =
        homeGroupIdOf(m_groupStore, key, m_versionManager->displayParts(versionId),
                      m_versionManager, knownGroupIds);
    const bool wasPinned = m_groupStore->isPinned(key);
    const bool nowPinned = dialog.pinned();

    // ① 老家被改过。
    if (dialog.chosenGroupId() != groupAtOpen) {
        setInstanceHomeGroup(versionId, dialog.chosenGroupId());
    }
    // ② 刚勾上置顶。
    if (nowPinned && !wasPinned) {
        pinInstance(versionId);
    }
    // ③ 原来置顶、现在取消了：回到记着的那一组（判据在本函数进来的那一刻，所以上面 ① 改
    //    没改过老家都不影响这一档 —— 换过老家之后再取消置顶，回的就是刚换的那一组）。
    if (wasPinned && !nowPinned) {
        unpinInstance(versionId);
    }
    return true;
}

/// 把一个参数按 cmd.exe 的规矩包起来（「导出启动脚本」那一串参数要用它）。
///
/// 两条规矩，都得照做：
///   - `%` 在批处理里是**变量展开**（`%PATH%`），而且写在双引号里**照样展开** ⇒ 一律写 `%%`；
///   - 含空格/制表符、空串、或者带 `&|<>()^"` 的参数要加双引号（不加的话那个参数会被 cmd
///     拆成好几段，或者整个消失）。
///
/// NOTE: 参数里真出现一个 `"` 是**没救**的 —— cmd 没有"引号里的引号"这种写法。这条路上
///       的参数来自版本 json 和用户设置（路径、UUID、玩家名），到不了那一步；真到了也只是
///       那一个参数被拆开，脚本里看得见，不是静默错。
/// NOTE: 故意**不**动 `!`：它只在"开了延迟展开"的 cmd 里有意义（脚本自己 `@echo off`，
///       默认就没开），而按延迟展开去转义（`^^!`）会让正常那一档凭空多出一个 `^`。
static QString batchQuote(const QString &arg) {
    QString escaped = arg;
    escaped.replace(QLatin1Char('%'), QStringLiteral("%%"));
    static const QString kSpecial = QStringLiteral(" \t&|<>()^\"");
    bool needsQuotes = escaped.isEmpty();
    for (const QChar &ch : escaped) {
        if (kSpecial.contains(ch)) {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) {
        return escaped;
    }
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

// ============================ 日志窗那两颗导出 ============================
//
// 用户 2026-09-28：「把日志和错误报告导出功能加上，就在那个日志窗口下方，右下角放着」。
// 三件事的分工：**界面**（两颗键、长什么样）在 ui/GameLogWindow；**文本怎么拼**（头部、
// 分段）在 util/ErrorReport；**这一片**是接线 —— 弹保存框、写盘、说结果。
//
// 为什么保存框在这一层而不是窗那一层：`ui/` 不认识数据目录（那一层的规矩，见
// GameLogWindow 的文件头），而"默认存哪儿"是个和数据目录一样属于这一层的问题。

namespace {

/// 导出那两份文件时保存框默认落在哪儿：桌面。
///
/// 退路是"我的文档"，再拿不到就给个空串（空串 = 交给保存框自己决定用上次那个地方）——
/// 比硬编一个绝对路径强：那台机器上不一定有那个目录。
QString exportDefaultDirectory() {
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktop.isEmpty()) {
        return desktop;
    }
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

/**
 * @brief 一段文本数出来是几行。**两份导出文件共用这一处**（报告头部的"共 N 行"读它）。
 *
 * NOTE: 末尾那半行（最后没有换行的那截）也算一行 —— 它和别的行一样占着屏幕上的一行，
 *       不数它的话报告里那个数字会比用户眼睛数出来的少一个。
 */
int lineCountOf(const QString &text) {
    if (text.isEmpty()) {
        return 0;
    }
    int count = text.count(QLatin1Char('\n'));
    if (!text.endsWith(QLatin1Char('\n'))) {
        ++count;
    }
    return count;
}

/**
 * @brief 把一段文本写进那个文件。
 * @return 空串 = 写成功了；否则是给用户看的原因（原样来自系统，已经能读懂）。
 *
 * QSaveFile：写不下去时**不会留下一个半截的文件** —— 导出的日志/报告是用户要拿去问人
 * 的东西，一个半截的文件发出去比没有更坏（和导出启动脚本那边同一个理由）。
 */
QString writeExportFile(const QString &path, const QString &text) {
    // 换行写成 CRLF：这两份文件的主要用途是在 Windows 上双击打开（记事本）——
    // 纯 LF 在旧一点的记事本里是一整坨。**不改内存里那一份**，只改写出去的那一份。
    QString body = text;
    body.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    body.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    const QByteArray bytes = body.toUtf8();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return file.errorString();
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        return file.errorString();
    }
    return QString();
}

}  // namespace

void MainWindow::exportGameLog(int sessionId) {
    GameLogWindow *window = m_gameLogWindows.value(sessionId, nullptr);
    if (window == nullptr) {
        return;  // 走不到：这两颗键就长在那个窗上
    }
    const QDateTime now = QDateTime::currentDateTime();
    const QString suggested =
        QDir(exportDefaultDirectory())
            .filePath(ErrorReport::suggestedLogFileName(window->instanceName(), now));
    // 保存框**挂在那个日志窗上**（不是主窗）：它是用户刚点过的那扇窗，弹出来的框该压着它，
    // 而不是被它盖住（日志窗没有父窗，是独立的顶层窗口，见它的文件头）。
    // NOTE: 不用 `DontUseNativeDialog` —— 这一颗是**导出到哪儿**，用户要的是那个熟悉的
    //       Windows 框（四处 QFileDialog 里另外那几处也是这么留的，见 setupConnections 里
    //       那一段说明）。
    const QString path = QFileDialog::getSaveFileName(
        window, tr("导出日志"), suggested,
        tr("文本文件 (*.log *.txt);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;  // 用户取消
    }
    const QString writeError = writeExportFile(path, window->allLogText());
    if (!writeError.isEmpty()) {
        appendLog(tr("导出日志失败：%1").arg(writeError));
        // 错误要弹窗（这一条是产品的硬约束）—— 弹的是启动器那一层，日志窗这时还在旁边
        // 摆着，用户回头看得到。
        InlineMessage::warning(m_ui->overlay(), tr("导出失败"),
                               tr("这个文件没能写出来（%1）：\n%2")
                                   .arg(QDir::toNativeSeparators(path), writeError));
        return;
    }
    const QString shown = QDir::toNativeSeparators(path);
    appendLog(tr("✓ 日志已导出：%1").arg(shown));
    // 成功那一下**写回那个日志窗**：用户就在那儿点的那颗键，眼睛也在那儿 —— 弹一个框
    // 还要他再点一次"知道了"，而这件事没有需要他决定的东西。写法跟着窗里已有的那几行
    // 自己补的话（"[游戏已退出，退出码：0]"），一样带方括号。
    window->appendText(tr("\n[日志已导出：%1]\n").arg(shown));
}

void MainWindow::exportGameErrorReport(int sessionId) {
    GameLogWindow *window = m_gameLogWindows.value(sessionId, nullptr);
    if (window == nullptr) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();

    // 事实从**快照**里取（理由见 m_gameLogFacts 的说明：会话一结束，那份记录就查不到了，
    // 而"崩溃之后导一份报告"正是这件事最主要的用法）。
    const GameLogFacts saved = m_gameLogFacts.value(sessionId);
    ErrorReport::Facts facts;
    facts.launcherVersion = QCoreApplication::applicationVersion();
    facts.instanceName = window->instanceName();
    facts.instanceId = saved.instanceId;
    facts.accountName = saved.accountName;
    facts.systemDescription = ErrorReport::systemDescription();
    facts.processId = saved.processId;
    facts.startedAt = saved.startedAtMs > 0 ? QDateTime::fromMSecsSinceEpoch(saved.startedAtMs)
                                            : QDateTime();
    facts.finished = saved.finished;
    facts.crashed = saved.crashed;
    facts.exitCode = saved.exitCode;

    // 两段正文都从窗里读**它自己那一份模型**：错误摘录走 `logTextFrom(Error)`（哪几档算
    // 错误只有那边一个出处），完整那份走 `allLogText()`（屏幕上筛掉的行也在里面 ——
    // 用户勾着 ERROR 的时候导出，报告里照样要是完整日志）。
    const QString errors = window->logTextFrom(GameLogWindow::LogLevel::Error);
    const QString log = window->allLogText();
    facts.errorLines = lineCountOf(errors);
    facts.totalLines = lineCountOf(log);

    const QString suggested =
        QDir(exportDefaultDirectory())
            .filePath(ErrorReport::suggestedFileName(window->instanceName(), now));
    const QString path = QFileDialog::getSaveFileName(
        window, tr("导出错误报告"), suggested,
        tr("文本文件 (*.txt);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }
    const QString writeError =
        writeExportFile(path, ErrorReport::text(facts, now, errors, log));
    if (!writeError.isEmpty()) {
        appendLog(tr("导出错误报告失败：%1").arg(writeError));
        InlineMessage::warning(m_ui->overlay(), tr("导出失败"),
                               tr("这个文件没能写出来（%1）：\n%2")
                                   .arg(QDir::toNativeSeparators(path), writeError));
        return;
    }
    const QString shown = QDir::toNativeSeparators(path);
    appendLog(tr("✓ 错误报告已导出：%1").arg(shown));
    window->appendText(tr("\n[错误报告已导出：%1]\n").arg(shown));
}

void MainWindow::exportLaunchScript(const QString &versionId, const QString &path) {
    if (versionId.isEmpty() || path.isEmpty()) {
        // 产品里走不到（对话框那边是"问出来的路径非空才算数"）。验证工程打一条空路径进来时
        // 什么都不做，比去写一个叫 "" 的文件强。
        return;
    }

    const QJsonObject versionJson = m_versionManager->getVersionJson(versionId);
    if (versionJson.isEmpty()) {
        appendLog(tr("导出启动脚本失败：找不到版本 %1 的 version.json").arg(versionId));
        InlineMessage::warning(m_ui->overlay(), tr("导出失败"),
                               tr("找不到版本 %1 的 version.json，导出来的脚本跑不起来。")
                                   .arg(versionId));
        return;
    }

    // 身份照**启动那条路**判（唯一出处 offlineIdentity()），令牌也一样要换 —— 这一份脚本
    // 要能直接跑起来，就得带着和启动那一刻同一个身份。
    const bool offlineMode = offlineIdentity();
    QString token;
    if (!offlineMode) {
        QString tokenError;
        // 这一段是同步的（要换令牌时它要在 UI 线程上发请求，单次超时 15 秒）—— 和启动一样
        // 先给一句话再进去。
        enterSyncWait(tr("正在校验登录状态..."));
        token = m_authManager->loadSavedToken(&tokenError);
        leaveSyncWait();
        if (token.isEmpty()) {
            // 导一份没有令牌的脚本 = 导一份起不来的脚本，所以这一档**不导**，说清为什么。
            appendLog(tr("导出启动脚本失败：%1").arg(tokenError));
            InlineMessage::warning(
                m_ui->overlay(), tr("导出失败"),
                tokenError.isEmpty()
                    ? tr("导出启动脚本要先能拿到登录令牌。请先登录 Microsoft 账户，"
                         "或者在「账号与外观…」里添加一个离线账号。")
                    : tr("%1\n\n导出启动脚本要先能拿到登录令牌，请重新登录后再试。")
                          .arg(tokenError));
            return;
        }
    }

    // 这次要用的那一套 Java 值：和启动一样合成（全局 + 这个实例的覆盖），自动档的内存也在
    // 这一步按**这一刻**的机器状态定下来。
    // NOTE: 这里**不挑 Java、也不下运行时**（ensureJavaForVersion 那一整套）：用户点的是
    //       "导出这个文件"，顺手在后台下几百 MB 的运行时不是他要的。路径空着就写裸 `java`，
    //       和 LauncherManager 那边"从 PATH 找"是同一个兜底。
    const JavaSettingsOverride override =
        javaOverrideFromJson(m_versionManager->versionMeta(versionId));
    EffectiveJavaSettings javaSettings =
        resolveJavaSettings(m_javaSettings->effective(), override);
    resolveMemoryForLaunch(javaSettings, override);

    const QString program = javaSettings.javaPath.isEmpty()
                                ? QStringLiteral("java")
                                : QDir::toNativeSeparators(javaSettings.javaPath);
    const QStringList args =
        launchArguments(versionId, versionJson, javaSettings, token, offlineMode);

    // ---------- 拼那个 .bat ----------
    //
    // 编码写**本地 8 位**，不写 UTF-8 + `chcp 65001`：cmd 是按当前代码页读文件的，而"双击
    // 一个 .bat"时先跑起来的正是 cmd 自己 —— 代码页要等它读到那一行 `chcp` 才能改，在那之前
    // 的字节已经按旧代码页解过了。写本地代码页则两边天然一致。
    //
    // 第一句必须是 `cd /d`：参数里 classpath 那几段是**相对 <数据目录>** 写的（启动器那边
    // 靠 QProcess 的 setWorkingDirectory(minecraftDir) 兜住），脚本不在那个目录下跑就找不到
    // jar。`/d` 是连盘符一起换 —— 数据目录在 E 盘、脚本导到 C 盘桌面上是最常见的情形。
    QString script;
    script += QStringLiteral("@echo off\r\n");
    script += QStringLiteral("rem 由 WTFML 导出：按这个实例现在的参数启动。\r\n");
    if (!offlineMode) {
        script += QStringLiteral("rem 这个脚本里带着你的登录令牌，别发给别人。\r\n");
    }
    script += QStringLiteral("cd /d ") +
              batchQuote(QDir::toNativeSeparators(AppPaths::minecraftDir())) +
              QStringLiteral("\r\n");
    script += batchQuote(program);
    for (const QString &arg : args) {
        script += QLatin1Char(' ') + batchQuote(arg);
    }
    script += QStringLiteral("\r\n");

    // QSaveFile：写不下去时**不会留下一个半截的 .bat**（那个东西双击起来的行为是随机的，
    // 而用户会以为"导出的就是用不了的"）。和写版本 json 那边同一个理由。
    const QByteArray bytes = script.toLocal8Bit();
    QSaveFile file(path);
    bool written = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    if (written) {
        written = file.write(bytes) == bytes.size() && file.commit();
    }
    if (!written) {
        QString writeError = file.errorString();
        if (writeError.isEmpty()) {
            writeError = tr("系统没给出原因。");
        }
        appendLog(tr("导出启动脚本失败：%1").arg(writeError));
        InlineMessage::warning(m_ui->overlay(), tr("导出失败"),
                               tr("这个文件没能写出来（%1）：\n%2")
                                   .arg(QDir::toNativeSeparators(path), writeError));
        return;
    }

    const QString shown = QDir::toNativeSeparators(path);
    appendLog(tr("✓ 启动脚本已导出：%1").arg(shown));
    QStringList notes;
    notes.append(offlineMode ? tr("这一份用的是离线身份（游戏里就是这个名字）。")
                             : tr("★ 这份脚本里带着你的登录令牌：别发给别人，也别传网盘。"));
    if (javaSettings.javaPath.isEmpty()) {
        notes.append(tr("脚本里写的是 PATH 上的 java（这个实例还没挑过 Java）。"));
    }
    notes.append(tr("参数和启动器这次启动用的那一份是同一份；要改就改完再导一遍。"));
    InlineMessage::information(
        m_ui->overlay(), tr("导出完成"),
        tr("已经写到：\n%1\n\n%2").arg(shown, notes.join(QStringLiteral("\n"))));
}

void MainWindow::exportModpack(const QString &versionId, const QString &targetPath) {
    if (versionId.isEmpty() || targetPath.isEmpty()) {
        return;
    }

    // ---------- ① 这个实例自己正在被改的时候不许导 ----------
    //
    // 判据用 `installingVersionIds()`（"现在谁在动盘"的**唯一出处**），而不是"有没有下载
    // 在跑"：导出只读盘、只发两发反查，**别的实例在装和它一点冲突都没有**，没必要跟着拦
    // （这一点和「补全文件」那道闸故意不同 —— 那个要开一批下载，所以那边还有第二道）。
    if (installingVersionIds().contains(versionId)) {
        InlineMessage::information(
            m_ui->overlay(), tr("现在做不了"),
            tr("「%1」这会儿正在装（或者正在导入 / 正在检查）。等它完了再导出 —— "
               "打包要读的正是这会儿正在被改的那些文件。")
                .arg(instanceDisplayName(versionId)));
        return;
    }

    // ---------- ② 格式只看扩展名 ----------
    //
    // 不讲第三档参数：那个分岔在对话框里已经问过了（保存框里那两个筛选项带的就是扩展名，
    // 见 InstanceSettingsDialog::exportModpackRequested 那条 NOTE），再多一处状态就是
    // 多一处"文件名叫 manifest.json、正文却是 Modrinth 那份"的机会。
    const ModpackExport::Format format =
        targetPath.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)
            ? ModpackExport::Format::CurseForge
            : ModpackExport::Format::Modrinth;

    // ---------- ③ 三件本地的事先问清楚，任何一件不成立就不必往下走 ----------
    //
    // 顺序是**从便宜到贵**，而且都是"读本地"。三件都在 `enterSyncWait()` **之前** ——
    // 进去之后 UI 线程是卡着的，那时候弹框会叠在忙指针上。
    const QString instanceDir = AppPaths::versionDir(versionId);
    if (!QFileInfo(instanceDir).isDir()) {
        appendLog(tr("导出失败：找不到实例目录 %1").arg(instanceDir));
        InlineMessage::warning(m_ui->overlay(), tr("导出失败"),
                               tr("找不到这个实例的目录：\n%1")
                                   .arg(QDir::toNativeSeparators(instanceDir)));
        return;
    }

    const QString gameVersion = m_versionManager->instanceGameVersion(versionId);
    if (gameVersion.isEmpty()) {
        appendLog(tr("导出失败：读不出 %1 基于哪个原版").arg(versionId));
        InlineMessage::warning(
            m_ui->overlay(), tr("读不出这个实例基于哪个原版"),
            tr("整合包清单里必须写清楚「基于哪个原版游戏」，而这一份读不出来 —— 要么本地"
               "没有一处写着它（wtfml.meta 里没有 realVersion，版本 json 里没有 clientVersion、"
               "也顺不出指得到原版的 inheritsFrom 链），要么写着的那个**不是一个真能装出来的"
               "版本号**（自己改过的名字就属于这种：清单里记着它，别人那边却没有这个版本可装）。\n\n"
               "这个实例多半是很早以前装的、或者被手动改过。在「实例设置…」里看不出版本号"
               "的话，建议重装一份再导出。"));
        return;
    }

    QString loaderKey;
    QString loaderVersion;
    const QString loaderProblem =
        m_versionManager->instanceLoaderInfo(versionId, &loaderKey, &loaderVersion);
    if (!loaderProblem.isEmpty()) {
        // 数据层那句已经是人话了（含"改成什么就能导"），原样给它。
        appendLog(tr("导出失败：认不出 %1 的加载器").arg(versionId));
        InlineMessage::warning(m_ui->overlay(), tr("这个实例没法按整合包规范导出"),
                               loaderProblem);
        return;
    }

    // ---------- ④ 干活 ----------
    //
    // 清单里那两格（名字 / 版本号）：名字用**显示名**（用户在界面上看到的就是它），版本号
    // 给一个固定值 —— 今天还没有"这个整合包自己的版本"这个概念，编一个数字出来不如写死
    // 一个有据可依的初值（改法就在下面这一行）。
    ModpackExport::ExportRequest request;
    request.format = format;
    request.instanceDir = instanceDir;
    request.name = instanceDisplayName(versionId);
    request.version = QStringLiteral("1.0.0");
    request.gameVersion = gameVersion;
    request.loaderKey = loaderKey;
    request.loaderVersion = loaderVersion;
    // `include` 保持默认（存档 / 截图不进包）：这两样是**这个人的**东西，不是这个整合包的，
    // 打进 overrides 之后收包的人一装就把自己的存档盖了。被排掉的那几个目录名下面会照实报。

    // 进度写**底栏那一格**（不另开进度窗 —— 用户给的方向是"不出现独立窗口"）。手法和启动
    // 路径那三段同步等一样：进去之前先把话说出来，之后每次刷新都配一次 processEvents
    // （**排掉用户输入**，理由见 enterSyncWait 里那段）。
    //
    // NOTE: 因此这一趟**中途不能取消**：取消要一个收得到点击的框，而这一趟 UI 线程是卡着
    //       的（能收点击的那套是"嵌进覆盖层的提示 + 嵌套事件循环"，那是另一件事了）。
    //       一个实例几百兆到一两 G，实测几秒到几十秒。
    appendLog(tr("开始导出「%1」（%2）...")
                  .arg(instanceDisplayName(versionId),
                       format == ModpackExport::Format::CurseForge ? tr("CurseForge 规范")
                                                                  : tr("Modrinth 规范")));
    enterSyncWait(tr("正在导出整合包…"));
    QElapsedTimer throttle;
    const ModpackExport::ExportProgress progress =
        [this, &throttle](int done, int total, const QString &current) {
            // 节流 100 ms：底栏那一格重排一次不便宜，而一个实例有几百上千个文件。
            // 最后一条**必刷**（done == total 那一发），否则进度会停在"少一格"上。
            if (throttle.isValid() && throttle.elapsed() < 100 && done < total) {
                return;
            }
            throttle.start();
            m_syncWaitStatus = tr("正在导出整合包…（%1/%2）%3")
                                   .arg(done)
                                   .arg(total)
                                   .arg(current.isEmpty() ? QString()
                                                          : QFileInfo(current).fileName());
            updateBottomBar();
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        };
    const ModpackExport::ExportPlan plan =
        m_versionManager->buildExportPlan(request, progress, ModpackExport::ExportCancel());
    leaveSyncWait();

    if (plan.cancelled) {
        // 今天没有能取消的界面（见上面那段 NOTE），所以这一支到不了。留着是因为**数据层
        // 有这一档**：真接了取消的界面之后，这里就是那个出口，别让它静默地往下走成
        // "一个文件都没认出来的包"。
        appendLog(tr("导出已取消。"));
        return;
    }
    if (!plan.problem.isEmpty()) {
        appendLog(tr("导出失败：%1").arg(plan.problem));
        InlineMessage::warning(m_ui->overlay(), tr("导出失败"), plan.problem);
        return;
    }

    // ---------- ⑤ 两个"要不要照样导"的问句 ----------
    //
    // 合成**一个**问句：两条理由可能同时成立（断网 + 一个都没认出来），分两次问就是让用户
    // 连点两次取消。默认那一边是**取消**（危险的那条路必须刻意点）。
    QStringList reasons;
    if (plan.lookupFailed) {
        reasons.append(plan.lookupProblem);
    }
    if (!ModpackExport::reimportable(plan.spec)) {
        reasons.append(
            tr("一个资源都没认出来 —— 这样的包里没有任何一条能自动下载的条目，我们自己的"
               "「导入整合包…」会直接判它无效。"));
    }
    if (!reasons.isEmpty()) {
        const int choice = InlineMessage::ask(
            m_ui->overlay(), MessageKind::Question, tr("继续导出吗"),
            tr("%1\n\n继续的话，认不出出处的文件会原样打进包里：别人装上照样能用，"
               "只是启动器不能替他单独更新或者去重。")
                .arg(reasons.join(QStringLiteral("\n\n"))),
            QStringList{tr("继续导出"), tr("取消")}, 1, -1);
        if (choice != 0) {
            appendLog(tr("已取消导出。"));
            return;
        }
    }

    // ---------- ⑥ 落盘 ----------
    //
    // 打包**另起一段同步等**（底栏那句换成"正在打包"）：它是这一趟里最慢的一步
    // （ZipUtil 走 PowerShell 压一遍，一两 G 的实例要几十秒），而且**没有进度点** ——
    // 里面是一次调用，插不进去东西（数据层里那个 lambda 只报"包好了没"）。
    enterSyncWait(tr("正在打包整合包…"));
    const QString writeProblem = m_versionManager->writePackArchive(plan, targetPath);
    leaveSyncWait();
    if (!writeProblem.isEmpty()) {
        appendLog(tr("导出失败：%1").arg(writeProblem));
        InlineMessage::warning(m_ui->overlay(), tr("导出失败"), writeProblem);
        return;
    }

    // ---------- ⑦ 报告 ----------
    const QString shown = QDir::toNativeSeparators(targetPath);
    appendLog(tr("✓ 整合包已导出：%1").arg(shown));
    QStringList lines;
    lines.append(tr("已经写到：\n%1").arg(shown));
    lines.append(tr("问过 %1 个文件，认出来 %2 个（写进清单，装的人那边自己去下）；"
                    "其余 %3 个原样打进了包里（模组本体、配置、资源包都在）。")
                     .arg(plan.queried)
                     .arg(plan.matched)
                     .arg(plan.overrides.size()));
    if (plan.notDistributable > 0) {
        lines.append(tr("其中 %1 个的作者关掉了第三方分发 —— 它们只能跟着包走。")
                         .arg(plan.notDistributable));
    }
    // 「其中」串的是上面那句「其余 N 个原样打进了包里」—— 这几句说的都是那一堆里的
    // （所以它们摆在「另外…」那句前面，那一句说的是**不在**那一堆里的）。
    if (plan.keptLocalState > 0) {
        lines.append(tr("其中 %1 个是你改过名字（或者禁用着）的 —— 它们也原样进包，"
                        "名字和状态都照你盘上那样留着，装的人那边不会被改回启用。")
                         .arg(plan.keptLocalState));
    }
    // ★ 2026-09-27：这一句和上面「作者关掉了第三方分发」那句**不能合成一句**。那一句
    //   说的是"接口明说了原因"，这一句说的是"接口没说" —— 接口没给 `isAvailable` 这一项时
    //   （字段改名、回复被截断等等），从前也算进那个数里，于是报告凭空替作者认下一个
    //   "关了第三方分发"。用户拿着那句话去 CurseForge 上翻作者设置，是永远翻不出所以然的。
    if (plan.notDistributableUnknown > 0) {
        lines.append(tr("另外 %1 个没能问出能不能分发（CurseForge 的回复里没有这一项）—— "
                        "它们也原样进了包，装的人照样能用，只是不能替他单独更新或者去重。")
                         .arg(plan.notDistributableUnknown));
    }
    if (!plan.hashFailures.isEmpty()) {
        lines.append(tr("有 %1 个文件连哈希都没算出来（读取失败），它们也原样进了包。")
                         .arg(plan.hashFailures.size()));
    }
    if (!plan.collect.skippedDirs.isEmpty()) {
        lines.append(tr("没进包：%1（共 %2 个文件）。存档和截图是**你自己**的东西，"
                        "打进包里会盖掉收包那个人的。")
                         .arg(plan.collect.skippedDirs.join(QStringLiteral("、")),
                              QString::number(plan.collect.skippedFileCount)));
    }
    lines.append(tr("这是 %1 的包：别人在启动器里用「导入整合包…」就能装回来。")
                     .arg(format == ModpackExport::Format::CurseForge
                              ? tr("CurseForge 规范")
                              : tr("Modrinth 规范")));
    InlineMessage::information(m_ui->overlay(), tr("导出完成"),
                               lines.join(QStringLiteral("\n\n")));
}

void MainWindow::repairInstanceFiles(const QString &versionId) {
    if (versionId.isEmpty()) {
        return;
    }

    // 三道守卫，各说各的：用户点的是"检查这个实例"，说清"现在为什么不行"比一句笼统的
    // "忙"有用。第一道看的是**这个实例自己**（不等于"有下载在跑"——别的实例在装不该拦它，
    // 但这里点的是补全，而补全要开一批下载，所以第二道照样得拦）。
    const bool thisInstanceBusy = versionId == m_installingInstanceId ||
                                  versionId == m_importingInstanceId ||
                                  versionId == m_repairingInstanceId;
    if (thisInstanceBusy) {
        InlineMessage::information(
            m_ui->overlay(), tr("现在做不了"),
            tr("「%1」这会儿正在装（或者正在导入 / 正在检查）。等它完了再来。")
                .arg(instanceDisplayName(versionId)));
        return;
    }
    if (m_downloadInFlight || m_versionManager->isDownloading()) {
        InlineMessage::information(
            m_ui->overlay(), tr("现在做不了"),
            tr("已经有一个下载或者安装任务在跑了，等它结束再检查这个实例。"));
        return;
    }

    // 读不出 json / 继承链不完整：**先说清楚**，别把忙态置上再让数据层回一句内部话。
    // 尤其继承链那一条 —— "缺文件"和"父版本整个没装"是两件事，后者补不出来。
    const QString inheritanceProblem = m_versionManager->inheritanceProblem(versionId);
    if (!inheritanceProblem.isEmpty()) {
        appendLog(tr("无法校验 %1 的文件：版本继承链不完整").arg(versionId));
        InlineMessage::warning(m_ui->overlay(), tr("版本不完整"), inheritanceProblem);
        return;
    }
    const QJsonObject versionJson = m_versionManager->getVersionJson(versionId);
    if (versionJson.isEmpty()) {
        appendLog(tr("无法校验 %1 的文件：找不到 version.json").arg(versionId));
        InlineMessage::warning(
            m_ui->overlay(), tr("版本信息缺失"),
            tr("找不到版本 %1 的 version.json，没法知道它要哪些文件。").arg(versionId));
        return;
    }

    // 不弹一次"确定吗"：这颗按钮自己就是那一下确认（它写着"开始检查…"，下面那句说明也把
    // 要干的事说全了），再问一遍只是让用户多点一次。真的会花时间的地方在底栏那根进度条上。
    appendLog(tr("开始校验「%1」的文件（已存在的会跳过）...").arg(instanceDisplayName(versionId)));
    // 顺序：**先记名字再置忙**。setDownloadUiBusy() 里那句 refreshInstalledVersions() 读的
    // 就是这个判据（正在补全的实例不该出现在启动列表里），反过来的话它漏得出那一次刷新。
    m_repairingInstanceId = versionId;
    setDownloadUiBusy(true);
    m_versionManager->repairVersionFiles(versionId);
}

MainWindow::JavaMismatchAnswer MainWindow::askAboutJavaMismatch(
    const QString &versionId, const JavaSettingsOverride &override, int expectedMajor) {
    // ① 只有"实例钉了 Java"才有这个问题。全局档不匹配时 ensureJavaForVersion() 会
    //    静默换一个匹配的 —— 那是有意的老行为（用户没钉任何东西，替他挑一个正确的
    //    运行时不叫"换掉他的选择"），不动它。
    if (!override.javaPath.has_value()) {
        return JavaMismatchAnswer::NotAsked;
    }

    // ② 全局总闸关着，③ 或者这个实例表过态"以后别再问了"。
    //
    // 两条的语义**一样**，都是"别问了，照他钉的那个启动" —— 返回 KeepInstanceJava
    // 正是那个意思（它唯一的作用就是把 allowMismatchedPath 打开）。这里不能返回
    // NotAsked：那条路会落到手动档的硬错误上（路径不匹配就 fail()），也就是说，
    // 用户关了提醒之后反而**再也启动不了**这个实例 —— 一个纯负作用的开关。
    if (!m_javaSettings->warnVersionMismatch() ||
        m_versionManager->skipJavaVersionWarning(versionId)) {
        return JavaMismatchAnswer::KeepInstanceJava;
    }

    // ④ 那条路径到底是不是另一个主版本。探不出主版本（-1）时**不问** —— 那是"路径
    //    不存在/认不出来"，和"版本不符"是两件事，它继续落到现有的「Java 运行时缺失」
    //    硬错误那条路上。主版本相同当然更不用问。
    QString actualVersionText;
    const int actualMajor =
        GlobalJavaSettings::probeJavaMajorVersion(*override.javaPath, &actualVersionText);
    if (actualMajor <= 0 || actualMajor == expectedMajor) {
        return JavaMismatchAnswer::NotAsked;
    }

    // 从"另开一个窗"改成"内容区中间那一块"（用户点名的「像 java 设置这种小弹窗」）。
    // 关掉 / 按 Esc / 点外面都**不动 `choice`**，它的初值就是 Abort —— 和原来
    // `dialog.exec()` 被关掉时是同一个结果（见那个对话框的 .cpp 末尾）。
    JavaMismatchDialog dialog(instanceDisplayName(versionId), expectedMajor, actualMajor,
                              actualVersionText, *override.javaPath);
    m_ui->overlay()->execPrompt(&dialog, false);
    const JavaMismatchDialog::Choice choice = dialog.choice();

    if (choice == JavaMismatchDialog::Choice::Abort) {
        appendLog(tr("已取消启动：「%1」指定的是 Java %2，而这个版本要求 Java %3。")
                      .arg(instanceDisplayName(versionId))
                      .arg(actualMajor)
                      .arg(expectedMajor));
        return JavaMismatchAnswer::AbortLaunch;
    }

    // 「以后不再提醒这个实例」是用户刚点下的一下**表态**，立刻落盘才对得上他的意思
    // （等他下次想起来去「实例设置…」里关，那个框会一直问下去）。
    //
    // NOTE: 写失败**只记日志、不拦启动**。这是全程唯一一处"错误不弹窗"：存不下一个
    //       偏好和"这次启动能不能跑"是两件事，不该因为它把用户刚授权的启动掐掉；
    //       而它自己的后果（下次还问一句）是无害的、也是看得见的。
    //
    // NOTE: 勾选框是**独立于选了哪条路**的一句表态（它的字面意思就是这样），所以
    //       「改用匹配的 Java」那一次也照写不误 —— 要是他后来又给这个实例钉了个
    //       对不上的 Java，"别问了"这句话仍然算数。
    if (dialog.dontRemindAgain()) {
        QString error;
        if (!m_versionManager->updateVersionMeta(
                versionId, {{VersionManager::skipJavaVersionWarningField(), QJsonValue(true)}},
                &error)) {
            appendLog(tr("没能记住「以后不再提醒」：%1").arg(error));
        } else {
            appendLog(tr("「%1」以后不再提醒 Java 版本不符。").arg(instanceDisplayName(versionId)));
        }
    }

    return choice == JavaMismatchDialog::Choice::UseMatchingJava
               ? JavaMismatchAnswer::UseMatchingJava
               : JavaMismatchAnswer::KeepInstanceJava;
}

void MainWindow::resolveMemoryForLaunch(EffectiveJavaSettings &io,
                                       const JavaSettingsOverride &override) {
    if (!io.autoMemory) {
        // 手动档：io 里那两个数字是"用户定的值"（可能来自全局、也可能来自这个实例
        // 的覆盖），就是这次要用的值，一个都不许动。
        return;
    }

    // NOTE: 自动档下 xmx/xms 的覆盖**在这里被丢掉**，这是有意的：自动档的定义就是
    //       "按系统内存现算"，用户单独钉一个 Xmx 而让 Xms 自动算，只会得到一对
    //       谁也说不清的数字。对话框那边是同一件事的另一半 —— 自动档下那两行整行
    //       禁用，点确定时也不把它们写进 meta。
    if (!override.autoMemory.has_value()) {
        // 自动这一档来自**全局**：走老路径。refreshSuggestedMemory() 会重算、落盘
        // （注册表）、并按全局档更新自己的值 —— 和加实例覆盖之前逐字节一致。
        m_javaSettings->refreshSuggestedMemory();
        io.xmx = m_javaSettings->xmx();
        io.xms = m_javaSettings->xms();
        return;
    }

    // 自动这一档是**这个实例**自己要的（全局可能是手动档）。现算一份给这次启动用，
    // 全局一个字节都不动 —— 实例 A 开着自动，不能让全局那份内存配置跟着变，
    // 那是跨作用域污染的同一种错（见 ensureJavaForVersion 的值版那段注释）。
    //
    // NOTE: 算出来的数**不写回 meta**。写回去的话，那个实例的 meta 里就多出一对
    //       xmx/xms（它们本来没有），而这对值是死的 —— 自动档每次启动都重算，
    //       下次读出来只会让「实例设置…」里那两行显示成"自定义 + 一个用户没填过的数"。
    //       跨作用域污染的反面不是"多写一份"，而是"只写在这次要用的值上"。
    const SystemMemory::Usage usage = SystemMemory::read();
    io.xmx = GlobalJavaSettings::suggestXmxMb(usage.totalMb, usage.availableMb);
    io.xms = GlobalJavaSettings::suggestXmsMb(io.xmx);
}

void MainWindow::onFolderSelectionChanged() {
    const QListWidgetItem *item = m_ui->getFolderList()->currentItem();
    if (item == nullptr) {
        return;
    }
    // 同一个目录时 FolderManager 直接返回、不发信号，所以这条链路是幂等的：
    // refreshFolderList() 里挡了信号，重灌不会把用户刚做的选择又跑一遍。
    m_folderManager->setCurrentFolder(item->data(Qt::UserRole).toString());
    updateFolderButtons();
}

/**
 * @brief 一个没起过名字的游戏文件夹在列表**上面那行**默认显示什么。
 * @param path 根目录的绝对路径。
 * @return 目录名（`D:\MC\.minecraft` → `.minecraft`）。取不出目录名时（`D:\` 这种根）
 *         返回整条路径 —— 那一行总得有字，空的会变成一个看不出是什么的空白行。
 *
 * NOTE: 这条路只在**没起过 customname** 时走到（起过名字的显示那个名字）。
 */
static QString folderDisplayNameOf(const QString &path) {
    const QString name = QDir(path).dirName();
    return name.isEmpty() ? path : name;
}

QString MainWindow::folderTitle(const QString &path) const {
    // 三档，从最具体的往最泛的排：用户起的名字 → 自带那份那个固定说法 → 目录名。
    const QString custom = m_folderManager->displayName(path);
    if (!custom.isEmpty()) {
        return custom;
    }
    if (FolderManager::isDefaultFolder(path)) {
        return MainWindowUI::folderAssistantTitle();
    }
    return folderDisplayNameOf(path);
}

void MainWindow::refreshFolderList() {
    const QString current = m_folderManager->currentFolder();
    QList<MainWindowUI::FolderRow> rows;
    for (const QString &path : m_folderManager->folders()) {
        MainWindowUI::FolderRow row;
        row.path = path;
        row.assistant = FolderManager::isDefaultFolder(path);
        row.current = (path == current);
        // 上面那行：用户起的 customname 优先；没起过时是默认那个显示名（见 folderTitle()）。
        row.title = folderTitle(path);
        // 下面那行：完整路径（系统分隔符，别把 QDir 的 `/` 露给用户）。目录不在盘上时
        // 还要缀一句 —— 光看路径看不出"这一份现在扫不出东西"。
        row.subtitle = QDir::toNativeSeparators(path);
        if (!QFileInfo(path).isDir()) {
            row.subtitle += tr("（目录不在了）");
        }
        rows.append(row);
    }
    m_ui->setFolderRows(rows);
    updateFolderButtons();
}

void MainWindow::updateFolderButtons() {
    // 下载期间整列都不给动（见 setDownloadUiBusy）：换文件夹会把后面那些文件写到
    // 另一份 .minecraft 里去，半批在里面半批在外面，比"这次下不了"难查得多。
    //
    // NOTE: 这里原来还按"当前选中的那一份是不是自带那份"给「移除文件夹」按钮算过
    //       enabled，那个按钮 2026-09-24 删掉了。「移除」现在不在这儿管：齿轮菜单里
    //       那一项的灰/亮由它自己按"这颗齿轮属于哪一行"算（见 FolderMenu 里那段
    //       protectedFolder）—— 菜单是照着**哪一行那颗齿轮**弹的，和"列表当前选中
    //       哪一行"可以不是同一行，这是它比按钮准的地方。
    m_ui->getAddFolderButton()->setEnabled(!m_downloadInFlight);
}

void MainWindow::applyCurrentFolderToPaths() {
    // 全局路径的**唯一**切换点。此后 AppPaths 的每一个函数（versions / assets /
    // libraries / java）都对着这一份 .minecraft，直到下一次切换。
    const QString root = m_folderManager->currentFolder();
    // 自带那份**不设覆盖**（传空串 = 用默认那份）：启动器被整个拷到别处之后，默认路径
    // 跟着 exe 走，而不是钉死在上次记下的那个老位置。
    AppPaths::setMinecraftDir(FolderManager::isDefaultFolder(root) ? QString() : root);
    appendLog(tr("当前游戏文件夹：%1").arg(QDir::toNativeSeparators(root)));
}

/**
 * @brief 日志行前面那个"[实例名] "。
 * @param sessionId 哪一份会话；0 或者查不到（会话已经结束了）时返回空串。
 *
 * 同时开着好几份时，一串"[游戏输出] xxx"根本分不出是谁说的 —— 多开这件事本身就把
 * "哪一行是谁"从显然变成了不显然。
 *
 * NOTE: 查不到就**不加前缀**、也不写"(未知)"：那种行是"又开一个被拦下了"这类不挂在
 *       任何实例上的话（sessionId == 0），或者会话刚好在这一行之前结束了。硬安一个名字
 *       比不写更糟。
 */
static QString sessionLogTag(const LauncherManager::GameSession &game) {
    if (game.sessionId == 0 || game.displayName.isEmpty()) {
        return QString();
    }
    return QStringLiteral("[%1] ").arg(game.displayName);
}

void MainWindow::onProcessOutput(int sessionId, const QString &text) {
    appendLog(tr("[游戏输出] %1%2")
                  .arg(sessionLogTag(m_launcherManager->session(sessionId)), text));
    // 「显示游戏日志」开着时，同一段字也进那一份的游戏日志窗。**放在 appendLog 之后**是
    // 刻意的：日志文件那一份是永远要写的（用户的排查底账），弹窗是附加的。
    showGameLogLine(sessionId, text);
    // 游戏自己出话了 = JVM 和游戏都初始化到能打日志了，**那一份**的"正在启动"到此为止。
    // 底栏中间那格让给下载进度（如果有），左下角换成"实例运行中"。
    //
    // NOTE: 按会话号摘，不按"有没有在启动" —— 同时启动两三个实例时，用布尔量的话第一个
    //       出话就会把后面那几个的启动进度一起收掉。
    if (m_launchingSessions.remove(sessionId)) {
        updateBottomBar();
    }
}

void MainWindow::onProcessError(int sessionId, const QString &text) {
    appendLog(tr("[游戏错误] %1%2")
                  .arg(sessionLogTag(m_launcherManager->session(sessionId)), text));
    // stderr 也进日志窗（下面那一行），理由和上面那条一样。**不**给它加"错误"前缀：
    // 游戏往 stderr 写的不全是错误（很多模组把 INFO 也写 stderr），在窗里加前缀反而误导。
    showGameLogLine(sessionId, text);
    // stderr 出东西也算"起来了"：有些版本把日志全写 stderr，等 stdout 会一直等下去。
    if (m_launchingSessions.remove(sessionId)) {
        updateBottomBar();
    }
}

void MainWindow::showGameLogLine(int sessionId, const QString &text) {
    // 没开这个开关就什么都不做 —— 这是绝大多数用户的状态，这条路径上不该有任何开销。
    if (!m_settings->showGameLog()) {
        return;
    }
    // sessionId <= 0 是"不挂在任何实例上的话"（比如"同时运行的游戏已经有 8 个了"那一句，
    // 它是 startGame 在登记会话之前发的，见那边的说明）。那种话没有日志窗可言。
    if (sessionId <= 0) {
        return;
    }

    GameLogWindow *window = m_gameLogWindows.value(sessionId, nullptr);
    if (window == nullptr) {
        // 用户关掉过这一份的窗就不再开（见 m_gameLogDismissedSessions 的说明）。
        if (m_gameLogDismissedSessions.contains(sessionId)) {
            return;
        }
        // 第一次收到**这一份**的输出才建窗（不去"启动成功"那一刻建）：这样三条早退路径
        // 上一个空窗都不会冒出来，而"进程真的起来了"那一句（startGame 内部同步发的第一条
        // processOutput）正好在建窗这一刻被写上 —— 用户看到的第一行就是"游戏已启动"。
        const QString name = m_launcherManager->session(sessionId).displayName;
        // **父窗口传 nullptr**（2026-09-24 改，原来传的是 this）。在 Windows 上"有主的窗"
        // 永远压在自己的主窗上面，用户点启动器也翻不上来 —— 那正是用户说的「日志一定会
        // 显示在启动器前面，会遮挡」。这是平台规则，不是能调的风格，所以只能不给它主。
        // 代价是"跟着启动器一起关掉"要自己接（见析构里那一段）。
        window = new GameLogWindow(name, nullptr);
        // 错开的位置数**在插进去之前**取：开第二扇时那是 1（往下错一点，不盖住第一扇）。
        const int cascade = m_gameLogWindows.size();
        m_gameLogWindows.insert(sessionId, window);
        // 这一份会话的事实留一个快照（「导出错误报告」要用，见 m_gameLogFacts 的说明）：
        // **建窗这一刻**取，因为再往后这个会话只会越来越空（结束时记录会被摘掉）。
        // 那一刻会话一定还在（窗是收到它的第一行输出才建的）。
        {
            const LauncherManager::GameSession live = m_launcherManager->session(sessionId);
            GameLogFacts snapshot;
            snapshot.instanceId = live.versionId;
            snapshot.accountName = live.accountName;
            snapshot.processId = live.processId;
            snapshot.startedAtMs = live.startedAtMs;
            m_gameLogFacts.insert(sessionId, snapshot);
        }
        // 底下那两颗导出键（2026-09-28）。它们只发信号，弹框、写盘全在这里 —— `ui/` 那一层
        // 不认识数据目录，也不该认识（见 GameLogWindow 的文件头）。
        connect(window, &GameLogWindow::exportLogRequested, this,
                [this, sessionId] { exportGameLog(sessionId); });
        connect(window, &GameLogWindow::exportReportRequested, this,
                [this, sessionId] { exportGameErrorReport(sessionId); });
        // 窗自己带 WA_DeleteOnClose，所以**关掉就会销毁**。销毁之前先把这一笔摘掉，
        // 顺便记下"用户关过这一份" —— 两个动作都必须在 finished 里做，不能在 destroyed 里：
        // finished 是"用户关窗"这一个动作的信号，destroyed 在父窗口析构时也会来一次，
        // 而那一次不该被当成"用户不想看日志"。
        connect(window, &QDialog::finished, this, [this, sessionId](int) {
            m_gameLogWindows.remove(sessionId);
            m_gameLogDismissedSessions.insert(sessionId);
            // 窗没了，那份快照也就没用了（它唯一的用途是"给那一扇窗导一份报告"）——
            // 不清的话它会随着开过的实例数一直长。
            m_gameLogFacts.remove(sessionId);
        });
        // 摆到启动器**旁边**（右 → 左 → 居中），别一上来就压住它 —— 用户抱怨的另一半
        // 正是"挡着"。启动器跨屏时按它在那一块屏的可用区域算（扣掉任务栏）。
        const QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
        if (screen == nullptr) {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen != nullptr) {
            window->move(GameLogWindow::placementFor(frameGeometry(), window->size(),
                                                      screen->availableGeometry(), cascade));
        }
        window->show();
        // 非模态（没用 exec()）：游戏跑着的时候用户还要在启动器上干别的。做成模态的话
        // 那扇窗会把整个启动器挡住 —— 一个"看输出"的窗没有拦人的道理。
    }
    window->appendText(text);
}

void MainWindow::onGameFinished(int sessionId, int exitCode, QProcess::ExitStatus exitStatus) {
    // ★ 2026-09-27 修：这里原来读的是 `m_launcherManager->session(sessionId)`，而**这一趟
    //    走到这儿的时候那份会话已经从表里摘掉了** —— handleProcessFinished() 里 discard 在
    //    emit 之前（那是有意的，见那边的 NOTE），LauncherManager.hpp 的 gameFinished 文档
    //    也明写了「收信号的槽去查 session(id) 会查不到，要用的东西请从参数/自己那边拿」。
    //    于是拿到的是**空快照**（sessionId == 0、displayName 空），sessionLogTag() 的
    //    sessionId==0 守卫把它降级成"不加前缀" ⇒ 同时跑两个实例、其中一个退出时，日志里
    //    是「游戏进程已结束，退出码：0」，没有 `[实例名] `，看不出是谁退了（它上面那些
    //    `[游戏输出]` 行都有名字）。名字在**启动那一刻**就记了一份，见 m_sessionDisplayNames。
    LauncherManager::GameSession game;
    game.sessionId = sessionId;
    game.displayName = m_sessionDisplayNames.value(sessionId);
    const QString tag = sessionLogTag(game);
    if (exitStatus == QProcess::CrashExit) {
        appendLog(tr("%1游戏崩溃，退出码：%2").arg(tag).arg(exitCode));
    } else {
        appendLog(tr("%1游戏进程已结束，退出码：%2").arg(tag).arg(exitCode));
    }
    // 日志窗那边也收个尾，两种收法：
    //
    // - **正常以 0 退出就自己关掉**（2026-09-24 用户说的「如果游戏正常以0退出就自动关闭日志
    //   界面」）。那一趟没有任何可看的东西（真出了事不会给 0），留一扇只写着一行"已退出"的
    //   窗等于替用户留了个垃圾。判据是 `GameLogWindow::autoCloseOnExit()`（**唯一出处**，
    //   验证工程拿真值表钉它）—— 注意崩溃一律不关，哪怕码正好是 0。
    // - 其余情况**留着**（用户正是要看崩溃前那几行），只补一行"到此为止"，不关窗也不销毁
    //   —— 关不关是用户的事（他关了就走 finished 那条路）。末尾这行必须是**末行**才读得懂：
    //   "进程已结束，退出码 -1" 之后还要有东西的话，用户会以为窗口没跟上。
    //
    // NOTE: 关窗走的是 close() → WA_DeleteOnClose → finished 那条路（它会顺手把这一笔
    //       记进"用户关过的不再自动开"）。下面那句 remove 因此**必须在关窗之后**：关窗
    //       那一刻刚记上的那一笔，紧接着就要清掉（这一份已经结束了，那个集合不该为它留名）。
    // 退出这件事记进那一份快照（「导出错误报告」的头部要写退出码，而**这个值是这一刻才知道
    // 的**、错过就再也拿不到了）。必须排在下面那次 close() **之前**：正常退出会关窗，而关窗
    // 那个 finished 里会把整份快照删掉。
    if (m_gameLogFacts.contains(sessionId)) {
        GameLogFacts &snapshot = m_gameLogFacts[sessionId];
        snapshot.finished = true;
        snapshot.crashed = exitStatus == QProcess::CrashExit;
        snapshot.exitCode = exitCode;
    }
    if (GameLogWindow *logWindow = m_gameLogWindows.value(sessionId, nullptr)) {
        if (GameLogWindow::autoCloseOnExit(exitCode, exitStatus == QProcess::CrashExit)) {
            logWindow->close();
        } else {
            logWindow->appendText(exitStatus == QProcess::CrashExit
                                      ? tr("\n[游戏已崩溃退出，退出码：%1]\n").arg(exitCode)
                                      : tr("\n[游戏已退出，退出码：%1]\n").arg(exitCode));
        }
    }
    // 这一份结束了，"用户关过它的日志窗"这件事不再有意义 —— 下一个实例是另一回事，
    // 不清的话这个集合会随着开过的实例数一直长。
    m_gameLogDismissedSessions.remove(sessionId);
    // 进程没了，它那一份的"启动中"要收掉；左下角那条"实例运行中"由 runningGames() 现算，
    // 这里只管重画（那一份在 LauncherManager 里已经从表里摘掉了）。
    m_launchingSessions.remove(sessionId);
    // 名字也到此为止（它唯一的用途就是上面那一行前缀）。不清的话这个表会随着开过的
    // 实例数一直长 —— 和 m_gameLogDismissedSessions 同一个理由。
    m_sessionDisplayNames.remove(sessionId);
    updateInstanceTreeRunState();
    updateBottomBar();

    // 「收起启动器，游戏退出后自动回来」那一档：见 applyLauncherBehaviourAfterLaunch()。
    // 只有**为了启动游戏才藏起来**的窗口才会被叫回来 —— 用户自己最小化、自己收起来的
    // 窗口不该因为游戏退出就跳出来。
    //
    // NOTE: 还要**一份都不剩**才叫回来。同时开着三个时，第一个退出就把窗口弹到用户脸上
    //       会打断他正在看的别的东西；那时他在启动器上想知道的是"还剩谁在跑"。
    if (m_hiddenForGame && m_launcherManager->runningCount() == 0) {
        m_hiddenForGame = false;
        showNormal();
        raise();
        activateWindow();
    }
}

QString MainWindow::launchingInstanceName() const {
    // 启动那一下是**同步**的（startGame() 在 UI 线程上等进程起来，见 enterSyncWait），
    // 所以真正卡在同步等待里时最多只有一份。能同时有两份以上的只有"进程起来了、游戏还
    // 没出话"那段异步进度 —— 那时报其中一个名字是错的，报个数才诚实。
    if (m_launchingSessions.size() == 1) {
        const LauncherManager::GameSession game =
            m_launcherManager->session(*m_launchingSessions.constBegin());
        if (game.sessionId != 0) {
            return instanceDisplayName(game.versionId);
        }
    }
    return tr("%1 个实例").arg(m_launchingSessions.size());
}

QString MainWindow::launchingStatusName() const {
    // 和上面 launchingInstanceName() 同一套判据（"只有一份时才报名字"，理由在那边），
    // 只有名字的**写法**不同：走 VersionDisplay::launchStatusName()，也就是
    // "有自定义名就只用它，没有就是 版本号-加载器名"，与「版本名格式」那四个开关无关。
    if (m_launchingSessions.size() == 1) {
        const LauncherManager::GameSession game =
            m_launcherManager->session(*m_launchingSessions.constBegin());
        if (game.sessionId != 0) {
            return launchStatusName(m_versionManager->displayParts(game.versionId));
        }
    }
    return tr("%1 个实例").arg(m_launchingSessions.size());
}

bool MainWindow::confirmStopGame(const QString &displayName, int sessions) {
    // 「停」在 Windows 上就是 TerminateProcess（见 LauncherManager::stopGame()），游戏
    // 来不及存档。**这件事必须在这里说清楚**，默认那一边是"别停"（回车 = 取消）。
    //
    // NOTE: 2026-09-25 之前就**只有**底栏左面板里那行电源走这儿了（实例页那颗「停」连同
    //       applyStopGame() 一起删了，见 MainWindowUI.cpp 里那段）。所以 `sessions > 1`
    //       那半句现在是**死支**—— stopGameSession() 恒传 1。留着它是因为它是这个函数
    //       的一部分（"一次停几份"这件事问话里要说清），不是因为还有人在用。
    const QString text =
        sessions > 1
            ? tr("「%1」正在运行（共 %2 个游戏进程，这一下全部停止）。\n\n停止会"
                 "**直接结束游戏进程** —— 游戏不会存档，没保存的进度会丢。\n"
                 "要停止它吗？")
                  .arg(displayName)
                  .arg(sessions)
            : tr("「%1」正在运行。\n\n停止会**直接结束游戏进程** —— 游戏不会存档，"
                 "没保存的进度会丢。\n要停止它吗？")
                  .arg(displayName);
    // 内嵌提示（不再另开一个窗）。默认那一边照旧是**取消**（下标 1），
    // 「停止游戏」是那一档里的危险动作（下标 0）。
    const int choice = InlineMessage::ask(
        m_ui->overlay(), MessageKind::Warning, tr("停止游戏"), text,
        QStringList{tr("停止游戏"), tr("取消")}, 1, 0);
    return choice == 0;
}

void MainWindow::stopGameSession(int sessionId) {
    // 面板是**弹出来那一刻**的一张快照（它不会跟着刷新），所以"这一行还在不在"得现问一遍。
    const LauncherManager::GameSession game = m_launcherManager->session(sessionId);
    if (game.sessionId == 0) {
        // 用户看那两眼的时候它自己退出了。**不弹窗**：他什么都没做错，弹一个框只会让他
        // 以为出事了；面板上那一行也确实已经不在了。
        appendLog(tr("这个实例刚刚已经结束了。"));
        return;
    }
    if (!confirmStopGame(game.displayName, 1)) {
        return;
    }
    if (!m_launcherManager->stopGame(sessionId)) {
        // 上面那一问期间它退出了（游戏自己崩了/被别处停了）。
        appendLog(tr("「%1」刚刚已经结束了。").arg(game.displayName));
        return;
    }
    appendLog(tr("已停止「%1」。").arg(game.displayName));
    // 进程真的没了要等 Qt 把 finished 送回来（onGameFinished 那边会重画），但"正在启动"
    // 这一格该现在收：用户刚看见的那句话已经不对了。
    m_launchingSessions.remove(sessionId);
    updateBottomBar();
}

void MainWindow::onImportModpackClicked() {
    if (m_downloadInFlight) {
        appendLog(tr("已有下载任务正在进行，请等它结束。"));
        return;
    }

    // NOTE: 这一处也是独立窗（Windows 自己的文件选择框）—— 理由和取舍见
    //       onAddFolderClicked() 上面那一段。
    const QString zipPath = QFileDialog::getOpenFileName(
        this, tr("选一个整合包"), QString(),
        tr("整合包 (*.zip *.mrpack);;所有文件 (*)"));
    if (zipPath.isEmpty()) {
        return;  // 用户取消：什么都不做，也**不弹**任何提示
    }
    importModpackFromFile(zipPath);
}

void MainWindow::importModpackFromFile(const QString &zipPath) {
    // 先只看一件事：这个压缩包的根目录里有没有清单。**不整包解开** —— 用户随手选一个
    // 几百 MB 的包，为了"知道它不是整合包"先解压几分钟是白等。
    //
    // 两种格式各读一个名字（哪一侧有就是哪一种）：Modrinth 的 modrinth.index.json、
    // CurseForge 的 manifest.json。**先读 Modrinth 那个**，读不到再读 CF 那个 ——
    // 次序只影响"两个都有"这种怪包报成哪一种，无所谓。
    //
    // NOTE: 第五个参数（动作名）是**给用户看的**：下面 Failed 那一路会把 error 原样弹进那个
    //       警告框（「读这个压缩包的时候出错了：…」），所以这里必须写「读整合包清单」，
    //       而不是 ZipUtil 那个通用的默认说法。
    QByteArray manifestJson;
    QString error;
    ModpackImport::Format format = ModpackImport::Format::Unknown;
    ZipUtil::EntryStatus status = ZipUtil::readEntry(
        zipPath, ModpackImport::modrinthIndexName(), &manifestJson, &error,
        QStringLiteral("读整合包清单"));
    if (status == ZipUtil::EntryStatus::Ok) {
        format = ModpackImport::Format::Modrinth;
    } else if (status == ZipUtil::EntryStatus::NotFound) {
        // 里面没有 Modrinth 那份清单。**这一档才值得再读第二个名字**：`NotAnArchive` /
        // `Failed` 说明这个文件根本打不开或者读坏了，再读一个名字是白读（而且会把
        // "不是 zip"那句换成"认不出格式"，把话说歪）。
        status = ZipUtil::readEntry(zipPath, ModpackImport::curseForgeManifestName(),
                                    &manifestJson, &error, QStringLiteral("读整合包清单"));
        if (status == ZipUtil::EntryStatus::Ok) {
            format = ModpackImport::Format::CurseForge;
        }
    }
    if (status != ZipUtil::EntryStatus::Ok) {
        // "里面没有清单"和"这个文件根本打不开"是**两句完全不同的话**：前者多半是选错了
        // 文件，后者是文件坏了/不是 zip。
        QString text;
        if (status == ZipUtil::EntryStatus::NotAnArchive) {
            text = tr("这个文件打不开 —— 它不是 zip 压缩包（也可能是文件坏了）。\n\n"
                      "整合包得是 zip：Modrinth 的 .mrpack 也是 zip，只是换了个后缀。");
        } else if (status == ZipUtil::EntryStatus::Failed) {
            text = tr("读这个压缩包的时候出错了：\n%1").arg(error);
        } else {
            text = ModpackImport::unsupportedFormatText(format);
        }
        appendLog(tr("没有导入 %1。").arg(QFileInfo(zipPath).fileName()));
        InlineMessage::warning(m_ui->overlay(), tr("这不是整合包"), text);
        return;
    }

    // 解析（纯函数）。清单读不懂、不是 Minecraft 的包、路径不安全、一条文件都没有，
    // 四种情况的原因都不一样，都由它一句话说清 —— 界面不自己编第二句。
    const auto parseManifest = [](ModpackImport::Format which, const QByteArray &json) {
        return which == ModpackImport::Format::CurseForge
                   ? ModpackImport::parseCurseForgeManifest(json)
                   : ModpackImport::parseModrinthIndex(json);
    };
    ModpackImport::Result result = parseManifest(format, manifestJson);
    if (!result.ok) {
        appendLog(tr("整合包清单读不懂：%1").arg(result.problem));
        InlineMessage::warning(m_ui->overlay(), tr("导入整合包"), result.problem);
        return;
    }
    ModpackImport::Pack &pack = result.pack;

    // 加载器要不上，在**问名字之前**就拦掉：装不了的包，问完名字再说装不了是白耽误他。
    const QString loaderProblem =
        ModpackImport::unsupportedLoaderText(pack.loaderKey, pack.loaderVersion);
    if (!loaderProblem.isEmpty()) {
        appendLog(tr("没有导入 %1：要 %2，这个启动器装不了。")
                      .arg(QFileInfo(zipPath).fileName(), pack.loaderKey));
        InlineMessage::information(m_ui->overlay(), tr("这个整合包装不了"), loaderProblem);
        return;
    }

    // 名字：先拿包名推一个候选（ModpackImport::suggestInstanceName 会把建不出目录的
    // 字符换掉、收长度），用户改不改都行。校验用**数据层那一份**
    // （InstanceFiles::validateRenameTarget），界面绝不自己抄一遍规则。
    QString name = ModpackImport::suggestInstanceName(pack.name);
    while (true) {
        bool ok = false;
        const QString input = InlineMessage::getText(
            m_ui->overlay(), tr("导入整合包"),
            tr("实例叫什么名字？这就是 versions 下面那个目录名，\n"
               "整合包「%1」的东西会装进它里面。").arg(pack.name),
            name, &ok);
        if (!ok) {
            return;
        }
        name = input.trimmed();
        // currentId 传空串 = "这是个新实例，盘上没有它"（见 checkImportTargetName）。
        QString problem;
        const InstanceFiles::ImportNameVerdict verdict =
            InstanceFiles::checkImportTargetName(name, &problem);
        if (verdict == InstanceFiles::ImportNameVerdict::Ok) {
            break;
        }
        if (verdict == InstanceFiles::ImportNameVerdict::OkResume) {
            // 撞上的是**上次没下完的那一份**（下到一半网断了那种）。这是唯一一条不逼用户
            // 改名的路：改名重导等于把已经下好的几百 MB 再下一遍。仍然要问一句 ——
            // 这一步会重新解开包里的 config / 资源包，压在用户改过的文件上。
            const auto resumeAnswer = InlineMessage::question(m_ui->overlay(), tr("接着上次的导入"),
                tr("versions 下面已经有一个「%1」了，是上一次**没下完**的导入。\n\n"
                   "接着往它里面下吗？已经下好的文件会按 sha1 跳过、不会重下，"
                   "没下成的那几个会重新下。\n\n"
                   "选「否」就换个名字重新导入 —— 那样已经下好的那些要重下一遍。")
                    .arg(name), tr("是"), tr("否"), true);
            if (resumeAnswer) {
                break;
            }
            continue;  // 回上面重问名字
        }
        // 循环重问（和重命名那条路不同，那边是弹一次就要用户自己再点一次）：这里退回
        // 重来的代价大得多 —— 要从"选文件"重新开始，而选的是哪个包用户未必记得住。
        InlineMessage::warning(m_ui->overlay(), tr("换个名字"), problem);
    }

    // 这一步只有 CurseForge 需要：那一份清单里**一个下载地址都没有**，只有一串编号
    // （projectID / fileID），得先问一次接口才换得出来（判据与实测见
    // ModpackImport.hpp 顶上那段）。放在这里、而不是数据层里，是因为**用户要在点确认
    // 之前看到真实数字**：这个包要下多少个文件、其中几个作者不许第三方代下 —— 那些数
    // 只有换完才知道。其他格式这一句立刻返回，不联网。
    if (format == ModpackImport::Format::CurseForge) {
        // 同步等一两发请求（通常一两秒）。底栏那格要顶着"在等什么"，不然用户看到的是
        // 点完确认之后界面卡住（和启动路径那三段同步等是同一个手法）。
        enterSyncWait(tr("正在向 CurseForge 换取文件清单..."));
        const QString problem = m_versionManager->resolveCurseForgeFiles(&pack);
        leaveSyncWait();
        if (!problem.isEmpty()) {
            appendLog(tr("没有导入 %1：%2").arg(QFileInfo(zipPath).fileName(), problem));
            InlineMessage::warning(m_ui->overlay(), tr("拿不到文件清单"), problem);
            return;
        }
    }

    // 确认摘要。点下去就是几百 MB 和十几分钟，这是用户**最后**能反悔的地方，
    // 所以四件事都要说全：装的是哪个包、基于哪个版本、带什么加载器、装到哪去。
    VersionManager::LoaderKind loaderKind = VersionManager::LoaderKind::Fabric;
    const QString loaderText =
        pack.loaderKey.isEmpty()
            ? tr("（纯原版，没有模组加载器）")
            : (VersionManager::loaderKindForDependencyKey(pack.loaderKey, &loaderKind)
                   ? tr(" + %1 %2").arg(VersionManager::loaderDisplayName(loaderKind),
                                       pack.loaderVersion)
                   : tr(" + %1 %2").arg(pack.loaderKey, pack.loaderVersion));
    const auto answer = InlineMessage::question(m_ui->overlay(), tr("开始导入"),
        tr("整合包：%1\nMinecraft：%2%3\n要下载的文件：%4 个%5\n装到：%6\n\n开始导入吗？")
            .arg(pack.name, pack.gameVersion, loaderText)
            // 判据是 fileCount()（= 要下的 + 还没换成地址的）：CurseForge 那一档走到
            // 这里已经换过了（上面那一发），两者相等；**万一**换完一条都不剩（接口说
            // "一个都不许代下"），这里报出来的就是 0 —— 那正是用户该看到的数。
            .arg(pack.fileCount())
            .arg(pack.filesSkipped > 0
                     ? tr("（另有 %1 个按包里的说明跳过）").arg(pack.filesSkipped)
                     : QString())
            .arg(QDir::toNativeSeparators(AppPaths::versionDir(name))), tr("是"), tr("否"), true);
    if (!answer) {
        appendLog(tr("已取消导入 %1。").arg(pack.name));
        return;
    }
    // 作者不许第三方代下的那些**要在动手之前说**（换完才知道有几个，所以只能在这儿说）：
    // 点下去之后那几十个文件里会少这几条，而"少装了东西"是这个流程里最容易到进游戏才
    // 发现的。选「是」不是同意"接受少装"，只是知道了。
    if (pack.filesNotDistributable > 0) {
        InlineMessage::information(m_ui->overlay(), tr("有文件作者不许代下"),
            tr("这个整合包里有 %1 个文件，作者在 CurseForge 上关掉了「允许第三方分发」，"
               "启动器不会替你下。\n\n装完之后你可能得自己去 CurseForge 上那个整合包的页面把"
               "它们下下来，手动放进 %2 里（模组放 mods/、资源包放 resourcepacks/、光影放 "
               "shaderpacks/）。收尾那句话里会再提醒一次。")
                .arg(pack.filesNotDistributable)
                .arg(QDir::toNativeSeparators(AppPaths::versionDir(name))));
    }

    applyModpackImport(zipPath, name, manifestJson, format);
}

bool MainWindow::applyModpackImport(const QString &zipPath, const QString &instanceName,
                                    const QByteArray &manifestJson,
                                    ModpackImport::Format format) {
    // 把界面上确认过的**同一串字节**再解析一遍（纯函数，一瞬）。好处是"确认框里说的
    // 那个包"和"真的去装的那个包"不可能不是同一个 —— 不必去维护一份"解析结果"的传递。
    ModpackImport::Result result =
        format == ModpackImport::Format::CurseForge
            ? ModpackImport::parseCurseForgeManifest(manifestJson)
            : ModpackImport::parseModrinthIndex(manifestJson);
    if (!result.ok) {
        InlineMessage::warning(m_ui->overlay(), tr("导入整合包"), result.problem);
        return false;
    }
    ModpackImport::Pack pack = result.pack;

    const QString loaderProblem =
        ModpackImport::unsupportedLoaderText(pack.loaderKey, pack.loaderVersion);
    if (!loaderProblem.isEmpty()) {
        InlineMessage::information(m_ui->overlay(), tr("这个整合包装不了"), loaderProblem);
        return false;
    }

    // CurseForge 那一档在这里要**再换一次**地址（上面确认框那一步换过一次）。代价是
    // 一发重复的请求，换来的是和上面那句"同一串字节再解析一遍"一模一样的东西：这一半
    // 自己手里的那份 `pack` 是完整的，不靠外面传进来的任何解析结果。数据层那边也有
    // 第二道闸（importModpack 里那句"没换过就报错"），所以这里漏了也不会装出一个空实例。
    if (format == ModpackImport::Format::CurseForge) {
        enterSyncWait(tr("正在向 CurseForge 换取文件清单..."));
        const QString problem = m_versionManager->resolveCurseForgeFiles(&pack);
        leaveSyncWait();
        if (!problem.isEmpty()) {
            InlineMessage::warning(m_ui->overlay(), tr("拿不到文件清单"), problem);
            return false;
        }
    }

    // 重名**在这里再查一遍**：上面那条路（问名字那个循环）查过一次，但两次之间隔着用户
    // 的输入，而且验证工程直接就能进这一半 —— 而数据层那边是有约定的（见
    // VersionManager::importModpack 的说明：调用方必须先查过重名，它自己不查）。
    // 判据是同一份 checkImportTargetName()，不会漂 —— 包括"接着上次没下完的那一份"
    // 这条口子：上面那个循环放行了、这一半却按重名拒掉的话，用户看到的是一句
    // 「已经有一个叫…的实例了」，而他刚才明明点了「接着下」。
    QString nameProblem;
    const InstanceFiles::ImportNameVerdict nameVerdict =
        InstanceFiles::checkImportTargetName(instanceName, &nameProblem);
    if (nameVerdict == InstanceFiles::ImportNameVerdict::Taken) {
        InlineMessage::warning(m_ui->overlay(), tr("换个名字"), nameProblem);
        return false;
    }
    const bool resumingImport =
        nameVerdict == InstanceFiles::ImportNameVerdict::OkResume;

    // 安装器（Forge/NeoForge）自己就是个 Java 程序（`java -jar <安装器>.jar ...`），所以
    // 要的运行环境和"这个版本要的 Java"是同一件事：版本 json 里的 javaVersion。这一段和
    // startLoaderInstall() 里那段是同一件事、同一个判据 —— 拿不到版本 json 就不猜主版本
    // （猜错比不猜更危险）。Fabric 和纯原版那条路一个进程都不起，用不上它。
    QString installerJava = m_javaSettings->javaPath();
    VersionManager::LoaderKind kind = VersionManager::LoaderKind::Fabric;
    const bool hasLoader =
        !pack.loaderKey.isEmpty() &&
        VersionManager::loaderKindForDependencyKey(pack.loaderKey, &kind);
    if (hasLoader && kind != VersionManager::LoaderKind::Fabric) {
        int requiredJavaMajor = 0;
        const QJsonObject gameJson = m_versionManager->getVersionJson(pack.gameVersion);
        if (!gameJson.isEmpty()) {
            requiredJavaMajor = gameJson["javaVersion"].toObject()["majorVersion"].toInt();
        }
        const QString configuredJava = installerJava;
        QString javaReason;
        if (!m_javaSettings->installerJavaFor(requiredJavaMajor, configuredJava, &installerJava,
                                             &javaReason)) {
            appendLog(tr("这个版本要的 Java 拿不到：%1").arg(javaReason));
            InlineMessage::warning(m_ui->overlay(), tr("Java 版本不够"), javaReason);
            return false;  // 还没进忙态，直接退出就是干净的状态
        }
        if (installerJava != configuredJava) {
            appendLog(tr("这个版本要 Java %1，装的时候改用 %2")
                          .arg(requiredJavaMajor)
                          .arg(installerJava));
        }
    }

    // 忙要在**点击这一刻**置上：导入的头两步（拉版本表 / 拉 profile）是异步的，中间那段
    // 时间按钮还能点的话用户会重复触发（和下载、装加载器两条路同一个理由）。
    m_importingInstanceId = instanceName;
    setDownloadUiBusy(true);
    m_progressThrottle.invalidate();
    appendLog(tr("开始导入整合包 %1 ...").arg(QFileInfo(zipPath).fileName()));
    if (resumingImport) {
        // 用户点的是「接着下」，日志里得留下这句 —— 不然排查的人只会看到"导入了一个已经
        // 存在的实例"，而那看起来像是覆盖了别人的东西。
        appendLog(tr("接着「%1」上次没下完的那一份往下下（已经下好的按 sha1 跳过）。")
                      .arg(instanceName));
    }
    m_versionManager->importModpack(zipPath, instanceName, pack, installerJava);
    return true;
}

void MainWindow::onVersionDownloadFinished(const QString &versionId, bool success,
                                           const QString &errorMessage) {
    // 这两笔要在 setDownloadUiBusy(false) **之前**清：那个函数会重灌"能启动的实例"，
    // 而判据里就有它们两个（见 installingVersionIds()）—— 顺序反了的话，刚装完的实例
    // 第一次刷新时还被滤着（要等下面那句再刷一次才出现），中间那一帧用户看到的是
    // "装完了可它不在列表里"。先清、后恢复界面，那一帧就不存在。
    const bool wasImporting =
        !m_importingInstanceId.isEmpty() && versionId == m_importingInstanceId;
    const bool wasInstallingInstance =
        !m_installingInstanceId.isEmpty() && versionId == m_installingInstanceId;
    if (wasImporting) {
        m_importingInstanceId.clear();
    }
    if (wasInstallingInstance) {
        m_installingInstanceId.clear();
    }
    // 「补全文件」那颗按钮那一笔（2026-09-26）。理由和上面两条逐字一样：它也走公共的
    // versionDownloadFinished(id, ok, msg)，也得在 setDownloadUiBusy(false)（那把"能启动的
    // 实例"重灌一遍，判据里就有它）之前清掉。
    const bool wasRepairing =
        !m_repairingInstanceId.isEmpty() && versionId == m_repairingInstanceId;
    if (wasRepairing) {
        m_repairingInstanceId.clear();
    }

    // 整批结束，无论成败都要把界面恢复回来
    setDownloadUiBusy(false);
    m_progressThrottle.invalidate();

    // 这一条讲的是整合包导入（数据层那边"一次点击 = 一个终态"，导入也走这同一个信号）。
    // 收尾那句话是**说给用户看的**（"整合包「X」导入好了…"），所以这里不再往上叠
    // 「版本 X 的文件已全部就绪」那一句 —— 对导入来说那句是错的：下的是这个整合包的
    // 模组，不是"某个版本的文件"。成功的样子是**实例列表里多一个**（顺便选中它）。
    if (wasImporting) {
        if (success) {
            appendLog(tr("✓ %1").arg(errorMessage));
            // 刷新会把下拉框和实例页那张树一起重灌（同一个函数里做的），新实例就在里面。
            refreshInstalledVersions();
            QComboBox *combo = m_ui->getInstalledVersionCombo();
            // 刷新只"保住原来选中的"，保不住"原来压根没选"：刚导入出来的这个顶上，
            // 省得用户还得自己去列表里找一遍（和下载完成那条路一个口径）。
            // NOTE: 那条口径是**两件事**：不但要"顶上"，还得"原来已经有选中的就别动"
            //       —— 用户自己挑的版本比"刚做完的那个"更重要。这里以前只有前半句，
            //       于是导入一个整合包会把他特意选好的版本顶掉（另外两条路都有
            //       currentIndex() < 0 这道守卫，只有这一条漏了）。
            if (combo->currentIndex() < 0) {
                if (const int index = combo->findData(versionId); index >= 0) {
                    combo->setCurrentIndex(index);
                }
            }
            updateStartButtonText();
            InlineMessage::information(m_ui->overlay(), tr("导入完成"), errorMessage);
        } else {
            appendLog(tr("✗ 整合包没有导入：%1").arg(errorMessage));
            InlineMessage::warning(m_ui->overlay(), tr("导入失败"), errorMessage);
        }
        return;
    }

    // 这一条讲的是"装一个带名字的实例"（安装页上点了「安装」）。它走的也是这同一个
    // 信号（数据层那边"一次点击 = 一个终态"），收尾那句话说的是**用户起的那个名字**。
    // 和整合包那段同一个道理：不再往上叠「版本 X 的文件已全部就绪」那一句。
    if (wasInstallingInstance) {
        if (success) {
            appendLog(tr("✓ %1").arg(errorMessage));
            // 装出来的实例要能被启动：刷"已安装版本"那一摊，并把它选上（刷新只保得住
            // "原来选中的"，保不住"原来压根没选"）。
            refreshInstalledVersions();
            QComboBox *installedCombo = m_ui->getInstalledVersionCombo();
            if (installedCombo->currentIndex() < 0) {
                if (const int index = installedCombo->findData(versionId); index >= 0) {
                    installedCombo->setCurrentIndex(index);
                }
            }
            updateStartButtonText();
            InlineMessage::information(m_ui->overlay(), tr("安装完成"), errorMessage);
        } else {
            appendLog(tr("✗ %1").arg(errorMessage));
            InlineMessage::warning(m_ui->overlay(), tr("安装失败"), errorMessage);
        }
        return;
    }

    // 这一条讲的是「补全文件」（实例设置页「启动」栏那颗按钮，2026-09-26）。走的也是公共的
    // versionDownloadFinished(id, ok, msg)：数据层那边"一次点击 = 一个终态"这条约定不破。
    // 这里要说的是**用户认得的那个名字**（实例名，不是目录名），而且不能说"下载完成" ——
    // 用户点的是"检查"，那就报"检查完"。
    if (wasRepairing) {
        const QString displayName = instanceDisplayName(versionId);
        if (success) {
            appendLog(tr("✓ 实例「%1」的文件校验完成：%2").arg(displayName, errorMessage));
            // 补全期间它被滤出了"能启动的实例"（installingVersionIds() 里有它），回来之后
            // 把它选中 —— 和上面那条同一种情况：刷新只保得住"原来选中的"，保不住"原来被
            // 滤掉、选中的那一格顺位落到了别人身上"。
            QComboBox *installedCombo = m_ui->getInstalledVersionCombo();
            if (installedCombo->currentIndex() < 0) {
                if (const int index = installedCombo->findData(versionId); index >= 0) {
                    installedCombo->setCurrentIndex(index);
                }
            }
            updateStartButtonText();
            InlineMessage::information(m_ui->overlay(), tr("检查完成"),
                                       tr("实例「%1」的文件都齐了。\n\n%2")
                                           .arg(displayName, errorMessage));
        } else {
            appendLog(tr("✗ 实例「%1」的文件没补齐：%2").arg(displayName, errorMessage));
            InlineMessage::warning(m_ui->overlay(), tr("文件没补齐"),
                                   tr("实例「%1」有文件没能拿到。\n\n%2")
                                       .arg(displayName, errorMessage));
        }
        return;
    }

    if (success) {
        appendLog(tr("✓ 版本 %1 下载完成：%2").arg(versionId, errorMessage));

        // 刷新"已安装版本"下拉框：client jar 已经落到
        // .minecraft/versions/<id>/<id>.jar，installedVersions() 下次扫盘就能看到它。
        // NOTE: 老代码注释里写着"这里不刷新列表：refreshVersionList() 会 clear() 掉
        // 列表、连用户选中的版本一起丢"—— 那个顾虑现在没有了，因为
        // fillVersionCombo() 自己负责把原来选中的版本捡回来。
        refreshInstalledVersions();

        // 刷新只能"保住"原来的选中项，保不住"原来压根没选"：那种情况下把刚下完的
        // 版本顶上，省得用户下完还要自己去下拉框里再找一遍。原来已经有选中项时
        // 不覆盖 —— 用户自己挑的版本比"最新下完的那个"更重要。
        QComboBox *installedCombo = m_ui->getInstalledVersionCombo();
        if (installedCombo->currentIndex() < 0) {
            // 按 id 找（findData），不是按显示文本（findText）——刚下完的版本可能
            // 已经有自定义名，显示文本不一定是版本号。
            if (const int index = installedCombo->findData(versionId); index >= 0) {
                installedCombo->setCurrentIndex(index);
            }
        }

        InlineMessage::information(m_ui->overlay(), tr("下载完成"),
                                 tr("版本 %1 的文件已全部就绪。\n\n%2").arg(versionId, errorMessage));
    } else {
        // 失败就不刷新了：没成功就不该有新条目冒出来（jar 大概率没落地）
        appendLog(tr("✗ 版本 %1 下载失败：%2").arg(versionId, errorMessage));
        InlineMessage::warning(m_ui->overlay(), tr("下载失败"),
                             tr("版本 %1 有文件未能下载完成。\n\n%2").arg(versionId, errorMessage));
    }
}

void MainWindow::onVersionDetailsFetched(const QString &versionId, const QJsonObject &versionJson) {
    if (versionJson.isEmpty()) {
        appendLog(tr("获取版本 %1 的信息失败。").arg(versionId));
        setDownloadUiBusy(false);
        InlineMessage::warning(m_ui->overlay(), tr("下载失败"),
                             tr("无法获取版本 %1 的信息，请检查网络后重试。").arg(versionId));
        return;
    }

    appendLog(tr("版本信息获取成功，开始下载游戏文件..."));
    const QString versionsPath = AppPaths::versionDir(versionId);
    // 这条槽是 VersionManager 的 versionDetailsFetched 唤起来的，而那条信号是在
    // DownloadManager 收完版本 json 的那个槽里同步发出来的 —— 直接在这里起下载批次，
    // 批次就整个跑在那条栈上。排到下一轮（见 VersionManager::deferNextStep 的 NOTE）。
    m_versionManager->deferNextStep([this, versionId, versionsPath] {
        m_versionManager->downloadVersion(versionId, versionsPath);
    });
}