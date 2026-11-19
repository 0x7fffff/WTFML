// 输入框宽度登记：短值按内容定宽，长值写明为什么拉伸。
// 给写界面的人看：新建 QLineEdit / QSpinBox / 可编辑下拉框后，在这两个函数里选一个；
// 回归检查（tests/ui_check/modules/UiInputWidthCheck.cpp）扫所有页面，
// 宽于 kUnregisteredMaxWidth 又没登记的输入框直接判失败。
#pragma once

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QLineEdit>
#include <QSizePolicy>
#include <QString>
#include <QWidget>

#include <algorithm>

namespace InputSizing {

inline constexpr const char *kKindProp = "wtfmlInputKind";      ///< "short" | "long"
inline constexpr const char *kLimitProp = "wtfmlInputLimit";    ///< 短值：宽度上限（逻辑像素）
inline constexpr const char *kReasonProp = "wtfmlInputReason";  ///< 长值：为什么拉伸

/// 没登记的输入框最宽不许超过这个数；搜索框之类自带 setMaximumWidth 的不必登记。
inline constexpr int kUnregisteredMaxWidth = 420;

/// 短值的上限本身不许超过这个数：再宽就不是短值。
inline constexpr int kShortMaxWidth = 340;

/// 名字类（实例名、预设名、配置名）的字符数。名字不限长，框只是留出常见长度，超出的部分在框里滚动。
inline constexpr int kNameChars = 24;

/// 样式表给输入框的左右留白：3px 透明边 + 8px padding，两侧共 22px，再多留 6px 给光标。
inline constexpr int kHorizontalChrome = 28;

/// 可编辑下拉框右侧箭头占的宽。
inline constexpr int kComboArrow = 28;

enum class Script { Latin, Han };

/// 最多 maxChars 个字符所需的宽度，且不小于占位文字的宽度（占位文字被截断等于没写）。
/// Latin 按数字 `0` 的宽度算，Han 按「汉」算。占位文字要在调用前设好。
inline int widthForChars(const QWidget *widget, int maxChars, Script script = Script::Latin) {
    const QFontMetrics metrics(widget->font());
    const QChar sample = script == Script::Han ? QChar(0x6C49) : QChar(u'0');
    int text = metrics.horizontalAdvance(QString(maxChars, sample));
    const QLineEdit *edit = qobject_cast<const QLineEdit *>(widget);
    if (edit == nullptr)
        if (const auto *combo = qobject_cast<const QComboBox *>(widget)) edit = combo->lineEdit();
    if (edit != nullptr) text = std::max(text, metrics.horizontalAdvance(edit->placeholderText()));
    return text + kHorizontalChrome + (qobject_cast<const QComboBox *>(widget) != nullptr ? kComboArrow : 0);
}

/// 短值（名字、端口、十六进制颜色、版本号……）：宽度按内容封顶，窄窗仍可收缩，不留固定宽。
/// QLineEdit 与可编辑下拉框按字符数定；数字框传 0 用自己的 sizeHint（范围已决定位数），
/// 传字符数则按字符数钉一个宽（同一组几个数字框要一样宽时用，免得右沿齐、左沿参差）。
inline void shortValue(QWidget *widget, int maxChars, Script script = Script::Latin) {
    widget->setProperty(kKindProp, QStringLiteral("short"));
    if (qobject_cast<QAbstractSpinBox *>(widget) != nullptr) {
        QSizePolicy policy = widget->sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Maximum);
        widget->setSizePolicy(policy);
        if (maxChars > 0) {
            const int width = std::min(widthForChars(widget, maxChars, script), kShortMaxWidth);
            widget->setFixedWidth(width);
            widget->setProperty(kLimitProp, width);
            return;
        }
        widget->setProperty(kLimitProp, 0);  // 数字框的上限就是 sizeHint，检查时现取
        return;
    }
    const int limit = std::min(widthForChars(widget, maxChars, script), kShortMaxWidth);
    widget->setProperty(kLimitProp, limit);
    widget->setMaximumWidth(limit);
}

/// 本来就长的值（路径、网址、JVM 参数、命令、哈希、搜索词）：保持拉伸，reason 写一句理由。
inline void longValue(QWidget *widget, const QString &reason) {
    widget->setProperty(kKindProp, QStringLiteral("long"));
    widget->setProperty(kReasonProp, reason);
}

inline void longValue(QWidget *widget, const char *reason) {
    widget->setProperty(kKindProp, QStringLiteral("long"));
    widget->setProperty(kReasonProp, QString::fromUtf8(reason));
}

}  // namespace InputSizing

