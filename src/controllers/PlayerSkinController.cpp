/**
 * @file PlayerSkinController.cpp
 * @brief 实现玩家外观的选取、取回、认领与降级。
 */
#include "controllers/PlayerSkinController.hpp"

#include <QFile>

// `SkinModel::defaultSteveSkin()` —— 载不进那张贴图时的**兜底外观**（程序画的经典配色，
// 不是把官方那张 Steve 贴图打进仓库）。`ui/` 在这一层的上面两级（src/controllers/ →
// src/ → 仓库根），和 MainWindow.cpp 那边写 `..\ui\...` 是同一个意思。
#include "../../ui/SkinModel.hpp"
#include "auth/AccountStore.hpp"
#include "auth/AuthManager.hpp"
#include "manage/DefaultSkin.hpp"
#include "manage/OfflineSkin.hpp"
#include "manage/RandomSkinStore.hpp"

PlayerSkinController::PlayerSkinController(AuthManager *auth, PlayerSkinStore *store,
                                           RandomSkinStore *randomStore, QObject *parent)
    : QObject(parent), m_auth(auth), m_store(store), m_randomStore(randomStore) {
    // 三个数据源都是主窗口那边的成员对象（它们的生命周期本来就比外观这一件事长得多），
    // 所以这里只接信号、不接管所有权 —— 认人靠的是它们自己。主窗口在构造里就把它们连上，
    // 见 MainWindow 的接线那一段。
    if (m_store != nullptr) {
        connect(m_store, &PlayerSkinStore::profileReady, this, &PlayerSkinController::onProfileReady);
        connect(m_store, &PlayerSkinStore::textureReady, this, &PlayerSkinController::onTextureReady);
        connect(m_store, &PlayerSkinStore::failed, this, &PlayerSkinController::onStoreFailed);
    }
    if (m_randomStore != nullptr) {
        connect(m_randomStore, &RandomSkinStore::skinReady, this,
                &PlayerSkinController::onRandomSkinReady);
        connect(m_randomStore, &RandomSkinStore::failed, this,
                &PlayerSkinController::onRandomStoreFailed);
    }
}

bool PlayerSkinController::offlineIdentity() {
    // 判据只有一个：**当前账号本身**是不是一个离线账号（不是"主页上某个框勾没勾"，
    // 那个开关 已经去掉了，见 AccountStore）。离线账号的 uuid 是
    // md5("OfflinePlayer:" + 名字)，所以"同名 = 同一个身份"。
    //
    // 读的是**盘上的活动槽**（AccountStore 那几个函数都是静态的、直接读写盘），不是
    // AuthManager 里那份内存副本 —— 两者在"切账号"那一刻会短暂不一致（activate() 先写盘，
    // 紧接着才调 setOfflinePlayer() / reloadStoredAccount()），而盘上那份才是权威。
    // 没有当前账号（从没登录、或者刚把当前那个删掉）时 activeUuid() 是空串，isOffline()
    // 对空串回 false —— 那种状态是"未登录"，不是"离线"。
    //
    // NOTE: 这一位是**全仓的唯一出处**（`MainWindow::offlineIdentity()` 只是一句转手）：
    // 底栏那行身份、启动时要不要令牌、外观这条路联不联网，三处各判一次就会出现
    // "底栏说离线、启动时却去要凭据"。它是静态的，所以判它不需要一个 controller 实例。
    return AccountStore::isOffline(AccountStore::activeUuid());
}

PlayerSkinController::HintFacts PlayerSkinController::hintFacts() const {
    HintFacts facts;
    facts.offline = offlineIdentity();
    facts.loggedIn = m_auth != nullptr && m_auth->loggedIn();
    facts.hasSkin = !m_skinPath.isEmpty();
    facts.hasCape = !m_capePath.isEmpty();
    facts.fetching = m_skinFetchInFlight;
    facts.offlineDefaultSkinVersionId = m_offlineDefaultSkinVersion;
    facts.offlineCustomSkin = m_offlineCustomSkin;
    return facts;
}

QStringList PlayerSkinController::offlineDefaultSkinCacheVersions(
    const QString &currentVersionId, const QString &lastLaunchedVersionId) const {
    // 猜"当初按哪个实例算的那张官方默认皮肤"，**按可能性从高到低**：
    // · 上一轮真的算过的那一个（m_offlineDefaultSkinVersion）—— 最准的一个；
    // · 现在选中的那个实例 —— 用户切到谁身上就按谁算；
    // · 上次启动过的版本 —— 换过实例、或者那会儿没选中任何版本时的兜底。
    // 三档都空就是空表（那时候这条路直接落空，退回首字母方块）。
    // NOTE: 一个账号只要**当过**当前账号，它的那张图就躺在盘上，而这三种猜测里总有一个
    // 能命中 —— 这正是"面板里三个人的脸都能画出来"的来源。
    QStringList versions;
    const auto add = [&versions](const QString &versionId) {
        if (!versionId.isEmpty() && !versions.contains(versionId)) {
            versions.append(versionId);
        }
    };
    add(m_offlineDefaultSkinVersion);
    add(currentVersionId);
    add(lastLaunchedVersionId);
    return versions;
}

void PlayerSkinController::reload(const QString &versionId) {
    // 记住"这一轮按哪个实例算" —— 那条随机皮肤过期后要**再问一次**（见 onRandomSkinReady
    // 末尾），而那一刻手上没有任何"当前选中"可言：这一层看不见那两个下拉框。所以它只能是
    // 最近一次让我重走的人的版本，语义就是"现在选中的那个实例"（每一处会让它变的地方都会
    // 叫一次 reload()）。
    m_versionId = versionId;

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
    m_offlineCustomSkin = false;

    // ---------- 离线账号那一档：手动挑的 → 算出来的 → 随机挑的 ----------
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
        // ⓪ **用户自己挑的那张最优先**（「账号与外观」里那颗「换一张…」，见 OfflineSkin）。
        // 这是一句明确的产品次序：手动挑过 = 用户说了算，游戏规矩算出来的那张只是"没有
        // 手动挑过时"的默认值。它不像官方默认皮肤那样跟着实例走 —— 换版本不该把用户
        // 亲自挑的那张换掉。
        const QString accountUuid = AccountStore::activeUuid();
        const QString manualPath = OfflineSkin::skinPathFor(accountUuid);
        if (!manualPath.isEmpty() && QFile::exists(manualPath)) {
            m_offlineCustomSkin = true;
            // 图是从盘上直接读的，不是"在路上"。
            m_skinFetchInFlight = false;
            applyPlayerSkin(manualPath, QString(),
                            OfflineSkin::loadModel(accountUuid) == QStringLiteral("slim"));
            emit hintChanged();
            return;
        }

        // ① 先算"游戏会给这个名字分配哪一张官方默认皮肤"（见 DefaultSkin）：那个"随机"其实
        // 是确定的，图就在**本地那个版本的客户端 JAR** 里，一个字节都不用下。同一个
        // (账号, 版本) 算过一次之后连 JAR 都不碰（缓存命中 = 两次 QFile::exists）。
        //
        // 名字取的是**进 --username 的那一个**（m_auth->playerName()）而不是
        // AccountStore 里那份存档：游戏是拿前者算 uuid 的，差一个字符就是另一张脸。
        // 版本取的是当前选中的那个实例 —— 换版本这张脸真的会变（两套规矩算出来的不是
        // 同一张，见 DefaultSkin.hpp 文件头），所以换版本时外面会重叫一次 reload()。
        QString defaultPath;
        bool defaultSlim = false;
        QString defaultReason;
        const DefaultSkin::Result resolveResult = DefaultSkin::resolve(
            m_auth->playerName(), versionId, &defaultPath, &defaultSlim, &defaultReason);
        if (resolveResult == DefaultSkin::Result::Resolved) {
            m_offlineDefaultSkinVersion = versionId;
            // 这张图**不是一份档案**（它没有地址、离线账号也没有披风），所以 m_skinProfile
            // 保持空着：上一轮若有别人的贴图回来，地址对不上就丢掉了（见 onTextureReady）。
            // NOTE: 这条路上 m_skinFetchInFlight 一直是 false —— 图是从盘上（或 JAR 里）
            // 直接拿的，不是"在路上"，那句话不该说"正在获取"。
            applyPlayerSkin(defaultPath, QString(), defaultSlim);
            return;
        }
        // Unavailable 是**常态**：没装任何版本、或者那个目录里没有客户端 JAR（不是原版
        // 客户端，第三方安装器做的薄壳 JAR 就长那样）—— 都不值得报。Failed 才是真出错
        // （读 JAR 失败、写不出去），那种留一行日志，不然"怎么老是随机那张"无从查起。
        if (resolveResult == DefaultSkin::Result::Failed) {
            emit logMessage(tr("算离线默认皮肤失败：%1").arg(defaultReason));
        }

        // ② 算不出来才走"随机挑一张"。
        //
        // 身份用的是上面那个 accountUuid（AccountStore 里那个），**不是** m_auth->playerUuid()
        // —— 后者对离线账号返回空串（它的 m_uuid 被清掉了）。离线账号的 uuid 是
        // md5("OfflinePlayer:" + 名字)，同名就是同一个 uuid，所以同一个人换不掉那张脸。
        const QString chosen = RandomSkinStore::loadChosenSkinUrl(accountUuid);
        if (chosen.isEmpty()) {
            // 还没挑过（第一次用这个账号）→ 要一页去。回来之后 onRandomSkinReady()
            // 接着走下面这段。这一格点亮是为了让那句话说"正在获取"。
            m_skinFetchInFlight = true;
            // 把**发请求时这位是谁**记下来。回来的时候用户可能已经切到另一个
            // 离线账号了，而那两个都是"离线身份" —— 只判 offlineIdentity() 分不出来，
            // 详见 onRandomSkinReady() 里那段。
            // NOTE: 只在**真的发出去了**的时候才记（fetchRandomSkin() 那个返回值）。已经有
            // 一趟在飞时这一趟什么都没开始，改掉这一位等于把上一趟的结果认到这一位
            // 名下 —— 那正是要防的那个错（切账号时这条路必被走到）。
            if (m_randomStore->fetchRandomSkin()) {
                m_randomSkinRequestUuid = accountUuid;
            }
        } else {
            // 挑过了，还是那一张。**当成一份档案用** —— 于是下不下得来、存哪、怎么画
            // 全是现成那条路（requestTexture → fetchTexture → onTextureReady）。
            // 离线账号没有披风，所以 capeUrl 留空。
            m_skinProfile = PlayerSkinProfile{chosen, QString(), QStringLiteral("classic")};
            requestTexture(chosen, /*allowDownload=*/true);
        }
        emit hintChanged();
        return;
    }

    if (!m_auth->loggedIn()) {
        // 没登录就没有 uuid，连问都不用问（问也问不出东西来）。那句话现在是
        // "未登录，显示的是默认外观"，由**开头那次 applyPlayerSkin()** 顺带写好了
        // —— 那一步发回来的 skinChanged 会让主窗口重挑一次。这里不再发（搬出来之前
        // 那一版这里也是不刷的）。
        return;
    }
    const QString uuid = m_auth->playerUuid();
    // 当前账号是离线账号的意思是"别联网"（启动时走的就是离线登录），这时候还发一个外观
    // 请求是自相矛盾的 —— 所以整条联网路径都跳过，只认已经躺在缓存里的那一份。
    //
    // NOTE: 判据从"勾选框勾没勾"换成了 offlineIdentity()。而且这一档现在**必然**同时是
    // "没有登录态"（setOfflinePlayer() 把 uuid 和令牌一起清了），上面那句 !loggedIn()
    // 其实已经先返回了 —— 留着它是为了这条路自己读起来是完整的，不靠上面的早退。
    const bool offline = offlineIdentity();

    // 1) 盘上那份档案：开窗那一刻零网络把**上次**的样子画出来。贴图是内容寻址的，
    // 命中缓存时 fetchTexture() 是**同步**发信号回来的，所以这一句之后就画好了。
    PlayerSkinProfile cached;
    if (PlayerSkinStore::loadCachedProfile(uuid, &cached)) {
        m_skinProfile = cached;
        requestTexture(cached.skinUrl, /*allowDownload=*/!offline);
        requestTexture(cached.capeUrl, /*allowDownload=*/!offline);
    }

    // 2) 后台刷一次档案：用户在游戏里换了皮肤，重开启动器就能看到。回来那份和手上
    // 这份一样时 onProfileReady() 什么都不做。
    if (!offline) {
        m_store->refreshForUuid(uuid);
    }
    emit hintChanged();
}

bool PlayerSkinController::requestTexture(const QString &url, bool allowDownload) {
    if (url.isEmpty()) {
        // 这个人没有披风（多数账号都没有）。**常态**，不是错误，也不是失败。
        return false;
    }
    if (QFile::exists(PlayerSkinStore::cachePathForUrl(url))) {
        // 命中缓存：fetchTexture() 会同步发 textureReady 回来，所以这一句之后那张
        // 贴图已经在屏幕上了。
        m_store->fetchTexture(url);
        return true;
    }
    if (allowDownload) {
        // 这一张要真的去下载了 —— 那句话得说"正在获取"，而不是"取不到"。
        m_skinFetchInFlight = true;
        m_store->fetchTexture(url);
    }
    return false;
}

void PlayerSkinController::onProfileReady(const QString &uuid, const PlayerSkinProfile &profile) {
    // 回来的时候身份可能已经变了：用户在请求飞着的时候换了账号、删掉了那个账号，或者
    // 登出了。**不是同一个身份就整份丢掉** —— 这份档案是发请求时那个人的，照着它落盘
    // 会把 A 的脸写进 profile-<B>.json（那份文件跨重启生效：下次开窗先按它画出 A 的
    // 外观，直到下一次刷新成功才纠正），照着它画就是"切了账号，脸还是上一个人的"。
    //
    // NOTE: 和 onRandomSkinReady() 那道判据是同一件事 —— 都是"发请求时那位 == 现在这位"。
    // 区别在判据从哪来：这条是信号自己带着身份回来的（uuid 就在形参里），随机皮肤
    // 那条没有，只好在发请求时自己记一份（m_randomSkinRequestUuid）。
    // 判据取 uuidKey()：它和 refreshForUuid() 里用的是同一个规整函数，所以"带不带横线"
    // 不会让这一比较失败。
    if (uuid.isEmpty() || uuid != PlayerSkinStore::uuidKey(m_auth->playerUuid())) {
        return;
    }
    const bool sameAsBefore = profile.sameAs(m_skinProfile);
    const QString previousCapeUrl = m_skinProfile.capeUrl;
    m_skinProfile = profile;
    if (sameAsBefore && !m_skinPath.isEmpty()) {
        // 手上的就是这一份，而且已经在屏幕上 → 这次刷新**完全看不见**：不重下贴图、
        // 不重画、那句话也不动。用户在游戏里没换皮肤时，绝大多数刷新都走这一条。
        m_skinFetchInFlight = false;
        emit hintChanged();
        return;
    }

    QString error;
    // 落盘用**信号带回来的那个身份**，不用 m_auth 里当下那个（上面那道闸之后
    // 它俩本来就相等了 —— 写 uuid 是让"这份是谁的"在读者眼里只有一处出处）。
    if (!PlayerSkinStore::saveProfile(uuid, profile, &error)) {
        // 存不下来不影响这一次显示（贴图照样下、照样画），只是下次开窗要先重取一遍。
        emit logMessage(tr("存玩家外观档案失败：%1").arg(error));
    }
    // 披风那一格只在**地址真的换了**的时候才作废。
    //
    // 作废是必须的：新档案里没有披风时（用户在游戏里把披风摘了），留着的那条旧路径会让
    // 下一趟 applyPlayerSkin(skin, m_capePath) 把上一套的披风又画回去，而且再没有哪一趟
    // 会来纠正它 —— 新的披风那一张根本不会到达。
    //
    // 但**不能一律清掉**：披风先到、皮肤还在路上的时候（见 onTextureReady）那一格是
    // "记忆"，不是"现在画着的东西"，清了就等于把刚记住的那张丢了 —— 于是同一个 bug 换个
    // 位置又出现一次。所以判据是"地址换没换"，而不是"档案换没换"。
    //
    // 皮肤那一格不用管：它下一趟必被新贴图覆盖，留着还能让"新披风先到"那一趟有皮肤可配。
    if (profile.capeUrl != previousCapeUrl) {
        m_capePath.clear();
    }
    requestTexture(profile.skinUrl, /*allowDownload=*/true);
    requestTexture(profile.capeUrl, /*allowDownload=*/true);
    emit hintChanged();
}

void PlayerSkinController::onTextureReady(const QString &url, const QString &path) {
    // 两张贴图是**分开**回来的（皮肤一张、披风一张），所以这里做的是"把这一张记到它
    // 该去的那一格，再用手上这两格重画一遍" —— 谁先到都能画。
    const bool slim = m_skinProfile.variant == QStringLiteral("slim");
    if (!url.isEmpty() && url == m_skinProfile.skinUrl) {
        // 皮肤到手了：不再是"正在获取"。就算这张图后面载不进来，也该由那句
        // "取不到…"来说，而不是一直停在"正在获取"。
        m_skinFetchInFlight = false;
        // 随机挑的那一张**到这里才记账**（ 修）：能走到这一句就说明字节已经在
        // 盘上了（这个信号的契约就是"本地就绪"，fetchTexture() 里那几条失败路一条都不发它），
        // 而那条记录是永不失效的 —— 记一条"图上没有"的地址，等于把这个账号钉死在重下同
        // 一个失败地址上。挑的那一刻只把它放进了 m_pendingRandomSkin*，见 onRandomSkinReady()。
        // NOTE: 记的是**挑的时候那个账号**（不是当下这个）：用户在这几百毫秒里换了账号时，
        // 这张图仍然是那个人挑的那一张，记在他名下才对（记错人就没用了 —— 下次
        // 没人会去读它）。两个都对得上才记，所以中途切过账号的情况这里天然放过。
        if (!m_pendingRandomSkinUrl.isEmpty() && url == m_pendingRandomSkinUrl) {
            QString error;
            if (!RandomSkinStore::saveChosenSkinUrl(m_pendingRandomSkinUuid, url, &error)) {
                // 记不下来不影响这一次显示（这一趟照样画得出来），只是下次开启动器会重挑一张。
                emit logMessage(tr("记下随机皮肤失败：%1").arg(error));
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
            // 每一趟都停在默认外观那句话上。 真机上就是这个现象。
            //
            // 所以这里只**记住**，不画：皮肤那一趟（上面那个分支）会带上 m_capePath
            // 一起画出来。这一小段时间里屏幕上是上一套外观（或者默认外观），不闪。
            m_capePath = path;
            // 皮肤那一趟**根本不会来**的时候（这个人的档案里没有皮肤那一格 ——
            // Mojang 的档案里"只有披风、没有 SKIN"是可能的），这一位必须在这儿落回 false：
            // 它的意思是"还有图在下"，而下完的就是手上这一张、皮肤那一张从没被请求过。
            // 不落的话那句「正在获取…」会一直挂着 —— 没有任何一条路会再来纠正它（皮肤
            // 那个分支永远不会执行、也不会失败，因为没有请求发出去）。
            // NOTE: 皮肤在路上的时候**不能**落（那才是正常的一小段），所以判据是
            // "档案里根本没有皮肤地址"，而不是"m_skinPath 还空着"。
            // NOTE: 这里**故意不发 `hintChanged()`** —— 搬出来之前那一版也没刷那句话
            // （那句话一直是靠"画一次"顺带刷的，而这一趟不画）。也就是说"档案里
            // 只有披风"那种人的「正在获取…」会一直挂着 —— 这是**搬之前就有的**，
            // 不是这次拆出来的；拆的时候原样留着，等哪一版专门修那句话时一起改。
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

void PlayerSkinController::onRandomSkinReady(const QString &skinUrl) {
    // 回来的时候身份可能已经变了：用户在这几百毫秒里换了账号、删掉了那个离线账号，
    // 或者干脆登录成正式账号了。**不是同一个离线身份就丢掉** —— 记下去等于把 A 的脸
    // 记到 B 名下，而且再也不会有人来纠正它（那份记录是"永不失效"的）。
    //
    // 修：这里原来只有 `if (!offlineIdentity()) return;` 这一道，而它回答的
    // 是"现在这位**是不是**离线账号"，**不是**"是不是**刚才那一位**" —— 从离线 A 切到
    // 离线 B 之后（两个都满足那一句，而且这是最常见的切法）A 那一趟的结果照样走得下去：
    // · m_pendingRandomSkinUuid 被写成 **B**（下面那句读的是"当下"），
    // · 贴图到了以后 onTextureReady() 就把 A 挑的那张记进 `random-<B>.txt`；
    // · 而 A 名下永远不会有记录 ⇒ A 每次开启动器都重挑一张、脸一直变（那份记录存在的
    // 全部意义正是"挑一次就定住"）。
    // 判据换成"发请求时那位 == 现在这位"：`m_randomSkinRequestUuid` 是发请求那一刻写的
    // （见 reload() 里那一句），两个都是 AccountStore 给的同一个规整 uuid，直接比字符串
    // 就行。
    //
    // NOTE: **没记过账的那一趟（这一位跑到现在一个请求都没发过）按当下这位收下**。
    // 两种收法是"当下这位"和"发请求时那位"的差别，而没有记账就没有"发请求时那位"
    // 可言 —— 那一趟的来路只能是"盘上已经记着一行，于是这一位从没挑过"（见
    // reload()：有记录就走那条路，一个请求都不发），这时候它跟"当下这位"本来就
    // 是同一位。生产里回不来"没记账却有结果"这种组合（信号只有 fetchRandomSkin()
    // 会发，而它一发就把那一位记上了），所以这一条不放松任何东西 —— 它挡的只是
    // "把空串当成一个身份去比"。mw_check 的 AF5/AF6/AF7 就是直接踢这个槽函数来演
    // "挑好的那张回来了"，那几趟正落在这个组合上。
    const bool requesterKnown = !m_randomSkinRequestUuid.isEmpty();
    if (!offlineIdentity() ||
        (requesterKnown && AccountStore::activeUuid() != m_randomSkinRequestUuid)) {
        // 丢掉之后**再问一次**。这一趟是替上一位挑的，而当下这一位（离线）
        // 可能正等着自己那一张 —— 切账号时外面那一趟确实发出去了，但再切一次就会被
        // RandomSkinStore::fetchRandomSkin() 的 m_inFlight 挡回来（那一趟返回 false，
        // 所以 m_randomSkinRequestUuid 没被改写，这里才认得出"过期"）。此刻 m_inFlight
        // 已经落回 false（reply 的回调第一句就清掉了，而这里是它直接发信号叫进来的），
        // 所以这一句能真的发出去。
        // NOTE: 只在**现在这位是离线账号**时才补 —— 正版账号的皮肤走官方那条路，
        // 切过去的时候已经有另一条路在刷了，不该由这里再捅一次。
        // NOTE: 重叫的是 reload()（原来叫的是 startPlayerSkinLoad()），参数取
        // m_versionId —— 那一位就是"最近一次让我重走时选中的那个实例"，语义和
        // 它原来自己读下拉框拿到的那个值是同一个。
        if (offlineIdentity()) {
            reload(m_versionId);
        }
        return;
    }
    // 挑中的这一张**先记在内存里**，等贴图真的落在盘上再写那行记录（见
    // onTextureReady 里那一段）。原来是在这里就写的，而这一句之前图一个字节都还没到：
    // requestTexture() 会因为超时、非 200、或者"不是一张 64 宽的皮肤 PNG"
    // （PlayerSkinStore::fetchTexture）**一个字节都不落盘**，而记录已经写下了 —— 那条记录
    // 是永不失效的，于是这个账号从此每次都去重下同一条永远失败的地址、永远停在默认外观，
    // 而且**这条随机记录没有界面入口**（要手动删 skins/random-<uuid>.txt —— 注意「恢复默认
    // 外观」那颗按钮清的是 manual-<uuid>.png，见 OfflineSkin，不是这一行）。记账的语义本来是
    // "这个人挑过哪一张、那张是好的"，那就该等它好。
    m_pendingRandomSkinUrl = skinUrl;
    m_pendingRandomSkinUuid = AccountStore::activeUuid();
    m_skinProfile = PlayerSkinProfile{skinUrl, QString(), QStringLiteral("classic")};
    // 走那条公用的路。**缓存命中时它是同步的**（fetchTexture 命中就同步发 textureReady），
    // 所以挑第二张的时候这一句之后屏幕上就已经换好了；真要下的时候它会自己把
    // m_skinFetchInFlight 点起来。
    requestTexture(skinUrl, /*allowDownload=*/true);
    emit hintChanged();
}

void PlayerSkinController::onStoreFailed(const QString &message) {
    // 外观是**装饰**：取不到就静默退回默认外观。这是全仓唯一一处不弹窗的错误，
    // 理由（别的错误都让用户做不成他想做的事，而这个只是不好看）写在
    // PlayerSkinStore.hpp 文件头。但日志得留一行，否则真出问题无从查起。
    emit logMessage(tr("取玩家皮肤失败：%1").arg(message));
    m_skinFetchInFlight = false;
    if (m_skinPath.isEmpty()) {
        // 已经有真皮肤在屏幕上时**不改那句话**：多数失败是**披风**那一张（皮肤早就画
        // 好了），这时候说"取不到皮肤"是错的。
        emit hintChanged();
    }
}

void PlayerSkinController::onRandomStoreFailed(const QString &message) {
    // 「挑一张」没挑到（见 RandomSkinStore）。和上面那条分开：这一条失败的是"挑一张"
    // 这件事，跟某个人的档案无关，所以那句话的前缀是"取随机皮肤失败"。
    emit logMessage(tr("取随机皮肤失败：%1").arg(message));
    m_skinFetchInFlight = false;
    emit hintChanged();
}

void PlayerSkinController::applyPlayerSkin(const QString &skinPath, const QString &capePath,
                                           bool slim) {
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
        // 主窗口按"手上有没有真皮肤"重新挑一档（skinChanged 那一趟它会顺带刷）。
        skin = SkinModel::defaultSteveSkin();
        cape = QImage();
        m_skinPath.clear();
        m_capePath.clear();
    } else {
        // 只记**真的载入成功了的**路径：所以 m_skinPath 为空就等于"现在显示的是默认外观"。
        m_skinPath = skinPath;
        m_capePath = cape.isNull() ? QString() : capePath;
    }

    // 剩下的都是"把这套外观摆到那几个窗口上""图省着算一次""那句话重挑一遍"—— 三件事都
    // 只有主窗口做得到（那些控件是它的），所以这里发一张**已经画得出来的图**出去。
    // NOTE: 那张图必须是**已经载进来的**（上面退回默认外观那一步保证了这一点）—— 主窗口
    // 那边是直接把这张图交给控件的，它没有"载不进来怎么办"这一说。
    emit skinChanged(skin, cape, slim);
}
