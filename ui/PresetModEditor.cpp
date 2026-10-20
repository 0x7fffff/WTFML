#include "PresetModEditor.hpp"
#include "Theme.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QSet>
#include <QRegularExpression>
#include <QLineEdit>
#include <QUrl>

namespace {
bool normalizeProject(QString *platform, QString *id) {
    *id = id->trimmed();
    if (!id->contains(QLatin1String("://"))) return true;
    const QUrl url(*id);
    const QString host = url.host().toLower();
    const auto parts = url.path().split('/', Qt::SkipEmptyParts);
    if ((url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http")) &&
        (host == QStringLiteral("modrinth.com") || host == QStringLiteral("www.modrinth.com")) &&
        parts.size() >= 2 && parts.first() == QStringLiteral("mod")) {
        *platform = QStringLiteral("modrinth");
        *id = parts.at(1);
        return true;
    }
    // CurseForge's public slug is not a stable numeric project ID. Never guess it.
    return false;
}
}

PresetModEditor::PresetModEditor(const QList<Entry> &entries, QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("编辑模组预设"));
    resize(760, 440);
    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("粘贴 Modrinth 模组链接即可添加，也可填写项目 ID。游戏版本和加载器留空表示跟随实例；勾选“必要”的模组失败会阻止预设安装。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *quickRow = new QHBoxLayout;
    auto *link = new QLineEdit(this);
    link->setObjectName(QStringLiteral("presetModQuickAdd"));
    link->setPlaceholderText(tr("Modrinth 链接或项目 ID，例如 https://modrinth.com/mod/sodium"));
    link->setClearButtonEnabled(true);
    auto *quickAdd = new QPushButton(tr("添加"), this);
    quickAdd->setObjectName(QStringLiteral("presetModQuickAddButton"));
    quickAdd->setAutoDefault(false);
    Theme::markButton(quickAdd, Theme::ButtonKind::Primary);
    quickRow->addWidget(link, 1); quickRow->addWidget(quickAdd);
    layout->addLayout(quickRow);
    m_table = new QTableWidget(0, 6, this);
    m_table->setObjectName(QStringLiteral("presetModEditorTable"));
    m_table->setHorizontalHeaderLabels({tr("平台"), tr("项目 ID"), tr("显示名称"), tr("必要"), tr("限定游戏版本"), tr("限定加载器")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(false);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);
    for (const auto &entry : entries) append(entry);
    auto *actions = new QHBoxLayout;
    auto *add = new QPushButton(tr("添加模组"), this);
    auto *remove = new QPushButton(tr("移除选中"), this);
    add->setAutoDefault(false); remove->setAutoDefault(false);
    Theme::markButton(add, Theme::ButtonKind::Neutral);
    Theme::markButton(remove, Theme::ButtonKind::Neutral);
    actions->addWidget(add); actions->addWidget(remove); actions->addStretch(); layout->addLayout(actions);
    connect(add, &QPushButton::clicked, this, [this] { append({QStringLiteral("modrinth"), {}, {}, {}, {}, true}); });
    connect(remove, &QPushButton::clicked, this, [this] {
        for (int row = m_table->rowCount() - 1; row >= 0; --row)
            if (m_table->selectionModel()->isRowSelected(row, QModelIndex())) m_table->removeRow(row);
    });
    m_problem = new QLabel(this); m_problem->setObjectName(QStringLiteral("presetModProblem")); m_problem->setWordWrap(true); layout->addWidget(m_problem);
    const auto addLink = [this, link] {
        QString platform = QStringLiteral("modrinth"), id = link->text().trimmed();
        if (!normalizeProject(&platform, &id) ||
            !QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]+$")).match(id).hasMatch()) {
            m_problem->setText(tr("请输入 Modrinth 模组链接或项目 ID。CurseForge 请在表格中选择平台并填写数字项目 ID，不能用网页名称代替。"));
            link->setFocus(); return;
        }
        for (int row = 0; row < m_table->rowCount(); ++row) {
            const auto *source = qobject_cast<QComboBox *>(m_table->cellWidget(row, 0));
            if (source->currentData().toString() == platform && m_table->item(row, 1)->text().trimmed() == id) {
                m_table->selectRow(row); m_table->scrollToItem(m_table->item(row, 1));
                m_problem->setText(tr("这个模组已经在清单中，已定位到该行。")); return;
            }
        }
        append({platform, id, id, {}, {}, true});
        m_table->selectRow(m_table->rowCount() - 1);
        m_table->scrollToBottom(); link->clear(); link->setFocus(); m_problem->clear();
    };
    connect(quickAdd, &QPushButton::clicked, this, addLink);
    connect(link, &QLineEdit::returnPressed, this, addLink);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    buttons->button(QDialogButtonBox::Save)->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Cancel)->setAutoDefault(false);
    Theme::markButton(buttons->button(QDialogButtonBox::Save), Theme::ButtonKind::Primary);
    Theme::markButton(buttons->button(QDialogButtonBox::Cancel), Theme::ButtonKind::Neutral);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QSet<QString> seen;
        int row = 0;
        for (const auto &entry : this->entries()) {
            const QString key = entry.platform + QLatin1Char(':') + entry.projectId;
            const auto pattern = entry.platform == QStringLiteral("curseforge") ? QStringLiteral("^[0-9]+$") : QStringLiteral("^[A-Za-z0-9_-]+$");
            if ((entry.platform != QStringLiteral("modrinth") && entry.platform != QStringLiteral("curseforge")) ||
                !QRegularExpression(pattern).match(entry.projectId).hasMatch() || seen.contains(key)) {
                m_problem->setText(tr("第 %1 行：项目 ID 为空、格式错误或重复。Modrinth 支持模组链接；CurseForge 必须填写数字项目 ID。").arg(row + 1));
                m_table->setCurrentCell(row, 1); m_table->scrollToItem(m_table->item(row, 1)); return;
            }
            seen.insert(key);
            ++row;
        }
        accept();
    });
}
void PresetModEditor::append(const Entry &entry) {
    const int row = m_table->rowCount(); m_table->insertRow(row);
    auto *platform = new QComboBox(m_table);
    platform->addItem(QStringLiteral("Modrinth"), QStringLiteral("modrinth"));
    platform->addItem(QStringLiteral("CurseForge"), QStringLiteral("curseforge"));
    int index = platform->findData(entry.platform);
    if (index < 0) { platform->addItem(tr("未知来源（请选择）"), entry.platform); index = platform->count() - 1; }
    platform->setCurrentIndex(index);
    m_table->setCellWidget(row, 0, platform);
    for (const auto &pair : {qMakePair(1, entry.projectId), qMakePair(2, entry.name), qMakePair(4, entry.gameVersion), qMakePair(5, entry.loader)})
        m_table->setItem(row, pair.first, new QTableWidgetItem(pair.second));
    auto *required = new QTableWidgetItem;
    required->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    required->setCheckState(entry.required ? Qt::Checked : Qt::Unchecked);
    m_table->setItem(row, 3, required);
}
QList<PresetModEditor::Entry> PresetModEditor::entries() const {
    QList<Entry> result;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto text = [&](int column) { return m_table->item(row, column)->text().trimmed(); };
        auto *platform = qobject_cast<QComboBox *>(m_table->cellWidget(row, 0));
        QString source = platform->currentData().toString(), id = text(1);
        normalizeProject(&source, &id);
        result.append({source, id, text(2), text(4), text(5).toLower(), m_table->item(row, 3)->checkState() == Qt::Checked});
    }
    return result;
}
