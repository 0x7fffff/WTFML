/**
 * @file AgentComposer.hpp
 * @brief 输入框：Enter 发送、Shift+Enter 换行、输入法组合期间 Enter 不发送。
 */

#pragma once

#include <QPlainTextEdit>

class AgentComposer : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit AgentComposer(QWidget *parent = nullptr);

    static int minLines();
    static int maxLines();

signals:
    void sendRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;

private:
    bool m_composing = false;
};
