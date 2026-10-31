#pragma once
#include <functional>
/**
 * @file main.cpp
 * @brief 一次性验证：offscreen 起真的 MainWindow，断言「启动游戏」按钮第二行 /
 * 「已安装版本」下拉框 / 占位文案 / 上次启动的版本 这几条不变式。
 *
 * 为什么必须立真的 MainWindow：这条同步有一条路径靠信号接不到 ——
 * refreshInstalledVersions() 内部用 QSignalBlocker 挡住重灌时的信号，所以
 * "刷新"必须自己收尾。只接 currentIndexChanged 的写法在"用户手选"这条路上
 * 看着是对的，一旦刷新就会露出陈旧文字。ui_check 只搭 MainWindowUI，验不到。
 *
 * 沙盒：跑之前先在工作目录（= exe 所在目录）下造一个假 .minecraft/versions，
 * 这样 applicationDirPath() 一解析就能扫到可控的版本清单，绝不碰真的存档目录。
 * QSettings 也重定向到同一个工作目录下的 check-settings/ —— 不这么做的话
 * "上次启动的版本"会写进**用户的真注册表**，而且读的也是用户真配置：
 * 启动恢复那条分支在测试里就永远是走的别人机器上的状态，等于没测。
 *
 * 用法：mw_check
 *
 * 另有五条**要联外网**的一次性探针（都不进默认那一跑，默认那一跑必须离线、不碰用户配置）：
 * mw_check.exe auth-live 真注册表 + 真网络：盘上那份令牌还能不能换出会话
 * mw_check.exe resource-live [类] [搜索词] [来源]
 * 从资源表里真装一个模组/资源包/光影到沙盒实例
 * （来源 = modrinth（默认）/ curseforge）
 * mw_check.exe icon-live [数量] 真网络量一次"一页图标多久回来、几张空手"
 * mw_check.exe icon-lookup-live <目录> 真网络 + **用户自己的盘**：一个目录里的模组
 * 各认出了什么、拿到几条真图标地址（**只读**）
 * mw_check.exe random-skin-live 真网络要一页随机皮肤、挑一条、真下一张
 * mw_check.exe modpack-import-live <包.zip> <实例名> <游戏根目录> [java]
 * 真跑一次整合包导入（几个 GB，几个钟头）
 */

#include <QApplication>
#include <QWizard>
#include "RangeChecks.hpp"
#include "SegmentInstallChecks.hpp"
#include "ToolFeatureChecks.hpp"
#include "PresetInstallChecks.hpp"
#include "ModActionPanelChecks.hpp"
#include "JavaManageChecks.hpp"
#include "InstanceUpgradeChecks.hpp"
#include <QFontDatabase>
#include "controllers/DirectDownloadController.hpp"
#include "download/StreamedFile.hpp"
#include "DownloadToolDialog.hpp"
#include <QStyleFactory>
#include <QAbstractButton>
#include <QBuffer>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>  // AH 段：令牌到期和"已运行多久"这两处都要按"现在"算
#include <QDesktopServices>  // P2 段：「打开」那一项到底调没调 openUrl（拦下来，别真开资源管理器）
#include <QKeyEvent>  // T24.7：详情页右上角摆着「模组设置」时按一下裸 Alt，量它什么都不拨
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QPainter>
#include <QImage>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QCryptographicHash>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScrollArea>  // S2：量覆盖层那一层整页滚动区还滚不滚得动（用户报的"左边也跟着滚"）
#include <QScrollBar>
#include <QSemaphore>  // T20.6：假服务器线程起来之后主线程要等它把端口听上
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QStringList>
#include <QMutex>
#include <QMutexLocker>
#include <QHostAddress>
#include <QProcess>
#include <QRandomGenerator>
// T22：那两发反查的正文**逐字比对**（Modrinth 要的是 sha1 表、CurseForge 要的是指纹表），
// 用 QSet 比"集合相等"而不是比顺序 —— 哈希表是 QHash 出来的，顺序本来就不保证。
#include <QSet>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QThread>  // AB 段：跨过底栏那个 200 ms 的进度节流窗口（见 AB1）
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QDragEnterEvent>  // T30 段：真发一发拖放事件（见 dropFilesOn）
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>  // P2 段：拦 QDesktopServices::openUrl 的那个插槽收的就是它
#include <QHash>
#include <QUrlQuery>  // T22.4b：`ids` 那一段要拆出来看（Qt 6 里 QUrl 自己没有 queryItemValue）
#include <QVector>

#include <cstdio>
#include <cstring>
// `--fake-server` 那一支要读 stdin 的每一行（`std::getline`）。
#include <iostream>
#include <string>
#include <functional>  // ZJ 节：那两发"没开起来就再排自己一轮"的检查员（递归 lambda）

// R2 节要自己调一次 DPAPI，好站在"外部写入者"的位置造字节（产品里那个 protectToken() 是私有的）。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>

#include "AccountSkinDialog.hpp"
#include "GameLogWindow.hpp"
#include "GroupDialog.hpp"  // T16：那一半顺序是在这张窗里摆的（只改工作副本）
#include "InstanceGroupDelegate.hpp"  // T10~T14：组行那颗齿轮占哪一格（几何的唯一出处）
#include "InlineMessage.hpp"  // 全仓的提示面板：这些助手要点掉的就是它
#include <QLineEdit>  // S2 那一节：问名字那一格
#include "InstanceSettingsDialog.hpp"  // AH 段：面板里那颗齿轮开的那一页（按页眉认它）
#include "InstanceFilesPage.hpp"  // T20：「文件管理」那六页的列表控件（禁用后缀 / objectName）
#include "ConfigFileEditorPage.hpp"  // T20.7：「配置文件」那一页的编辑器（真落盘那一段）
#include "controllers/InstanceMaintenanceController.hpp"
#include "ResourcePackPage.hpp"
#include "manage/ResourcePackOptions.hpp"
#include "ConfigFileText.hpp"  // T20.7：行尾 / BOM 那一套（"原样写回去"是拿它比的）
#include "OverlayHost.hpp"  // 提示开在它里面 —— activeModalWidget() 找不到它们
// ZJ 节：详情页那一份（这一节要读它摆出来的 chip / 组 / 行，以及行上那把钥匙的角色号）。
#include "ResourceDetailPage.hpp"
#include "MainWindow.hpp"
#include "JavaManageDialog.hpp"  // T26：设置页那颗「Java 管理…」开出来的整页
#include "JavaMismatchDialog.hpp"
#include "MainWindowUI.hpp"
#include "PresetInstallPicker.hpp"  // P1：安装页点「预设」那一行开的那张紧凑弹窗
#include "controllers/PresetController.hpp"  // P1：四类的次序 / 标题（弹窗里那几个页签）
#include <QTabBar>  // P1：数弹窗里是不是四类各一页
#include "TaskCenterPanel.hpp"
#include "SkinModel.hpp"
#include "SkinViewer.hpp"
#include "TitleBar.hpp"  // AI 节：设置页改名字之后条上那行字跟着变（titleText()/setBrandText()）
#include "VersionIcons.hpp"
#include "VersionTreeDelegate.hpp"
// T27.7：那一格世界设置本身（页面上摆了什么值、那几条规则各是第几行）。
#include "WorldSettingsPage.hpp"
// T28 / T32：那一趟「检查更新」的结论**开成一整页**（「更新中心」）—— 按页眉
// 认它（`PageCloser::setOnlyTitle()`）。
#include "UpdateCenterPage.hpp"
// T38：那一页上「忽略这一版 / 固定这一版」记的那本账、以及更新前那一份回滚点。
// 这一节**从盘上把它们读回来**核对（读内存里那份等于让产品自己证明自己）。
#include "manage/UpdatePolicy.hpp"
#include "manage/UpdateRollback.hpp"
#include "auth/AccountStore.hpp"
#include "controllers/ActivityCenterController.hpp"
#include "auth/AuthManager.hpp"
#include "auth/TokenStore.hpp"
#include "download/DownloadManager.hpp"
#include "download/DownloadRetryPolicy.hpp"
#include "download/InstallerFailure.hpp"
// T31：前置依赖弹窗那几行字（标题 / 正文 / 灰字 / 两颗按钮）**只在判据层有唯一出处**
// —— 这一节拿它们断言，不在测试里另写一句。
#include "download/ModDependencies.hpp"
#include "download/ResourceCatalog.hpp"
#include "download/ResourceStore.hpp"
// AH 段要算"这一行该画哪个方块"的期望值 —— 那一套分类判据和产品是**同一个出处**
// （MainWindow.cpp 里那三行也是调它），别在测试里另写一个 switch。
#include "download/VersionCategory.hpp"
#include "launcher/LauncherManager.hpp"
// T36：那一对 quick play 的判据（`supports()` 是"这个版本认不认这两条参数"的唯一出处 ——
// 卡片上那颗按钮写什么、真点下去发不发那两条，问的都是它）。
#include "launcher/QuickPlay.hpp"
// AE 那一节要拿游戏窗口那三档的**代号**去对界面上的 itemData（代号是存盘格式的一部分，
// 唯一出处在这里）—— 别在测试里另写一遍那三个串。
#include "settings/GameWindowSettings.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include "settings/LauncherSettings.hpp"
// 速度上限那几档的数字（限速）—— N 节要拿它们去数框里的项和 itemData。
#include "download/DownloadSpeedLimit.hpp"
// T35：世界与实例备份那一层的**引擎**（清单 / 打包 / 还原）。这一节从界面那几个入口
// 点下去，然后拿它把盘上那个包**读回来**核对 —— "真打出一个包了"和"包里是我要的那些"
// 是两件事，只有读回来才说得清。
#include "manage/BackupFiles.hpp"
#include "manage/DefaultSkin.hpp"
// 离线账号「换一张…」的落盘层（ZG 段最后那几条接线断言要直接读它写出来的东西）。
#include "manage/OfflineSkin.hpp"
#include "manage/FolderManager.hpp"
// T36：「上一次启动」那本账（主页那张卡的数据源）+ 服务器列表的写侧（夹具要往
// servers.dat 里加一条"朋友的服"，验"服务器那一档记的是名字不是地址"）。
#include "manage/LaunchHistory.hpp"
#include "manage/ServerListFile.hpp"
#include "net/HttpClient.hpp"
#include "manage/InstanceGroupStore.hpp"
#include "manage/InstanceFiles.hpp"
// T22：「文件管理」那几页的图标从哪来 —— 按文件哈希反查"这是哪个资源"。这一节直接调
// 它（不经过 MainWindow 那条异步线：那一条的收尾要等真的 CDN，离线机器上等不到）。
#include "manage/InstanceIconLookup.hpp"
// T21：导出整合包那条路的两半 —— 这一层要拿它算哈希（当"接口该回什么"的期望值），
// 还要拿它把打出来的包**读回来**（"我们自己装得回去"才是导出成功的判据）。
#include "manage/ModpackExport.hpp"
#include "manage/ModpackImport.hpp"
#include "manage/PlayerSkinStore.hpp"
#include "manage/RandomSkinStore.hpp"
#include "manage/VersionManager.hpp"
// T38：服务端工作区那一套（M4-1）。三份各一角：`ServerInstall` 是"这一档
// 装什么、装完长什么样"，`ServerModel` 是"一个服务端是什么 / 状态怎么叫"，`ServerRegistry`
// 是"盘上那一份谁在住"（扫目录 + 读写 `.wtfml-server`）。这一节直接调它们摆夹具 / 核对，
// 也拿来比 MainWindow 那一按之后盘上真的多了什么。
#include "download/ServerInstall.hpp"
#include "server/ServerModel.hpp"
#include "server/ServerRegistry.hpp"
// T40：安装那四段的**判据层**（`unsupportedReason` / `installerJarName` 那几个唯一出处）。
// 这一节拿它算期望值 —— Spigot 那句"暂时不支持"、还有"这一档跑不跑安装器"，判据都不在
// 测试里另写一遍。
#include "server/ServerInstallRun.hpp"
// T41：「跑起来 / 停下来」那一层。`ServerRun.hpp` 是那几条判据（**在 verify 的 ZZ 一节里
// 钉死**，这一节不重复量），`ServerProcess.hpp` 是那条真 `QProcess` —— 这一节拿本工程自己的
// exe 当"服务端"（`--fake-server`）把它的整条命跑一遍。
#include "server/ServerProcess.hpp"
#include "server/ServerRun.hpp"
// T42：「服务端设置」那一整页（M4-4）—— 这一节把它经真
// `MainWindow` 整页开出来、改一格、保存、看盘上那份文件、按「立即备份」、读体检单。
#include "ServerSettingsPage.hpp"
// T42.10：玩家名单那四份文件（`whitelist.json` / `ops.json` / `banned-players.json` /
// `banned-ips.json`）的判据层 —— 这一节加完一条之后**从盘上读回来核对**，不拿界面上的行
// 自己证明自己。
#include "server/ServerPlayers.hpp"
// T38：那一页（「新建服务端…」开出来的）要按 objectName 认控件、填字段、点「创建」。
#include "NewServerPage.hpp"
// T27.7：「世界设置…」那条路要**自己造一份真的 level.dat**（T27 前面那些夹具里那一份是
// 4096 个 'x'，读不动），存完还要用产品自己那套读法把它读回来核对 —— 一事一份：
// NbtTree 是搭树/写字节那半，WorldFiles 是"世界目录上读 / 写 level.dat"那半。
#include "manage/WorldFiles.hpp"
#include "settings/EffectiveJavaSettings.hpp"
// 任务账本那一套（T34 读它来数"多了几条 crash"）：`load()` 是那一层给这一节的唯一口子
// —— 账本落在启动器自带的固定位置（`defaultHistoryPath()`），从这里读到的就是**产品
// 真的写下去的那份文件**，比去问某个私有成员可信。
#include "tasks/TaskHistory.hpp"
#include "tasks/TaskSnapshot.hpp"
// T29：设置页「背景图」那两行，一头是**主题规格**（`ui/Theme.hpp`），另一头是那两行控件。
// `ThemeImage` 是 MainWindow 收图那一步调的（把用户挑的那张拷进数据目录）—— 这一节要在
// 沙盒里先真的存一张进去，才能量"设了图之后那两行长什么样"。
#include "Animations.hpp"  // 动效总开关：这个工程把它关掉
#include "Theme.hpp"
#include "util/AppPaths.hpp"
#include "util/ImageDecode.hpp"
#include "util/ThemeImage.hpp"
#include "util/NbtTree.hpp"
// T20.6 那台假服务器要按协议拼响应包（产品的 MainWindow::pingServers 直接调它）。
#include "util/ServerPing.hpp"
#include "util/SystemMemory.hpp"
#include "util/ZipUtil.hpp"
// 造"假 JAR"的那两个小工具（和 verify 的 ZF 段共用一份，见那个文件头）。
#include "fakejar.hpp"



inline int g_pass = 0;
inline int g_fail = 0;

/// 界面里的日志面板已经去掉了（日志改走 Qt 的日志系统，在命令行里看），所以
/// "这条消息有没有写出来、写了几遍"只能从消息处理器上收。**必须**在处理完消息后
/// 仍然转发给原来的处理器，否则测试自己的输出（check 的 PASS/FAIL 走的是 printf，
/// 但 Qt 自己的 qWarning 会看不见）就被吞了。
inline QStringList g_messages;
inline QMutex g_messageMutex;
inline QStringList messagesSnapshot() {
    QMutexLocker guard(&g_messageMutex);
    return g_messages;
}
inline QtMessageHandler g_previousHandler = nullptr;

inline void collectMessage(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    {
        QMutexLocker guard(&g_messageMutex);
        g_messages.append(message);
    }
    if (g_previousHandler != nullptr) {
        g_previousHandler(type, context, message);
    }
}

/// 一段文本在整个会话的日志里出现了几次。
inline int countOccurrences(const QStringList &messages, const QString &needle) {
    int total = 0;
    for (const QString &message : messages) {
        for (int at = message.indexOf(needle); at >= 0;
             at = message.indexOf(needle, at + needle.size())) {
            ++total;
        }
    }
    return total;
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

inline void header(const QString &title) {
    std::printf("===== %s =====\n", qPrintable(title));
    std::fflush(stdout);
}

/// 选中的**版本号**。生产代码里的同名函数取的是 Qt::UserRole，这里也必须是 ——
/// 拿 currentText() 当版本号正是这次重构要消灭的写法。
inline QString selectedId(const QComboBox *combo) {
    return combo->currentIndex() < 0 ? QString() : combo->currentData().toString();
}

/// 按钮文字的第二行。约定是"启动游戏\n<显示名>"，所以取最后一个换行之后的内容。
inline QString buttonVersionLine(const QPushButton *button) {
    const QString text = button->text();
    const int nl = text.lastIndexOf(QLatin1Char('\n'));
    return nl < 0 ? QString() : text.mid(nl + 1);
}

inline QString buttonFirstLine(const QPushButton *button) {
    const QString text = button->text();
    const int nl = text.indexOf(QLatin1Char('\n'));
    return nl < 0 ? text : text.left(nl);
}

/// 在沙盒里放一个"看起来已安装"的版本：installedVersions() 只认 <id>/<id>.jar。
inline void makeInstalledVersion(const QDir &versionsDir, const QString &id) {
    versionsDir.mkpath(id);
    QFile jar(versionsDir.filePath(id + QStringLiteral("/") + id + QStringLiteral(".jar")));
    if (jar.open(QIODevice::WriteOnly)) {
        jar.write("stub");
        jar.close();
    }
}

/// 给一个已安装版本写可选的 wtfml.meta（重命名 / 模组加载器信息的来源）。
inline void makeVersionMeta(const QDir &versionsDir, const QString &id, const QByteArray &json) {
    versionsDir.mkpath(id);
    QFile meta(versionsDir.filePath(id + QStringLiteral("/wtfml.meta")));
    if (meta.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        meta.write(json);
        meta.close();
    }
}

/// 往任意路径写一段内容（目录不存在就建出来），**写完立刻关掉句柄**。
/// NOTE: 关句柄不是讲究，是必需的：Windows 上文件被一个还开着的 QFile 攥着时，
/// 后面的删除会静默失败，于是沙盒里每跑一次多留一个目录，而断言照样全绿。
inline void writeFileAt(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(bytes);
        file.close();
    }
}

/// 拼一份**真格式**的 `servers.dat`（T20.6 用）：根 TAG_Compound → `servers` 一格
/// TAG_List<TAG_Compound> → 每条一个 `name` + 一个 `ip`。
///
/// NOTE: 只能自己拼字节 —— 产品的 NbtUtil **只读不写**（那是游戏自己的文件，启动器
/// 一个字都不许改，见那条产品约束）。拼错的话 T20.6 那几条会当场红（表格里一
/// 行都摆不出来），不会悄悄过去。
inline QByteArray buildServersDat(const QList<QPair<QString, QString>> &servers) {
    QByteArray bytes;
    const auto u16 = [&bytes](int value) {
        bytes.append(static_cast<char>((value >> 8) & 0xFF));
        bytes.append(static_cast<char>(value & 0xFF));
    };
    const auto u32 = [&bytes](int value) {
        bytes.append(static_cast<char>((value >> 24) & 0xFF));
        bytes.append(static_cast<char>((value >> 16) & 0xFF));
        bytes.append(static_cast<char>((value >> 8) & 0xFF));
        bytes.append(static_cast<char>(value & 0xFF));
    };
    // NBT 里的字符串 = u16 长度 + 字节（"modified UTF-8"，BMP 以内的字和 UTF-8 一样）。
    const auto text = [&u16, &bytes](const QString &value) {
        const QByteArray utf8 = value.toUtf8();
        u16(static_cast<int>(utf8.size()));
        bytes.append(utf8);
    };
    const auto namedString = [&bytes, &text](const char *name, const QString &value) {
        bytes.append(static_cast<char>(0x08));  // TAG_String
        text(QString::fromLatin1(name));
        text(value);
    };

    bytes.append(static_cast<char>(0x0A));  // 根：TAG_Compound
    u16(0);                                 // 根标签名（实机上是个空串）
    bytes.append(static_cast<char>(0x09));  // TAG_List
    text(QStringLiteral("servers"));
    bytes.append(static_cast<char>(0x0A));  // 元素类型：TAG_Compound
    u32(static_cast<int>(servers.size()));
    for (const QPair<QString, QString> &server : servers) {
        namedString("name", server.first);
        namedString("ip", server.second);
        bytes.append(static_cast<char>(0x00));  // 这一条到头
    }
    bytes.append(static_cast<char>(0x00));  // 根到头
    return bytes;
}

/// 读一个文件的全部字节（打不开就是空）。和 writeFileAt 成对：U 段那些"逐字节没被动过"
/// 的断言靠它 —— 值层面的对照看不见"写了一个一样的值"，而污染可能长得正是那样。
inline QByteArray readBytesAt(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    const QByteArray bytes = file.readAll();
    file.close();  // Windows：别攥着句柄，后面还要删它
    return bytes;
}

/// 按 objectName 认控件。**不要**退回按占位文案或条目文本去认：那些是会被产品逻辑
/// 改写的显示内容（占位文案现在就要随"有没有可选版本"切换），拿显示内容当身份标识
/// 的测试会在产品改动时以"installed == nullptr，后面全部断言不跑"的方式挂掉。
template <typename T> T *byObjectName(QWidget *parent, const QString &name) {
    for (T *widget : parent->findChildren<T *>()) {
        if (widget->objectName() == name) {
            return widget;
        }
    }
    return nullptr;
}

inline QComboBox *comboByObjectName(QWidget *parent, const QString &name) {
    return byObjectName<QComboBox>(parent, name);
}

/// 一个颜色落在**哪一档状态色**上：0=红、1=黄、2=绿、-1=认不出（含灰）。
/// 只看色相和饱和度（分类的边界与 ui_check 的 J2 段同一套），不看具体色值 ——
/// 这样它验的是"选了哪个状态"，而"深底/浅底各取哪个值"是另一件事。
///
/// NOTE: ★ 起"没实例在跑"那一档是**灰**的，而灰**没有色相** ⇒ 它在这儿
/// 回 -1，和"认不出"撞在一起。要量那一档请用下面的 `runStateIsGrey()`；
/// `-1` 不能再被当成"那档灰"来用（这一档最容易漏）。
inline int runStateHueBand(const QColor &color) {
    if (color.saturation() < 60) {
        return -1;
    }
    const int hue = color.hue() < 0 ? 0 : color.hue();
    if (hue <= 20 || hue >= 340) {
        return 0;
    }
    if (hue >= 30 && hue <= 60) {
        return 1;
    }
    if (hue >= 100 && hue <= 160) {
        return 2;
    }
    return -1;
}

/// 那一档灰（"没实例在跑"）：**说不上是什么颜色** —— 饱和度低到一定程度。
///
/// 灰不走色相（`QColor::hue()` 对它是 -1），所以它量不了"在红/黄/绿哪一段"；
/// 这一档和"红"（有实例异常退出）的分别正是**饱和**：一个说不上颜色，一个是纯正的红。
inline bool runStateIsGrey(const QColor &color) { return color.saturation() <= 40; }

/// 左下角那三档色值是**照哪个调色板**算出来的。产品取的是**底栏**的（方块画在底栏
/// 那块底上）；测试必须取同一处，不然量到的永远是"另一档主题"的颜色。
/// NOTE: 千万别拿方块那个 QLabel 自己的 palette() 或者那颗**按钮**的：按钮上挂着 QSS，
/// 它解析出来的 QPalette 里 Window 是黑的（亮度 0），谁拿它算谁就掉进深色那一档。
inline QPalette runStateMarkPalette(QWidget *window) {
    QWidget *bar = byObjectName<QWidget>(window, MainWindowUI::bottomBarName());
    return bar != nullptr ? bar->palette() : window->palette();
}

/// 实例页那张树里的**实例条目**（跳过组头）。
///
/// 判据和产品里 instanceIdOf() 是同一个：组头挂着 groupIdRole，实例条目没有。用
/// QTreeWidgetItemIterator 而不是两层循环 —— 组现在是平的，但真加了嵌套也不会漏。
/// 测试里凡是要"数一数有哪些实例"的地方都该走它，别再写 count()：那一层里还混着组头。
inline QList<QTreeWidgetItem *> instanceItemsOf(QTreeWidget *tree) {
    QList<QTreeWidgetItem *> items;
    if (tree == nullptr) {
        return items;
    }
    QTreeWidgetItemIterator it(tree);
    while (*it != nullptr) {
        if (!(*it)->data(0, MainWindowUI::groupIdRole()).isValid()) {
            items.append(*it);
        }
        ++it;
    }
    return items;
}

/// 树里所有**组头**的文字（没有分组时是空的）。
inline QStringList groupHeadersOf(QTreeWidget *tree) {
    QStringList headers;
    if (tree == nullptr) {
        return headers;
    }
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        if (item != nullptr && item->data(0, MainWindowUI::groupIdRole()).isValid()) {
            headers << item->text(0);
        }
    }
    return headers;
}

/// 某个组头底下挂着哪些实例 id。
/// @param tree 实例页那张树。
/// @param headerText 组头那一行的文字（就是 groupHeadersOf() 里那种，带着 (N)）。
///
/// NOTE: 返回值**排过序**。底下的顺序跟着扫描顺序走（installedVersions() 给的那一串），
/// 那是"目录的列举顺序"，不是产品语义 —— 断言跟着它漂的话，换一台机器跑出来的
/// 红是一个和被测行为无关的差异。
inline QStringList groupChildrenOf(QTreeWidget *tree, const QString &headerText) {
    QStringList ids;
    if (tree == nullptr) {
        return ids;
    }
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *header = tree->topLevelItem(i);
        if (header == nullptr || header->text(0) != headerText) {
            continue;
        }
        for (int j = 0; j < header->childCount(); ++j) {
            ids << header->child(j)->data(0, Qt::UserRole).toString();
        }
    }
    ids.sort();
    return ids;
}

inline QPushButton *startButton(QWidget *parent) {
    for (QPushButton *button : parent->findChildren<QPushButton *>()) {
        if (buttonFirstLine(button) == QStringLiteral("启动游戏")) {
            return button;
        }
    }
    return nullptr;
}

/// 把"记住上次启动的版本"写进沙盒 QSettings。直接走生产类，顺带验它真能落盘。
inline void rememberLastVersion(const QString &id) {
    LauncherSettings settings;
    settings.setLastLaunchedVersion(id);
}

/// 一边转事件循环一边等某件事成真，顺带把进程日志流出来（`collectMessage` 收的那些）。
///
/// 存在的理由：resource-live 那一节等的是**真网络**，中间那几步（搜索 → 版本清单 → 下载）
/// 里"走到哪儿了"只有产品自己写的那几行日志看得见（进度不进日志，见产品级约束）——
/// 不流出来，一个卡住的探针和一个还没跑完的探针长得一模一样。
///
/// 「没等到」不当场报红：有的等待两边都是合法结局（"这个游戏版本上没有可下载的版本"
/// 和"作者真的传过东西"），判据留给调用方。
///
/// @param printedOut 上次流到哪一行了（跨多次调用要接着往下流，不能每次从头播）。
template <typename F> inline bool pumpUntil(F done, qint64 timeoutMs, int *printedOut = nullptr) {
    QElapsedTimer clock;
    clock.start();
    for (;;) {
        // 50 毫秒一轮：processEvents() 带时限才不会在没有事件时一直憋着。
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        if (printedOut != nullptr) {
            const auto messages = messagesSnapshot();
            for (; *printedOut < messages.size(); ++*printedOut) {
                std::printf("        [日志] %s\n", qPrintable(messages.at(*printedOut)));
            }
            std::fflush(stdout);
        }
        if (done()) {
            return true;
        }
        if (clock.elapsed() > timeoutMs) {
            return false;
        }
    }
}

// ---------- 模态窗：登录成功/失败都要弹窗，而模态窗会卡住 exec() ----------
// 产品约定是"挡住操作的错误必须弹窗"（不能只写日志），所以登录失败会弹，成功也会弹。
// 弹出来的那一刻 exec() 就在等用户点 —— 测试里不安排人把它点掉就会永远卡在那儿。
// 做法是先排一个 0 毫秒的定时器：它在模态窗自己那个事件循环里立刻响，把窗关掉，顺手
// 把上面写了什么记下来，好让断言能检查"弹窗说的对不对"。
inline QString g_modalTitle;
inline QString g_modalText;
inline bool g_modalWasWarning = false;

/// 下面那几个助手共用的**"这一发还作不作数"**的凭据：每调用一次助手就加一，过期的
/// 定时器到点自己退出去。
///
/// NOTE: 为什么必须有它 —— 兜底那条等的是**事件循环**里的 300 毫秒。前面某一段要是卡了
/// 一下（起游戏进程、同步等），那条定时器会晚到很久，落在**后面某一段**弹出来的
/// 东西上；它认不出那已经不是自己等的那个，照旧把它点掉。实测见过：U1 的兜底跑到
/// U5 的「Java 版本对不上」那一块上，把它按"取消"关了 —— 用户视角就是"这次启动被
/// 莫名取消了"，而测试红的是 U5 那两条。这不是产品的问题（关掉那个框本来就该取消
/// 启动），是测试自己两只手打架。
inline int g_modalHandlerGeneration = 0;

/// 现在**真的摆在屏幕上**的顶层窗有几个。
///
/// 这是用户给的那条方向（「最终不出现独立的窗口」）唯一能量出来的形状：提示摆出来之前
/// 数一遍、摆出来之后再数一遍，两个数必须一样。
///
/// NOTE: 判据是 `isWindow() && isVisible()` 而不是只数 `topLevelWidgets().size()` ——
/// Qt 里"没有父控件的 QWidget 就是窗口"，覆盖层自己那个 `m_parking`（寄存还回去的
/// 内容用的空控件，见 OverlayHost.hpp）也算一个，可它从来没显示过，不该算"多了一个窗"。
void runPromptProbeChecks();
int topLevelWindowCount();

/// 现在开着的那一层覆盖层（设置页和各种提示都开在它里面）。找不到返回 nullptr。
OverlayHost *currentOverlay();

/// 现在开着的**整页**那一层（「实例设置」这种）。找不到返回 nullptr。
OverlayHost *currentPageOverlay();

/// 现在开着的那一条**提示**。找不到返回 nullptr（= 这一发根本没弹东西）。
InlineMessageBox *currentPrompt();

/// 把这一条说了什么记进 g_modal* 那几个全局里（断言读的就是它们）。
void recordPrompt(InlineMessageBox *box);

/// 点这一条提示上的第 index 颗按钮。点不着就把它整块收掉（等于"没选"，见 InlineMessage）。
void clickPromptButton(int index);

/// `dismissNextPrompt()` 的语义是**不选**（不是把框 `close()` 掉）。
/// 在这一层里"没选"就是 `clickedIndex()` 停在 -1。映射过来的那些问句里**最后一颗恒是
/// 「否 / 取消」**（`InlineMessage::question` 的按钮表就是 `{是要问的那件事, 否}`），
/// 所以点最后一颗得到的是同一个结果 —— 这也是为什么这里要数到最后一颗，而不是点第一颗。
void dismissNextPrompt();

/// 点**一颗具体的按钮**（不是"不选"）。
///
/// 为什么不能靠 dismissNextPrompt()：那一颗是最后一颗（否 / 取消），而"我要答是"和
/// "框被关了"在断言里必须是两件不同的事。位置按**下标**认（不是按
/// QMessageBox::StandardButton 认）：映射过来时按钮表就是 `{是, 否}`，所以
/// `QMessageBox::Yes` → 下标 0、`QMessageBox::No` → 下标 1。
void answerNextPrompt(int index);

/// 底栏左面板里那颗电源点出来的「停止游戏」那一问：**点那颗"停止"**。
///
/// 那一问的按钮表是 `{停止游戏, 取消}`（见 MainWindow::confirmStopGame），停止是**第一颗**。
/// 这一层里没有角色这一说："哪一颗是危险动作"是按**下标**给的（`destructiveIndex`，
/// 它还顺带决定那颗按钮染不染红，见 InlineMessageBox 的 ctor），所以只能按下标认。
/// **两颗的字都断言一遍**：光按下标点，将来按钮顺序换了也照样绿。
void answerStopGamePrompt();

/// @param instanceOut 页眉那句话（调用方在 processEvents() 之后读它 —— 这个助手只是把
/// 定时器排上去，**不**在这里等）。
///
/// NOTE: 判据还是"页眉上那句话"，不是"有没有这一页" —— 那样的话"开错了实例"这条断言
/// 就永远绿。弹出的是别的东西时，这里也会把它记进 g_modalTitle/g_modalText 再收掉，
/// 于是断言看到的是"多弹了一个"。
void closeInstanceSettingsPage(QString *instanceOut);

/// 那个「Java 版本对不上」的三岔口上**点具体的按钮**：不同的按钮通向三条不同的路，
/// 而光把它收掉只能得到"不启动"那一条。
inline QString g_mismatchLabel;

void answerJavaMismatchDialog(const QString &buttonName, bool checkDontRemind);

/// 「连着答几条提示」的那台小答话器（AJ 那一节用）。
///
/// 为什么不能用上面那几个"排一发"的助手（`answerNextPrompt()` 那一族）：启动前那两条提醒
/// **可以前后一起来**（同一个实例上连点两下、两条都开着时，先问「重复启动」再问
/// 「进度提醒」），而排一发那套里 300 毫秒的兜底会在第二问开着时**再点一次** ——
/// `recordPrompt(nullptr)` 把刚记下来的标题清掉，断言读到的是空的（红的理由看着像"产品
/// 没弹"，方向正好反了）。
///
/// 所以这里改用**自走的定时器**：把"要答的按钮下标"排成脚本，每一拍看一眼有没有提示开着，
/// 有就记下它说了什么（标题 / 正文 / 小字 / 按钮表）、按脚本点下一发。产品那条路是**在
/// 嵌套事件循环里**弹框的（`execPrompt`），所以定时器只要在跑，它自己就会在框摆出来的
/// 那一刻醒过来 —— 调用方那边是阻塞的，不用管。
class PromptAnswerer {
public:
    /// @param script 第 1、2、3… 条提示各点第几颗按钮（不够用时最后一格顶上）。
    explicit PromptAnswerer(const QList<int> &script) : m_script(script) {
        m_timer.setInterval(10);
        QObject::connect(&m_timer, &QTimer::timeout, [this] { tick(); });
    }
    void start() {
        // 排发那套（`dismissNextPrompt()` / `answerNextPrompt()`）留下的**那一条 300 毫秒
        // 兜底可能还挂在事件循环里**（就是 `g_modalHandlerGeneration` 那段 NOTE 说的
        // "测试自己两只手打架"）。前面那一段要是跑了很久（这一节前面在写盘、压 gzip，
        // 一次 PowerShell 就是小一秒），那条定时器**一进事件循环就立刻到期**，落在
        // **我们**等来的第一问上，照旧把它点掉 —— 症状是"脚本一条都没记着、框像是压根
        // 没弹过"，而下面那些断言会红在"页没打开"上，方向正好反了。
        // 这一加等于说：从这一刻起，排发那套一律作废（它们自己认代数）。
        ++g_modalHandlerGeneration;
        m_clock.start();
        m_timer.start();
    }
    void stop() { m_timer.stop(); }

    /// 一条提示说了什么（按弹出的先后顺序）。
    struct Answered {
        QString title;
        QString text;
        QString note;
        QStringList buttons;
    };
    QList<Answered> answered;

private:
    void tick() {
        if (m_clock.elapsed() > 20000) {
            // 兜底：不让测试挂在"等一个永远不来的框"上（正常情况下一拍就答完了）。
            m_timer.stop();
            return;
        }
        InlineMessageBox *box = currentPrompt();
        if (box == nullptr) {
            return;  // 这会儿没有框开着：下一拍再看
        }
        Answered one;
        if (auto *title = box->findChild<QLabel *>(InlineMessageBox::titleLabelName())) {
            one.title = title->text();
        }
        if (auto *text = box->findChild<QLabel *>(InlineMessageBox::textLabelName())) {
            one.text = text->text();
        }
        one.note = box->noteText();
        for (int i = 0;; ++i) {
            auto *button = box->findChild<QPushButton *>(InlineMessageBox::buttonName(i));
            if (button == nullptr) {
                break;
            }
            one.buttons.append(button->text());
        }
        answered.append(one);
        const int nth = answered.size();  // 这是第几条（1 起）
        const int index = nth <= m_script.size() ? m_script.at(nth - 1)
                                                : m_script.value(m_script.size() - 1, 0);
        clickPromptButton(index);
    }
    QTimer m_timer;
    QElapsedTimer m_clock;
    QList<int> m_script;
};

/// 把**整页**（`OverlayHost::execPage()` 开出来的那种）收掉：它们自己开出来、调用方卡在
/// 嵌套事件循环里，测试这边得有人点返回箭头。
///
/// 为什么不用 `closeInstanceSettingsPage()`（那一族是 0 毫秒 + 300 毫秒排两发）：这一页是
/// **等后台那一趟跑完才开出来的**，落在兜底那一发之后 —— 排发那套全落空，测试就挂在
/// 嵌套循环里了。所以这里也用**自走的定时器**：每一拍看一眼有没有整页开着，有就记下它长
/// 什么样、点返回（见 PromptAnswerer 那段同一套理由）。
class PageCloser {
public:
    void start() {
        ++g_modalHandlerGeneration;  // 排发那套一律作废（理由见 PromptAnswerer::start）
        m_clock.start();
        m_timer.setInterval(10);
        QObject::connect(&m_timer, &QTimer::timeout, [this] { tick(); });
        m_timer.start();
    }
    void stop() { m_timer.stop(); }

    /// 只认**这个标题**的那一页（留空 = 每一页都算，默认如此）。
    void setOnlyTitle(const QString &title) { m_onlyTitle = title; }

    /// 开过又收掉的每一页说了什么（按先后顺序）。
    struct Seen {
        QString title;    ///< 覆盖层页眉上那句话
        QString summary;  ///< 体检报告页顶部那句总述（别的页上没有这一格，留空）
        /// 更新中心那一页上的东西：总述那一句、两组的标题、
        /// 每一行的名字（左栏）和状态（右边那一格）。**收页之前**读下来。
        ///
        /// 为什么读的是**控件上的字**而不是哪一份数据：这一页是 MainWindow 那一层
        /// 翻好的（`updateCenterReportForPage()`），"翻得对不对"只有看屏幕上的字才算数
        /// （读数据等于把翻译那一层绕过去自证）。
        QString updateCenterSummary;
        QStringList updateCenterGroupTitles;
        QStringList updateCenterRowNames;
        QStringList updateCenterRowStates;
    };
    QList<Seen> seen;

private:
    void tick() {
        if (m_clock.elapsed() > 20000) {
            m_timer.stop();
            return;  // 兜底：不让测试挂在"等一页永远不来的页"上
        }
        OverlayHost *host = currentOverlay();
        if (m_closing) {
            // 收页只是**请求**（见 OverlayHost::evict() 那段）：它要等这一趟调用栈退出去
            // 才真的收干净。在这之前别再记一遍（同一页会被记成两条）。
            if (host == nullptr || !host->pageOpen()) {
                m_closing = false;
            }
            return;
        }
        if (host == nullptr || !host->pageOpen()) {
            return;
        }
        Seen one;
        if (auto *title = host->findChild<QLabel *>(OverlayHost::overlayTitleLabelName())) {
            one.title = title->text();
        }
        // 指定了标题就只认那一页（见 setOnlyTitle 那段）：**不记也不收**，让它继续开着。
        if (!m_onlyTitle.isEmpty() && one.title != m_onlyTitle) {
            return;
        }
        if (auto *summary =
                host->findChild<QLabel *>(MainWindowUI::integrityReportSummaryName())) {
            one.summary = summary->text();
        }
        // 更新中心：总述 + 两组的标题 + 每一行那两格。组的数量是**固定的两个**
        // （见 UpdateCenterPage::rebuild() —— 一组空了也摆），行数按 `updateCenterRowN`
        // 往后数到找不到为止。
        if (auto *summary =
                host->findChild<QLabel *>(MainWindowUI::updateCenterSummaryName())) {
            one.updateCenterSummary = summary->text();
        }
        for (int group = 0; group < 8; ++group) {
            auto *title = host->findChild<QLabel *>(MainWindowUI::updateCenterGroupName(group));
            if (title == nullptr) {
                break;
            }
            one.updateCenterGroupTitles.append(title->text());
        }
        for (int index = 0; index < 512; ++index) {
            auto *row = host->findChild<QWidget *>(MainWindowUI::updateCenterRowName(index));
            if (row == nullptr) {
                break;
            }
            auto *name = row->findChild<QLabel *>(MainWindowUI::updateCenterRowNameLabelName(index));
            auto *state = row->findChild<QLabel *>(MainWindowUI::updateCenterRowStateName(index));
            one.updateCenterRowNames.append(name == nullptr ? QString() : name->text());
            one.updateCenterRowStates.append(state == nullptr ? QString() : state->text());
        }
        seen.append(one);
        m_closing = true;
        if (auto *back = host->findChild<QPushButton *>(OverlayHost::overlayBackButtonName())) {
            back->click();  // 返回箭头 = reject（见 OverlayHost::execPage）
            return;
        }
        host->closeOverlay();  // 兜底：至少别让测试卡在这一层上
    }
    QTimer m_timer;
    QElapsedTimer m_clock;
    bool m_closing = false;
    QString m_onlyTitle;
};

/// 让底栏**重画一次**（走 updateBottomBar() 那条真路）。
///
/// updateBottomBar() 是私有的，只能由公开的那条路触发。这里用的是**切一下选项卡**：
/// 那个信号在 setupConnections() 里接着 onMainTabChanged() → updateBottomBar()，而且
/// 它一行状态都不改 —— 这一点是必须的：AB / AD 两节量的都是"重画之后那几个数是从记着的
/// 那份里捞回来的"，扳机自己要是先把数写一遍（比如重灌一次 onVersionDownloadStats），
/// 那就成了自证。
///
/// 为什么不用主页那个「离线模式」勾选框：它已经去掉了（离线身份由当前账号决定）。
/// 为什么
/// 不用 setDownloadUiBusy()：那是全项目唯一的 Q_INVOKABLE，但它会把下载那几个数清零，
/// 正好把要量的现场毁掉。真正在生产里触发这次重画的是「游戏吐出第一行输出」
/// （onProcessOutput / onGameFinished），那两条路要有真的进程起来，验不了。
inline void repaintBottomBar(QWidget *window) {
    auto *tabs = window->findChild<QTabWidget *>(MainWindowUI::tabWidgetName());
    if (tabs == nullptr) {
        return;
    }
    const int back = tabs->currentIndex();
    // 换到主页再换回来。**绝不用设置页**：切到那边会顺带扫一遍 Java（要起进程）。
    tabs->setCurrentIndex(back == MainWindowUI::homeTabIndex()
                              ? MainWindowUI::downloadTabIndex()
                              : MainWindowUI::homeTabIndex());
    tabs->setCurrentIndex(back);
    QApplication::processEvents();
}

  // namespace

// QTreeWidgetItem 不是 QObject，Qt 没给它带 Q_DECLARE_METATYPE —— 而下面 Y6 要按名字
// 调 QTreeWidget::itemClicked（那条信号的参数就是它），invokeMethod 得先在元系统里
// 认得这个名字。声明放在 main 外面：Q_DECLARE_METATYPE 只能在文件作用域展开。
Q_DECLARE_METATYPE(QTreeWidgetItem *)

/// 把 `QDesktopServices::openUrl()` **截胡**下来的一只替身（P2 段用）。
///
/// 为什么非得截：齿轮菜单里的「打开」走的就是 `QDesktopServices::openUrl()`（Windows 上
/// 底下是 ShellExecute），真跑一次 = 在跑测试这台机器上**弹出一个真的资源管理器窗口**。
/// 那既污染了这台机器，也让"路径对不对"变成肉眼看一眼才能知道的事。
///
/// `setUrlHandler("file", ...)` 之后那一发就直接送到这里，不再往下走到系统那儿 ——
/// 于是"点了「打开」，启动器请求打开的到底是哪个目录"变成了一条可断言的事。
///
/// NOTE: 必须是**插槽**（moc 那一套按名字找它）。这也是这个文件里唯一的 Q_OBJECT ——
/// 元对象只由 wtfml_mw_fixture 对本头文件生成，测试模块复用它。
class UrlSpy : public QObject {
    Q_OBJECT
public:
    /// 截下来的每一发（按顺序）。用字符串比用 QUrl：断言里打出来是人能读的那一行。
    QStringList urls;
public slots:
    void handle(const QUrl &url) { urls.append(url.toLocalFile()); }
};

// =========================================================================
// ZB8：「核对那一发」的**三种出路**，端到端跑一遍（本地假服务器，不碰外网）。
//
// 这是用户 那条「缓存默认不过期，每次打开用网络刷新，比较两个文件是否有
// 区别（如果这个会严重占用网络的话就不做了）」在**代码**上的正脸：同一个图标，
// 这一趟第一次被要的时候会带条件头发一发，然后三条路各走各的 ——
// * **304**（没变）→ 不写盘、不发信号、界面上一格都不动；
// * **200 且字节一模一样**（主机不认条件头时就是这样）→ 同上；
// * **200 且字节不一样**（作者换了图标）→ 写盘、存新凭据、再发一次 iconReady。
// 第一条错了（比如把 304 当成失败）就会用一个空 QPixmap 把已经画上去的那一格**擦掉**；
// 第三条错了就是"作者换了图标，用户永远看不到新的"。两条都**不报任何错**，
// 所以只能在本地假服务器上钉住 —— 真网络那条探针（下面 icon-live）只报数、不断言。
//
// 【为什么住在 mw_check 而不是 verify】
//
// 这条路要 **QPixmap**，而 QPixmap 没有 QGuiApplication 根本建不出来 —— verify 那个工程
// 是 QCoreApplication（纯控制台），在那里跑出来的症状是
// 「QPixmap cannot be created without a QGuiApplication」，然后**每一条都红**，
// 而且红得莫名其妙：盘上那份解不出来 → 被当成"盘上没有"，走到另一个分支去了。
// 这里是真的 QApplication，所以跑得起来。
//
// 前提：只有**要 webp 那条路**的地址是测试能定的（那条用的是接口给的 hit.iconUrl；
// 要 PNG 时是 ResourceCatalog 里写死的 CDN 地址）。所以这一节跑在"这台机器解得了 webp"
// 之上 —— 本机（用户这台）实测是真的；真换了台没装那个编解码器的机器，这里打一句
// 说明就跳过（那台机器上产品走的是 PNG 那条路，逻辑一样，只是这里够不着那个地址）。
// =========================================================================

/// 一张现造的、解得出像素的 PNG（这一节要拿它当真图标字节用）。
static QByteArray solidPngBytes(const QColor &color, int side = 32) {
    QImage image(side, side, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    buffer.close();
    return bytes;
}

/// 一个只答一种话的假图标服务器（127.0.0.1、随机端口）。
///
/// 记着"来问过几次"和"那一发带了哪些条件头"—— ZB8 要验的东西全在这两件事上。用它就
/// 不碰外网，在哪台机器上跑都是同一个结果；而"核对那一发"是这次改动最要紧的那条路，
/// 光靠 icon-live 那条只报数的探针是钉不住的。
/// 预置用的两个地址。哈希是**真的**（从 `api.mineskin.org/v2/skins` 拿回来的
/// 那一页里抄的，见 verify 的 ZE 段），形态和产品里会落到盘上的一模一样。
///
/// 两张是为了 AF 段能验"又挑中了一张"这件事：只给一张的话，"换上了"和"还是原来那张"
/// 在屏幕上长得一样，断言就恒真了。
static const QString kSeededSkinUrl = QStringLiteral(
    "https://textures.minecraft.net/texture/"
    "ba61d4e3a112c1959cfc5c7811ee442f055334ef787eee1795f9d947d56bfa39");
static const QString kSeededSkinUrl2 = QStringLiteral(
    "https://textures.minecraft.net/texture/"
    "a48f69d77cb323c5bbe0250143759cd7abff5e08774f7888556fa641cfa75f12");

/// 给某个离线账号**预置一张随机皮肤**：盘上记下"挑中的是这一张"，并把那张图本体按
/// 内容寻址塞进贴图缓存（`<skinsDir>/<sha1(url)>.png`）。
///
/// 为什么非预置不可： 起，**离线账号**的外观走的是 RandomSkinStore ——
/// 盘上还没记过就真的去问 MineSkin 要一页列表。而这个工程里凡是"当前账号是离线账号"
/// 的地方都会触发它：
/// - 每一次 `MainWindow w;` —— 构造函数末尾那句 startPlayerSkinLoad()（见 MainWindow.cpp）；
/// - 每一次切到离线账号（refreshAccountSkin → startPlayerSkinLoad）；
/// - 打开「账号与外观…」窗（那个窗会刷新外观）。
/// 不预置的话这一跑就会真发请求：有网时快、没网时要等满 10 秒超时，而**回来的那条信号
/// 还会去写日志、还可能把屏幕上那张脸换掉** —— 等于给别的断言掺进一个看运气的因素
/// （正是 U 段"每一步都要点启动、每一步都要看弹窗"最怕的那种）。
///
/// 预置之后这条路就变成**同步命中**：RandomSkinStore 读出盘上那一行 → requestSkinTexture
/// → PlayerSkinStore 发现缓存里有 → 同步发 textureReady → 立刻画上。一个包都不发。
///
/// NOTE: 用 `AccountStore::offlineUuidFor(name)` 算 uuid，所以**账号还没建出来就能先摆好**
/// （这是它作为 public static 存在的理由之一）。三处都要提前调 —— 建账号那一下
/// 本身就会把它设成当前身份。
static void seedOfflineRandomSkin(const QString &name, const QString &skinUrl,
                                  const QColor &tint) {
    QDir().mkpath(AppPaths::skinsDir());
    const QString cached = PlayerSkinStore::cachePathForUrl(skinUrl);
    // 皮肤贴图是 64×64 那种（X5 那几条塞的也是这个尺寸）。
    QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);
    image.fill(tint);
    image.save(cached);
    RandomSkinStore::saveChosenSkinUrl(AccountStore::offlineUuidFor(name), skinUrl);
}

/// 一个只答一种话的假图标服务器（127.0.0.1、随机端口）。
///
/// 记着"来问过几次"和"那一发带了哪些条件头"—— ZB8 要验的东西全在这两件事上。用它就
/// 不碰外网，在哪台机器上跑都是同一个结果；而"核对那一发"是这次改动最要紧的那条路，
/// 光靠 icon-live 那条只报数的探针是钉不住的。
struct FakeIconServer {
    QTcpServer server;
    QByteArray body;      ///< 200 时发回去的正文
    QByteArray headers;   ///< 200 时额外带的头（ETag / Last-Modified 那两条）
    int status = 200;
    int requests = 0;
    QString lastIfNoneMatch;
    QString lastIfModifiedSince;

    bool start() {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this]() {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                socket->setParent(&server);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    if (socket->property("answered").toBool()) {
                        return; // 一个连接上只答一发（这一发不带正文，读一次就够）
                    }
                    socket->setProperty("answered", true);
                    const QByteArray request = socket->readAll();
                    ++requests;
                    const int headerEnd = request.indexOf("\r\n\r\n");
                    const QByteArray head = headerEnd > 0 ? request.left(headerEnd) : request;
                    for (const QByteArray &line : head.split('\n')) {
                        const QByteArray trimmed = line.trimmed();
                        const int colon = trimmed.indexOf(':');
                        if (colon <= 0) {
                            continue;
                        }
                        const QByteArray name = trimmed.left(colon).trimmed().toLower();
                        const QString value =
                            QString::fromUtf8(trimmed.mid(colon + 1)).trimmed();
                        if (name == "if-none-match") {
                            lastIfNoneMatch = value;
                        } else if (name == "if-modified-since") {
                            lastIfModifiedSince = value;
                        }
                    }
                    QByteArray response = QByteArrayLiteral("HTTP/1.1 ") +
                                          QByteArray::number(status) +
                                          (status == 304 ? QByteArrayLiteral(" Not Modified\r\n")
                                                         : QByteArrayLiteral(" OK\r\n"));
                    if (status == 304) {
                        // 没变：一个字节的正文都没有。**这一条正是"不严重占网络"的全部根据。**
                        response += QByteArrayLiteral("Content-Length: 0\r\n\r\n");
                    } else {
                        response += headers;
                        response += QByteArrayLiteral("Content-Length: ") +
                                    QByteArray::number(body.size()) +
                                    QByteArrayLiteral("\r\n\r\n");
                        response += body;
                    }
                    socket->write(response);
                    socket->flush();
                });
            }
        });
        return server.listen(QHostAddress::LocalHost, 0);
    }
};

/// 一个"上来就答 500"的假 CurseForge 接口（127.0.0.1、随机端口）。
///
/// AA10 要的只有两件事：**这一发确实发到了这里**（数连接），以及答复是**一句我们能认出来
/// 的错**（那句原话必须原样出现在弹窗里）。所以它比 verify 里那个真解析请求、按路径分发的
/// 版本短得多 —— 那边要验的是"正文写成什么样"，这边要验的是"到底发没发出来"。
///
/// NOTE: 得等到 `readyRead` 再答（不能 accept 了就写）：客户端那时才刚把请求送出来，
/// 早答是一场比赛，赢了也只是运气好。
struct OneShotCfErrorApi {
    QTcpServer server;
    int hits = 0;

    bool start() {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this]() {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                socket->setParent(&server);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    if (socket->property("answered").toBool()) {
                        return;
                    }
                    socket->setProperty("answered", true);
                    ++hits;
                    socket->readAll();
                    const QByteArray body =
                        QByteArrayLiteral("{\"error\":\"接口说你没填 key\"}");
                    socket->write(QByteArrayLiteral("HTTP/1.1 500 Error\r\n"
                                                    "Content-Type: application/json\r\n"
                                                    "Content-Length: ") +
                                  QByteArray::number(body.size()) +
                                  QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
                    socket->flush();
                    socket->disconnectFromHost();
                });
            }
        });
        return server.listen(QHostAddress::LocalHost, 0);
    }

    QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());
    }
};

void runIconRevalidateTests();

// ---------------------------------------------------------------------------
// 默认那一跑的**假外网**：Mojang 清单 / 皮肤档案 / Minecraft Services /
// Modrinth / CurseForge 全部指到这一台 127.0.0.1 上。
//
// 为什么非要它：默认那一跑里第一个 MainWindow 一构造就发版本清单，下载页那四页切过去
// 又各发一次搜索，账号那几节还会问皮肤档案。真实网络在场时它们的**耗时和结果**都不
// 归这一跑管 —— 断网机器上每次都要等满 15 秒超时，整跑时长跟着网络抖；反过来，连得上
// 的机器上又会把真清单塞进树里，断言读到多少条全看那天官方发了什么版本。
//
// 所以这里**按请求目标路由**：路径认得出就答一份最小正文，认不出就 404（真服务器上
// "没有这个路由"也是这个样子）。每一发都记进 `hits`，断言看的是**请求次数**，不是
// 响应回到的次序 —— 顺序在并发下本来就不该被断言。
//
// 覆盖从哪来：`LauncherSettings` 的四条新键 + 已有的两条目录接口地址，在第一个
// MainWindow 构造之前写进沙盒 INI（见 main.cpp）。产品侧永远是空串、走官方地址。
struct FakeNetwork {
    QTcpServer server;
    /// 请求路径（去掉查询串）→ 次数。
    QHash<QString, int> hits;
    QStringList paths;  ///< 每一发的路径（带查询串），排查用
    /// 没有路由、被 404 掉的请求数。夹具没演到的路由会落在这里 —— 它比"某条断言
    /// 意外地没红"更早说明这一跑其实没测到东西。
    int notFound = 0;
    /// 被 404 的那几条路径（带查询串）。排查"为什么有 404"时直接看这个，别去翻整个 `paths`。
    QStringList unrouted;

    bool start() {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this]() {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                socket->setParent(&server);
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                 [this, socket]() { answer(socket); });
            }
        });
        return server.listen(QHostAddress::LocalHost, 0);
    }

    QString base() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    }
    QString versionManifestUrl() const {
        return base() + QStringLiteral("/version_manifest_v2.json");
    }
    /// 皮肤档案端点的**完整前缀**（`…/session/minecraft/profile`）。产品那边
    /// `PlayerSkinStore::setEndpointOverrides()` 拿到的就是这个前缀，然后直接拼 32 位 uuid ——
    /// 少写 `/minecraft/profile` 的话请求会变成 `/session/<uuid>`（真实 sessionserver 上没有
    /// 这条路由），夹具这边只会回 404，而界面看起来和"这个人没有皮肤"一模一样。
    QString sessionServerBase() const {
        return base() + QStringLiteral("/session/minecraft/profile");
    }
    QString minecraftServicesBase() const { return base(); }
    QString modrinthBase() const { return base() + QStringLiteral("/v2"); }
    QString curseForgeBase() const { return base() + QStringLiteral("/v1"); }

    /// 某条路径（不带查询串）被打了几次。**断言只认这个**，不认 `paths` 的下标。
    int count(const QString &path) const { return hits.value(path, 0); }

    void answer(QTcpSocket *socket) {
        if (socket->property("answered").toBool()) {
            return;
        }
        const QByteArray buffer = socket->property("buffer").toByteArray() + socket->readAll();
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            socket->setProperty("buffer", buffer);  // 头还没到齐
            return;
        }
        socket->setProperty("buffer", QByteArray());
        socket->setProperty("answered", true);

        QString target;
        for (const QByteArray &line : buffer.left(headerEnd).split('\n')) {
            const QList<QByteArray> parts = line.trimmed().split(' ');
            if (parts.size() >= 2 && parts.at(1).startsWith('/')) {
                target = QString::fromUtf8(parts.at(1));
                break;
            }
        }
        paths << target;
        const int query = target.indexOf(QLatin1Char('?'));
        const QString path = query < 0 ? target : target.left(query);
        ++hits[path];

        const auto send = [socket](int status, const QByteArray &body,
                                   const QByteArray &contentType = QByteArrayLiteral(
                                       "application/json")) {
            socket->write(QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + ' ' +
                          (status == 200 ? QByteArrayLiteral("OK")
                                         : QByteArrayLiteral("Not Found")) +
                          QByteArrayLiteral("\r\nContent-Type: ") + contentType +
                          QByteArrayLiteral("\r\nContent-Length: ") +
                          QByteArray::number(body.size()) +
                          QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
            socket->flush();
            socket->disconnectFromHost();
        };

        // ---- 版本清单（`Launcher/VersionManifestUrl`）----
        if (path == QLatin1String("/version_manifest_v2.json")) {
            const QString versions =
                QStringLiteral(
                    R"([{"id":"1.21.4","type":"release","url":"%1/versions/1.21.4.json",)"
                    R"("time":"2024-12-03T10:12:00+00:00","releaseTime":"2024-12-03T10:12:00+00:00"},)"
                    R"({"id":"26.3","type":"snapshot","url":"%1/versions/26.3.json",)"
                    R"("time":"2026-03-01T10:00:00+00:00","releaseTime":"2026-03-01T10:00:00+00:00"},)"
                    R"({"id":"1.20.1","type":"release","url":"%1/versions/1.20.1.json",)"
                    R"("time":"2023-06-12T10:00:00+00:00","releaseTime":"2023-06-12T10:00:00+00:00"}])")
                    .arg(base());
            send(200, QStringLiteral(R"({"latest":{"release":"1.21.4","snapshot":"26.3"},)"
                                     R"("versions":%1})")
                          .arg(versions)
                          .toUtf8());
            return;
        }
        if (path.startsWith(QLatin1String("/versions/")) &&
            path.endsWith(QLatin1String(".json"))) {
            const QString id =
                path.mid(10, path.size() - 10 - 5);  // 去掉 `/versions/` 和 `.json`
            send(200, QStringLiteral(R"({"id":"%1","type":"release",)"
                                     R"("mainClass":"net.minecraft.client.main.Main",)"
                                     R"("assets":"1.20","libraries":[],)"
                                     R"("arguments":{"game":[],"jvm":[]}})")
                          .arg(id)
                          .toUtf8());
            return;
        }

        // ---- 皮肤档案（`Launcher/SessionServerBase`）与 Minecraft Services ----
        // 只给一份**没有贴图**的档案：`PlayerSkinStore` 会当场判"这份不算数"并退回默认
        // 外观。这里的目的是**不联网、不挂 15 秒**，不是演一份真外观（真外观那几条在
        // MwPlayerSkinChecks 里直接喂信号，不经过这条路）。
        if (path.startsWith(QLatin1String("/session/minecraft/profile/")) ||
            path == QLatin1String("/minecraft/profile")) {
            send(200, QByteArrayLiteral(
                          R"({"id":"0123456789abcdef0123456789abcdef","name":"Fixture",)"
                          R"("skins":[],"capes":[],"properties":[]})"));
            return;
        }

        // ---- Modrinth 目录（`Launcher/ModrinthApiBase`）----
        if (path == QLatin1String("/v2/search")) {
            send(200, QByteArrayLiteral(
                          R"({"hits":[],"offset":0,"limit":20,"total_hits":0})"));
            return;
        }
        if (path.startsWith(QLatin1String("/v2/project/")) &&
            path.endsWith(QLatin1String("/version"))) {
            send(200, QByteArrayLiteral("[]"));
            return;
        }
        if (path == QLatin1String("/v2/projects")) {
            send(200, QByteArrayLiteral("[]"));
            return;
        }
        if (path == QLatin1String("/v2/version_files") ||
            path == QLatin1String("/v2/version_files/update")) {
            send(200, QByteArrayLiteral("{}"));
            return;
        }
        if (path.startsWith(QLatin1String("/v2/project/"))) {
            send(200, QByteArrayLiteral(R"({"id":"fixture","title":"Fixture"})"));
            return;
        }

        // ---- CurseForge 目录（`Launcher/CurseForgeApiBase`）----
        if (path == QLatin1String("/v1/mods/search")) {
            send(200, QByteArrayLiteral(
                          R"({"data":[],"pagination":{"index":0,"pageSize":20,)"
                          R"("resultCount":0,"totalCount":0}})"));
            return;
        }
        if (path == QLatin1String("/v1/mods/files") ||
            path == QLatin1String("/v1/mods") ||
            path == QLatin1String("/v1/fingerprints")) {
            send(200, QByteArrayLiteral(R"({"data":[]})"));
            return;
        }
        if (path.startsWith(QLatin1String("/v1/mods/")) &&
            path.endsWith(QLatin1String("/files"))) {
            send(200, QByteArrayLiteral(R"({"data":[]})"));
            return;
        }
        if (path.startsWith(QLatin1String("/v1/mods/"))) {
            send(200, QByteArrayLiteral(R"({"data":{}})"));
            return;
        }

        ++notFound;
        unrouted << target;
        send(404, QByteArrayLiteral(R"({"error":"fixture: no route"})"));
    }
};

/// 把假外网的五条地址写进**当前沙盒 INI**。必须在构造任何产品对象之前调 ——
/// `MainWindow` 一构造就发版本清单，地址读的是那一刻的设置。
///
/// 以前只有默认那一跑（`tests/mw_check/main.cpp`）这么写，独立构建的专项
/// （例如 `wtfml.workspace`）各自构造 `MainWindow` 却什么都没装 ⇒ 那一跑实际打的是
/// 真 Mojang：断网机器上等满超时，连得上时把当天官方清单塞进树里，而断言全绿。
/// 凡是 ctest 里会构造 `MainWindow` 的入口都走这里。
static void installFakeNetworkSettings(const FakeNetwork &fakeNet) {
    QSettings sandbox;
    sandbox.setValue(QStringLiteral("Launcher/VersionManifestUrl"),
                     fakeNet.versionManifestUrl());
    sandbox.setValue(QStringLiteral("Launcher/SessionServerBase"),
                     fakeNet.sessionServerBase());
    // NOTE: **不设 `Launcher/TextureBase`**。贴图地址那条路（PlayerSkinStore::
    // textureRequestUrl）把 scheme 强制升级成 https，也按覆盖后的 host 校验地址；
    // 指到一个明文 http 的 127.0.0.1 夹具上会让"官方贴图地址"一律判成不合法 ——
    // 预置皮肤那几节（AF）读回来的盘上那一行会变成空串。而假档案里 `skins`/`capes`
    // 都是空的，本来就不会去取任何贴图；真要去取也还是官方 host，不会打到别的什么地方。
    sandbox.setValue(QStringLiteral("Launcher/MinecraftServicesBase"),
                     fakeNet.minecraftServicesBase());
    sandbox.setValue(QStringLiteral("Launcher/ModrinthApiBase"), fakeNet.modrinthBase());
    sandbox.setValue(QStringLiteral("Launcher/CurseForgeApiBase"), fakeNet.curseForgeBase());
    sandbox.sync();
}

// ---------------------------------------------------------------------------
// ZH. 下载页第二取数源：ResourceStore 按来源发请求（本地假服务器，不碰外网）
//
// 这是 "搜索加上来源"那件事的**接线**那一半。地址怎么拼、正文怎么读是
// ResourceCatalog 的纯函数，那两条在 verify 的 Z7 一节逐字钉着；这一节钉的是**这一层
// 到底把哪一发发出去了、带了什么头、回来交给了哪个解析器** —— 纯函数全绿而这里错了的话，
// 症状正是用户看到的那种："选了 CurseForge，界面还是 Modrinth 的东西"、或者"CurseForge
// 那一页永远说版本列表读不懂"。
//
// 四件事，每一件都有个"不查就永远不知道"的形状：
// ① `x-api-key` **只在有 key 时发**（默认走社区镜像、一个头都不带），而**两个来源里只有
// CurseForge 认这个头** —— 给 Modrinth 那发带上它是白发一个别人看不懂的头；
// ② 搜索回来的正文走**对的那个解析器**（CF 的根在 `data`，Modrinth 在 `hits`）；
// ③ 版本列表那道"200 但正文不成形"的闸**按来源分叉**：CF 要 `{"data":[…]}`、
// Modrinth 要数组。只看 isArray() 的话 CF 每一页都会说"版本列表读不懂"；
// ④ 那两条地址（key / override）是**这一次请求读的**，不是构造时读的（用户填完 key
// 回到下载页再点一次搜索就该走新的那条）。
//
// 假服务器：GET 一发，按路径分发出两种答复，并把"请求行 + x-api-key"记下来。
// NOTE: 这里的答复只用**最小**的合成正文（真夹具那两条在 verify 的 Z7f/Z7h 里）——
// 这一节量的是"发到哪、带什么头、谁接的"，正文长得对不对不归它管。
struct FakeResourceApi {
    QTcpServer server;
    QByteArray searchBody;   ///< `/mods/search` 那一发的答复
    QByteArray filesBody;    ///< `/mods/{id}/files` 那一发的答复
    int searchStatus = 200;
    int filesStatus = 200;
    QStringList requests;    ///< 每一发的请求行（`GET /v1/mods/search?… HTTP/1.1`）
    QStringList apiKeys;     ///< 每一发带的 x-api-key（**没带 = 空串**）
    /// 按**请求目标**数的次数（不按下标、不依赖回答次序 —— 并发下先后本来就不定）。
    int searchHits = 0;      ///< `/mods/search` 那一发
    int filesHits = 0;       ///< `/mods/{id}/files` 那一发

    bool start() {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this]() {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                socket->setParent(&server);
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                 [this, socket]() { answer(socket); });
            }
        });
        return server.listen(QHostAddress::LocalHost, 0);
    }

    QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());
    }

    /// 最后那一发的请求行（断言里看的就是它）。
    QString lastRequest() const { return requests.isEmpty() ? QString() : requests.last(); }

    void answer(QTcpSocket *socket) {
        if (socket->property("answered").toBool()) {
            return; // 一发只答一次（GET 没有正文，但 readyRead 可能来不止一回）
        }
        const QByteArray buffer = socket->property("buffer").toByteArray() + socket->readAll();
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            socket->setProperty("buffer", buffer);  // 头还没到齐
            return;
        }
        socket->setProperty("buffer", QByteArray());
        socket->setProperty("answered", true);
        const QList<QByteArray> headLines = buffer.left(headerEnd).split('\n');
        QString apiKey;
        QString target;
        for (const QByteArray &line : headLines) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.startsWith(QByteArrayLiteral("GET "))) {
                target = QString::fromUtf8(trimmed.split(' ').value(1));
            } else if (trimmed.toLower().startsWith(QByteArrayLiteral("x-api-key:"))) {
                apiKey = QString::fromUtf8(trimmed.mid(10)).trimmed();
            }
        }
        requests << QStringLiteral("GET ") + target + QStringLiteral(" HTTP/1.1");
        apiKeys << apiKey;

        const bool isSearch = target.contains(QStringLiteral("/mods/search"));
        if (isSearch) {
            ++searchHits;
        } else {
            ++filesHits;
        }
        const int status = isSearch ? searchStatus : filesStatus;
        const QByteArray payload = isSearch ? searchBody : filesBody;
        socket->write(QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + ' ' +
                      (status == 200 ? QByteArrayLiteral("OK") : QByteArrayLiteral("Error")) +
                      QByteArrayLiteral("\r\nContent-Type: application/json\r\nContent-Length: ") +
                      QByteArray::number(payload.size()) +
                      QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + payload);
        socket->flush();
        socket->disconnectFromHost();
    }
};

/// 发一发、等它回来。返回 false = 超时（这一条本身就该红）。
static bool waitForSearch(ResourceStore &store, const QString &key,
                          ResourceCatalog::SearchResult *out) {
    bool done = false;
    const QMetaObject::Connection conn = QObject::connect(
        &store, &ResourceStore::searchFinished, &store,
        [&done, out](const QString &, const ResourceCatalog::SearchResult &result) {
            *out = result;
            done = true;
        });
    const bool ok = pumpUntil([&done]() { return done; }, 10000);
    QObject::disconnect(conn);
    return ok;
}

static bool waitForVersions(ResourceStore &store, const QString &key,
                            QList<ResourceCatalog::VersionEntry> *out, QString *problem) {
    bool done = false;
    const QMetaObject::Connection conn = QObject::connect(
        &store, &ResourceStore::versionsFinished, &store,
        [&done, out, problem](const QString &,
                              const QList<ResourceCatalog::VersionEntry> &versions,
                              const QString &why) {
            *out = versions;
            *problem = why;
            done = true;
        });
    const bool ok = pumpUntil([&done]() { return done; }, 10000);
    QObject::disconnect(conn);
    return ok;
}

void runResourceSourceTests();

// ---------------------------------------------------------------------------
// ZI. 下载页那一行筛子的接线：控件 → 请求地址（本地假 CF 接口，不碰外网）
//
// 要求：「搜索，加入按版本 模组加载器 来源 类型 名称搜索，一个搜索键和一个清空条件键，
// 名字/版本可自行输入，这两个框选中时按回车等同于按搜索键…整合包那一页带一个导入功能」。
//
// 界面那一半（这几个控件长什么样、摆在哪、模组加载器只在模组页有）在 ui_check 那边量过；
// 数据那一半（这几个参数拼出来的地址对不对）由 ZH 那一节量过。**中间这一段** —— 用户在框里
// 挑的那一项有没有真的变成发出去的那一发请求上的参数 —— 这一段要盯住，而它错起来一个错都
// 不报：搜索照发、结果照回，只是筛子根本没生效（用户看到的是"筛了跟没筛一样"）。
void runDownloadFilterTests();

// ---------------------------------------------------------------------------
// ZJ. 资源详情页那一整条路：下载页点某一行 → 整页详情 → 中间那排 chip → 底下点某一版
//
// 要求：「下载页选择了模组以后进入一个独立界面，上方写模组图像 名字 来源，然后有转到
// modrinth/curseforge/mcmod.cn 的选项，中间一排版本，下方排列版本，和pcl类似」。
//
// 五件事：
// ① 箭头那一格点它**不开页**（那一格是展开/收起，判据和代理画箭头是同一个 arrowRect）；
// ② 点行体开的是**这一行**那一页，而且**不会顺手把这一行收起**（三发事件都被吃掉了）；
// ③ 开页只发一发，而且是"整份清单"那一发（地址里**不带** gameVersion = 不筛）；
// ④ 中间那排 chip 是**本地筛**：点一颗一个请求都不发，筛出来的组/条跟着变；
// ⑤ 点某一版 = 走到「装进哪个实例」那条路。这一节故意**不选实例**，该弹的就是那句提醒 ——
// 这是"行上那把钥匙有没有认对"的唯一凭据：钥匙对不上（比如拿行号当 version id）时
// `downloadResourcePageVersion` 会**静静地什么都不做**，不弹框也不报错。
void runResourceDetailPageTests();

// ---------------------------------------------------------------------------
// T19. 实例设置「启动」栏那两颗按钮：导出启动脚本 / 补全文件
//
// 「补全文件」（repairInstanceFiles → VersionManager::repairVersionFiles）要钉住的是它
// 和"装一个版本"那几条路的区别：
// ① 判据是 **sha1**，不是字节数：摆一份"大小一样、内容不对"的 jar，它必须被换回来；
// ② 本来就是好的文件**一个字节都不许动**（"只补缺的和坏的"，不是全量重写）；
// ③ 它**不写 version.json**（那是 downloadVersion 的事，数据层那边有断言，这里只认
// 界面这一半）；
// ④ 期间这个实例从「能启动的实例」里滤掉，结束之后**回来并且被选中**；
// ⑤ 收尾那句话说的是**用户认得的实例名**，不是裸目录名 —— 这一条钉的是
// onVersionDownloadFinished() 里"先清「补全」那一档占用、再 setDownloadUiBusy(false)"
// 那个顺序：反过来（或者顺序颠倒）时它还在过滤名单里，instanceDisplayName() 只能
// 退回目录名，而两句日志/两个框都会跟着说错是谁。
//
// 「导出启动脚本」要钉的是**那一份 .bat 的内容**：它必须和真正启动那一次同源（两边调的
// 是同一个 launchArguments()），离线身份走 --accessToken 0 那一条，而"令牌拿不到"时
// **一份都不导** —— 导一份起不来的脚本比不导更坏（用户以为导出来的能用）。
//
// 远端全用 `file://`（这一节自己那个 remote 目录），一个请求都不出网。
// ---------------------------------------------------------------------------
void runInstanceStartActionsTests();

// ---------------------------------------------------------------------------
// T33. 完整性检查里"从私有槽点下去"的那一半
//
// 闸口那几条为什么是**确定性**的：判「正在体检」的唯一出处是
// `MainWindow::integrityScanningVersion()`，那一位在 `submitIntegrityCheck()` 里
// **同步**写下去；后台那一趟的收尾（清这一位）是 `invokeMethod(QueuedConnection)` 排回
// 事件循环的。所以「点下去之后**不 pump 事件**就调闸口」这中间它一直是真的。
//
// 为什么每一发闸口都配一趟自己的体检：闸口函数会**弹提示**（挡住操作的必须让用户看见），
// 而弹提示是**嵌套事件循环** —— 那一趟里后台那一趟正好跑完（夹具就两个文件），「正在体检」
// 这位就清了。第二发闸口要是不重新点一次体检，它走的就不是闸口那一条、而是**真去删**了。
//
// 全部离线：身份是这一节自己加的离线账号，远端是 `file://`（其实体检**一条请求都不发**，
// 这里造远端只是让 version.json 里那两条 sha1/size 有个说得通的来路）。
// ---------------------------------------------------------------------------
void runIntegrityScanTests();

// T34. 游戏异常退出里"从 onGameFinished 那一路补出来"的那一半
//
// 分三处验：
// ① **那几句话本身**（每一条提示从哪个字段推出来、退出码怎么写成人话）在 verify 的
// ZP 段（纯函数，逐格念一遍）；
// ② **这一节**：真的把 `onGameFinished` 那一槽打进去 → 账本上**多出一项** kind=crash 的
// 失败、标题/目标/那句原因都对得上；而且**以 0 正常退出的那一档一项都不补**；
// ③ **那一项在界面上露什么**（「导出错误报告」「打开崩溃报告目录」两颗键只在它身上露、
// 详情页那两块字）在 ui_check 的 U29 段。
//
// 为什么这一节**不换数据根目录**：账本落在**启动器自带**那一个固定位置
// （`TaskHistory::defaultHistoryPath()` 用的是 `defaultMinecraftDir()`，故意不跟当前
// 游戏文件夹走 —— 见那边的说明）。所以这里只在跑之前数一遍"已经有几条 crash"，之后
// 看**多了几条**：同一个可执行文件里别的节也会往这本账里写东西，绝对值靠不住。
//
// 为什么那一格 Java 要**造一个假的**：`buildCrashRecord()` 报的"这一趟实际用的 Java"
// 是**现读** `<javaPath>/release` 得到的（`probeJavaMajorVersion()` 先读那个文件，
// 读不到才去真起一个 java -version）。造一个只有 `release` 的目录，那一句就是确定的
// —— 整个这一节因此一条外网请求、一个子进程都不发。
// ---------------------------------------------------------------------------
void runGameCrashTests();

// ---------------------------------------------------------------------------
// T35. 世界与实例备份里"从那几个入口点下去"的那一半
//
// 分三处验：
// ① **引擎**（清单怎么读写、目录怎么走、还原之前那份安全快照、还原之后的对账）在 verify
// 的 ZQ 段（真打 zip、真删目录、纯函数那一层，连目录都不换）；
// ② **这一节**：`snapshotInstance()` / `backupWorldNow()` / `startBackupRestore()` 打下去
// → 账本上那一项（`TaskKind::Backup`）的标题 / 目标 / 收尾都对得上 → 盘上真多出一个包
// → 拿产品的读侧把那个包**读回来**核对（该进的进了、能重新下回来的没进去）→ 还原
// 回来内容一字不差、还原前那一份安全快照也真在；
// ③ **那几颗按钮和那个分岔框长什么样**在 ui_check 的 U30 段。
//
// 这一节**换数据根目录**（和 T34 相反）：备份落在 `<当前游戏文件夹>/cache/backups` 底下，
// 本来就是跟着当前游戏文件夹走的（见 AppPaths::backupDir() 那段）。账本那一份反过来挂在
// 启动器自带的固定位置，所以"这一节新补出来的那几项"一律**按号认**，不按位置。
//
// 那一发「创建快照」的分岔框（默认排除 / 完整 / 取消）用 `PromptAnswerer` 答：它按
// **条数**给按钮下标，第一问点第 0 颗 = 默认那一档 —— 要的正是"默认排除能重新
// 下回来的那部分"，所以这一节量的就是默认那一档。
void runBackupTaskTests();

// ---------------------------------------------------------------------------
// T36. 「上一次启动」那张卡：主页左栏那一小块（离线）
//
// 要求的是「一小块最近活动提示，不是一个仪表盘」，所以这一段分三处验：
//
// ① **账怎么写**：`LaunchHistoryController::recordStart()` / `recordFinish()` 是产品唯一的
// 写点（主窗口只剩一个补"启动那一刻的显示名 / 当时是谁"的转手），
// 摆账就摆这两条（`seedLaunchRecordForTest` 那一族全走它们），摆完看盘上那份
// `wtfml.history` 逐格对不对。判据层（格式 / 时间怎么说 / 目标没了怎么降级 / 上限）
// 在 verify 的 ZR 段逐条念过，这里验的是**接线**：产品读的是同一本账。
// ② **卡片上写出什么**：三行字 + 那颗按钮写「继续游玩」还是「启动实例」。
// ③ **点下去走哪条路**：目标还在 ⇒ 快捷启动（完整入口）；目标没了 / 版本不认那两条
// 参数 ⇒ 降级成「把这一份开起来」（= 用户自己在下拉框里选好实例再点「启动游戏」
// 那条路）；那一份实例自己也已经没了 ⇒ **说一句、不启动**。
//
// 全程离线：身份是这一节自己加的离线账号；三个夹具实例的 `wtfml.meta` 都钉着一个
// **不存在的** Java 路径 ⇒ 真点到启动那一步时它停在「这个实例的 Java 用不了」那一问上，
// **一个进程都不起、一个请求都不发**（U 段那套手法的同一份）。
// ---------------------------------------------------------------------------
#if 0
void runHomeRecentCardTests();
#endif

// ---------------------------------------------------------------------------
// T20. 实例设置「文件管理」那八页：真正碰盘的这一半
//
// 那八页**长什么样**在 ui_check 的 U10 段里验（列得对不对、三条开关摆得对不对、
// "用不上"那一页说的是哪句）。这里验的是另一半 —— 目录从哪儿来、改名和删除到底动了
// 盘上的什么。三件事，每件都有一个"不报错但全错"的失败形状：
//
// ① **目录从哪儿来**。四个能下载的类必须走 `ResourceCatalog::folderForKind()`（那是
// 目录名的唯一出处），另外三个写在 `instanceFileDirs()` 里。把 `datapacks` 写成
// `datapack`、或者漏掉一个键，页面上不会报任何错 —— 它只是安静地看着一个**不存在
// 的目录**，用户以为"我一个数据包都没装"。
// ② **「启用 / 禁用」= 改名**：`X.jar` ⇄ `X.jar.disabled`。这里唯一不可接受的失败是
// **撞车时静默盖掉一份**（目标名已经存在还照改，就等于用户手打的那一份没了）。
// ③ **「删除」得先问一句**，而问的时候按「取消」，盘上必须一个字节都没少。
//
// 顺带把 `loadServerList()` 那条映射也验了（它只有三行，但"文件不在盘上"被当成
// problem 递上去的话，用户看到的是「你的服务器列表读不动」而其实只是没有）。
//
// 全部离线：身份是这一节自己加的离线账号，一个请求都不出网。
// ---------------------------------------------------------------------------
void runInstanceFilesPagesTests();

// ---------------------------------------------------------------------------
// T21. 导出整合包：那颗「导出…」按下去之后到底发生了什么（离线，假接口）
//
// 【和 verify 的 V5 段怎么分工】
// V5 钉的是**数据层**：排除表、两张清单的正文、往返解析、zip 的条目名。那些全是纯函数，
// 在那边一条一条钉得住。这一节钉的是**接上去之后才存在**的东西，三件：
//
// ① 设置键 → 数据层那段**接线**。`Launcher/ModrinthApiBase` 在盘上，最后必须变成
// `POST http://127.0.0.1:<port>/v2/version_files`。中间隔着 LauncherSettings →
// syncCurseForgeSettings → VersionManager::m_modrinthApiBaseOverride 三段，任何一段
// 没接上，发出去的就是 api.modrinth.com —— 而**那一发在离线的机器上只会静静超时
// 15 秒**，症状是"导出一个包卡了半分钟、然后说没连上"，谁也不会往"设置没推下去"上想。
// 所以断言里既有"假服务器收到了那一发"，也有请求行里的那个路径。
// ② 那条**继承链**。`gameVersion` 与加载器那一行都是从盘上真实的版本 json 走出来的
// （实例的 json 指着 fabric-loader-…，那一份又指着 1.20.1）。这两格写错了的后果是
// "包看着是好的、别人装不回来"，界面上一个字都看不出来。
// 链末端那个版本号必须是一个**真版本号**：`instanceGameVersion()` 那一格在"在线清单
// 已经拿到"时以清单为准（`isKnownGameVersion()`），编一个 `1.99.9` 出来的话，这一节
// 的结果就取决于启动时那次清单请求到没到 —— 同一份夹具在两种模式下给出两种答案。
// ③ 门口那三个问句和它下面两条岔路：OptiFine（认得出、写不下去）、读不出原版、
// 反查没成要不要继续。它们的形状都是"弹一句、然后**盘上什么都不许留**" ——
// 留半个包比不导出更坏（用户会以为做出来了）。
//
// 假服务器一台罩两条路（`/v2/version_files` 与 `/v1/fingerprints`）：两个地址都是从
// **产品的设置键**推下去的，所以"推没推、推对了没"本身就是被验的东西。
// ---------------------------------------------------------------------------

/// 一个答"反查"那两发的假服务器（127.0.0.1、随机端口）。
///
/// NOTE: 这一台是**POST**，正文可能来得比头晚（`FakeResourceApi` 那边是 GET、没有正文，
/// 所以那边读到第一包就答了）—— 这里必须攒够 `Content-Length` 再回，否则会对着
/// 一份半截的请求体答话，而产品那边读到的是一个"人已经走了"的连接。
struct FakeLookupApi {
    QTcpServer server;
    QByteArray modrinthReply;   ///< `POST /v2/version_files` 的答复正文
    QByteArray modrinthVersionFilesUpdateReply;
    QByteArray curseForgeVersionListReply;
    /// 按项目号分叉的那一份（同一个手法、同一个理由，见 `modrinthVersionListByProject`）。
    QHash<QString, QByteArray> curseForgeVersionListByMod;
    /// `GET /v2/projects?ids=…` 的答复正文（**一个数组**）——T22 的图标地址只有这一发给
    /// 得出来（那条 `<id>/icon.png` 只是老项目的别名，见 ResourceCatalog 里那段实测）。
    QByteArray modrinthProjectsReply;
    /// `GET /v2/project/<id>/version` 的答复正文（**一个数组**）—— 详情页底下那一份版本列表
    /// 只有这一发给得出来（开页时"按实例的版本预选、把它的加载器排前面"这件事
    /// 要**有版本可排**才量得到，一列空表上什么都看不出来）。
    QByteArray modrinthVersionListReply;
    /// T31 补：**按项目号分叉**的那一份版本列表（项目号 → 答复正文）。
    ///
    /// 为什么非要它：T31 要在一个假服务器上同时给出"这个模组的版本"和"它那个前置的版本"
    /// —— 两份**不是同一份正文**（前置那一条自己不能又声明一遍前置，不然就递归了）。
    /// 只看 `modrinthVersionListReply` 的话，两发请求拿到的是同一个答案，于是要么模组那页
    /// 摆着前置的版本、要么前置解不开 —— 而屏幕上/日志里都看不出来是夹具的问题。
    /// 表里没有的项目号照旧走 `modrinthVersionListReply`（其余几节一个字都不受影响）。
    QHash<QString, QByteArray> modrinthVersionListByProject;
    QByteArray curseForgeReply; ///< `/v1/fingerprints` 的答复正文
    /// `POST /v1/mods` 的答复正文（T22 才用得上：图标反查要拿 `logo` 那两条地址）。
    QByteArray curseForgeModsReply;
    int modrinthStatus = 200;   ///< 想演"接口挂了"那一档时改成 500
    int modrinthProjectsStatus = 200;
    int modrinthVersionListStatus = 200;
    int modrinthVersionFilesUpdateStatus = 200;
    /// 为什么需要它：产品那一边"批量那一发"和"一项目一发那一拨"是**并着跑**的，谁先回来
    /// 由网络定。而有一个真出现过的坏法**只在某个次序下才露**：CurseForge 那一拨先回来 ⇒
    /// 整趟被提前收尾 ⇒ 批量那一发回来时已经没人等它了（收尾判据漏了 `pendingBatchKeys`，
    /// 见 `onUpdateCheckVersionsFinished()` 里那段）⇒ 报告里只剩单发那些。真机上真这么
    /// 发生过（用户那句「无法正常获取更新」）。
    ///
    /// 所以 T32 把这一发**按 400 毫秒压后**，让"CurseForge 先答"这件事从"看运气"变成
    /// **每次都是**。不压的话那一条断言是在赌：输的那几次照样绿，用户那边照样出错。
    int modrinthVersionFilesUpdateDelayMs = 0;
    int curseForgeStatus = 200;
    int curseForgeModsStatus = 200;
    int curseForgeVersionListStatus = 200;
    QStringList requests;       ///< 每一发的请求行（`GET`/`POST` 都在，target 带着查询串）
    QList<QByteArray> bodies;   ///< 每一发的正文（GET 是空的，仍然占一位，和 requests 对齐）
    /// **按请求目标**数的次数。断言不该看 `requests` 的下标 —— 单发和批量是并着跑的，
    /// 谁先回来由网络定（T32 那条延迟就是为了把"某个坏次序"变成每次都发生才存在的）。
    QHash<QString, int> routeHits;

    int count(const QString &route) const { return routeHits.value(route, 0); }

    bool start() {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this]() {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                socket->setParent(&server);
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                 [this, socket]() { answer(socket); });
            }
        });
        return server.listen(QHostAddress::LocalHost, 0);
    }

    /// 推进 `Launcher/ModrinthApiBase` 的那一个地址（产品会在它后面接 `/version_files`）。
    QString modrinthBase() const {
        return QStringLiteral("http://127.0.0.1:%1/v2").arg(server.serverPort());
    }
    /// 推进 `Launcher/CurseForgeApiBase` 的那一个（base 自带 `/v1`，见那个键的说明）。
    QString curseForgeBase() const {
        return QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());
    }

    /// 最后那一发的请求行（断言里看的就是它）。
    QString lastRequest() const { return requests.isEmpty() ? QString() : requests.last(); }

    void answer(QTcpSocket *socket) {
        if (socket->property("answered").toBool()) {
            return;
        }
        const QByteArray buffer = socket->property("buffer").toByteArray() + socket->readAll();
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            socket->setProperty("buffer", buffer);  // 头还没到齐
            return;
        }
        int contentLength = 0;
        QString target;
        QString verb;
        const QList<QByteArray> headLines = buffer.left(headerEnd).split('\n');
        for (const QByteArray &line : headLines) {
            const QByteArray trimmed = line.trimmed();
            // 请求行 `GET /v2/projects?ids=… HTTP/1.1` —— 头那一行（`Host: …`）第二段不以
            // `/` 开头，所以这一个判据认得出请求行。**GET 和 POST 都要认**：图标地址那一发
            // 是 GET（`VersionManager` 那两发是 POST；只认 POST 的话 GET 会被当成 404）。
            const QList<QByteArray> parts = trimmed.split(' ');
            if (parts.size() >= 2 && parts.at(1).startsWith('/')) {
                verb = QString::fromUtf8(parts.at(0));
                target = QString::fromUtf8(parts.at(1));
            } else if (trimmed.toLower().startsWith(QByteArrayLiteral("content-length:"))) {
                contentLength = trimmed.mid(15).trimmed().toInt();
            }
        }
        const QByteArray body = buffer.mid(headerEnd + 4);
        if (body.size() < contentLength) {
            socket->setProperty("buffer", buffer);  // 正文还没到齐
            return;
        }
        socket->setProperty("buffer", QByteArray());
        socket->setProperty("answered", true);
        requests << verb + QStringLiteral(" ") + target + QStringLiteral(" HTTP/1.1");
        bodies << body;

        const bool isModrinth = target.endsWith(QStringLiteral("/version_files"));
        // `POST /v2/version_files/update`（更新检测那一拨的批量那一发，见上面那段）。
        // 它**不是** `/version_files` 的尾巴对得上就完事 —— 那个后缀判断对它正好是 false，
        // 所以两件事分得开。
        const bool isModrinthUpdate = target.endsWith(QStringLiteral("/version_files/update"));
        // `GET {base}/mods/<项目号>/files`（CurseForge 的版本列表）。和下面那条 `/mods`
        // （POST，图标反查要的 `logo`）不是一回事。
        const bool isCurseForgeVersionList = target.contains(QStringLiteral("/mods/")) &&
                                             target.contains(QStringLiteral("/files"));
        // `GET /v2/projects?ids=…`（T22 的图标地址）。**带查询串**，所以按"含"认，
        // 不按后缀（`endsWith("/projects")` 对上的是 `/v2/projects`，后面还有 `?ids=`）。
        const bool isModrinthProjects = target.contains(QStringLiteral("/projects?"));
        // `GET /v2/project/<项目号>/version`（资源详情页底下那一份版本列表）。**单数**
        // `/project/`，和搜索那几条不撞；也**不是** `/version_files`（那条是 POST，见上）。
        // 尾巴那一截可能带查询串（`?game_versions=…`），所以按"含 + 尾"两样一起认。
        const bool isModrinthVersionList = target.contains(QStringLiteral("/project/")) &&
                                           target.contains(QStringLiteral("/version"));
        const bool isCurseForge = target.endsWith(QStringLiteral("/fingerprints"));
        // `POST /v1/mods`（T22 的图标反查用它换 `logo` 那两条地址）。
        const bool isCurseForgeMods = target.endsWith(QStringLiteral("/mods"));
        // 这一发认不认（认不出来一律 404 —— 那台服务器上"没有这个路由"就是这个样子）。
        const bool known = isModrinth || isModrinthUpdate || isModrinthProjects ||
                           isModrinthVersionList || isCurseForge || isCurseForgeMods ||
                           isCurseForgeVersionList;
        if (isModrinth) {
            ++routeHits[QStringLiteral("modrinth-version-files")];
        } else if (isModrinthUpdate) {
            ++routeHits[QStringLiteral("modrinth-version-files-update")];
        } else if (isModrinthProjects) {
            ++routeHits[QStringLiteral("modrinth-projects")];
        } else if (isModrinthVersionList) {
            ++routeHits[QStringLiteral("modrinth-project-version")];
        } else if (isCurseForgeVersionList) {
            ++routeHits[QStringLiteral("curseforge-mod-files")];
        } else if (isCurseForge) {
            ++routeHits[QStringLiteral("curseforge-fingerprints")];
        } else if (isCurseForgeMods) {
            ++routeHits[QStringLiteral("curseforge-mods")];
        }
        int status = 404;
        if (known) {
            status = isModrinthUpdate
                         ? modrinthVersionFilesUpdateStatus
                         : (isModrinth
                                ? modrinthStatus
                                : (isModrinthProjects
                                       ? modrinthProjectsStatus
                                       : (isModrinthVersionList
                                              ? modrinthVersionListStatus
                                              : (isCurseForgeVersionList
                                                     ? curseForgeVersionListStatus
                                                     : (isCurseForge ? curseForgeStatus
                                                                     : curseForgeModsStatus)))));
        }
        // 版本列表那一发**按项目号挑一份**（见 `modrinthVersionListByProject` 那段）：
        // 地址长这样 `/v2/project/<项目号>/version?game_versions=…`，所以夹在
        // `/project/` 和下一个 `/` 中间那一截就是项目号。表里没有就退回公共那一份。
        QByteArray versionListPayload = modrinthVersionListReply;
        if (isModrinthVersionList) {
            const int at = target.indexOf(QStringLiteral("/project/"));
            if (at >= 0) {
                const QString rest = target.mid(at + 9);
                const QString projectId = rest.left(rest.indexOf(QLatin1Char('/')));
                if (modrinthVersionListByProject.contains(projectId)) {
                    versionListPayload = modrinthVersionListByProject.value(projectId);
                }
            }
        }
        // CurseForge 那份版本列表**也按项目号挑一份**（同一个手法、同一个理由，见上面
        // Modrinth 那一段）：地址长这样 `…/mods/<项目号>/files?…`，所以夹在 `/mods/`
        // 和下一个 `/` 中间那一截就是项目号。
        QByteArray curseForgeVersionListPayload = curseForgeVersionListReply;
        if (isCurseForgeVersionList) {
            const int at = target.indexOf(QStringLiteral("/mods/"));
            if (at >= 0) {
                const QString rest = target.mid(at + 6);
                const QString modId = rest.left(rest.indexOf(QLatin1Char('/')));
                if (curseForgeVersionListByMod.contains(modId)) {
                    curseForgeVersionListPayload = curseForgeVersionListByMod.value(modId);
                }
            }
        }
        QByteArray payload;
        if (isModrinthUpdate) {
            payload = modrinthVersionFilesUpdateReply;
        } else if (isModrinth) {
            payload = modrinthReply;
        } else if (isModrinthProjects) {
            payload = modrinthProjectsReply;
        } else if (isModrinthVersionList) {
            payload = versionListPayload;
        } else if (isCurseForgeVersionList) {
            payload = curseForgeVersionListPayload;
        } else if (isCurseForge) {
            payload = curseForgeReply;
        } else if (isCurseForgeMods) {
            payload = curseForgeModsReply;
        }
        const QByteArray response =
            QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + ' ' +
            (status == 200 ? QByteArrayLiteral("OK") : QByteArrayLiteral("Error")) +
            QByteArrayLiteral("\r\nContent-Type: application/json\r\nContent-Length: ") +
            QByteArray::number(payload.size()) +
            QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + payload;
        if (isModrinthUpdate && modrinthVersionFilesUpdateDelayMs > 0) {
            QPointer<QTcpSocket> guard(socket);
            QTimer::singleShot(modrinthVersionFilesUpdateDelayMs, &server, [guard, response]() {
                if (guard.isNull()) {
                    return;
                }
                guard->write(response);
                guard->flush();
                guard->disconnectFromHost();
            });
            return;
        }
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }
};

void runModpackExportTests();

// ---------------------------------------------------------------------------
// T22. 「文件管理」那几页的图标：按文件哈希反查出"这是哪个资源"（离线，假接口）
//
// 要求：「模组和资源包等都有一个图标，把图标显示出来」。这一节钉的是
// **反查那一半**：盘上那个文件名**不是身份**（用户改过名、各家起名不一样、同一个资源
// 两版的名字可以完全不同），能把它和一个资源对上的只有**文件内容** —— sha1 问
// Modrinth（`POST /v2/version_files`）、指纹问 CurseForge（`POST /v1/fingerprints`），
// 再拿项目号去 `POST /v1/mods` 换 `logo` 那两条地址。
//
// 这一节唯一值钱的地方是**发出去的正文是不是真算出来的**：哈希算错了（少读一趟、
// 指纹初值不对、拿文件名去凑），症状是"图标永远是那张占位拼图"，不报任何错、屏幕上
// 也看不出来。所以假服务器**按收到的正文答话**，而断言里对着的是 `ModpackExport::
// hashFile()` 独立算出来的那两份值（它是导出整合包那条路在用的同一份实现）。
//
// 【为什么量到"身份"为止，不量屏幕上那张图】
//
// 【★ 补的那一发：图标地址】
//
// 要求「模组的图标没有正确加载」—— 根子是"Modrinth 那两支反查都给不出图标地址，
// 于是退回 `Hit::iconPngUrl()` 自己拼的 `<id>/icon.png`"，而那条只是**老项目的别名**
// （实测 20 个模组里 11 个回 404）。真正的地址在带哈希的文件名底下，只有接口说得出来，
// 所以现在认出来之后**多发一发** `GET /v2/projects?ids=…`（④b 量它的形状与正文，
// ⑤ 量它自己挂掉时"身份照旧留着、而 problem 不许被它带歪"）。
// ---------------------------------------------------------------------------

void runInstanceIconLookupTests();

// ---------------------------------------------------------------------------
// T23. 服务器那点东西落盘（`cache/servers/<地址 sha1>.json`）
//
// 要求：「测出来的 motd 和图案保留在缓存里」。这一节钉的是**那几个字节
// 来回一趟不走样** —— 一次测速要真的连上去（每台最多几秒超时），服务器列表又是"每次
// 打开实例设置都会看见"的一页；缓存读歪了的症状是"那一页上一次明明问到过、这次又是
// 一片空白"，不报任何错。
//
// 【最值钱的一条：`-1` 那个哨兵】
//
// 【为什么在这里而不是 verify】
//
// `ServerStatusCache.cpp` 只有这一份可执行文件链进来了（它读 AppPaths，而 AppPaths 要
// 一个进程级的数据根目录）。这一节把数据根目录换到一个临时目录，跑完换回来。
// ---------------------------------------------------------------------------

void runServerStatusCacheTests();

// ---------------------------------------------------------------------------
// ZG. 离线账号那张脸：算得出来就用**游戏会给的那一张**，算不出来才退回随机那张
//
// 这一节要的是**真主窗口的那条路**。verify 的 ZF 段已经把"名字 → 哪一张图"立住了
// （纯函数 + 真 JAR），这里验的是它有没有接到屏幕上：选中哪个版本、右栏画的是哪张、
// 底栏那句话说的是哪一档、换版本时会不会跟着换。
//
// 四个假版本各管一条路（都在临时根目录里，跑完随根目录一起消失）：
// 1.20.1-defskin 新规矩那条在 → 算得出来
// 1.18.2-defskin 只有旧版布局那条在 → 换一套布局也算得出来
// 1.19.9-noskin 两条都不在 → **常态**：退回随机那张
// 1.7.10-broken jar 根本不是 zip（第三方安装器的薄壳那种）→ 记一行日志再退回随机
// ---------------------------------------------------------------------------
void runOfflineDefaultSkinTests();

// ---------------------------------------------------------------------------
// AH. 底栏那两块上拉面板：左「正在跑的实例」/ 右「最近登录的三个账户」
// ---------------------------------------------------------------------------
// 这一节的判据全部来自用户 那两句话：
// 「一行一个会话，有时长是好主意，做进去」—— 左面板**一份会话一行**（不是"一个实例
// 一行"：同一个实例开两份就是两行），行首那个方块按这条实例的分类给，行尾两颗按钮
// （电源 = 关掉**这一份**、齿轮 = 进**这一行那个实例**的设置），第二行写它跑了多久；
// 「最后登录的三个账户就拿缓存放（包含离线和正版，总共三个）」—— 右面板读的是账号列表
// 那条缓存（AccountStore::accounts()），按 saved_at 倒序取三个，离线那颗写「选择」、
// 正版写「登录」；而「登录」的意思比"切过去"多一步：**看一眼盘上那张令牌够不够用**
// （剩不到 6 小时就顺手换一张，见 AuthManager::tokenRefreshWindowSecs）。
//
// 【这一节为什么要真起会话，以及"假 java"是什么】
//
// 「一份会话一行」这句话只有真的有两份会话在跑才验得了。真的 JVM 在这儿是个又贵又脆的
// 依赖（机器上未必有、起来了还得等它写日志），所以 javaPath 指到**启动器自己这个 exe**，
// argv 里塞一个 `session-holder`：main() 最前面认这个词就睡够四分钟（见那里的说明）。
// 于是"有一个进程真的在跑"和"跑的是什么"解耦了，而 QProcess 那条路本身是真跑的 ——
// 真起来、真 kill、handleProcessFinished 真收会话。
//
// NOTE: 正因为 holder 就是 mw_check.exe 自己，**这一节的每一份会话都必须停掉**：活着的
// holder 会把这个 exe 文件锁住，下一次构建直接写不进去（而且一锁四分钟）。所以
// 下面每一段都成对（起了几份就停几份），收尾那一段还会再数一遍。
//
// 全程**不联网**：三份会话是本地进程；右面板那三个账号里，两个的令牌够用（Bob 那张到
// 2999 年）、一个的凭据根本换不出新令牌（Carol 那份微软 payload 里**没有 refresh_token**，
// refreshMinecraftToken() 第一道判据就拦下了，一个包都不发）。真要验那条刷新路走到哪儿，
// 那是 mw_check 的一次性探针 auth-live，不是这一节。
void runBottomBarPanelTests();

/// AJ. 启动前那两条提醒（设置页「启动提醒」那一组）。
///
/// 要求：「设置里加两条 在10s内用同一用户启动同一实例时弹出提示：已经有
/// 一个相同的运行中游戏进程了！（小字写 可以在设置中关闭此提示）下方 取消/仍要启动 在启动
/// 同一个实例时（在前面选择仍要启动的情况下/不同账户）弹出进入单人世界可能会丢失进度的
/// 提示…也写明可以关掉，下方 取消/仍要启动」。
///
/// 这一节量三层：
/// · **接线**：点「启动游戏」那一下真的会走到这两问（`onStartButtonClicked()` 里那处调用）；
/// · **拦得住 / 放得行**：选「取消」⇒ 一份新的游戏进程都没起来；选「仍要启动」⇒ 那条路
/// 照旧走到底（这里用**假 java** 真起一份进程来证，和 AG 那一节同一套办法 —— 只看
/// "框收掉了没有"是分不出"放行"和"静静地把启动掐掉"的）；
/// · **两条各自的判据**：十秒窗口的两侧、同不同一个身份、同不同一个实例，以及
/// "① 之后接着问 ②"那个配对（要求要的就是"前后两个框"，不是二选一）。
///
/// 全程离线：身份是一个**离线账号**（不碰令牌、一个请求都不发），实例指着沙盒里的假 java。
void runLaunchGuardTests();

// ---------------------------------------------------------------------------
// T24. 实例设置里**点一条行尾的「详情」** ⇒ 开那个资源自己的那一页（离线，假接口）
//
// 要求：「除了模组以外的几个都是点击模组名称自动进入下载界面（前提是
// 匹配上了，模组的待会再说）」； 又把它挪了个地方：「把点击条目进入对应详情页的
// 功能变成一个详情按钮，放到开关和删除中间，单击改为选中」。这一节走的就是那条路本身，
// 而且**一整条都走**：
//
// 反查那一趟真跑（后台线程 + 假接口）→ 行尾那颗「详情」真的从灰的变亮 → 真的按它一下
// → 实例设置那一页收起来 → 资源详情页真的开出来、标题就是接口给的那个名字。
//
// NOTE: 名字那一格**不是链接**（单击 = 选中这一行）；"这一条是哪个资源"
// 整件事就落在那颗按钮上，所以这一节量的就是那颗按钮。
//
// 【为什么非要端到端，不满足于"两个部件各自对"】
// 这一条路上有**三处**只有接起来才存在的判据，任何一处错了，界面上都是"点一下没反应"：
// ① 身份表（`m_instanceFileIdentities`）有没有被那一趟填上、钥匙（路径 + 大小 + 时间）
// 对不对得上 —— 对不上就查不到那一条是哪个资源；
// ② 收页那一下**必须是 dismiss()**（那一页正卡在 `execPage()` 的嵌套事件循环里，
// `closeOverlay()` 一个信号都不发，那个循环永远不返回）；
// ③ 开新页那一下**必须排到下一轮事件循环**（这一刻还在那个嵌套循环的调用栈里，
// `openResourceDetailPage()` 开头那条 `overlay->isOpen()` 会把这次点击当场吃掉）。
//
// 【模组那一页是**故意**点不动的】
// 用户那句里后半截是「模组的待会再说」⇒ 判据落在 `filesNameOpensResource()` 一处。这一节
// 用一个**已经认出来了的**模组来量它：它的项目号确实被问回来了（下一发里带着它）、
// 图标照旧会出来，可它的名字既没有链接的样子、点下去也什么都不发生。
// 拿一个"本来就没认出来"的文件来量这件事是量不出来的（那种情况下名字点不动是应该的）。

void runInstanceFileResourceTests();

// ---------------------------------------------------------------------------
// T25. 「从实例设置里点『下载新模组』」那条路
//
// 要求：「点击下载新模组/资源包/...时左上角保留一个返回键，返回到实例设置里面，和
// 资源包详情啥的一致」，以及「在设置里加一个选项 …… 勾选->下载新模组就自动放进当前实例
// 文件夹 …… 不勾选->不论是在哪里进入的下载页都弹explorer选择保存位置」。
//
// 这一节钉的是**接线**：界面层那两颗控件长什么样在 ui_check 里量（Z3c / C 两节），
// 「点下去到底发生了什么事」只有把真的 MainWindow 拿起来才量得到 —— 中间任何一段断了，
// 屏幕上的样子都是"按了那颗键没反应"，而那既不像崩溃也不像报错，只是**没动**。
void runDownloadFromInstanceTests();

/// T26. 「Java 管理」那一页：四行 + 两颗动作键 + 那颗「选用」（离线）
///
/// 要求：「把设置里 java 版本换成一个单独的按钮 java 管理…然后里面就直接是
/// Java25：C:\... 重新下载（按钮）/ Java21: ... / Java17：缺失 下载 / Java 8：C:\ 重新下载
/// / 下载所有缺少的Java（按钮） 检测已有java（按钮） 左上角放一个返回键」。
///
/// 这一节**一颗「下载 / 重新下载」都不按**：那四颗按下去是真的往 Adoptium 发请求、几百 MB
/// 同步下完才返回（`GlobalJavaSettings::downloadJavaFromMojang`）。这一节量的是"这一页长
/// 什么样、按哪一颗会走到哪儿"—— 下载那一趟是它自己的事，在这儿按下去这一跑就既不离线、
/// 也不再是几十秒。「检测已有 Java」和「选用」两颗**可以**按：前者只扫盘，后者只写设置。
void runJavaManageTests();

// =========================================================================
// T27. 「复制实例」和「存档」那一页那三条线
//
// 要求：「实例设置的概览里面加一条复制实例，点击以后让用户选择复制到的文件夹，然后
// 给定复制后的实例名称 / 在实例设置里面存档那一栏每个世界都显示存档大小和最后游玩时间，
// 右边画一支笔意为编辑，和基岩版一样，顶端加个导入世界按钮和批量编辑按钮（这个先做占位）」。
//
// 这一节验的是**接线**：那颗按钮在不在、点下去真的动了盘没有、那一页发出来的三发信号是不是
// 接到了 MainWindow 上（接到之后**屏幕上真的出现东西**）。判据本身（拷树的规矩、重名的
// 挡法、名字的校验）在 verify 的 ZJ 节里，那一节是纯值的。
//
// NOTE: 两颗原生选择框（"复制到哪个游戏文件夹"、"要导入哪个世界"）在测试里**驱动不了**
// —— 那一步在选中之前根本进不去（一个模态的原生窗口），所以：
// · 「复制实例」走**动手的那一半** `copyInstanceTo()`（收三个字符串，见 .hpp）；
// · 「导入世界」只验它动手的那一半 `importWorldInto()`，**那一颗按钮本身不点**
// （点了就卡在原生框上）。这一点和产品里另外四处 QFileDialog 是同一个待遇。
// =========================================================================
void runCopyAndSavePageTests();

// =========================================================================
// T44. 管理页右侧那张实例树：**打开时不许停在最底下**
//
// 症状是"每次进管理页，右边那列已经滚到底了"。真因不是选中项挑错：syncTreeToCombo()
// 选的是当前实例，Qt 会滚到它那儿；开机那一趟树还藏着，这一滚被攒到第一次显示、
// 排完版才补（藏起来时 range 还是 0，挡在 setCurrentItem() 周围的 scrollTo() 拦不住）。
// 所以修法只能是"记下重建前的位置、排版结束后放回去"，这一节就钉那个机制的三个面：
// Qt 的行为还在不在、放回去之后选中项丢没丢、树露着的那一趟是不是当场放回。
// =========================================================================
void runInstanceTreeScrollTests();

// =========================================================================
// T28. 文件管理那几页顶上那条搜索 + 按钮下面那栏状态里的「可更新」
//
// 要求：「模组等的下载新/刷新/打开文件夹的**上方**加一个搜索，三个按钮的**下方**一个
// 选择 全部/启用/禁用/可更新」。
//
// 【和 ui_check 的 U10P 怎么分工】
// U10P 量的是**屏幕上那一半**：两排控件在不在、摆在按钮的哪一边（按 y 坐标量）、筛得对不对、
// 「筛完一条不剩」和「目录本来就是空的」是不是两个状态。这一节量的是**那一格「可更新」的
// 数据从哪来** —— MainWindow 那趟检测查出来的结果，除了弹一份报告，还得**留下来**并喂给那几页：
// * 不留：关掉设置页再进来，那一格永远挂着"还没有查过更新"，而那一趟刚刚才跑完；
// * 不喂：筛出来是空的，用户以为"这个实例干净得很"。
// 两样在屏幕上的样子都**不像坏**（不崩、不报错、连日志都正常），所以只有把真的 MainWindow
// 立起来才量得到 —— 而这也正是"检测结果只活在那一个框里"这件事唯一露馅的地方。
void runInstanceUpdateFilterTests();

// =========================================================================
// T32. 更新检测：**一个文件只查一家** + 两家都认出来时退回问 CurseForge
//
// 两条都坏的时候屏幕上**不像坏**：弹一份报告、里头几条「说不清」，和"网络不好"一个样子 ——
// 而 429 那条真因（"一次问得太多"）更是一个字都露不出来。所以只有把真的 MainWindow 立起来、
// 让那一趟跑完再读**它发出去的那几发请求**，才量得到。
//
// 夹具：两个模组文件，一个**两家都认得出**（Modrinth 有哈希表 + CurseForge 有指纹），
// 一个**只有 CurseForge 认得出**。批量那一发对第一个故意**什么都不答**（`{}`）——
// 这正是真机上"这个哈希在 Modrinth 的表里没有"的样子。
// =========================================================================
void runUpdateCheckSourceFallbackTests();

// =========================================================================
// T32b. 同一个项目底下的**两个文件**，批量那一发只答得出其中一个
//
// 夹具：两个**不同内容**的文件（sha1 / 指纹都不同），反查后都指向**同一个 CurseForge
// 项目**；批量那一发只给 A 那个 sha1 答案（B 那个哈希"这一家没有"），CurseForge 的
// 版本列表给的正是 B 那一份 ⇒ 两条都该有结论，而且**各只有一条**。
// =========================================================================
void runUpdateCheckSameProjectFallbackTests();

/// 按 objectName 找**最新那一代**控件（同一个名字下面同时躺着好几代的时候取最后一个）。
///
/// 为什么需要它：更新中心那一页重喂走的是 `deleteLater()`，而**从嵌套事件循环里发出的
/// 延迟删除要等那一层退出去才真的派发**（Qt 的规矩：延迟删除事件带着发出时的循环层号，
/// 只有回到那一层才放它走）。所以这一页开着的整段时间里，同一个 objectName 底下会同时
/// 躺着好几代控件 —— 取第一个读到的是上一代（还写着「可更新到 2.0」），断言会红在一个
/// 假象上。新控件是后 append 进父对象那张孩子表的，所以**最后一个**就是最新那一代。
template <typename T> T *newestByObjectName(QObject *parent, const QString &name) {
    const QList<T *> all = parent->findChildren<T *>(name);
    return all.isEmpty() ? nullptr : all.constLast();
}

// =========================================================================
// T38. 「更新中心」上那几颗动作
//
// · 「忽略这一版」→ 账本（`<数据目录>/wtfml.updates`）里记下"要换上的**那一版**的 id"，
// 那一行当场写成「已忽略这一版（2.0）」、那一颗翻成「不再忽略」；
// · 「不再忽略」/「不再固定」→ 那一笔记录清掉、那一行回到「可更新到 2.0」；
// · 「固定这一版」→ 记的是**盘上那一份的内容 sha1**（不是版本号），那一行写「已固定」；
// · 那一行自己的「更新」→ **先摆回滚点**再动手。这一批我们**故意让它失败**（那一版给的
// 地址指向假服务器上一个不存在的路径）⇒ 正好量到"失败了能退回去"这条承诺：失败框里
// **逐条**说着每个资源的结果、更新中心自己又开回来了、底下那颗「撤销上一次更新」露着；
// · 「撤销上一次更新」→ 问一句 → 把文件退回更新前那一份 → **用掉**那份回滚点。
//
// 这几段任何一处断了，屏幕上的样子都是"按了没反应"或者"那句话没变" —— 既不像崩溃也不像
// 报错。只有把真的 MainWindow 立起来、真按那几颗 chip、真读盘才量得到。
//
// 走法上有一处**不是随手写的**：这一页从开到关一直卡在 `OverlayHost::execPage()` 那一层
// 嵌套事件循环里，而上面那些动作全在**这一层里面**发生（"收页"是 reject、下一件事由
// MainWindow 排到下一轮）。所以这里用一台 10 毫秒的自走定时器按阶段往下推，每一阶段**先
// 等到该看到的样子再动手** —— 谁先谁后由产品那边决定，不由测试决定（"点完就往下走"的
// 量法会读到上一代控件，见 `newestByObjectName()` 那段）。外面那一层只 `pumpUntil` 到
// 最后一阶段为止（和 T28/T32 是同一个形状）。
// =========================================================================
void runUpdateCenterActionTests();

void runThemeImageRowTests();

/// T39. 设置页「主题」那一节：控件 → 主题规格 → 盘。
///
/// 两处**故意不点**（底下是覆盖层 / 原生框，测试里点下去就是卡住）：
/// · 那三块色板（主题色 + 四个语义色）—— 底下是 `InlineMessage::pickColor()`；
/// · 「导出 / 导入」—— 底下是 `QFileDialog`。
/// 「挑完一个色之后」那一态照产品那两步搭出来：直接改规格 + 按名字调那一发重画
/// （产品里"挑完色"走的正是 `applyThemeSpec()` 那条路）。
void runThemeControlsTests();

/// 把一个本地文件"拖"到某个控件上（真发那一串事件：`DragEnter` 然后 `Drop`）。
///
/// 走的是**真的那几发事件**（`QApplication::sendEvent`），不是直接调那个槽：这条路上要
/// 验的恰恰有"这一页/这个窗到底收不收拖放"（`acceptDrops()` 那一格），绕过事件派发就
/// 等于把准入判据一起绕过去了。
///
/// 两发**必须成对**发，这是 Qt 自己的规矩，不是这里的规定：`QApplication::notify()` 里
/// `Drop`/`DragMove`/`DragLeave` 那一段**根本不看接收者是谁** —— 它把事件交给
/// `QDragManager::self()->currentTarget()`，而那个 `currentTarget` **只有**被接受的那一发
/// `DragEnter` 才会设上（见 `case QEvent::DragEnter` 里那句 `setCurrentTarget(w)`）；一个
/// 没有 currentTarget 的 `Drop` 会被**直接扔掉**（那一段开头的 `else break;`）。
/// 于是"只发 Drop"这件事看起来像发了，其实什么都没发生 —— 更坏的情况是**跑去别人身上**：
/// 上一发被接受的 `DragEnter` 还记着它，这一发 Drop 就落到**上一个控件**头上了。
/// 实测踩到过：T30.3 的 Drop 被 T30.1 留下的 currentTarget 收走，落进
/// `MainWindow::dropEvent`（症状是"这一页一个字都没收到，主窗那张「装到哪个实例」的
/// 列表倒弹出来了"）。
///
/// NOTE: 返回值是 `isAccepted()` —— 收没收，由控件自己说（`dragEnterEvent`/`dropEvent`
/// 里那一句 `acceptProposedAction()`），不是我们猜的。准入那一发就没过的话，真机上
/// 也不会有 Drop 这一发，所以直接返回 false（不硬塞一发进去）。
static bool dropFilesOn(QWidget *target, const QStringList &paths, bool enterOnly = false) {
    if (target == nullptr) {
        return false;
    }
    QMimeData mime;
    QList<QUrl> urls;
    for (const QString &path : paths) {
        urls.append(QUrl::fromLocalFile(path));
    }
    mime.setUrls(urls);
    const QPoint pos(target->width() / 2, target->height() / 2);
    const QPointF posF(pos);
    QDragEnterEvent enter(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    if (enterOnly || !enter.isAccepted()) {
        return enter.isAccepted();
    }
    QDropEvent event(posF, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
    return event.isAccepted();
}

/// T30. 拖进来的文件：认类型 → 问不问 → 落到哪个目录 → 哪个实例。
///
/// 要求：「打开某个实例设置的时候如果拖入.jar/.zip之类的文件，检测是整合包还是资源包
/// 等，如果是模组且正好在模组设置那一页，那就直接塞进去，不是对应页面就弹窗提示：将在此
/// 实例中安装 资源包/...（判断出的文件类型），是否继续；…在一些其它的地方拖入模组等时
/// 选择要安装在的目标实例」。
///
/// 这一节量的是**接线与落地**（认类型那半在 verify 的 V5z 里，喂的是真压缩包）：
/// · 主窗自己收不收拖放；
/// · 「正好在那一页」⇒ 一句话不问、文件真的进了 `mods/`；
/// · 不在那一页 ⇒ 弹的是**那一句原话**，答"不装"⇒ 盘上什么都不多；
/// · 认不出来的东西 ⇒ 一句人话，别静默；
/// · 同名文件 ⇒ 问一句，答"不盖"⇒ 旧那份一个字节没动；
/// · 没开实例设置时 ⇒ 问"装到哪个实例"，而"一行都没选中"**不算**选了第一个。
///
/// **一个原生框都不点**：整合包那一路第一步是 `QFileDialog::getExistingDirectory`
/// （选游戏文件夹），测试里点下去就是卡死 —— 所以这一节不拖整合包（那两条路
/// ——"离开实例设置"和"选文件夹"—— 在别的节里量过它们的零件）。
void runDroppedFileTests();

// ---------------------------------------------------------------------------
// T31. 前置依赖弹窗 + 从实例点进详情页再下载（落进**那一个**实例）
// + 批量启用 / 禁用 / 删除真改盘
//
// 要求（这一节两件事的出处）：
// · 「点击下载模组时检测当前实例里面是否存在所有的前置，如果有缺失的前置，弹出此模组
// 缺失必要的前置：（模组名称，一行一个…）」+「弹窗里灰字写可以在设置里面关掉弹窗」；
// · 「在模组/资源包等的设置里面把点击条目进入对应详情页的功能变成一个详情按钮…在选中
// 任意数量模组时在列表下方的位置显示几个按钮：更新 禁用 启用 全选 反选 取消 删除」。
//
// 这一节量的是**屏幕上的字和盘上的字节**，不是"接线接上了没有"：
// · 弹窗那三样（标题 / 正文 / 灰字）一律取判据层 `ModDependencies::*Text()` 逐字比，
// 测试里不另写一句 —— 写一句就等于把"唯一出处"这条规矩在测试里破一个口子；
// · 「关掉之后什么都不装」的判据是**那条前置查询压根没发**（`/v2/projects?ids=DEPPROJB`）
// + **前置那份文件没被要求下**（`/dep-b.jar`）—— 那是"要不要一起下"那条路上
// 唯一一次、也是第一次问出这个问题的地方，它没发出去就说明整段被跳过了；
// · 「落进那一个实例」的判据是产品自己写的那行日志里带着**这个实例的 mods 目录**。
//
// 【为什么整条路要点两次「详情」】
//
// 这一节的两趟（开关开着 / 关掉）走的是**同一条**用户路（实例设置 → 模组 → 详情 →
// 点某一版），所以写成同一个 lambda 跑两遍。写成两份的话，第二趟迟早会和第一趟长歪。
// ---------------------------------------------------------------------------

/// 在树的行体上按一下（按下 + 松开两发，**不**顺手泵事件）。
///
/// 和 ZJ/T24 那几段里的 `clickRow` 是同一个手法（产品认"行上点一下"走的是
/// `RowActivation` 那层事件过滤器，所以两发都要发）。**不泵事件**是有意的：产品的
/// "开新页"是排到下一轮才落地的，泵的那一下交给调用方，不然顺序就反了。
static void clickDetailTreeRow(QTreeWidget *tree, const QPoint &pos) {
    QWidget *viewport = tree->viewport();
    const QPointF local(pos);
    const QPointF global(viewport->mapToGlobal(pos));
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(viewport, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(viewport, &release);
}

void runDependencyAndBatchTests();

// =========================================================================
// T41. 服务端「跑起来 / 停下来」（M4-3）
//
// 那一层叫 `ServerRun::ServerProcess` —— 它管一条真进程的整条命：起、读它的 stdout、
// 往它的 stdin 写、正常停、宽限到点强杀。这条路上任何一处断了，屏幕上的样子都是
// 「点了没反应」或者「那行字一直是启动中」，既不像崩溃也不像报错。
//
// 这一节把"服务端"换成了**这个 exe 自己**（`--fake-server`，见 `main()` 最前面那一支）：
// 真服务端要几个 GB 的下载 + 联网，在这个工程里是个又贵又脆的依赖；而 `ServerProcess`
// 本来就不认识 java —— 它只认识"一个会吐行、会读行的进程"。所以这里量得到的东西
// （状态怎么走、日志怎么切行、`stop` 写进去了没有、宽限到点强杀）和在真机上量到的
// 是同一件事。
//
// 这一节**不造 MainWindow、不碰数据根目录**：这一层只看它拿到的那几个入参（工作目录
// 只是 `QProcess` 的 cwd），盘上什么都不写。所以它可以排在哪儿都行 —— 放在最后只是
// 图个整齐。
//
// 判据那一半（`-Xmx` 插到第几位、`Done (` 认哪一行、退出码算哪一档状态、宽限时间那张表）
// 在 verify 的 ZZ 一节里钉死，这一节不重复量 —— 这里量的是**它们真的被用上了**。
// =========================================================================
void runServerProcessTests();

// =========================================================================
// T42. 「服务端设置」那一整页（M4-4）
//
// 这一节量的是**只能在这一层说得清**的事：那一页经真 `MainWindow` 开到内容区上之后，
// 按下去的那几发到底有没有落到盘上 —— 表单改一格 → 保存 → 盘上那份 `server.properties`
// 只动了那一行（别的行一个字节都不碰）；切到「原始文本」时表单的补丁**已经合进那份文本**
// （两页看到的不是两份东西）；文本框里改一行 → 保存 → 整份写回去；「立即备份」→ 盘上真
// 多一个 zip；体检单里那些条目是 `ServerCheck` 现算的。
void runServerSettingsPageTests();

// =========================================================================
// T43. 关窗那一刻：worker 线程 / 子进程 / 还在飞的下载各自怎么收场
//
// 那条问的是「关掉启动器的时候，后台那些活分别怎么收尾」。这一段钉的就是它们：
// 这几条路各自都能把"关个窗"变成"崩一下"，或者"把用户的东西弄丢"：
//
// · **worker 线程**：`MainWindow` 手上那几趟后台活（体检 / 备份 / 图标反查 / 检查更新）
// 都是 `QThread::create()` + `setParent(this)` 出来的。线程还在跑、窗口先散 ⇒ Qt 直接
// `qFatal("QThread: Destroyed while thread is still running")`，也就是 abort。
// 体检和备份那两趟在析构里会被等（那两段写着 `~MainWindow()` 里），**图标反查和
// 检查更新那两趟没人等** —— T43.3 给它们摆了钉子。
// · **子进程（游戏）**：关窗**不许杀**它（用户那句「关掉启动器游戏继续跑」）。窗口一关，
// `LauncherManager` 就跟着析构，而 `QProcess` 的析构里第一件事就是 `kill()` ——
// 所以那一问走完必须先 `detachRunningGame()` 把进程**交出去**。T43.2 量到的是"窗口连
// 对象都拆干净了，那个进程还活在系统里"。
// · **还在飞的下载**：那批 `QNetworkReply` 随窗口一起散，不许崩（T43.4）。
//
// 全程离线：唯一的"网络"是本机两个**接了不答话**的假端口（下面 `StallingApi` 那个跑在
// 自己的线程里）—— 不然那一趟一秒内就跑完，"关窗时它还在跑"这个现场根本摆不出来。
// =========================================================================

/// 一台「接上就不答话」的假接口，**跑在它自己的线程里**。
///
/// 用途只有一个：让 `InstanceIconLookup` 那一趟卡在网络那一发上，好让关窗那一刻它**真的
/// 还在跑**。为什么非它不可：官方地址在离线的机器上是"连不上、立刻失败"（`HttpClient`
/// 那一发几百毫秒就回来），那一趟一秒内就跑完，钉子成了空钉子；而本机一个**听着但不回话**
/// 的端口，客户端连得上、然后就一直等响应，一直到 `HttpClient` 的 15 秒。
///
/// 必须**另起线程**：`HttpClient::send()` 是同步的（自己转自己的事件循环），而 T43.3 那
/// 一段**主线程正卡在 `~MainWindow()` 里等它**、根本转不动事件循环 —— 摆在主线程这边的
/// `QTimer` 那一下**永远响不了**，"过一会儿把连接放掉"就没机会发生（和 ServerPing 一样：
/// "假服务器必须另起线程"）。这一条线程用的是 `msleep`，不靠任何事件循环。
class StallingApi : public QThread {
public:
    /// @param holdMs 收到一发连接之后**吊多久**再把它放掉。
    explicit StallingApi(int holdMs) : m_holdMs(holdMs) {}

    /// 起来，并**等到端口拿到手**（端口要写进设置键、还得赶在造窗口之前，所以必须等到）。
    bool startAndWait(int timeoutMs = 5000) {
        start();
        return m_ready.tryAcquire(1, timeoutMs);
    }
    quint16 port() const { return m_port; }
    /// 写进 `Launcher/ModrinthApiBase` 的那一份（产品会在它后面接 `/version_files` 那条尾巴）。
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_port); }
    /// 收摊：**必须**在对象出作用域之前等它自己退出来（`QThread` 析构时还在跑，照样 abort）。
    void finish() { wait(); }

protected:
    void run() override {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) {
            m_ready.release();
            return;
        }
        // 端口这一位就绪了 —— 信号量本身就管好了两条线程之间的先后，写完了再放。
        m_port = server.serverPort();
        m_ready.release();
        // 等第一发连接上来（最多 10 秒：连不上就是夹具坏了，别把整趟吊死在这儿）。
        if (!server.waitForNewConnection(10000)) {
            return;
        }
        QTcpSocket *socket = server.nextPendingConnection();
        if (socket == nullptr) {
            return;
        }
        // 一个字节都不回：客户端那一发就卡在"等响应"上（它自己那 15 秒超时兜底）。
        msleep(static_cast<unsigned long>(m_holdMs));
        socket->abort();  // 到点放掉 —— 客户端那一发这时才失败收手（socket 归 server 管）
    }

private:
    int m_holdMs = 0;
    quint16 m_port = 0;
    QSemaphore m_ready;
};

/// 按 PID 问操作系统要一次「这个进程还在吗」。
///
/// 判据只能**从外面看**：我们自己手上那份 `QProcess` 在 `detachRunningGame()` 之后就
/// **故意不管它了**（那一段写得很清楚 —— 释放那些壳子等于执行析构、等于把游戏强杀），
/// 所以"它还活着"这件事没有别的问法。
inline bool processAlive(qint64 pid) {
    if (pid <= 0) {
        return false;
    }
    HANDLE handle =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (handle == nullptr) {
        return false;
    }
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(handle, &code) != 0 && code == STILL_ACTIVE;
    CloseHandle(handle);
    return alive;
}

/// 收拾干净：这一节起的那个替身进程**必须**在节尾走掉 —— 它攥着 `mw_check.exe` 这个文件
/// （真机上就是"残留 exe 占住文件、重新链接 Permission denied"那条）。
inline void killProcess(qint64 pid) {
    if (pid <= 0) {
        return;
    }
    HANDLE handle = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (handle == nullptr) {
        return;
    }
    TerminateProcess(handle, 0);
    CloseHandle(handle);
}

void runShutdownTests();


int MwHighSpeedDownloadCheck(QApplication &app);
int MwDownloadRetryCheck();
int MwActivitySelectionCheck();
int MwWorkspaceCheck(QApplication &app);
int MwPlayerSkinChecks(MainWindow &window);
int MwJavaPreferencesChecks(const bool settingsWired, MainWindow &window, QPushButton * &javaManageButton, QLabel * &javaLabel, QCheckBox * &javaWarn, QComboBox * &memoryMode, QLabel * &memoryHint, QSpinBox * &maxSpin, QComboBox * &behaviourCombo, QComboBox * &sourceCombo, QPlainTextEdit * &jvmEdit, QCheckBox * &customCheck, QCheckBox * &realCheck, QCheckBox * &loaderCheck, QCheckBox * &loaderVersionCheck, MemorySlider * &maxSlider);
int MwMemoryPreferencesChecks(const bool settingsWired, const QString &kDecorated, MainWindow &window, QComboBox * &installed, QComboBox * &memoryMode, QLabel * &memoryHint, QSpinBox * &maxSpin, QComboBox * &behaviourCombo, QComboBox * &sourceCombo, QPlainTextEdit * &jvmEdit, QPushButton * &applyJvm, QCheckBox * &loaderCheck, QCheckBox * &loaderVersionCheck, MemorySlider * &maxSlider);
int MwJavaSelectionChecks(const QDir &root);
int MwFolderSwitchingChecks(const QDir &root, MainWindow &window);
int MwLoginCompletionChecks();
int MwRenameStateChecks(const QDir &versionsDir);
int MwInstanceRenameChecks(const QDir &versionsDir, MainWindow &window);
int MwInstanceGroupsChecks(const QDir &root, const QDir &versionsDir);
int MwInstanceFiltersChecks();
int MwServerCreationChecks(const QDir &root);
int MwIntegrityActionsChecks(const QDir &root);
int MwInstanceOverviewChecks(const QDir &root, const QDir &versionsDir);
int MwOverridePersistenceChecks(const QDir &root, const QString &settingsDir, const QDir &versionsDir, QSpinBox * &maxSpin, MemorySlider * &maxSlider);
int MwRunStateLifecycleChecks(const QDir &root, const QDir &versionsDir);
int MwInstalledVersionsChecks(const QDir &versionsDir, MainWindow &window, QStackedWidget * &gameStack, QLineEdit * &installNameEdit, QLabel * &installNameError, QPushButton * &installBtn, QPushButton * &installBackBtn, GlobalJavaSettings * &windowJava, const std::function<void(const QString &)> &pinGlobalJava, const std::function<void()> &unpinGlobalJava);
int MwDownloadNavigationChecks(MainWindow &window);
int MwVersionActivationChecks(MainWindow &window, QTreeWidget * &versionTree, QStackedWidget * &gameStack, QLineEdit * &installNameEdit, QLabel * &installTargetLabel, QPushButton * &installBackBtn);
int MwRapidRowActivationChecks(MainWindow &window, QTreeWidget * &versionTree, QStackedWidget * &gameStack, QLineEdit * &installNameEdit, QLabel * &installTargetLabel, QPushButton * &installBackBtn);
int MwAccountAppearanceChecks();
int MwModpackImportChecks(const QDir &versionsDir, MainWindow &window, GlobalJavaSettings * &windowJava, const std::function<void(const QString &)> &pinGlobalJava, const std::function<void()> &unpinGlobalJava);
int MwTaskSummaryChecks(MainWindow &window);
int MwTaskStressChecks(MainWindow &window);
int MwGameLogLifecycleChecks(const bool settingsWired, MainWindow &window, QCheckBox * &showLogCheck, QComboBox * &priorityCombo, QSpinBox * &windowWidthSpin, QSpinBox * &windowHeightSpin, QComboBox * &windowModeCombo, QLabel * &windowSizeTimes);
int MwRandomSkinChecks();

