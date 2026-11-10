/**
 * @file AgentSecretEdit.hpp
 * @brief API Key 输入：默认星号遮挡，右侧眼睛只在用户按住/点开时显示明文。
 *
 * 不回显已保存值；已有 key 时输入框留空，占位文字说明“留空不修改”。
 */

#pragma once

#include <QWidget>

class QLineEdit;
class QToolButton;

class AgentSecretEdit : public QWidget {
    Q_OBJECT

public:
    explicit AgentSecretEdit(QWidget *parent = nullptr);

    QString text() const;
    void setText(const QString &text);
    void clear();
    bool hasStoredKey() const { return m_hasStored; }
    void setStoredKey(bool stored);
    bool isRevealed() const;
    QLineEdit *lineEdit() const { return m_edit; }
    QToolButton *revealButton() const { return m_reveal; }

signals:
    void textChanged();

private:
    QLineEdit *m_edit = nullptr;
    QToolButton *m_reveal = nullptr;
    bool m_hasStored = false;
};
