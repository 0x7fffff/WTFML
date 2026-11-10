/**
 * @file AgentMarkdownTests.cpp
 * @brief 受控 Markdown：HTML 转义、脚本/图片移除、围栏、链接、表格、CJK。
 */

#include "Support.hpp"

#include "AgentMarkdown.hpp"

void runAgentMarkdownTests() {
    {
        const QString html = AgentMarkdown::toHtml(QStringLiteral("<script>alert(1)</script>"));
        check(!html.contains(QStringLiteral("<script")), "Markdown：原始 script 被转义");
        check(html.contains(QStringLiteral("&lt;script&gt;")), "Markdown：script 以文本显示");
    }
    {
        const QString html = AgentMarkdown::toHtml(QStringLiteral("![x](https://evil/x.png)"));
        check(!html.contains(QStringLiteral("<img")), "Markdown：远程图片不生成 img");
        check(!html.contains(QStringLiteral("<iframe")), "Markdown：不生成 iframe");
    }
    {
        const QString html = AgentMarkdown::toHtml(
            QStringLiteral("**粗体** 和 *斜体* 和 `code` 和 [链接](https://example.com)"));
        check(html.contains(QStringLiteral("<b>粗体</b>")), "Markdown：粗体");
        check(html.contains(QStringLiteral("<i>斜体</i>")), "Markdown：斜体");
        check(html.contains(QStringLiteral("<code>code</code>")), "Markdown：行内代码");
        check(html.contains(QStringLiteral("href=\"https://example.com\"")),
              "Markdown：http(s) 链接");
    }
    {
        const QString html = AgentMarkdown::toHtml(QStringLiteral("[坏链接](javascript:alert(1))"));
        check(!html.contains(QStringLiteral("href=\"javascript")),
              "Markdown：非 http(s) 链接不生成 href");
    }
    {
        const QString html = AgentMarkdown::toHtml(QStringLiteral("```cpp\nint a = 1;\n```"));
        check(html.contains(QStringLiteral("<pre><code>")) &&
                  html.contains(QStringLiteral("int a = 1;")),
              "Markdown：围栏代码块");
        const QString unclosed = AgentMarkdown::toHtml(QStringLiteral("```\nhello"));
        check(unclosed.contains(QStringLiteral("</code></pre>")),
              "Markdown：未闭合围栏降级为普通代码块并收尾");
    }
    {
        const QString html = AgentMarkdown::toHtml(
            QStringLiteral("# 标题\n\n- 一\n- 二\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\n> 引用"));
        check(html.contains(QStringLiteral("<h1>标题</h1>")), "Markdown：标题");
        check(html.contains(QStringLiteral("<ul>")) && html.contains(QStringLiteral("<li>一</li>")),
              "Markdown：无序列表");
        check(html.contains(QStringLiteral("<table")) && html.contains(QStringLiteral("<td>1</td>")),
              "Markdown：简单表格");
        check(html.contains(QStringLiteral("<blockquote>引用</blockquote>")), "Markdown：引用");
    }
    {
        const QString html = AgentMarkdown::toHtml(QStringLiteral("中文 **加粗** 😀"));
        check(html.contains(QStringLiteral("中文")) && html.contains(QStringLiteral("<b>加粗</b>")),
              "Markdown：CJK 正常");
    }
}
