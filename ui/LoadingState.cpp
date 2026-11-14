#include "LoadingState.hpp"

#include "Animations.hpp"
#include "Theme.hpp"
#include "VersionIcons.hpp"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QCoreApplication>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QPushButton>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QTreeWidget>

#include <cmath>

namespace {

constexpr const char *kOverlayName = "wtfmlLoadingOverlay";
constexpr const char *kRetryName = "wtfmlLoadingRetry";
constexpr const char *kRetryFilterMark = "wtfmlLoadingRetryFilter";
constexpr qreal kStaticDot = 0.55;

QString defaultText() { return QObject::tr("正在获取…"); }

QElapsedTimer &loadingClock() {
    static QElapsedTimer timer;
    if (!timer.isValid()) timer.start();
    return timer;
}

qreal outCubic(qreal t) {
    t = qBound<qreal>(0.0, t, 1.0);
    return 1.0 - std::pow(1.0 - t, 3);
}

/// 第 i 颗点此刻的不透明度（.25 ~ 1）。
qreal dotOpacity(qint64 now, int i) {
    if (!Anim::enabled()) return kStaticDot;
    const int period = Anim::loadingDotPeriodMs();
    const qint64 t = ((now - i * Anim::loadingDotStaggerMs()) % period + period) % period;
    const qreal p = qreal(t) / period;
    static const QEasingCurve curve(QEasingCurve::InOutSine);
    if (p < 0.3) return 0.25 + 0.75 * curve.valueForProgress(p / 0.3);
    if (p < 0.6) return 1.0 - 0.75 * curve.valueForProgress((p - 0.3) / 0.3);
    return 0.25;
}

void paintDots(QPainter *painter, QPointF leftCenter, qreal size, qreal gap, qint64 now) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    const qreal base = painter->opacity();
    for (int i = 0; i < 3; ++i) {
        painter->setOpacity(base * dotOpacity(now, i));
        painter->setBrush(Theme::tokens().primary);
        painter->drawEllipse(QRectF(leftCenter.x() + i * (size + gap), leftCenter.y() - size / 2, size, size));
    }
    painter->restore();
}

/// 断开的云（24 格线稿，和样张那支同一条路径）。
void paintCloudOff(QPainter *painter, const QRectF &box, qreal strokeIn24, const QColor &color) {
    QPainterPath path;
    path.moveTo(4, 4);
    path.lineTo(20, 20);
    // 上面那一大坨：圆心 (11.54, 11.18) r 5.5，从左上 (8.5, 6.6) 越过顶到右边 (17, 10.5)。
    path.moveTo(8.5, 6.6);
    path.arcTo(QRectF(11.54 - 5.5, 11.18 - 5.5, 11, 11), 123.6, -116.5);
    // 右边那一小坨：圆心 (16.40, 14.455) r 4，到右下 (19.4, 17.1)。
    path.arcTo(QRectF(16.40 - 4, 14.455 - 4, 8, 8), 81.4, -122.8);
    // 底边 + 左边那一坨：从 (15.5, 19) 到 (7, 19)，再绕到左上 (5.4, 10.3)。
    path.moveTo(15.5, 19);
    path.lineTo(7, 19);
    path.arcTo(QRectF(7.015 - 4.5, 14.5 - 4.5, 9, 9), -90, -159);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->translate(box.topLeft());
    painter->scale(box.width() / 24.0, box.height() / 24.0);
    QPen pen(color, strokeIn24, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
    painter->restore();
}

// ---------------------------------------------------------------------------
// 整块那一层
// ---------------------------------------------------------------------------

class LoadingOverlay;
void registerOverlay(LoadingOverlay *overlay);

class LoadingOverlay : public QWidget {
public:
    explicit LoadingOverlay(QWidget *host) : QWidget(host) {
        setObjectName(QString::fromLatin1(kOverlayName));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setGeometry(host->rect());
        host->installEventFilter(this);
        m_retry = new QPushButton(tr("重试"), this);
        m_retry->setObjectName(QString::fromLatin1(kRetryName));
        Theme::markButton(m_retry, Theme::ButtonKind::Neutral);
        m_retry->setIcon(QIcon(VersionIcons::refresh(16, Theme::neutralInkColor())));
        m_retry->hide();
        connect(m_retry, &QPushButton::clicked, this, [this] {
            const auto retry = m_onRetry;  // retry 里会 begin()，可能改掉 m_onRetry
            if (retry) retry();
        });
        hide();
    }

    void startLoading(const QString &text) {
        const qint64 now = clockMs();
        m_text = text.isEmpty() ? defaultText() : text;
        m_error = false;
        m_onRetry = nullptr;
        m_leaving = false;
        m_retry->hide();
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setProperty("state", QStringLiteral("loading"));
        setAccessibleName(m_text);
        // 已经露着（出错态点了重试）：直接换成点，不先藏起来再等 150ms。
        m_start = isVisible() ? now - Anim::loadingDelayMs() - Anim::appearMs() : now;
        m_opacity = isVisible() ? 1.0 : 0.0;
        registerOverlay(this);
        update();
    }

    void startError(const QString &reason, std::function<void()> retry) {
        const bool wasVisible = isVisible();
        m_text = reason;
        m_error = true;
        m_leaving = false;
        m_onRetry = std::move(retry);
        m_opacity = 1.0;
        setProperty("state", QStringLiteral("error"));
        setAccessibleName(reason);
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        m_retry->show();
        layoutError();
        showOnTop();
        if (!wasVisible) Anim::overlayAppear(this);
        update();
    }

    /// 返回 true = 已经可以撤了。
    bool leave() {
        m_leaving = true;
        if (!isVisible() || !Anim::enabled() || m_error) {
            m_dead = true;  // 调用方马上 deleteLater；这之前别再被时钟叫出来
            return true;
        }
        m_leaveFrom = m_opacity;
        m_leaveStart = clockMs();
        registerOverlay(this);
        return false;
    }

    /// 共享时钟每一跳。返回 false = 不用再叫我了。
    bool tick(qint64 now) {
        if (m_error || m_dead) return false;
        if (m_leaving) {
            const qreal p = qreal(now - m_leaveStart) / Anim::exitMs();
            m_opacity = m_leaveFrom * (1.0 - outCubic(p));
            update();
            if (p >= 1.0) {
                deleteLater();
                return false;
            }
            return true;
        }
        const qint64 shown = now - m_start - Anim::loadingDelayMs();
        if (shown < 0) return true;
        if (!isVisible()) showOnTop();
        m_opacity = Anim::enabled() ? outCubic(qreal(shown) / Anim::appearMs()) : 1.0;
        update();
        // 动效关着：露面那一下画完就不用再跳了（点是静的）。
        return Anim::enabled();
    }

    bool leaving() const { return m_leaving; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == parent() && event->type() == QEvent::Resize) {
            setGeometry(parentWidget()->rect());
            if (m_error) layoutError();
        }
        return false;
    }

    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        const Theme::Tokens t = Theme::tokens();
        QFont font = Theme::textFont(this->font(), Theme::TextRole::Body);
        painter.setFont(font);
        if (m_error) {
            paintCloudOff(&painter, QRectF(m_iconRect), 1.5, t.disabled);
            painter.setPen(t.secondary);
            painter.drawText(m_reasonRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_text);
            return;
        }
        painter.setOpacity(m_opacity);
        constexpr int dot = 8, gap = 8, below = 12, lineH = 22;
        const int blockH = dot + below + lineH;
        const int top = (height() - blockH) / 2;
        const int dotsW = 3 * dot + 2 * gap;
        paintDots(&painter, QPointF((width() - dotsW) / 2.0, top + dot / 2.0), dot, gap, clockMs());
        painter.setPen(t.secondary);
        const QFontMetrics metrics(font);
        const QRect textRect(16, top + dot + below, qMax(0, width() - 32), lineH);
        painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter,
                         metrics.elidedText(m_text, Qt::ElideRight, textRect.width()));
    }

private:
    void showOnTop() {
        setGeometry(parentWidget()->rect());
        show();
        raise();
    }

    void layoutError() {
        constexpr int icon = 28, gap = 10;
        const QFont font = Theme::textFont(this->font(), Theme::TextRole::Body);
        const QFontMetrics metrics(font);
        const int textW = qMax(0, qMin(width() - 32, 420));
        const QRect textBounds =
            metrics.boundingRect(QRect(0, 0, textW, 10000), Qt::AlignHCenter | Qt::TextWordWrap, m_text);
        const QSize button = m_retry->sizeHint();
        const int blockH = icon + gap + textBounds.height() + gap + button.height();
        const int top = qMax(0, (height() - blockH) / 2);
        m_iconRect = QRect((width() - icon) / 2, top, icon, icon);
        m_reasonRect = QRect((width() - textW) / 2, top + icon + gap, textW, textBounds.height());
        m_retry->setGeometry((width() - button.width()) / 2, m_reasonRect.bottom() + 1 + gap, button.width(),
                             button.height());
    }

    QPushButton *m_retry = nullptr;
    QString m_text;
    bool m_error = false;
    bool m_leaving = false;
    bool m_dead = false;
    qint64 m_start = 0;
    qint64 m_leaveStart = 0;
    qreal m_opacity = 0.0;
    qreal m_leaveFrom = 0.0;
    QRect m_iconRect;
    QRect m_reasonRect;
    std::function<void()> m_onRetry;

    static qint64 clockMs() { return LoadingState::clockMs(); }
};

// ---------------------------------------------------------------------------
// 共享时钟 + 行内登记
// ---------------------------------------------------------------------------

struct RowEntry {
    QPointer<QAbstractItemView> view;
    QPersistentModelIndex index;
    std::function<void()> retry;  // 出错行才有
    QRect retryRect;
    bool ticking = false;  // 加载中、还要按时钟重绘
    bool shownOnce = false;  // 动效关时：露面那一下画过了
};

QList<QPointer<LoadingOverlay>> &overlays() {
    static QList<QPointer<LoadingOverlay>> list;
    return list;
}

QList<RowEntry> &rows() {
    static QList<RowEntry> list;
    return list;
}

void tick();

QTimer &timer() {
    static QTimer *t = [] {
        auto *created = new QTimer(qApp);
        created->setInterval(Anim::stepMs());
        QObject::connect(created, &QTimer::timeout, created, &tick);
        return created;
    }();
    return *t;
}

void ensureClock() {
    if (!timer().isActive()) timer().start();
}

void registerOverlay(LoadingOverlay *overlay) {
    if (!overlays().contains(overlay)) overlays().append(overlay);
    ensureClock();
}

void tick() {
    const qint64 now = LoadingState::clockMs();
    auto &list = overlays();
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list[i].isNull() || !list[i]->tick(now)) list.removeAt(i);
    }
    auto &entries = rows();
    for (int i = entries.size() - 1; i >= 0; --i) {
        RowEntry &e = entries[i];
        if (e.view.isNull() || !e.index.isValid()) {
            entries.removeAt(i);
            continue;
        }
        if (!e.ticking) continue;
        const qint64 started = e.index.data(LoadingState::rowLoadingRole()).toLongLong();
        const qint64 shown = now - started - Anim::loadingDelayMs();
        if (shown < 0) continue;
        e.view->viewport()->update(e.view->visualRect(e.index));
        if (!Anim::enabled()) e.ticking = false;  // 露面那一下画了，点是静的
    }
    bool anyTicking = false;
    for (const RowEntry &e : entries) anyTicking = anyTicking || e.ticking;
    if (list.isEmpty() && !anyTicking) timer().stop();
}

RowEntry *entryFor(const QModelIndex &index) {
    for (RowEntry &e : rows())
        if (e.index.isValid() && e.index == index) return &e;
    return nullptr;
}

QModelIndex indexOf(QTreeWidgetItem *item) {
    QTreeWidget *tree = item->treeWidget();
    if (tree == nullptr) return {};
    // indexFromItem 在 Qt 6 里是 public。
    return tree->indexFromItem(item, 0);
}

/// 出错行「重试」那一格的点击：挂在 viewport 上，按 paintRow 记下的那一格判。
class RetryFilter : public QObject {
public:
    explicit RetryFilter(QAbstractItemView *view) : QObject(view), m_view(view) {}

protected:
    bool eventFilter(QObject *, QEvent *event) override {
        const auto type = event->type();
        if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease &&
            type != QEvent::MouseButtonDblClick)
            return false;
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) return false;
        for (const RowEntry &e : rows()) {
            if (e.view != m_view || !e.retry || !e.index.isValid() ||
                !e.retryRect.contains(mouse->position().toPoint()))
                continue;
            if (type == QEvent::MouseButtonRelease) {
                // 先拷一份再调：retry 会删掉这一行、往登记表里加新行（e 随之失效）。调完立刻返回。
                const auto retry = e.retry;
                retry();
            }
            return true;
        }
        return false;
    }

private:
    QAbstractItemView *m_view;
};

}  // namespace

namespace LoadingState {

qint64 clockMs() { return loadingClock().elapsed(); }

QWidget *overlay(QWidget *host) {
    if (host == nullptr) return nullptr;
    // 正在淡出的那一层不算（它马上要 deleteLater），所以可能同时有两层，挑没在走的那一层。
    for (QWidget *child :
         host->findChildren<QWidget *>(QString::fromLatin1(kOverlayName), Qt::FindDirectChildrenOnly)) {
        auto *layer = dynamic_cast<LoadingOverlay *>(child);
        if (layer != nullptr && !layer->leaving()) return layer;
    }
    return nullptr;
}

QString retryButtonName() { return QString::fromLatin1(kRetryName); }

void begin(QWidget *host, const QString &text) {
    if (host == nullptr) return;
    auto *existing = static_cast<LoadingOverlay *>(overlay(host));
    if (existing == nullptr) {
        // 正在淡出的那一层不复用（它马上要 deleteLater），另起一层。
        existing = new LoadingOverlay(host);
    }
    existing->startLoading(text);
}

void finish(QWidget *host) {
    auto *layer = static_cast<LoadingOverlay *>(overlay(host));
    if (layer == nullptr) return;
    const bool wasVisible = layer->isVisible();
    if (layer->leave()) {
        layer->hide();
        layer->deleteLater();
    }
    if (wasVisible) {
        // 内容淡入：列表/树淡它的 viewport（加载态是 host 的子控件，不在 viewport 里，不跟着淡）。
        auto *area = qobject_cast<QAbstractScrollArea *>(host);
        Anim::overlayAppear(area != nullptr ? area->viewport() : host);
    }
}

void fail(QWidget *host, const QString &reason, std::function<void()> retry) {
    if (host == nullptr) return;
    auto *layer = static_cast<LoadingOverlay *>(overlay(host));
    if (layer == nullptr) layer = new LoadingOverlay(host);
    layer->startError(reason, std::move(retry));
}

QTreeWidgetItem *beginRow(QTreeWidgetItem *parent, const QString &text) {
    if (parent == nullptr) return nullptr;
    auto *row = new QTreeWidgetItem(parent);
    row->setText(0, text.isEmpty() ? defaultText() : text);
    row->setDisabled(true);  // 灰的、点不动：它不是能选的东西
    row->setData(0, rowLoadingRole(), clockMs());
    const QModelIndex index = indexOf(row);
    if (index.isValid()) {
        rows().append({row->treeWidget(), QPersistentModelIndex(index), nullptr, QRect(), true, false});
        ensureClock();
    }
    return row;
}

void failRow(QTreeWidgetItem *row, const QString &reason, std::function<void()> retry) {
    if (row == nullptr) return;
    row->setText(0, reason);
    row->setDisabled(true);
    row->setData(0, rowLoadingRole(), qint64(-1));
    QTreeWidget *tree = row->treeWidget();
    const QModelIndex index = indexOf(row);
    if (tree == nullptr || !index.isValid()) return;
    RowEntry *entry = entryFor(index);
    if (entry == nullptr) {
        rows().append({tree, QPersistentModelIndex(index), nullptr, QRect(), false, false});
        entry = &rows().last();
    }
    entry->ticking = false;
    entry->retry = std::move(retry);
    if (!tree->viewport()->property(kRetryFilterMark).toBool()) {
        tree->viewport()->setProperty(kRetryFilterMark, true);
        tree->viewport()->installEventFilter(new RetryFilter(tree));
    }
    tree->viewport()->update(tree->visualRect(index));
}

bool paintRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index, int textLeft) {
    const QVariant state = index.data(rowLoadingRole());
    if (!state.isValid()) return false;
    const qint64 started = state.toLongLong();
    const Theme::Tokens t = Theme::tokens();
    const QRect row = option.rect;
    const QFont font = Theme::textFont(option.font, Theme::TextRole::Caption);
    const QFontMetrics metrics(font);
    const QString text = index.data(Qt::DisplayRole).toString();
    const int right = row.right() - 8;
    painter->save();
    painter->setFont(font);
    if (started >= 0) {
        // 加载中：没满门槛什么都不画（这一行照旧占住高度）。
        const qint64 shown = clockMs() - started - Anim::loadingDelayMs();
        if (shown >= 0) {
            painter->setOpacity(Anim::enabled() ? outCubic(qreal(shown) / Anim::appearMs()) : 1.0);
            constexpr int dot = 6, gap = 6, after = 10;
            paintDots(painter, QPointF(textLeft, row.center().y() + 0.5), dot, gap, clockMs());
            const int x = textLeft + 3 * dot + 2 * gap + after;
            painter->setPen(t.secondary);
            painter->drawText(QRect(x, row.top(), qMax(0, right - x), row.height()), Qt::AlignLeft | Qt::AlignVCenter,
                              metrics.elidedText(text, Qt::ElideRight, qMax(0, right - x)));
        }
        painter->restore();
        return true;
    }
    // 出错：图标 + 原因 + 「重试」文字按钮。原因太长就截，「重试」那一格永远留着。
    constexpr int icon = 16, iconGap = 8, buttonPad = 8;
    paintCloudOff(painter, QRectF(textLeft, row.center().y() - icon / 2.0, icon, icon), 1.6, t.disabled);
    QFont buttonFont = Theme::textFont(option.font, Theme::TextRole::Body);
    const QFontMetrics buttonMetrics(buttonFont);
    const QString retryText = QObject::tr("重试");
    const int buttonW = buttonMetrics.horizontalAdvance(retryText) + 2 * buttonPad;
    const int x = textLeft + icon + iconGap;
    const int reasonW = qMax(0, qMin(metrics.horizontalAdvance(text), right - x - 4 - buttonW));
    painter->setPen(t.secondary);
    painter->drawText(QRect(x, row.top(), reasonW, row.height()), Qt::AlignLeft | Qt::AlignVCenter,
                      metrics.elidedText(text, Qt::ElideRight, reasonW));
    const QRect button(x + reasonW + 4, row.center().y() - 14, buttonW, 28);
    painter->setFont(buttonFont);
    painter->setPen(Theme::textTone(t.accentInk, t.deep));
    painter->drawText(button, Qt::AlignCenter, retryText);
    painter->restore();
    if (RowEntry *entry = entryFor(index)) entry->retryRect = button;
    return true;
}

bool clockRunning() { return timer().isActive(); }

int tickingRowCount() {
    int count = 0;
    for (const RowEntry &e : rows()) count += e.ticking && e.index.isValid() ? 1 : 0;
    return count;
}

QRect rowRetryRect(const QModelIndex &index) {
    const RowEntry *entry = entryFor(index);
    return entry != nullptr ? entry->retryRect : QRect();
}

}  // namespace LoadingState
