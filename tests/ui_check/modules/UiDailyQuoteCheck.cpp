#include "Support.hpp"

#include "MainWindowUI.hpp"
#include "Theme.hpp"

// 主页「每日一言」那一行：在有 / 没有之间来回摆，量的是**版面和字体**，不是网络。
//
// 位置就是被删掉的英文副题那一格（8fc9f5e 的反向 diff）：大标题下面、启动区上面，
// 单行小字。这一层要证的三件事：
// 1. 有句子时它在、没句子/关掉时它**不占位**（启动区那一块回到原位）；
// 2. 中文 / 英文 / 日文各自选的 family 对得上（不靠像素，靠 family 列表）；
// 3. 窄窗（<1024）字号收一号，超宽自己收省略号。
int UiDailyQuoteCheck(QApplication &app) {
    app.setStyle(Theme::createStyle());
    Theme::setSpec(Theme::defaultSpec());
    Theme::apply(app);
    Anim::setEnabled(false);

    QMainWindow window;
    MainWindowUI ui(&window);
    window.resize(1280, 800);
    window.show();
    app.processEvents();

    QLabel *quote = ui.getHomeHeroQuote();
    check(quote != nullptr, "DQ-UI1 主页建出了「每日一言」那一行");
    if (quote == nullptr) return 1;
    check(quote->objectName() == MainWindowUI::homeHeroQuoteName(), "DQ-UI2 它的 objectName 是 homeHeroQuote");
    check(quote->isHidden(), "DQ-UI3 还没句子时它是隐藏的（大标题下面什么都不占）");
    check(!quote->wordWrap(), "DQ-UI4 不换行（单行）");
    check(quote->property("elideMode").toInt() == int(Qt::ElideRight),
          "DQ-UI5 是 ElidedLabel，装不下从尾巴收省略号");

    QLabel *title = window.findChild<QLabel *>(QStringLiteral("homeHeroTitle"));
    check(title != nullptr, "DQ-UI6 大标题在（拿它的纵向位置当版面基准）");
    const int titleY = title != nullptr ? title->mapTo(&window, QPoint(0, 0)).y() : -1;

    // ---- 中文：文楷 ----
    ui.setHomeHeroQuote(QStringLiteral("长河千嶂，大荒孤城，历历在目。"),
                        QStringLiteral("— 令《明日方舟》"), Theme::QuoteScript::Han);
    app.processEvents();
    check(quote->isVisible() && quote->text() == QStringLiteral("长河千嶂，大荒孤城，历历在目。"),
          "DQ-UI7 有句子时显示出来，文字就是那一句");
    check(quote->font().families().value(0).contains(QStringLiteral("WenKai")),
          "DQ-UI8 中文选的是文楷（family 列表第一档）", quote->font().families().join(QStringLiteral(" / ")));
    check(quote->font().pixelSize() == 16, "DQ-UI9 宽窗字号 16",
          QString::number(quote->font().pixelSize()));
    check(quote->toolTip() == QStringLiteral("— 令《明日方舟》"), "DQ-UI10 悬停提示是「— 作者《出处》」",
          quote->toolTip());
    check(quote->height() > 0 && quote->mapTo(&window, QPoint(0, 0)).y() > titleY,
          "DQ-UI11 它在大标题下方、自己占得到高度（不是压在标题上）",
          QStringLiteral("titleY=%1 quoteY=%2 h=%3")
              .arg(titleY)
              .arg(quote->mapTo(&window, QPoint(0, 0)).y())
              .arg(quote->height()));

    // ---- 英文：Dancing Script ----
    ui.setHomeHeroQuote(QStringLiteral("The significance of life is living."),
                        QStringLiteral("— Jiddu Krishnamurti"), Theme::QuoteScript::Latin);
    app.processEvents();
    check(quote->font().families().value(0).contains(QStringLiteral("Dancing")),
          "DQ-UI12 纯拉丁选的是 Dancing Script", quote->font().families().join(QStringLiteral(" / ")));

    // ---- 日文：系统日文字体链（文楷没有假名） ----
    ui.setHomeHeroQuote(QStringLiteral("希望とシェイクハンドして、真新しい自分。"),
                        QStringLiteral("— 大森元貴《Brand New》"), Theme::QuoteScript::Kana);
    app.processEvents();
    const QStringList jp = quote->font().families();
    check(jp.value(0) == QStringLiteral("Yu Mincho") && jp.contains(QStringLiteral("Meiryo UI")),
          "DQ-UI13 日文排的是 Yu Mincho → Yu Gothic UI → Meiryo UI", jp.join(QStringLiteral(" / ")));
    check(!quote->font().families().value(0).contains(QStringLiteral("WenKai")),
          "DQ-UI14 日文不把文楷排第一（文楷没有假名，混排会一行两种字形）");
    check(!quote->property("wtfmlDisplay").toBool(),
          "DQ-UI15 日文不挂 wtfmlDisplay（系统字体不算手写体）");

    // ---- 窄窗收一号 ----
    window.resize(900, 700);
    app.processEvents();
    check(quote->font().pixelSize() == 14, "DQ-UI16 窄窗（<1024）字号收一号到 14",
          QString::number(quote->font().pixelSize()));
    window.resize(1280, 800);
    app.processEvents();
    check(quote->font().pixelSize() == 16, "DQ-UI17 回到宽窗又变回 16");

    // ---- 收起来：不占位 ----
    ui.setHomeHeroQuote(QString(), QString(), Theme::QuoteScript::Han);
    app.processEvents();
    check(quote->isHidden(), "DQ-UI18 空句子 → 隐藏");
    check(title != nullptr && title->mapTo(&window, QPoint(0, 0)).y() == titleY,
          "DQ-UI19 收起之后版面回到原来那一格（大标题位置不变、那一行不占位）");
    check(quote->toolTip().isEmpty(), "DQ-UI20 收起时不留悬停提示");

    // ---- 设置页那个开关 ----
    auto *dailyQuote = window.findChild<QCheckBox *>(MainWindowUI::dailyQuoteCheckName());
    check(dailyQuote != nullptr, "DQ-UI21 设置 → 个性化 → 外观 有「每日一言」开关");
    if (dailyQuote != nullptr) {
        check(dailyQuote->accessibleDescription().contains(QStringLiteral("hitokoto")) &&
                  dailyQuote->accessibleDescription().contains(QStringLiteral("zenquotes.io")),
              "DQ-UI22 说明文字写明发往哪几个站、给 ZenQuotes 署名",
              dailyQuote->accessibleDescription());
    }
    return 0;
}
