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
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QPainter>

namespace {
class OptionValueDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const QString key = index.siblingAtColumn(0).data().toString();
        if (index.column() == 1) {
            if (key == QStringLiteral("autoJump")) {
                auto *combo = new QComboBox(parent);
                combo->addItem(QObject::tr("关闭"), QStringLiteral("false"));
                combo->addItem(QObject::tr("开启"), QStringLiteral("true"));
                return combo;
            }
            if (key == QStringLiteral("fov") || key == QStringLiteral("guiScale") || key == QStringLiteral("renderDistance")) {
                auto *spin = new QSpinBox(parent);
                if (key == QStringLiteral("fov")) { spin->setRange(30, 110); spin->setSuffix(QStringLiteral("°")); }
                else if (key == QStringLiteral("guiScale")) { spin->setRange(0, 16); spin->setSpecialValueText(QObject::tr("自动")); }
                else { spin->setRange(2, 64); spin->setSuffix(QObject::tr(" 区块")); }
                return spin;
            }
        }
        return QStyledItemDelegate::createEditor(parent, option, index);
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        if (auto *spin = qobject_cast<QSpinBox *>(editor)) {
            const bool fov = index.siblingAtColumn(0).data().toString() == QStringLiteral("fov");
            spin->setValue(fov ? qRound(70 + 40 * index.data().toDouble()) : index.data().toInt()); return;
        }
        if (auto *combo = qobject_cast<QComboBox *>(editor)) { combo->setCurrentIndex(combo->findData(index.data().toString())); return; }
        QStyledItemDelegate::setEditorData(editor, index);
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        if (auto *spin = qobject_cast<QSpinBox *>(editor)) {
            spin->interpretText();
            const bool fov = index.siblingAtColumn(0).data().toString() == QStringLiteral("fov");
            model->setData(index, fov ? QString::number((spin->value() - 70) / 40.0, 'g', 8) : QString::number(spin->value())); return;
        }
        if (auto *combo = qobject_cast<QComboBox *>(editor)) { model->setData(index, combo->currentData().toString()); return; }
        QStyledItemDelegate::setModelData(editor, model, index);
    }
protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        const QString key = index.siblingAtColumn(0).data().toString();
        if (index.column() == 0) {
            const QHash<QString, QString> names{{"fov", QObject::tr("视角")}, {"guiScale", QObject::tr("界面尺寸")},
                {"autoJump", QObject::tr("自动跳跃")}, {"renderDistance", QObject::tr("渲染距离")}, {"key_key.jump", QObject::tr("跳跃按键")}};
            if (names.contains(key)) option->text = names.value(key);
        } else if (key == QStringLiteral("fov")) {
            bool ok = false; const double value = index.data().toDouble(&ok);
            if (ok) option->text = QString::number(70 + 40 * value, 'g', 5) + QStringLiteral("°");
        } else if (key == QStringLiteral("guiScale") && index.data().toString() == QStringLiteral("0")) option->text = QObject::tr("自动");
        else if (key == QStringLiteral("autoJump")) {
            if (index.data().toString() == QStringLiteral("true")) option->text = QObject::tr("开启");
            else if (index.data().toString() == QStringLiteral("false")) option->text = QObject::tr("关闭");
        }
    }
};
}

PresetOptionsEditor::PresetOptionsEditor(const QList<Entry> &entries, QWidget *parent, Mode mode) : QDialog(parent) {
    setWindowTitle(tr("编辑游戏设置预设")); resize(600, 440);
    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("从下方添加常用设置，双击右侧修改数值。只应用清单内的设置。"), this);
    hint->setWordWrap(true); layout->addWidget(hint);
    m_table = new QTableWidget(0, 2, this);
    m_table->setObjectName(QStringLiteral("presetOptionsEditorTable"));
    m_table->setHorizontalHeaderLabels({tr("设置键 / key_ 按键"), tr("值")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setItemDelegate(new OptionValueDelegate(m_table));
    layout->addWidget(m_table, 1);
    for (const auto &entry : entries) append(entry);
    auto *actions = new QHBoxLayout;
    auto *common = new QComboBox(this);
    common->addItem(tr("视角（70°）"), QStringList{QStringLiteral("fov"), QStringLiteral("0.0")});
    common->addItem(tr("界面尺寸（0 = 自动）"), QStringList{QStringLiteral("guiScale"), QStringLiteral("0")});
    common->addItem(tr("关闭自动跳跃"), QStringList{QStringLiteral("autoJump"), QStringLiteral("false")});
    common->addItem(tr("渲染距离"), QStringList{QStringLiteral("renderDistance"), QStringLiteral("12")});
    common->addItem(tr("跳跃按键"), QStringList{QStringLiteral("key_key.jump"), QStringLiteral("key.keyboard.space")});
    for (const auto &key : QStringList{"forward", "back", "left", "right", "inventory", "sneak", "sprint", "attack", "use"}) {
        const QHash<QString,QString> labels{{"forward",tr("前进")},{"back",tr("后退")},{"left",tr("左移")},{"right",tr("右移")},{"inventory",tr("物品栏")},{"sneak",tr("潜行")},{"sprint",tr("疾跑")},{"attack",tr("攻击")},{"use",tr("使用")}};
        const QHash<QString,QString> defaults{{"forward","key.keyboard.w"},{"back","key.keyboard.s"},{"left","key.keyboard.a"},{"right","key.keyboard.d"},{"inventory","key.keyboard.e"},{"sneak","key.keyboard.left.shift"},{"sprint","key.keyboard.left.control"},{"attack","key.mouse.left"},{"use","key.mouse.right"}};
        common->addItem(labels.value(key),QStringList{QStringLiteral("key_key.")+key,defaults.value(key)});
    }
    if (mode!=Mode::All) for(int i=common->count()-1;i>=0;--i) {
        const bool key=common->itemData(i).toStringList().value(0).startsWith("key_");
        if(key!=(mode==Mode::Keys))common->removeItem(i);
    }
    auto *add = new QPushButton(tr("添加"), this);
    auto *remove = new QPushButton(tr("移除选中"), this);
    add->setAutoDefault(false); remove->setAutoDefault(false);
    Theme::markButton(add, Theme::ButtonKind::Neutral); Theme::markButton(remove, Theme::ButtonKind::Neutral);
    actions->addWidget(common, 1); actions->addWidget(add); actions->addWidget(remove); layout->addLayout(actions);
    connect(add, &QPushButton::clicked, this, [this, common] {
        const auto values = common->currentData().toStringList();
        for (int row = 0; row < m_table->rowCount(); ++row) if (m_table->item(row, 0)->text() == values[0]) {
            m_table->setCurrentCell(row, 1); m_table->scrollToItem(m_table->item(row, 1)); m_table->editItem(m_table->item(row, 1)); return;
        }
        append({values[0], values[1]});
        m_table->setCurrentCell(m_table->rowCount() - 1, 1);
        m_table->editItem(m_table->item(m_table->rowCount() - 1, 1));
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        for (int row = m_table->rowCount() - 1; row >= 0; --row)
            if (m_table->selectionModel()->isRowSelected(row, QModelIndex())) m_table->removeRow(row);
    });
    auto *problem = new QLabel(this); problem->setWordWrap(true); layout->addWidget(problem);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    buttons->button(QDialogButtonBox::Save)->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Cancel)->setAutoDefault(false);
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
