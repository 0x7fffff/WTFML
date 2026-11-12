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
#include <QResizeEvent>
#include <QVBoxLayout>

namespace {
class Step final : public QWizardPage {
public:
    bool ready = false;
    bool isComplete() const override { return ready; }
    void refresh(bool value) { if (ready != value) { ready = value; emit completeChanged(); } }
};
}
InstanceUpdateWizard::InstanceUpdateWizard(QWidget *target, QWidget *mods, QWidget *confirmation,
    QWidget *progress, QWidget *parent) : QWizard(parent) {
    setObjectName(QStringLiteral("instanceUpdateWizard"));
    setWindowTitle(tr("实例更新向导"));
    setWindowModality(Qt::WindowModal);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    setWizardStyle(QWizard::ModernStyle);
    setMinimumSize(820, 560); resize(1060, 720);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setOption(QWizard::NoBackButtonOnLastPage, true);
    const QStringList titles{tr("选择目标版本"), tr("检查并处理模组"), tr("确认更新方案"), tr("执行与结果")};
    const QList<QWidget *> content{target, mods, confirmation, progress};
    for (int i = 0; i < content.size(); ++i) {
        auto *step = new Step; step->setTitle(tr("%1 / 4 / %2").arg(i + 1).arg(titles[i]));
        auto *layout = new QVBoxLayout(step); layout->setContentsMargins(0, 8, 0, 0);
        layout->addWidget(content[i], 1); addPage(step);
    }
    setButtonText(QWizard::BackButton, tr("上一步"));
    setButtonText(QWizard::NextButton, tr("下一步"));
    setButtonText(QWizard::FinishButton, tr("完成"));
    setButtonText(QWizard::CancelButton, tr("取消"));
    Theme::markButton(button(QWizard::NextButton), Theme::ButtonKind::Primary);
    Theme::markButton(button(QWizard::FinishButton), Theme::ButtonKind::Primary);
    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        setButtonText(QWizard::NextButton, id == 2 ? tr("开始更新") : tr("下一步"));
    });
    for (auto *combo : findChildren<QComboBox *>()) combo->installEventFilter(this);
    for (auto *view : findChildren<QAbstractItemView *>()) view->viewport()->installEventFilter(this);
    // The native modal window owns its prompts; never hide questions behind it.
    // QWizard 把页面装在它自己那块内容里（铺满整窗）；顺着第一页往上找到那一块，resizeEvent 里挪。
    for (m_content = page(0); m_content && m_content->parentWidget() != this; m_content = m_content->parentWidget()) {}
    // 提示卡盖那块内容、不盖标题条（条上的 × 和拖动照常能用）。
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
    if (m_content) m_content->setGeometry(0, top, width(), height() - top);
    m_strip->raise();
}
void InstanceUpdateWizard::paintEvent(QPaintEvent *event) {
    ChromeDialog::paintBackdrop(this);
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
