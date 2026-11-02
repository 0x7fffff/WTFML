/**
 * @file InstanceFilesPageStructures.cpp
 * @brief 「结构 / 原理图」那一页顶上的两排「来源」：重建那些下拉框、读回选中的目录。
 *
 * 【为什么单独一个翻译单元】
 *
 * 这一页和别的七页不一样：它一次列**好几个目录**（每一行存档有自己的 `generated/`，
 * 投影那一排还有一个 `schematics/`），而排数是**每一趟现算**的 —— 这一趟有三个存档有
 * 结构就是三排 + 一排投影，下一趟可能只剩一排。于是 `setStructureSources()` 要整块拆了
 * 重建，`selectedStructurePath()` 又要按"第几排"读回来。
 *
 * 【几条说死的规矩】
 *
 * - 拆旧的时候**先藏再排删除**：`deleteLater()` 要等下一趟事件循环，这一会儿那一排还挂
 *   在原来的位置上，屏幕上就是"上一趟那一排闪一下"；
 * - 排头宽度**统一成最宽的那一个**，量的是递进来的 title（不是一个写死的词）—— 哪一天某
 *   一排的排头多一个字，两排的下拉框就整体错开一格；
 * - 一个来源一行都没有 ⇒ 摆一行**灰的**写「无可用的结构」，它不是一个能选中的值；
 * - 换一个来源 = 换一份要列的东西 ⇒ 当场重列；填 items 时先填、再 connect。
 */

#include "InstanceFilesPage.hpp"

#include "InstanceFilesPageInternals.hpp"  // makeSecondary()：那一排的排头是灰的

#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QWidget>

using namespace InstanceFilesPageInternals;

void InstanceFilesPage::setStructureSources(const QList<StructureSource> &sources) {
    m_structureSources = sources;
    // 整块重建那几个下拉框（排数会变：这个实例上有几个存档有结构，是**每一趟现算**的）。
    // 那些控件都挂在这一格底下 ⇒ 删子控件就够了（`m_structureCombos` 里那几把是裸指针，
    // 下面这一句之后一个都不作数，必须一起清掉）。
    m_structureCombos.clear();
    m_structureTitles.clear();
    if (m_structureLayout != nullptr) {
        while (QLayoutItem *item = m_structureLayout->takeAt(0)) {
            if (QWidget *w = item->widget()) {
                // **先藏再排删除**：`deleteLater()` 要等下一趟事件循环，而这一会儿那一排
                // 还挂在原来的位置上（几何没变）——屏幕上就是"上一趟那一排闪一下"。
                w->hide();
                w->deleteLater();
            }
            delete item;
        }
    }
    // 两个排头占的宽度**统一成最宽的那一个**（下面每一格都拿这一份），这样两排的下拉框
    // 左边对得齐 —— 一列下来歪一格是最显眼的那种不齐。
    // 这一句量的是**递进来的那些 title**，不是一个写死的词：「存档」/「投影」当前同宽
    // （两格全角），但"排头一样宽"是这两句话凑巧的结果，不是这套排版的保证 ——
    // 照当前那两个字去量的话（`horizontalAdvance(structureWorldTitle())`），哪一天某一排
    // 的排头多一个字，它那一排的下拉框就整体右移一格，屏幕上就是两排没对齐。
    // `makeSecondary()` 改的只是颜色、字号没动 ⇒ 拿这一页的 fontMetrics() 量是准的。
    int structureTitleWidth = 0;
    const QFontMetrics titleMetrics = fontMetrics();
    for (const StructureSource &source : m_structureSources) {
        structureTitleWidth =
            qMax(structureTitleWidth, titleMetrics.horizontalAdvance(source.title));
    }
    for (int row = 0; row < m_structureSources.size(); ++row) {
        const StructureSource &source = m_structureSources.at(row);
        auto *line = new QWidget(m_structureRows);
        auto *layout = new QHBoxLayout(line);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        auto *title = new QLabel(source.title, line);
        title->setObjectName(structureTitleName(row));
        makeSecondary(title);
        title->setMinimumWidth(structureTitleWidth + 4);
        layout->addWidget(title);
        auto *combo = new QComboBox(line);
        combo->setObjectName(structureComboName(row));
        // 一个来源一行都没有 ⇒ 摆一行**灰的**，写「无可用的结构」。它**不是**一个
        // 可选中的值：选中它就等于没选，
        // 于是这一排对下面那份列表一个条目都不贡献。
        if (source.labels.isEmpty()) {
            combo->addItem(structureNoneText());
            combo->setEnabled(false);
        } else {
            combo->addItems(source.labels);
        }
        // 换一个来源 = 换一份要列的东西 ⇒ 当场重列（和「刷新」同一件事，只是目录换了）。
        // NOTE: 填 items 那一会儿**不发这一发**（那时 row 还没进 m_structureCombos，
        // 下面这个 lambda 里那句查找自然落空 —— 但那不足以当防护，所以填之前
        // 先把 connect 摆好是**错的**）。这里的次序是：先填、再 connect。
        connect(combo, &QComboBox::currentIndexChanged, this, [this, row](int) {
            if (row >= 0 && row < m_structureCombos.size()) {
                refresh();
            }
        });
        layout->addWidget(combo, 1);
        m_structureLayout->addWidget(line);
        m_structureTitles.append(title);
        m_structureCombos.append(combo);
    }
    // 摆完当场列一遍（和 setDirectory 同一个口径：看不见的那几页先欠着）。
    if (!isCurrentPage()) {
        m_refreshPending = true;
        return;
    }
    refresh();
}

QString InstanceFilesPage::selectedStructurePath(int row) const {
    if (row < 0 || row >= m_structureSources.size() || row >= m_structureCombos.size()) {
        return QString();
    }
    const StructureSource &source = m_structureSources.at(row);
    const QComboBox *combo = m_structureCombos.at(row);
    if (combo == nullptr || source.paths.size() != source.labels.size()) {
        return QString();
    }
    const int index = combo->currentIndex();
    // 「无可用的结构」那一行：labels 本来就是空的 ⇒ 上面那一句已经返回了。
    if (index < 0 || index >= source.paths.size()) {
        return QString();
    }
    return source.paths.at(index);
}
