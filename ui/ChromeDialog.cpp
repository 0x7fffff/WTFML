#include "ChromeDialog.hpp"

#include "Glass.hpp"
#include "Theme.hpp"
#include "TitleBar.hpp"
#include "WindowChrome.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

namespace {
constexpr int kStripHeight = 34;
constexpr int kButtonWidth = 42;
}  // namespace

int ChromeDialog::stripHeight() { return kStripHeight; }

ChromeDialog::ChromeDialog(QWidget *parent, bool minMax) : QDialog(parent) {
    if (minMax) setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_strip = new QWidget(this);
    m_strip->setObjectName(titleStripName());
    m_strip->setFixedHeight(kStripHeight);
    auto *strip = new QHBoxLayout(m_strip);
    strip->setContentsMargins(16, 0, 0, 0);
    strip->setSpacing(0);
    m_title = new QLabel(m_strip);
    m_title->setObjectName(titleLabelName());
    m_title->setFont(Theme::textFont(m_title->font(), Theme::TextRole::Body));
    // 标题字不接鼠标：WindowChrome 把条上"没有子控件"的地方认成拖窗区（childAt 跳过它）。
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents);
    strip->addWidget(m_title, 0, Qt::AlignVCenter);
    strip->addStretch(1);

    QAbstractButton *minimize = nullptr;
    const auto addButton = [this, strip](TitleBarButton::Glyph glyph, const QString &tip) {
        auto *button = new TitleBarButton(glyph, QColor(), QColor(), m_strip);
        button->setFixedSize(kButtonWidth, kStripHeight);
        button->setToolTip(tip);
        strip->addWidget(button);
        return button;
    };
    if (minMax) {
        minimize = addButton(TitleBarButton::Glyph::Minimize, tr("最小化"));
        m_maximize = addButton(TitleBarButton::Glyph::Maximize, tr("最大化"));
        connect(minimize, &QAbstractButton::clicked, this, &QWidget::showMinimized);
        connect(m_maximize, &QAbstractButton::clicked, this,
                [this] { isMaximized() ? showNormal() : showMaximized(); });
    }
    QAbstractButton *close = addButton(TitleBarButton::Glyph::Close, tr("关闭"));
    close->setObjectName(closeButtonName());
    // 右上角的 × 和按 Esc 一个意思：reject()（exec() 的调用方据此分辨"取消"）。
    connect(close, &QAbstractButton::clicked, this, &QDialog::reject);
    const auto style = [minimize, maximize = m_maximize, close] {
        TitleBar::styleCaptionButtons(minimize, maximize, close);
    };
    style();
    Theme::follow(m_strip, style);
    root->addWidget(m_strip);

    m_body = new QWidget(this);
    m_body->setObjectName(QStringLiteral("chromeDialogBody"));
    root->addWidget(m_body, 1);

    m_chrome = new WindowChrome(this, m_strip, this);
}

bool ChromeDialog::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    if (m_chrome->handleNativeEvent(message, result)) return true;
    return QDialog::nativeEvent(eventType, message, result);
}

void ChromeDialog::paintEvent(QPaintEvent *event) {
    // 和游戏日志窗一样借主窗那张磨砂风景；没有主窗时铺窗口色（WindowChrome 开了半透明，别透成空的）。
    QPainter painter(this);
    if (auto *backdrop = Glass::Backdrop::active()) {
        backdrop->paintScene(painter, rect());
        return;
    }
    painter.fillRect(rect(), palette().color(QPalette::Window));
    painter.end();
    QDialog::paintEvent(event);
}

void ChromeDialog::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::WindowTitleChange) {
        m_title->setText(windowTitle());
    } else if (event->type() == QEvent::WindowStateChange) {
        if (auto *button = qobject_cast<TitleBarButton *>(m_maximize)) {
            button->setRestoreGlyph(isMaximized());
            button->setToolTip(isMaximized() ? tr("还原") : tr("最大化"));
        }
    }
}
