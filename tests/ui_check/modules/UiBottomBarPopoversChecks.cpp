#include "Support.hpp"
int UiBottomBarPopoversChecks(QMainWindow &window, MainWindowUI &ui, QTabWidget * &tabs) {

    {
        // 要求：「这个文本这里和另一边头像那里都做成个按钮，点击以后能向上展开更多信息
        // 的那种（像官启那样的）」； 又定下两块面板装什么：「左边那个列出进行中
        // 的实例…右边一个电源图标一个齿轮图标，分别是关闭实例和进入实例设置，右边是快速
        // 切换账号，列出最后登录的三个账户，离线的右侧按钮写 选择，正版的右侧写 登录」。
        // 这一节量的是**面板机制**（谁装在谁里面、弹在哪、怎么关、点了行尾按钮回来什么）
        // 加上**一行长什么样**（图标 + 两行字 + 行尾几颗按钮），内容由这里自己喂进去 ——
        // ui/ 不许 include src/，真数据（哪个实例、最近哪三个账号）是 MainWindow 的事，
        // 那几条在 mw_check 里量。
        QPushButton *runBtn = ui.getRunStateButton();
        QPushButton *accBtn = ui.getAccountButton();
        // 账号那一块面板的锚点**换成了主页右上那张账号卡片** —— 底栏那颗
        // 账号按钮 `hide()` 之后点不到它了（要求：「这个的对应功能继承到右上角那个
        // 按钮去」）。`accBtn` 继续留着量"底栏那组兼容控件还在、宽度还钉着"，
        // 而凡是"点开/收起那一块"的地方、以及量面板几何时的锚点，都走 `accCard`。
        // NOTE: 卡片在主页那一页上，几何是**那一页当前摆着的时候**才算得准（量之前那一节
        // 会把主页切到最前面）。
        QPushButton *accCard = ui.getAccountSkinButton();
        QLabel *runState = ui.getRunStateLabel();
        QLabel *runDetail = ui.getRunStateDetailLabel();
        QLabel *avatar = ui.getAvatarLabel();
        QProgressBar *progress = ui.getProgressBar();
        check(runBtn != nullptr && accBtn != nullptr && accCard != nullptr,
              "I4 底栏那两颗按钮 + 主页那张账号卡片都在");
        if (runBtn != nullptr && accBtn != nullptr && runState != nullptr &&
            runDetail != nullptr && avatar != nullptr && progress != nullptr) {
            // ① 整格是**一颗按钮**：那两行字（右边是头像那组）都装在按钮里面。
            // 这条同时是"挤短进度条"那个 bug 的护栏 —— 宽度钉在**按钮**上，
            // 里面有没有字都一样宽（隐藏的控件在布局里宽度是 0）。
            check(runState->parentWidget() == runBtn && runDetail->parentWidget() == runBtn,
                  "I4 左边那两行字装在那颗按钮里",
                  runState->parentWidget() != nullptr ? runState->parentWidget()->objectName()
                                                      : QStringLiteral("<null>"));
            check(avatar->parentWidget() == accBtn, "I4 头像装在那颗按钮里",
                  avatar->parentWidget() != nullptr ? avatar->parentWidget()->objectName()
                                                    : QStringLiteral("<null>"));
            // 是**贴着按钮左边缘**摆的：靠右摆会在头像左边空出一大段（实测 115 px ——
            // 那一格宽 208、内容只有九十来 px），「头像那里的左侧有一段空白」就是它。
            // 箭头照旧钉在按钮右边缘。
            check(avatar->x() == 0, "I4 头像贴着按钮左边缘（左边不留空）",
                  QStringLiteral("头像 x=%1 / 按钮宽 %2").arg(avatar->x()).arg(accBtn->width()));
            QLabel *accChevron = accBtn->findChild<QLabel *>(MainWindowUI::bottomBarChevronName());
            check(accChevron != nullptr &&
                      accChevron->x() + accChevron->width() == accBtn->width(),
                  "I4 右那颗的箭头仍然钉在按钮右边缘（空档留在姓名格里）",
                  accChevron != nullptr
                      ? QStringLiteral("箭头右边 %1 / 按钮宽 %2")
                            .arg(accChevron->x() + accChevron->width()).arg(accBtn->width())
                      : QStringLiteral("<没找到箭头>"));

            // ② 两颗按钮各带一个**朝上**的箭头。朝向不看"像不像"，量画出来的像素：
            // 朝上那个顶行是**尖的**（一小撮、居中），朝下那个顶行是张开的两个点。
            const QColor ink(0x12, 0x34, 0x56);
            const int chevSize = MainWindowUI::bottomBarChevronSize();
            const auto paintedSpan = [](const QImage &img, bool top) {
                if (img.isNull()) {
                    return 0;
                }
                const int step = top ? 1 : -1;
                for (int y = top ? 0 : img.height() - 1; y >= 0 && y < img.height(); y += step) {
                    int lo = -1;
                    int hi = -1;
                    int n = 0;
                    for (int x = 0; x < img.width(); ++x) {
                        if (qAlpha(img.pixel(x, y)) > 40) {
                            if (lo < 0) {
                                lo = x;
                            }
                            hi = x;
                            ++n;
                        }
                    }
                    if (n > 0) {
                        return hi - lo + 1;  // 这一行画到的宽度
                    }
                }
                return 0;
            };
            const QImage upImg = VersionIcons::chevronUp(chevSize, ink).toImage();
            const QImage downImg = VersionIcons::chevron(true, chevSize, ink).toImage();
            const int upTop = paintedSpan(upImg, true);
            const int upBottom = paintedSpan(upImg, false);
            const int downTop = paintedSpan(downImg, true);
            check(upTop > 0 && upTop < downTop,
                  "I4 按钮上那个箭头是**朝上**的（顶行是尖的，不是版本树里那个张开的 V）",
                  QStringLiteral("朝上顶行宽 %1 / 朝下顶行宽 %2").arg(upTop).arg(downTop));
            check(upTop > 0 && upTop < upBottom, "I4 上窄下宽 = 尖朝上",
                  QStringLiteral("顶 %1 / 底 %2").arg(upTop).arg(upBottom));
            // 三格各带一个箭头：左边"几份在跑"、
            // 中间"任务摘要"、右边"账号"，三块都是往上弹的面板，所以箭头长得一样、名字也
            // 共用（`makeChevronLabel()` 是唯一出处）。数的时候按**整窗**数 —— 这也就顺手
            // 钉住了"三颗都得挂上"。
            check(window.findChildren<QLabel *>(MainWindowUI::bottomBarChevronName()).size() == 2,
                  "I4 「在跑的实例」和「账号」各带一个箭头（任务卡换成进度环，不再挂箭头）",
                  QStringLiteral("找到 %1 个")
                      .arg(window.findChildren<QLabel *>(MainWindowUI::bottomBarChevronName()).size()));

            // ③ 点一下 —— 向上弹出一块面板。先把进度条亮出来，好量"弹面板不动底栏"。
            //
            // 面板**吃的是外面喂进来的行**（ui/ 不许 include src/，数据在 MainWindow 手里，
            // 见 setBottomBarPanelContent()）。所以这一节自己装一个假 provider 喂两行：
            // 第 0 行是"一份实例"（图标 + 两行字 + 行尾两颗图标按钮，其中一颗灰掉），
            // 第 1 行是"什么都没有"那种说明行（没图标、没按钮）。
            // 真数据（哪个实例在跑、最近哪三个账号、当前账号那颗写什么）是 MainWindow 的事，
            // 在 mw_check 里量 —— 那边有真会话和真账号。
            QList<bool> firedPanels;
            QList<QString> firedKeys;
            QList<int> firedActions;
            ui.setBottomBarPanelContent(
                [](bool account) -> QList<MainWindowUI::PanelRow> {
                    if (account) {
                        // 右边那块（账号）：当前账号那颗按钮写「登出」、**能点**，
                        // 另一个账号那颗写「选择」/「登录」。
                        QList<MainWindowUI::PanelRow> accounts;
                        MainWindowUI::PanelRow mine;
                        mine.key = QStringLiteral("uuid-mine");
                        mine.title = QStringLiteral("LoserKugua");
                        mine.subtitle = MainWindowUI::bottomBarPanelOnlineAccountText();
                        MainWindowUI::PanelAction current;
                        current.text = MainWindowUI::bottomBarPanelLogoutText();
                        current.tooltip = MainWindowUI::bottomBarPanelLogoutTip();
                        mine.actions << current;
                        accounts << mine;

                        MainWindowUI::PanelRow other;
                        other.key = QStringLiteral("uuid-other");
                        other.title = QStringLiteral("选手二号");
                        other.subtitle = MainWindowUI::bottomBarPanelOfflineAccountText();
                        MainWindowUI::PanelAction select;
                        select.text = MainWindowUI::bottomBarPanelSelectText();
                        other.actions << select;
                        accounts << other;
                        return accounts;
                    }
                    QList<MainWindowUI::PanelRow> rows;
                    MainWindowUI::PanelRow game;
                    game.key = QStringLiteral("sess-7");
                    game.icon = VersionIcons::versionChannelMark(
                        0, MainWindowUI::bottomBarPanelIconSize());
                    game.title = QStringLiteral("我的整合包-1.20.1-NeoForge");
                    game.subtitle = QStringLiteral("已运行 12 分钟");
                    const QColor ink(Qt::black);
                    MainWindowUI::PanelAction power;
                    power.icon = VersionIcons::power(MainWindowUI::bottomBarPanelActionIconSize(),
                                                     ink);
                    power.tooltip = QStringLiteral("关闭这个实例");
                    MainWindowUI::PanelAction gear;
                    gear.icon = VersionIcons::gear(MainWindowUI::bottomBarPanelActionIconSize(),
                                                   ink);
                    gear.tooltip = QStringLiteral("实例设置");
                    gear.enabled = false;  // 灰掉一颗，"灰的那颗点不动"才有得量
                    // 第三颗是**字的**（运行中实例弹层"打开日志/停止"那一套，
                    // 见 MainWindowUI::bottomBarPanelLogText()）。摆成和产品里一样的
                    // 「图标 / 图标 / 字」三颗，那条"文字那颗按字撑宽、图标那两颗是正方形"
                    // 才有得量。
                    MainWindowUI::PanelAction log;
                    log.text = MainWindowUI::bottomBarPanelLogText();
                    log.tooltip = QStringLiteral("打开这一场游戏的日志窗");
                    game.actions << power << gear << log;
                    rows << game;

                    MainWindowUI::PanelRow note;
                    note.title = MainWindowUI::bottomBarPanelNoRunningTitle();
                    note.subtitle = MainWindowUI::bottomBarPanelNoRunningNote();
                    note.placeholder = true;
                    rows << note;
                    return rows;
                },
                [&firedPanels, &firedKeys, &firedActions](bool account, const QString &key,
                                                          int action) {
                    firedPanels << account;
                    firedKeys << key;
                    firedActions << action;
                });
            progress->show();
            ui.getStatusLabel()->show();
            QApplication::processEvents();
            const int barWidthBefore = progress->width();
            runBtn->click();
            QApplication::processEvents();
            QWidget *panel = window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName());
            check(panel != nullptr && panel->isVisible(), "I4 点左边那颗按钮弹出了一块面板");
            if (panel != nullptr) {
                check(panel->windowFlags().testFlag(Qt::Popup),
                      "I4 它是 Qt::Popup（点界面别处自动收回去、不抢主窗口焦点）",
                      QStringLiteral("flags = 0x%1").arg(uint(panel->windowFlags()), 0, 16));
                // 行控件按**行号**找得回来（objectName 由 bottomBarPanelRowName() 给），
                // 行里的两行字各有各的名字 —— 验证工程不用数"有几个 QLabel"这种脆弱判据。
                QWidget *rowWidget0 =
                    panel->findChild<QWidget *>(MainWindowUI::bottomBarPanelRowName(0));
                QWidget *rowWidget1 =
                    panel->findChild<QWidget *>(MainWindowUI::bottomBarPanelRowName(1));
                QLabel *title0 =
                    panel->findChild<QLabel *>(MainWindowUI::bottomBarPanelRowTitleName(0));
                QLabel *subtitle0 =
                    panel->findChild<QLabel *>(MainWindowUI::bottomBarPanelRowSubtitleName(0));
                QLabel *title1 =
                    panel->findChild<QLabel *>(MainWindowUI::bottomBarPanelRowTitleName(1));
                QLabel *subtitle1 =
                    panel->findChild<QLabel *>(MainWindowUI::bottomBarPanelRowSubtitleName(1));
                check(rowWidget0 != nullptr && rowWidget1 != nullptr,
                      "I4 喂进去两行，面板里就两行（行控件按行号找得回来）",
                      QStringLiteral("第 0 行 %1 / 第 1 行 %2")
                          .arg(rowWidget0 != nullptr ? QStringLiteral("在") : QStringLiteral("没找到"),
                               rowWidget1 != nullptr ? QStringLiteral("在") : QStringLiteral("没找到")));
                // 「一行一个会话」：主行字是实例名，次行字是**这一份跑了多久**（用户
                // 原话：「一行一个会话，有时长是好主意，做进去」）。
                check(title0 != nullptr &&
                          title0->text() == QStringLiteral("我的整合包-1.20.1-NeoForge"),
                      "I4 第一行的主行字就是喂进去的实例名",
                      title0 != nullptr ? title0->text() : QStringLiteral("<没找到>"));
                check(subtitle0 != nullptr && subtitle0->text() == QStringLiteral("已运行 12 分钟"),
                      "I4 第一行的次行字是那一份的已运行时长（一行一个会话，各带各的时长）",
                      subtitle0 != nullptr ? subtitle0->text() : QStringLiteral("<没找到>"));
                check(title1 != nullptr &&
                          title1->text() == MainWindowUI::bottomBarPanelNoRunningTitle() &&
                          subtitle1 != nullptr &&
                          subtitle1->text() == MainWindowUI::bottomBarPanelNoRunningNote(),
                      "I4 第二行是那句说明（主页那句「现在没有实例在运行」和它的下一句）",
                      title1 != nullptr && subtitle1 != nullptr
                          ? title1->text() + " / " + subtitle1->text()
                          : QStringLiteral("<没找到>"));
                // 每一行都得**装得下自己那行字**：面板要是只有一行那么高（用户报过
                // 「左右上拉只有一行的高度 文字…显示不全」），这些字会被压成半个字高。
                int squashed = 0;
                const QList<QLabel *> texts = {title0, subtitle0, title1, subtitle1};
                for (QLabel *line : texts) {
                    if (line != nullptr && line->height() < line->fontMetrics().height()) {
                        ++squashed;
                    }
                }
                check(squashed == 0 && title0 != nullptr && subtitle0 != nullptr &&
                          title1 != nullptr && subtitle1 != nullptr,
                      "I4 面板里每一行都装得下自己那行字（没被压扁）",
                      QStringLiteral("压扁了 %1 行").arg(squashed));
                // 那一列字**宽度钉死**：面板的宽度是"内容多宽就多宽"（popUp() 里
                // adjustSize()），跟着长实例名走的话，长名字会把面板顶出窗口。
                check(title0 != nullptr &&
                          title0->width() == MainWindowUI::bottomBarPanelTextWidth(),
                      "I4 那一列字的宽度钉死在 bottomBarPanelTextWidth()",
                      title0 != nullptr
                          ? QStringLiteral("宽 %1 / 该占 %2")
                                .arg(title0->width())
                                .arg(MainWindowUI::bottomBarPanelTextWidth())
                          : QStringLiteral("<没找到>"));
                // 行尾那几颗按钮：实例那一行是**两颗图标**（电源 = 关闭这一份实例、
                // 齿轮 = 进它的实例设置），说明那一行一颗都没有。
                const int actionSize = MainWindowUI::bottomBarPanelActionSize();
                QPushButton *powerBtn = panel->findChild<QPushButton *>(
                    MainWindowUI::bottomBarPanelRowActionName(0, 0));
                QPushButton *gearBtn = panel->findChild<QPushButton *>(
                    MainWindowUI::bottomBarPanelRowActionName(0, 1));
                QPushButton *logBtn = panel->findChild<QPushButton *>(
                    MainWindowUI::bottomBarPanelRowActionName(0, 2));
                check(powerBtn != nullptr && gearBtn != nullptr && logBtn != nullptr,
                      "I4 实例那一行行尾有三颗按钮（第 0 = 关闭 / 第 1 = 实例设置 / "
                      "第 2 = 打开日志窗）");
                check(powerBtn != nullptr && powerBtn->size() == QSize(actionSize, actionSize),
                      "I4 行尾按钮的大小钉死在 bottomBarPanelActionSize()",
                      powerBtn != nullptr
                          ? QStringLiteral("%1x%2 / 该是 %3x%3")
                                .arg(powerBtn->width())
                                .arg(powerBtn->height())
                                .arg(actionSize)
                          : QStringLiteral("<没找到>"));
                check(powerBtn != nullptr && powerBtn->text().isEmpty() &&
                          !powerBtn->icon().isNull(),
                      "I4 行尾画的是**图标**不是字（给了 icon 就不画 text）");
                check(powerBtn != nullptr && !powerBtn->toolTip().isEmpty(),
                      "I4 电源那颗带一句能看懂的说明（一个电源符号说不清点了会把游戏杀掉）",
                      powerBtn != nullptr ? powerBtn->toolTip() : QStringLiteral("<没找到>"));
                check(gearBtn != nullptr && !gearBtn->isEnabled(),
                      "I4 灰掉的那一颗真的点不动（照 PanelAction::enabled 走，右面板"
                      "「当前」那颗也靠它）");
                // 「打开日志」那颗：**没给图标就给字**，而且按字撑宽 ——
                // 图标那两颗是钉死的正方形（22），文字这颗要是也被压成 22 宽，「日志」两个字
                // 就只剩半个。两条合起来钉住 "PanelAction 的两种形态各走各的那一支"。
                check(logBtn != nullptr && logBtn->icon().isNull() && !logBtn->text().isEmpty(),
                      "I4 第三颗画的是**字**不是图标（没给 icon 就走 text 那一支）",
                      logBtn != nullptr
                          ? QStringLiteral("text =「%1」/ icon 空 = %2")
                                .arg(logBtn->text(),
                                     logBtn->icon().isNull() ? QStringLiteral("是")
                                                             : QStringLiteral("不是"))
                          : QStringLiteral("<没找到>"));
                check(logBtn != nullptr && logBtn->text() == MainWindowUI::bottomBarPanelLogText(),
                      "I4 那颗上的字就是 bottomBarPanelLogText()（产品里那颗和它同一个出处）",
                      logBtn != nullptr ? logBtn->text() : QStringLiteral("<没找到>"));
                check(logBtn != nullptr && logBtn->width() > actionSize &&
                          logBtn->width() >= logBtn->sizeHint().width(),
                      "I4 文字那颗比图标那颗宽（按字撑开，没被压成图标那个正方形）",
                      logBtn != nullptr
                          ? QStringLiteral("宽 %1 / 图标那颗 %2 / 想要 %3")
                                .arg(logBtn->width())
                                .arg(actionSize)
                                .arg(logBtn->sizeHint().width())
                          : QStringLiteral("<没找到>"));
                // 三颗的**左右次序就是编号次序**（0 → 1 → 2）。面板是"编号 = 第几颗"认人的
                // （动作回调里那个下标），摆错位置的话用户按下去的是另一件事。
                check(powerBtn != nullptr && gearBtn != nullptr && logBtn != nullptr &&
                          powerBtn->x() < gearBtn->x() && gearBtn->x() < logBtn->x(),
                      "I4 三颗按钮从左到右就是 0/1/2（编号认人，摆错了按下去做的是别的事）",
                      logBtn != nullptr
                          ? QStringLiteral("x = %1 / %2 / %3")
                                .arg(powerBtn != nullptr ? powerBtn->x() : -1)
                                .arg(gearBtn != nullptr ? gearBtn->x() : -1)
                                .arg(logBtn->x())
                          : QStringLiteral("<没找到>"));
                check(panel->findChild<QPushButton *>(
                          MainWindowUI::bottomBarPanelRowActionName(1, 0)) == nullptr,
                      "I4 说明那一行行尾一颗按钮都没有");
                // 行首那块方块：给了图标的那一行画出来、而且是**正好那么大**（按
                // bottomBarPanelIconSize() 摆，尺寸不对就是被拉伸了或者面板被撑高）。
                // 没给图标的那一行（这里是"没有实例在运行"那句，真面板上还有账号那几行）
                // **一个图标控件都不该有** —— 留一格空白的话，两行字的左边缘就对不齐了。
                QLabel *icon0 =
                    panel->findChild<QLabel *>(MainWindowUI::bottomBarPanelRowIconName(0));
                check(icon0 != nullptr && !icon0->pixmap().isNull() &&
                          icon0->size() == QSize(MainWindowUI::bottomBarPanelIconSize(),
                                                 MainWindowUI::bottomBarPanelIconSize()),
                      "I4 行首那块方块画出来了、边长是 bottomBarPanelIconSize()",
                      icon0 != nullptr
                          ? QStringLiteral("%1x%2").arg(icon0->width()).arg(icon0->height())
                          : QStringLiteral("<没找到>"));
                check(panel->findChild<QLabel *>(MainWindowUI::bottomBarPanelRowIconName(1)) ==
                          nullptr,
                      "I4 没给图标的那一行连图标控件都没有（那一格不是「留白」）");
                // 那句"跑了多久"的四档话术。**纯函数**（MainWindowUI::runningDurationText），
                // 所以四档都能在这儿钉住 —— "已经跑了 3 天"那一档在真界面上等不出来。
                // 要求：「一行一个会话，有时长是好主意，做进去」。
                const qint64 base = QDateTime::currentMSecsSinceEpoch();
                check(MainWindowUI::runningDurationText(base, base + 59 * 1000) ==
                          QStringLiteral("已运行不到 1 分钟") &&
                          MainWindowUI::runningDurationText(base, base + 12 * 60 * 1000) ==
                              QStringLiteral("已运行 12 分钟") &&
                          MainWindowUI::runningDurationText(base, base + (2 * 60 + 7) * 60 * 1000) ==
                              QStringLiteral("已运行 2 小时 7 分钟") &&
                          MainWindowUI::runningDurationText(base,
                                                            base + (3 * 24 + 5) * 60 * 60 * 1000) ==
                              QStringLiteral("已运行 3 天 5 小时"),
                      "I4 时长四档话术：不到 1 分钟 / N 分钟 / N 小时 M 分钟 / N 天 M 小时",
                      QStringLiteral("%1 | %2 | %3 | %4")
                          .arg(MainWindowUI::runningDurationText(base, base + 59 * 1000),
                               MainWindowUI::runningDurationText(base, base + 12 * 60 * 1000),
                               MainWindowUI::runningDurationText(
                                   base, base + (2 * 60 + 7) * 60 * 1000),
                               MainWindowUI::runningDurationText(
                                   base, base + (3 * 24 + 5) * 60 * 60 * 1000)));
                // 整点那半句不缀：2 小时整就说"2 小时"，不是"2 小时 0 分钟"。
                check(MainWindowUI::runningDurationText(base, base + 120 * 60 * 1000) ==
                          QStringLiteral("已运行 2 小时"),
                      "I4 整点分钟不缀那半句（读起来不像机器写的）",
                      MainWindowUI::runningDurationText(base, base + 120 * 60 * 1000));
                // 几何：面板整个在按钮**上面**，左边缘贴按钮左边缘
                const QPoint runAnchor = runBtn->mapToGlobal(QPoint(0, 0));
                check(panel->y() + panel->height() <= runAnchor.y(),
                      "I4 面板在按钮上方（不是压在底栏上）",
                      QStringLiteral("面板底 %1 / 按钮顶 %2")
                          .arg(panel->y() + panel->height()).arg(runAnchor.y()));
                check(panel->x() == runAnchor.x(), "I4 左边那块左边缘贴按钮左边缘",
                      QStringLiteral("面板 x %1 / 按钮 x %2").arg(panel->x()).arg(runAnchor.x()));
                check(panel->width() >= runBtn->width(), "I4 面板不比按钮窄",
                      QStringLiteral("面板宽 %1 / 按钮宽 %2")
                          .arg(panel->width()).arg(runBtn->width()));
                // 弹面板**不许动底栏** —— 这一轮修的就是"弹出来把中间那根条挤动"。
                check(progress->width() == barWidthBefore,
                      "I4 弹出面板前后，中间那根进度条一点没动",
                      QStringLiteral("%1 px → %2 px").arg(barWidthBefore).arg(progress->width()));
                // Esc 关掉 + WA_DeleteOnClose：关完在窗口里就找不到了（不是藏起来）
                QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(panel, &esc);
                QApplication::processEvents();
                QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                check(window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName()) == nullptr,
                      "I4 Esc 关掉之后面板就没了（WA_DeleteOnClose 真删）");
            }
            // ④ 行尾那颗按钮：点一下要**先把面板收回去**，再把「哪一行是谁 + 第几颗」交回来。
            //
            // NOTE: 先收回去是硬要求 —— 这两个动作里有一个要弹模态窗（实例设置那个对话框），
            // 而面板是个 Qt::Popup，它按着鼠标键盘的 grab；模态窗开在一层抓着 grab
            // 的东西底下，在 Windows 上就是"窗出来了但点不动"。所以回调是**排到
            // 下一轮事件循环**去的（close() 只发了个关闭事件，面板要等这一轮走完
            // 才真的没了，当轮就弹模态窗等于没关）。
            // NOTE: 回来的身份是 PanelRow::key（左面板 = 会话号），**不是行号** ——
            // 面板弹出来到按钮被点之间隔着用户看那两眼，这中间列表是会变的，
            // 认行号就会"关掉另一份游戏"。
            firedPanels.clear();
            firedKeys.clear();
            firedActions.clear();
            runBtn->click();
            QApplication::processEvents();
            QWidget *panelAgain = window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName());
            QPushButton *powerAgain =
                panelAgain != nullptr
                    ? panelAgain->findChild<QPushButton *>(
                          MainWindowUI::bottomBarPanelRowActionName(0, 0))
                    : nullptr;
            check(powerAgain != nullptr, "I4 再开一次，量「点行尾按钮」那条的前提还在");
            if (powerAgain != nullptr) {
                powerAgain->click();
                check(firedKeys.isEmpty(),
                      "I4 点行尾那颗按钮：动作不**当场**回来（面板得先真的收回去）",
                      QStringLiteral("当场就回来了 %1 次").arg(firedKeys.size()));
                QApplication::processEvents();
                check(firedPanels == QList<bool>{false} &&
                          firedKeys == QList<QString>{QStringLiteral("sess-7")} &&
                          firedActions == QList<int>{0},
                      "I4 回来的是「哪一行是谁 + 第几颗」：key = 会话号，动作号 0（左面板 "
                      "account = false）",
                      QStringLiteral("account=%1 key=%2 action=%3")
                          .arg(firedPanels.value(0))
                          .arg(firedKeys.value(0))
                          .arg(firedActions.value(0)));
                QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                check(window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName()) == nullptr,
                      "I4 点完行尾按钮，面板已经收回去了");
            }
            // ④b **第三颗**（那颗文字按钮「日志」）点下去报的是编号 2，不是 0 —— "第几颗"
            // 是面板唯一认得出的身份（按钮上是图标还是字、写着什么，都不参与判断）。
            // 这条和上面那条第 0 颗的合起来说明"编号 = 位置"这件事两头都成立。
            firedPanels.clear();
            firedKeys.clear();
            firedActions.clear();
            runBtn->click();
            QApplication::processEvents();
            QWidget *panelText = window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName());
            QPushButton *logAgain =
                panelText != nullptr
                    ? panelText->findChild<QPushButton *>(
                          MainWindowUI::bottomBarPanelRowActionName(0, 2))
                    : nullptr;
            check(logAgain != nullptr, "I4 再开一次，那颗文字按钮还在原位（第 0 行第 2 颗）");
            if (logAgain != nullptr) {
                logAgain->click();
                QApplication::processEvents();
                check(firedPanels == QList<bool>{false} &&
                          firedKeys == QList<QString>{QStringLiteral("sess-7")} &&
                          firedActions == QList<int>{2},
                      "I4 点第三颗回来的是动作号 2（文字那颗和图标那两颗一样按位置认人）",
                      QStringLiteral("account=%1 key=%2 action=%3")
                          .arg(firedPanels.value(0))
                          .arg(firedKeys.value(0))
                          .arg(firedActions.value(0)));
                QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
            // ⑤ 开关：开着的时候再点**同一颗**按钮要**收回**（要求：
            // 「那个按钮，点开以后处于展开状态时再次点击按钮收回」）。
            // NOTE: 这一下 Qt 会把它重放给按钮（Qt::Popup 点外面自动关的时候专门补的，
            // 不然"菜单关掉之后再点不开"），所以按钮照旧收到 clicked —— 收回去
            // 靠的是接线那道"已经开着就不许再开"，不是靠 Qt 帮忙。
            runBtn->click();
            QApplication::processEvents();
            check(window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName()) != nullptr,
                  "I4 点一下弹出来（开关的第一下）");
            runBtn->click();
            QApplication::processEvents();
            QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            const QWidget *stillOpen =
                window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName());
            check(stillOpen == nullptr,
                  "I4 展开状态下再点同一颗按钮 = 收回（不是关了又立刻开一块新的）",
                  stillOpen != nullptr ? QStringLiteral("还留着一块 %1x%2，visible=%3")
                                             .arg(stillOpen->width())
                                             .arg(stillOpen->height())
                                             .arg(stillOpen->isVisible())
                                       : QString());
            runBtn->click();
            QApplication::processEvents();
            check(window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName()) != nullptr,
                  "I4 收回之后再点一下还能打开（开关是双向的）");
            // 右边那一块（锚点是**主页右上那张账号卡片**）：贴卡片**右**边缘
            // （面板比卡片宽的时候往左长，不出窗口），而且往下弹 —— 卡片贴着头顶，往上弹没地方。
            // 这会儿左边那块是开着的：点卡片要**这块收、那块开**，不能两块一起挂着。
            // NOTE: 真鼠标下这一下多半先被左那块（Qt::Popup）吃掉、只收不开（Qt 的规矩：
            // 点面板外面 = 关掉，这一下不往下传 —— 探针 popup_probe 实测如此），
            // 要再点一下才开；这里量的是"clicked 真落到按钮上时不许叠两块"。
            QWidget *leftOpen = window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName());
            check(leftOpen != nullptr && leftOpen->isVisible() &&
                      leftOpen->x() == runBtn->mapToGlobal(QPoint(0, 0)).x(),
                  "I4 换面之前左边那块开着（量下面那条的前提）");
            if (QTabWidget *tabs = ui.getTabWidget()) {
                tabs->setCurrentIndex(MainWindowUI::homeTabIndex());
                QApplication::processEvents();
            }
            accCard->click();
            QApplication::processEvents();
            QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QWidget *rightPanel = window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName());
            check(rightPanel != nullptr && rightPanel->isVisible(),
                  "I4 点右边那颗：这块收、那块开（一次点击就换过来）");
            check(window.findChildren<QWidget *>(MainWindowUI::bottomBarPanelName()).size() == 1,
                  "I4 换面之后窗口里只剩一块面板（旧的那块真关了）",
                  QStringLiteral("还剩 %1 块")
                      .arg(window.findChildren<QWidget *>(MainWindowUI::bottomBarPanelName()).size()));
            if (rightPanel != nullptr) {
                const QPoint accAnchor = accCard->mapToGlobal(QPoint(accCard->width(), 0));
                // 这一条量的是**整条规矩**（「窄窗口避让」）：
                // 「右边缘贴锚点右边缘」**且**「整块不许越过屏幕右边缘」。这两句会打架
                // —— 而它们在 **offscreen 那一趟真的打架**：那块虚拟屏只有 800x600，
                // 而主窗是按 900 摆的，右边那个锚点的全局 x 落在屏幕**外面**（888），
                // 于是面板被避让逻辑夹回屏幕里（右边缘 = 800），"贴着锚点"这句当场不成立。
                // NOTE: 所以期望值要按"锚点在不在屏幕里"分两支 —— 只钉前一句的话，
                // 离屏那一趟必红；只钉后一句的话，正常那一趟就量不出"贴着"了
                // （而"贴着锚点"正是用户 点名的那句）。
                // 锚点从**底栏那颗账号按钮**换成**主页右上那张账号卡片**
                // （见本节开头的 NOTE），两支判据本身一个字都没改。
                const QRect accScreen =
                    accCard->screen() != nullptr ? accCard->screen()->availableGeometry() : QRect();
                const bool anchorOnScreen =
                    !accScreen.isNull() && accAnchor.x() <= accScreen.right() + 1;
                const int wantRight =
                    anchorOnScreen ? accAnchor.x() : accScreen.right() + 1;
                check(rightPanel->x() + rightPanel->width() == wantRight,
                      anchorOnScreen
                          ? "I4 账号那一块右边缘贴卡片右边缘"
                          : "I4 卡片在屏幕外面时，账号那一块被夹在屏幕右边缘上",
                      QStringLiteral("面板右边 %1 / 期望 %2（卡片右边 %3，屏幕 %4..%5）")
                          .arg(rightPanel->x() + rightPanel->width())
                          .arg(wantRight)
                          .arg(accAnchor.x())
                          .arg(accScreen.left())
                          .arg(accScreen.right()));
                check(rightPanel->y() >=
                          accCard->mapToGlobal(QPoint(0, accCard->height())).y(),
                      "I4 账号那一块是在卡片**下方**（卡片贴着头顶，往上弹没地方）",
                      QStringLiteral("面板顶 %1 / 卡片底 %2")
                          .arg(rightPanel->y())
                          .arg(accCard->mapToGlobal(QPoint(0, accCard->height())).y()));
                // 右面板的行**是外面按 account = true 喂进来的**（同一个 provider，两块
                // 面板要的东西不一样）—— 这里量"账号行照画"。
                QLabel *accountTitle0 =
                    rightPanel->findChild<QLabel *>(MainWindowUI::bottomBarPanelRowTitleName(0));
                check(accountTitle0 != nullptr &&
                          accountTitle0->text() == QStringLiteral("LoserKugua"),
                      "I4 右面板要的行是**按 account = true 去要**的（给的是账号那两行）",
                      accountTitle0 != nullptr ? accountTitle0->text()
                                               : QStringLiteral("<没找到>"));
                QPushButton *currentBtn = rightPanel->findChild<QPushButton *>(
                    MainWindowUI::bottomBarPanelRowActionName(0, 0));
                QPushButton *selectBtn = rightPanel->findChild<QPushButton *>(
                    MainWindowUI::bottomBarPanelRowActionName(1, 0));
                check(currentBtn != nullptr &&
                          currentBtn->text() == MainWindowUI::bottomBarPanelLogoutText() &&
                          currentBtn->isEnabled() && !currentBtn->toolTip().isEmpty(),
                      "I4 当前账号那一行写「登出」并且**能点**（不是灰掉的「当前」）",
                      currentBtn != nullptr
                          ? QStringLiteral("%1 / enabled=%2")
                                .arg(currentBtn->text())
                                .arg(currentBtn->isEnabled())
                          : QStringLiteral("<没找到>"));
                check(selectBtn != nullptr &&
                          selectBtn->text() == MainWindowUI::bottomBarPanelSelectText(),
                      "I4 另一个账号那颗写「选择」（正版账号那颗写成「登录」是 MainWindow "
                      "那边挑的字）",
                      selectBtn != nullptr ? selectBtn->text() : QStringLiteral("<没找到>"));
                // 几行的按钮要**竖着对齐**：文字按钮的最小宽度是钉死的，跟着字宽变的话
                // 「选择」和「登录」会错开几个像素，看着像没对齐的表格。
                check(currentBtn != nullptr && selectBtn != nullptr &&
                          currentBtn->width() == selectBtn->width(),
                      "I4 两行的行尾按钮一样宽（同宽 = 竖着对齐）",
                      currentBtn != nullptr && selectBtn != nullptr
                          ? QStringLiteral("%1 / %2").arg(currentBtn->width()).arg(selectBtn->width())
                          : QStringLiteral("<没找到>"));
                // 点「登出」那颗：面板收回去，回调带 account = true + **那一行**的 uuid +
                // action 0。MainWindow 就是拿 key 跟 activeUuid() 比来分流"登出"还是"切账号"
                // 的（相等走登出，见 onBottomBarPanelAction()）—— 一颗按钮按行的状态身兼二职，
                // key 必须原样还回来，不然点了登出会去切账号。
                firedPanels.clear();
                firedKeys.clear();
                firedActions.clear();
                if (currentBtn != nullptr) {
                    currentBtn->click();
                    QApplication::processEvents();
                    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                    check(firedPanels == QList<bool>{true} &&
                              firedKeys == QList<QString>{QStringLiteral("uuid-mine")} &&
                              firedActions == QList<int>{0},
                          "I4 点「登出」回来的 key 就是那一行的 uuid、action 还是 0",
                          QStringLiteral("account=%1 key=%2 action=%3")
                              .arg(firedPanels.value(0))
                              .arg(firedKeys.value(0))
                              .arg(firedActions.value(0)));
                    check(window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName()) == nullptr,
                          "I4 点了「登出」之后面板也收回去（和「选择」一样）");
                }
                // NOTE: 产品里没有任何一颗走 `PanelAction::enabled = false`（那个开关留着，
                // 给将来"这一下现在做不了"的按钮）。为它硬摆一个合成的假灰按钮就变成在量
                // Qt 自己（disabled 的 QPushButton 不发 clicked 是 Qt 的规矩）。右面板里
                // 当前账号那颗写着「登出」且能点 —— 下面那条"真能点、回调带对了 key"就是它。
                //
                // 点「选择」那颗：面板收回去，回调带 account = true + 那个账号的 uuid。
                // 先把它重新打开（上面那一下已经收掉了），**并把上一下那几条回调记录清掉**
                // —— 不清的话下面那三条断言拿到的是"两次点击加在一起"，而报出来的会是
                // 第一下的 key（看着像"点选择回来的是登出那一行"，其实只是没清）。
                firedPanels.clear();
                firedKeys.clear();
                firedActions.clear();
                accCard->click();
                QApplication::processEvents();
                QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                rightPanel = window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName());
                selectBtn = rightPanel != nullptr
                                ? rightPanel->findChild<QPushButton *>(
                                      MainWindowUI::bottomBarPanelRowActionName(1, 0))
                                : nullptr;
                if (selectBtn != nullptr) {
                    selectBtn->click();
                    QApplication::processEvents();
                    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                    check(firedPanels == QList<bool>{true} &&
                              firedKeys == QList<QString>{QStringLiteral("uuid-other")} &&
                              firedActions == QList<int>{0},
                          "I4 右面板回来的 account = true、key = 账号 uuid（MainWindow 靠 "
                          "account 分辨切账号还是关实例）",
                          QStringLiteral("account=%1 key=%2 action=%3")
                              .arg(firedPanels.value(0))
                              .arg(firedKeys.value(0))
                              .arg(firedActions.value(0)));
                    check(window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName()) == nullptr,
                          "I4 点了「选择」之后面板已经收回去了");
                } else if (rightPanel != nullptr) {
                    rightPanel->close();
                    QApplication::processEvents();
                }
            }
            // ⑥b 账户那一块**只有一行高的时候就一行高**（
            // 「账户弹层保持紧凑，单账户不浪费成大列表，多账户时再展开切换列表」）。
            // 面板的高度跟着**行数**走（popUp() 里 adjustSize()）：一个账号时那块就是
            // "一行 + 上下内边距"，**不许**留出三行那种大盒子的空位；账号多起来（这里是
            // 三行）那块自然就长高了。判据刻意不写死字号/内边距那几个数字（它们是
            // MainWindowUI.cpp 里的文件级常量），只钉"**不到两行**"和"三行那块更高"。
            {
                int wantedRows = 1;
                ui.setBottomBarPanelContent(
                    [&wantedRows](bool account) -> QList<MainWindowUI::PanelRow> {
                        if (!account) {
                            MainWindowUI::PanelRow none;
                            none.placeholder = true;
                            none.title = MainWindowUI::bottomBarPanelNoRunningTitle();
                            return {none};
                        }
                        QList<MainWindowUI::PanelRow> rows;
                        for (int i = 0; i < wantedRows; ++i) {
                            MainWindowUI::PanelRow one;
                            one.key = QStringLiteral("uuid-%1").arg(i);
                            one.title = QStringLiteral("账号 %1").arg(i + 1);
                            one.subtitle = MainWindowUI::bottomBarPanelOnlineAccountText();
                            MainWindowUI::PanelAction use;
                            use.text = MainWindowUI::bottomBarPanelSelectText();
                            one.actions = {use};
                            rows << one;
                        }
                        return rows;
                    },
                    nullptr);
                const auto measureAccountPanel = [&window, accCard](int *rowHeightOut) {
                    accCard->click();
                    QApplication::processEvents();
                    QWidget *panel =
                        window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName());
                    if (rowHeightOut != nullptr) {
                        QWidget *row = panel != nullptr
                                           ? panel->findChild<QWidget *>(
                                                 MainWindowUI::bottomBarPanelRowName(0))
                                           : nullptr;
                        // 面板带 WA_DeleteOnClose；只在 close() 前取数，绝不把它的子控件
                        // 裸指针带出这个 helper。
                        *rowHeightOut = row != nullptr ? row->height() : -1;
                    }
                    const int height = panel != nullptr ? panel->height() : -1;
                    if (panel != nullptr) {
                        panel->close();
                        QApplication::processEvents();
                        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                        QApplication::processEvents();
                    }
                    return height;
                };
                int rowHeight = -1;
                const int heightOne = measureAccountPanel(&rowHeight);
                wantedRows = 3;
                const int heightThree = measureAccountPanel(nullptr);
                check(rowHeight > 0 && heightOne > 0 && heightOne < 2 * rowHeight,
                      "I4 只有一个账号时那块**不到两行高**（不被撑成三行那种大盒子）",
                      QStringLiteral("面板 %1 / 一行 %2 / 两行 %3")
                          .arg(heightOne)
                          .arg(rowHeight)
                          .arg(rowHeight * 2));
                check(heightThree > heightOne,
                      "I4 账号多起来（三行）那块跟着长高 —— 多账户时才展开那份切换列表",
                      QStringLiteral("三行 %1 / 一行 %2").arg(heightThree).arg(heightOne));
                wantedRows = 1;
                int accountRowHeight = -1;
                measureAccountPanel(&accountRowHeight);
                check(accountRowHeight > 0 &&
                          window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName()) == nullptr,
                      "I4 量完那一块收回去了（这一节的收尾不留给下一节）");
                // 这一份 provider 捕的是上面那个局部量 —— **用完立刻卸**（⑦ 虽然也会把它
                // 置空，但"依赖下一节替我收尾"这种约定迟早会在中间插进一句话之后变成悬空引用）。
                ui.setBottomBarPanelContent(nullptr, nullptr);
            }
            // ⑦ 兜底：没装 provider（或它给了个空列表）时，面板里留**一句话**而不是一块
            // 空白 —— 空白一块比一句话更难查（用户分不清"没弹出来"和"弹出来是空的"）。
            ui.setBottomBarPanelContent(nullptr, nullptr);
            runBtn->click();
            QApplication::processEvents();
            QWidget *emptyPanel = window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName());
            QLabel *emptyTitle =
                emptyPanel != nullptr
                    ? emptyPanel->findChild<QLabel *>(MainWindowUI::bottomBarPanelRowTitleName(0))
                    : nullptr;
            check(emptyTitle != nullptr && !emptyTitle->text().isEmpty() &&
                      emptyPanel->findChild<QPushButton *>(
                          MainWindowUI::bottomBarPanelRowActionName(0, 0)) == nullptr,
                  "I4 没内容时面板里留一句话（不是一块空白、行尾也没有按钮）",
                  emptyTitle != nullptr ? emptyTitle->text() : QStringLiteral("<没找到>"));
            if (emptyPanel != nullptr) {
                emptyPanel->close();
                QApplication::processEvents();
                QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
            // 收尾：那个假 provider 捕的是这一节里的局部量，用完就卸掉（留着的话，
            // 后面哪一节再点开面板就是拿着悬空引用在跑）。
            ui.setBottomBarPanelContent(nullptr, nullptr);
            progress->hide();
            // 那一格还成开局那一份 —— 不能再 `status->hide()`：它开局就是亮的
            // （理由见 E4/I 那两节）。
            ui.setBottomBarTaskSummary(MainWindowUI::BottomBarTaskSummary());
            QApplication::processEvents();

            // ⑧ 左下那一格**点不点得开**。判据在 MainWindow 手里
            // （`runningGames()` 空不空），界面这一头只管把"点得开 / 点不开"画出来
            // （见 setRunPanelAvailable()）—— 所以这里量两件事：**初值**必须是点得开
            // （谁都没摆过的时候不该一上来就点不动，独立构造 MainWindowUI 的地方吃这个
            // 初值），以及摆下去之后**真的**点不开（不是只把箭头藏起来做样子）。
            //
            // NOTE: 这一节用 `click()` 点，不用真鼠标事件：`QAbstractButton::click()`
            // 在 disabled 的时候会**直接返回**（那样断言就白绿了），而这一格用的
            // 是 WA_TransparentForMouseEvents、按钮本身是 enabled 的 —— 所以
            // `click()` 照样发 clicked()，能不能弹出来全看 toggle 里那道闸。
            {
                QLabel *chevron =
                    runBtn->findChild<QLabel *>(MainWindowUI::bottomBarChevronName());
                const auto describe = [runBtn, chevron]() {
                    return QStringLiteral("箭头 %1 / 收鼠标 %2 / tooltip「%3」")
                        .arg(chevron == nullptr
                                 ? QStringLiteral("<没找到>")
                                 : (chevron->isVisibleTo(runBtn) ? QStringLiteral("在")
                                                                 : QStringLiteral("藏了")))
                        .arg(runBtn->testAttribute(Qt::WA_TransparentForMouseEvents)
                                 ? QStringLiteral("是")
                                 : QStringLiteral("不是"))
                        .arg(runBtn->toolTip());
                };
                check(chevron != nullptr && chevron->isVisibleTo(runBtn) &&
                          !runBtn->testAttribute(Qt::WA_TransparentForMouseEvents) &&
                          runBtn->toolTip() == MainWindowUI::bottomBarRunPanelTip(),
                      "I4 没被告知「没有实例在跑」时，这一格照旧点得开（箭头在、鼠标收得到、"
                      "悬停那句话也挂着）",
                      describe());

                ui.setRunPanelAvailable(false);
                QApplication::processEvents();
                check(chevron != nullptr && !chevron->isVisibleTo(runBtn) &&
                          runBtn->testAttribute(Qt::WA_TransparentForMouseEvents) &&
                          runBtn->toolTip().isEmpty(),
                      "I4 被告知「没有实例在跑」之后：箭头**收起来**、这一格**不再收鼠标**、"
                      "那句「点一下向上展开」也摘掉了",
                      describe());
                // 箭头只是藏起来、**没有删**：「底栏三格各带一个箭头」那条断言（上面
                // I4 ②）拿的是 findChildren，删了就当场红 —— 这里顺手把它钉住。
                check(window.findChildren<QLabel *>(MainWindowUI::bottomBarChevronName()).size() == 2,
                      "I4 收起来的箭头还在（findChildren 照旧数得到两个）",
                      QStringLiteral("找到 %1 个")
                          .arg(window.findChildren<QLabel *>(
                                   MainWindowUI::bottomBarChevronName()).size()));

                runBtn->click();
                QApplication::processEvents();
                check(window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName()) == nullptr,
                      "I4 点不开的时候点它**什么都不弹**（不是弹一块空的出来）");

                ui.setRunPanelAvailable(true);
                QApplication::processEvents();
                runBtn->click();
                QApplication::processEvents();
                QWidget *backPanel =
                    window.findChild<QWidget *>(MainWindowUI::bottomBarPanelName());
                check(chevron != nullptr && chevron->isVisibleTo(runBtn) && backPanel != nullptr &&
                          backPanel->isVisible(),
                      "I4 摆回「点得开」之后，箭头回来了、点它照旧弹得出来",
                      describe());
                if (backPanel != nullptr) {
                    backPanel->close();
                    QApplication::processEvents();
                    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                }
            }
        }
    }
 return 0;
}


