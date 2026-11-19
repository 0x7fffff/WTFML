#include "Support.hpp"
int MwLoginCompletionChecks() {
    // ------------------------------------------------------------------
    // 对应一次真实的用户报告：「浏览器里显示登录成功，启动器一点反应都没有」。
    // 根因有两条：那条链只走了一半（只拿了微软令牌，没接着换 Minecraft 令牌和档案 ——
    // 所以既没有能启动游戏的令牌，也没有玩家名），而界面上只有 loginSucceeded 接了、
    // 刷出来的还是那个占位名 "Player"，于是三处都是死的。
    //
    // NOTE: 真机上的**完整链路**要真的登一次账户才验得了（这一节不连网）。能在这儿钉住的
    // 是：错误话术、失败原因的传递、以及信号到达界面之后那几下 —— 也就是"启动器
    // 有没有反应"里除网络之外的全部。
    {
        // ---- XErr 话术 ----
        // 这些码比 INT_MAX 还大（2148916233），收成 int 就是负数、一律走兜底 —— 这一条
        // 就是钉这个（写成 int 的话，下面第一条会以"兜底那句里带着码"的方式挂掉）。
        check(AuthManager::xstsErrorMessage(2148916233LL)
                  .contains(QStringLiteral("尚未创建 Xbox 档案")),
              "2148916233（没注册过 Xbox 账户）给的是那条该怎么办，不是兜底",
              AuthManager::xstsErrorMessage(2148916233LL));
        check(AuthManager::xstsErrorMessage(2148916238LL).contains(QStringLiteral("家庭组")),
              "2148916238（未成年账户）说的是家庭组", AuthManager::xstsErrorMessage(2148916238LL));
        check(AuthManager::xstsErrorMessage(2148916235LL) !=
                  AuthManager::xstsErrorMessage(2148916227LL),
              "两个不同的码不会给出同一句话（说明真的按码分了）");
        check(AuthManager::xstsErrorMessage(123456LL).contains(QStringLiteral("123456")),
              "认不出来的码走兜底，并且把码带上（用户能拿它去搜）",
              AuthManager::xstsErrorMessage(123456LL));

        // ---- login_with_xbox 的 403：应用没被 Mojang 批准 ----
        // 真机上真正卡住登录的就是这一步，正文原文见下面第一条（照抄的）。
        // 通用那套说法只报得出"HTTP 403"，而这句话是用户唯一能照着做的东西。
        const QString appRegBody = QStringLiteral(
            "Invalid app registration, see https://aka.ms/AppRegInfo for more information");
        check(AuthManager::appRegistrationHint(appRegBody)
                  .contains(QStringLiteral("mce-reviewappid")),
              "403 的正文认得出是「应用没被批准」，并给出申请入口",
              AuthManager::appRegistrationHint(appRegBody));
        check(AuthManager::appRegistrationHint(QStringLiteral("invalid app REGISTRATION"))
                  .contains(QStringLiteral("应用程序（客户端）ID")),
              "大小写不敏感，并指出需要提交应用程序（客户端）ID",
              AuthManager::appRegistrationHint(QStringLiteral("invalid app REGISTRATION")));
        check(AuthManager::appRegistrationHint(QStringLiteral("some other failure")).isEmpty(),
              "别的错误正文不认（给空串，调用方走通用那套）",
              AuthManager::appRegistrationHint(QStringLiteral("some other failure")));
        // NOTE: 通用那套（httpFailureText 把正文里的 errorMessage / Message 接进话术）是
        // 本文件的内部函数，验证工程看不见，这一条只能靠上面"认得出/认不出"间接盯住。
        // ---- 刷新令牌被微软拒绝：正文怎么翻成人话 ----
        // 实测原文（照抄，只删掉了具体的 id）—— 用户报"正版登录炸了"就是它：
        // 盘上那张 refresh_token 是**另一个 Azure 应用**签发的，而刷新带的是本应用的
        // client id，微软把 refresh token 绑在签发它的应用上，所以这张令牌换不出来。
        const QString aad70000 = QStringLiteral(
            "AADSTS70000: The provided value for the input parameter 'refresh_token' or "
            "'assertion' is not valid. The token was issued for a different client id "
            "Trace ID: 00000000-0000-0000-0000-000000000000 "
            "Correlation ID: 11111111-1111-1111-1111-111111111111 "
            "Timestamp: 2026-09-22 08:54:53Z");
        const QString aad70000Text = AuthManager::oauthErrorText(
            QStringLiteral("invalid_grant"), aad70000, QStringLiteral("刷新微软令牌失败"));
        check(aad70000Text.contains(QStringLiteral("重新登录")),
              "AADSTS70000 给出的是「重新登录」这条路，不是微软原文",
              aad70000Text);
        check(!aad70000Text.contains(QStringLiteral("Trace ID")) &&
                  !aad70000Text.contains(QStringLiteral("Correlation ID")) &&
                  !aad70000Text.contains(QStringLiteral("Timestamp")),
              "把微软那截 Trace ID / Correlation ID / Timestamp 尾巴切掉了",
              aad70000Text);
        check(aad70000Text.contains(QStringLiteral("AADSTS70000")),
              "错误码本身留着（用户能拿它去搜）", aad70000Text);

        // 别的 OAuth 失败（invalid_grant 的各种其它成因）仍走通用那句：用户能看懂的那半
        // 描述留着，尾巴照样切掉 —— 这一条钉的是"不要把 description 原样端出去"。
        const QString otherGrant = AuthManager::oauthErrorText(
            QStringLiteral("invalid_grant"),
            QStringLiteral("AADSTS50020: User account does not exist in tenant. "
                           "Trace ID: 22222222-2222-2222-2222-222222222222"),
            QStringLiteral("刷新微软令牌失败"));
        check(otherGrant.contains(QStringLiteral("does not exist in tenant")) &&
                  !otherGrant.contains(QStringLiteral("Trace ID")),
              "认不出来的 AADSTS 码：描述留着、尾巴切掉", otherGrant);
        check(AuthManager::oauthErrorText(QString(),
                                          QStringLiteral("some body"),
                                          QStringLiteral("刷新微软令牌失败")).isEmpty(),
              "error 字段是空的时候给空串（调用方走通用那套，别在这儿编一句话）");


        // ---- 档案里的 UUID → 游戏命令行的 --uuid ----
        // 档案（profile.id）给的是不带横线的 32 位十六进制，游戏要的是标准写法。
        // 这个转换只有真登录一次才拿得到真值，所以在这里单独钉住。
        check(AuthManager::dashedUuid(QStringLiteral("069a79f444e94726a5befca90e38aaf5")) ==
                  QStringLiteral("069a79f4-44e9-4726-a5be-fca90e38aaf5"),
              "不带横线的 UUID 补成标准写法",
              AuthManager::dashedUuid(QStringLiteral("069a79f444e94726a5befca90e38aaf5")));
        check(AuthManager::dashedUuid(QStringLiteral("069a79f4-44e9-4726-a5be-fca90e38aaf5")) ==
                  QStringLiteral("069a79f4-44e9-4726-a5be-fca90e38aaf5"),
              "本来就带横线的不会被补成两截");
        check(AuthManager::dashedUuid(QStringLiteral("nope")).isEmpty(),
              "长度不对的一律给空串（宁可不给，也不拼一个看起来像的假 UUID）",
              AuthManager::dashedUuid(QStringLiteral("nope")));

        // ---- 没登录过时，启动路径要能说清"为什么没有令牌" ----
        // 光返回空串的话，界面只能说一句"请先登录"；而"从没登录过"和"登录过期了换不出来"
        // 是两回事（后者照那句提示做也还是登不上）。沙盒里的 QSettings 是干净的。
        {
            AuthManager probe;
            QString reason;
            const QString token = probe.loadSavedToken(&reason);
            check(token.isEmpty(), "没登录过时拿不到启动令牌", token);
            check(reason.contains(QStringLiteral("未找到完整的登录信息")),
                  "空令牌旁边带着原因（不是只有一句「请先登录」）", reason);
            check(!probe.loggedIn(), "没登录过时 loggedIn() 是 false");
            check(probe.playerName() == QStringLiteral("Player"),
                  "没登录过时的用户名仍是 Player", probe.playerName());
            // 空串是让启动那一层知道"该退回全零 UUID"的信号（见 LauncherManager 那边）
            check(probe.playerUuid().isEmpty(), "没登录过时 playerUuid() 是空串（不是全零）",
                  probe.playerUuid());
        }

        // ---- 界面三态：点下去 / 成功 / 失败 ----
        {
            MainWindow w7;
            w7.show();
            QApplication::processEvents();

            // AuthManager 是 MainWindow 的子对象（new AuthManager(this)），按类型找得到。
            AuthManager *auth = w7.findChild<AuthManager *>();
            QLabel *account = byObjectName<QLabel>(&w7, MainWindowUI::accountLabelName());
            QLabel *accountType = byObjectName<QLabel>(&w7, MainWindowUI::accountTypeLabelName());
            QLabel *avatar = byObjectName<QLabel>(&w7, MainWindowUI::avatarLabelName());
            QPushButton *entry =
                byObjectName<QPushButton>(&w7, MainWindowUI::accountSkinSettingsButtonName());
            check(auth != nullptr && account != nullptr && accountType != nullptr &&
                      avatar != nullptr && entry != nullptr,
                  "第七个窗口里 AuthManager / 底栏两行 / 头像 / 设置页那个账号入口都找得到");
            if (auth != nullptr && account != nullptr && accountType != nullptr &&
                avatar != nullptr && entry != nullptr) {
                // 底栏的用户名只有一个来源：AuthManager::playerName()（不是信号参数、
                // 也不是界面自己存的副本）。这一条是后面"换成真名"那条断言的前提。
                check(account->text() == auth->playerName(),
                      "底栏的用户名就是 AuthManager::playerName()", account->text());
                // 第二行说的是"这次启动用哪种身份"：盘上一份凭据都没有的时候它有话直说
                // 「未登录」——写「离线登录」是骗人（那种状态点启动会被"请先登录"挡回来）。
                check(accountType->text() == MainWindowUI::accountTypeNotLoggedInText(),
                      "没登录过时底栏第二行照实说「未登录」", accountType->text());
                // 设置页那格是「账号与外观…」的入口。
                check(entry->text() == MainWindowUI::accountSkinButtonText() && entry->isEnabled(),
                      "设置页那格是「账号与外观…」的入口，一直点得动", entry->text());

                // 下面的三个信号就是 AuthManager 在真实流程里发的那三个（真实发点见
                // AuthManager::handleNewConnection）。手工触发它们，验的是**界面收下之后
                // 做了什么** —— 那正是"启动器没反应"这句话指的东西。
                QMetaObject::invokeMethod(auth, "loginStarted", Qt::DirectConnection);
                // 中间态（那个窗上的忙态）只有真的点一次才看得见 —— loginInFlight() 要真
                // 走了流程才为真，手工发信号改不了它。这里钉的是它的**依据**：底栏那两行
                // 完全由 AuthManager 自己的状态决定，界面不留第二份 —— 所以上面发了
                // loginStarted 之后它们**不该**变；要是变了，说明界面自己存了一份，
                // 而那一份在"超时"那条路上会和真的失配。
                // NOTE: 这里**不能**断言底栏会变成"正在登录"之类 —— 那种状态在界面上是
                // 那个窗自己的忙态（MainWindow::onLoginStarted() → setBusy），
                // 不在底栏；底栏只回答"这次用哪种身份"。
                check(account->text() == auth->playerName() &&
                          accountType->text() == MainWindowUI::accountTypeNotLoggedInText(),
                      "手工发一个 loginStarted 改不了底栏那两行（它们的依据只有 AuthManager）");

                // 成功：会弹一个模态窗（"已登录为…"），得安排人点掉它，否则卡死在这儿。
                dismissNextPrompt();
                QMetaObject::invokeMethod(auth, "loginSucceeded", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("Steve")));
                check(g_modalTitle == QStringLiteral("登录成功") &&
                          g_modalText.contains(QStringLiteral("Steve")),
                      "成功之后启动器弹窗说登的是哪个账户（不是静默）",
                      g_modalTitle + QStringLiteral(" / ") + g_modalText);
                check(!g_modalWasWarning, "成功那条用的是信息框，不是警告框");
                // NOTE: 那条信号只带一个名字，改不了 AuthManager 自己的状态（loggedIn()、
                // playerName() 都是"这次会话真的换到过 Minecraft 令牌"这件事），
                // ——所以底栏**不该**跟着变。这一条钉的正是"名字只有一个来源"：
                // 界面要是自己拿信号参数去改那一行，这里就会红。
                check(account->text() == auth->playerName() &&
                          accountType->text() == MainWindowUI::accountTypeNotLoggedInText(),
                      "界面不拿信号参数去改底栏（那两行只认 AuthManager 自己的状态）",
                      account->text() + QStringLiteral(" / ") + accountType->text());

                // 失败：必须让用户看见 —— 没有接收方时用户在启动器上一个字都看不到，
                // 只有浏览器里那个页面说了句"登录失败"。
                dismissNextPrompt();
                QMetaObject::invokeMethod(auth, "loginFailed", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("这个微软账户还没有注册过 Xbox 账户。")));
                check(g_modalWasWarning && g_modalText.contains(QStringLiteral("Xbox 账户")),
                      "失败之后弹的是警告框，正文就是 AuthManager 给的那句原因",
                      g_modalTitle + QStringLiteral(" / ") + g_modalText);
                check(account->text() == auth->playerName(),
                      "失败之后底栏还是原来的样子（失败不许把界面改一半）", account->text());
            }
        }
    }
    return 0;
}


