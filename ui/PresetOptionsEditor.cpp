#include "PresetOptionsEditor.hpp"
#include "Theme.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QSet>

PresetOptionsEditor::PresetOptionsEditor(const QList<Entry> &entries, QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("编辑游戏设置预设")); resize(600, 440);
    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("只保存列表里的设置与按键，不覆盖整份 options.txt。可从常用项添加，也可修改设置键和值；目标版本不认识的键由应用计划提示。其它模组配置段保持不变。"), this);
    hint->setWordWrap(true); layout->addWidget(hint);
    m_table = new QTableWidget(0, 2, this);
    m_table->setObjectName(QStringLiteral("presetOptionsEditorTable"));
    m_table->setHorizontalHeaderLabels({tr("设置键 / key_ 按键"), tr("值")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_table, 1);
    for (const auto &entry : entries) append(entry);
    auto *actions = new QHBoxLayout;
    auto *common = new QComboBox(this);
    common->addItem(tr("视角（0.0 = 70°）"), QStringList{QStringLiteral("fov"), QStringLiteral("0.0")});
    common->addItem(tr("界面尺寸（0 = 自动）"), QStringList{QStringLiteral("guiScale"), QStringLiteral("0")});
    common->addItem(tr("关闭自动跳跃"), QStringList{QStringLiteral("autoJump"), QStringLiteral("false")});
    common->addItem(tr("渲染距离"), QStringList{QStringLiteral("renderDistance"), QStringLiteral("12")});
    common->addItem(tr("跳跃按键"), QStringList{QStringLiteral("key_key.jump"), QStringLiteral("key.keyboard.space")});
    auto *add = new QPushButton(tr("添加"), this);
    auto *remove = new QPushButton(tr("移除选中"), this);
    Theme::markButton(add, Theme::ButtonKind::Neutral); Theme::markButton(remove, Theme::ButtonKind::Neutral);
    actions->addWidget(common, 1); actions->addWidget(add); actions->addWidget(remove); layout->addLayout(actions);
    connect(add, &QPushButton::clicked, this, [this, common] {
        const auto values = common->currentData().toStringList();
        for (const auto &entry : this->entries()) if (entry.key == values[0]) return;
        append({values[0], values[1]});
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        for (int row = m_table->rowCount() - 1; row >= 0; --row)
            if (m_table->selectionModel()->isRowSelected(row, QModelIndex())) m_table->removeRow(row);
    });
    auto *problem = new QLabel(this); problem->setWordWrap(true); layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    Theme::markButton(buttons->button(QDialogButtonBox::Save), Theme::ButtonKind::Primary);
    Theme::markButton(buttons->button(QDialogButtonBox::Cancel), Theme::ButtonKind::Neutral);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, problem] {
        QSet<QString> keys;
        for (const auto &entry : this->entries()) {
            if (entry.key.isEmpty() || entry.key.contains(QLatin1Char(':')) || entry.key.contains(QLatin1Char('\n')) ||
                entry.key.contains(QLatin1Char('\r')) || entry.value.contains(QLatin1Char('\n')) ||
                entry.value.contains(QLatin1Char('\r')) || keys.contains(entry.key)) {
                problem->setText(tr("设置键不能为空或重复，键和值不能含换行，键不能含冒号。")); return;
            }
            keys.insert(entry.key);
        }
        accept();
    });
}
void PresetOptionsEditor::append(const Entry &entry) {
    const int row = m_table->rowCount(); m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(entry.key));
    m_table->setItem(row, 1, new QTableWidgetItem(entry.value));
}
QList<PresetOptionsEditor::Entry> PresetOptionsEditor::entries() const {
    QList<Entry> result;
    for (int row = 0; row < m_table->rowCount(); ++row)
        result.append({m_table->item(row, 0)->text().trimmed(), m_table->item(row, 1)->text()});
    return result;
}
