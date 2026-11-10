/**
 * @file AgentMarkdown.hpp
 * @brief 智能体消息的受控 Markdown → HTML（只给 QLabel::setText 用）。
 *
 * 允许：标题、段落、列表、引用、粗体/斜体、行内代码、围栏代码块、http(s) 链接、
 * 水平线、简单表格。禁止：原始 HTML、脚本、iframe、远程图片、自动加载资源。
 * 未闭合围栏按普通代码块降级。
 */

#pragma once

#include <QString>

namespace AgentMarkdown {
QString toHtml(const QString &markdown);
}
