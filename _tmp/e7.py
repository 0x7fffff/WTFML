import sys; sys.path.insert(0, '_tmp')
import ed

c = 'src/MainWindow.cpp'
ed.sub(c, '''namespace {

/// 「结构/原理图」那一页顶上那两排「来源」（★ ，见''',
'''namespace {

/// 主页「每日一言」的文字种类：判据层那套（`util/DailyQuote.hpp`）映射到界面层那套
/// （`Theme::QuoteScript`）。两张表不共用类型是分层铁律：ui/ 不许 include src/。
Theme::QuoteScript quoteScriptForTheme(DailyQuote::Script script) {
    switch (script) {
    case DailyQuote::Script::Han: return Theme::QuoteScript::Han;
    case DailyQuote::Script::Kana: return Theme::QuoteScript::Kana;
    case DailyQuote::Script::Latin: return Theme::QuoteScript::Latin;
    }
    return Theme::QuoteScript::Han;
}

/// 「结构/原理图」那一页顶上那两排「来源」（★ ，见''')
