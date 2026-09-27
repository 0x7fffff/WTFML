/**
 * @file ConfigEntries.cpp
 * @brief ConfigEntries 的实现：**一趟字符级扫描**。
 * @author 0x7fffff
 * @date 2026-09-27
 * @note 见 .hpp 那一段（为什么不用现成的库、认得出什么认不出什么）。这里补三处实现上
 *       的说明：为什么是"字符级"而不是"按行切"、`B:key=value` 那个 `:` 怎么和 JSON 的
 *       `:` 分开、以及为什么区间记的是字符下标。
 *
 *       ① **字符级，不按行切**：一行里塞得下整个嵌套对象（`{"a":{"b":1}}` 盘上真有），
 *          按行切就得先猜"这一行是不是一整块"；而字符级扫描天然认得出来 —— 括号配对
 *          走一趟就够了。顺带，多行容器（`"list": [` 后面跟几十行）也能正确地下潜到
 *          里面去，而不会把数组里那些元素当成"没有 `=` 的怪行"丢掉（那些行是元素，
 *          本来也不该产生条目）。
 *       ② **`:` 到底是分隔符还是类型前缀**：`.cfg`（1.12 那代 Forge）把类型写在键名里
 *          （`B:shouldShowDuplicateTrees=true`），而 JSON/JSON5 拿 `:` 当分隔符（`"a": 1`）。
 *          分开的判据是量出来的两条一起看：**`:` 前面那一截短（≤2 个字符）、纯字母，
 *          而且这一行后面还有 `=`** —— 才是类型前缀。于是 `B:key=value` 是"键 B:key、
 *          值 value"，`"a": 1` 和 `ab: 1` 都还是"键 a / 键 ab、值 1"。
 *       ③ **字符下标**：客户是 `QPlainTextEdit`（见 .hpp 那 NOTE）。所以整份实现里
 *          **没有一处 `QByteArray`** —— 它连字节都不认识，也就没机会把区间算歪。
 */
#include "ConfigEntries.hpp"

namespace {

/// 空白（**不含 `\n`** —— 换行在这一层是一个"这一行结束了"的信号，不是空白）。
bool isSpaceChar(QChar c) {
    return c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('\r') ||
           c == QLatin1Char('\v') || c == QLatin1Char('\f');
}

bool isQuote(QChar c) { return c == QLatin1Char('"') || c == QLatin1Char('\''); }

bool isAllAlpha(const QString &s) {
    if (s.isEmpty()) {
        return false;
    }
    for (const QChar c : s) {
        if (!c.isLetter()) {
            return false;
        }
    }
    return true;
}

/// 从 `pos` 那个引号开始，这个串到哪儿结束。返回**闭合引号之后**的下标。
///
/// NOTE: 认转义（`\"`、`\\`），也认 TOML 的三引号（`"""` / `'''`）。**没闭合就到行尾**
///       （容错：半截文件、手写漏了一个引号都不该让整份索引空掉）。
int endOfQuoted(const QString &text, int pos) {
    const int n = text.size();
    if (pos < 0 || pos >= n) {
        return n;
    }
    const QChar q = text.at(pos);
    if (!isQuote(q)) {
        return pos;
    }
    if (pos + 2 < n && text.at(pos + 1) == q && text.at(pos + 2) == q) {
        int i = pos + 3;
        while (i + 2 < n) {
            if (text.at(i) == q && text.at(i + 1) == q && text.at(i + 2) == q) {
                return i + 3;
            }
            ++i;
        }
        return n;
    }
    int i = pos + 1;
    while (i < n) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\\') && i + 1 < n) {
            i += 2;
            continue;
        }
        if (c == q) {
            return i + 1;
        }
        if (c == QLatin1Char('\n')) {
            return i;
        }
        ++i;
    }
    return n;
}

/// 带引号的键：把引号剥掉、把 `\"` 那类转义还原。
QString unquote(const QString &raw) {
    if (raw.size() < 2 || !isQuote(raw.at(0)) || raw.at(raw.size() - 1) != raw.at(0)) {
        return raw;
    }
    const QString body = raw.mid(1, raw.size() - 2);
    QString out;
    out.reserve(body.size());
    for (int i = 0; i < body.size(); ++i) {
        if (body.at(i) == QLatin1Char('\\') && i + 1 < body.size()) {
            ++i;
            out += body.at(i);
            continue;
        }
        out += body.at(i);
    }
    return out;
}

/// 注释那一行去掉开头的注释符和一个空格。`#Range: 1 ~ 5` → `Range: 1 ~ 5`。
QString stripMarker(const QString &raw) {
    QString s = raw;
    if (s.startsWith(QLatin1String("//"))) {
        s = s.mid(2);
    } else if (s.startsWith(QLatin1String("/*"))) {
        s = s.mid(2);
        if (s.endsWith(QLatin1String("*/"))) {
            s.chop(2);
        }
    } else if (s.startsWith(QLatin1Char('#')) || s.startsWith(QLatin1Char(';'))) {
        s = s.mid(1);
    }
    if (s.startsWith(QLatin1Char(' '))) {
        s = s.mid(1);
    }
    return s.trimmed();
}

/// 这一串看着是不是个数字。**只判形状**（`+`/`-`、小数、指数、`0x`/`0b`/`0o`），
/// 不转成数、也不管范围 —— 这一层只负责"这一格是数字还是字符串"，改不改得动是后面的事。
bool looksNumeric(const QString &s) {
    if (s.isEmpty()) {
        return false;
    }
    int i = 0;
    if (s.at(i) == QLatin1Char('+') || s.at(i) == QLatin1Char('-')) {
        ++i;
    }
    if (i >= s.size()) {
        return false;
    }
    const QString rest = s.mid(i);
    if (rest.startsWith(QLatin1String("0x"), Qt::CaseInsensitive) ||
        rest.startsWith(QLatin1String("0b"), Qt::CaseInsensitive) ||
        rest.startsWith(QLatin1String("0o"), Qt::CaseInsensitive)) {
        return rest.size() > 2;
    }
    bool digits = false;
    bool dot = false;
    bool exponent = false;
    for (; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c.isDigit()) {
            digits = true;
            continue;
        }
        if (c == QLatin1Char('.') && !dot && !exponent) {
            dot = true;
            continue;
        }
        if ((c == QLatin1Char('e') || c == QLatin1Char('E')) && digits && !exponent) {
            exponent = true;
            if (i + 1 < s.size() &&
                (s.at(i + 1) == QLatin1Char('+') || s.at(i + 1) == QLatin1Char('-'))) {
                ++i;
            }
            continue;
        }
        return false;
    }
    return digits;
}

ConfigEntries::Kind classify(const QString &value) {
    if (value.isEmpty()) {
        return ConfigEntries::Kind::Other;
    }
    const QChar c0 = value.at(0);
    if (isQuote(c0)) {
        return ConfigEntries::Kind::String;
    }
    if (c0 == QLatin1Char('[')) {
        return ConfigEntries::Kind::Array;
    }
    if (c0 == QLatin1Char('{')) {
        return ConfigEntries::Kind::Object;
    }
    if (value == QLatin1String("true") || value == QLatin1String("false")) {
        return ConfigEntries::Kind::Bool;
    }
    if (looksNumeric(value)) {
        return ConfigEntries::Kind::Number;
    }
    return ConfigEntries::Kind::Other;
}

/// 注释里那一句 `Range: 1 ~ 32767`（Forge / NightConfig 生成的那批 TOML 都有）。
QString rangeOf(const QStringList &noteLines) {
    for (const QString &line : noteLines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("Range:"), Qt::CaseInsensitive)) {
            return trimmed.mid(6).trimmed();
        }
    }
    return QString();
}

/// 走一趟正文的那个状态机。**不解析、不建树**，只把位置记下来。
class Walker {
public:
    explicit Walker(const QString &text) : m_text(text), m_n(text.size()) {}

    QList<ConfigEntries::Entry> run() {
        while (m_i < m_n && !m_done) {
            const QChar c = m_text.at(m_i);
            if (c == QLatin1Char('\n')) {
                // 空行 = 上一段注释到此为止。不切这一刀的话，文件开头那段版权说明会一路
                // 挂到第 30 行那个键上 —— "这一条上面写着什么"就成了假话。
                if (m_onlySpace) {
                    m_pending.clear();
                }
                eatTo(m_i + 1);
                continue;
            }
            if (isSpaceChar(c)) {
                eatTo(m_i + 1);
                continue;
            }
            if (atCommentStart()) {
                readComment();
                continue;
            }
            if (c == QLatin1Char('}') || c == QLatin1Char(']')) {
                pop();
                eatTo(m_i + 1);
                continue;
            }
            if (c == QLatin1Char(',')) {
                eatTo(m_i + 1);
                continue;
            }
            if (c == QLatin1Char('[') && atSectionHeader()) {
                readSectionHeader();
                continue;
            }
            if (c == QLatin1Char('{') || c == QLatin1Char('[')) {
                push(QString(), c == QLatin1Char('['));
                eatTo(m_i + 1);
                continue;
            }
            readKey();
        }
        return m_out;
    }

private:
    /// 容器那一层的栈。`segment` 是它在路径里那一段（数组里的元素是 `[0]` 这样）。
    struct Frame {
        QString segment;
        bool isArray = false;
        int index = 0;
    };

    /// 从当前位置吃到 `to`（不含）。**换行、缩进都从这儿过** —— 于是"第几行"和"这一行
    /// 到目前是不是只有空白"这两件事只有一个出处，不用在每个分支里各算一遍。
    void eatTo(int to) {
        const int end = qMin(to, m_n);
        for (; m_i < end; ++m_i) {
            const QChar c = m_text.at(m_i);
            if (c == QLatin1Char('\n')) {
                ++m_line;
                m_onlySpace = true;
            } else if (!isSpaceChar(c)) {
                m_onlySpace = false;
            }
        }
    }

    int lineOf(int pos) const {
        // 只有"认不出引号闭合"那种半截文件才会走到这儿（值的区间跨了行），
        // 那时候按当前位置给个近似行号就够了 —— 索引用它只为区分重名的键。
        Q_UNUSED(pos);
        return m_line;
    }

    bool atCommentStart() const {
        const QChar c = m_text.at(m_i);
        if (c == QLatin1Char('#')) {
            return true;
        }
        // `;` 只在行首算注释（.ini/.cfg 的规矩）—— 行中间那个 `;` 更可能是值里的一部分，
        // 把它当注释会把值截断，而截断出来的索引会指到一个错的区间上。
        if (c == QLatin1Char(';') && m_onlySpace) {
            return true;
        }
        return c == QLatin1Char('/') && m_i + 1 < m_n &&
               (m_text.at(m_i + 1) == QLatin1Char('/') || m_text.at(m_i + 1) == QLatin1Char('*'));
    }

    void readComment() {
        const bool fullLine = m_onlySpace;
        const int begin = m_i;
        int end = m_i;
        if (m_text.at(m_i) == QLatin1Char('/') && m_i + 1 < m_n &&
            m_text.at(m_i + 1) == QLatin1Char('*')) {
            const int close = m_text.indexOf(QLatin1String("*/"), m_i + 2);
            end = close < 0 ? m_n : close + 2;
        } else {
            while (end < m_n && m_text.at(end) != QLatin1Char('\n')) {
                ++end;
            }
        }
        // 只有**整行**注释才算"这个键上面写着什么"。行尾那种（`key = 1 # 注释`）挂在值上，
        // 而值是原样给出的，不会再拼一遍。
        if (fullLine) {
            m_pending.append(stripMarker(m_text.mid(begin, end - begin)));
        }
        eatTo(end);
    }

    /// `[section]` / `[[array of tables]]`。判据：`[` 起头、**本行之内**配得上、`]` 之后
    /// 除空白与注释外没别的东西。
    bool atSectionHeader() const {
        if (m_text.at(m_i) != QLatin1Char('[')) {
            return false;
        }
        int depth = 1;
        int j = m_i + 1;
        while (j < m_n) {
            const QChar c = m_text.at(j);
            if (c == QLatin1Char('\n') || c == QLatin1Char('#') || c == QLatin1Char(';')) {
                return false;
            }
            if (isQuote(c)) {
                return false;
            }
            if (c == QLatin1Char('=') || c == QLatin1Char(',')) {
                return false;  // `["a", "b"]` 那种是值，不是段头
            }
            if (c == QLatin1Char('[')) {
                ++depth;
            } else if (c == QLatin1Char(']')) {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
            ++j;
        }
        if (j >= m_n) {
            return false;
        }
        int k = j + 1;
        while (k < m_n && isSpaceChar(m_text.at(k))) {
            ++k;
        }
        return k >= m_n || m_text.at(k) == QLatin1Char('\n') || m_text.at(k) == QLatin1Char('#');
    }

    void readSectionHeader() {
        int j = m_i + 1;
        while (j < m_n && m_text.at(j) == QLatin1Char('[')) {
            ++j;
        }
        const int nameBegin = j;
        while (j < m_n && m_text.at(j) != QLatin1Char(']')) {
            ++j;
        }
        int nameEnd = j;
        while (nameEnd > nameBegin && isSpaceChar(m_text.at(nameEnd - 1))) {
            --nameEnd;
        }
        QString name = m_text.mid(nameBegin, nameEnd - nameBegin);
        if (name.size() >= 2 && isQuote(name.at(0))) {
            name = unquote(name);
        }
        int end = j;
        while (end < m_n && m_text.at(end) != QLatin1Char('\n')) {
            ++end;
        }
        // 段头把栈**清掉**（TOML / INI 的语义：段不嵌套），于是后面那些键的路径就
        // 从一个干净的地方重新开始。
        m_pending.clear();
        m_path.clear();
        Frame frame;
        frame.segment = name;
        m_path.append(frame);
        eatTo(end);
    }

    void push(const QString &name, bool isArray) {
        Frame frame;
        frame.segment = name;
        frame.isArray = isArray;
        // 数组里的元素：路径上带一截 `[i]`。**只有多行的数组才走得到这儿** —— 一行写完的
        // 那些在 readValue() 里就当成一整条了。
        if (!m_path.isEmpty() && m_path.last().isArray) {
            frame.segment = QStringLiteral("[%1]").arg(m_path.last().index);
            if (!name.isEmpty()) {
                frame.segment += QLatin1Char('.') + name;
            }
        }
        m_path.append(frame);
    }

    void pop() {
        if (!m_path.isEmpty()) {
            m_path.removeLast();
            // 出栈 = 这个元素读完了 ⇒ 外面的数组该数下一个了。
            if (!m_path.isEmpty() && m_path.last().isArray) {
                ++m_path.last().index;
            }
        }
        m_pending.clear();
    }

    QString pathString() const {
        QString out;
        for (const Frame &frame : m_path) {
            if (frame.segment.isEmpty()) {
                continue;
            }
            if (frame.segment.startsWith(QLatin1Char('['))) {
                out += frame.segment;
            } else {
                if (!out.isEmpty()) {
                    out += QLatin1Char('.');
                }
                out += frame.segment;
            }
        }
        return out;
    }

    QStringList segmentsOf() const {
        QStringList out;
        for (const Frame &frame : m_path) {
            if (!frame.segment.isEmpty()) {
                out.append(frame.segment);
            }
        }
        return out;
    }

    /// 一行写完的容器：`[...]` / `{...}` 到配对的那个括号为止。
    /// @return 闭合符**之后**的下标；**跨了行或者没闭合返回 -1**（那种要下潜，不能当一条）。
    int matchingClose(int pos) const {
        const QChar open = m_text.at(pos);
        const QChar close = open == QLatin1Char('[') ? QLatin1Char(']') : QLatin1Char('}');
        int depth = 1;
        int j = pos + 1;
        while (j < m_n) {
            const QChar c = m_text.at(j);
            if (c == QLatin1Char('\n')) {
                return -1;
            }
            if (isQuote(c)) {
                const int end = endOfQuoted(m_text, j);
                if (end <= j) {
                    return -1;
                }
                j = end;
                continue;
            }
            if (c == open) {
                ++depth;
            } else if (c == close) {
                --depth;
                if (depth == 0) {
                    return j + 1;
                }
            }
            ++j;
        }
        return -1;
    }

    void readKey() {
        QString key;
        if (isQuote(m_text.at(m_i))) {
            const int end = endOfQuoted(m_text, m_i);
            key = unquote(m_text.mid(m_i, end - m_i));
            eatTo(end);
        } else {
            int j = m_i;
            while (j < m_n) {
                const QChar c = m_text.at(j);
                if (isSpaceChar(c) || c == QLatin1Char('\n') || c == QLatin1Char('=') ||
                    c == QLatin1Char('{') || c == QLatin1Char('[') || c == QLatin1Char(',') ||
                    c == QLatin1Char('}') || c == QLatin1Char(']') || c == QLatin1Char('#') ||
                    c == QLatin1Char('/')) {
                    break;
                }
                if (c == QLatin1Char(':') && !atTypePrefix(j)) {
                    break;
                }
                ++j;
            }
            key = m_text.mid(m_i, j - m_i);
            eatTo(j);
        }
        if (key.isEmpty()) {
            // 认不出来的东西：吃掉一个字符，别原地打转。
            eatTo(m_i + 1);
            return;
        }

        int k = m_i;
        while (k < m_n && isSpaceChar(m_text.at(k))) {
            ++k;
        }
        if (k < m_n && (m_text.at(k) == QLatin1Char('=') || m_text.at(k) == QLatin1Char(':'))) {
            eatTo(k + 1);
            readValue(key);
            return;
        }
        if (k < m_n && m_text.at(k) == QLatin1Char('{')) {
            // `general {` —— CFG 那种块：它是个分组，不是一条设置。
            m_pending.clear();
            push(key, false);
            eatTo(k + 1);
            return;
        }
        // 别的东西：数组里的元素、或者单纯就是没认出来。不产生条目。
        m_pending.clear();
    }

    /// `:` 这个位置是不是 `.cfg` 的类型前缀（`B:` / `I:` / `S:` / `D:`）。
    ///
    /// 两条一起看：**前面那一截 ≤2 个字符、纯字母**，而且**这一行后面还有 `=`**。
    /// 单看前一条的话，`.properties` 里 `a:b` 这种单字母键会被吞掉；单看后一条的话，
    /// JSON5 里 `{"a": {"b": 1}}` 那一行的 `:` 也会被吞掉。
    bool atTypePrefix(int colonPos) const {
        const int len = colonPos - m_i;
        if (len <= 0 || len > 2 || !isAllAlpha(m_text.mid(m_i, len))) {
            return false;
        }
        for (int j = colonPos + 1; j < m_n && m_text.at(j) != QLatin1Char('\n'); ++j) {
            if (m_text.at(j) == QLatin1Char('=')) {
                return true;
            }
        }
        return false;
    }

    void readValue(const QString &key) {
        while (m_i < m_n && isSpaceChar(m_text.at(m_i))) {
            eatTo(m_i + 1);
        }
        if (m_i >= m_n) {
            return;
        }
        const QChar c = m_text.at(m_i);
        if (isQuote(c)) {
            const int end = endOfQuoted(m_text, m_i);
            emitEntry(key, m_i, end, m_text.mid(m_i, end - m_i));
            eatTo(end);
            return;
        }
        if (c == QLatin1Char('{') || c == QLatin1Char('[')) {
            const int end = matchingClose(m_i);
            if (end > 0) {
                emitEntry(key, m_i, end, m_text.mid(m_i, end - m_i));
                eatTo(end);
                return;
            }
            // 跨行的容器：它自己不是一条（一选就选中几十行没有意义），下潜进去，
            // 里面那些键照样各自成条，路径上带着它的名字。
            m_pending.clear();
            push(key, c == QLatin1Char('['));
            eatTo(m_i + 1);
            return;
        }
        // 裸标量：一路读到这一格的尽头。
        int j = m_i;
        while (j < m_n) {
            const QChar x = m_text.at(j);
            if (x == QLatin1Char('\n') || x == QLatin1Char('#') || x == QLatin1Char(',') ||
                x == QLatin1Char('}') || x == QLatin1Char(']')) {
                break;
            }
            // 行内注释：`1 // 说明` 这种，`//` 前面得有空白才算 —— 不然 `.properties` 里
            // `url=http://…` 那种值会被从中间切开。
            if (x == QLatin1Char('/') && j + 1 < m_n &&
                (m_text.at(j + 1) == QLatin1Char('/') || m_text.at(j + 1) == QLatin1Char('*')) &&
                j > m_i && isSpaceChar(m_text.at(j - 1))) {
                break;
            }
            ++j;
        }
        int valueEnd = j;
        while (valueEnd > m_i && isSpaceChar(m_text.at(valueEnd - 1))) {
            --valueEnd;
        }
        if (valueEnd <= m_i) {
            eatTo(j);
            return;
        }
        emitEntry(key, m_i, valueEnd, m_text.mid(m_i, valueEnd - m_i));
        eatTo(valueEnd);
    }

    void emitEntry(const QString &key, int begin, int end, const QString &value) {
        if (m_out.size() >= ConfigEntries::maxEntries()) {
            m_done = true;
            return;
        }
        const QString prefix = pathString();
        ConfigEntries::Entry entry;
        entry.path = prefix.isEmpty() ? key : prefix + QLatin1Char('.') + key;
        entry.segments = segmentsOf();
        entry.segments.append(key);
        entry.key = key;
        entry.value = value;
        entry.kind = classify(value);
        entry.valueBegin = begin;
        entry.valueEnd = end;
        entry.line = lineOf(begin);
        entry.note = m_pending.join(QLatin1Char('\n'));
        entry.range = rangeOf(m_pending);
        m_pending.clear();
        m_out.append(entry);
    }

    const QString &m_text;
    const int m_n;
    int m_i = 0;
    int m_line = 0;
    bool m_onlySpace = true;
    bool m_done = false;
    QList<Frame> m_path;
    QStringList m_pending;
    QList<ConfigEntries::Entry> m_out;
};

}  // namespace

QList<ConfigEntries::Entry> ConfigEntries::scan(const QString &text) { return Walker(text).run(); }

int ConfigEntries::maxEntries() {
    // 5000 条：足够覆盖盘上任何一份真配置（最大那份 3.96 MB 也不是设置，是数据快照），
    // 又不至于让一次"用户敲了一个字"的重建慢到看得见。
    return 5000;
}

QString ConfigEntries::kindName(Kind kind) {
    switch (kind) {
    case Kind::Bool:
        return QStringLiteral("布尔");
    case Kind::Number:
        return QStringLiteral("数字");
    case Kind::String:
        return QStringLiteral("字符串");
    case Kind::Array:
        return QStringLiteral("数组");
    case Kind::Object:
        return QStringLiteral("对象");
    case Kind::Other:
        break;
    }
    return QStringLiteral("未识别");
}
