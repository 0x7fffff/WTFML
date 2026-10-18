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

PresetModEditor::PresetModEditor(const QList<Entry> &entries, QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("编辑模组预设"));
    resize(760, 440);
    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("填写平台项目 ID（不是模组名称或文件名）。版本和加载器留空时跟随目标实例；必要前置会在安装时解析。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    m_table = new QTableWidget(0, 6, this);
    m_table->setObjectName(QStringLiteral("presetModEditorTable"));
    m_table->setHorizontalHeaderLabels({tr("平台"), tr("项目 ID"), tr("显示名称"), tr("必要"), tr("限定游戏版本"), tr("限定加载器")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_table, 1);
    for (const auto &entry : entries) append(entry);
    auto *actions = new QHBoxLayout;
    auto *add = new QPushButton(tr("添加模组"), this);
    auto *remove = new QPushButton(tr("移除选中"), this);
    Theme::markButton(add, Theme::ButtonKind::Neutral);
    Theme::markButton(remove, Theme::ButtonKind::Neutral);
    actions->addWidget(add); actions->addWidget(remove); actions->addStretch(); layout->addLayout(actions);
    connect(add, &QPushButton::clicked, this, [this] { append({QStringLiteral("modrinth"), {}, {}, {}, {}, true}); });
    connect(remove, &QPushButton::clicked, this, [this] {
        for (int row = m_table->rowCount() - 1; row >= 0; --row)
            if (m_table->selectionModel()->isRowSelected(row, QModelIndex())) m_table->removeRow(row);
    });
    m_problem = new QLabel(this); m_problem->setWordWrap(true); layout->addWidget(m_problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    Theme::markButton(buttons->button(QDialogButtonBox::Save), Theme::ButtonKind::Primary);
    Theme::markButton(buttons->button(QDialogButtonBox::Cancel), Theme::ButtonKind::Neutral);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QSet<QString> seen;
        for (const auto &entry : this->entries()) {
            const QString key = entry.platform + QLatin1Char(':') + entry.projectId;
            const auto pattern = entry.platform == QStringLiteral("curseforge") ? QStringLiteral("^[0-9]+$") : QStringLiteral("^[A-Za-z0-9_-]+$");
            if ((entry.platform != QStringLiteral("modrinth") && entry.platform != QStringLiteral("curseforge")) ||
                !QRegularExpression(pattern).match(entry.projectId).hasMatch() || seen.contains(key)) {
                m_problem->setText(tr("项目 ID 不能为空、不能填写网址，也不能重复；CurseForge ID 必须是数字。")); return;
            }
            seen.insert(key);
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
        result.append({platform->currentData().toString(), text(1), text(2), text(4), text(5), m_table->item(row, 3)->checkState() == Qt::Checked});
    }
    return result;
}
