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

#include "PresetOptionCatalog.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QDoubleSpinBox>
#include <QApplication>
#include <functional>

namespace {
using namespace PresetOptionCatalog;
QString keyLabel(QString value) {
    const QHash<QString, QString> names{{"key.keyboard.space", "空格"}, {"key.keyboard.left.shift", "左 Shift"},
        {"key.keyboard.left.control", "左 Ctrl"}, {"key.keyboard.left.alt", "左 Alt"},
        {"key.keyboard.tab", "Tab"}, {"key.mouse.left", "鼠标左键"}, {"key.mouse.right", "鼠标右键"},
        {"key.mouse.middle", "鼠标中键"}, {"key.keyboard.unknown", "未绑定"}};
    if (names.contains(value)) return names.value(value);
    if (value.startsWith("key.keyboard.")) return value.mid(13).toUpper();
    return value;
}
class KeyCapture : public QPushButton {
public:
    KeyCapture(QWidget *parent) : QPushButton(parent) {
        setAutoDefault(false);
        Theme::markButton(this, Theme::ButtonKind::Neutral);
        setToolTip(tr("点击后按下键盘或鼠标按键；Esc 取消，退格清除"));
        connect(this, &QPushButton::clicked, this, [this] {
            armed = true; setText(tr("请按键…")); setFocus();
        });
    }
    void setValue(const QString &value) { current = value; setText(keyLabel(value)); }
    std::function<void(const QString &)> changed;
protected:
    void focusOutEvent(QFocusEvent *event) override {
        armed = false; setText(keyLabel(current)); QPushButton::focusOutEvent(event);
    }
    bool event(QEvent *event) override {
        if (armed && event->type() == QEvent::ShortcutOverride) { event->accept(); return true; }
        if (armed && event->type() == QEvent::KeyPress) {
            keyPressEvent(static_cast<QKeyEvent *>(event)); return true;
        }
        return QPushButton::event(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (!armed) { QPushButton::keyPressEvent(event); return; }
        if (event->isAutoRepeat()) return;
        if (event->key() == Qt::Key_Escape) { armed = false; setText(keyLabel(current)); return; }
        QString suffix;
        const int key = event->key();
        if (key >= Qt::Key_A && key <= Qt::Key_Z) suffix = QChar('a' + key - Qt::Key_A);
        else if (key >= Qt::Key_0 && key <= Qt::Key_9) suffix = QChar('0' + key - Qt::Key_0);
        else if (key >= Qt::Key_F1 && key <= Qt::Key_F25) suffix = "f" + QString::number(key - Qt::Key_F1 + 1);
        else {
            const QHash<int, QString> names{{Qt::Key_Space,"space"},{Qt::Key_Shift,"left.shift"},
                {Qt::Key_Control,"left.control"},{Qt::Key_Alt,"left.alt"},{Qt::Key_Meta,"left.win"},
                {Qt::Key_Tab,"tab"},{Qt::Key_Return,"enter"},{Qt::Key_Enter,"keypad.enter"},
                {Qt::Key_Up,"up"},{Qt::Key_Down,"down"},{Qt::Key_Left,"left"},{Qt::Key_Right,"right"},
                {Qt::Key_Insert,"insert"},{Qt::Key_Delete,"delete"},{Qt::Key_Home,"home"},{Qt::Key_End,"end"},
                {Qt::Key_PageUp,"page.up"},{Qt::Key_PageDown,"page.down"},{Qt::Key_Minus,"minus"},
                {Qt::Key_Equal,"equal"},{Qt::Key_BracketLeft,"left.bracket"},{Qt::Key_BracketRight,"right.bracket"},
                {Qt::Key_Semicolon,"semicolon"},{Qt::Key_Apostrophe,"apostrophe"},{Qt::Key_Comma,"comma"},
                {Qt::Key_Period,"period"},{Qt::Key_Slash,"slash"},{Qt::Key_Backslash,"backslash"},
                {Qt::Key_QuoteLeft,"grave.accent"},{Qt::Key_CapsLock,"caps.lock"}};
            suffix = names.value(key);
        }
        // Qt 合并了左右修饰键，Windows 的虚拟键仍可用于区分。
        if (event->nativeVirtualKey() == 0xA1) suffix = "right.shift";
        if (event->nativeVirtualKey() == 0xA3) suffix = "right.control";
        if (event->nativeVirtualKey() == 0xA5) suffix = "right.alt";
        if ((event->modifiers() & Qt::KeypadModifier) && key >= Qt::Key_0 && key <= Qt::Key_9)
            suffix = "keypad." + suffix;
        if (key == Qt::Key_Backspace) commit("key.keyboard.unknown");
        else if (!suffix.isEmpty()) commit("key.keyboard." + suffix);
        event->accept();
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (!armed) { QPushButton::mousePressEvent(event); return; }
        const QHash<Qt::MouseButton, QString> names{{Qt::LeftButton,"left"}, {Qt::RightButton,"right"},
            {Qt::MiddleButton,"middle"}, {Qt::BackButton,"4"}, {Qt::ForwardButton,"5"}};
        if (names.contains(event->button())) { commit("key.mouse." + names.value(event->button())); suppressRelease = true; }
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (suppressRelease) { suppressRelease = false; event->accept(); return; }
        QPushButton::mouseReleaseEvent(event);
    }
private:
    void commit(const QString &value) {
        armed = false; setValue(value);
        if (changed) changed(value);
    }
    QString current;
    bool armed = false, suppressRelease = false;
};
class OptionValueDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const auto *spec = PresetOptionCatalog::find(index.siblingAtColumn(0).data().toString());
        if (index.column() == 1 && spec) {
            if (spec->kind == Kind::Key) return new KeyCapture(parent);
            if (spec->kind == Kind::Boolean || spec->kind == Kind::Choice) {
                auto *combo = new QComboBox(parent);
                if (spec->kind == Kind::Boolean) { combo->addItem(tr("关闭"), "false"); combo->addItem(tr("开启"), "true"); }
                else for (const auto &choice : spec->choices) {
                    const int separator = choice.indexOf('|');
                    combo->addItem(choice.mid(separator + 1), choice.left(separator));
                }
                return combo;
            }
            if (spec->kind == Kind::Fraction) {
                auto *spin = new QDoubleSpinBox(parent);
                spin->setRange(spec->minimum * 100, spec->maximum * 100);
                spin->setDecimals(1); spin->setSuffix("%"); spin->setKeyboardTracking(false);
                return spin;
            }
            auto *spin = new QSpinBox(parent);
            spin->setRange(int(spec->minimum), int(spec->maximum));
            spin->setKeyboardTracking(false);
            if (spec->key == "fov") spin->setSuffix("°");
            if (spec->key == "guiScale") spin->setSpecialValueText(tr("自动"));
            if (spec->key.endsWith("Distance")) spin->setSuffix(tr(" 区块"));
            if (spec->key == "maxFps") { spin->setSuffix(" FPS"); }
            return spin;
        }
        return QStyledItemDelegate::createEditor(parent, option, index);
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        const QSignalBlocker blocker(editor);
        if (auto *key = dynamic_cast<KeyCapture *>(editor)) { key->setValue(index.data().toString()); return; }
        if (auto *spin = qobject_cast<QDoubleSpinBox *>(editor)) { spin->setValue(index.data().toDouble() * 100); return; }
        if (auto *spin = qobject_cast<QSpinBox *>(editor)) {
            spin->setValue(index.siblingAtColumn(0).data().toString() == "fov"
                ? qRound(70 + 40 * index.data().toDouble()) : index.data().toInt()); return;
        }
        if (auto *combo = qobject_cast<QComboBox *>(editor)) {
            int selected = combo->findData(index.data().toString());
            if (selected < 0) { combo->addItem(index.data().toString(), index.data().toString()); selected = combo->count()-1; }
            combo->setCurrentIndex(selected); return;
        }
        QStyledItemDelegate::setEditorData(editor, index);
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        if (auto *spin = qobject_cast<QDoubleSpinBox *>(editor)) {
            spin->interpretText(); model->setData(index, QString::number(spin->value()/100.0, 'g', 8)); return;
        }
        if (auto *spin = qobject_cast<QSpinBox *>(editor)) {
            spin->interpretText();
            model->setData(index, index.siblingAtColumn(0).data().toString() == "fov"
                ? QString::number((spin->value()-70)/40.0, 'g', 8) : QString::number(spin->value())); return;
        }
        if (auto *combo = qobject_cast<QComboBox *>(editor)) { model->setData(index, combo->currentData().toString()); return; }
        if (dynamic_cast<KeyCapture *>(editor)) return;
        QStyledItemDelegate::setModelData(editor, model, index);
    }
protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        if (index.column() == 0) if (const auto *spec = PresetOptionCatalog::find(index.data().toString())) option->text = spec->name;
    }
};
}

PresetOptionsEditor::PresetOptionsEditor(const QList<Entry> &entries, QWidget *parent, Mode mode) : QDialog(parent) {
    setWindowTitle(tr("编辑游戏设置预设")); resize(600, 440);
    auto *layout = new QVBoxLayout(this);
    auto *search = new QLineEdit(this);
    search->setObjectName("presetOptionsSearch");
    search->setPlaceholderText(mode == Mode::Keys ? tr("搜索操作或按键") : tr("搜索设置"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);
    m_table = new QTableWidget(0, 2, this);
    m_table->setObjectName(QStringLiteral("presetOptionsEditorTable"));
    m_table->setHorizontalHeaderLabels({tr("纳入预设"), mode == Mode::Keys ? tr("按键") : tr("设置值")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(48);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setItemDelegate(new OptionValueDelegate(m_table));
    layout->addWidget(m_table, 1);
    QSet<QString> present;
    for (const auto &entry : entries) { append(entry); present.insert(entry.key); }
    for (const auto &spec : PresetOptionCatalog::all()) {
        if (present.contains(spec.key) || (mode == Mode::Keys && spec.kind != Kind::Key) ||
            (mode == Mode::Options && spec.kind == Kind::Key)) continue;
        append({spec.key, spec.initial}, false);
    }
    connect(search, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int row=0; row<m_table->rowCount(); ++row) {
            const auto *item = m_table->item(row,0);
            const auto *spec = PresetOptionCatalog::find(item->text());
            const QString haystack = item->text() + " " + (spec ? spec->name + " " + spec->group : QString())
                + " " + m_table->item(row,1)->text();
            m_table->setRowHidden(row, !haystack.contains(text.trimmed(), Qt::CaseInsensitive));
        }
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column()!=0) return;
        if (auto *editor=m_table->cellWidget(item->row(),1)) editor->setEnabled(item->checkState()==Qt::Checked);
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
void PresetOptionsEditor::append(const Entry &entry, bool included) {
    using namespace PresetOptionCatalog;
    const int row = m_table->rowCount(); m_table->insertRow(row);
    auto *key = new QTableWidgetItem(entry.key);
    key->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    key->setCheckState(included ? Qt::Checked : Qt::Unchecked);
    const auto *spec = PresetOptionCatalog::find(entry.key);
    key->setToolTip(spec ? spec->group + " / " + entry.key : entry.key);
    auto *value = new QTableWidgetItem(entry.value);
    m_table->setItem(row, 0, key);
    m_table->setItem(row, 1, value);
    // 未识别或超出控件范围的导入值仍按原文保留，不能静默截断。
    bool valid = true;
    const double number = entry.value.toDouble(&valid);
    const double display = entry.key == "fov" ? 70 + 40 * number : number;
    bool supported = spec != nullptr;
    if (spec && (spec->kind == Kind::Integer || spec->kind == Kind::Fraction))
        supported = valid && display >= spec->minimum && display <= spec->maximum;
    QWidget *editor = supported ? m_table->itemDelegate()->createEditor(m_table, {}, m_table->model()->index(row,1))
                                : new QLineEdit(m_table);
    if (supported) m_table->itemDelegate()->setEditorData(editor, m_table->model()->index(row,1));
    else qobject_cast<QLineEdit *>(editor)->setText(entry.value);
    editor->setObjectName("presetValue_" + entry.key);
    editor->setEnabled(included);
    m_table->setCellWidget(row,1,editor);
    const auto commit = [this, editor, value, supported] {
        if (supported) m_table->itemDelegate()->setModelData(editor,m_table->model(),m_table->indexFromItem(value));
        else value->setText(qobject_cast<QLineEdit *>(editor)->text());
    };
    if (auto *capture = dynamic_cast<KeyCapture *>(editor))
        capture->changed = [value](const QString &binding) { value->setText(binding); };
    else if (auto *spin = qobject_cast<QSpinBox *>(editor)) connect(spin,qOverload<int>(&QSpinBox::valueChanged),this,commit);
    else if (auto *spin = qobject_cast<QDoubleSpinBox *>(editor)) connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,commit);
    else if (auto *combo = qobject_cast<QComboBox *>(editor)) connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),this,commit);
    else if (auto *line = qobject_cast<QLineEdit *>(editor)) connect(line,&QLineEdit::textChanged,this,commit);
}
QList<PresetOptionsEditor::Entry> PresetOptionsEditor::entries() const {
    QList<Entry> result;
    for (int row = 0; row < m_table->rowCount(); ++row)
        if (m_table->item(row,0)->checkState() == Qt::Checked) {
            // 导航离开检查可能先于失焦，保存前提交仍在输入中的数字。
            if (auto *spin = qobject_cast<QAbstractSpinBox *>(m_table->cellWidget(row,1))) spin->interpretText();
            result.append({m_table->item(row, 0)->text().trimmed(), m_table->item(row, 1)->text()});
        }
    return result;
}
