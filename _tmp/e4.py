import sys; sys.path.insert(0, '_tmp')
import ed

h = 'ui/MainWindowUI.hpp'
ed.sub(h, '''    QPushButton *homeInstanceSelectButton = nullptr;''',
'''    void updateHomeQuoteFont(bool narrow);

    QPushButton *homeInstanceSelectButton = nullptr;''')

lay = 'ui/MainWindowPageLayout.hpp'
ed.sub(lay, '''/// 主页大标题的字号（设计稿 home.html：细楷 50，窄窗 40）。
inline constexpr int kHomeHeroTitlePixelSize = 50;''',
'''/// 主页大标题的字号（设计稿 home.html：细楷 50，窄窗 40）。
inline constexpr int kHomeHeroTitlePixelSize = 50;
/// 大标题下那行「每日一言」的字号：正常 16，窄窗 <1024 收一号到 14
/// （和原来的英文副题同一档，见 8fc9f5e 的反向 diff）。
inline constexpr int kHomeQuotePixelSize = 16;
inline constexpr int kHomeQuoteNarrowPixelSize = 14;''')

c = 'ui/MainWindowUI.cpp'
ed.sub(c, '''        // 英文副题「Your next world awaits.」已去掉（11-12 用户反馈）：大标题一句就够。
        followHeroInk(welcome, false);
        // 窄于 1024 的窗口：标题收一号（50 → 40），立绘缩到七成二（设计稿 @media）。
        const auto applyScale = [this, homeTab, welcome]() {
            const bool narrow = homeTab->window()->width() < 1024;
            Theme::markDisplay(welcome, handPx(welcome->font(), Theme::HandFont::KaiLight,
                                               narrow ? 40 : kHomeHeroTitlePixelSize, narrow ? 3 : 4));
            if (skinViewer != nullptr) {''',
'''        followHeroInk(welcome, false);
        // 大标题正下方那一行「每日一言」：一天一句，零实例与有实例都显示。
        // 位置就是被删掉的英文副题那一格（边距 kHomeHeroInset + 4, 12）。没句子时**隐藏**，
        // 版面和没有这一行时一模一样（设置里关掉也走这条路）。
        auto *quote = new ElidedLabel(home.left.widget);
        quote->setObjectName(homeHeroQuoteName());
        quote->setContentsMargins(kHomeHeroInset + 4, 12, 0, 0);
        // 宽度交给布局（Ignored），文字长短不会把左栏抻开；装不下自己收省略号。
        quote->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        quote->setToolTip(QString());
        quote->setVisible(false);
        homeHeroQuote = quote;
        home.left.layout->addWidget(quote);
        followHeroInk(quote, true);
        // 窄于 1024 的窗口：标题收一号（50 → 40），一言收一号（16 → 14），立绘缩到七成二（设计稿 @media）。
        const auto applyScale = [this, homeTab, welcome]() {
            const bool narrow = homeTab->window()->width() < 1024;
            Theme::markDisplay(welcome, handPx(welcome->font(), Theme::HandFont::KaiLight,
                                               narrow ? 40 : kHomeHeroTitlePixelSize, narrow ? 3 : 4));
            updateHomeQuoteFont(narrow);
            if (skinViewer != nullptr) {''')

ed.sub(c, '''/// 主页那一页：左 = 挑一个已装好的版本、启动、三种快捷入口；右 = 我的角色（3D 预览）。''',
'''void MainWindowUI::updateHomeQuoteFont(bool narrow) {
    if (homeHeroQuote == nullptr) return;
    const QFont font = Theme::quoteFont(homeHeroQuote->font(), homeQuoteScript,
                                       narrow ? kHomeQuoteNarrowPixelSize : kHomeQuotePixelSize);
    if (homeQuoteScript == Theme::QuoteScript::Kana) {
        // 日文走系统字体链，不算手写体：不挂 wtfmlDisplay（ui_check 按它查手写体有没有漏）。
        homeHeroQuote->setFont(font);
        homeHeroQuote->setProperty(Theme::displayPropertyName().toUtf8().constData(), false);
        return;
    }
    Theme::markDisplay(homeHeroQuote, font);
}

void MainWindowUI::setHomeHeroQuote(const QString &text, const QString &tooltip,
                                    Theme::QuoteScript script) {
    if (homeHeroQuote == nullptr) return;
    homeQuoteScript = script;
    const QWidget *host = homeHeroQuote->window();
    updateHomeQuoteFont(host != nullptr && host->width() < 1024);
    homeHeroQuote->setText(text);
    homeHeroQuote->setToolTip(tooltip);
    homeHeroQuote->setVisible(!text.isEmpty());
}

/// 主页那一页：左 = 挑一个已装好的版本、启动、三种快捷入口；右 = 我的角色（3D 预览）。''')
