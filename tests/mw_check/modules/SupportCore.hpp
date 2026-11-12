/**
 * @file SupportCore.hpp
 * @brief mw_check 公共夹具的**底座**：Qt / 产品头文件 + 断言（`check()`）+ 几个不碰界面的小工具。
 *
 * 这一层往上才是真东西：
 *  * `SupportFakes.hpp` —— 假外网 / 假接口 / 假"接了不答话"的端口（各自拥有一份状态）；
 *  * `Support.hpp` —— 真主窗口那套 UI 夹具（点弹窗、翻页、关页）与各节的入口声明。
 *
 * 拆开的理由是"状态归谁"：以前两千多行挤在一个头里，改一台假服务器要在一整份文件里翻；
 * 现在断言与小工具只有这一处，那几台假服务器也各在自己的文件里。
 *
 * NOTE: 这个文件里**没有 Q_OBJECT**；`UrlSpy` 那几个带元对象的留在 `Support.hpp`
 * （AUTOMOC 只对显式列进 `wtfml_mw_fixture` 的头生成元对象）。
 */
#pragma once

#include <functional>
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
#include "DownloadToolPanel.hpp"
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
