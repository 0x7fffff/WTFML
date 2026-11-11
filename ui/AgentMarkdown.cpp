/**
 * @file AgentMarkdown.cpp
 * @brief 逐行 Markdown 渲染（先转义 HTML，再放行白名单标签）。
 */

#include "AgentMarkdown.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace {

QString escapeHtml(const QString &text) {
    QString escaped = text;
    escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    escaped.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    escaped.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return escaped;
}

QString inlineMarkdown(const QString &text) {
    QString result;
    result.reserve(text.size());
    int i = 0;
    while (i < text.size()) {
        if (text.at(i) == QLatin1Char('`')) {
            const int end = text.indexOf(QLatin1Char('`'), i + 1);
            if (end > i) {
                result += QStringLiteral("<code>") +
                          escapeHtml(text.mid(i + 1, end - i - 1)) + QStringLiteral("</code>");
                i = end + 1;
                continue;
            }
        }
        if (text.mid(i, 2) == QLatin1String("**")) {
            const int end = text.indexOf(QStringLiteral("**"), i + 2);
            if (end > i) {
                result += QStringLiteral("<b>") +
                          inlineMarkdown(text.mid(i + 2, end - i - 2)) + QStringLiteral("</b>");
                i = end + 2;
                continue;
            }
        }
        if (text.at(i) == QLatin1Char('[')) {
            const int close = text.indexOf(QStringLiteral("]("), i + 1);
            const int end = close >= 0 ? text.indexOf(QLatin1Char(')'), close + 2) : -1;
            if (close > i && end > close) {
                const QString label = text.mid(i + 1, close - i - 1);
                const QString url = text.mid(close + 2, end - close - 2);
                if ((url.startsWith(QLatin1String("https://")) ||
                     url.startsWith(QLatin1String("http://"))) &&
                    !url.contains(QLatin1Char('"')) && !url.contains(QLatin1Char('\''))) {
                    result += QStringLiteral("<a href=\"") + escapeHtml(url) +
                              QStringLiteral("\">") + inlineMarkdown(label) +
                              QStringLiteral("</a>");
                    i = end + 1;
                    continue;
                }
            }
        }
        if (text.at(i) == QLatin1Char('*') || text.at(i) == QLatin1Char('_')) {
            const QChar marker = text.at(i);
            const int end = text.indexOf(marker, i + 1);
            if (end > i + 1) {
                result += QStringLiteral("<i>") +
                          inlineMarkdown(text.mid(i + 1, end - i - 1)) + QStringLiteral("</i>");
                i = end + 1;
                continue;
            }
        }
        result += escapeHtml(QString(text.at(i)));
        ++i;
    }
    return result;
}

}  // namespace

namespace AgentMarkdown {

QString toHtml(const QString &markdown) {
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    QString html;
    bool inCode = false;
    bool inList = false;
    bool inTable = false;
    QString paragraph;

    const auto flushParagraph = [&] {
        if (!paragraph.isEmpty()) {
            html += QStringLiteral("<p>") + inlineMarkdown(paragraph) + QStringLiteral("</p>");
            paragraph.clear();
        }
    };
    const auto closeList = [&] {
        if (inList) {
            html += QStringLiteral("</ul>");
            inList = false;
        }
    };

    for (int index = 0; index < lines.size(); ++index) {
        const QString line = lines.at(index);
        if (line.startsWith(QLatin1String("```"))) {
            flushParagraph();
            closeList();
            if (inCode) {
                html += QStringLiteral("</code></pre>");
                inCode = false;
            } else {
                html += QStringLiteral("<pre><code>");
                inCode = true;
            }
            continue;
        }
        if (inCode) {
            html += escapeHtml(line) + QLatin1Char('\n');
            continue;
        }
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            flushParagraph();
            closeList();
            if (inTable) {
                html += QStringLiteral("</table>");
                inTable = false;
            }
            continue;
        }
        if (trimmed.startsWith(QLatin1Char('|')) && trimmed.endsWith(QLatin1Char('|'))) {
            flushParagraph();
            closeList();
            QStringList cells = trimmed.mid(1, trimmed.size() - 2).split(QLatin1Char('|'));
            bool separator = true;
            for (const QString &cell : cells) {
                QString value = cell.trimmed();
                value.remove(QLatin1Char('-'));
                value.remove(QLatin1Char(':'));
                if (value.trimmed().size() != 0) {
                    separator = false;
                }
            }
            if (separator) {
                continue;  // |---|:--:| 这一行只是分隔
            }
            if (!inTable) {
                html += QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\">");
                inTable = true;
            }
            html += QStringLiteral("<tr>");
            for (const QString &cell : cells) {
                html += QStringLiteral("<td>") + inlineMarkdown(cell.trimmed()) + QStringLiteral("</td>");
            }
            html += QStringLiteral("</tr>");
            continue;
        }
        if (inTable) {
            html += QStringLiteral("</table>");
            inTable = false;
        }
        if (trimmed.startsWith(QLatin1String("---")) || trimmed.startsWith(QLatin1String("***"))) {
            flushParagraph();
            closeList();
            html += QStringLiteral("<hr/>");
            continue;
        }
        if (trimmed.startsWith(QLatin1Char('#'))) {
            flushParagraph();
            closeList();
            int level = 0;
            while (level < trimmed.size() && level < 6 && trimmed.at(level) == QLatin1Char('#')) {
                ++level;
            }
            const QString text = trimmed.mid(level).trimmed();
            html += QStringLiteral("<h%1>%2</h%1>").arg(level).arg(inlineMarkdown(text));
            continue;
        }
        if (trimmed.startsWith(QLatin1String("> ")) || trimmed == QLatin1String(">")) {
            flushParagraph();
            closeList();
            html += QStringLiteral("<blockquote>") + inlineMarkdown(trimmed.mid(1).trimmed()) +
                    QStringLiteral("</blockquote>");
            continue;
        }
        const bool bullet = trimmed.startsWith(QLatin1String("- ")) ||
                            trimmed.startsWith(QLatin1String("* "));
        if (bullet) {
            flushParagraph();
            if (!inList) {
                html += QStringLiteral("<ul>");
                inList = true;
            }
            html += QStringLiteral("<li>") + inlineMarkdown(trimmed.mid(2)) + QStringLiteral("</li>");
            continue;
        }
        closeList();
        if (!paragraph.isEmpty()) paragraph += QLatin1Char('\n');
        paragraph += trimmed;
    }
    if (inCode) {
        html += QStringLiteral("</code></pre>");
    }
    flushParagraph();
    closeList();
    if (inTable) html += QStringLiteral("</table>");
    return html;
}

}  // namespace AgentMarkdown
