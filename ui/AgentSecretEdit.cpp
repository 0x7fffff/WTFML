/**
 * @file AgentSecretEdit.cpp
 * @brief 密码框 + 眼睛按钮（星号由控件级 QProxyStyle 提供）。
 */

#include "AgentSecretEdit.hpp"
#include "InputSizing.hpp"
#include "Theme.hpp"
#include "VersionIcons.hpp"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QProxyStyle>
#include <QStyleOption>
#include <QToolButton>

namespace {
/// 只作用于这一格输入框：把 Qt 默认的圆点密码字符换成 `*`。
class StarPasswordStyle : public QProxyStyle {
public:
    explicit StarPasswordStyle(QStyle *base) : QProxyStyle(base) {}
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *returnData) const override {
        if (hint == SH_LineEdit_PasswordCharacter) {
            return int('*');
        }
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};
}  // namespace

AgentSecretEdit::AgentSecretEdit(QWidget *parent) : QWidget(parent) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    m_edit = new QLineEdit(this);
    m_edit->setEchoMode(QLineEdit::Password);
    m_edit->setStyle(new StarPasswordStyle(m_edit->style()));
    m_edit->setAutoFillBackground(true);
    InputSizing::longValue(m_edit, "API 密钥：一般 40~100 个字符，要能看全");
    layout->addWidget(m_edit, 1);
    m_reveal = new QToolButton(this);
    m_reveal->setCheckable(true);
    m_reveal->setIconSize(QSize(18, 18));
    m_reveal->setAccessibleName(QStringLiteral("显示密钥"));
    m_reveal->setFocusPolicy(Qt::StrongFocus);
    // 扁平：不留系统 QToolButton 那圈方框（悬停 / 按住才出底）。
    m_reveal->setAutoRaise(true);
    layout->addWidget(m_reveal);

    // 图标是画的（VersionIcons::eye），墨色随主题重取；看得见时画带斜杠那只。
    const auto paintIcon = [this] {
        const bool shown = m_reveal->isChecked();
        m_reveal->setIcon(QIcon(VersionIcons::eye(18 * 2, Theme::tokens().secondary, shown)));
        m_reveal->setToolTip(shown ? QStringLiteral("遮住密钥") : QStringLiteral("显示密钥（失焦后自动遮住）"));
    };
    paintIcon();
    Theme::follow(m_reveal, paintIcon);
    connect(m_reveal, &QToolButton::toggled, this, [this, paintIcon](bool checked) {
        m_edit->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
        paintIcon();
    });
    connect(m_edit, &QLineEdit::textChanged, this, &AgentSecretEdit::textChanged);
    connect(m_edit, &QLineEdit::editingFinished, this, [this] {
        // 失焦/回车后恢复遮挡，并且不把明文留在可读状态。
        m_reveal->setChecked(false);
    });
}

QString AgentSecretEdit::text() const { return m_edit->text(); }
void AgentSecretEdit::setText(const QString &text) { m_edit->setText(text); }
void AgentSecretEdit::clear() { m_edit->clear(); }
bool AgentSecretEdit::isRevealed() const { return m_edit->echoMode() == QLineEdit::Normal; }

void AgentSecretEdit::setStoredKey(bool stored) {
    m_hasStored = stored;
    m_edit->setPlaceholderText(
        stored ? QStringLiteral("已保存（留空不修改）")
               : QStringLiteral("尚未保存 API Key"));
}
