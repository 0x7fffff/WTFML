/**
 * @file AgentComposer.cpp
 * @brief 输入框的键盘边界。
 */

#include "AgentComposer.hpp"

#include <QFontMetrics>
#include <QInputMethodEvent>
#include <QKeyEvent>

AgentComposer::AgentComposer(QWidget *parent) : QPlainTextEdit(parent) {
    setObjectName(QStringLiteral("agentComposer"));
    setTabChangesFocus(true);
    setPlaceholderText(QStringLiteral("描述你遇到的问题…（Enter 发送，Shift+Enter 换行）"));
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

int AgentComposer::minLines() { return 1; }
int AgentComposer::maxLines() { return 8; }

void AgentComposer::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const bool shift = event->modifiers() & Qt::ShiftModifier;
        if (!shift && !m_composing && !(event->modifiers() & Qt::ControlModifier)) {
            emit sendRequested();
            event->accept();
            return;
        }
    }
    QPlainTextEdit::keyPressEvent(event);
}

void AgentComposer::inputMethodEvent(QInputMethodEvent *event) {
    m_composing = !event->commitString().size() && !event->preeditString().isEmpty();
    QPlainTextEdit::inputMethodEvent(event);
}
