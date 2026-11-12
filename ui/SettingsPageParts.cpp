#include "SettingsPageParts.hpp"

#include "Animations.hpp"
#include "MainWindowPageLayout.hpp"
#include "Theme.hpp"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEnterEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>

using namespace MainWindowPageLayout;

namespace SettingsParts {
namespace {

constexpr int kRadiusGroup = 14;  // --r-pop：分组首尾行的外圆角
constexpr int kRadiusInner = 4;   // 行与行之间的内侧小圆角

QFont uiFont(int pixel, QFont::Weight weight = QFont::Normal) {
    QFont font = QApplication::font();
    font.setPixelSize(pixel);
    font.setWeight(weight);
    return font;
}

QColor accentText() {
    const Theme::Tokens t = Theme::tokens();
    return Theme::textTone(t.accentInk, t.deep);
}

/// 四角半径各自给定的圆角矩形。
QPainterPath cornerPath(const QRectF &r, qreal tl, qreal tr, qreal br, qreal bl) {
    QPainterPath p;
    p.moveTo(r.left() + tl, r.top());
    p.lineTo(r.right() - tr, r.top());
    p.arcTo(QRectF(r.right() - 2 * tr, r.top(), 2 * tr, 2 * tr), 90, -90);
    p.lineTo(r.right(), r.bottom() - br);
    p.arcTo(QRectF(r.right() - 2 * br, r.bottom() - 2 * br, 2 * br, 2 * br), 0, -90);
    p.lineTo(r.left() + bl, r.bottom());
    p.arcTo(QRectF(r.left(), r.bottom() - 2 * bl, 2 * bl, 2 * bl), 270, -90);
    p.lineTo(r.left(), r.top() + tl);
    p.arcTo(QRectF(r.left(), r.top(), 2 * tl, 2 * tl), 180, -90);
    p.closeSubpath();
    return p;
}

/// 标签旁的 ⓘ：只给它挂 tooltip，不可点。
class InfoDot : public QWidget {
public:
    explicit InfoDot(const QString &tip, QWidget *parent) : QWidget(parent) {
        setFixedSize(16, 16);
        setToolTip(tip);
        setAccessibleName(QStringLiteral("说明"));
        setAccessibleDescription(tip);
        setCursor(Qt::WhatsThisCursor);
        setProperty("settingsInfoDot", true);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QColor ink = Theme::tokens().secondary;
        ink.setAlphaF(0.8);
        p.setPen(QPen(ink, 1.2));
        p.drawEllipse(QRectF(1.5, 1.5, 13, 13));
        p.setFont(uiFont(11, QFont::DemiBold));
        p.drawText(QRectF(0, 0, 16, 16), Qt::AlignCenter, QStringLiteral("i"));
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// SettingsRow
// ---------------------------------------------------------------------------

SettingsRow::SettingsRow(const RowSpec &spec, QWidget *control, QWidget *parent)
    : QWidget(parent), m_spec(spec), m_control(control) {
    setProperty("settingsItemRow", true);
    setAttribute(Qt::WA_Hover, true);
    setMinimumHeight(52);

    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(14, 9, 14, 9);
    m_grid->setHorizontalSpacing(24);
    m_grid->setVerticalSpacing(10);
    m_grid->setColumnStretch(0, 1);

    m_left = new QWidget(this);
    m_left->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *leftLayout = new QVBoxLayout(m_left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(2);
    auto *top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(6);
    m_label = new QLabel(spec.label, m_left);
    m_label->setProperty("settingsItemLabel", true);
    m_label->setWordWrap(true);
    m_label->setFont(uiFont(14));
    top->addWidget(m_label, 0, Qt::AlignVCenter);
    if (!spec.tip.isEmpty()) top->addWidget(new InfoDot(spec.tip, m_left), 0, Qt::AlignVCenter);
    top->addStretch(1);
    leftLayout->addLayout(top);
    if (!spec.help.isEmpty()) {
        auto *help = new QLabel(spec.help, m_left);
        help->setProperty("settingsHelp", true);
        help->setWordWrap(true);
        help->setFont(uiFont(12));
        markTextRole(help, Theme::secondaryTextRoleValue());
        leftLayout->addWidget(help);
    }

    m_cell = new QWidget(this);
    auto *cellLayout = new QHBoxLayout(m_cell);
    cellLayout->setContentsMargins(0, 0, 0, 0);
    cellLayout->setSpacing(0);
    control->setParent(m_cell);
    cellLayout->addWidget(control);
    if (auto *combo = qobject_cast<QComboBox *>(control)) {
        // 列宽固定 220：下拉框不能被自身文字撑出这一列。
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(6);
        combo->setMinimumWidth(0);
    }
    if (spec.placement == Placement::Column) {
        control->setSizePolicy(QSizePolicy::Expanding, control->sizePolicy().verticalPolicy());
        control->setMinimumWidth(0);
    }

    m_hoverAnim = new QVariantAnimation(this);
    m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_hover = v.toReal();
        update();
    });
    place();
}

void SettingsRow::place() {
    m_grid->removeWidget(m_left);
    m_grid->removeWidget(m_cell);
    const bool below = m_stacked || m_spec.placement == Placement::Wide;
    auto *cellLayout = qobject_cast<QHBoxLayout *>(m_cell->layout());
    if (below) {
        m_grid->addWidget(m_left, 0, 0, 1, 2);
        const bool wide = m_spec.placement == Placement::Wide;
        m_grid->addWidget(m_cell, 1, 0, 1, 2, wide ? Qt::Alignment() : Qt::AlignLeft);
        if (m_spec.placement == Placement::Column) {
            m_cell->setMinimumWidth(kControlColumn);
            m_cell->setMaximumWidth(kStackedControlMax);
        } else {
            m_cell->setMinimumWidth(0);
            m_cell->setMaximumWidth(QWIDGETSIZE_MAX);
        }
        if (!wide && cellLayout) cellLayout->setAlignment(m_control, Qt::AlignLeft);
    } else {
        m_grid->addWidget(m_left, 0, 0, Qt::AlignVCenter);
        m_grid->addWidget(m_cell, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
        if (m_spec.placement == Placement::Column) {
            m_cell->setFixedWidth(kControlColumn);
        } else {
            m_cell->setMinimumWidth(0);
            m_cell->setMaximumWidth(QWIDGETSIZE_MAX);
        }
        if (cellLayout) cellLayout->setAlignment(m_control, Qt::AlignRight);
    }
    m_grid->invalidate();
}

void SettingsRow::setStacked(bool stacked) {
    if (stacked == m_stacked) return;
    m_stacked = stacked;
    setProperty("settingsRowStacked", stacked);
    place();
}

void SettingsRow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    setStacked(width() < kStackBelow);
}

void SettingsRow::enterEvent(QEnterEvent *event) {
    QWidget::enterEvent(event);
    m_hoverAnim->stop();
    if (Anim::enabled()) {
        m_hoverAnim->setDuration(100);
        m_hoverAnim->setStartValue(m_hover);
        m_hoverAnim->setEndValue(1.0);
        m_hoverAnim->start();
    } else {
        m_hover = 1;
        update();
    }
}

void SettingsRow::leaveEvent(QEvent *event) {
    QWidget::leaveEvent(event);
    m_hoverAnim->stop();
    if (Anim::enabled()) {
        m_hoverAnim->setDuration(160);
        m_hoverAnim->setStartValue(m_hover);
        m_hoverAnim->setEndValue(0.0);
        m_hoverAnim->start();
    } else {
        m_hover = 0;
        update();
    }
}

void SettingsRow::paintEvent(QPaintEvent *) {
    // --group：暗色 白 4.5%，浅色 白 42%；悬停 7.5% / 62%。
    const bool dark = Theme::isDark();
    const qreal base = dark ? 0.045 : 0.42;
    const qreal hover = dark ? 0.075 : 0.62;
    QColor fill(255, 255, 255);
    fill.setAlphaF(base + (hover - base) * m_hover);
    int index = 0;
    int count = 1;
    SettingsGroup::position(this, &index, &count);
    const qreal top = index == 0 ? kRadiusGroup : kRadiusInner;
    const qreal bottom = index == count - 1 ? kRadiusGroup : kRadiusInner;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawPath(cornerPath(QRectF(rect()), top, top, bottom, bottom));
}

// ---------------------------------------------------------------------------
// SettingsGroup
// ---------------------------------------------------------------------------

SettingsGroup::SettingsGroup(const QString &title, QWidget *parent) : QWidget(parent), m_title(title) {
    setProperty("settingsSectionTitle", title);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    m_header = new QHBoxLayout;
    m_header->setContentsMargins(0, 0, 0, 0);
    auto *titleLabel = new QLabel(title, this);
    markTextRole(titleLabel, Theme::sectionTitleTextRoleValue());
    titleLabel->setFont(uiFont(16, QFont::DemiBold));
    m_header->addWidget(titleLabel);
    m_header->addStretch(1);
    layout->addLayout(m_header);
    m_rows = new QWidget(this);
    m_rowsLayout = new QVBoxLayout(m_rows);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(2);
    layout->addWidget(m_rows);
}

SettingsRow *SettingsGroup::addRow(const RowSpec &spec, QWidget *control) {
    auto *row = new SettingsRow(spec, control, m_rows);
    m_rowsLayout->addWidget(row);
    return row;
}

void SettingsGroup::setHeaderAction(QWidget *action) {
    action->setParent(this);
    m_header->addWidget(action, 0, Qt::AlignVCenter);
}

void SettingsGroup::position(const QWidget *row, int *index, int *count) {
    *index = 0;
    *count = 1;
    const QWidget *host = row->parentWidget();
    if (host == nullptr || host->layout() == nullptr) return;
    QLayout *layout = host->layout();
    int visible = 0;
    int mine = 0;
    for (int i = 0; i < layout->count(); ++i) {
        const QWidget *w = layout->itemAt(i)->widget();
        if (w == nullptr || w->isHidden()) continue;
        if (w == row) mine = visible;
        ++visible;
    }
    *index = mine;
    *count = qMax(1, visible);
}

// ---------------------------------------------------------------------------
// SlidingTabs
// ---------------------------------------------------------------------------

SlidingTabs::SlidingTabs(QWidget *parent) : QWidget(parent) {
    setFocusPolicy(Qt::TabFocus);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(200);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_indicator = v.toRectF();
        update();
    });
}

int SlidingTabs::addTab(const QString &text) {
    m_texts.append(text);
    updateGeometry();
    m_indicator = tabRect(m_current);
    update();
    return m_texts.size() - 1;
}

QRect SlidingTabs::tabRect(int index) const {
    const QFontMetrics fm(uiFont(14, QFont::Medium));
    int x = 3;
    for (int i = 0; i < m_texts.size(); ++i) {
        const int w = fm.horizontalAdvance(m_texts.at(i)) + 28;
        if (i == index) return QRect(x, 3, w, 30);
        x += w + 2;
    }
    return QRect();
}

QSize SlidingTabs::sizeHint() const {
    if (m_texts.isEmpty()) return QSize(0, 36);
    const QRect last = tabRect(m_texts.size() - 1);
    return QSize(last.right() + 1 + 3, 36);
}

void SlidingTabs::setCurrentIndex(int index) {
    if (index < 0 || index >= m_texts.size() || index == m_current) return;
    m_current = index;
    const QRectF target = tabRect(index);
    m_anim->stop();
    if (Anim::enabled() && isVisible()) {
        m_anim->setStartValue(m_indicator);
        m_anim->setEndValue(target);
        m_anim->start();
    } else {
        m_indicator = target;
    }
    update();
    emit currentChanged(index);
}

int SlidingTabs::indexAt(const QPoint &pos) const {
    for (int i = 0; i < m_texts.size(); ++i)
        if (tabRect(i).adjusted(-1, -3, 1, 3).contains(pos)) return i;
    return -1;
}

void SlidingTabs::mousePressEvent(QMouseEvent *event) {
    const int i = indexAt(event->position().toPoint());
    if (i >= 0) setCurrentIndex(i);
}

void SlidingTabs::mouseMoveEvent(QMouseEvent *event) {
    const int i = indexAt(event->position().toPoint());
    if (i != m_hover) {
        m_hover = i;
        update();
    }
}

void SlidingTabs::leaveEvent(QEvent *) {
    m_hover = -1;
    update();
}

void SlidingTabs::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Left) setCurrentIndex(qMax(0, m_current - 1));
    else if (event->key() == Qt::Key_Right) setCurrentIndex(qMin(count() - 1, m_current + 1));
    else QWidget::keyPressEvent(event);
}

void SlidingTabs::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (m_anim->state() != QAbstractAnimation::Running) m_indicator = tabRect(m_current);
}

void SlidingTabs::paintEvent(QPaintEvent *) {
    const Theme::Tokens t = Theme::tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(t.hover);
    p.drawRoundedRect(QRectF(rect()), 12, 12);
    p.setBrush(t.selected);
    p.drawRoundedRect(m_indicator, 9, 9);
    p.setFont(uiFont(14, QFont::Medium));
    for (int i = 0; i < m_texts.size(); ++i) {
        p.setPen(i == m_current ? accentText() : (i == m_hover ? t.text : t.secondary));
        p.drawText(tabRect(i), Qt::AlignCenter, m_texts.at(i));
    }
    if (hasFocus()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.focus, 3));
        p.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 11, 11);
    }
}

// ---------------------------------------------------------------------------
// ExpandSection
// ---------------------------------------------------------------------------

ExpandSection::ExpandSection(const QString &title, QWidget *parent) : QWidget(parent) {
    setProperty("settingsExpandSection", true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *header = Theme::makeFoldRow(this, title);
    header->setFixedHeight(44);
    m_header = header;
    layout->addWidget(header);
    m_body = new QWidget(this);
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(0, 22, 0, 0);
    m_bodyLayout->setSpacing(22);
    m_body->hide();
    layout->addWidget(m_body);
    m_anim = new QPropertyAnimation(m_body, "maximumHeight", this);
    m_anim->setDuration(200);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(header, &QAbstractButton::toggled, this, [this](bool open) { setExpanded(open); });
}

void ExpandSection::setExpanded(bool expanded, bool animate) {
    if (expanded == m_expanded) return;
    m_expanded = expanded;
    static_cast<QAbstractButton *>(m_header)->setChecked(expanded);
    m_anim->stop();
    m_anim->disconnect(this);
    const auto settle = [this, expanded] {
        m_body->setMaximumHeight(QWIDGETSIZE_MAX);
        m_body->setVisible(expanded);
    };
    if (!animate || !Anim::enabled() || !isVisible()) {
        settle();
    } else {
        QLayout *inner = m_body->layout();
        inner->activate();
        const int width = qMax(1, this->width());
        const int full = inner->hasHeightForWidth() ? inner->totalHeightForWidth(width)
                                                    : inner->totalSizeHint().height();
        const int from = expanded ? 0 : m_body->height();
        const int to = expanded ? full : 0;
        m_body->setMaximumHeight(from);
        m_body->show();
        m_anim->setStartValue(from);
        m_anim->setEndValue(to);
        connect(m_anim, &QPropertyAnimation::finished, this, settle);
        m_anim->start();
    }
    emit toggled(expanded);
}

// ---------------------------------------------------------------------------
// SavedToast
// ---------------------------------------------------------------------------

SavedToast::SavedToast(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setObjectName(QStringLiteral("settingsSavedToast"));
    setFixedSize(sizeHint());
    m_hold = new QTimer(this);
    m_hold->setSingleShot(true);
    m_hold->setInterval(1200);
    connect(m_hold, &QTimer::timeout, this, [this] { animateTo(0, 300); });
    m_anim = new QVariantAnimation(this);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_opacity = v.toReal();
        update();
    });
    connect(m_anim, &QVariantAnimation::finished, this, [this] {
        if (m_opacity <= 0.001) hide();
    });
    hide();
}

QSize SavedToast::sizeHint() const {
    const QFontMetrics fm(uiFont(12));
    return QSize(fm.horizontalAdvance(QStringLiteral("已保存")) + 42, 26);
}

void SavedToast::anchor() {
    if (parentWidget() == nullptr) return;
    move(parentWidget()->width() - width() - 18, 10);
    raise();
}

void SavedToast::animateTo(qreal target, int ms) {
    m_anim->stop();
    if (!Anim::enabled()) {
        m_opacity = target;
        setVisible(target > 0);
        update();
        return;
    }
    m_anim->setDuration(ms);
    m_anim->setEasingCurve(target > m_opacity ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    m_anim->setStartValue(m_opacity);
    m_anim->setEndValue(target);
    m_anim->start();
}

void SavedToast::ping() {
    anchor();
    show();
    raise();
    animateTo(1, 180);
    m_hold->start();
}

void SavedToast::paintEvent(QPaintEvent *) {
    const Theme::Tokens t = Theme::tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setOpacity(m_opacity);
    p.setPen(Qt::NoPen);
    p.setBrush(t.okSoft);
    p.drawRoundedRect(QRectF(rect()), height() / 2.0, height() / 2.0);
    const QColor ink = Theme::textTone(t.okInk, t.ok);
    QPen pen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.drawPolyline(QPolygonF({QPointF(11, 13.5), QPointF(14.2, 16.5), QPointF(19, 10)}));
    p.setFont(uiFont(12));
    p.drawText(QRectF(24, 0, width() - 24, height()), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("已保存"));
}

// ---------------------------------------------------------------------------
// CommitOnFocusOut / watchSaved
// ---------------------------------------------------------------------------

CommitOnFocusOut::CommitOnFocusOut(QWidget *edit) : QObject(edit) {
    edit->installEventFilter(this);
}

bool CommitOnFocusOut::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::FocusOut) {
        emit committed();
    } else if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            (key->modifiers() & Qt::ControlModifier)) {
            emit committed();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

void watchSaved(QWidget *root, const std::function<void()> &ping) {
    for (auto *combo : root->findChildren<QComboBox *>())
        QObject::connect(combo, &QComboBox::activated, combo, [ping](int) { ping(); });
    for (auto *button : root->findChildren<QAbstractButton *>())
        if (button->isCheckable())
            QObject::connect(button, &QAbstractButton::clicked, button, [ping](bool) { ping(); });
    for (auto *slider : root->findChildren<QSlider *>()) {
        QObject::connect(slider, &QSlider::sliderReleased, slider, [ping] { ping(); });
        QObject::connect(slider, &QSlider::actionTriggered, slider, [ping](int action) {
            if (action != QAbstractSlider::SliderMove) ping();
        });
    }
    // 单行输入：改过才算，失焦或回车时 ping。
    for (auto *edit : root->findChildren<QLineEdit *>()) {
        if (edit->parentWidget() != nullptr && qobject_cast<QAbstractSpinBox *>(edit->parentWidget()) != nullptr)
            continue;
        QObject::connect(edit, &QLineEdit::textEdited, edit, [edit](const QString &) {
            edit->setProperty("settingsDirty", true);
        });
        QObject::connect(edit, &QLineEdit::editingFinished, edit, [edit, ping] {
            if (!edit->property("settingsDirty").toBool()) return;
            edit->setProperty("settingsDirty", false);
            ping();
        });
    }
    for (auto *spin : root->findChildren<QAbstractSpinBox *>()) {
        if (auto *inner = spin->findChild<QLineEdit *>())
            QObject::connect(inner, &QLineEdit::textEdited, spin,
                             [spin](const QString &) { spin->setProperty("settingsDirty", true); });
        if (auto *plain = qobject_cast<QSpinBox *>(spin))
            QObject::connect(plain, &QSpinBox::valueChanged, spin, [spin](int) {
                if (spin->hasFocus()) spin->setProperty("settingsDirty", true);
            });
        QObject::connect(spin, &QAbstractSpinBox::editingFinished, spin, [spin, ping] {
            if (!spin->property("settingsDirty").toBool()) return;
            spin->setProperty("settingsDirty", false);
            ping();
        });
    }
    // 多行文本：用户改过（文档已修改）才算，失焦 / Ctrl+Enter 提交时 ping。
    for (auto *plain : root->findChildren<QPlainTextEdit *>()) {
        if (auto *commit = plain->findChild<CommitOnFocusOut *>())
            QObject::connect(commit, &CommitOnFocusOut::committed, plain, [plain, ping] {
                if (!plain->document()->isModified()) return;
                plain->document()->setModified(false);
                ping();
            });
    }
}

}  // namespace SettingsParts
