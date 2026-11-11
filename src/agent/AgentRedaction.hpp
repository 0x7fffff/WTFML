/**
 * @file AgentRedaction.hpp
 * @brief 凭据形态的**内容**扫描与遮挡（文件名只做第一道筛，见 docs/features/AGENT.md §5.6.2.1）。
 *
 * 这一层不读盘、不联网：给一段文本，返回“遮过没有、命中了几处、命中了哪几类”。
 * 日志/错误/界面消息在写出之前都要先过它；发送给模型的文本也要先过它。
 */

#pragma once

#include <QString>
#include <QStringList>

namespace AgentRedaction {

struct Result {
    QString text;
    int hits = 0;
    QStringList kinds;
    bool containsCredentials() const { return hits > 0; }
};

/// 遮挡可能的凭据，保留字段名/上下文，值替换成 `<redacted>`。
Result redact(const QString &text);

/// 只判断、不构造输出（UI 用来决定要不要提示）。
bool containsCredentials(const QString &text);

/// 脱敏后的预览（最多 @p maxChars 个字符；截断处加省略标记）。
QString preview(const QString &text, int maxChars = 2000);

QString replacement();

}  // namespace AgentRedaction
