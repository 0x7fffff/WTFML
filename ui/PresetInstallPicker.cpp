/**
 * @file PresetInstallPicker.cpp
 * @brief 安装页那张「预设」弹窗的正文（头文件里写了它为什么长这样）。
 */

#include "PresetInstallPicker.hpp"

#include "MainWindowUI.hpp"
#include "Theme.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QHBoxLayout>
#include "ElidedLabel.hpp"
#include <QCheckBox>
#include <QListWidget>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QStyleOptionButton>
#include <QGridLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QScrollArea>

#include <QVBoxLayout>

namespace {
constexpr int kStatusRole = Qt::UserRole + 1;
constexpr int kCompositeRole = Qt::UserRole + 2;
constexpr int kCountRole = Qt::UserRole + 3;
constexpr int kProblemRole = Qt::UserRole + 4;
constexpr int kTypeRole = Qt::UserRole + 5;

class PresetChoiceDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
        return QSize(180, 76);
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const QPalette palette = Theme::selectionPalette(option.palette, selected);
        painter->save();
        painter->setClipRect(option.rect);
        painter->fillRect(option.rect, selected
            ? Theme::selectionFill(option.palette.color(QPalette::Base))
            : option.palette.color(QPalette::Base));
        QStyleOptionButton radio;
        radio.rect = QRect(option.rect.left() + 10, option.rect.center().y() - 9, 18, 18);
        radio.palette = palette;
        radio.state = QStyle::State_Enabled | (selected ? QStyle::State_On : QStyle::State_Off);
        QApplication::style()->drawPrimitive(QStyle::PE_IndicatorRadioButton, &radio, painter, option.widget);
        const bool none = index.data(Qt::UserRole).toString().isEmpty();
        if (!none) {
            QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            if (icon.isNull()) icon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
            icon.paint(painter, QRect(option.rect.left() + 38, option.rect.center().y() - 16, 32, 32));
        }
        const QRect text = option.rect.adjusted(none ? 40 : 80, 7, -10, -7);
        QString status = index.data(kStatusRole).toString();
        const int count = index.data(kCountRole).toInt();
        QString metadata = index.data(kTypeRole).toString();
        if (count >= 0) metadata += tr(" / %1 项").arg(count);
        const QString name = index.data(Qt::DisplayRole).toString();
        painter->setFont(Theme::textFont(option.font, Theme::TextRole::Body));
        painter->setPen(palette.color(QPalette::Text));
        const QRect nameRect = none ? text : QRect(text.topLeft(), QSize(text.width(), 22));
        painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
                         painter->fontMetrics().elidedText(name, Qt::ElideRight, text.width()));
        if (!none) {
            painter->setFont(Theme::textFont(option.font, Theme::TextRole::Caption));
            painter->setPen(palette.color(QPalette::PlaceholderText));
            painter->drawText(QRect(text.left(), text.top() + 22, text.width(), 18), Qt::AlignVCenter | Qt::AlignLeft,
                painter->fontMetrics().elidedText(metadata, Qt::ElideRight, text.width()));
        }
        if (!status.isEmpty()) {
            painter->setFont(Theme::textFont(option.font, Theme::TextRole::Caption));
            painter->setPen(index.data(kProblemRole).toBool()
                ? MainWindowUI::installNameErrorColor(palette)
                : palette.color(QPalette::PlaceholderText));
            const QRect statusRect(text.left(), text.top() + 42, text.width(), text.height() - 42);
            painter->drawText(statusRect, Qt::AlignVCenter | Qt::AlignLeft,
                             painter->fontMetrics().elidedText(status, Qt::ElideRight, text.width()));
        }
        painter->restore();
    }
};
}

PresetInstallPicker::PresetInstallPicker(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);
    m_pendingBar = new QWidget(this);
    m_pendingBar->setObjectName(QStringLiteral("presetReplacementPrompt"));
    auto *pending = new QHBoxLayout(m_pendingBar);
    pending->setContentsMargins(0, 0, 0, 0);
    m_pendingLabel = new QLabel(m_pendingBar);
    m_pendingLabel->setTextFormat(Qt::PlainText);
    m_pendingLabel->setWordWrap(true);
    pending->addWidget(m_pendingLabel, 1);
    auto *replace = new QPushButton(tr("替换"), m_pendingBar);
    replace->setObjectName(QStringLiteral("presetReplaceConfirm"));
    auto *cancel = new QPushButton(tr("取消"), m_pendingBar);
    cancel->setObjectName(QStringLiteral("presetReplaceCancel"));
    Theme::markButton(replace, Theme::ButtonKind::Primary);
    Theme::markButton(cancel, Theme::ButtonKind::Neutral);
    pending->addWidget(cancel); pending->addWidget(replace);
    connect(replace, &QPushButton::clicked, this, [this] {
        auto action = std::move(m_pendingSelection);
        m_pendingBar->hide(); emit pendingChanged(false);
        if (action) action();
    });
    connect(cancel, &QPushButton::clicked, this, [this] {
        m_pendingSelection = {}; m_pendingBar->hide(); emit pendingChanged(false);
    });
    m_pendingBar->hide(); root->addWidget(m_pendingBar);
    auto *body = new QHBoxLayout;
    body->setSpacing(16);
    m_navigation = new QListWidget(this);
    m_navigation->setObjectName(tabBarName());
    m_navigation->setProperty("wtfmlSidebarRail", true);
    m_navigation->setFixedWidth(126);
    m_navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body->addWidget(m_navigation);
    m_stack = new QStackedWidget(this);
    m_stack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    body->addWidget(m_stack, 1);
    root->addLayout(body, 1);
    m_summary = new QWidget(this);
    m_summary->setObjectName(QStringLiteral("presetInstallSummary"));
    auto *summary = new QGridLayout(m_summary);
    summary->setContentsMargins(0, 0, 0, 0);
    summary->setHorizontalSpacing(16);
    m_summaryTitle = new QLabel(m_summary);
    summary->addWidget(m_summaryTitle, 0, 0, 1, 2);
    root->addWidget(m_summary);
    connect(m_navigation, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    setMinimumSize(0, 330);
}

void PresetInstallPicker::setContext(const QString &gameVersion, const QString &loader) {
    // 上下文只供无障碍与检查使用，不重复安装页已有的信息。
    setAccessibleDescription(tr("Minecraft %1 / %2").arg(gameVersion, loader.isEmpty() ? tr("原版") : loader));
    const QString kind = loader.trimmed().toLower();
    m_modsAllowed = kind == QStringLiteral("fabric") || kind == QStringLiteral("forge") ||
        kind == QStringLiteral("neoforge") || kind == QStringLiteral("quilt");
    const int mods = indexOfKey(QStringLiteral("mods"));
    if (mods >= 0 && !m_modsAllowed) selectId(QStringLiteral("mods"), QString());
    if (mods >= 0 && m_pages[mods].list) {
        auto *list = m_pages[mods].list;
        for (int i = 1; i < list->count(); ++i) {
            auto flags = list->item(i)->flags();
            flags.setFlag(Qt::ItemIsEnabled, m_modsAllowed);
            list->item(i)->setFlags(flags);
            if (!m_modsAllowed) list->item(i)->setToolTip(tr("需要模组加载器"));
            else if (list->item(i)->data(kStatusRole).toString() == tr("需要模组加载器")) {
                list->item(i)->setData(kStatusRole, tr("未检查")); list->item(i)->setToolTip(QString());
            }
        }
    }
    updateCompositeChecks();
}

QWidget *PresetInstallPicker::buildPage(int index, const Category &category) {
    auto *page = new QWidget(m_stack);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(category.title, page);
    title->setFont(Theme::textFont(title->font(), Theme::TextRole::Heading));
    layout->addWidget(title);
    if (category.options.isEmpty()) {
        auto *empty = new QLabel(tr("暂无可用的%1预设").arg(category.title), page);
        empty->setObjectName(emptyLabelName(index));
        empty->setWordWrap(true);
        layout->addWidget(empty);
        auto *manage = new QPushButton(manageText(), page);
        manage->setObjectName(manageButtonName(index));
        Theme::markButton(manage, Theme::ButtonKind::Neutral);
        layout->addWidget(manage, 0, Qt::AlignLeft);
        connect(manage, &QPushButton::clicked, this, &PresetInstallPicker::manageRequested);
        layout->addStretch(1);
        return page;
    }
    auto *search = new QLineEdit(page);
    search->setPlaceholderText(tr("搜索预设"));
    layout->addWidget(search);
    auto *list = new QListWidget(page);
    m_pages[index].list = list;
    list->setObjectName(listName(index));
    list->setAccessibleName(category.title);
    list->setItemDelegate(new PresetChoiceDelegate(list));
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setMinimumHeight(100);
    auto *none = new QListWidgetItem(noneText(), list);
    none->setData(Qt::UserRole, QString()); none->setData(kCountRole, -1);
    int selected = 0;
    for (const auto &option : category.options) {
        auto *item = new QListWidgetItem(option.name.isEmpty() ? option.id : option.name, list);
        item->setData(Qt::UserRole, option.id);
        item->setData(kCompositeRole, option.composite);
        item->setData(kCountRole, option.itemCount);
        item->setIcon(option.icon);
        item->setData(kTypeRole, option.composite ? tr("综合预设") : category.title + tr("预设"));
        item->setData(kStatusRole, option.available ? tr("未检查") : tr("读取失败"));
        if (category.key == QStringLiteral("mods") && !m_modsAllowed) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setToolTip(tr("需要模组加载器"));
            item->setData(kStatusRole, tr("需要模组加载器"));
        }
        if (option.id == category.selectedId) selected = list->count() - 1;
    }
    list->setCurrentRow(selected);
    layout->addWidget(list, 1);
    auto *show = new QCheckBox(tr("显示综合预设"), page);
    show->setObjectName(QStringLiteral("presetShowComposite%1").arg(index));
    show->setChecked(true);
    layout->addWidget(show);
    const auto filter = [list, show, search] {
        for (int i = 1; i < list->count(); ++i) {
            auto *item = list->item(i);
            const bool excluded = (!show->isChecked() && item->data(kCompositeRole).toBool()) ||
                !item->text().contains(search->text().trimmed(), Qt::CaseInsensitive);
            item->setHidden(excluded);
        }
    };
    connect(show, &QCheckBox::toggled, list, [filter](bool) { filter(); });
    connect(search, &QLineEdit::textChanged, list, [filter](const QString &) { filter(); });
    auto *details = new QPushButton(reasonsText(), page);
    details->setObjectName(reasonsButtonName(index));
    Theme::markButton(details, Theme::ButtonKind::Neutral);
    layout->addWidget(details, 0, Qt::AlignLeft);
    m_pages[index].details = details;
    details->setEnabled(!category.selectedId.isEmpty());
    connect(details, &QPushButton::clicked, this, [this, index] { openDetails(index); });
    connect(list, &QListWidget::currentRowChanged, this, [this, index, list](int row) {
        if (row < 0) return;
        const QString id = list->item(row)->data(Qt::UserRole).toString();
        const QString old = m_categories[index].selectedId;
        // 先回到已经提交的选择，确认替换后才改变最终来源。
        { const QSignalBlocker blocker(list);
          for (int i = 0; i < list->count(); ++i)
              if (list->item(i)->data(Qt::UserRole).toString() == old) list->setCurrentRow(i); }
        requestSelection(index, id);
    });
    connect(list, &QListWidget::itemClicked, this, [this, index](QListWidgetItem *item) {
        if (item->data(Qt::UserRole).toString() == m_categories[index].selectedId)
            m_userTouched.insert(m_categories[index].key);
    });
    return page;
}

void PresetInstallPicker::setCategories(const QList<Category> &categories) {
    m_pendingSelection = {}; m_pendingBar->hide(); emit pendingChanged(false);
    m_compositePage = nullptr; m_compositeList = nullptr; m_compositeChecks.clear();
    m_selectedComposite.clear(); m_composites.clear();
    while (m_stack->count()) { QWidget *page = m_stack->widget(0); m_stack->removeWidget(page); delete page; }
    { const QSignalBlocker blocker(m_navigation); m_navigation->clear(); }
    m_categories = categories;
    if (!m_modsAllowed) for (auto &category : m_categories)
        if (category.key == QStringLiteral("mods")) category.selectedId.clear();
    m_pages.clear(); m_userTouched.clear();
    m_pages.resize(m_categories.size());
    auto *summary = qobject_cast<QGridLayout *>(m_summary->layout());
    while (summary->count() > 1) { auto *item = summary->takeAt(1); delete item->widget(); delete item; }
    for (int i = 0; i < m_categories.size(); ++i) {
        const auto &category = m_categories[i];
        auto *nav = new QListWidgetItem(tr("%1 (%2)").arg(category.title).arg(category.options.size()), m_navigation);
        nav->setSizeHint(QSize(110, 38));
        m_stack->addWidget(buildPage(i, category));
        auto *source = new ElidedLabel(m_summary);
        source->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        source->setObjectName(QStringLiteral("presetSource_") + category.key);
        m_pages[i].source = source;
        summary->addWidget(source, i / 2 + 1, i % 2);
        setStatus(category.key, category.status, category.reasons, category.problem);
    }
    if (!m_categories.isEmpty()) m_navigation->setCurrentRow(0);
    updateSummary();
}

int PresetInstallPicker::indexOfKey(const QString &key) const {
    for (int i = 0; i < m_categories.size(); ++i) if (m_categories[i].key == key) return i;
    return -1;
}
QString PresetInstallPicker::selectedId(const QString &key) const {
    const int i = indexOfKey(key); return i < 0 ? QString() : m_categories[i].selectedId;
}
QString PresetInstallPicker::nameFor(const QString &key, const QString &id) const {
    if (id.isEmpty()) return tr("未选择");
    const int i = indexOfKey(key);
    if (i >= 0) for (const auto &option : m_categories[i].options) if (option.id == id) return option.name;
    return id;
}
void PresetInstallPicker::confirmReplacement(const QString &text, std::function<void()> action) {
    m_pendingLabel->setText(text); m_pendingSelection = std::move(action);
    m_pendingBar->show(); emit pendingChanged(true);
}
void PresetInstallPicker::requestSelection(int index, const QString &id) {
    const auto &category = m_categories[index];
    if (category.key == QStringLiteral("mods") && !m_modsAllowed && !id.isEmpty()) return;
    if (!id.isEmpty() && !category.selectedId.isEmpty() && id != category.selectedId) {
        const QString key = category.key;
        confirmReplacement(tr("替换%1：%2 → %3").arg(category.title, nameFor(key, category.selectedId), nameFor(key, id)),
                           [this, key, id] { selectId(key, id); });
    } else selectId(category.key, id);
}
void PresetInstallPicker::selectId(const QString &key, const QString &id) {
    const int i = indexOfKey(key);
    if (key == QStringLiteral("mods") && !m_modsAllowed && !id.isEmpty()) return;
    if (i < 0 || !m_pages[i].list) return;
    QListWidget *list = m_pages[i].list;
    int row = -1;
    for (int n = 0; n < list->count(); ++n) if (list->item(n)->data(Qt::UserRole).toString() == id) { row = n; break; }
    if (row < 0) return;
    m_userTouched.insert(key);
    { const QSignalBlocker blocker(list); list->setCurrentRow(row); }
    const bool changed = m_categories[i].selectedId != id;
    m_categories[i].selectedId = id;
    if (changed) {
        m_pages[i].missing = m_pages[i].unknown = -1;
        setStatus(key, id.isEmpty() ? QString() : tr("未检查"), {}, false);
        emit selectionChanged(key, id);
    }
    m_pages[i].details->setEnabled(!id.isEmpty());
    updateSummary(); updateCompositeChecks();
}
void PresetInstallPicker::setStatus(const QString &key, const QString &status, const QStringList &reasons,
                                  bool problem, int itemCount, int missing, int unknown) {
    const int i = indexOfKey(key); if (i < 0) return;
    Page &page = m_pages[i]; page.status = status; page.reasons = reasons;
    if (missing >= 0) page.missing = missing;
    if (unknown >= 0) page.unknown = unknown;
    if (page.list && page.list->currentItem()) {
        auto *item = page.list->currentItem();
        item->setData(kStatusRole, status);
        item->setData(kProblemRole, problem);
        if (itemCount >= 0) item->setData(kCountRole, itemCount);
        item->setData(Qt::AccessibleDescriptionRole, status);
        item->setToolTip(reasons.join(QLatin1Char('\n')));
    }
    updateSummary(); if (m_detailsIndex == i) updateDetails();
}
void PresetInstallPicker::updateSummary() {
    int missing = 0, unknown = 0;
    for (int i = 0; i < m_categories.size(); ++i) {
        const auto &category = m_categories[i];
        const QString text = category.title + QStringLiteral(": ") + nameFor(category.key, category.selectedId);
        if (m_pages[i].source) { m_pages[i].source->setText(text); m_pages[i].source->setToolTip(text); }
        if (!category.selectedId.isEmpty()) { missing += qMax(0, m_pages[i].missing); unknown += qMax(0, m_pages[i].unknown); }
    }
    QString text = tr("本次应用：%1 类").arg(selectedCount());
    if (missing) text += tr(" / 缺失 %1 项").arg(missing);
    if (unknown) text += tr(" / 待确认 %1 项").arg(unknown);
    m_summaryTitle->setText(text);
}
QList<QPair<QString, QString>> PresetInstallPicker::selections() const {
    QList<QPair<QString, QString>> out;
    for (const auto &category : m_categories)
        if (!category.selectedId.isEmpty() || m_userTouched.contains(category.key)) out.append({category.key, category.selectedId});
    return out;
}
int PresetInstallPicker::selectedCount() const {
    int count = 0; for (const auto &category : m_categories) if (!category.selectedId.isEmpty()) ++count; return count;
}
int PresetInstallPicker::currentTab() const { return m_navigation->currentRow(); }
void PresetInstallPicker::setCurrentTab(int index) { m_navigation->setCurrentRow(index); }
void PresetInstallPicker::openDetails(int index) {
    if (index < 0 || index >= m_categories.size() || m_categories[index].selectedId.isEmpty()) return;
    if (m_details) m_details->close();
    auto *dialog = new QDialog(window());
    dialog->setObjectName(QStringLiteral("presetCompatibilityDetails"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(reasonsText()); dialog->resize(600, 400);
    auto *layout = new QVBoxLayout(dialog);
    auto *text = new QPlainTextEdit(dialog); text->setReadOnly(true);
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    m_details = dialog; m_detailsText = text; m_detailsIndex = index; updateDetails(); dialog->show();
}
void PresetInstallPicker::updateDetails() {
    if (!m_details || m_detailsIndex < 0 || m_detailsIndex >= m_pages.size()) return;
    const auto &category = m_categories[m_detailsIndex];
    const auto &page = m_pages[m_detailsIndex];
    m_detailsText->setPlainText(nameFor(category.key, category.selectedId) + QStringLiteral("\n\n") +
        page.status + QStringLiteral("\n\n") + page.reasons.join(QLatin1Char('\n')));
}

void PresetInstallPicker::setComposites(const QList<Composite> &presets) {
    if (m_compositePage) {
        const int index = m_stack->indexOf(m_compositePage);
        m_stack->removeWidget(m_compositePage); delete m_compositePage;
        delete m_navigation->takeItem(index);
    }
    m_composites = presets; m_compositeChecks.clear();
    auto *page = new QWidget(m_stack); m_compositePage = page;
    page->setObjectName(QStringLiteral("presetCompositePicker"));
    auto *layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(tr("综合预设"), page); title->setFont(Theme::textFont(title->font(), Theme::TextRole::Heading)); layout->addWidget(title);
    auto *list = new QListWidget(page); m_compositeList = list;
    list->setObjectName(QStringLiteral("presetCompositeList"));
    list->setItemDelegate(new PresetChoiceDelegate(list));
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (const auto &preset : presets) {
        auto *item = new QListWidgetItem(preset.name, list); item->setData(Qt::UserRole, preset.id);
        item->setData(kCountRole, preset.itemCount); item->setIcon(preset.icon);
        item->setData(kTypeRole, tr("综合预设"));
        item->setData(kStatusRole, tr("综合预设"));
    }
    if (presets.isEmpty()) layout->addWidget(new QLabel(tr("暂无可用的综合预设"), page));
    layout->addWidget(list, 1);
    auto *parts = new QGridLayout;
    for (int i = 0; i < m_categories.size(); ++i) {
        const auto &category = m_categories[i]; auto *check = new QCheckBox(category.title, page);
        check->setObjectName(QStringLiteral("presetCompositePart_") + category.key);
        check->setEnabled(false); parts->addWidget(check, i / 3, i % 3);
        m_compositeChecks.insert(category.key, check);
        connect(check, &QCheckBox::toggled, page, [this, key = category.key](bool on) {
            for (const auto &preset : m_composites) if (preset.id == m_selectedComposite) {
                const QString id = preset.parts.value(key); const int i = indexOfKey(key);
                if (i >= 0 && !id.isEmpty()) {
                    if (on) requestSelection(i, id);
                    else if (selectedId(key) == id) selectId(key, QString());
                }
                updateCompositeChecks(); break;
            }
        });
    }
    layout->addLayout(parts);
    connect(list, &QListWidget::currentRowChanged, page, [this, list](int row) {
        if (row < 0) return;
        const Composite preset = m_composites[row];
        const auto apply = [this, preset] {
            m_selectedComposite = preset.id;
            { const QSignalBlocker blocker(m_compositeList);
              for (int i = 0; i < m_compositeList->count(); ++i)
                  if (m_compositeList->item(i)->data(Qt::UserRole).toString() == preset.id) m_compositeList->setCurrentRow(i); }
            for (auto it = preset.parts.cbegin(); it != preset.parts.cend(); ++it) selectId(it.key(), it.value());
            updateCompositeChecks();
        };
        QStringList replacements;
        for (auto it = preset.parts.cbegin(); it != preset.parts.cend(); ++it) {
            const int i = indexOfKey(it.key());
            if (i >= 0 && !selectedId(it.key()).isEmpty() && selectedId(it.key()) != it.value()) replacements.append(m_categories[i].title);
        }
        if (replacements.isEmpty()) apply();
        else {
            { const QSignalBlocker blocker(list); list->clearSelection(); list->setCurrentRow(-1); }
            confirmReplacement(tr("替换%1，使用「%2」").arg(replacements.join(QStringLiteral("、")), preset.name), apply);
        }
    });
    m_stack->addWidget(page);
    auto *nav = new QListWidgetItem(tr("综合预设 (%1)").arg(presets.size()), m_navigation);
    nav->setSizeHint(QSize(110, 38)); updateCompositeChecks();
}
void PresetInstallPicker::updateCompositeChecks() {
    QMap<QString, QString> parts;
    QMap<QString, int> counts;
    for (const auto &preset : m_composites) if (preset.id == m_selectedComposite) { parts = preset.parts; counts = preset.counts; }
    for (auto it = m_compositeChecks.cbegin(); it != m_compositeChecks.cend(); ++it) {
        const QSignalBlocker blocker(it.value()); const QString id = parts.value(it.key());
        const int index = indexOfKey(it.key());
        if (index >= 0) it.value()->setText(m_categories[index].title +
            (counts.contains(it.key()) ? tr(" (%1)").arg(counts.value(it.key())) : QString()));
        const bool allowed = it.key() != QStringLiteral("mods") || m_modsAllowed;
        it.value()->setEnabled(!id.isEmpty() && allowed);
        it.value()->setToolTip(allowed ? QString() : tr("需要模组加载器"));
        it.value()->setChecked(!id.isEmpty() && selectedId(it.key()) == id);
    }
}
