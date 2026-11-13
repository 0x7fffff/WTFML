import sys; sys.path.insert(0, '_tmp')
import ed

h = 'src/MainWindow.hpp'
ed.sub(h, '''class ConnectionsController;
class InstanceIconController;''', '''class ConnectionsController;
class DailyQuoteController;
class InstanceIconController;''')
ed.sub(h, '''    ConnectionsController *m_connectionsController = nullptr;''',
'''    ConnectionsController *m_connectionsController = nullptr;
    /// 主页「每日一言」：取句、当天缓存与句子池（见 controllers/DailyQuoteController.hpp）。
    DailyQuoteController *m_dailyQuote = nullptr;''')
ed.sub(h, '''    void onShowGameLogChanged();''', '''    void onShowGameLogChanged();
    /// 设置页「每日一言」那个开关：立刻落盘，开着就按当前日期取一句（关着不发任何请求）。
    void onDailyQuoteChanged();''')

c = 'src/MainWindow.cpp'
ed.sub(c, '''    setupConnections();''',
'''    // ---------- 主页「每日一言」 ----------
    // 建在 setupConnections() 之前：设置页那个开关的槽要它。取句全在后台异步，
    // 起点是下面 applySettingsToUi() 之后的 refresh()（关着就一个请求都不发）。
    m_dailyQuote = new DailyQuoteController(this, m_settings);
    m_dailyQuote->shown = [this](const DailyQuote::Quote &quote, bool visible) {
        m_ui->setHomeHeroQuote(visible ? quote.text : QString(),
                               visible ? DailyQuote::attribution(quote) : QString(),
                               quoteScriptForTheme(quote.script));
    };
    setupConnections();''')

ed.sub(c, '''    applySettingsToUi();''',
'''    applySettingsToUi();
    // 主页那一行到这一步才决定：盘上关着就什么都不发；开着先显示缓存 / 池 / 兜底，
    // 网络回来再替换（不阻塞首帧）。
    m_dailyQuote->refresh();''')

ed.sub(c, '''#include "controllers/HomeQuickController.hpp"''',
'''#include "controllers/DailyQuoteController.hpp"
#include "controllers/HomeQuickController.hpp"''')
