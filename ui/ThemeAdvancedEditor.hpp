#pragma once
#include "Theme.hpp"
#include <QWidget>

class QComboBox;
class QPushButton;
class QSpinBox;
class QLabel;

/// 局部主题编辑器；仅编辑 Spec，不读写设置，持久化由主题控制器统一负责。
class ThemeAdvancedEditor final : public QWidget {
    Q_OBJECT
public:
    explicit ThemeAdvancedEditor(QWidget *parent = nullptr);
    void setSpec(const Theme::Spec &spec);
    void setRoleColor(int role, const QColor &color);
    void setPartColor(int part, bool foreground, const QColor &color);
    void resetPart(int part);
signals:
    void roleColorRequested(int role);
    void partColorRequested(int part, bool foreground);
    void specEdited(const Theme::Spec &spec);
private:
    void refresh();
    void publish();
    Theme::Spec m_spec;
    QComboBox *m_role = nullptr;
    QComboBox *m_part = nullptr;
    QPushButton *m_roleColor = nullptr;
    QPushButton *m_background = nullptr;
    QPushButton *m_foreground = nullptr;
    QLabel *m_roleState = nullptr;
    QSpinBox *m_opacity = nullptr;
    QWidget *m_foregroundRow = nullptr;
};
