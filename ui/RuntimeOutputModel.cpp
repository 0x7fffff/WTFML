/**
 * @file RuntimeOutputModel.cpp
 * @brief 共用日志模型的实现（形状与取舍见头文件）。
 */

#include "RuntimeOutputModel.hpp"

#include <algorithm>

namespace {

/// 级别字面量 → 枚举（大小写不分）。认不出来回 false。
bool levelFromToken(const QString &token, RuntimeOutputModel::Level *out) {
    struct Entry {
        const char *token;
        RuntimeOutputModel::Level level;
    };
    // TRACE 归 Debug、WARNING 归 Warn、SEVERE 归 Error：都是同一个意思的不同写法
    // （log4j2 / java.util.logging / 各家的老代码），对用户来说"该不该看一眼"才重要。
    static const Entry table[] = {
        {"TRACE", RuntimeOutputModel::Level::Debug},
        {"DEBUG", RuntimeOutputModel::Level::Debug},
        {"INFO", RuntimeOutputModel::Level::Info},
        {"WARN", RuntimeOutputModel::Level::Warn},
        {"WARNING", RuntimeOutputModel::Level::Warn},
        {"ERROR", RuntimeOutputModel::Level::Error},
        {"SEVERE", RuntimeOutputModel::Level::Error},
        {"FATAL", RuntimeOutputModel::Level::Fatal},
    };
    const QString upper = token.trimmed().toUpper();
    if (upper.isEmpty()) {
        return false;
    }
    for (const Entry &entry : table) {
        if (upper == QLatin1String(entry.token)) {
            *out = entry.level;
            return true;
        }
    }
    return false;
}

/// 只认这一行**自己写出来的**级别。返回 false 就表示它可能是上一条的续行。
bool explicitLevelOf(const QString &line, RuntimeOutputModel::Level *out) {
    // ① 行首那串**连着开头**的方括号：`[12:34:56] [main/INFO] [net.minecraft.Foo]`
    // 只认连着开头的那几格 —— 消息体里出现的方括号不算。
    int i = 0;
    while (i < line.size() && line.at(i) == QLatin1Char('[')) {
        const int close = line.indexOf(QLatin1Char(']'), i + 1);
        if (close < 0) {
            break;
        }
        const QString inside = line.mid(i + 1, close - i - 1);
        RuntimeOutputModel::Level level = RuntimeOutputModel::Level::Info;
        if (levelFromToken(inside, &level) ||
            levelFromToken(inside.section(QLatin1Char('/'), -1), &level)) {
            if (out != nullptr) {
                *out = level;
            }
            return true;
        }
        i = close + 1;
        while (i < line.size() && line.at(i).isSpace()) {
            ++i;
        }
    }

    // ② 开头那一小段里**带冒号**的裸词（`12:34:56 WARN: msg`，java.util.logging 那一套）。
    // 冒号是必须的：不然 `Loaded 3 INFO files` 会被认成 INFO。
    const QString head = line.left(RuntimeOutputModel::levelScanHead());
    int p = 0;
    while (p < head.size()) {
        if (!head.at(p).isLetter()) {
            ++p;
            continue;
        }
        int end = p;
        while (end < head.size() && head.at(end).isLetter()) {
            ++end;
        }
        int after = end;
        while (after < head.size() && head.at(after) == QLatin1Char(' ')) {
            ++after;
        }
        if (after < head.size() && head.at(after) == QLatin1Char(':')) {
            RuntimeOutputModel::Level level = RuntimeOutputModel::Level::Info;
            if (levelFromToken(head.mid(p, end - p), &level)) {
                if (out != nullptr) {
                    *out = level;
                }
                return true;
            }
        }
        p = end;
    }
    return false;
}

}  // namespace

RuntimeOutputModel::Level RuntimeOutputModel::levelAt(int index) {
    return index >= 0 && index < levelCount() ? static_cast<Level>(index) : Level::Info;
}

RuntimeOutputModel::Level RuntimeOutputModel::classifyLine(const QString &line,
                                                           Level fallback) {
    Level level = Level::Info;
    if (explicitLevelOf(line, &level)) {
        return level;
    }
    return fallback;
}

RuntimeOutputModel::RuntimeOutputModel(int capacity) {
    // 先摆好两张定长的表再定容量：`setCapacity()` 会顺着 m_counts 减计数，顺序反了就是
    // 往一张空表里写。
    m_counts = QVector<int>(levelCount(), 0);
    m_droppedByLevel = QVector<int>(levelCount(), 0);
    m_visible = QVector<bool>(levelCount(), true);
    setCapacity(capacity);
}

void RuntimeOutputModel::setCapacity(int capacity) {
    m_capacity = qMax(1, capacity);
    while (m_lines.size() > m_capacity) {
        const Line dropped = m_lines.takeFirst();
        ++m_dropped;
        if (!m_droppedByLevel.isEmpty()) {
            ++m_droppedByLevel[levelIndex(dropped.level)];
        }
        if (dropped.counted && !m_counts.isEmpty()) {
            --m_counts[levelIndex(dropped.level)];
        }
    }
}

void RuntimeOutputModel::clear() {
    m_lines.clear();
    m_counts.fill(0);
    // 被上限挤掉的那几行也一起归零：用户按「清空」是**主动**不要这些行了，之后再导出的
    // 报告不该还拿它们说"原始共 N 行"。
    m_dropped = 0;
    m_droppedByLevel.fill(0);
}

RuntimeOutputModel::Level RuntimeOutputModel::addLine(const QString &text) {
    // 认不出来就跟上一行：崩栈的 `at net.minecraft...` 续行、我们自己补的那句
    // "游戏已退出" 都属于上一条日志，跟着上一行的颜色读起来正对。
    // 模型还是空的（第一行就没级别）时算 Info。
    const Level fallback = lastLevel();
    Level level = Level::Info;
    const bool hasOwnLevel = explicitLevelOf(text, &level);
    if (!hasOwnLevel) {
        // 空行是记录之间的分隔，不该继承上一条 ERROR 的颜色；同时也不算一条 INFO。
        level = text.isEmpty() ? Level::Info : fallback;
    }
    // 没写级别的普通输出在 INFO 上按一行一条记；但 ERROR/WARN/FATAL/DEBUG 后面的无级别行
    // 是崩栈或正文续行，只跟随颜色，不重复增加严重级别计数。
    const bool counted = hasOwnLevel || (!text.isEmpty() && level == Level::Info);
    m_lines.append(Line{level, text, counted});
    if (counted) {
        ++m_counts[levelIndex(level)];
    }
    if (m_lines.size() > m_capacity) {
        const Line dropped = m_lines.takeFirst();
        ++m_dropped;
        ++m_droppedByLevel[levelIndex(dropped.level)];
        if (dropped.counted) {
            --m_counts[levelIndex(dropped.level)];
        }
    }
    return level;
}

int RuntimeOutputModel::droppedLineCountFrom(Level minLevel) const {
    // 被挤掉的行是按**每一行自己的级别**记的（续行也算一行 —— 导出那一段就是逐行输出，
    // 续行本来也会出现在 `textFrom()` 里）。
    int count = 0;
    for (int index = levelIndex(minLevel); index < m_droppedByLevel.size(); ++index) {
        count += m_droppedByLevel.at(index);
    }
    return count;
}

int RuntimeOutputModel::countFor(Level level) const {
    const int index = levelIndex(level);
    return index >= 0 && index < m_counts.size() ? m_counts.at(index) : 0;
}

RuntimeOutputModel::Level RuntimeOutputModel::lastLevel() const {
    return m_lines.isEmpty() ? Level::Info : m_lines.last().level;
}

void RuntimeOutputModel::setLevelVisible(Level level, bool visible) {
    const int index = levelIndex(level);
    if (index >= 0 && index < m_visible.size()) {
        m_visible[index] = visible;
    }
}

bool RuntimeOutputModel::levelVisible(Level level) const {
    const int index = levelIndex(level);
    return index >= 0 && index < m_visible.size() ? m_visible.at(index) : true;
}

void RuntimeOutputModel::setSearchText(const QString &text) {
    m_searchText = text;
}

void RuntimeOutputModel::setDisplayLimit(int limit) {
    m_displayLimit = qMax(0, limit);
}

bool RuntimeOutputModel::matches(const Line &line) const {
    return matches(line.level, line.text);
}

bool RuntimeOutputModel::matches(Level level, const QString &text) const {
    if (!levelVisible(level)) {
        return false;
    }
    if (!m_searchText.isEmpty() &&
        !text.contains(m_searchText, Qt::CaseInsensitive)) {
        return false;
    }
    return true;
}

QVector<RuntimeOutputModel::VisibleLine> RuntimeOutputModel::visibleLines() const {
    QVector<VisibleLine> out;
    out.reserve(m_lines.size());
    for (const Line &line : m_lines) {
        if (matches(line)) {
            out.append(VisibleLine{line.level, line.text});
        }
    }
    // 显示上限只留**最后** N 行（最新的那些）。
    if (m_displayLimit > 0 && out.size() > m_displayLimit) {
        out = out.mid(out.size() - m_displayLimit);
    }
    return out;
}

int RuntimeOutputModel::matchingLineCount() const {
    int count = 0;
    for (const Line &line : m_lines) {
        if (matches(line)) {
            ++count;
        }
    }
    return count;
}

QString RuntimeOutputModel::allText() const {
    QStringList parts;
    parts.reserve(m_lines.size());
    for (const Line &line : m_lines) {
        parts.append(line.text);
    }
    return parts.isEmpty() ? QString() : parts.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString RuntimeOutputModel::textFrom(Level minLevel) const {
    QStringList parts;
    parts.reserve(m_lines.size());
    for (const Line &line : m_lines) {
        if (levelIndex(line.level) >= levelIndex(minLevel)) {
            parts.append(line.text);
        }
    }
    return parts.isEmpty() ? QString() : parts.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString RuntimeOutputModel::visibleText() const {
    QStringList parts;
    for (const Line &line : m_lines) {
        if (matches(line)) {
            parts.append(line.text);
        }
    }
    return parts.isEmpty() ? QString() : parts.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

// ---------------------------------------------------------------------------
// 「显示多少行」那几档（两页共用同一份文案）
// ---------------------------------------------------------------------------

QVector<int> RuntimeOutputModel::lineLimitChoices() {
    // 六档，从"只想看最后一段"到"全都看"。最后一档是 0（全部）—— 它同时是默认值，
    // 也就是"打开这个窗看到的和 之前一样"。
    //
    // 第四档是 5000：用户当天原话里打的是「25」，那是**打错了**（他随后订正为 5000）。
    // 所以这一档**不是**从原话推出来的，是问出来的 —— 别照着「25」去猜 250。
    return {500, 1000, 2000, 5000, 10000, 0};
}

QString RuntimeOutputModel::lineLimitText(int limit) {
    // 数字**写全**（用户 说的「直接写数字」，别学他打的 1k/2k）。
    if (limit <= 0) {
        return QStringLiteral("全部");
    }
    return QStringLiteral("%1 条").arg(limit);
}

int RuntimeOutputModel::lineLimitOfIndex(int index) {
    const QVector<int> choices = lineLimitChoices();
    if (index < 0 || index >= choices.size()) {
        return 0;  // 越界回「全部」：不为一个越界把上限设成一个看不见的怪数
    }
    return choices.at(index);
}
