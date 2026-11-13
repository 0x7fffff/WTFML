import sys; sys.path.insert(0, '_tmp')
import ed

h = 'ui/MainWindowUI.hpp'
ed.sub(h, '''    QLineEdit *titleTextEdit = nullptr;''',
'''    QLineEdit *titleTextEdit = nullptr;
    QCheckBox *dailyQuoteCheck = nullptr;''')
ed.sub(h, '''    QLabel *getHomeHeroQuote() const { return homeHeroQuote; }''',
'''    QLabel *getHomeHeroQuote() const { return homeHeroQuote; }
    /// 设置页「外观」组里「每日一言」那个开关。
    QCheckBox *getDailyQuoteCheck() const { return dailyQuoteCheck; }''')

c = 'ui/MainWindowSettingsPage.cpp'
ed.sub(c, '''        addSwitch(look, colors, QStringLiteral("日志颜色"), QStringLiteral("对新日志窗口生效。"));
    }''',
'''        addSwitch(look, colors, QStringLiteral("日志颜色"), QStringLiteral("对新日志窗口生效。"));
    }
    {
        // 「每日一言」：默认开。checked 状态由 MainWindow 灌（值在 LauncherSettings，
        // 和上面「日志颜色」那种自己落盘的不一样 —— 它还要驱动主页那一行）。
        auto *quote = new QCheckBox(QStringLiteral("每日一言"), form);
        quote->setObjectName(dailyQuoteCheckName());
        addSwitch(look, quote, QStringLiteral("每日一言"),
                  QStringLiteral("每天向 hitokoto、ZenQuotes 或 PoetryDB 请求一句，不带个人信息。"
                                 "英文句子来自 ZenQuotes（zenquotes.io）。"));
        dailyQuoteCheck = quote;
    }''')
