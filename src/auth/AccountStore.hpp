/**
 * @file AccountStore.hpp
 * @brief 多账号：盘上存着好几个账户各自的登录凭据，其中一个是"当前账号"。
 * @note **活动槽**和**列表**是两回事，这是这个文件全部设计的出发点：
 * - 活动槽 = `MicrosoftTokenData` / `MinecraftTokenData` 那两个键。**登录链、
 * 启动游戏、换皮肤披风读的都是它**，一个字都没改（AuthManager 照旧）。
 * - 列表 = `SavedAccounts` 这个键，装的是"每个账号各自那一份凭据副本"。
 *
 * "当前账号"于是**不是**一份额外状态：谁的两个键躺在活动槽里，谁就是当前账号
 * （`CurrentAccountUuid` 只是把这个事实记下来给界面看，读的时候以活动槽为准）。
 * 这样启动路径上没有任何新东西要读 —— 换账号不会给"能不能启动"这件事引入新的失败点。
 *
 * 【为什么副本必须用 TokenStore 那套格式】
 *
 * 换账号 = 把那两个键换成另一个账号的那份字节。所以列表里的副本和活动槽里那份**本来就是
 * 同一种东西**，编解码只有 TokenStore 一处（见那边的文件头）。列表整份也是同一层保护：
 * DPAPI、绑定当前 Windows 用户。
 *
 * 【退出登录，和"删除账号"是两件事】
 *
 * - `logout()`（界面上是底栏账号面板那一行当前账号的「登出」按钮）：
 * **只**清活动槽（连同 `CurrentAccountUuid`），列表里那一项留着。之后就是**没登录**，
 * 界面自己会退回未登录那一档；点那一行的「选择」/「登录」就回来了。
 * - `remove()`：连列表项一起删。删掉的如果正好是当前账号，活动槽**也**一起清空 ——
 * 留着会让"当前账号不在列表里"变成一种说得通但没人想要的状态。
 *
 * 两个动作的最后一步是同一句话（清那三个键），所以 remove() 里那一档和 logout() 长得像
 * 是**故意**的：盘上"当前账号"这件事只有活动槽一处记账，没有第二个地方要收尾。
 */

#pragma once

#include <QList>
#include <QString>

/// 列表里的一项。**只有显示用的字段**（uuid / 名字 / 收进来的时间），凭据字节不往外出。
struct SavedAccount {
    /// 不带横线的 32 位十六进制，和 AuthManager 内部存的那个形式一致（档案里给的就是它）。
    QString uuid;
    QString name;
    /// 最后一次把这份凭据收进列表的时刻（ISO）。给界面显示"什么时候存的"。
    QString savedAt;
    /// 盘上活动槽现在是不是这个人。由 AccountStore 按活动槽现算，**不是**存下来的字段。
    bool isCurrent = false;
    /// 这是一条**离线账号**：只有一个名字，盘上没有它的任何凭据。
    ///
    /// NOTE: 它的 uuid 不是微软给的，是按游戏自己那套离线规则从名字推出来的
    /// （见 offlineUuidFor()）—— 于是"uuid 即身份"这条约定对它照样成立，只是
    /// 身份的来源不一样：同一个名字在哪台机器上推出来都是同一个 uuid，重装启动器、
    /// 换台电脑，切过去还是同一个人。
    bool offline = false;
    /// 刷新令牌是否已失效、必须重新登录（如微软返回 invalid_grant）。离线账号恒为 false。
    bool needsRelogin = false;
};

class AccountStore {
public:
    /// 列表里所有账号（按收进来的先后）。读不到/解不开一律当空列表，不抛。
    static QList<SavedAccount> accounts();

    /// 有没有这个账号。
    static bool contains(const QString &uuid);

    /// 列表里这个账号的名字（没有就空串）。
    static QString nameOf(const QString &uuid);

    /// 当前账号是谁：**先看活动槽**（那份凭据属于谁）；活动槽是空的再看
    /// `CurrentAccountUuid` —— 而且只认它指向的那一项**是离线账号**的时候。
    ///
    /// NOTE: 为什么要分这两档：在线账号的"当前"是「凭据正躺在活动槽里」这个事实，
    /// 凭据没了就是没登录，不能凭一个记录键就说"当前是某某"；离线账号压根没有凭据，
    /// 它的"当前"只能靠那个记录键。两档都卡死之后，`CurrentAccountUuid` 就不可能
    /// 让一个已经退出登录的在线账号重新变成"当前"。
    static QString activeUuid();

    /**
     * @brief 把活动槽里那份凭据收进列表（登录成功之后调）。
     * @param errorOut 可选。写盘失败的原因。
     * @return 盘上确实有凭据、也确实写进列表了才返回 true。
     *
     * NOTE: uuid 和名字都**从凭据自己身上读**（Minecraft 那份里就有），不接受调用方传进来
     * —— 传进来的值和盘上的值一旦不一致，列表里就会出现一项点了切不过去的账号。
     * 已有同 uuid 的项会被就地更新（令牌换新了、名字改了都算同一个人）。
     */
    static bool captureActive(QString *errorOut = nullptr);

    /**
     * @brief 切换当前账号：把列表里那个人的凭据写回活动槽（离线账号则是清空活动槽）。
     * @param errorOut 可选。失败的原因（那一项不在列表里、或者副本已经解不开了）。
     * @return 成功返回 true。成功后**调用方要记得**让 AuthManager 重新读一遍盘
     * （AuthManager::reloadStoredAccount()），否则内存里还是上一个人的名字。
     *
     * NOTE: 副本可能比活动槽旧（在这个账号里登录之后，令牌在别处又刷新过）—— 这里**不**
     * 判断新旧，直接把副本写回去。判据是"最后收进列表的那一份"，那是用户看得见的
     * 事实（列表上写着存于什么时候）；自己去比时间戳反而会出现"我明明切了、它却说
     * 没有更新的"这种说不清的状态。令牌过期了启动时会照常刷新（AuthManager 那条路）。
     *
     * NOTE: 目标是**离线账号**时相反 —— 它没有副本可写，切过去 = 清空活动槽 + 把"当前"
     * 记成它。这一档一个凭据字节都不碰，所以除了"那一项根本不在列表里"没有别的
     * 失败方式。
     */
    static bool activate(const QString &uuid, QString *errorOut = nullptr);

    /**
     * @brief 从列表里删掉一个账号。
     * @param errorOut 可选。删的是当前账号、而活动槽那几个键没清干净时的原因（R13）。
     * @return 列表里本来有它、并且写盘成功才返回 true。
     *
     * NOTE: 删的正好是当前账号时**连同活动槽一起清空**（等于退出登录），理由见文件头。
     * 列表项删掉之后活动槽清不掉 ⇒ 也返回 false：账号确实不在列表里了，但盘上的登录状态
     * 还留着，调用方得如实说（不能报"删干净了"）。
     */
    static bool remove(const QString &uuid, QString *errorOut = nullptr);

    /**
     * @brief 退出登录：清掉盘上的登录状态，**账号本身留在列表里**。
     * @return 本来就有登录态、而且清干净了才返回 true（本来就没登录返回 false）。
     *
     * NOTE: 和 remove() 只差一条 —— 列表里那一项**一个字节都不动**。所以登出之后点那一行的
     * 「选择」/「登录」还能回来（清单里存着这个账号唯一的一份凭据副本，见 activate()）。
     * 想彻底不要这个账号走「删除所选」（那一条会连带把列表项删掉）。
     *
     * NOTE: 清活动槽之前**先把里面那份凭据收进列表**（和 makeOfflineCurrent() 同一个理由）：
     * 登出这一下会把活动槽里那份 refresh_token 从盘上抹掉，而它可能比列表里那份**新**
     * （这个账号这一趟用着的时候刷新过令牌）。
     *
     * R8：收不进去**分两种**。活动槽里本来就没有完整凭据（没登录 / 缺了微软那半边）→
     * 清一遍是空操作，照常登出；可要是凭据齐着、只是列表读不出来 / 写盘失败 → 这次
     * **不登出、返回 false**（一个字节都不动）。清掉的话盘上再没有第二份 refresh_token，
     * 用户以为"账号还留在列表里点一下就能回来"，实际只能重走浏览器登录。
     *
     * R13：三个键**都真的清掉了**才返回 true。`TokenStore::clear()` 现在会报失败
     * （拿不到目录锁 / QSettings 写不进去）—— 那种情况下界面不能报"已退出登录"。
     *
     * NOTE: 三个键一起清（微软 / Minecraft / CurrentAccountUuid）。最后一个**必须**清：
     * activeUuid() 第二档只看 `CurrentAccountUuid` 是不是指向一个**离线**账号，登出的是
     * 离线账号时不清它，盘上就还是"当前是某某"。
     */
    static bool logout();

    /// 列表里那一项。找不到返回 false，*out 不动。
    static bool find(const QString &uuid, SavedAccount *out);

    /// 查询该正版账号是否已被标记为「需要重新登录」（刷新令牌失效）。离线账号或未找到返回 false。
    static bool needsRelogin(const QString &uuid);

    /// 更新列表里该正版账号的「需要重新登录」标记。找不到或离线账号时返回 false。
    static bool setNeedsRelogin(const QString &uuid, bool required, QString *errorOut = nullptr);

    /// 从名字推出来的离线 uuid（32 位小写十六进制，不带横线）。
    ///
    /// NOTE: 规则是 md5("OfflinePlayer:" + 名字)，再把版本位/变体位摆成 RFC 4122 的 v3
    /// —— 和游戏自己在离线模式下算的是**同一个值**。这里不另发明一套：两套算法
    /// 意味着同一个名字在启动器里和游戏里是两个身份，皮肤、存档、服务器白名单
    /// 会各认各的。
    static QString offlineUuidFor(const QString &name);

    /**
     * @brief 往列表里加一条离线账号，并**切到它**。
     * @param errorOut 可选。名字不合格时写原因（空的 / 有空白 / 太长）。
     * @return 名字合格、而且写盘成功才返回 true。
     *
     * NOTE: 加完顺手切过去 —— 这是"添加账号"这个动作在另一条路上的样子：走微软那套登录
     * 时，成功之后凭据就躺在活动槽里了，等于当场成了当前账号。离线账号要是只加不切，
     * 用户点完"添加"看到的唯一变化是多了一行，还得再点一次"切换到所选" —— 同一个
     * 按钮两种结果，那才是真的费解。
     * 切过去 = 清空活动槽（离开在线身份）+ 记下当前是它，和 activate() 那条路完全
     * 一样。当前那个在线账号**不会丢**：凭据还在列表里，点一下就回来了。
     */
    static bool addOffline(const QString &name, QString *errorOut = nullptr);

    /// 这一项是不是离线账号。不在列表里也返回 false。
    static bool isOffline(const QString &uuid);
};
