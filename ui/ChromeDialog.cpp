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

ChromeStrip::ChromeStrip(QWidget *window, bool minMax) : QWidget(window), m_window(window) {
    if (minMax) window->setWindowFlags(window->windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);
    setObjectName(ChromeDialog::titleStripName());
    setFixedHeight(kStripHeight);
    auto *strip = new QHBoxLayout(this);
    strip->setContentsMargins(16, 0, 0, 0);
    strip->setSpacing(0);
    m_title = new QLabel(window->windowTitle(), this);
    m_title->setObjectName(ChromeDialog::titleLabelName());
    m_title->setFont(Theme::textFont(m_title->font(), Theme::TextRole::Body));
    // 标题字不接鼠标：WindowChrome 把条上"没有子控件"的地方认成拖窗区（childAt 跳过它）。
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents);
    strip->addWidget(m_title, 0, Qt::AlignVCenter);
    strip->addStretch(1);

    QAbstractButton *minimize = nullptr;
    const auto addButton = [this, strip](TitleBarButton::Glyph glyph, const QString &tip) {
        auto *button = new TitleBarButton(glyph, QColor(), QColor(), this);
        button->setFixedSize(kButtonWidth, kStripHeight);
        button->setToolTip(tip);
        strip->addWidget(button);
        return button;
    };
    if (minMax) {
        minimize = addButton(TitleBarButton::Glyph::Minimize, ChromeDialog::tr("最小化"));
        m_maximize = addButton(TitleBarButton::Glyph::Maximize, ChromeDialog::tr("最大化"));
        connect(minimize, &QAbstractButton::clicked, window, &QWidget::showMinimized);
        connect(m_maximize, &QAbstractButton::clicked, window,
                [window] { window->isMaximized() ? window->showNormal() : window->showMaximized(); });
    }
    QAbstractButton *close = addButton(TitleBarButton::Glyph::Close, ChromeDialog::tr("关闭"));
    close->setObjectName(ChromeDialog::closeButtonName());
    // 右上角的 × 和按 Esc 一个意思：reject()（exec() 的调用方据此分辨"取消"；子类改写的 reject 照走）。
    connect(close, &QAbstractButton::clicked, window, [window] {
        if (auto *dialog = qobject_cast<QDialog *>(window)) dialog->reject();
        else window->close();
    });
    const auto style = [minimize, maximize = m_maximize, close] {
        TitleBar::styleCaptionButtons(minimize, maximize, close);
    };
    style();
    Theme::follow(this, style);
    window->installEventFilter(this);
}

bool ChromeStrip::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_window) {
        if (event->type() == QEvent::WindowTitleChange) {
            m_title->setText(m_window->windowTitle());
        } else if (event->type() == QEvent::WindowStateChange) {
            if (auto *button = qobject_cast<TitleBarButton *>(m_maximize)) {
                button->setRestoreGlyph(m_window->isMaximized());
                button->setToolTip(m_window->isMaximized() ? ChromeDialog::tr("还原") : ChromeDialog::tr("最大化"));
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

ChromeDialog::ChromeDialog(QWidget *parent, bool minMax) : QDialog(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    m_strip = new ChromeStrip(this, minMax);
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

void ChromeDialog::paintBackdrop(QWidget *window) {
    QPainter painter(window);
    if (auto *backdrop = Glass::Backdrop::active()) {
        backdrop->paintScene(painter, window->rect());
        return;
    }
    painter.fillRect(window->rect(), window->palette().color(QPalette::Window));
}

void ChromeDialog::paintEvent(QPaintEvent *event) {
    // 和游戏日志窗一样借主窗那张磨砂风景。
    paintBackdrop(this);
    QDialog::paintEvent(event);
}
