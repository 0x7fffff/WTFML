import sys; sys.path.insert(0, '_tmp')
import ed

h = 'ui/Theme.hpp'
ed.sub(h, '''enum class HandFont { Kai, KaiLight, Script };''',
'''enum class HandFont { Kai, KaiLight, Script };
/// 主页「每日一言」那一行的文字种类：决定用哪支字体、以及限长（限长在
/// `util/DailyQuote` 里判，这一层只管字体）。名字和那边的 `DailyQuote::Script`
/// 一一对应，但**不能共用那个类型**：ui/ 不许 include src/（分层铁律）。
enum class QuoteScript { Latin, Han, Kana };
/// 三类文字各自的 family 列表（第一档是主字体，后面是回退）。
/// - 拉丁：Dancing Script（品牌手写体）；
/// - 汉字无假名：文楷 Light（和主页大标题同一支）；
/// - 含假名：Windows 自带的日文字体（Yu Mincho → Yu Gothic UI → Meiryo UI）。
///   **文楷没有假名**：实测 assets/fonts 那两支文楷的 cmap 只有 3911 个码点，
///   0x3040–0x30FF（平假名 / 片假名 / 长音符「ー」）一个都没有，连「々」也没有；
///   把文楷排在前面只会让日文句子里汉字用文楷、假名落回系统字，一行两种字形。
QStringList quoteFamilies(QuoteScript script);
/// 「每日一言」的字号也是分档的：正常 16，窄窗 14（见 MainWindowUI 的 applyScale）。
QFont quoteFont(QFont font, QuoteScript script, int pixelSize);''')

c = 'ui/Theme.cpp'
ed.sub(c, '''QFont handFont(QFont font, HandFont which) {''',
'''QStringList quoteFamilies(QuoteScript script) {
    const QString base = QApplication::font().family();
    switch (script) {
    case QuoteScript::Kana:
        return {QStringLiteral("Yu Mincho"), QStringLiteral("Yu Gothic UI"),
                QStringLiteral("Meiryo UI"), base};
    case QuoteScript::Han: {
        const QString kai = handFamily(HandFont::KaiLight);
        return kai.isEmpty() ? QStringList{base} : QStringList{kai, base};
    }
    case QuoteScript::Latin: {
        const QString latin = handFamily(HandFont::Script);
        return latin.isEmpty() ? QStringList{base} : QStringList{latin, base};
    }
    }
    return {base};
}

QFont quoteFont(QFont font, QuoteScript script, int pixelSize) {
    font.setFamilies(quoteFamilies(script));
    font.setPixelSize(pixelSize);
    font.setBold(false);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    return font;
}

QFont handFont(QFont font, HandFont which) {''')
