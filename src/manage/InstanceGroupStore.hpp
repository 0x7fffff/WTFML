/**
 * @file InstanceGroupStore.hpp
 * @brief 实例分组：把一份 .minecraft 里的实例归到用户自己建的几个组里。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 落盘在 <数据目录>/wtfml.folders，与 FolderManager 平级（都是"读盘 + 语义 +
 *       发信号"，界面拿信号去重灌列表）。本文件只管数据与文件，**不弹窗**。
 *
 * 【为什么每份 .minecraft 一个文件】
 *
 * 分组是"这一份 .minecraft 里的实例怎么摆"，不是"启动器全局的实例怎么摆"——
 * 用户加了三份游戏目录，每份里那些整合包的分组方式毫无关系。挂在数据根目录下
 * （而不是 versions/ 下）另有理由：删掉 versions 重下一遍是常见操作，用户的
 * 分组不该跟着陪葬。
 *
 * 【为什么成员键是「稳定 id，没有就退回目录名」】
 *
 * 实例的天然身份是目录名，但目录名会被改（启动器里改名、用户在资源管理器里改名）。
 * 所以长期来看归属得挂在一个不随名字变的 id 上（wtfml.meta 里的 instanceId，见
 * VersionManager::ensureInstanceId()）。
 *
 * NOTE: 但 instanceId 是 2026-09 才有的东西，**现存实例一个都没有**。要求"必须有稳定 id"
 *       就等于要么在扫描时偷偷写盘（扫描是只读路径，绝不能带副作用），要么老实例一上来
 *       全丢分组。所以读取时"先按稳定 id 找、再按目录名找"，写入一律用调用方给的键，
 *       而调用方（MainWindow::instanceKey()）的规则是"有 id 用 id，没有用目录名"。
 *       于是老实例的分组归属在工作到"被启动器碰过一次"（三条写路径都会顺手生成 id）
 *       之前是靠目录名认的 —— 这正是它唯一的现实语义。
 *
 * 【读盘策略：不缓存，按需读】
 *
 * 每个访问器内部比一下 AppPaths::minecraftDir() 和上次读的那份，不一样就重读。于是
 * "换游戏文件夹 → 分组自动换成那一份"不需要任何显式重置钩子 —— 那种钩子的典型失效
 * 形态是"某条换目录的路径忘了调它"，而这里它在结构上不可能发生。
 */
#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class InstanceGroupStore : public QObject {
    Q_OBJECT

public:
    /// 一个分组。id 是启动器生成的不变标识（组可以改名，id 不变），name 是显示名。
    struct Group {
        QString id;
        QString name;
        /// 这一组在实例树里**画成展开的还是收着的**（用户 2026-09-25 原话里的
        /// 「默认展开/关闭」）。
        ///
        /// NOTE: 它既是"默认"也是"当前"——**只有这一份状态**。用户点一下组行（或者齿轮
        ///       菜单里那一项）就把它翻过来并落盘，下一次重灌那张树照着它摆。两处各记一份
        ///       的话，树每次刷新（装完一个版本、改个名、换个目录都会重灌）就会把用户刚
        ///       收起来的组又撑开 —— 而"点一下打开/关闭"正是用户要的那件事。
        bool expanded = true;

        // 逐字段比较：MainWindow 靠它判断"用户在对话框里改过组清单没有"，
        // 没改就不去写盘。**expanded 也算在里面**：折叠状态是用户改出来的一格。
        friend bool operator==(const Group &, const Group &) = default;
    };

    /// 五个**默认组**的身份（用户 2026-09-24 原话：「可安装mod，原版，快照版，远古版，置顶」）。
    ///
    /// 枚举的顺序就是界面上从上到下的顺序（见 defaultGroups()）—— **置顶排在最前**，
    /// 「置顶」这个名字本身就是"放到最上面去"的意思，排在末尾就名不副实了。
    enum class DefaultKind {
        Pinned,     ///< 置顶：唯一一个**不自动往里塞**的（放什么进去是用户自己的事）
        Moddable,   ///< 可安装mod：装了模组加载器的实例（wtfml.meta 里有 loaderName）
        Vanilla,    ///< 原版：正式版，没装加载器
        Snapshot,   ///< 快照版
        Old,        ///< 远古版
    };

    explicit InstanceGroupStore(QObject *parent = nullptr);

    /// 一个默认组的 id。**写死的字符串、不是 UUID**：自动归类是按 id 找组的，
    /// 每次启动换一个 id 就等于每次都认不出来（表现是"昨天分好的，今天又全回未分组"）。
    /// 顺带的好处是用户在文件里一眼能看出哪几个是默认组。
    static QString defaultGroupId(DefaultKind kind);

    /// 一个默认组的名字（就是用户给的那五个词）。
    static QString defaultGroupName(DefaultKind kind);

    /// 五个默认组，按**界面上从上到下**的顺序（置顶在最前）。id 见 defaultGroupId()。
    static QList<Group> defaultGroups();

    /// 把五个默认组**种**进这一份 .minecraft（第一次用它的时候）。
    ///
    /// NOTE: 只在 wtfml.folders **不存在**时种。文件在（哪怕里面一个组都没有 —— 用户把
    ///       五个默认组全删光了）就一个字节都不写。**"默认组可以删"这件要求的全部实现
    ///       就是这一条**：删了不会再长回来。
    /// NOTE: 发 groupsChanged（写成功才发）。调用方如果是"因为要重画才来种的"，自己挡信号。
    /// @param errorOut 失败原因（给用户看的一句话）。文件本来就在 = 直接成功、不写盘。
    bool seedDefaultGroups(QString *errorOut = nullptr);

    /// 把组清单**恢复成五个默认组**（用户 2026-09-25 原话里的「恢复默认分组设置」）。
    ///
    /// 它是 `replaceGroups(defaultGroups())` 的一个有名字的说法，单独列出来是为了让
    /// "这一件事"在调用处读得出来，也为了让两个后果在唯一一处写清楚：
    ///   - **用户自己建的组会消失**，组里那些实例**一个都不会被删**：它们指向的组没了，
    ///     按 replaceGroups() 的规矩那条归属一起被清掉 —— 于是它们回到"没表过态"，
    ///     下一次刷新按版本类型自动归类（可安装mod / 原版 / 快照版 / 远古版）。
    ///   - **五个默认组的 id 是写死的**（defaultGroupId），所以原来归在默认组里的实例
    ///     还在原处：哪怕用户把「原版」改过名，恢复之后名字回到「原版」、成员不变。
    /// @param errorOut 失败原因（给用户看的一句话）。
    bool restoreDefaultGroups(QString *errorOut = nullptr);

    /// 归属表里那个**保留值**：用户**明确**选过「未分组」。
    ///
    /// 【为什么"没有这一条"和"这一条写着它"必须分开】
    ///
    /// 没有这一条 = 用户从来没给这个实例表过态 → MainWindow 会按版本类型把它归到对应的
    /// 默认组里去（"可安装mod / 原版 / 快照版 / 远古版"就是靠这个动起来的）。
    /// 而用户在对话框里选了「未分组」，表的就是"别归类，我就想让它待在未分组里"这个态 ——
    /// 两者在界面上是同一格（都画在「未分组」底下），在文件里必须是两回事，否则
    /// "点了确定但没生效"：下一个刷新就又把它拉回默认组去。
    ///
    /// 取值是一个**不可能和组 id 撞**的串（组 id 是 UUID 或者 default-* 那五个）。
    static QString ungroupedMarker();

    /// 这个实例的归属**表过态**没有（不管表的是哪个组、哪怕那个组已经被删了）。
    ///
    /// NOTE: 判据刻意**不看那个组还在不在**：组被删掉之后指向它的那条残留（清理要等下一次
    ///       写）在这条上仍然是"表过态"。于是"删掉一个默认组，它里面的实例落到未分组"是
    ///       天然的 —— 用不着为它写任何清理代码，也不会被自动归类又抓回别的组去。
    bool isAssigned(const QString &instanceKey) const;

    /// 全部组，按用户排的顺序。文件不存在 / 读不懂时是空的。
    ///
    /// 读出来的东西按不可信数据清洗：没有 id 的条目丢掉（认不出来）、id 重复的只留
    /// 第一个、名字为空的用 id 顶上（界面上至少还认得出来是哪一个）。
    QList<Group> groups() const;

    /// @param instanceKey 实例的归属键（见文件头）。
    /// @return 它归在哪个组；没分组、或者指向一个**已经不存在的组**时返回空串。
    ///
    /// 指向不存在的组是"那个组被删了"的正常残留（删组会清成员，但用户手改文件、
    /// 或者两份文件不同步时会有），当未分组处理，不报错、也不在这一层清理 ——
    /// 读路径不该有副作用，清理交给下一次写。
    QString groupIdOf(const QString &instanceKey) const;

    /// 整个组清单换成这一份（新建 / 重命名 / 删除都由它表达）。
    ///
    /// NOTE: 是**全量替换**而不是三个细粒度操作。唯一写它的地方是分组对话框，那是个
    ///       对整张组清单的模态编辑器 —— 细粒度操作会让 MainWindow 拿着编辑前后的
    ///       两份清单自己 diff 一遍，那份 diff 逻辑里"删组要不要清成员"这类判断
    ///       必然要和这里抄第二遍。全量替换把规范化收在一处。
    /// NOTE: 指向被删掉的组的成员条目会**一起清掉**（那个归属已经没有意义了），
    ///       而**实例本身一个字节都不动** —— 删组从来不删实例。
    /// @param errorOut 失败原因（给用户看的一句话）。
    bool replaceGroups(const QList<Group> &groups, QString *errorOut = nullptr);

    /// 把某一组的「展开还是收着」（Group::expanded）改掉并落盘。
    ///
    /// NOTE: 【为什么它**不发** groupsChanged】
    ///       那个信号的意思是"组清单变了，界面该重灌"（加组、删组、改名、换顺序、换归属）。
    ///       而这里改的是**界面刚刚自己摆好的那一个状态**（用户点的那一下就是这个新状态），
    ///       重灌一遍只会把整棵树重建一次：选择、滚动位置、正在展开的另一条都跟着抖一下，
    ///       换来的东西是零。剩下的一处读者（重灌树时读它摆展开状态）本来就会在这次之后
    ///       的任意一次重灌里读到新值。
    /// NOTE: 状态本来就是那个值时**不写盘**（双击那两发里第二发会走到这儿），和 rekey()
    ///       那几条空操作同一条规矩。
    /// @param groupId 组 id（空串按调用方的 bug 拒掉）。
    /// @param expanded 想要的状态。
    /// @param errorOut 失败原因（给用户看的一句话）。
    bool setGroupExpanded(const QString &groupId, bool expanded, QString *errorOut = nullptr);

    /// 把某个实例归到某个组。`groupId` 传空串 = 取消分组。
    ///
    /// NOTE: 空串进去**不是"把这一条删掉"**，而是写成一个 `ungroupedMarker()`（= 用户明确
    ///       选了「未分组」）。两件事在界面上都画在「未分组」底下，在文件里必须分得开 ——
    ///       见 ungroupedMarker() 那段。读回来（groupIdOf）仍然是空串，调用方看不出来。
    /// @param instanceKey 实例的归属键（见文件头）。
    /// @return groupId 非空但磁盘上没这个组时返回 false。
    bool assign(const QString &instanceKey, const QString &groupId, QString *errorOut = nullptr);

    /// 实例改名的同时把它的归属搬运到新键上。
    /// @param oldKey 改名前的归属键。@param newKey 改名后的。
    /// @return 老键本来就没有归属（或者两个键一样）时返回 true 且**不写盘**。
    ///
    /// NOTE: 目标键上已经有一个归属时会被覆盖。这不是丢数据 —— 两个键指向同一个实例
    ///       （"同一个实例有两个键"正是改名造成的），那个被覆盖的值本来就是我方的。
    bool rekey(const QString &oldKey, const QString &newKey, QString *errorOut = nullptr);

    /// 一个新的分组 id。对话框新建组时用它。
    ///
    /// 用 UUID 而不是自增整数：成员表里的归属值是**跨会话**的，自增 id 在"删了一个组
    /// 再加一个"时会和文件里残留的归属撞上（撞上的表现是几个实例莫名其妙钻进新组），
    /// 而 UUID 不会。和 VersionManager 的实例 id 同一个理由、同一个形式。
    static QString newGroupId();

signals:
    /// 分组数据变了（组清单或归属）。界面据此重灌实例列表。
    /// NOTE: 写成功才发。读从来不发 —— 读不改变任何东西。
    void groupsChanged();

private:
    /// 目录换了（或者第一次用）就重读。const 是因为所有访问器都是 const 的，
    /// 而"按需重读"对调用方来说只是"这次读得慢一点"。
    void ensureLoaded() const;

    /// 把规范化之后的组清单 + 成员表写出去。几条写路径共用，免得"哪些成员该留"
    /// 这套判断抄三遍。
    /// @param notify 写成功之后发不发 groupsChanged()。
    ///
    /// NOTE: 只有 setGroupExpanded() 传 false —— 展开/收着那一个字段的读者是"重灌树的
    ///       时候读它摆状态"，而用户点的那一下界面**已经自己摆好了**，为它重建一次整棵
    ///       树只会把选择、滚动位置、别的组的展开状态一起抖一下（完整理由见
    ///       setGroupExpanded() 那段 NOTE）。别的写路径动的都是组清单/归属，界面非重灌
    ///       不可，一律发。
    bool writeThrough(const QList<Group> &groups, const QJsonObject &rawMembers,
                      QString *errorOut, bool notify = true);

    /// 上次读的是哪一份 .minecraft 下的文件。空串 = 还没读过。
    mutable QString m_loadedFor;
    /// 文件里除我们认识的键之外的**原样保留**（这是给以后的版本留的余量：更新版本的
    /// 启动器往里写的新字段，被这一版启动器碰一次就不该消失）。
    mutable QJsonObject m_root;
    /// 非空 = 这个文件读不懂（不是合法 JSON / 版本号不认识 / 打不开），值是给用户看的
    /// 那句话。此时**拒绝写** —— 往一份读不懂的内容上合并等于把用户的东西丢掉，
    /// 和 VersionManager::updateVersionMeta() 对 wtfml.meta 的处理是同一条规矩。
    mutable QString m_unreadableReason;
};
