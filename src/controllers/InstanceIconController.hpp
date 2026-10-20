#pragma once

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <functional>

#include "manage/InstanceIconLookup.hpp"

class InstanceSettingsDialog;
class LauncherSettings;
class ResourceStore;
class QThread;

/// 「文件管理」那几页里那些**资源文件**的图标反查，连同它的会话缓存（2026-09-27）。
///
/// ★ 用户原话：「模组和资源包等都有一个图标，把图标显示出来」。盘上那个文件名**不是
///   身份**（用户改过名、各家起名不一样），所以"这是哪个资源"只能按**文件内容**反查
///   （sha1 → Modrinth、指纹 → CurseForge，见 `InstanceIconLookup`）。那几发是网络，
///   而算哈希要读一遍整个文件 —— 两样都**不能在界面线程上做**。
///
/// ★★ 2026-10-15：这一整块（那一趟的状态 + 会话缓存 + "这一条是哪个资源"那张表）从
///   `MainWindow` 搬了进来。搬之前它是主窗口的七个私有成员，于是搬出去的每一段都得先
///   拿到 `MainWindow *` 才能干活（靠的是一句 `friend class`）—— 那是"功能没真的分出去"
///   的典型形状：控制器只是主窗口的一段后缀。现在主窗口只递三样进来（设置 /
///   `ResourceStore` / 四个回调），方向是**这一层说了算、主窗口听**。
///
/// ★ 这一层**不弹任何东西**：图标取不到是常态（自己改过的 jar、作者没传过图标的、断网
///   的），画个占位拼图就是正确的界面 —— 它挡不住用户做任何事。失败只说一句日志。
class InstanceIconController final : public QObject {
    Q_OBJECT
public:
    /// 这一层伸手去够"只有主窗口说得出来"的那几样。一个一个都是**问**，不是拿：主窗口
    /// 那一侧的原始数据（实例目录表 / 显示名 / 日志 / 下载页那份 `Hit` 表）一个字节都不
    /// 搬过来，于是这一层能脱开主窗口单独驱动（拿四个 lambda 就够）。
    struct Host {
        /// 一个实例的目录表（键 = `ResourceCatalog` 那几张表用的 key）。
        std::function<QHash<QString, QString>(const QString &versionId)> fileDirs;
        /// 实例的显示名（日志那一句「正在认「X」里的资源图标…」要用）。
        std::function<QString(const QString &versionId)> displayName;
        /// 记一句日志。
        std::function<void(const QString &text)> log;
        /// 认出来的那一条**交给下载页那一份表**（`MainWindow::m_resourceHits`）。
        ///
        /// 为什么非交不可：点这一行的名字进它自己的那一页，走的正是**下载页点一行**那条路
        /// （`openResourceDetailPage()` 按这把钥匙取标题 / 简介 / slug / 游戏版本），两处
        /// 的分别只有"来路"那一位。这一层自己也留一份（`m_fileResources`）—— 那是为了摆
        /// 那一行的名字，两边说的必须是同一件事，谁也不许另攒一份。
        std::function<void(const QString &iconKey, const ResourceCatalog::Hit &hit)> rememberHit;
    };

    InstanceIconController(LauncherSettings *settings, ResourceStore *resourceStore, Host host,
                           QObject *parent);

    /// 把「文件管理」那几页里那些**资源文件**的图标要回来（2026-09-27）。
    ///
    /// ★ 用户原话：「模组和资源包等都有一个图标，把图标显示出来」。盘上那个文件名**不是
    ///   身份**（用户改过名、各家起名不一样），所以"这是哪个资源"只能按**文件内容**反查
    ///   （sha1 → Modrinth、指纹 → CurseForge，见 `InstanceIconLookup`）。那几发是网络，
    ///   而算哈希要读一遍整个文件 —— 两样都**不能在界面线程上做**（用户对"锁住其他按钮"的
    ///   意见见 pingServers 那段）。
    ///
    /// 所以这一发只做三件事：列一遍候选文件、把**已经知道的**先摆上去、需要问的丢给后台
    /// 线程。重复调用是安全的：已经有一趟在跑时不重开（只把界面接回新开的那一页）。
    void requestInstanceFileIcons(InstanceSettingsDialog *dialog, const QString &versionId);

    /// 这一条文件的**缓存钥匙**：路径 + 大小 + 修改时间。
    ///
    /// NOTE: 为什么要带上后两样（而不是只按路径）：路径复用了、内容换了的形态是**真的
    ///       会发生的**（删掉一个模组、换上另一个同名文件），只按路径键住的话，那一格会
    ///       一直画着**上一个**资源的图标 —— 不报错、看着也像张正经图标，只有用户认得出来。
    static QString instanceIconCacheKey(const QString &path);

    /// 这个实例里那些**可能查得到图标**的文件（绝对路径）：四个能下载的目录 + 原理图，
    /// 只要 `.jar` / `.zip`（判据是 `InstanceIconLookup::isLookupCandidate()`）。
    QStringList instanceIconCandidatePaths(const QString &versionId) const;

    /// 某一张图标回来了（按它的 `<来源>-<项目号>` 认）⇒ 记到**认得出这个资源的那几条
    /// 文件**头上，并当场摆给正开着的那一页。
    ///
    /// ★ 调用点必须排在"那一发取图请求"之后：盘上已经有缓存的话它是**同步**发图标回来
    ///   的，所以到这儿就是"记了账、立刻就用上"，而记账那一步在请求之前就写完了。
    void rememberResourceIcon(const QString &iconKey, const QPixmap &icon);

    /// 这个目录底下**认出来了**的那些资源（键 = `<来源>-<项目号>`，见
    /// `ResourceCatalog::Hit::iconCacheId()`）。走的是下面那张 `m_fileResources`：
    /// 键是 `路径\n大小\n时间`，前缀比得上就说明这个文件在这个目录底下。
    ///
    /// ★ 这份判据的**强度就是"这个会话认过多少"** —— 没进过那一页就是空的。所以调用方
    ///   还得配一个"到底认过没有"（见 `recognizedInstanceIs()`）：**空表**和"确实一个都
    ///   没装"是两件事，界面上那两句话不一样（见 `MainWindow::installedModKeys()`）。
    QSet<QString> recognizedResourceKeysUnder(const QString &dir) const;

    /// 现在摊着的那一页是不是**这个实例**的 —— 也就是"这个实例的 files 到底认过没有"。
    bool recognizedInstanceIs(const QString &instanceId) const;

    /// 这一条文件是哪个资源（`Hit::iconCacheId()`）。没认出来 = 返回 false，@p iconKey
    /// 不动。
    bool fileResourceIconKey(const QString &path, QString *iconKey) const;

    /// 关窗那一刻正卡在等响应上是最平常的一件事，而这一趟的线程父对象是**这一层** ——
    /// 不等它跑完就析构的话，`QThread` 会带着一个还在跑的线程被删掉。主窗口析构里那张
    /// "一个一个等过来"的单子留一条给这儿（见 `MainWindow::~MainWindow()`）。
    void waitForLookup();

private:
    /// ★ 2026-09-27 拆出来的那一段：照**现在这一页**（`m_instanceIcons.paths`）里还没问过
    /// 的那些开一趟后台。`requestInstanceFileIcons()` 和"上一趟收尾时发现用户已经换了页"
    /// （见 `onInstanceIconLookupFinished()` 末尾）两处都走它。
    ///
    /// 不这么拆的话，"A 的线程还在跑、用户又开了 B 的设置页"这一形态下 B 会被**静默丢下**：
    /// 那次请求撞在 `active` 闸上直接 return 了，而 A 跑完时谁也不会回头替 B 问 —— B 的
    /// 文件要等用户再进一次这一页才有图标。
    void startInstanceFileIconLookup();

    /// 后台那一趟回来了。见 .cpp 里那一段。
    void onInstanceIconLookupFinished(const InstanceIconLookup::Result &result);

    /// 把**现在知道的**那些图标摆到正开着的那一页上（没开着就什么都不做）。
    /// 「图标到手」和「新开了一页」两条路都走它 —— 两处各摆一遍的话，两边的判据迟早漂开。
    ///
    /// ★ 2026-09-27 起它同时摆"这一条的名字点不点得动"（见 `setFileResource()`）：两件事
    ///   读的是同一份会话缓存、同一个循环、同一个"这一条现在算不算知道"的判据 ——
    ///   拆成两个函数摆，迟早出现"图标有了、名字点不动"这种两边打架的界面。
    void pushInstanceFileIcons();

    Host m_host;
    /// 三个口子和 `ResourceStore` / `VersionManager` 那两处**同一份设置**（CurseForge 的
    /// key 是用户自己填的那一份；两个 override 只有验证工程会写）。
    LauncherSettings *m_settings = nullptr;
    /// 取图交给它（它有一套现成的两级缓存 + 条件请求 + 挑 webp/PNG 那套判断）。
    ResourceStore *m_resourceStore = nullptr;

    // ---------- 一趟图标反查的状态 ----------
    //
    /// 一趟图标反查的状态。形状和那趟测速**故意是一样的**（弱引用 + 后台线程 +
    /// 结果回来时"那一页还在不在"不确定），理由也逐字相同。
    struct InstanceIconRun {
        /// 现在开着的那一页。**弱引用** —— 关掉之后它是空的，回来的图标就只进缓存、
        /// 不碰界面（往一个析构了的对话框上写是 use-after-free）。
        QPointer<InstanceSettingsDialog> dialog;
        /// 这一趟列出来的候选文件（绝对路径）。回灌那一步走的就是它 —— 只摆这一份里的，
        /// 而不是把整张会话缓存（可能上千条）挨个问那六页一遍。
        ///
        /// NOTE: 这是"**现在这一页**是谁"的快照，每次 requestInstanceFileIcons() 都整个换掉。
        ///       **别拿它当"某一趟问了哪些文件"用** —— 那个是下面 m_instanceIconRunPaths，
        ///       两者在这一页被换掉之后就不一样了。
        QStringList paths;
        /// 这一页是**哪个实例**（versionId）。只为一件事：这一趟在跑的时候用户换了一页
        /// 的话，收尾时看得出"现在这一页已经不是这一趟问的那一页"，得给新那页补一趟。
        QString versionId;
        /// ★ 2026-10-20：这一页开的时候，这个实例那几个目录**长什么样**（`fileDirs()` 排序
        /// 拼出来的签名）。
        ///
        /// 判"这个实例认过没有"（`recognizedInstanceIs()`）时**不能只比 versionId**：同一个
        /// 版本名在另一个游戏文件夹里也有一份是常事（切换游戏文件夹 / 多开 / 两个启动器各
        /// 指一个目录）。只比名字的话，在 A 里进过文件管理页、切到 B 之后同名那一份会被当成
        /// "认过了" —— 而认出来的键全在 A 的目录底下，B 那一边一条都对不上，前置检查就会
        /// 说"你一个都没装"。
        QString dirsSignature;
        /// `Hit::iconCacheId()`（带来源前缀）→ 认得出这个资源的那些**文件**。
        ///
        /// 反过来的方向（一个文件 → 它的 iconId）在下面那张表里；这一张是给
        /// `rememberResourceIcon()` 用的：图标是按身份回来的，而屏幕上要换的是**行**。
        /// NOTE: 跨对话框累积（同一个资源在别的实例里也有，那张图本来就该两边都画上）。
        QHash<QString, QStringList> pathsByIconKey;
        /// 现在算不算"有一趟在跑"。**这一位是权威**（见 requestInstanceFileIcons 里那道闸）。
        bool active = false;
    };
    InstanceIconRun m_instanceIcons;
    /// ★ 2026-09-27 加：**这一趟真的问了哪些文件**（绝对路径），以及是给哪个实例问的。
    ///
    /// 为什么不读 `m_instanceIcons.paths`：那是"最后一次请求"换上去的那一份，而
    /// requestInstanceFileIcons() 里换它的那一句排在 `active` 闸**之前** —— 于是"A 的线程
    /// 还在读盘、用户又开了 B 的设置页"这一形态下，A 的结果回来时那张表**已经是 B 的了**。
    /// 以前负缓存（m_instanceIconMissing）正是照那张表写的：A 那一趟的"没找到"被逐条记到了
    /// **B 的文件**头上，而且那张表除析构外**从不清理** ⇒ B 这一整个会话都看不到图标，
    /// 连"再点一次刷新"都没用（下一次请求在同一个假钥匙上照样跳过）。按趟记账之后，被记的
    /// 永远是**真的问过**的那一批。
    QStringList m_instanceIconRunPaths;
    /// 这一趟是给哪个实例问的（同上；收尾时拿它和 `m_instanceIcons.versionId` 比一比，
    /// 不一样就说明用户中途换了页，得补一趟）。
    QString m_instanceIconRunVersionId;
    /// 跑那一趟的线程。`QThread::create()` 出来的（省掉"再写一个 QThread 子类"，
    /// 同 `ServerPing::Pinger` 里那条 NOTE）。跑完就 deleteLater（见 .cpp 里那段）。
    QThread *m_instanceIconThread = nullptr;
    /// **会话级**的真图标缓存，键是 `instanceIconCacheKey()`（路径 + 大小 + 时间）。
    ///
    /// 为什么要有（而不是"每次都重新反查"）：那张图是一次读盘 + 联网换来的，而用户关掉
    /// 设置页再打开是很平常的一件事（改个开关就要进去一次）—— 每次重来一遍的话，一列
    /// 几十个 jar 要重新读一遍（几百 MB），而且那几秒里图标是占位图。值**可以是空
    /// pixmap**（见 m_instanceIconMissing 那段）。
    QHash<QString, QPixmap> m_instanceFileIcons;
    /// 问过了、**没有**图标的那一批（同一把钥匙）。存它只为一件事：别把同一个文件问第二遍。
    ///
    /// ★ 只有"这一趟**完整跑完**了"才会往里写（`Result::problem` 空，见
    ///   onInstanceIconLookupFinished）。断网那一趟要是也记成"没有"，用户这一整个会话就
    ///   再也看不到图标了 —— 而那本来是"网好了就会有"。
    QSet<QString> m_instanceIconMissing;
    /// 认出来的那几条**是哪个资源**：同一把钥匙（路径 + 大小 + 时间）→ 整份 `Hit`。
    ///
    /// ★ 2026-09-27 补，为的是"点名字进它自己的那一页"（见 `openInstanceFileResource()`）：
    ///   那件事要的正好就是下载页点一行要的那几栏（标题 / 简介 / slug / 游戏版本），
    ///   而它们**本来就在手上** —— 反查那一趟早就把整个项目对象问回来了（见
    ///   `InstanceIconLookup::Found::hit`），从前读完图标地址就把其余几栏丢了。
    ///   所以这里存**整条**（`Hit::iconCacheId()` 只是它的一个键），而不是只存项目号
    ///   再回头去别处要标题。
    /// NOTE: 同一份 Hit **还要**交给下载页那一份表（`Host::rememberHit`）：点是那一行
    ///       名字要开的那一页，就是按那份表开出来的（同一页、同一套 chip）。
    ///
    /// NOTE: 和 m_instanceFileIcons 同一把钥匙 ⇒ 同一个名字上换了一个文件就自然作废，
    ///       不会出现"词条写着 A、名字是 B"。
    /// NOTE: 键是**路径的钥匙**而不是路径，所以文件删掉之后这一条留着也不会有人读到它
    ///       （pushInstanceFileIcons() 只走 `m_instanceIcons.paths` 里那些还在的路径）。
    QHash<QString, ResourceCatalog::Hit> m_fileResources;
};
