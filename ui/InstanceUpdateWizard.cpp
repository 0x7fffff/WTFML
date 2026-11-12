/**
 * @file InstanceUpdateWizard.cpp
 * @brief 四步原生窗口；收起的下拉框不响应滚轮，列表不向父级传递滚动。
 */
#include "InstanceUpdateWizard.hpp"
#include "ChromeDialog.hpp"
#include "WindowChrome.hpp"
#include "OverlayHost.hpp"
#include "Theme.hpp"
#include <QAbstractItemView>
#include <QAbstractButton>
#include <QComboBox>
#include <QEvent>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace {
class Step final : public QWizardPage {
public:
    bool ready = false;
    bool isComplete() const override { return ready; }
    void refresh(bool value) { if (ready != value) { ready = value; emit completeChanged(); } }
};
/// 页头右侧的步骤指示：四颗小圆点，当前那颗拉长成主题色胶囊，走过的淡主题色，没到的轨道色。
class StepDots final : public QWidget {
public:
    StepDots(int current, int count, QWidget *parent) : QWidget(parent), m_current(current), m_count(count) {
        setObjectName(QStringLiteral("instanceUpdateStepDots"));
        setFixedSize((count - 1) * (kDot + kGap) + kCurrent, kDot);
        Theme::follow(this, [this] { update(); });
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        const Theme::Tokens tokens = Theme::tokens();
        qreal x = 0;
        for (int i = 0; i < m_count; ++i) {
            QColor fill = i > m_current ? tokens.text : tokens.primary;
            if (i < m_current) fill.setAlphaF(0.5);
            else if (i > m_current) fill.setAlphaF(0.18);
            const int width = i == m_current ? kCurrent : kDot;
            painter.setBrush(fill);
            painter.drawRoundedRect(QRectF(x, 0, width, kDot), kDot / 2.0, kDot / 2.0);
            x += width + kGap;
        }
    }
private:
    static constexpr int kDot = 8, kGap = 6, kCurrent = 20;
    int m_current, m_count;
};
}
namespace {
// 正文区边距（见 TL2）：左右 24、标题条下 16；按钮上方那条分隔线离按钮 12。
constexpr int kPageMarginLeft = 24, kPageMarginTop = 16, kPageMarginRight = 24, kPageMarginBottom = 16, kFooterGap = 12;
}
InstanceUpdateWizard::InstanceUpdateWizard(QWidget *target, QWidget *mods, QWidget *confirmation,
    QWidget *progress, QWidget *parent) : QWizard(parent) {
    setObjectName(QStringLiteral("instanceUpdateWizard"));
    setWindowTitle(tr("实例更新向导"));
    setWindowModality(Qt::WindowModal);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    // Classic + 页面不设标题 = QWizard 自己不画页头（Modern 那条页头字小、底色一整块）；页头每页自己排。
    setWizardStyle(QWizard::ClassicStyle);
    setMinimumSize(640, 460); resize(720, 520);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setOption(QWizard::NoBackButtonOnLastPage, true);
    const QStringList titles{tr("选择目标版本"), tr("检查并处理模组"), tr("确认更新方案"), tr("执行与结果")};
    const QList<QWidget *> content{target, mods, confirmation, progress};
    const QByteArray textRole = Theme::textRolePropertyName().toUtf8();
    for (int i = 0; i < content.size(); ++i) {
        auto *step = new Step;
        auto *layout = new QVBoxLayout(step);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto *header = new QHBoxLayout;
        auto *texts = new QVBoxLayout; texts->setSpacing(2);
        auto *title = new QLabel(titles[i], step);
        title->setObjectName(QStringLiteral("instanceUpdateStepTitle"));
        title->setFont(Theme::textFont(title->font(), Theme::TextRole::Heading));
        auto *subtitle = new QLabel(tr("第 %1 步 / 共 %2 步").arg(i + 1).arg(content.size()), step);
        subtitle->setObjectName(QStringLiteral("instanceUpdateStepSubtitle"));
        subtitle->setFont(Theme::textFont(subtitle->font(), Theme::TextRole::Caption));
        subtitle->setProperty(textRole.constData(), Theme::secondaryTextRoleValue());
        texts->addWidget(title); texts->addWidget(subtitle);
        header->addLayout(texts); header->addStretch(1);
        header->addWidget(new StepDots(i, int(content.size()), step), 0, Qt::AlignVCenter);
        layout->addLayout(header);
        layout->addSpacing(16);
        // 正文贴着页头的左沿排：调用方那块自带的外边距去掉（边距统一由向导给）。
        if (QLayout *inner = content[i]->layout()) inner->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(content[i], 1);
        addPage(step);
    }
    // 主钮放最右（和高速下载窗那排一致）：取消扁平、上一步中性、下一步 / 完成实心主钮。
    setButtonLayout({QWizard::Stretch, QWizard::CancelButton, QWizard::BackButton,
                     QWizard::NextButton, QWizard::FinishButton});
    setButtonText(QWizard::BackButton, tr("上一步"));
    setButtonText(QWizard::NextButton, tr("下一步"));
    setButtonText(QWizard::FinishButton, tr("完成"));
    setButtonText(QWizard::CancelButton, tr("取消"));
    Theme::markButton(button(QWizard::NextButton), Theme::ButtonKind::Primary);
    Theme::markButton(button(QWizard::FinishButton), Theme::ButtonKind::Primary);
    Theme::markButton(button(QWizard::BackButton), Theme::ButtonKind::Neutral);
    Theme::markButton(button(QWizard::CancelButton), Theme::ButtonKind::Ghost);
    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        setButtonText(QWizard::NextButton, id == 2 ? tr("开始更新") : tr("下一步"));
    });
    for (auto *combo : findChildren<QComboBox *>()) combo->installEventFilter(this);
    for (auto *view : findChildren<QAbstractItemView *>()) view->viewport()->installEventFilter(this);
    // The native modal window owns its prompts; never hide questions behind it.
    // QWizard 把页面装在它自己那块内容里（铺满整窗）；顺着第一页往上找到那一块，resizeEvent 里挪。
    for (m_content = page(0); m_content && m_content->parentWidget() != this; m_content = m_content->parentWidget()) {}
    // 提示卡盖那块内容、不盖标题条（条上的 × 和拖动照常能用）。
    // 边距给 QWizard 内部那张网格（页面和按钮排一起往里收），翻页时 Qt 不重设它（TL2 实测）。
    if (m_content && m_content->layout()) {
        m_content->layout()->setContentsMargins(kPageMarginLeft, kPageMarginTop, kPageMarginRight, kPageMarginBottom);
        // Classic 风格按钮上方那条凹槽线（Qt 私有的 QWizardRuler，固定 2px 高、没有 Q_OBJECT 认不出类名；
        // Qt 自己会再 setVisible 它）压成 0 高，换成 paintEvent 里那条极淡的。
        for (QWidget *ruler : m_content->findChildren<QWidget *>(Qt::FindDirectChildrenOnly))
            if (ruler->minimumHeight() == 2 && ruler->maximumHeight() == 2) ruler->setFixedHeight(0);
    }
    QWidget *promptArea = m_content != nullptr ? m_content : this;
    m_promptHost = new OverlayHost(promptArea, promptArea);
    m_promptHost->setObjectName(QStringLiteral("instanceUpdatePromptHost"));
    m_strip = new ChromeStrip(this, true);
    m_chrome = new WindowChrome(this, m_strip, this);
}
bool InstanceUpdateWizard::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    if (m_chrome != nullptr && m_chrome->handleNativeEvent(message, result)) return true;
    return QWizard::nativeEvent(eventType, message, result);
}
void InstanceUpdateWizard::resizeEvent(QResizeEvent *event) {
    QWizard::resizeEvent(event);  // 它把内容那块重新铺满整窗 ⇒ 每次之后再往下挪一条
    if (m_strip == nullptr) return;  // setWizardStyle() 在构造中途就同步发一次 resize，那时条还没建
    const int top = ChromeDialog::stripHeight();
    m_strip->setGeometry(0, 0, width(), top);
    // 不 raise 条：两块不重叠。
    if (m_content) m_content->setGeometry(0, top, width(), height() - top);
}
void InstanceUpdateWizard::paintEvent(QPaintEvent *event) {
    ChromeDialog::paintBackdrop(this);
    // 底栏不铺底，只在按钮上方画一条极淡的分隔线（内容那块不铺底，线透得上来）。
    if (QAbstractButton *cancel = button(QWizard::CancelButton)) {
        const int y = cancel->mapTo(this, QPoint(0, 0)).y() - kFooterGap;
        QColor line = Theme::tokens().divider;
        line.setAlphaF(line.alphaF() * 0.6);
        QPainter painter(this);
        painter.fillRect(QRect(0, y, width(), 1), line);
    }
    QWizard::paintEvent(event);
}
void InstanceUpdateWizard::setStepReady(int step, bool ready) {
    if (auto *target = static_cast<Step *>(page(step))) target->refresh(ready);
}
void InstanceUpdateWizard::setRunning(bool running) {
    m_running = running;
    button(QWizard::BackButton)->setEnabled(!running && currentId() > 0);
    setButtonText(QWizard::CancelButton, running ? tr("取消 / 关闭") : tr("取消"));
}
bool InstanceUpdateWizard::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Wheel) {
        if (auto *combo = qobject_cast<QComboBox *>(watched); combo && !combo->view()->isVisible()) return true;
        // Let the item view process scrolling once, then consume the event even at an edge.
        if (auto *viewport = qobject_cast<QWidget *>(watched)) {
            if (qobject_cast<QAbstractItemView *>(viewport->parentWidget())) {
                viewport->removeEventFilter(this);
                QCoreApplication::sendEvent(viewport, event);
                viewport->installEventFilter(this);
                return true;
            }
        }
    }
    return QWizard::eventFilter(watched, event);
}
void InstanceUpdateWizard::reject() {
    // Closing a window with an embedded question must wake its nested event loop,
    // not hide the unanswered question together with the whole wizard.
    if (m_promptHost && m_promptHost->promptOpen()) { m_promptHost->dismiss(); return; }
    if (cancelRequested && !cancelRequested()) return;
    if (!m_running) QWizard::reject();
}
