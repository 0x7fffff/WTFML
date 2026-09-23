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

#include "..\ui\GroupDialog.hpp"
#include "..\ui\InstanceSettingsDialog.hpp"
#include "..\ui\JavaMismatchDialog.hpp"
#include "..\ui\AccountSkinDialog.hpp"
#include "..\ui\MainWindowUI.hpp"
// 主页右栏那个小人怎么画：几何、投影、默认外观、头像裁切全在它里面（纯函数 + 值）。
#include "..\ui\SkinModel.hpp"
// 加载器那棵树底下每一条版本号前面挂的**它自己那个加载器的标志**（见
// fillLoaderVersionItems()）：那几条是"Fabric 能装哪些版本"，前面顶个草方块会让人
// 以为是原版号。树的顶层那五条由 MainWindowUI 那边给图标，这里只管子条目。
#include "..\ui\VersionIcons.hpp"

// 添加以下头文件（防止hpp变动导致文件失效）
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>

#include "auth/AccountStore.hpp"
#include "auth/AuthManager.hpp"
#include "auth/SkinApi.hpp"
#include "launcher/LauncherManager.hpp"
#include "manage/FolderManager.hpp"
#include "manage/InstanceFiles.hpp"
#include "manage/InstanceGroupStore.hpp"
#include "manage/ModpackImport.hpp"
#include "manage/PlayerSkinStore.hpp"
#include "manage/VersionDisplay.hpp"
#include "manage/VersionManager.hpp"
#include "settings/EffectiveJavaSettings.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include "settings/LauncherSettings.hpp"
#include "util/AppPaths.hpp"
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
 *
 * 两种形态，由调用方给进来的 buckets 决定（它才知道有没有真实分组）：
 *   - **只有一格、groupId 为空** → 平铺，不插组头。这是"还没分过组"的样子，
 *     和换树之前那张列表逐项一致。
 *   - 否则每格画一个组头，实例挂在它下面。**空组照样画**："先把组建好、再往里
 *     放东西"是常见顺序，空组不画的话用户会以为没建成。
 *
 * NOTE: 灌完**不动选中项**（和 fillVersionList 一致），收尾由调用方 syncTreeToCombo()。
 * NOTE: 每个实例条目的 tooltip 是**目录的完整路径**。显示名和目录名可能不一样
 *       （自定义名、拼进去的加载器名、或者用户在资源管理器里改过目录名），而
 *       "这个实例到底在哪个目录"是排查问题时唯一要问的东西。
 */
void fillInstanceTree(QTreeWidget *tree, const QList<InstanceGroupBucket> &buckets,
                      const VersionNameOptions &options) {
    const QSignalBlocker blocker(tree);
    tree->clear();

    const bool flat = buckets.size() == 1 && buckets.first().groupId.isEmpty();
    for (const InstanceGroupBucket &bucket : buckets) {
        QTreeWidgetItem *parent = nullptr;
        if (!flat) {
            parent = new QTreeWidgetItem(tree);
            parent->setText(0, MainWindowUI::groupItemText(bucket.name, bucket.parts.size()));
            parent->setData(0, MainWindowUI::groupIdRole(), bucket.groupId);
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
            // 默认展开：折起来的组里有什么，用户得点一下才知道，而这里正是"想让
            // 用户看见有什么"的地方。他折起来之后的重灌会再展开 —— 重灌本来就
            // 不负责记状态（连选中项都是显式对齐的）。
            parent->setExpanded(true);
        }
        // 平铺时挂在**不可见的根**下面，那和"顶层条目"是一回事，而且省掉了
        // "parent 是空的就换一个构造函数"那条分叉（两个构造函数收的类型不一样）。
        QTreeWidgetItem *holder = parent != nullptr ? parent : tree->invisibleRootItem();
        for (const VersionDisplayParts &part : bucket.parts) {
            auto *item = new QTreeWidgetItem(holder);
            item->setText(0, formatVersionName(part, options));
            item->setData(0, MainWindowUI::instanceIdRole(), part.id);
            item->setToolTip(0, QDir::toNativeSeparators(AppPaths::versionDir(part.id)));
        }
    }
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
        to.append(item);
    }
    return to;
}

// 「一个实例的启动五项覆盖」的两份表示之间的转换。理由和上面那两段一样：ui/ 不 include
// src/，所以 InstanceSettingsDialog 有自己的 Override、settings/ 有自己的
// JavaSettingsOverride，逐字段相同、由这两段循环在两边搬。

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

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_ui(new MainWindowUI(this)),
      m_settings(new LauncherSettings(this)),
      m_launcherManager(new LauncherManager(this)),
      m_authManager(new AuthManager(this)),
      m_folderManager(new FolderManager(m_settings, this)),
      m_versionManager(new VersionManager(this)),
      m_javaSettings(new GlobalJavaSettings(this)),
      m_groupStore(new InstanceGroupStore(this)),
      m_skinStore(new PlayerSkinStore(this)),
      m_skinApi(new SkinApi(this)) {
    // 安装页那棵树默认停在「不装（纯原版）」那一条（打开安装页时也是它，见
    // openInstallPage()）—— 没进过安装页之前这一位也得是个真值：updateLoaderRows()
    // 在下面就会被调到。初值不写在头文件里是因为那边只前置声明了 MainWindowUI。
    m_selectedLoaderIndex = MainWindowUI::modLoaderNoneIndex();
    setupConnections();
    // 设置页先按盘上的值摆好（不扫 Java，那一段等第一次切到设置页，见
    // refreshJavaList()）。必须在下面 fetchAvailableVersions() **之前**：
    // 清单请求要不要走镜像就是在这一步交给 VersionManager 的。
    applySettingsToUi();
    // 上次用的是个**离线账号**：把名字接回 AuthManager，并且把「离线模式」勾上。
    // 两件事都得在这里做，而且都在下面 updateBottomBar() 之前：
    //   - AuthManager 构造函数只读活动槽，而离线账号在盘上**没有凭据**，它读出来的是占位
    //     的 "Player" —— 不接回来的话底栏和 --username 里是个假名字；
    //   - 不勾「离线模式」的话，启动那条路会去要凭据，而盘上一份都没有，用户拿到的是
    //     "请先登录"，一个死胡同（底栏却明明写着他的名字，那种状态最没法解释）。
    {
        const QString active = AccountStore::activeUuid();
        if (AccountStore::isOffline(active)) {
            m_authManager->setOfflinePlayer(AccountStore::nameOf(active));
            m_ui->getOfflineModeCheckBox()->setChecked(true);
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
    m_versionManager->fetchAvailableVersions();
}

MainWindow::~MainWindow() { delete m_ui; }

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
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("游戏正在运行"));
    const QString versionId = m_launcherManager->runningVersionId();
    box.setText(versionId.isEmpty()
                    ? tr("游戏还在运行。\n\n关掉启动器**不会**关掉游戏 —— 它会继续跑，"
                         "存档、联机都照常。\n要现在关掉启动器吗？")
                    : tr("游戏「%1」还在运行。\n\n关掉启动器**不会**关掉游戏 —— 它会继续跑，"
                         "存档、联机都照常。\n要现在关掉启动器吗？")
                          .arg(versionId));
    // 按钮文字自己写：启动器没装 Qt 的中文翻译，标准按钮在界面上是英文的 Yes/No。
    QPushButton *quitButton = box.addButton(tr("关掉启动器"), QMessageBox::AcceptRole);
    QPushButton *cancelButton = box.addButton(tr("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(cancelButton);
    box.exec();
    if (box.clickedButton() != quitButton) {
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
    // 底栏名字下面那行写的是"这次启动用哪种身份"，勾/取消「离线模式」就得当场跟着变 ——
    // 不然它要等到下一次进度刷新（几百毫秒一次、而且只有下载/启动时才有）才更新，
    // 用户看到的是旧答案。
    connect(m_ui->getOfflineModeCheckBox(), &QCheckBox::toggled, this, [this] { updateBottomBar(); });
    // 「账号与外观…」那两个入口（主页右栏 3D 预览下面那个 + 设置页「账户」那格里的）。
    // 同一件事、同一个窗 —— 所以接的是同一个槽。
    connect(m_ui->getAccountSkinButton(), &QPushButton::clicked, this,
            &MainWindow::onAccountSkinButtonClicked);
    connect(m_ui->getAccountSkinSettingsButton(), &QPushButton::clicked, this,
            &MainWindow::onAccountSkinButtonClicked);
    connect(m_ui->getAddFolderButton(), &QPushButton::clicked, this,
            &MainWindow::onAddFolderClicked);
    connect(m_ui->getRemoveFolderButton(), &QPushButton::clicked, this,
            &MainWindow::onRemoveFolderClicked);
    connect(m_ui->getRenameInstanceButton(), &QPushButton::clicked, this,
            &MainWindow::onRenameInstanceClicked);
    connect(m_ui->getGroupInstanceButton(), &QPushButton::clicked, this,
            &MainWindow::onGroupInstanceClicked);
    connect(m_ui->getInstanceSettingsButton(), &QPushButton::clicked, this,
            &MainWindow::onInstanceSettingsClicked);
    connect(m_ui->getImportModpackButton(), &QPushButton::clicked, this,
            &MainWindow::onImportModpackClicked);
    // 左边列表选中哪一份 .minecraft，启动器就换到哪一份上（PCL 的卡片是同一个交互）。
    connect(m_ui->getFolderList(), &QListWidget::currentRowChanged, this,
            &MainWindow::onFolderSelectionChanged);
    // FolderManager 才是改状态的那一方：加载/删除目录、当前目录不存在时的退回，
    // 都会从它这儿发信号出来，界面只管跟着重画。
    connect(m_folderManager, &FolderManager::foldersChanged, this,
            &MainWindow::refreshFolderList);
    connect(m_folderManager, &FolderManager::currentFolderChanged, this, [this] {
        applyCurrentFolderToPaths();
        // 换了一整份 .minecraft，两件跟着"上一份"走的事要重置：
        //   - "目录名和 jar 名对不上"的告警只报一次，新目录得重新能报；
        //   - 本机 Java 列表是**按根目录**扫的（自管 Java 在 <数据目录>/java 下），
        //     下次进设置页要重扫一遍。NOTE: 重扫是"往里加"，下拉框里已经摆着的旧条目
        //     （上一个根目录里那份自管 Java）不会因此消失 —— 它指着的是个真实存在的
        //     java.exe，选它也能跑，要重开启动器才会清干净。
        m_reportedUnrecognizedVersions = false;
        m_javaListScanned = false;
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
            });
    // 树 → 下拉框。点树里的一行 = 在上面把它选中（用户要求的行为）。
    // 选中组头时不动作（syncComboToTree 里的守卫）—— 点一个组的标题不是在挑版本。
    connect(m_ui->getInstanceList(), &QTreeWidget::currentItemChanged, this, [this] {
        syncComboToTree(m_ui->getInstalledVersionCombo(), m_ui->getInstanceList());
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
    const auto openFromTreeItem = [this](QTreeWidgetItem *item, int) {
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
    connect(m_ui->getVersionTree(), &QTreeWidget::itemClicked, this, openFromTreeItem);
    // 键盘走这条路（回车/空格"激活"一个条目）—— 只接鼠标点击的话，键盘用户永远进不了
    // 安装页。两条路都指向同一份 lambda，重复进去是幂等的（openInstallPage 只是重设一遍）。
    connect(m_ui->getVersionTree(), &QTreeWidget::itemActivated, this, openFromTreeItem);
    connect(m_ui->getInstallBackButton(), &QPushButton::clicked, this,
            &MainWindow::backToVersionTree);
    connect(m_ui->getInstallButton(), &QPushButton::clicked, this,
            &MainWindow::onInstallButtonClicked);
    // 名称框：每敲一个字都要重算那句小字（重名提示要"实时"——用户原话）。
    connect(m_ui->getInstallNameEdit(), &QLineEdit::textChanged, this,
            &MainWindow::onInstallNameEdited);
    // 加载器那棵树（安装页）：**点一下**那条里的两件事分开办 ——
    //   · 点顶层（一个加载器）：选中它，然后展开/收起那一条（用户点的是行最右边那个
    //     >/V，整个行都是热区）—— 和上一页版本树那边的点法一模一样；
    //   · 点子条目（一个版本号）：把这一条钉在那个版本上，**收起**那条，并把版本号
    //     写回它的第二行小字（用户原话："选择版本以后收缩，显示版本"）。
    // 共用一份 lambda 而不是槽：槽签名里带 QTreeWidgetItem* 就得让 moc 也认得这个类型
    // （版本树那边同一个理由，见 openFromTreeItem 上面那段）。
    const auto onLoaderTreeClicked = [this](QTreeWidgetItem *item, int) {
        if (item == nullptr) {
            return;
        }
        if (item->parent() == nullptr) {
            // 顶层：先记下"选中的是它"（这是产品状态，见 m_selectedLoaderIndex 那段），
            // 再展开/收起。选中和展开是两件事：收起一条加载器不代表"改成不装它了"。
            selectLoader(item->data(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole).toInt());
            item->setExpanded(!item->isExpanded());
            return;
        }
        QTreeWidgetItem *loader = item->parent();
        const int index =
            loader->data(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole).toInt();
        // 版本号存在 userData 里（空串 = 「自动（推荐）」那一条，见 loaderVersionAutoText()）。
        const QString version =
            item->data(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole).toString();
        // 那条"正在获取…"/"列表没取到"是灰的、**不是能选的东西**：点它什么都不该发生
        // （不然"列表没取到"会被当成一个选中的版本号填进默认名里）。
        if (!item->isDisabled()) {
            m_chosenLoaderVersion.insert(index, version);
            selectLoader(index);
            loader->setExpanded(false);
            // 钉的版本号是默认名的一部分（`1.20.1-Forge_47.4.10`），换一条就得重算。
            updateInstallName();
        }
    };
    connect(m_ui->getLoaderTree(), &QTreeWidget::itemClicked, this, onLoaderTreeClicked);
    // 键盘走这条路（回车/空格"激活"一个条目）：两条路指向同一份 lambda。
    connect(m_ui->getLoaderTree(), &QTreeWidget::itemActivated, this, onLoaderTreeClicked);
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
            });

    // ---------- 设置页 ----------
    // 「Java 版本」：选自动，或者钉死一个具体运行时。
    connect(m_ui->getJavaVersionCombo(), &QComboBox::currentIndexChanged, this,
            &MainWindow::onJavaSelectionChanged);
    // 「实例指定的 Java 版本不符时提醒」这个总闸。管的是**启动时那个三岔口**
    // （onStartButtonClicked 里问的那一次），不是这一页的显示。
    connect(m_ui->getJavaWarnMismatchCheck(), &QCheckBox::toggled, this,
            &MainWindow::onJavaWarnMismatchChanged);
    // 内存：先接「自动 / 手动」总开关，再接两个数字框。
    // 自动档下两个框是禁用的（见 refreshMemoryUi），所以"用户改数字"这件事只可能发生在
    // 手动档，onMemoryChanged 里那句 autoMemory() 守卫是给程序内部 setValue 兜底的。
    connect(m_ui->getMemoryAutoCheck(), &QCheckBox::toggled, this,
            &MainWindow::onMemoryAutoChanged);
    // 两个 spin 共用一个槽。谁改的在这里区分不了（也不该区分，见 onMemoryChanged）。
    connect(m_ui->getMemoryMaxSpin(), &QSpinBox::valueChanged, this,
            &MainWindow::onMemoryChanged);
    connect(m_ui->getMemoryMinSpin(), &QSpinBox::valueChanged, this,
            &MainWindow::onMemoryChanged);
    connect(m_ui->getLauncherBehaviourCombo(), &QComboBox::currentIndexChanged, this,
            &MainWindow::onLauncherBehaviourChanged);
    connect(m_ui->getDownloadSourceCombo(), &QComboBox::currentIndexChanged, this,
            &MainWindow::onDownloadSourceChanged);
    // JVM 参数刻意**不**接 textChanged：边打字边存会把打到一半的半条参数写进配置，
    // 而它是直接塞进命令行的东西，半条参数会让游戏起不来。改完点「应用」。
    connect(m_ui->getApplyExtraJvmArgsButton(), &QPushButton::clicked, this,
            &MainWindow::onApplyExtraJvmArgsClicked);
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

    // 游戏进程信号
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
                   const QStringList &versions, const QString &errorMessage) {
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
    m_ui->getStartButton()->setEnabled(!busy);
    // 安装页那一整层（名称框、四个加载器、安装按钮、返回）一起灰：在装什么是点击那一刻
    // 定下的，这时候改名字/换加载器并不会把手上这次安装换掉（真正干活的 VersionManager
    // 已经拿着那个枚举在跑了），留着能改只会让人以为改生效了。
    m_ui->getInstallButton()->setEnabled(!busy);
    m_ui->getInstallNameEdit()->setEnabled(!busy);
    m_ui->getVersionTree()->setEnabled(!busy);
    // 从安装页上退回去也不给（退回去那棵树也是灰的）：那会让人以为"这次安装取消了"。
    m_ui->getInstallBackButton()->setEnabled(!busy);
    // 两个下拉框一起灰掉。在下什么，在点击那一刻就已经交给 VersionManager 了，
    // 这时候改选中项并不会改变在下的是哪个版本 —— 灰掉纯粹是不让"界面显示选的 A、
    // 实际在下的却是 B"这种事发生，免得看的人误判。
    m_ui->getInstalledVersionCombo()->setEnabled(!busy);
    // 实例页那张树是同一份数据的另一种看法，也一起灰掉：不然用户能在里面点来点去、
    // 看着选中的版本变了，而实际在下的是另一个。
    m_ui->getInstanceList()->setEnabled(!busy);
    // 「导入整合包…」也一起灰：导入期间再点一次，选文件和问名字那两个框走完，
    // 数据层才会用一句"已有任务正在进行"把它顶回来 —— 白走一趟对话框。
    m_ui->getImportModpackButton()->setEnabled(!busy);
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
    updateFolderButtons();

    QProgressBar *progressBar = m_ui->getProgressBar();
    if (busy) {
        // 还不知道总任务数：先转成不确定态的滚动条，拿到 stats 再换成确定态
        progressBar->setRange(0, 0);
        m_ui->getStatusLabel()->setText(tr("正在准备下载..."));
    } else {
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
    // 名字下面那行：这次启动会用哪种身份。**判据只看两个开关**（离线勾没勾、有没有
    // 登录态），不自己推"应该用哪个" —— 真正的判据在 onStartButtonClicked() 里，两处
    // 一旦各写一套就会出现"底栏说正版、启动时却说请先登录"。
    // NOTE: loggedIn() 是"盘上有账户"的意思（AuthManager::loggedIn() = uuid 非空），
    //       令牌过期了它照样是 true —— 那没关系：这一行说的是**身份**，令牌过没过期是
    //       启动那一刻刷新才知道的事（那时失败会有弹窗说清原因）。
    m_ui->getAccountTypeLabel()->setText(
        m_ui->getOfflineModeCheckBox()->isChecked()
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
    // 判据本身放在 MainWindowUI::bottomBarShowsProgress() 里，那儿是它唯一的出处。
    const bool showProgress = MainWindowUI::bottomBarShowsProgress(
        m_launchInFlight, m_downloadInFlight, !m_syncWaitStatus.isEmpty());
    progressBar->setVisible(showProgress);
    statusLabel->setVisible(showProgress);
    if (m_launchInFlight) {
        // 启动进度没有可测量的百分比（JVM 和游戏自己在初始化，没有任何进度回调），
        // 所以是不确定态的滚动条。等游戏吐出第一行输出（或进程结束）就收起来。
        progressBar->setRange(0, 0);
        statusLabel->setText(
            MainWindowUI::launchingStatusText(instanceDisplayName(m_launchingVersionId)));
    } else if (!m_syncWaitStatus.isEmpty()) {
        // 同步等待那一段：不知道要等多久（网络 / 下 Java / 进程启动），所以也是不确定态。
        // 文字就是进那一段之前写下的"在等什么"。
        progressBar->setRange(0, 0);
        statusLabel->setText(m_syncWaitStatus);
    } else if (!m_downloadInFlight) {
        // 没有下载时把进度条复位：下一次下载开始前它会先显示上次的旧值，闪一下。
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
        statusLabel->clear();
    }
    // 下载态下的文字和数值由 updateDownloadProgressUi() 维护，这里不覆盖。

    // ---------- 右：当前实例运行状态 ----------
    QLabel *runStateLabel = m_ui->getRunStateLabel();
    QLabel *runStateDetailLabel = m_ui->getRunStateDetailLabel();
    const QString running = m_launcherManager->runningVersionId();
    if (running.isEmpty()) {
        // 没有实例在跑：左下角整格藏起来，不显示"未运行"这种废话
        runStateLabel->setVisible(false);
        runStateDetailLabel->setVisible(false);
        return;
    }

    runStateLabel->setText(MainWindowUI::runStateRunningText());
    runStateLabel->setVisible(true);
    // 第二行要不要出，判据在 runStateShowsDetail() 里（那里也是它唯一的出处）。
    const bool showDetail = MainWindowUI::runStateShowsDetail(
        m_ui->isHomeTabCurrent(), selectedVersionId(m_ui->getInstalledVersionCombo()), running);
    runStateDetailLabel->setVisible(showDetail);
    runStateDetailLabel->setText(showDetail ? instanceDisplayName(running) : QString());
}

void MainWindow::startPlayerSkinLoad() {
    // 先摆默认外观。**这一步之后，右栏那个小人任何时候都不是空白的** —— 未登录、
    // 还没取到、取不到，看到的都是它。默认外观是程序画的经典配色（见
    // SkinModel::defaultSteveSkin()），不是把官方那张 Steve 贴图打进仓库。
    applyPlayerSkin(QString(), QString(), false);
    // 上一次那个账号的档案要清掉：换账号登录时它已经没用了，留着会让"贴图回来时
    // 认领"那一步把上一套外观当成这一套。
    m_skinProfile = PlayerSkinProfile();

    if (!m_authManager->loggedIn()) {
        // 没登录就没有 uuid，连问都不用问（问也问不出东西来）。那句话现在是
        // "未登录，显示的是默认外观"，由上面那次 updateSkinHint() 写好。
        return;
    }
    const QString uuid = m_authManager->playerUuid();
    // 离线模式那个勾选框的意思是"别联网"（启动时会走离线登录），这时候还发一个外观
    // 请求是自相矛盾的 —— 所以整条联网路径都跳过，只认已经躺在缓存里的那一份。
    const bool offline = m_ui->getOfflineModeCheckBox()->isChecked();

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

void MainWindow::onSkinProfileReady(const PlayerSkinProfile &profile) {
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
    if (!PlayerSkinStore::saveProfile(m_authManager->playerUuid(), profile, &error)) {
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
            return;
        }
        applyPlayerSkin(m_skinPath, path, slim);
        return;
    }
    // 两个都对不上：这是一次**过期**的回调（档案已经换成别人了）。丢掉 —— 照着它画
    // 等于把上一个账号的皮肤画回来。
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
    if (!m_skinPath.isEmpty()) {
        hint->setText(MainWindowUI::skinHintLoadedText(name, !m_capePath.isEmpty()));
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
    //       QMessageBox 挡住操作，绝不能只写这里 —— 从资源管理器双击启动时，
    //       这里写的东西没有任何地方能显示出来。
    qDebug().noquote() << QStringLiteral("[WTFML]") << text;
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
    QList<VersionDisplayParts> parts;
    parts.reserve(installed.size());
    for (const QString &id : installed) {
        // 每个版本读一次 <id>/wtfml.meta（几百字节的可选文件，绝大多数版本没有）。
        // 刻意不缓存：这个文件会被"重命名"功能改写，缓存了就会变成"改了名不生效"。
        // 开销上不必担心 —— 真机上实例数可以到 61 个，而这个函数一次会话没有调用上界。
        parts.append(m_versionManager->displayParts(id));
    }
    // 主页上"已安装版本"只有下拉框这一个控件了（右栏那张列表 2026-09 去掉，见
    // MainWindowUI 里主页那一段），所以这里只灌下拉框，同步选中项也只剩实例页那张列表。
    fillVersionCombo(combo, parts, m_settings->versionNameOptions());

    // 实例页：顶上写清这是从哪一份 .minecraft 里扫出来的（左边换个文件夹它就跟着变），
    // 右边那张树灌同一批数据（按分组摆）。它本身不是"又一份数据源"，只是同一个东西
    // 摆在文件夹旁边更好看 —— 点击行为是"点一下等于在主页把它选中"（见 setupConnections）。
    const QString root = AppPaths::minecraftDir();
    QString folderText = tr("当前文件夹：%1").arg(QDir::toNativeSeparators(root));
    folderText += QDir(root).exists() ? tr("（%1 个实例）").arg(installed.size())
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
    for (const InstanceGroupStore::Group &group : groups) {
        InstanceGroupBucket bucket;
        bucket.groupId = group.id;
        bucket.name = group.name;
        bucketOfGroup.insert(group.id, buckets.size());
        buckets.append(bucket);
    }
    QList<VersionDisplayParts> ungrouped;
    for (const VersionDisplayParts &part : parts) {
        const QString groupId = m_groupStore->groupIdOf(instanceKey(part.id));
        const auto found = bucketOfGroup.constFind(groupId);
        if (found != bucketOfGroup.constEnd()) {
            buckets[found.value()].parts.append(part);
        } else {
            ungrouped.append(part);
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
        // 它**总是**画出来（哪怕 0 个）——树里少一格、下一帧又出现，比一直留着更难理解。
        InstanceGroupBucket rest;
        rest.name = MainWindowUI::ungroupedGroupText();
        rest.parts = ungrouped;
        buckets.append(rest);
    }
    fillInstanceTree(m_ui->getInstanceList(), buckets, m_settings->versionNameOptions());
    syncTreeToCombo(combo, m_ui->getInstanceList());

    // 占位文案随上下文变。**两个串都必须非空**：空占位文案会让 QComboBox 在往空
    // 模型里插第一批数据时静默选中第 0 项（qcombobox.cpp:1753-1759），而这里必须
    // 停在"没选"，否则用户会在毫无察觉的情况下启动一个自己没挑过的版本。
    //
    // 这一段同时治掉一个真实的用户报告：「打开启动器时显示没有已安装版本」——
    // 真因就是这句以前写死成"暂无已安装版本"，灌完数据后没人改它，于是明明装了
    // 好几个版本，画面上却说着一个都没有。
    combo->setPlaceholderText(installed.isEmpty() ? MainWindowUI::noInstalledVersionPlaceholder()
                                                 : MainWindowUI::chooseVersionPlaceholder());

    // 刷完必须手动同步按钮文字和 tooltip：上面 fillVersionCombo() 里套了
    // QSignalBlocker，重灌过程不会发信号，光靠 connect 那条接不到。
    updateStartButtonText();
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
    // 只填树里那四条分类：它们唯一的数据源就是在线的版本清单（和已安装那一摊完全
    // 不相干 —— 以前清单为空时这里会退回显示本地已安装版本，两个数据源混在一张列表里，
    // 用户分不清点下去到底是启动还是下载）。四堆怎么分是 VersionManager 的事
    // （它那边只有 VersionCategory 一处判据，含那张写死的愚人节版名单）。
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

    const QSignalBlocker blocker(tree);
    tree->clear();
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
        for (const MinecraftVersion &version : channel.versions) {
            auto *child = new QTreeWidgetItem(top);
            child->setData(MainWindowUI::versionTreeKeyColumn(), Qt::UserRole, version.id);
            child->setText(MainWindowUI::versionTreeKeyColumn(), version.id);
            // 版本号下面那行小字：发布时间（精确到日）。清单里没有 / 认不出来的，那行就不画。
            child->setData(MainWindowUI::versionTreeKeyColumn(), MainWindowUI::rowSubTextRole(),
                           releaseDateText(version.releaseTime));
        }
        if (expandedKeys.contains(channel.key)) {
            top->setExpanded(true);
        }
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
    // （Forge 每个游戏版本能装的版本完全不同），连列表一起清掉，展开时现拉一次。
    m_chosenLoaderVersion.clear();
    for (int i = 0; i < MainWindowUI::modLoaderOptionTexts().size(); ++i) {
        QTreeWidgetItem *item = loaderItem(i);
        if (item == nullptr) {
            continue;
        }
        // 收起来 + 清掉上次灌进去的子条目（**不能**只 clear 整棵树：那样顶层那五条也
        // 一起没了）。清空本身的意思就是"这一条现在还不知道有哪些版本"。
        item->setExpanded(false);
        // takeChildren() 交出来的那些条目已经不带爹了，得自己删 —— 不删就是每进一次
        // 安装页漏一批（几十上百个 QTreeWidgetItem）。
        qDeleteAll(item->takeChildren());
    }
    m_loaderVersionCache.clear();
    updateLoaderRows();
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
    return pinned.isEmpty() ? QStringLiteral("%1-%2").arg(m_installGameVersion, loaderName)
                            : QStringLiteral("%1-%2_%3").arg(m_installGameVersion, loaderName,
                                                             pinned);
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
    // 空串就是"不钉版本"（第 0 条那个「自动（推荐）」选的也是它）—— 这件事不靠一条
    // 特判表达，见 loaderVersionAutoText()。没进过 map 的下标等于没钉。
    return m_chosenLoaderVersion.value(loaderIndex);
}

void MainWindow::updateLoaderRows() {
    // 那棵树的样子**全部**在这一处决定（别处一个 setText/setData 都不许写）：
    //   · 选中的那一条：第二行小字写它选的版本号（没钉就是「自动（推荐）」）；
    //   · 其余几条加载器：第二行小字写「与 X 不兼容」（用户要的那句话）；
    //   · 「不装（纯原版）」那条：没有第二行（它不是加载器，不跟谁冲突）。
    // NOTE: 这几条**仍然可点** —— 点它就是改用那一个（本启动器一次只装一个加载器，
    //       所以"不兼容"说的是"两个不能同时装"，不是"这个点不得"）。今天点它和点
    //       一个单选钮是一回事，用户不用先绕回「不装」再选。
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
                subText = pinned.isEmpty() ? MainWindowUI::loaderVersionAutoText() : pinned;
            } else {
                subText = MainWindowUI::loaderConflictText(selectedName);
            }
        }
        // 角色是 rowSubTextRole()：空串 = 那一行不画第二行小字（代理那边统一处理）。
        item->setData(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::rowSubTextRole(), subText);
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
    // 选了一条加载器 = 该把它能装的版本列表备好了（拉过的话直接用缓存）。
    ensureLoaderVersions(m_selectedLoaderIndex);
}

void MainWindow::ensureLoaderVersions(int loaderIndex) {
    VersionManager::LoaderKind kind = VersionManager::LoaderKind::Fabric;
    if (!VersionManager::loaderKindFromIndex(loaderIndex, &kind)) {
        return;  // 「不装（纯原版）」那一条：没有版本可拉
    }
    if (m_installGameVersion.isEmpty()) {
        return;
    }
    QTreeWidgetItem *item = loaderItem(loaderIndex);
    if (item == nullptr) {
        return;
    }

    // 已经拉到过就不要再拉一次（用户来回点两条比一比是常事，每次都发一发 60~250 KB
    // 的请求纯属浪费）。缓存按"加载器 + 游戏版本"存 —— 换游戏版本时那一份就作废了
    // （Forge 每个游戏版本能装的版本完全不同），所以 openInstallPage() 会清掉整个缓存。
    const QString cacheKey = QStringLiteral("%1|%2").arg(int(kind)).arg(m_installGameVersion);
    if (m_loaderVersionCache.contains(cacheKey)) {
        fillLoaderVersionItems(loaderIndex, m_loaderVersionCache.value(cacheKey));
        return;
    }
    if (item->childCount() > 0) {
        return;  // 已经在手上（列表来自缓存之外的某次填充），不用重来
    }

    // 还没到：先摆一条灰的"正在获取…"占住那一条底下的位置（空着一条看着像"这个加载器
    // 一个版本都没有"），再把请求发出去。回复在 onLoaderVersionsFetched() 里收 ——
    // 它按"加载器 + 游戏版本"认领，所以这里不用记"我在等哪一发"。
    auto *waiting = new QTreeWidgetItem(item);
    waiting->setText(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::loaderVersionFetchingText());
    waiting->setDisabled(true);  // 灰的、点不动：它不是能选的东西（选了也不该有反应）
    m_versionManager->requestLoaderVersions(m_installGameVersion, kind);
}

void MainWindow::fillLoaderVersionItems(int loaderIndex, const QStringList &versions) {
    QTreeWidgetItem *item = loaderItem(loaderIndex);
    if (item == nullptr) {
        return;
    }
    // 别动用户已经选中的那一条：列表可能刚好是"同一条"（缓存命中的那次会走到这里），
    // 重灌一次把它顶回第 0 条就是"我选的 47.4.10 自己变回自动了"。选中的版本号在
    // m_chosenLoaderVersion 里，所以重灌只是把条目重建一遍、再把光标放回去。
    const QString chosen = pinnedLoaderVersion(loaderIndex);
    const bool wasExpanded = item->isExpanded();
    // 连"正在获取…"那条一起清掉：它就是等这一份列表用的。
    qDeleteAll(item->takeChildren());
    // 第 0 条 = 不钉版本（让安装器按老规矩挑，见 loaderVersionAutoText()）；它的
    // userData 是**空串**，"没钉版本"这件事在数据上就靠这个空串表达。
    auto *autoItem = new QTreeWidgetItem(item);
    autoItem->setText(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::loaderVersionAutoText());
    autoItem->setData(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole, QString());
    autoItem->setIcon(MainWindowUI::loaderTreeKeyColumn(),
                      QIcon(VersionIcons::loaderMark(loaderIndex, MainWindowUI::versionTreeIconSize())));
    QTreeWidgetItem *chosenItem = autoItem;
    for (const QString &version : versions) {
        auto *child = new QTreeWidgetItem(item);
        child->setText(MainWindowUI::loaderTreeKeyColumn(), version);
        child->setData(MainWindowUI::loaderTreeKeyColumn(), Qt::UserRole, version);
        // 子条目前面挂**它自己那个加载器**的标志（那一列列出来的是"Fabric 能装哪些版本"，
        // 前面顶个草方块会让人以为是原版号）。
        child->setIcon(MainWindowUI::loaderTreeKeyColumn(),
                       QIcon(VersionIcons::loaderMark(loaderIndex,
                                                      MainWindowUI::versionTreeIconSize())));
        if (version == chosen) {
            chosenItem = child;
        }
    }
    // 光标放回"现在钉着的那一条"（没钉就是「自动（推荐）」）—— 展开之后一眼看到的就是
    // 它。**这不改选中状态**：选中是 m_selectedLoaderIndex 的事，这里只是画光标。
    QTreeWidget *tree = m_ui->getLoaderTree();
    if (tree != nullptr) {
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
                                         const QStringList &versions, const QString &errorMessage) {
    // 这一发回复可能已经过时了（用户换了游戏版本）。按"这条回复讲的是不是现在这一页"来判，
    // 过时的直接丢 —— 填错地方的下场是"点了 Fabric 却看到 Forge 的版本号"，那不报错、
    // 只会装错东西。
    if (gameVersion != m_installGameVersion) {
        return;
    }
    // 缓存键、失败时那句"是哪个加载器"的文案都按**枚举**说（缓存键必须和
    // ensureLoaderVersions() 里那把一致），所以这里翻译回枚举；反函数在这边失手
    // （表没改全）就当没收到 —— 宁可那一条底下空着，也不要填错。
    VersionManager::LoaderKind kind;
    if (!VersionManager::loaderKindFromIndex(loaderIndex, &kind)) {
        return;
    }
    QTreeWidgetItem *item = loaderItem(loaderIndex);
    if (item == nullptr) {
        return;
    }
    if (!success) {
        // 拉不到：那一条底下**只留"自动（推荐）"**，外加一条灰的「列表没取到」把话说到
        // —— 不钉版本这条路是不需要这份列表的（安装器自己会挑），所以"列不出来"不该把
        // 安装整件事堵死，但也不能一声不吭。
        // 具体原因（超时？404？）写进日志 —— 那是给排查用的，不是给用户读的。
        fillLoaderVersionItems(loaderIndex, QStringList());
        auto *failed = new QTreeWidgetItem(item);
        failed->setText(MainWindowUI::loaderTreeKeyColumn(), MainWindowUI::loaderVersionFailedText());
        failed->setDisabled(true);
        appendLog(tr("%1 在 %2 上有哪些版本没取到：%3")
                      .arg(VersionManager::loaderDisplayName(kind), gameVersion, errorMessage));
        return;
    }
    m_loaderVersionCache.insert(QStringLiteral("%1|%2").arg(int(kind)).arg(gameVersion), versions);
    fillLoaderVersionItems(loaderIndex, versions);
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
    if (invalid.isEmpty() && name.isEmpty()) {
        // validateDirName("") 回的是"名字不能是空的"那句，所以上面那个判据其实已经包住
        // 这一支了 —— 这里留着是为了"空名字"这件事在本函数里有个看得见的落点。
        return;
    }

    QString hint;
    bool canInstall = invalid.isEmpty();
    if (!canInstall) {
        hint = invalid;
    } else {
        // 重名：**只提示、不拦**（用户原话："如果文件夹里有重名则在下方小字提示"）。
        // 同名就是同一个实例 —— 装下去不是新建一个，而是校验并补齐那个目录里的文件
        // （「重下 = 校验跳过」）。这句话必须说清这一点，否则用户会以为会多出一个。
        const QString dir = AppPaths::versionDir(name);
        if (QDir(dir).exists()) {
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
    m_ui->getInstallNameHint()->setText(hint);
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
        QMessageBox::warning(this, tr("没有选中版本"),
                             tr("不知道要基于哪个原版安装，请回到版本列表重新挑一个。"));
        return;
    }

    const QString name = m_ui->getInstallNameEdit()->text().trimmed();
    const QString invalid = InstanceFiles::validateDirName(name);
    if (!invalid.isEmpty()) {
        QMessageBox::warning(this, tr("名字不能用"), invalid);
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
        const auto answer = QMessageBox::question(
            this, sameAsVanilla ? tr("版本已存在") : tr("实例已存在"),
            sameAsVanilla
                ? tr("版本 %1 已经下载过了。\n是否校验并修复缺失或损坏的文件？").arg(name)
                : tr("实例「%1」已经装过了。\n是否校验并修复缺失或损坏的文件？").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes) {
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
    setDownloadUiBusy(true);
    m_progressThrottle.invalidate();
    m_installingInstanceId = name;
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
        QMessageBox::warning(this, tr("Java 版本不够"), javaReason);
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

    // ---------- 下载源 ----------
    QComboBox *sourceCombo = m_ui->getDownloadSourceCombo();
    sourceCombo->addItem(tr("官方源（Mojang）"), int(DownloadSource::Official));
    sourceCombo->addItem(tr("镜像源（BMCLAPI）"), int(DownloadSource::Mirror));
    selectByEnumData(sourceCombo, m_settings->downloadSource());

    // ---------- 版本名格式 ----------
    pushVersionNameOptionsToUi(m_settings->versionNameOptions());

    // 下载源得立刻交给发请求的那一层：构造函数里紧接着的
    // fetchAvailableVersions() 就会走它。
    syncDownloadSource();
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
    if (autoMemory) {
        // 自动算出来的值也落盘：盘上那份就是"上次按系统内存算出来的值"，用户从自动切到
        // 手动时正好拿它当起点，也让"界面显示的就是盘上存的"这条一直成立。
        m_javaSettings->save();
    }

    const QSignalBlocker blockCheck(m_ui->getMemoryAutoCheck());
    const QSignalBlocker blockMax(m_ui->getMemoryMaxSpin());
    const QSignalBlocker blockMin(m_ui->getMemoryMinSpin());
    m_ui->getMemoryAutoCheck()->setChecked(autoMemory);
    m_ui->getMemoryMaxSpin()->setValue(m_javaSettings->xmx());
    m_ui->getMemoryMinSpin()->setValue(m_javaSettings->xms());
    // 自动档下把两个框禁掉：里面是刚算出来的建议值，让人以为能改就是"改了不生效"。
    m_ui->getMemoryMaxSpin()->setEnabled(!autoMemory);
    m_ui->getMemoryMinSpin()->setEnabled(!autoMemory);

    // 说明文字：把"这台机器有多少内存、现在还剩多少、最后给了多少"三件事说全 ——
    // 自动档下用户看不到（也改不了）那两个数，只能靠这句话知道为什么是这个值。
    const QString totalText = humanMemorySize(usage.totalMb);
    const QString availableText = humanMemorySize(usage.availableMb);
    if (usage.totalMb <= 0) {
        // 读不到系统内存（非 Windows 平台或系统调用失败）：照实说，别让用户以为是"按你的
        // 内存算的"。实际用的是内置默认值。
        // NOTE: 这一支**不看 autoMemory**，是照着自动档写的：手动档下会对着用户自己刚
        //       定的那两个数说"自动分配用的是内置默认值"。手动档下那句话两头都不对，
        //       只是数值本身在上面两个框里能直接看到，就先这么写着。
        m_ui->getMemoryAutoHint()->setText(
            tr("读不到系统内存，自动分配用的是内置默认值（最大 %1 MB / 最小 %2 MB）。")
                .arg(m_javaSettings->xmx())
                .arg(m_javaSettings->xms()));
        return;
    }
    if (autoMemory) {
        m_ui->getMemoryAutoHint()->setText(
            tr("系统总内存 %1、当前可用 %2，自动给到 最大 %3 MB / 最小 %4 MB。"
               "每次启动前都会按当时的可用内存重算。")
                .arg(totalText, availableText)
                .arg(m_javaSettings->xmx())
                .arg(m_javaSettings->xms()));
        return;
    }
    m_ui->getMemoryAutoHint()->setText(
        tr("系统总内存 %1、当前可用 %2；现在是手动指定（最大 %3 MB / 最小 %4 MB）。")
            .arg(totalText, availableText)
            .arg(m_javaSettings->xmx())
            .arg(m_javaSettings->xms()));
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
        // 自动档下这两个框是禁用的，用户改不到这里。真走到这一句（程序内部 setValue）
        // 也不能把自动算出来的值当成"用户手动调的值"存下去 —— 那样下次进设置页就会看到
        // 手动档亮着，而用户从没动过它。
        return;
    }
    int maxMb = m_ui->getMemoryMaxSpin()->value();
    const int minMb = m_ui->getMemoryMinSpin()->value();

    if (minMb > maxMb) {
        // 最大 < 最小是 JVM 直接拒绝启动的配置。越界时**总是**把最小内存拉下来，
        // 不去顶用户刚填的那个数：两个 spin 共用一个槽，这里区分不了是谁刚被改的
        // （猜错的代价是把用户正在输入的那个数改掉，比"最小内存被拉平"难受得多）。
        // 同时挡信号：下面还要手动把两个值一起写回设置，不需要再来一轮。
        const QSignalBlocker blocker(m_ui->getMemoryMinSpin());
        m_ui->getMemoryMinSpin()->setValue(maxMb);
    }

    m_javaSettings->setXmx(maxMb);
    m_javaSettings->setXms(m_ui->getMemoryMinSpin()->value());
    m_javaSettings->save();
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

void MainWindow::syncDownloadSource() {
    // 改完立刻生效，不用重启启动器 —— 设置是**发请求那一刻**读的（见 DownloadManager）。
    // 正在下着的那一批不会被中途改道，这是对的：半批官方半批镜像更难查。机制上是"一个
    // 批次的 URL 在入队那一刻就定死了"（downloadFile 收到的是一整个字符串），所以代价是
    // 这次下载开始之后再改设置，要等下一次下载才见效。
    m_versionManager->setUseMirror(m_settings->downloadSource() == DownloadSource::Mirror);
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

void MainWindow::onStartButtonClicked() {
    // 防御：下载期间按钮本来就是灰的，但这里再拦一道。改动前没有任何阻拦，
    // 用户看着日志以为下完了直接点启动，游戏会因为文件不全崩掉。
    //
    // 四个判据缺一不可，它们兜的是**同一种缝**：活儿还在进行，但此刻没有任何一批下载
    // 在跑（isDownloading() 是 false），于是按钮亮着、点下去就启动一个文件不全的实例。
    //   - isLoaderInstalling()：加载器安装的头两步（拉列表、拉 profile）。
    //   - isModpackImporting() / isNamedInstanceInstalling()：两者都有"等原版版本 json"
    //     那一段（那是元数据请求，不算下载）；整合包还多一段"往实例目录里解压覆盖文件"。
    // NOTE: 后两个判据以前**一个都没写进来** —— VersionManager 那边早就备好了这两个函数
    //       （连注释都写着"一起进 UI 的忙态判据"），而全仓没有任何调用点：判据写着、
    //       却没人问。所以"导入整合包的这几秒里点启动"一直是能点下去的。
    if (m_downloadInFlight || m_versionManager->isDownloading() ||
        m_versionManager->isLoaderInstalling() || m_versionManager->isModpackImporting() ||
        m_versionManager->isNamedInstanceInstalling()) {
        QMessageBox::warning(this, tr("下载进行中"),
                             tr("该版本的文件还没有下载完成（或者还在安装加载器 / 导入整合包），"
                                "请等它结束后再启动。"));
        return;
    }

    bool offlineMode = m_ui->getOfflineModeCheckBox()->isChecked();

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
            QMessageBox::warning(this, tr("登录缺失"),
                                 tokenError.isEmpty()
                                     ? tr("请先登录 Microsoft 账户，或使用离线模式。")
                                     : tr("%1\n\n请重新登录 Microsoft 账户，或改用离线模式。")
                                           .arg(tokenError));
            return;
        }
    }

    // 只认"已安装版本"下拉框：启动只可能发生在本地已经装好的版本上
    const QString versionId = selectedVersionId(m_ui->getInstalledVersionCombo());
    if (versionId.isEmpty()) {
        QMessageBox::warning(this, tr("未选择版本"),
                             tr("请先在\"已安装版本\"下拉框中选择一个版本。"));
        return;
    }

    // version.json 要提前取：natives 的准备和启动前检查都依赖它
    QJsonObject versionJson = m_versionManager->getVersionJson(versionId);

    // 拿不到 version.json 就不能猜。以前这里会静默用 javaMajorVersion = 21 和
    // assetIndex = "29" 继续启动，26.1（需要 Java 25）就会崩在
    // UnsupportedClassVersionError，且看不出原因。
    if (versionJson.isEmpty()) {
        appendLog(tr("找不到版本 %1 的 version.json").arg(versionId));
        QMessageBox::warning(
            this, tr("版本信息缺失"),
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
        QMessageBox::warning(this, tr("版本不完整"), inheritanceProblem);
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
        QMessageBox::warning(this, tr("无法启动"),
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
        QMessageBox::warning(this, tr("无法启动"),
                             tr("启动前检查没通过，游戏起不来：\n\n%1")
                                 .arg(errors.join(QStringLiteral("\n"))));
        return;
    }

    // 兜底值只在 version.json 没写这两个字段时才用得上
    QString assetIndexId = "29";
    int javaMajorVersion = 21;
    QJsonObject assetIndexObj = versionJson["assetIndex"].toObject();
    const QString actualAssetIndexId = assetIndexObj["id"].toString();
    if (!actualAssetIndexId.isEmpty()) {
        assetIndexId = actualAssetIndexId;
    }
    // version.json 里的 javaVersion.component 指明了 Mojang 为该版本准备的运行时，
    // 例如 26.1 是 java-runtime-epsilon（Java 25）。
    const QJsonObject javaVersionObj = versionJson["javaVersion"].toObject();
    const int majorVersion = javaVersionObj["majorVersion"].toInt();
    if (majorVersion > 0) {
        javaMajorVersion = majorVersion;
    }
    const QString javaComponent = javaVersionObj["component"].toString();

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
        QMessageBox::warning(
            this, tr("Java 运行时缺失"),
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
            QMessageBox::warning(
                this, tr("实例设置写入失败"),
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

    // 按版本的自定义参数来自 <id>/wtfml.meta 的 "jvmArgs"（PCL 那半边"自定义启动
    // 参数"的对应物）；全局那半边是 EffectiveJavaSettings::extraJvmArgs，在
    // generateJvmArguments() 里比它先入列，所以单个实例能盖掉全局默认值。
    // 玩家名和底栏左下角显示的是**同一个**（AuthManager::playerName()），离线与正版
    // 两条分支都用它 —— 以前离线分支自己拼 "Player<时间戳>"、正版分支写死 "Player"，
    // 于是底栏上的名字和游戏里看到的名字可以不一样。
    // UUID 也一样：正版分支用它当 --uuid（没登录时 AuthManager 给空串，那边退回全零）。
    const QStringList jvmArgs = m_launcherManager->generateJvmArguments(
        token, versionId, assetIndexId, versionJson, javaSettings, offlineMode,
        javaMajorVersion, m_versionManager->versionJvmArgs(versionId),
        m_authManager->playerName(), m_authManager->playerUuid());
    // 记住"上次启动的版本"的时机是**进程真的起来了**，不是"表达式算完了"。
    // startGame() 有三条早退路径（进程已在运行 / 找不到 Java / waitForStarted 超时），
    // 以前它们都会先骗一句"已开始启动游戏"再显示错误。以"起来了"为准也是对的：
    // 因为库缺失之类的原因崩掉，恰恰是最需要下次打开还选着这个版本的时候。
    //
    // startGame() 返回之前那一段（准备 natives、探 Java、等进程起来）是**同步阻塞**的，
    // 界面根本没机会重画 —— 所以那一段报不出百分比，只能报"在等什么"。
    // NOTE: 这里那句和下面 updateBottomBar() 里那句是**同一句话**（都出自
    //       MainWindowUI::launchingStatusText()），所以进程起来的那一刻文字不会跳。
    enterSyncWait(MainWindowUI::launchingStatusText(instanceDisplayName(versionId)));
    const bool started = m_launcherManager->startGame(jvmArgs, javaSettings, versionId);
    leaveSyncWait();
    if (started) {
        m_settings->setLastLaunchedVersion(versionId);
        appendLog(tr("已开始启动游戏，正在执行 JVM 参数..."));
        // 启动进度从这里开始走到"游戏自己吐出第一行输出"为止。startGame() 返回之前
        // 那段只能报"在等什么"（见上面那次 enterSyncWait()）—— 这里能覆盖的是随后的
        // JVM/游戏初始化。
        // 顺序上安全：startGame() 内部那条 "游戏已启动" 的 processOutput 是在它
        // return 之前同步发出的，会先于这次赋值到达，不会被当成"游戏已经出话了"。
        m_launchInFlight = true;
        m_launchingVersionId = versionId;
        // 这一档只对"真的起来了"生效 —— startGame() 那三条早退路径（进程已在跑 /
        // 找不到 Java / 超时）下面根本走不到这里。
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
    // 非模态显示：登录那一步会把浏览器推到前台，用户要在这两个窗之间来回切。做成模态的话
    // 这边会被拦住（而浏览器那个页面本来就在外面，模态拦不住它，只会让这边看着像卡住）。
    //
    // NOTE: 顺序是**先开窗、再刷新**，不能倒过来。refreshAccountSkin() 最后那一段会去拉
    //       披风列表（真联网，最长能等十几秒），跑完才 show 的话，头一次点这个入口就是
    //       "什么也没发生" —— 窗还没画出来，那个请求已经在跑了，用户只会以为按钮点坏了。
    //       先 show 出来，那一趟就变成"窗开着、状态行写着在忙"。
    m_accountSkinDialog->show();
    m_accountSkinDialog->raise();
    m_accountSkinDialog->activateWindow();
    refreshAccountSkin();
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

QString MainWindow::accountSkinStatusText() const {
    // 当前账号**本身**是离线账号时先说这一句：它比"勾没勾离线模式"更具体（名字就在上面
    // 那一列里躺着）。下面那一档说的是另一件事 —— "有正版账号，但这次启动走离线"。
    // NOTE: 这一句必须排在勾选框那一档**前面**：切到离线账号时勾选框也是勾着的，反过来的
    //       话用户看到的是泛泛的离线提醒，而看不到自己刚加的那个名字。
    const QString active = AccountStore::activeUuid();
    if (AccountStore::isOffline(active)) {
        return AccountSkinDialog::offlineIdentityText(AccountStore::nameOf(active));
    }
    // 离线模式**不是一个账号**：它不改那一列（列表装的是"我保存过哪些账号"），它改的是
    // "这次启动用哪一种身份"。所以勾着的时候这句话说的是那件事，见下面那两句。
    if (m_ui->getOfflineModeCheckBox()->isChecked()) {
        return AccountSkinDialog::offlineModeNote();
    }
    if (!m_authManager->loggedIn()) {
        return AccountSkinDialog::notLoggedInNote();
    }
    return AccountSkinDialog::identityText(m_authManager->playerName());
}

void MainWindow::refreshAccountSkin() {
    if (m_accountSkinDialog == nullptr) {
        return;  // 窗还没建（没点过那个入口），没什么可刷的
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
    //    会把同一份贴图同时喂给主页那个小人和这个窗里的预览。离线模式勾着时那条路自己
    //    就不联网，所以这里不用再判一次。
    startPlayerSkinLoad();

    // 4) 披风列表要令牌，是这一整块里唯一会真的去联网的地方（过期令牌也是在这里顺带刷新的）。
    if (m_ui->getOfflineModeCheckBox()->isChecked()) {
        // 离线模式勾着 = "别联网"，而拉披风列表正是联网。跳过。
        m_accountSkinDialog->setCapes({});
        return;
    }
    QString error;
    const QString token = m_authManager->loadSavedToken(&error);
    if (token.isEmpty()) {
        // 没登录（或者令牌换不出来）。原因写进状态行 —— 它比"当前身份"那句更要紧，
        // 而"当前是哪个账号"在列表里那个（当前）标记上照样看得到。
        m_accountSkinDialog->setCapes({});
        if (!error.isEmpty()) {
            m_accountSkinDialog->setStatusText(error);
        }
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
        QMessageBox::warning(this, tr("添加离线账号失败"), error);
        return;
    }
    const QString trimmed = name.trimmed();
    // 离线账号 = "用这个名字离线进游戏"，所以「离线模式」必须跟着勾上（不勾的话启动那条路
    // 会去要凭据，而盘上一份都没有）。addOffline() 已经把它设成当前账号了。
    if (!m_ui->getOfflineModeCheckBox()->isChecked()) {
        m_ui->getOfflineModeCheckBox()->setChecked(true);  // 这一下会顺带刷新底栏
    }
    // 名字进 AuthManager：底栏左下那行和启动参数里的 --username 取的都是它。
    m_authManager->setOfflinePlayer(trimmed);
    if (m_accountSkinDialog != nullptr) {
        // 添加成功之后才清输入框：失败时留着让用户改一下再点（重打一遍是白费）。
        m_accountSkinDialog->clearOfflineNameDraft();
    }
    updateBottomBar();
    appendLog(tr("已添加离线账号「%1」并切换到它。").arg(trimmed));
    // 列表、状态行、预览、披风这一整套刷新 —— 和切换账号之后做的事情一样（离线账号没有
    // 披风可拉，refreshAccountSkin() 在"离线模式勾着"那一档就返回了，不会联网）。
    refreshAccountSkin();
}

void MainWindow::onAccountSwitchRequested(const QString &uuid) {
    if (uuid.isEmpty() || uuid == AccountStore::activeUuid()) {
        return;  // 已经是它了：写盘一遍再读回来，白折腾
    }
    // 先问清是哪一种：activate() 之后盘上的状态已经变了，那时候再问容易问错。
    const bool offline = AccountStore::isOffline(uuid);
    QString error;
    if (!AccountStore::activate(uuid, &error)) {
        QMessageBox::warning(this, tr("切换账号失败"), error);
        return;
    }
    // 盘上换人了，内存里那一份也得跟着换 —— 不换的话底栏和启动参数里还是上一个人的名字
    // （AuthManager 只在构造时读过盘，见 reloadStoredAccount()）。
    if (offline) {
        // 离线账号没有凭据可读（活动槽刚被它清空），名字从账号列表里来。
        m_authManager->setOfflinePlayer(AccountStore::nameOf(uuid));
        // 切到离线账号必须把「离线模式」勾上：不然启动那条路会去要凭据，而盘上一份都没有，
        // 用户拿到的是"请先登录"，一个死胡同。
        // NOTE: 反过来（切到在线账号）**不动**这个勾选框 —— 它可能是用户自己勾的，而
        //       "有正版账号但这次离线启动"本来就是它的原意。切个号就把别人的选择抹掉才怪。
        if (!m_ui->getOfflineModeCheckBox()->isChecked()) {
            m_ui->getOfflineModeCheckBox()->setChecked(true);
        }
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
    if (QMessageBox::question(this, tr("删除账号"), question) != QMessageBox::Yes) {
        return;
    }
    if (!AccountStore::remove(uuid)) {
        QMessageBox::warning(this, tr("删除账号失败"), tr("这个账号不在已保存的列表里。"));
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
        QMessageBox::warning(this, tr("上传皮肤失败"),
                             tr("读不到「%1」这个文件。").arg(fileName));
        return;
    }
    const QByteArray png = file.readAll();
    file.close();
    const QImage image(path);  // 用 QImage 再读一遍只为拿宽高（下面换的是原始字节）
    if (image.isNull()) {
        QMessageBox::warning(this, tr("上传皮肤失败"),
                             tr("「%1」不是一张能读出来的图片。").arg(fileName));
        return;
    }
    if (!SkinApi::isAcceptableSkinSize(image.width(), image.height())) {
        QMessageBox::warning(this, tr("上传皮肤失败"),
                             tr("皮肤图必须是 64×64（新格式）或者 64×32（老格式），这张是 "
                                "%1×%2。")
                                 .arg(image.width())
                                 .arg(image.height()));
        return;
    }

    QString error;
    const QString token = m_authManager->loadSavedToken(&error);
    if (token.isEmpty()) {
        QMessageBox::warning(this, tr("上传皮肤失败"), error);
        return;
    }
    if (!m_skinApi->uploadSkin(token, png, variant, &error)) {
        QMessageBox::warning(this, tr("上传皮肤失败"), error);
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
        QMessageBox::warning(this, tr("更换披风失败"), error);
        return;
    }
    if (!m_skinApi->setActiveCape(token, capeId, &error)) {
        QMessageBox::warning(this, tr("更换披风失败"), error);
        return;
    }
    appendLog(tr("已更换披风。"));
    refreshAccountSkin();
}

void MainWindow::onCapeHideRequested() {
    QString error;
    const QString token = m_authManager->loadSavedToken(&error);
    if (token.isEmpty()) {
        QMessageBox::warning(this, tr("取消披风失败"), error);
        return;
    }
    if (!m_skinApi->hideCape(token, &error)) {
        QMessageBox::warning(this, tr("取消披风失败"), error);
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
    QMessageBox::information(this, tr("登录成功"), tr("已登录为「%1」。").arg(playerName));
}

void MainWindow::onLoginFailed(const QString &errorMessage) {
    if (m_accountSkinDialog != nullptr) {
        // 那个窗上也可能有一个人在等（它是从「登录新账号…」起的流程）。忙态必须在这里
        // 松开，否则弹窗关掉之后它那几个按钮一直灰着，看着像卡死了。
        m_accountSkinDialog->setBusy(false);
        m_accountSkinDialog->setStatusText(accountSkinStatusText());
    }
    // 登录失败是**挡住启动**的错误（没有令牌就点不动「启动游戏」），按产品约定必须弹窗。
    // 正文是 AuthManager 那边给的、能直接给用户看的话（"这个微软账户还没注册过 Xbox 账户，
    // 到 xbox.com 登录一次"这种），不是一句"登录失败"。
    QMessageBox::warning(this, tr("登录失败"), errorMessage);
}

void MainWindow::onAddFolderClicked() {
    // 用目录选择对话框而不是让用户手打路径：路径里有中文、空格、全角括号（真机上的
    // 实例目录就是这样），打错一个字符得到的是一个"加进去了但一个实例都没有"的空文件夹，
    // 而界面上看不出区别。
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("选择 Minecraft 游戏文件夹"), QDir::homePath());
    if (folder.isEmpty()) {
        return;  // 用户取消了，不是错误
    }
    if (!m_folderManager->addFolder(folder)) {
        QMessageBox::warning(this, tr("添加文件夹失败"),
                             tr("「%1」不是一个存在的目录。").arg(folder));
        return;
    }
    appendLog(tr("已添加文件夹：%1").arg(folder));
    // 加完直接切过去：点「添加文件夹」的意图就是要用那一份。已经在列表里时会切到它，
    // 已经是当前的则什么都不做（FolderManager 自己判）。
    m_folderManager->setCurrentFolder(folder);
}

void MainWindow::onRemoveFolderClicked() {
    const QListWidgetItem *item = m_ui->getFolderList()->currentItem();
    if (item == nullptr) {
        QMessageBox::information(this, tr("移除文件夹"), tr("请先在列表里选中一个文件夹。"));
        return;
    }
    const QString folder = item->data(Qt::UserRole).toString();
    if (m_folderManager->isDefaultFolder(folder)) {
        // 只有"自带的那一份"会走到这儿（正常路径下按钮本来就是灰的），照实说明原因，
        // 不是"移除失败"这种没法处理的消息。
        QMessageBox::information(
            this, tr("移除文件夹"),
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
    const auto answer = QMessageBox::question(this, tr("从列表里移除文件夹"), question,
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        return;  // 取消不是错误，不写日志（这里也没有"取消了一次删除"可言）
    }
    if (!m_folderManager->removeFolder(folder)) {
        // 正常路径下走不到这儿（上面那道 isDefaultFolder 已经挡住了唯一的失败原因）。
        // 留着是防"列表变了但一个提示都没有"。
        QMessageBox::warning(
            this, tr("移除文件夹"),
            tr("没能把「%1」从列表里移除。").arg(QDir::toNativeSeparators(folder)));
        return;
    }
    appendLog(tr("已从列表里移除文件夹：%1").arg(folder));
}

void MainWindow::onRenameInstanceClicked() {
    // 作用于"当前选中的实例"。实例页右边那张列表和主页的下拉框是同一份数据的两种
    // 看法（双向同步，见 setupConnections），所以**以下拉框为准** —— 和"启动游戏"
    // 取的是同一个值，不会出现"列表里高亮 A、结果改的是 B"。
    const QString oldId = selectedVersionId(m_ui->getInstalledVersionCombo());
    if (oldId.isEmpty()) {
        QMessageBox::information(this, tr("重命名实例"), tr("先选中一个实例。"));
        return;
    }

    // 闸口一：正在跑的不能改。放在最前面、并且必须在调 renameInstance() **之前** ——
    // 运行中的实例在 Windows 上 jar 被 JVM 锁着，改到一半才失败的话用户看到的是一句
    // "拒绝访问"，而真正该说的是这一句。
    if (m_launcherManager->runningVersionId() == oldId) {
        QMessageBox::warning(this, tr("重命名实例"),
                             tr("「%1」正在运行，先关掉游戏再改名。").arg(instanceDisplayName(oldId)));
        return;
    }

    bool ok = false;
    const QString input =
        QInputDialog::getText(this, tr("重命名实例"),
                             tr("新名字会成为 versions 下面那个目录的名字，\n"
                                "也就是这个实例的版本号。里面的 jar 和 json 会一起改。"),
                             QLineEdit::Normal, oldId, &ok);
    if (!ok) {
        return;
    }
    const QString newName = input.trimmed();
    if (newName == oldId) {
        // 名字没动就悄悄退出，不弹窗：用户点开、看看、点确定，这不是错误。
        return;
    }
    // 校验和"真的去改"都在 applyRename() 里 —— 那是纯逻辑那一半，验证工程直接驱动它。
    applyRename(oldId, newName);
}

bool MainWindow::applyRename(const QString &oldId, const QString &newId) {
    const QString name = newId.trimmed();

    // 名字合不合法。判据和数据层是**同一份**（InstanceFiles::validateRenameTarget），
    // 界面绝不自己抄一遍规则 —— 抄一遍的下场是两边慢慢漂开，出现"界面放行、数据层拒绝"
    // 或者更糟的"界面拒绝、数据层其实能接受"。
    const QString problem = InstanceFiles::validateRenameTarget(name, oldId);
    if (!problem.isEmpty()) {
        // 不循环重问：用户看到原因后可以自己再点一次，那样他能顺便改别的决定。
        QMessageBox::warning(this, tr("重命名实例"), problem);
        return false;
    }

    // 分组归属的**老键必须在改名之前取**：改名会把老目录名连同"按目录名认人"那条
    // 退路一起改掉，改完再问它，问的是磁盘上一个已经不存在的目录。
    const QString oldKey = instanceKey(oldId);

    // 执行：改磁盘、含失败回滚，都在数据层。失败原因是给用户看的一整句
    // （可能带"回滚到什么程度"），界面原样弹出来，不再自己加工。
    QString error;
    if (!m_versionManager->renameInstance(oldId, name, &error)) {
        QMessageBox::critical(this, tr("重命名失败"), error);
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
        QMessageBox::warning(this, tr("分组归属没跟着搬"),
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

QString MainWindow::instanceKey(const QString &versionId) const {
    // 有稳定 id 就用它，没有就退回目录名 —— 见 InstanceGroupStore 文件头那段。
    // NOTE: 退回目录名不是"临时凑合"，它是老实例（2026-09 之前建的，一个都没有稳定 id）
    //       唯一的现实身份。更要紧的是**读和写必须走同一个函数**：读的时候按目录名找、
    //       写的时候按 id 存，归属会在存下去的那一刻就对不上，而那个错不报任何东西。
    const QString id = m_versionManager->instanceId(versionId);
    return id.isEmpty() ? versionId : id;
}

void MainWindow::onGroupInstanceClicked() {
    // 和「重命名…」同一个取值口径：以下拉框为准（它就是"当前选中的那个实例"）。
    const QString versionId = selectedVersionId(m_ui->getInstalledVersionCombo());
    if (versionId.isEmpty()) {
        QMessageBox::information(this, tr("分组"), tr("先选中一个实例。"));
        return;
    }

    // 每个组里有多少个实例：从**当前那张树**上数，不重新扫盘。它就是用户此刻看到的
    // 东西，而且对话框拿它显示「组名 (N)」和"删这个组会有多少实例变成未分组"——
    // 两处都该和屏幕上的数字一致。
    QHash<QString, int> counts;
    const QTreeWidget *tree = m_ui->getInstanceList();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *item = tree->topLevelItem(i);
        const QVariant groupId = item->data(0, MainWindowUI::groupIdRole());
        if (groupId.isValid()) {
            counts.insert(groupId.toString(), item->childCount());
        }
    }

    const QList<InstanceGroupStore::Group> groups = m_groupStore->groups();
    GroupDialog dialog(toDialogGroups(groups), counts, m_groupStore->groupIdOf(instanceKey(versionId)),
                       this);
    if (dialog.exec() != QDialog::Accepted) {
        // 取消：磁盘一个字节都没动（对话框只改自己手里那份工作副本）。
        return;
    }
    applyGrouping(instanceKey(versionId), dialog);
}

bool MainWindow::applyGrouping(const QString &key, const GroupDialog &dialog) {
    const QList<InstanceGroupStore::Group> wanted = toStoreGroups(dialog.groups());
    const QString wantedGroupId = dialog.assignedGroupId();

    // 两件事分开、都是"变了才写"：组清单和归属是两个独立的字段，用户可能只动了其中一个。
    // 没变就写的话，每点一次「确定」都会重写一遍文件（连带把 mtime 刷新），
    // 而"什么都没改"是最常见的一次操作。
    QString error;
    bool groupsSaved = false;
    if (wanted != m_groupStore->groups()) {
        if (!m_groupStore->replaceGroups(wanted, &error)) {
            QMessageBox::critical(this, tr("分组保存失败"), error);
            return false;
        }
        groupsSaved = true;
    }

    if (wantedGroupId != m_groupStore->groupIdOf(key)) {
        if (!m_groupStore->assign(key, wantedGroupId, &error)) {
            // 组清单那一半**可能已经落盘了**（上面那次写成功了）。说清楚，别让用户
            // 以为整件事都没成、然后重开一遍对话框再填一次。
            QMessageBox::critical(this, tr("分组保存失败"),
                                  groupsSaved
                                      ? tr("分组本身已经保存了，但这个实例的归属没能写进去：\n%1")
                                            .arg(error)
                                      : error);
            return false;
        }
        QString destination;
        for (const InstanceGroupStore::Group &group : m_groupStore->groups()) {
            if (group.id == wantedGroupId) {
                destination = group.name;
                break;
            }
        }
        if (destination.isEmpty()) {
            // 归属是空串 = 取消分组。那句"未分组"是界面上的说法（GroupDialog 也是
            // 这么写的），不是文件里的值。
            destination = tr("未分组");
        }
        appendLog(tr("实例已分组：%1 → %2").arg(key, destination));
    }
    // NOTE: 这里**不**手动重灌那张树。三条写路径成功之后数据层都会发 groupsChanged，
    //       界面接的是同一个刷新函数（见 setupConnections）—— 在这里再来一次就是
    //       同一件事做两遍，而且"哪些路径会刷新"会变成两处各自维护的约定。
    return true;
}

void MainWindow::onInstanceSettingsClicked() {
    // 取值口径和「重命名…」「分组…」一致：以下拉框为准（它就是"当前选中的那个实例"）。
    // 顺带一提，这里**不用**去读那张树的 currentItem()，也就绕开了组头那道守卫 ——
    // 三个按钮的取值口径一致比"少读一个控件"重要。
    const QString versionId = selectedVersionId(m_ui->getInstalledVersionCombo());
    if (versionId.isEmpty()) {
        QMessageBox::information(this, tr("实例设置"), tr("先选中一个实例。"));
        return;
    }

    // 已经写了的那部分覆盖从 meta 里读回来当起点，「当前全局是哪个值」由 global 提供。
    // 读归读，这里一个字节都不写 —— 用户点「取消」时磁盘必须原样。
    const JavaSettingsOverride current =
        javaOverrideFromJson(m_versionManager->versionMeta(versionId));
    InstanceSettingsDialog dialog(instanceDisplayName(versionId), toDialogOverride(current),
                                  toDialogGlobal(m_javaSettings->effective()),
                                  m_versionManager->versionJvmArgs(versionId),
                                  !m_versionManager->skipJavaVersionWarning(versionId), this);
    if (dialog.exec() != QDialog::Accepted) {
        // 取消：wtfml.meta 一个字节都没动（对话框只把控件读成一份值）。
        return;
    }
    applyInstanceSettings(versionId, dialog);
}

bool MainWindow::applyInstanceSettings(const QString &versionId,
                                      const InstanceSettingsDialog &dialog) {
    // 先把稳定 id 落下去。**必须排在最前**：这一项覆盖要"跟着这个实例走"，而实例的
    // 身份就是那个 id（目录名会被改，改名之后按目录名记的东西全丢）。失败就不写 ——
    // 写成一次"靠目录名认出来的覆盖"比这次没写成糟得多。
    if (m_versionManager->ensureInstanceId(versionId).isEmpty()) {
        QMessageBox::critical(this, tr("实例设置没能保存"),
                              tr("「%1」的 wtfml.meta 写不进去，这次的设置没有保存。\n\n"
                                 "（这份文件可能被别的程序占着，或者它所在的目录不可写。）")
                                  .arg(instanceDisplayName(versionId)));
        return false;
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

    QString error;
    if (!m_versionManager->updateVersionMeta(versionId, fields, &error)) {
        // 数据层不弹窗是约定，所以弹在这一层。error 里已经写清了"是哪个文件、为什么"。
        QMessageBox::critical(this, tr("实例设置没能保存"), error);
        return false;
    }
    appendLog(tr("实例设置已保存：%1").arg(versionId));
    return true;
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

    JavaMismatchDialog dialog(instanceDisplayName(versionId), expectedMajor, actualMajor,
                              actualVersionText, *override.javaPath, this);
    dialog.exec();
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

void MainWindow::refreshFolderList() {
    QListWidget *list = m_ui->getFolderList();
    const QString current = m_folderManager->currentFolder();
    {
        // 重灌不是"用户改选"，别把切换逻辑再跑一遍。下面要按当前目录摆选中项。
        const QSignalBlocker blocker(list);
        list->clear();
        for (const QString &path : m_folderManager->folders()) {
            const bool isDefault = FolderManager::isDefaultFolder(path);
            // 路径用系统分隔符显示（`D:\MC\.minecraft`），别把 QDir 的 `/` 露给用户。
            const QString shown = QDir::toNativeSeparators(path);
            auto *item = new QListWidgetItem(isDefault ? tr("%1（自带）").arg(shown) : shown, list);
            item->setData(Qt::UserRole, path);
            // 列表窄，长路径会被省略号截断，所以完整路径挂 tooltip 上。
            item->setToolTip(isDefault ? tr("%1\n启动器自带的游戏文件夹，删不掉。").arg(shown)
                                       : shown);
            if (path == current) {
                list->setCurrentItem(item);
            }
        }
    }
    updateFolderButtons();
}

void MainWindow::updateFolderButtons() {
    const QListWidgetItem *item = m_ui->getFolderList()->currentItem();
    const bool removable = item != nullptr &&
                           !FolderManager::isDefaultFolder(item->data(Qt::UserRole).toString());
    // 下载期间整列都不给动（见 setDownloadUiBusy）：换文件夹会把后面那些文件写到
    // 另一份 .minecraft 里去，半批在里面半批在外面，比"这次下不了"难查得多。
    m_ui->getRemoveFolderButton()->setEnabled(removable && !m_downloadInFlight);
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

void MainWindow::onProcessOutput(const QString &text) {
    appendLog(tr("[游戏输出] %1").arg(text));
    // 游戏自己出话了 = JVM 和游戏都初始化到能打日志了，"正在启动"这段到此为止。
    // 底栏中间那格让给下载进度（如果有），左下角换成"实例运行中"。
    if (m_launchInFlight) {
        m_launchInFlight = false;
        updateBottomBar();
    }
}

void MainWindow::onProcessError(const QString &text) {
    appendLog(tr("[游戏错误] %1").arg(text));
    // stderr 出东西也算"起来了"：有些版本把日志全写 stderr，等 stdout 会一直等下去。
    if (m_launchInFlight) {
        m_launchInFlight = false;
        updateBottomBar();
    }
}

void MainWindow::onGameFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (exitStatus == QProcess::CrashExit) {
        appendLog(tr("游戏崩溃，退出码：%1").arg(exitCode));
    } else {
        appendLog(tr("游戏进程已结束，退出码：%1").arg(exitCode));
    }
    // 进程没了，"启动中"和左下角那条"实例运行中"都得收起来（runningVersionId() 在
    // LauncherManager 里已经清了，这里只管重画）。
    m_launchInFlight = false;
    updateBottomBar();

    // 「收起启动器，游戏退出后自动回来」那一档：见 applyLauncherBehaviourAfterLaunch()。
    // 只有**为了启动游戏才藏起来**的窗口才会被叫回来 —— 用户自己最小化、自己收起来的
    // 窗口不该因为游戏退出就跳出来。
    if (m_hiddenForGame) {
        m_hiddenForGame = false;
        showNormal();
        raise();
        activateWindow();
    }
}

void MainWindow::onImportModpackClicked() {
    if (m_downloadInFlight) {
        appendLog(tr("已有下载任务正在进行，请等它结束。"));
        return;
    }

    const QString zipPath = QFileDialog::getOpenFileName(
        this, tr("选一个整合包"), QString(),
        tr("整合包 (*.zip *.mrpack);;所有文件 (*)"));
    if (zipPath.isEmpty()) {
        return;  // 用户取消：什么都不做，也**不弹**任何提示
    }

    // 先只看一件事：这个压缩包的根目录里有没有 modrinth.index.json。**不整包解开** ——
    // 用户随手选一个几百 MB 的包，为了"知道它不是整合包"先解压几分钟是白等。
    QByteArray indexJson;
    QString error;
    const ZipUtil::EntryStatus status =
        ZipUtil::readEntry(zipPath, ModpackImport::modrinthIndexName(), &indexJson, &error);
    if (status != ZipUtil::EntryStatus::Ok) {
        // "里面没有这份清单"和"这个文件根本打不开"是**两句完全不同的话**：前者多半是
        // 选错了文件（或者那是 CurseForge 格式的包），后者是文件坏了/不是 zip。所以这里
        // 还要再读一个名字，把 CurseForge 那一类也分出来。
        ModpackImport::Format format = ModpackImport::Format::Unknown;
        if (status == ZipUtil::EntryStatus::NotFound) {
            QByteArray manifest;
            if (ZipUtil::readEntry(zipPath, ModpackImport::curseForgeManifestName(), &manifest,
                                   nullptr) == ZipUtil::EntryStatus::Ok) {
                format = ModpackImport::Format::CurseForge;
            }
        }
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
        QMessageBox::warning(this, tr("这不是整合包"), text);
        return;
    }

    // 解析（纯函数）。清单读不懂、不是 Minecraft 的包、路径不安全、一条文件都没有，
    // 四种情况的原因都不一样，都由它一句话说清 —— 界面不自己编第二句。
    const ModpackImport::Result result = ModpackImport::parseModrinthIndex(indexJson);
    if (!result.ok) {
        appendLog(tr("整合包清单读不懂：%1").arg(result.problem));
        QMessageBox::warning(this, tr("导入整合包"), result.problem);
        return;
    }
    const ModpackImport::Pack &pack = result.pack;

    // 加载器要不上，在**问名字之前**就拦掉：装不了的包，问完名字再说装不了是白耽误他。
    const QString loaderProblem =
        ModpackImport::unsupportedLoaderText(pack.loaderKey, pack.loaderVersion);
    if (!loaderProblem.isEmpty()) {
        appendLog(tr("没有导入 %1：要 %2，这个启动器装不了。")
                      .arg(QFileInfo(zipPath).fileName(), pack.loaderKey));
        QMessageBox::information(this, tr("这个整合包装不了"), loaderProblem);
        return;
    }

    // 名字：先拿包名推一个候选（ModpackImport::suggestInstanceName 会把建不出目录的
    // 字符换掉、收长度），用户改不改都行。校验用**数据层那一份**
    // （InstanceFiles::validateRenameTarget），界面绝不自己抄一遍规则。
    QString name = ModpackImport::suggestInstanceName(pack.name);
    while (true) {
        bool ok = false;
        const QString input = QInputDialog::getText(
            this, tr("导入整合包"),
            tr("实例叫什么名字？这就是 versions 下面那个目录名，\n"
               "整合包「%1」的东西会装进它里面。").arg(pack.name),
            QLineEdit::Normal, name, &ok);
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
            const auto resumeAnswer = QMessageBox::question(
                this, tr("接着上次的导入"),
                tr("versions 下面已经有一个「%1」了，是上一次**没下完**的导入。\n\n"
                   "接着往它里面下吗？已经下好的文件会按 sha1 跳过、不会重下，"
                   "没下成的那几个会重新下。\n\n"
                   "选「否」就换个名字重新导入 —— 那样已经下好的那些要重下一遍。")
                    .arg(name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (resumeAnswer == QMessageBox::Yes) {
                break;
            }
            continue;  // 回上面重问名字
        }
        // 循环重问（和重命名那条路不同，那边是弹一次就要用户自己再点一次）：这里退回
        // 重来的代价大得多 —— 要从"选文件"重新开始，而选的是哪个包用户未必记得住。
        QMessageBox::warning(this, tr("换个名字"), problem);
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
    const auto answer = QMessageBox::question(
        this, tr("开始导入"),
        tr("整合包：%1\nMinecraft：%2%3\n要下载的文件：%4 个%5\n装到：%6\n\n开始导入吗？")
            .arg(pack.name, pack.gameVersion, loaderText)
            .arg(pack.files.size())
            .arg(pack.filesSkipped > 0
                     ? tr("（另有 %1 个按包里的说明跳过）").arg(pack.filesSkipped)
                     : QString())
            .arg(QDir::toNativeSeparators(AppPaths::versionDir(name))),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        appendLog(tr("已取消导入 %1。").arg(pack.name));
        return;
    }

    applyModpackImport(zipPath, name, indexJson);
}

bool MainWindow::applyModpackImport(const QString &zipPath, const QString &instanceName,
                                    const QByteArray &indexJson) {
    // 把界面上确认过的**同一串字节**再解析一遍（纯函数，一瞬）。好处是"确认框里说的
    // 那个包"和"真的去装的那个包"不可能不是同一个 —— 不必去维护一份"解析结果"的传递。
    const ModpackImport::Result result = ModpackImport::parseModrinthIndex(indexJson);
    if (!result.ok) {
        QMessageBox::warning(this, tr("导入整合包"), result.problem);
        return false;
    }
    const ModpackImport::Pack pack = result.pack;

    const QString loaderProblem =
        ModpackImport::unsupportedLoaderText(pack.loaderKey, pack.loaderVersion);
    if (!loaderProblem.isEmpty()) {
        QMessageBox::information(this, tr("这个整合包装不了"), loaderProblem);
        return false;
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
        QMessageBox::warning(this, tr("换个名字"), nameProblem);
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
            QMessageBox::warning(this, tr("Java 版本不够"), javaReason);
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
    // 整批结束，无论成败都要把界面恢复回来
    setDownloadUiBusy(false);
    m_progressThrottle.invalidate();

    // 这一条讲的是整合包导入（数据层那边"一次点击 = 一个终态"，导入也走这同一个信号）。
    // 收尾那句话是**说给用户看的**（"整合包「X」导入好了…"），所以这里不再往上叠
    // 「版本 X 的文件已全部就绪」那一句 —— 对导入来说那句是错的：下的是这个整合包的
    // 模组，不是"某个版本的文件"。成功的样子是**实例列表里多一个**（顺便选中它）。
    if (!m_importingInstanceId.isEmpty() && versionId == m_importingInstanceId) {
        m_importingInstanceId.clear();
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
            QMessageBox::information(this, tr("导入完成"), errorMessage);
        } else {
            appendLog(tr("✗ 整合包没有导入：%1").arg(errorMessage));
            QMessageBox::warning(this, tr("导入失败"), errorMessage);
        }
        return;
    }

    // 这一条讲的是"装一个带名字的实例"（安装页上点了「安装」）。它走的也是这同一个
    // 信号（数据层那边"一次点击 = 一个终态"），收尾那句话说的是**用户起的那个名字**。
    // 和整合包那段同一个道理：不再往上叠「版本 X 的文件已全部就绪」那一句。
    if (!m_installingInstanceId.isEmpty() && versionId == m_installingInstanceId) {
        m_installingInstanceId.clear();
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
            QMessageBox::information(this, tr("安装完成"), errorMessage);
        } else {
            appendLog(tr("✗ %1").arg(errorMessage));
            QMessageBox::warning(this, tr("安装失败"), errorMessage);
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

        QMessageBox::information(this, tr("下载完成"),
                                 tr("版本 %1 的文件已全部就绪。\n\n%2").arg(versionId, errorMessage));
    } else {
        // 失败就不刷新了：没成功就不该有新条目冒出来（jar 大概率没落地）
        appendLog(tr("✗ 版本 %1 下载失败：%2").arg(versionId, errorMessage));
        QMessageBox::warning(this, tr("下载失败"),
                             tr("版本 %1 有文件未能下载完成。\n\n%2").arg(versionId, errorMessage));
    }
}

void MainWindow::onVersionDetailsFetched(const QString &versionId, const QJsonObject &versionJson) {
    if (versionJson.isEmpty()) {
        appendLog(tr("获取版本 %1 的信息失败。").arg(versionId));
        setDownloadUiBusy(false);
        QMessageBox::warning(this, tr("下载失败"),
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