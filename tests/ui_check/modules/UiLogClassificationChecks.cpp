#include "Support.hpp"
int UiLogClassificationChecks() {
    {
        // 挑夹具照这个仓库里验像素那几条的规矩来：**每一条都要分得开**。
        // 正例挑的是现实里真的会出现的两种写法（原版那一行 / Forge 那一行），反例挑的是
        // "换个笨实现就会误判"的那些写法（消息体里提到 ERROR、没有冒号的裸词……），
        // 而不是随便一句没有级别的话 —— 后者无论怎么实现都是对的，等于白验。
        struct Case {
            QString line;
            GameLogWindow::LogLevel fallback;
            GameLogWindow::LogLevel want;
            QString what;
        };
        using L = GameLogWindow::LogLevel;
        const QList<Case> cases = {
            // ---- 正例 ----
            {QStringLiteral("[12:34:56] [main/INFO]: Setting user: Steve"), L::Info, L::Info,
             QStringLiteral("原版那一行（时间是第一格、级别在第二格斜杠后）")},
            {QStringLiteral("[24Sep2026 12:34:56.789] [Chunk Render Task 12/WARN] [m.Foo/]: x"),
             L::Info, L::Warn,
             QStringLiteral("Forge 那一行（长日期 + 长线程名，级别排在第 47 个字符上 —— 谁给"
                            "「行首那一小段」切一刀，先切掉的就是它）")},
            {QStringLiteral("[12:34:56] [Render thread/ERROR]: boom"), L::Info, L::Error,
             QStringLiteral("ERROR 那档")},
            {QStringLiteral("[12:34:56] [main/FATAL]: 起不来了"), L::Info, L::Fatal,
             QStringLiteral("FATAL 那档")},
            {QStringLiteral("[12:34:56] [Worker-Main-1/DEBUG]: x"), L::Info, L::Debug,
             QStringLiteral("DEBUG 那档")},
            {QStringLiteral("[12:34:56] [main/TRACE]: x"), L::Info, L::Debug,
             QStringLiteral("TRACE 归 Debug（比 Debug 还轻，和「出了什么事」没关系）")},
            {QStringLiteral("[12:34:56] [main/WARNING]: x"), L::Info, L::Warn,
             QStringLiteral("WARNING 这种写法也认（同一个意思）")},
            {QStringLiteral("[12:34:56] [main/SEVERE]: x"), L::Info, L::Error,
             QStringLiteral("SEVERE 归 Error")},
            {QStringLiteral("[12:34:56] [main/info]: x"), L::Warn, L::Info,
             QStringLiteral("大小写不分")},
            {QStringLiteral("[INFO] 没有线程名那种"), L::Warn, L::Info,
             QStringLiteral("没有线程名、整格就是级别")},
            {QStringLiteral("12:34:56 WARN: 冒号裸词那种"), L::Info, L::Warn,
             QStringLiteral("行首没有方括号，`WARN:` 这种（java.util.logging 那一套）")},
            // ---- 反例：认不出来就得原样回 fallback，不许猜 ----
            {QStringLiteral("[12:34:56] [main/INFO]: could not load ERROR config"), L::Debug,
             L::Info, QStringLiteral("消息体里提到 ERROR，不许改判（级别看前缀那一格）")},
            {QStringLiteral("Failed to parse [ERROR] block"), L::Debug, L::Debug,
             QStringLiteral("行首不是方括号 → 消息体里那个 [ERROR] 不算数")},
            {QStringLiteral("Loaded 3 INFO files"), L::Debug, L::Debug,
             QStringLiteral("裸词**没有冒号**不算（这条就是「冒号必须有」那个判据）")},
            {QStringLiteral("[游戏已退出，退出码：0]"), L::Error, L::Error,
             QStringLiteral("我们自己补的那句跟着上一行走")},
            {QStringLiteral("\tat net.minecraft.Foo.bar(Foo.java:12)"), L::Error, L::Error,
             QStringLiteral("崩栈的续行跟着上一行走（上一行是 ERROR 就整段是红的）")},
            {QString(), L::Warn, L::Warn, QStringLiteral("空行跟着上一行走")},
            {QString(60, QLatin1Char('a')) + QStringLiteral(" ERROR: x"), L::Debug, L::Debug,
             QStringLiteral("级别排在开头那一小段**之后**了，不算（不然正文里带冒号的词全会"
                            "被当成级别）")},
        };
        QStringList wrong;
        for (const Case &c : cases) {
            if (GameLogWindow::classifyLine(c.line, c.fallback) != c.want) {
                wrong << QStringLiteral("%1 → %2（该是 %3）")
                             .arg(c.what,
                                  GameLogWindow::levelName(
                                      GameLogWindow::classifyLine(c.line, c.fallback)),
                                  GameLogWindow::levelName(c.want));
            }
        }
        check(wrong.isEmpty(),
              QStringLiteral("Z3c %1 条真值表全中（11 条正例 + 7 条反例）").arg(cases.size()),
              wrong.join(QStringLiteral("\n      ")));

        // ---- 五档的名字 / 按钮名 / 底色的唯一性 ----
        // 加一档时漏改一处是这类表最容易犯的错（版本树那三个角色号码撞过一回），
        // 所以"两两不同"这几条要一起钉。
        QStringList names;
        QStringList buttonNames;
        for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
            const L level = GameLogWindow::levelAt(i);
            names << GameLogWindow::levelName(level);
            buttonNames << GameLogWindow::filterButtonName(level);
            if (GameLogWindow::levelIndex(level) != i) {
                names << QStringLiteral("<下标对不上:%1>").arg(i);
            }
        }
        check(names.size() == GameLogWindow::levelCount() && !names.contains(QString())
                      && QSet<QString>(names.begin(), names.end()).size() == names.size(),
              "Z3c 五档的名字都非空、两两不同（下标 ↔ 级别也一一对上）",
              names.join(QStringLiteral(" | ")));
        check(QSet<QString>(buttonNames.begin(), buttonNames.end()).size() ==
                      buttonNames.size() &&
                  !buttonNames.contains(QString()),
              "Z3c 五档按钮的 objectName 也两两不同",
              buttonNames.join(QStringLiteral(" | ")));
        check(GameLogWindow::levelAt(-1) == L::Info &&
                  GameLogWindow::levelAt(GameLogWindow::levelCount()) == L::Info,
              "Z3c 下标越界回 Info（不为一个越界把日志窗弄崩）");
        check(!GameLogWindow::levelBackground(L::Info).isValid(),
              "Z3c Info 那一档故意**不染底**（返回无效色 = 用窗口自己的底色）");
        int closest = 999;
        QString closestPair;
        for (int i = 0; i < GameLogWindow::levelCount(); ++i) {
            for (int j = i + 1; j < GameLogWindow::levelCount(); ++j) {
                const QColor a = GameLogWindow::levelBackground(GameLogWindow::levelAt(i));
                const QColor b = GameLogWindow::levelBackground(GameLogWindow::levelAt(j));
                if (!a.isValid() || !b.isValid()) {
                    continue;  // Info 那一档和谁都"不算距离"：它不染底，见 Z6 里那条像素断言
                }
                const int distance = colorDistance(a, b);
                if (distance < closest) {
                    closest = distance;
                    closestPair = QStringLiteral("%1/%2")
                                      .arg(GameLogWindow::levelName(GameLogWindow::levelAt(i)),
                                           GameLogWindow::levelName(GameLogWindow::levelAt(j)));
                }
            }
        }
        // 四条染色的档两两看得出来：20 是个很松的线，只挡"两档配成了同一个色"那种错。
        check(closest >= 20, "Z3c 四档染色的底色两两不同",
              QStringLiteral("最接近的一对：%1，差 %2").arg(closestPair).arg(closest));
        check(!GameLogWindow::filterButtonTooltip().isEmpty() &&
                  !GameLogWindow::filterButtonText(L::Warn, 3).isEmpty() &&
                  !GameLogWindow::lineLimitTooltip().isEmpty() &&
                  !GameLogWindow::lineLimitText(500).isEmpty() &&
                  !GameLogWindow::filteredOutPlaceholderText().isEmpty() &&
                  GameLogWindow::filteredOutPlaceholderText() !=
                      GameLogWindow::emptyPlaceholderText(),
              "Z3c 开关/下拉框上那几句文案都在，而且「都被筛掉了」和「还没有输出」是两句不同的话");
    }
    return 0;
}


