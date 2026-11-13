// 扫一棵控件树里所有「用户会输入文字或数字」的控件：宽度、登记情况。
// 内部编辑器（数字框 / 可编辑下拉框里面那格 QLineEdit）不算。
#pragma once

#include "InputSizing.hpp"

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QList>
#include <QString>
#include <QWidget>

struct InputProbe {
    QString cls;
    QString name;
    QString hint;  ///< 占位文字或当前文本，方便人认出是哪一个
    QString kind;  ///< "short" / "long" / ""（未登记）
    QString reason;
    int limit = 0;  ///< 短值登记时算出的宽度上限
    int width = 0;
    int maxWidth = 0;
    QWidget *widget = nullptr;
};

inline QList<InputProbe> scanInputs(QWidget *root) {
    QList<InputProbe> out;
    const auto add = [&out](QWidget *w, const QString &cls, const QString &hint) {
        InputProbe p;
        p.cls = cls;
        p.name = w->objectName();
        p.hint = hint;
        p.kind = w->property(InputSizing::kKindProp).toString();
        p.reason = w->property(InputSizing::kReasonProp).toString();
        p.limit = w->property(InputSizing::kLimitProp).toInt();
        p.width = w->width();
        p.maxWidth = w->maximumWidth();
        p.widget = w;
        out.append(p);
    };
    for (QWidget *w : root->findChildren<QWidget *>()) {
        if (!w->isVisible() || w->width() <= 0) continue;
        if (auto *edit = qobject_cast<QLineEdit *>(w)) {
            QWidget *owner = edit->parentWidget();
            if (qobject_cast<QAbstractSpinBox *>(owner) != nullptr || qobject_cast<QComboBox *>(owner) != nullptr)
                continue;
            add(edit, QStringLiteral("QLineEdit"), edit->placeholderText().isEmpty() ? edit->text() : edit->placeholderText());
        } else if (auto *spin = qobject_cast<QAbstractSpinBox *>(w)) {
            add(spin, QString::fromLatin1(spin->metaObject()->className()), spin->text());
        } else if (auto *combo = qobject_cast<QComboBox *>(w); combo != nullptr && combo->isEditable()) {
            add(combo, QStringLiteral("QComboBox(editable)"), combo->currentText());
        }
    }
    return out;
}
