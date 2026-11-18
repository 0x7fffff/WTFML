/**
 * @file FlowLayout.hpp
 * @brief 流式布局：一行放不下就折到下一行，子项按各自的 sizeHint 摆，不拉伸。
 *
 * 用在"项数不定、又不许出横向滚动条"的地方（工具箱的选项行、资源详情页的版本 chip）。
 * 父布局靠 heightForWidth 拿到折行后的高度，所以放它的容器不要钉死高度。
 */
#pragma once

#include <QLayout>
#include <QList>
#include <QRect>
#include <QSize>

class FlowLayout final : public QLayout {
public:
    explicit FlowLayout(int hSpacing = 8, int vSpacing = 8) : m_h(hSpacing), m_v(vSpacing) {
        setContentsMargins(0, 0, 0, 0);
    }
    ~FlowLayout() override { qDeleteAll(m_items); }
    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return int(m_items.size()); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override {
        return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), true); }
    void setGeometry(const QRect &rect) override {
        QLayout::setGeometry(rect);
        arrange(rect, false);
    }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override {
        QSize size;
        for (QLayoutItem *item : m_items) size = size.expandedTo(item->minimumSize());
        const QMargins m = contentsMargins();
        return size + QSize(m.left() + m.right(), m.top() + m.bottom());
    }

private:
    int arrange(const QRect &rect, bool testOnly) const {
        const QMargins m = contentsMargins();
        const QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
        int x = area.x(), y = area.y(), lineHeight = 0;
        for (QLayoutItem *item : m_items) {
            if (item->isEmpty()) continue;
            const QSize hint = item->sizeHint();
            if (x > area.x() && x + hint.width() > area.right() + 1) {
                x = area.x();
                y += lineHeight + m_v;
                lineHeight = 0;
            }
            if (!testOnly) item->setGeometry(QRect(QPoint(x, y), hint));
            x += hint.width() + m_h;
            lineHeight = qMax(lineHeight, hint.height());
        }
        return y + lineHeight - rect.y() + m.top() + m.bottom();
    }
    QList<QLayoutItem *> m_items;
    int m_h, m_v;
};
