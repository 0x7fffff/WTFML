#include "Support.hpp"
int UiBottomBarGeometryChecks(MainWindowUI &ui, QFrame * &bar) {
    // 底栏现在挂在导航轨底部；窗口窄于 1024 时轨折成图标轨、文字收起。这一节量的是展开那一档。
    QWidget *const host = bar->window();
    const QSize hostSize = host->size();
    host->resize(1280, 800);
    QApplication::processEvents();

    {
        QLabel *avatar = ui.getAvatarLabel();
        QLabel *account = ui.getAccountLabel();
        QLabel *status = ui.getStatusLabel();
        QProgressBar *progress = ui.getProgressBar();
        QLabel *runState = ui.getRunStateLabel();
        QLabel *runDetail = ui.getRunStateDetailLabel();
        QLabel *mark = ui.getRunStateMarkLabel();

        check(avatar != nullptr && account != nullptr && status != nullptr && progress != nullptr &&
                  runState != nullptr && runDetail != nullptr && mark != nullptr,
              "七块控件都在（头像/用户名/进度文字/进度条/状态正文/状态副行/状态方块）");
        if (avatar != nullptr && account != nullptr && status != nullptr && progress != nullptr &&
            runState != nullptr && runDetail != nullptr && mark != nullptr) {
            // 中间那格：**没有任务时那根进度条不出现**（"如果正在下载"那条要求），但那一格
            // 自己（连同那句占位话）是**亮着的** —— 它是"平时也写一句「暂无后台任务」、
            // 点一下展开任务中心"的按钮，不整格藏起来。
            check(!progress->isVisible() && status->isVisible(),
                  "一开始没有任务：进度条藏着，但那格（和占位话）是亮着的",
                  QStringLiteral("进度条 %1 / 那句话 %2")
                      .arg(progress->isVisible() ? QStringLiteral("亮") : QStringLiteral("藏"))
                      .arg(status->isVisible() ? QStringLiteral("亮") : QStringLiteral("藏")));
            // 左下角：没有实例在跑时什么都不显示，不留一句"未运行"占地方
            check(!runState->isVisible() && !runDetail->isVisible(),
                  "一开始运行状态那两行也是藏着的");
            // 那颗方块也一样：界面自己不知道状态（颜色是 MainWindow 按状态给的），
            // 所以初值跟那两行一起藏着 —— 不然会先亮一个"没有含义的颜色"再被改掉。
            check(!mark->isVisible(), "一开始那颗状态方块也是藏着的");

            // 两格的左右顺序：运行状态 → 任务摘要（进度）→ 工具箱。
            // 右边那一格（头像 + 用户名）整个在主页右上那张账号卡片里 —— `accountButton`
            // 还在、业务层照旧往里写数据，但它**不进布局**（`hide()`），所以它里面那几样
            // （头像/用户名/第二行）的几何是"没人给它分位置"的旧值，**不能**再拿它们当
            // "右边那一格"来量。右下角是跨页面的「工具箱」入口（`toolboxButton`，宽度和
            // 左边那格一样、也是 170）。
            // NOTE: 量之前必须先把要量的控件显示出来 —— 布局不给隐藏的控件分位置，
            // 藏着的控件几何停在旧值（通常是 0），拿它比大小等于没比。
            progress->show();
            status->show();
            ui.setRunStateActive(true); // 运行状态那一格平时收着，量它先露出来
            runState->show();
            runDetail->show();
            mark->show();
            QApplication::processEvents();
            QPushButton *accCell = ui.getAccountButton();
            QPushButton *toolsCell = ui.getToolboxButton();
            check(accCell != nullptr && toolsCell != nullptr,
                  "底栏那两格（隐藏的账号格 + 工具箱入口）都在");
            check(accCell != nullptr && !accCell->isVisible(),
                  "底栏右边那一格（账号那组）不显示了 —— 入口整个收进主页右上那张卡片",
                  accCell != nullptr ? QStringLiteral("visible=%1").arg(accCell->isVisible())
                                     : QString());
            check(toolsCell != nullptr && toolsCell->isVisible(),
                  "右下角换成了跨页面的「工具箱」入口");
            // 轨底部从上到下：任务卡（进度环 + 摘要）/ 运行状态 / 工具箱。
            const auto centerY = [bar](QWidget *w) {
                return w->mapTo(bar, w->rect().center()).y();
            };
            check(centerY(progress) < centerY(runState), "任务卡在运行状态那格上面",
                  QString("%1 / %2").arg(centerY(progress)).arg(centerY(runState)));
            if (toolsCell != nullptr) {
                check(centerY(runState) < centerY(toolsCell),
                      "运行状态那格在工具箱上面（从上到下：任务卡 / 运行状态 / 工具箱）",
                      QString("%1 / %2").arg(centerY(runState)).arg(centerY(toolsCell)));
            }
            check(avatar->mapTo(bar, QPoint(0, 0)).x() < account->mapTo(bar, QPoint(0, 0)).x(),
                  "用户名在头像右边（账户那一组内部）");

            // 中间那格**不吃掉全部富余宽度**：左右各留空档，两头都留白所以它还居中。
            // 右边那头拿**工具箱**那格的左边缘当参照 —— 隐藏的账号格里那颗头像不在
            // 布局里，拿它量出来的空档是负的，等于没量。
            // 头像是个正方形方块（不加 border-radius，这里只量尺寸）
            check(avatar->width() == avatar->height() && avatar->width() == 32,
                  "头像是 32x32 的方块",
                  QString("%1x%2").arg(avatar->width()).arg(avatar->height()));
            check(avatar->text() == QStringLiteral("?") && !account->text().isEmpty(),
                  "界面没搭完时头像画问号、用户名有占位文字",
                  avatar->text() + " / " + account->text());

            // 运行状态那一格的两行文字：正文的出处就在界面上，两个验证工程都引它，
            // 免得产品里改一个字、两边测试各写一遍字面量。
            check(MainWindowUI::runStateRunningText() == QStringLiteral("运行中"),
                  "状态正文是「运行中」", MainWindowUI::runStateRunningText());
            check(runState->alignment().testFlag(Qt::AlignLeft) &&
                      runDetail->alignment().testFlag(Qt::AlignLeft),
                  "两行都靠左对齐（贴着窗口左下角）");
            check(runDetail->mapTo(bar, QPoint(0, 0)).y() > runState->mapTo(bar, QPoint(0, 0)).y(),
                  "实例名那行在状态正文下面");

            // ---- 那颗状态方块摆在哪 ----
            // 它是**第一行的一部分**（方块 + 文字的一个横排），不是"整格最左边的一个点"：
            // 后者在显示第二行时会停在两行的中间，看着不属于任何一行。
            check(mark->width() == mark->height() &&
                      mark->width() == MainWindowUI::runStateMarkSize(),
                  "那颗方块是正方形（边长走唯一出处 runStateMarkSize()）",
                  QStringLiteral("%1x%2 / 该是 %3")
                      .arg(mark->width()).arg(mark->height())
                      .arg(MainWindowUI::runStateMarkSize()));
            const int markLeft = mark->mapTo(bar, QPoint(0, 0)).x();
            const int runLeft = runState->mapTo(bar, QPoint(0, 0)).x();
            check(markLeft < runLeft, "方块在第一行文字左边",
                  QStringLiteral("方块 %1 / 文字 %2").arg(markLeft).arg(runLeft));
            // 它左边只有这一格自己的左内边距（和导航项的图标对齐），没有别的空档。
            QPushButton *const runBtn = ui.getRunStateButton();
            const int runBtnLeft = runBtn->mapTo(bar, QPoint(0, 0)).x() +
                                   (runBtn->layout() != nullptr ? runBtn->layout()->contentsMargins().left() : 0);
            check(markLeft == runBtnLeft, "方块是这一格里的第一个东西（左边只有那一格的内边距）",
                  QStringLiteral("方块 x=%1 / 那一格的左边 x=%2").arg(markLeft).arg(runBtnLeft));
            // 和第一行**同一行**：竖直方向的两条中线对得上（方块比字矮一号，居中对齐）。
            const int markMid = mark->mapTo(bar, mark->rect().center()).y();
            const int runMid = runState->mapTo(bar, runState->rect().center()).y();
            check(qAbs(markMid - runMid) <= 2, "方块和第一行文字竖直居中对齐（差在 2 px 内）",
                  QStringLiteral("方块中线 %1 / 文字中线 %2").arg(markMid).arg(runMid));
            check(markMid < runDetail->mapTo(bar, runDetail->rect().center()).y(),
                  "方块**不是**摆在两行中间的那个位置（它在第一行上）",
                  QStringLiteral("方块中线 %1 / 第二行中线 %2")
                      .arg(markMid)
                      .arg(runDetail->mapTo(bar, runDetail->rect().center()).y()));
            // 第二行缩进的距离 = 方块那一行**实际**占掉的宽度（量出来的，不是引常量重算一遍）：
            // 这样"方块改宽了但第二行没跟着缩"会被抓住。
            const int markBand = runLeft - markLeft;
            check(runDetail->contentsMargins().left() == markBand,
                  "第二行按方块那一段**实际**宽度缩进，两行文字的左边对齐",
                  QStringLiteral("缩进 %1 px / 方块那段 %2 px")
                      .arg(runDetail->contentsMargins().left())
                      .arg(markBand));
            // 这一格的宽度预算要**对得上**：方块那段 + 文字宽 = 整格（右侧不再挂箭头）。
            // 加方块的时候最容易漏掉的就是"文字的上限没跟着减"，那一步会让方块把整格撑出去。
            check(MainWindowUI::bottomBarRunStateMarkWidth() +
                      MainWindowUI::bottomBarRunStateTextWidth() ==
                  MainWindowUI::bottomBarRunStateColumnWidth(),
                  "那一格的宽度预算加得起来（方块段 + 文字上限 = 整格）",
                  QStringLiteral("%1 + %2 vs %3")
                      .arg(MainWindowUI::bottomBarRunStateMarkWidth())
                      .arg(MainWindowUI::bottomBarRunStateTextWidth())
                      .arg(MainWindowUI::bottomBarRunStateColumnWidth()));
            // 两行**留给字**的宽度必须一样。第一行那颗方块是布局里
            // 单独的控件（占的是上限**之外**的位置），第二行的缩进走 contentsMargins
            // （从上限**里面**扣）⇒ 两个 QLabel 自己的上限本来就该差一段，扣掉各自
            // "不属于字"的那一段之后才是同一个数。两行封同一个数的话，第二行实际只有
            // 120 px、右边比第一行短一截 —— 那条断言量的是"方块那一段到底多宽"，
            // 量不出"第二行少写了 16 px"，会一直是绿的。
            const int row1TextBand = runState->maximumWidth() - runState->contentsMargins().left();
            const int row2TextBand = runDetail->maximumWidth() - runDetail->contentsMargins().left();
            check(row1TextBand == row2TextBand &&
                      row1TextBand == MainWindowUI::bottomBarRunStateTextWidth(),
                  "两行留给字的宽度一样（都是 bottomBarRunStateTextWidth()）",
                  QStringLiteral("第一行 %1 px / 第二行 %2 px（上限 %3 - 左缩进 %4）")
                      .arg(row1TextBand)
                      .arg(row2TextBand)
                      .arg(runDetail->maximumWidth())
                      .arg(runDetail->contentsMargins().left()));
            // 收尾：那根进度条藏回去（上面 show() 过），并**把那一格还成开局那一份**
            // ——不能再 `status->hide()` 了：它开局就是亮的（见本节开头那条）。
            progress->hide();
            ui.setBottomBarTaskSummary(MainWindowUI::BottomBarTaskSummary());
            runState->hide();
            runDetail->hide();
            mark->hide();
            QApplication::processEvents();
        }
    }
    host->resize(hostSize);
    QApplication::processEvents();
 return 0;
}




