/**
 * @file MainWindowAccount.cpp
 * @brief 「账号与外观」那一扇窗的接线：登录 / 切号 / 登出 / 上传皮肤 / 挑披风，以及底栏两块面板里账号那一行。
 *
 * 【为什么单独一个翻译单元】
 *
 * 这一片的主语是"现在是谁、他长什么样"：`offlineIdentity()`（离线身份的唯一出处）、
 * `refreshAccountSkin()`（把账号那一扇窗的预览与提示铺一遍）、`onAccountSkinButtonClicked()`
 * 那一扇的开关，以及登录成功 / 失败 / 切换 / 移除之后各自要刷新什么。皮肤贴图本身落到窗口
 * 上那几处在 `MainWindowSkin.cpp`；这里只管"谁"和"动作之后刷新什么"。
 *
 * 【底栏面板那一支为什么也在这里】
 *
 * `onBottomBarPanelAction()` 是底栏两块上拉面板**唯一**的入口，一个函数同时分派账号那一行与
 * 「正在跑的游戏」那一行 —— 拆不开。它夹在账号这一串动作中间，且账号那一支（登出 / 选择）
 * 正是这里的主角；跑着的游戏那一支只是把编号转给 `stopGameSession()` /
 * `openInstanceSettings()` / `GameSessionController`。`openGameLogForSession()` 是日志窗那个
 * 兼容入口（底栏与验证工程按名字调用），跟着入口一起走。
 *
 * 【盘上的账不在这一层】
 *
 * 多账号列表在 `auth/AccountStore`，凭据字节格式在 `auth/TokenStore`，
 * 界面的样子在 `ui/AccountSkinDialog`。这一层只接线，不读注册表、不排版。
 */

#include "MainWindow.hpp"

#include "..\ui\AccountSkinDialog.hpp"
#include "..\ui\InlineMessage.hpp"
#include "..\ui\MainWindowUI.hpp"
#include "..\ui\OverlayHost.hpp"
#include "auth/AccountStore.hpp"
#include "auth/AuthManager.hpp"
#include "auth/SkinApi.hpp"
#include "controllers/GameSessionController.hpp"
#include "controllers/MainWindowViewData.hpp"
#include "controllers/PlayerSkinController.hpp"
#include "launcher/LauncherManager.hpp"
#include "manage/OfflineSkin.hpp"
#include "util/ImageDecode.hpp"

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>

using namespace MainWindowViewData;

// ============================ 「账号与外观」那个模块 ============================
//
// 三件事在这个模块里的分工（写清楚免得下次改错地方）：
// - **界面的样子**全在 ui/AccountSkinDialog.{hpp,cpp}（它不 include 任何 src/ 下的头）；
// - **盘上的账**在 src/auth/AccountStore（多账号列表）+ TokenStore（凭据字节格式）；
// - **下面这一片**是接线：谁来读谁、失败怎么报、哪个动作之后要刷新什么。
// 所以这里没有任何"界面布局"的代码，那边也没有任何"读注册表"的代码。

void MainWindow::onAccountSkinButtonClicked() {
    // 覆盖层已经开着时**不能直接 return** —— 屏幕上什么都不发生：用户在别的选项卡上逛，
    // 实例设置那一页还收着占住那一层；这时按「账号与外观」看着就是"按钮点坏了"。
    // 所以并进同一套规矩：**静默收掉占着的那一层，再开**（让位那一段的唯一出处是
    // `runWhenOverlayFree()`）。
    runWhenOverlayFree([this] {
        setupAccountSkinDialog();
        OverlayHost *overlay = m_ui->overlay();
        if (overlay == nullptr) {
            return;  // 没有覆盖层就开不了（主界面建的时候就有，正常到不了这一支）
        }
        // 页面把内容区整块盖掉，不是独立窗口。登录那一步会把浏览器推到前台、用户要
        // 在两边来回切 —— 界面一直是活的，浏览器本来也是外面的独立窗，谁都拦不着它。
        //
        // NOTE: 顺序是**先摆出来、再刷新**。refreshAccountSkin() 最后那一段会去拉披风列表
        // （真联网，最长能等十几秒），倒过来做的话头一次点这个入口就是"什么也没发生"
        // —— 页面还没画出来，那个请求已经在跑了，用户只会以为按钮点坏了。
        // 这里排一发队：开页是**非阻塞**的（`OverlayHost::showPage()`），返回之后主循环照常
        // 转，这一发就排在"页面已经在屏幕上"之后跑。
        QMetaObject::invokeMethod(this, [this] { refreshAccountSkin(); }, Qt::QueuedConnection);
        // 那一发刷新排在页面摆上来之后；先把「皮肤」那一行摆对，免得开页的第一帧
        // 露的是正版才有的「上传皮肤…」（离线账号按下去只会失败）。
        m_accountSkinDialog->setOfflineSkinState(offlineIdentity(),
                                                 OfflineSkin::hasSkin(AccountStore::activeUuid()));
        if (!overlay->showPage(m_accountSkinDialog, AccountSkinDialog::windowTitle())) {
            appendLog(tr("账号与外观那一页没开成：覆盖层刚被另一页占上了。"));
        }
    });
}

void MainWindow::setupAccountSkinDialog() {
    if (m_accountSkinDialog != nullptr) {
        return;
    }
    m_accountSkinDialog = new AccountSkinDialog(this);
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
    connect(m_accountSkinDialog, &AccountSkinDialog::offlineSkinPickRequested, this,
            &MainWindow::onOfflineSkinPickRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::offlineSkinResetRequested, this,
            &MainWindow::onOfflineSkinResetRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::capeSelectRequested, this,
            &MainWindow::onCapeSelectRequested);
    connect(m_accountSkinDialog, &AccountSkinDialog::hideCapeRequested, this,
            &MainWindow::onCapeHideRequested);
}

bool MainWindow::offlineIdentity() const {
    // 判据（连那段"为什么读盘不读内存副本"的道理）在
    // `PlayerSkinController::offlineIdentity()` 里 —— 外观那一套要按它决定"这一趟能不能
    // 联网"，而这一层还有十几处读它，所以留一句转手。**唯一出处仍然是那一个静态函数**：
    // 谁要判"现在是不是离线身份"都该走它，不要在这儿另写一份。
    return PlayerSkinController::offlineIdentity();
}

QString MainWindow::accountSkinStatusText() const {
    // 当前账号是离线账号时先说这一句：名字就在上面那一列里躺着，比任何泛泛的说法都具体。
    //
    // NOTE: **离线 = 当前账号本身是离线账号**，没有第二种"离线"。
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
    // 主页那个小人、底栏头像、那个窗里的 3D 预览**都只在
    // startPlayerSkinLoad() → applyPlayerSkin() 里更新**，这一句 return 若把预览也挡掉，
    // 没点开过「账号与外观…」的会话里切账号根本不换脸，一直停在上一个人的脸上
    // （从"未登录"切过去时停的就是那张兜底占位图）。
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
    // 同步请求已经回来了（下面第 4 步会再离开这个方法去联网）。
    m_accountSkinDialog->setBusy(false);
    m_accountSkinDialog->setStatusText(accountSkinStatusText());
    // 「皮肤」那一行露哪一档（正版：上传；离线：换一张 / 恢复默认）。判据和下面那句
    // `if (offlineIdentity())` 同一个出处，只是这里要的是"有没有自己挑过"那一格。
    {
        const QString activeUuid = AccountStore::activeUuid();
        m_accountSkinDialog->setOfflineSkinState(
            offlineIdentity(), OfflineSkin::hasSkin(activeUuid));
    }

    // 3) 3D 预览走**和主页同一条路**（同一份缓存、同一次后台刷新）：applyPlayerSkin()
    // 会把同一份贴图同时喂给主页那个小人和这个窗里的预览。
    //
    // NOTE: 这一句**不保证不联网**：离线账号第一次进来时它要 MineSkin 那一页随机皮肤
    // （见 startPlayerSkinLoad() 里 offlineIdentity() 那一档）。挑过一次之后就只在盘上
    // 读那一行。
    // 这也正是这个窗**只在离线账号那一档开**的原因（见 onAccountSkinButtonClicked()
    // 里那段）：正版那一档开窗会顺带拉披风，那才是真联网的那一下。
    startPlayerSkinLoad();

    // 4) 披风列表要令牌，是这一整块里唯一会真的去联网的地方（过期令牌也是在这里顺带刷新的）。
    if (offlineIdentity()) {
        // 当前是离线账号 = "别联网"，而拉披风列表正是联网。跳过。
        m_accountSkinDialog->setCapes({});
        return;
    }
    // 为什么非得另立一条判据：上面那句 `m_accountSkinDialog == nullptr` 分不出"关掉了" ——
    // 这个对话框是**建一次就留着**的（setupAccountSkinDialog() 见非空就返回，父对象是本
    // 窗口），页面关掉只是把它藏起来，指针永远不为空。于是"开过一次账号页、之后从右面板
    // 切账号"这一条路上，每一次切换都会在**没有人看着的那个页面上**点起忙态、然后同步
    // 等一次网络往返（loadSavedToken 还可能真去换一次令牌）：用户看到的是"切个账号整个
    // 启动器卡几秒"，而卡住的理由（「正在获取披风列表…」）摆在一个藏起来的页面里。
    //
    // NOTE: 判据是"**现在真摆在眼前**"（isVisible），和 OverlayHost 里 retargetCoveredPage()
    // 那条 NOTE 同一条规矩：页面收在别的选项卡上时它不在屏幕上，一样跳过。
    // NOTE: 上半截（账号列表 / 状态行 / 3D 预览）**照样刷了** —— 那些不联网。披风列表会在
    // 用户真的打开这一页时补上：开页那条路必走一次本函数（见 onAccountSkinButtonClicked），
    // 而那时页面已经在屏幕上了。
    if (!m_accountSkinDialog->isVisible()) {
        return;
    }
    // 下面这两下是**同步**的（补忙态）：loadSavedToken() 可能要真去换一次令牌、
    // fetchOwnedCapes() 一定发一次 GET —— 两个函数都是"调完才返回"，于是点「刷新」之后的
    // 这几秒钟里 UI 线程一直在这两行里面出不来：窗不重绘、什么都按不动，Windows 过一会儿
    // 还会给标题挂上"(无响应)"。
    //
    // NOTE: 到这儿必须先把忙态点起来并 processEvents 一次：第 2 步的 setBusy(false)
    // 之后到这里谁也没把忙态点起来的话，用户看到的是一个画着旧内容、点什么都没反应的窗。
    // 忙态 + 一次 processEvents（排掉用户输入）和 enterSyncWait() 是同一个手法：
    // 进去之前先把话说出来。下面每条返回路上都要松开它。
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
    // （offlineIdentity()）。
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
    // NOTE: 离线账号那一档**不是"整条路都不联网"**：披风确实不拉
    // （离线账号没有披风可拉），但这个账号**还没挑过**
    // 随机皮肤时，startPlayerSkinLoad() 会去 MineSkin 要一页列表（见 RandomSkinStore）。
    // 挑过一次之后就只在盘上读那一行 —— 本地命中，一个包都不发。
    refreshAccountSkin();
}

void MainWindow::onBottomBarPanelAction(bool account, const QString &key, int action) {
    if (account) {
        if (action != kAccountPanelUseAction) {
            return;  // 右面板那一行只有一颗按钮；别的编号是接线错了，什么都不做
        }
        if (key == kAccountPanelManageKey) {
            onAccountSkinButtonClicked();
            return;
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
        case kRunningPanelLogAction:
            openGameLogForSession(key.toInt());
            return;
        default:
            return;
    }
}

void MainWindow::openGameLogForSession(int sessionId) {
    // 保留这个兼容入口：底栏和验证工程按这个名字调用。
    m_gameSessionController->openGameLogForSession(sessionId);
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
        // 别替失败编一句"现在还是登录着"：可能一个键都没清掉，也可能清掉了一半（见
        // AccountStore::logout() 的 R13）。说"没能清干净"在任何一种情况下都是事实。
        InlineMessage::warning(m_ui->overlay(), tr("退出登录失败"),
                             tr("盘上的登录状态没能清干净。\n\n再点一次「登出」试试；一直不行的话，"
                                "可能是这台机器上的启动器配置写不进去。"));
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
    // NOTE: 这里刻意**不用 loadSavedToken()**：它只在"已经过期"时才去换，而要的是
    // "剩不到 6 小时就换"（见 AuthManager::tokenRefreshWindowSecs 那段）。
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
    // 都不是这里能修的，只能重新登录一次 —— 所以那句话得指出**下一步**，不能只说
    // "刷新失败"。
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
    if (!InlineMessage::question(m_ui->overlay(), tr("删除账号"), question, tr("是"), tr("否"), true,
                                            /*destructive=*/true)) {
        return;
    }
    QString removeError;
    if (!AccountStore::remove(uuid, &removeError)) {
        // 两种失败：列表里本来就没有它；或者列表项删了、但活动槽那几个键没清干净
        // （R13，removeError 里写着原因）。
        InlineMessage::warning(m_ui->overlay(), tr("删除账号失败"),
                             removeError.isEmpty() ? tr("这个账号不在已保存的列表里。")
                                                   : removeError);
        return;
    }
    if (wasCurrent) {
        m_authManager->reloadStoredAccount();
        updateBottomBar();
    }
    appendLog(tr("已删除账号「%1」。").arg(name));
    refreshAccountSkin();
}

namespace {

/// 用户挑的皮肤图**文件**的字节上限（一张合法的皮肤不过几 KB；这里挡的是"把一整部
/// 电影当皮肤选进来"那种 —— 读进来的每个字节都要占内存）。
constexpr qint64 kMaxPickedSkinBytes = 16LL * 1024 * 1024;

/// 皮肤图的解码上限。皮肤只可能是 64×64 / 64×32（真正的判据是
/// `SkinApi::isAcceptableSkinSize()`），256 只是给将来放宽留余量；这一道挡的是
/// "照一个荒唐的声明去分配"（见 `ImageDecode::Limits`）。
constexpr ImageDecode::Limits kPickedSkinLimits{256, 256 * 256};

} // namespace

void MainWindow::onSkinUploadRequested(const QString &path, const QString &variant) {
    // 先把文件读出来。**尺寸那条规矩只在 SkinApi::isAcceptableSkinSize() 里**（唯一出处），
    // 这里不自己再判一遍。但"读不出来"得在这里说 —— 它连尺寸都没有。
    QFile file(path);
    const QString fileName = QFileInfo(path).fileName();
    // 文件本身也封一个顶：一张合法皮肤不过几 KB，读进来的每一字节都要占内存。
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0
        || file.size() > kMaxPickedSkinBytes) {
        InlineMessage::warning(m_ui->overlay(), tr("上传皮肤失败"),
                             tr("读不到「%1」这个文件（或者它大得不像一张皮肤图）。")
                                 .arg(fileName));
        return;
    }
    const QByteArray png = file.readAll();
    file.close();
    // 宽高从**同一份字节**里拿（不再按路径读第二遍）。皮肤图是用户挑的本机文件，
    // 也可能来自群里那个"皮肤包" —— 先问尺寸、超了不解（见 `ImageDecode::Limits`）。
    const QImage image = ImageDecode::decode(png, kPickedSkinLimits);
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

void MainWindow::onOfflineSkinPickRequested(const QString &path, const QString &variant) {
    // 离线账号那一颗。**尺寸那条规矩仍然只在 SkinApi::isAcceptableSkinSize() 里**
    // （和正版上传那条路同一个出处，见 onSkinUploadRequested）。
    const QString uuid = AccountStore::activeUuid();
    if (!AccountStore::isOffline(uuid)) {
        // 界面上这一颗只在离线档露着；真走到这里说明状态已经变了（比如另一处刚切了账号）。
        InlineMessage::warning(m_ui->overlay(), tr("换皮肤失败"),
                               tr("当前不是离线账号，这一张没有换。"));
        return;
    }
    const QString fileName = QFileInfo(path).fileName();
    // 和正版上传那条路同一个上限、同一个理由（那是一张用户挑的本机文件，而
    // "皮肤包"里的图不一定是好心的）。
    QImage image;
    QFile skinFile(path);
    if (skinFile.open(QIODevice::ReadOnly) && skinFile.size() > 0
        && skinFile.size() <= kMaxPickedSkinBytes) {
        image = ImageDecode::decode(skinFile.readAll(), kPickedSkinLimits);
        skinFile.close();
    }
    if (image.isNull()) {
        InlineMessage::warning(m_ui->overlay(), tr("换皮肤失败"),
                               tr("读不到「%1」这张图（或者它不是一张能读出来的图片）。")
                                   .arg(fileName));
        return;
    }
    if (!SkinApi::isAcceptableSkinSize(image.width(), image.height())) {
        InlineMessage::warning(m_ui->overlay(), tr("换皮肤失败"),
                               tr("皮肤图必须是 64×64（新格式）或者 64×32（老格式），这张是 "
                                  "%1×%2。")
                                   .arg(image.width())
                                   .arg(image.height()));
        return;
    }
    QString error;
    if (!OfflineSkin::save(uuid, image, variant, &error)) {
        InlineMessage::warning(m_ui->overlay(), tr("换皮肤失败"), error);
        return;
    }
    appendLog(tr("已换成自己挑的皮肤（模型 %1）。").arg(OfflineSkin::loadModel(uuid)));
    // 重走一遍：主页的小人、底栏的头像、这一页的 3D 预览都是同一份图（见
    // PlayerSkinController::reload() 里手动那张排在最前面）。
    refreshAccountSkin();
}

void MainWindow::onOfflineSkinResetRequested() {
    const QString uuid = AccountStore::activeUuid();
    if (!AccountStore::isOffline(uuid)) {
        return;  // 这一颗只在离线档露着，同上
    }
    if (!OfflineSkin::clear(uuid)) {
        InlineMessage::warning(m_ui->overlay(), tr("恢复默认外观失败"),
                               tr("删不掉之前挑的那张皮肤，检查一下 data 目录的权限。"));
        return;
    }
    appendLog(tr("已恢复默认外观。"));
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
    // （AuthManager 自己），以后再多一个入口起这个流程，界面照样跟得上。
    // 超时兜底也在那边 —— 浏览器再也不回来时它会发一次 loginFailed()，
    // 忙态由 onLoginFailed() 松开。
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
    // 登录失败还可能是"**上一个在线账号已经不在盘上了**"。
    // 登录那一趟开头会把活动槽里的 Minecraft 凭据清掉（防"旧 uuid 配新刷新令牌"的错配，
    // 见 AuthManager::exchangeAuthorizationCode），而 AuthManager 在失败那条路上已经把
    // 内存里那份身份跟着盘清了（见 handleNewConnection 里那段）。**界面得跟着画一遍**：
    // 底栏那个名字是上一次 updateBottomBar() 摆上去的，不重画就会一直停在上一个人的名字
    // 上，而底栏第二行"未登录"的判断（offlineIdentity()，读盘）与启动那句判据都说"没人"。
    //
    // 条件写全是为了**别动离线账号那一档**：那时候盘上什么都没变（离线账号没有凭据），
    // 底栏该继续写离线那个名字。而"从来没登录过 + 登录失败"也会走进来一次，那一趟是
    // 幂等的：画出来还是原来那张默认脸、占位名字。
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
