/**
 * @file PresetInstallPicker.cpp
 * @brief 安装页那张「预设」弹窗的正文（头文件里写了它为什么长这样）。
 */

#include "PresetInstallPicker.hpp"

#include "MainWindowUI.hpp"
#include "Theme.hpp"

#include <QComboBox>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

PresetInstallPicker::PresetInstallPicker(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    m_context = new QLabel(this);
    m_context->setObjectName(contextLabelName());
    m_context->setWordWrap(true);
    root->addWidget(m_context);

    m_tabs = new QTabBar(this);
    m_tabs->setObjectName(tabBarName());
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    m_tabs->setUsesScrollButtons(true);
    root->addWidget(m_tabs);

    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    connect(m_tabs, &QTabBar::currentChanged, m_stack, &QStackedWidget::setCurrentIndex);
}

void PresetInstallPicker::setContext(const QString &gameVersion, const QString &loader) {
    const QString versionText = gameVersion.trimmed();
    const QString loaderText = loader.trimmed();
    QString text = tr("游戏版本 %1").arg(versionText.isEmpty() ? tr("（未选）") : versionText);
    if (!loaderText.isEmpty()) {
        text += QStringLiteral(" · ") + loaderText;
    }
    m_context->setText(text);
}

QWidget *PresetInstallPicker::buildPage(int index, const Category &category, Page *out) {
    auto *page = new QWidget(m_stack);
    out->key = category.key;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // ---- 空状态：这一类一份预设都没有 ----
    if (category.options.isEmpty()) {
        auto *empty = new QWidget(page);
        auto *emptyLayout = new QVBoxLayout(empty);
        emptyLayout->setContentsMargins(0, 0, 0, 0);
        emptyLayout->setSpacing(8);
        auto *label = new QLabel(tr("这一类还没有预设。"), empty);
        label->setObjectName(emptyLabelName(index));
        label->setWordWrap(true);
        emptyLayout->addWidget(label);
        auto *manage = new QPushButton(manageText(), empty);
        manage->setObjectName(manageButtonName(index));
        Theme::markButton(manage, Theme::ButtonKind::Neutral);
        manage->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(manage, &QPushButton::clicked, this, &PresetInstallPicker::manageRequested);
        emptyLayout->addWidget(manage, 0, Qt::AlignLeft);
        layout->addWidget(empty);
        layout->addStretch(1);

        out->empty = empty;
        out->emptyLabel = label;
        out->manageButton = manage;
        return page;
    }

    // ---- 正常状态：下拉框 + 状态那一行 + 可展开的原因 ----
    auto *combo = new QComboBox(page);
    combo->setObjectName(comboName(index));
    combo->addItem(noneText(), QString());
    for (const Option &option : category.options) {
        QString label = option.name;
        if (label.isEmpty()) {
            label = option.id;
        }
        if (!option.available) {
            // 「已删除/损坏的所选项明确提示，不静默换为另一项」：那一项**留在原地**，
            // 名字后面直说它不在了 —— 换成别的项的话，用户下一次看到的不是他选的那一份。
            label += tr("（已不在盘上）");
        }
        combo->addItem(label, option.id);
    }
    {
        const QSignalBlocker blocker(combo);
        const int row = category.selectedId.isEmpty() ? 0 : combo->findData(category.selectedId);
        combo->setCurrentIndex(row >= 0 ? row : 0);
    }
    layout->addWidget(combo);

    auto *status = new QLabel(page);
    status->setObjectName(statusName(index));
    status->setWordWrap(true);
    layout->addWidget(status);

    auto *reasonsButton = new QPushButton(reasonsText(), page);
    reasonsButton->setObjectName(reasonsButtonName(index));
    reasonsButton->setCheckable(true);
    Theme::markButton(reasonsButton, Theme::ButtonKind::Neutral);
    reasonsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    layout->addWidget(reasonsButton, 0, Qt::AlignLeft);

    auto *reasonsLabel = new QLabel(page);
    reasonsLabel->setObjectName(reasonsLabelName(index));
    reasonsLabel->setWordWrap(true);
    reasonsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    reasonsLabel->setVisible(false);
    layout->addWidget(reasonsLabel);
    layout->addStretch(1);
    connect(reasonsButton, &QPushButton::toggled, reasonsLabel, &QLabel::setVisible);

    out->combo = combo;
    out->status = status;
    out->normalTextColor = status->palette().color(QPalette::WindowText);
    out->reasonsButton = reasonsButton;
    out->reasonsLabel = reasonsLabel;
    applyStatus(*out, category.status, category.reasons, category.problem);

    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, index, combo](int row) {
                if (index < 0 || index >= m_categories.size() || index >= m_pages.size()) {
                    return;
                }
                const QString id = combo->itemData(row).toString();
                // 选中的那一份变了 ⇒ 状态行上那几句话**不再成立**（那是上一份的计划）。
                // 先清掉，调用方算完新计划再 `setStatus()` 填回来 —— 留着旧数字是最糟的
                // 一种：用户以为新选的那份就是"会动 12 项"。
                m_categories[index].selectedId = id;
                applyStatus(m_pages[index], QString(), QStringList(), false);
                emit selectionChanged(m_categories.at(index).key, id);
            });
    return page;
}

void PresetInstallPicker::setCategories(const QList<Category> &categories) {
    // 全量重建：打开弹窗时调一次。页签那几项和数据是同一份次序（下标即身份）。
    while (m_stack->count() > 0) {
        QWidget *page = m_stack->widget(0);
        m_stack->removeWidget(page);
        page->deleteLater();
    }
    {
        const QSignalBlocker blocker(m_tabs);
        while (m_tabs->count() > 0) {
            m_tabs->removeTab(0);
        }
    }
    m_categories = categories;
    m_pages.clear();

    for (int i = 0; i < m_categories.size(); ++i) {
        const Category &category = m_categories.at(i);
        m_tabs->addTab(category.title);
        Page pageRefs;
        QWidget *page = buildPage(i, category, &pageRefs);
        m_stack->addWidget(page);
        m_pages.append(pageRefs);
    }
    if (m_tabs->count() > 0) {
        m_tabs->setCurrentIndex(0);
        m_stack->setCurrentIndex(0);
    }
}

void PresetInstallPicker::applyStatus(Page &page, const QString &status, const QStringList &reasons,
                                      bool problem) {
    if (page.status == nullptr) {
        return;
    }
    QString text = status;
    if (text.isEmpty()) {
        // 调用方还没算（刚换了下拉框那一拍）时，只说"现在选没选"这一件确定的事。
        const int categoryIndex = indexOfKey(page.key);
        text = categoryIndex >= 0 && m_categories.at(categoryIndex).selectedId.isEmpty()
                   ? QStringLiteral("未选择")
                   : QString();
    }
    page.status->setText(text);
    page.status->setVisible(!text.isEmpty());

    QPalette palette = page.status->palette();
    palette.setColor(QPalette::WindowText,
                     problem ? MainWindowUI::installNameErrorColor(palette)
                             : page.normalTextColor);
    page.status->setPalette(palette);

    if (page.reasonsButton != nullptr) {
        page.reasonsButton->setVisible(!reasons.isEmpty());
        page.reasonsLabel->setText(reasons.join(QLatin1Char('\n')));
        page.reasonsLabel->setVisible(page.reasonsButton->isChecked() && !reasons.isEmpty());
    }
}

void PresetInstallPicker::setStatus(const QString &key, const QString &status,
                                    const QStringList &reasons, bool problem) {
    const int index = indexOfKey(key);
    if (index < 0 || index >= m_pages.size()) {
        return;
    }
    applyStatus(m_pages[index], status, reasons, problem);
}

QString PresetInstallPicker::selectedId(const QString &key) const {
    const int index = indexOfKey(key);
    return index >= 0 ? m_categories.at(index).selectedId : QString();
}

QList<QPair<QString, QString>> PresetInstallPicker::selections() const {
    QList<QPair<QString, QString>> out;
    for (const Category &category : m_categories) {
        if (!category.selectedId.isEmpty()) {
            out.append({category.key, category.selectedId});
        }
    }
    return out;
}

int PresetInstallPicker::selectedCount() const {
    int count = 0;
    for (const Category &category : m_categories) {
        count += category.selectedId.isEmpty() ? 0 : 1;
    }
    return count;
}

int PresetInstallPicker::currentTab() const {
    return m_tabs != nullptr ? m_tabs->currentIndex() : -1;
}

void PresetInstallPicker::setCurrentTab(int index) {
    if (m_tabs != nullptr && index >= 0 && index < m_tabs->count()) {
        m_tabs->setCurrentIndex(index);
    }
}

int PresetInstallPicker::indexOfKey(const QString &key) const {
    for (int i = 0; i < m_categories.size(); ++i) {
        if (m_categories.at(i).key == key) {
            return i;
        }
    }
    return -1;
}
