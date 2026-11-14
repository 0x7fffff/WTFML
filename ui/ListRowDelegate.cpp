#include "ListRowDelegate.hpp"

#include "Theme.hpp"
#include "VersionIcons.hpp"

#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
// 设计稿 tokens.css .row：左右内边距 12、间距 14、圆角 r-ctl(10)；快捷操作 sm 键 30 高、间距 4。
constexpr int kPad = 12;
constexpr int kGap = 14;
constexpr int kRadius = 10;
constexpr int kActionHeight = 30;
constexpr int kActionGap = 4;
constexpr int kLaunchWidth = 72;

bool rowHovered(const QStyleOptionViewItem &option) {
    return option.state.testFlag(QStyle::State_MouseOver) && option.state.testFlag(QStyle::State_Enabled);
}
}  // namespace

ListRowDelegate *ListRowDelegate::install(QAbstractItemView *view) {
    auto *delegate = new ListRowDelegate(view);
    view->setItemDelegate(delegate);
    view->setMouseTracking(true);
    view->viewport()->setAttribute(Qt::WA_Hover, true);
    return delegate;
}

QRect ListRowDelegate::actionRect(const QRect &row, Action action) {
    const int top = row.center().y() - kActionHeight / 2 + 1;
    const int launchLeft = row.right() - kPad - kLaunchWidth + 1;
    switch (action) {
    case Action::Launch: return QRect(launchLeft, top, kLaunchWidth, kActionHeight);
    case Action::More: return QRect(launchLeft - kActionGap - kActionHeight, top, kActionHeight, kActionHeight);
    case Action::Folder:
        return QRect(launchLeft - 2 * (kActionGap + kActionHeight), top, kActionHeight, kActionHeight);
    }
    return {};
}

QSize ListRowDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    return QSize(QStyledItemDelegate::sizeHint(option, index).width(), rowHeight());
}

void ListRowDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    const Theme::Tokens t = Theme::tokens();
    const QRect r = option.rect;
    const bool hovered = rowHovered(option);
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // 行间 1px 极淡分隔线（左右各缩 12）；本行或上一行有底色时不画，免得线压在色块边上。
    bool prevSelected = false;
    if (index.row() > 0) {
        const auto *view = qobject_cast<const QAbstractItemView *>(option.widget);
        prevSelected = view != nullptr && view->selectionModel() != nullptr &&
                       view->selectionModel()->isSelected(index.siblingAtRow(index.row() - 1));
    }
    if (index.row() > 0 && !hovered && !selected && !prevSelected) {
        QColor line = t.divider;  // 压到四成：只提示「换行了」，不画成格子
        line.setAlphaF(line.alphaF() * 0.4);
        painter->fillRect(QRect(r.left() + kPad, r.top(), r.width() - 2 * kPad, 1), line);
    }
    if (hovered || selected) {
        QPainterPath bg;
        bg.addRoundedRect(QRectF(r), kRadius, kRadius);
        painter->fillPath(bg, selected ? t.selected : t.hover);
    }

    // 封面：圆角淡底块（设计稿 .cover），里面居中摆图标（7/12 边长）
    const QRect cover(r.left() + kPad, r.center().y() - coverSize() / 2 + 1, coverSize(), coverSize());
    painter->setPen(Qt::NoPen);  // 不描边：淡底块自己就是形状
    painter->setBrush(t.subtle);
    painter->drawRoundedRect(QRectF(cover).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
    const int inner = coverSize() * 7 / 12;
    const QVariant cover_ = index.data(CoverRole);
    const QVariant decoration = cover_.isValid() ? cover_ : index.data(Qt::DecorationRole);
    QPixmap pix = decoration.canConvert<QIcon>() ? decoration.value<QIcon>().pixmap(inner * 2, inner * 2)
                                                 : decoration.value<QPixmap>();
    if (!pix.isNull())
        painter->drawPixmap(QRect(cover.center().x() - inner / 2 + 1, cover.center().y() - inner / 2 + 1, inner, inner), pix);

    // 右侧：悬停时快捷操作，否则元信息
    int textRight = r.right() - kPad;
    const QVariant metaIcon = index.data(MetaIconRole);
    if (metaIcon.isValid()) {
        // 设计稿 manage：加载器那一列钉在行宽 46% 处，图标 16 + 字，悬停时照常显示。
        const int x = r.left() + r.width() * 46 / 100;
        const QPixmap icon = metaIcon.value<QIcon>().pixmap(32, 32);
        if (!icon.isNull()) painter->drawPixmap(QRect(x, r.center().y() - 8, 16, 16), icon);
        QFont f = option.font;
        f.setPixelSize(Theme::textSize(Theme::TextRole::Caption));
        painter->setFont(f);
        painter->setPen(t.secondary);
        painter->drawText(QRect(x + 22, r.top(), qMax(0, r.right() - x - 22), r.height()), Qt::AlignVCenter | Qt::AlignLeft,
                          painter->fontMetrics().elidedText(index.data(MetaRole).toString(), Qt::ElideRight, r.width() / 4));
        textRight = x - kGap;
    }
    if (hovered) {
        for (const Action a : {Action::Folder, Action::More}) {
            const QRect box = actionRect(r, a);
            VersionIcons::paintLine(*painter, a == Action::Folder ? QStringLiteral("folder") : QStringLiteral("more"),
                                    QRectF(box).adjusted(6, 6, -6, -6), t.secondary, a == Action::More ? 2.6 : 1.6);
        }
        const QRect launch = actionRect(r, Action::Launch);
        QPainterPath pill;
        pill.addRoundedRect(QRectF(launch), launch.height() / 2.0, launch.height() / 2.0);
        painter->fillPath(pill, t.primary);
        QFont f = option.font;
        f.setWeight(QFont::Medium);
        painter->setFont(f);
        const QString text = QStringLiteral("启动");
        const int textW = painter->fontMetrics().horizontalAdvance(text);
        const int iconW = 14;
        const int startX = launch.center().x() - (iconW + 6 + textW) / 2;
        VersionIcons::paintLine(*painter, QStringLiteral("play"),
                                QRectF(startX, launch.center().y() - iconW / 2.0 + 1, iconW, iconW), t.onPrimary);
        painter->setPen(t.onPrimary);
        painter->drawText(QRect(startX + iconW + 6, launch.top(), textW + 2, launch.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, text);
        if (!metaIcon.isValid()) textRight = actionRect(r, Action::Folder).left() - kGap;
    } else if (!metaIcon.isValid()) {
        const QString meta = index.data(MetaRole).toString();
        if (!meta.isEmpty()) {
            QFont f = option.font;
            f.setPixelSize(Theme::textSize(Theme::TextRole::Caption));
            painter->setFont(f);
            painter->setPen(t.disabled);
            const int w = painter->fontMetrics().horizontalAdvance(meta);
            painter->drawText(QRect(r.right() - kPad - w, r.top(), w + 1, r.height()),
                              Qt::AlignVCenter | Qt::AlignRight, meta);
            textRight = r.right() - kPad - w - kGap;
        }
    }

    // 两行字：标题 14/600（设计稿 500，Qt 下同字号显轻，加一档），副行辅助档 13 次要色
    const int textLeft = cover.right() + 1 + kGap;
    const QRect textBox(textLeft, r.top(), qMax(0, textRight - textLeft), r.height());
    QFont titleFont = option.font;
    titleFont.setPixelSize(Theme::textSize(Theme::TextRole::Body));
    titleFont.setWeight(QFont::DemiBold);
    QFont subFont = option.font;
    subFont.setPixelSize(Theme::textSize(Theme::TextRole::Caption));
    const QString title = index.data(Qt::DisplayRole).toString();
    const QString sub = index.data(SubtitleRole).toString();
    const int titleH = 20;
    const int subH = sub.isEmpty() ? 0 : 18;
    const int top = r.top() + (r.height() - titleH - subH) / 2;
    painter->setFont(titleFont);
    painter->setPen(t.text);
    const QString shownTitle = painter->fontMetrics().elidedText(title, Qt::ElideRight, textBox.width());
    painter->drawText(QRect(textBox.left(), top, textBox.width(), titleH), Qt::AlignVCenter | Qt::AlignLeft, shownTitle);
    if (shownTitle != title) m_elidedTitles.insert(index);
    else m_elidedTitles.remove(index);
    if (!sub.isEmpty()) {
        painter->setFont(subFont);
        painter->setPen(t.secondary);
        painter->drawText(QRect(textBox.left(), top + titleH, textBox.width(), subH), Qt::AlignVCenter | Qt::AlignLeft,
                          painter->fontMetrics().elidedText(sub, Qt::ElideRight, textBox.width()));
    }
    painter->restore();
}

bool ListRowDelegate::editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                                  const QModelIndex &index) {
    if (event->type() == QEvent::MouseButtonRelease) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            for (const Action a : {Action::Folder, Action::More, Action::Launch}) {
                if (actionRect(option.rect, a).contains(mouse->position().toPoint())) {
                    emit actionTriggered(index, a);
                    return true;
                }
            }
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}
