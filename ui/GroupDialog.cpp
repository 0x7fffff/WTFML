/**
 * @file GroupDialog.cpp
 * @brief Implements the instance-group editor dialog.
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 布局：左边管组清单（列表 + 新建/重命名/删除），右边挑这个实例归哪个组。
 *       为什么左右并排而不是上下：右边那排单选钮的条数等于组数，用户建上十几个组
 *       就是十几行；竖着堆会把下面那两个按钮挤出屏幕。
 */

#include "GroupDialog.hpp"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QUuid>
#include <QVBoxLayout>

namespace {

/// 组名的长度上限。比实例目录名的 64 短，因为组名只出现在界面上（不进路径）、
/// 而且它和实例数一起挤在同一行里。超过这个长度的名字只会被省略号截掉，
/// 挡下来是为了让用户当场知道"名字太长了"，而不是过一会儿才发现看不全。
constexpr int kMaxGroupNameLength = 32;

/// 一个新的组 id。
///
/// NOTE: 和 InstanceGroupStore::newGroupId() 是同一件事，这里没有复用而是各写一份
///       —— ui/ 不依赖 src/（见 .hpp 文件头），为了一个 QUuid 的调用把业务层拖进来
///       不划算。两份必须**同格式**（WithoutBraces 的 UUID），因为这里的 id 会被
///       原样写进那个文件。
QString newGroupId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

}  // namespace

GroupDialog::GroupDialog(const QList<Group> &groups, const QHash<QString, int> &memberCounts,
                         const QString &currentGroupId, QWidget *parent)
    : QDialog(parent), m_groups(groups), m_memberCounts(memberCounts) {
    setWindowTitle(tr("分组"));
    resize(560, 420);

    auto *mainLayout = new QVBoxLayout(this);

    auto *hint = new QLabel(
        tr("一个实例只能属于一个组。组的归属跟着实例走 —— 给实例改名不会把它抖出组，"
           "删掉组也不会删掉里面的实例。"),
        this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);

    auto *columns = new QHBoxLayout;
    mainLayout->addLayout(columns, 1);

    {
        auto *groupBox = new QGroupBox(tr("分组"), this);
        auto *layout = new QVBoxLayout(groupBox);

        m_groupList = new QListWidget(groupBox);
        m_groupList->setObjectName(groupListName());
        layout->addWidget(m_groupList, 1);

        auto *buttons = new QHBoxLayout;
        m_addButton = new QPushButton(tr("新建…"), groupBox);
        m_addButton->setObjectName(addGroupButtonName());
        m_renameButton = new QPushButton(tr("重命名…"), groupBox);
        m_renameButton->setObjectName(renameGroupButtonName());
        m_removeButton = new QPushButton(tr("删除"), groupBox);
        m_removeButton->setObjectName(removeGroupButtonName());
        buttons->addWidget(m_addButton);
        buttons->addWidget(m_renameButton);
        buttons->addWidget(m_removeButton);
        layout->addLayout(buttons);

        columns->addWidget(groupBox, 1);

        // 「新建」永远可点（一个组都没有的时候它是唯一的出路）；另外两个要有选中项
        // 才有意义，可用性由 refreshActionButtons() 统一管。
        connect(m_addButton, &QPushButton::clicked, this, [this] {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("新建分组"), tr("组名："),
                                                      QLineEdit::Normal, QString(), &ok);
            if (ok) {
                createGroup(name);
            }
        });
        connect(m_renameButton, &QPushButton::clicked, this, [this] {
            const QString groupId = selectedGroupId();
            if (groupId.isEmpty()) {
                return;
            }
            // 初始文本是现在的名字 —— 用户多半只是想改一两个字。
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("重命名分组"), tr("组名："),
                                                       QLineEdit::Normal, groupName(groupId), &ok);
            if (ok) {
                renameGroup(groupId, name);
            }
        });
        connect(m_removeButton, &QPushButton::clicked, this, [this] {
            const QString groupId = selectedGroupId();
            if (groupId.isEmpty()) {
                return;
            }
            // 先问再删，而且说清**实例不会被删**：用户看到"删除"两个字时的第一反应
            // 就是怕连里面的东西一起没了，而这里确实不会。
            const QMessageBox::StandardButton answer = QMessageBox::question(
                this, tr("删除分组"),
                tr("「%1」会被删除，组里的 %2 个实例不会被删掉，它们只是变成未分组。\n\n"
                   "确定要删除吗？")
                    .arg(groupName(groupId))
                    .arg(m_memberCounts.value(groupId)),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer == QMessageBox::Yes) {
                deleteGroup(groupId);
            }
        });
        connect(m_groupList, &QListWidget::currentRowChanged, this,
                [this] { refreshActionButtons(); });
        // 双击一行 = 重命名它（列表上的常规手势，省一次"先选中再点按钮"）。
        connect(m_groupList, &QListWidget::itemDoubleClicked, this, [this] {
            if (m_renameButton->isEnabled()) {
                m_renameButton->click();
            }
        });
    }
    {
        auto *box = new QGroupBox(tr("这个实例归到"), this);
        box->setObjectName(assignmentBoxName());
        auto *boxLayout = new QVBoxLayout(box);

        // 单选钮放在**自己的容器**里，下面那个 addStretch 才不会被重画弄成好几个：
        // 重画只清空这个容器，容器外面的弹簧从头到尾只有一个。
        m_assignmentBox = box;
        m_choices = new QWidget(box);
        m_choiceLayout = new QVBoxLayout(m_choices);
        m_choiceLayout->setContentsMargins(0, 0, 0, 0);
        boxLayout->addWidget(m_choices);
        boxLayout->addStretch(1);

        m_assignmentGroup = new QButtonGroup(this);
        m_assignmentGroup->setExclusive(true);
        columns->addWidget(box, 1);
    }

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancelButton = new QPushButton(tr("取消"), this);
    cancelButton->setObjectName(cancelButtonName());
    auto *confirmButton = new QPushButton(tr("确定"), this);
    confirmButton->setObjectName(confirmButtonName());
    confirmButton->setDefault(true);
    buttons->addWidget(cancelButton);
    buttons->addWidget(confirmButton);
    mainLayout->addLayout(buttons);

    // 「确定」接 accept()、「取消」接 reject()。QDialog::exec() 的返回值区分得开，
    // 调用方只在 Accepted 时才去看 groups()/assignedGroupId()。
    connect(confirmButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    // 进参的归属可能指向一个已经不存在的组（那种残留是正常的）。当未分组处理，
    // 免得下面那排单选钮一条都选不中，用户看着像"没得选"。
    m_assignedGroupId = currentGroupId;
    if (!m_assignedGroupId.isEmpty() && rowOfGroup(m_assignedGroupId) < 0) {
        m_assignedGroupId.clear();
    }

    rebuildGroupList();
    rebuildAssignment();
}

QWidget *GroupDialog::assignmentButtonAt(int index) const {
    return m_assignmentGroup != nullptr ? m_assignmentGroup->button(index) : nullptr;
}

QString GroupDialog::selectedGroupId() const {
    if (m_groupList == nullptr) {
        return QString();
    }
    const QListWidgetItem *item = m_groupList->currentItem();
    if (item == nullptr) {
        return QString();
    }
    // 组 id 存在 UserRole 里（和实例列表同一个约定：显示文本会变，不是身份标识）。
    return item->data(Qt::UserRole).toString();
}

QString GroupDialog::groupName(const QString &groupId) const {
    for (const Group &group : m_groups) {
        if (group.id == groupId) {
            return group.name;
        }
    }
    return QString();
}

int GroupDialog::rowOfGroup(const QString &groupId) const {
    if (groupId.isEmpty()) {
        return -1;
    }
    for (int i = 0; i < m_groups.size(); ++i) {
        if (m_groups.at(i).id == groupId) {
            return i;
        }
    }
    return -1;
}

QString GroupDialog::validateGroupName(const QString &rawName, const QString &exceptGroupId) const {
    const QString name = rawName.trimmed();
    if (name.isEmpty()) {
        return tr("组名不能是空的。");
    }
    if (name.size() > kMaxGroupNameLength) {
        return tr("组名最多 %1 个字，现在有 %2 个。").arg(kMaxGroupNameLength).arg(name.size());
    }
    for (const Group &group : m_groups) {
        if (group.id == exceptGroupId) {
            continue;
        }
        // 不区分大小写：界面上「Forge 整合包」和「forge 整合包」看着就是同一个，
        // 放过去的结果是用户在同名两组之间搬来搬去，永远搞不清自己在哪个组里。
        if (group.name.compare(name, Qt::CaseInsensitive) == 0) {
            return tr("已经有一个叫「%1」的组了。").arg(group.name);
        }
    }
    return QString();
}

bool GroupDialog::createGroup(const QString &name) {
    const QString problem = validateGroupName(name, QString());
    if (!problem.isEmpty()) {
        QMessageBox::warning(this, tr("新建分组失败"), problem);
        return false;
    }

    Group group;
    group.id = newGroupId();
    group.name = name.trimmed();
    m_groups.append(group);

    rebuildGroupList();
    rebuildAssignment();
    // 新建完就选中它：用户刚建完一个组，下一步大概率是把这个实例放进去。
    m_groupList->setCurrentRow(m_groups.size() - 1);
    return true;
}

bool GroupDialog::renameGroup(const QString &groupId, const QString &name) {
    const int row = rowOfGroup(groupId);
    if (row < 0) {
        // 找不到这个组（比如它刚被删掉了）。不弹窗：这不是用户此刻做的操作，
        // 弹一个他看不懂的框只会让人以为坏了。
        return false;
    }
    const QString problem = validateGroupName(name, groupId);
    if (!problem.isEmpty()) {
        QMessageBox::warning(this, tr("重命名分组失败"), problem);
        return false;
    }

    m_groups[row].name = name.trimmed();
    // 归属挂的是 id 不是名字，所以这里**不动** m_assignedGroupId —— 给组改个名
    // 不该把正在编辑的这个实例抖出去。
    rebuildGroupList();
    rebuildAssignment();
    return true;
}

bool GroupDialog::deleteGroup(const QString &groupId) {
    const int row = rowOfGroup(groupId);
    if (row < 0) {
        return false;
    }

    m_groups.removeAt(row);
    m_memberCounts.remove(groupId);
    if (m_assignedGroupId == groupId) {
        // 正在编辑的这个实例本来在这个组里 —— 组没了，它跟着变成未分组。
        // 这是**界面上**的归属变了，不是删实例（谁都没删）。
        m_assignedGroupId.clear();
    }

    rebuildGroupList();
    rebuildAssignment();
    return true;
}

void GroupDialog::setAssignedGroupId(const QString &groupId) {
    if (m_assignedGroupId == groupId) {
        return;
    }
    // 工作副本里的实例数跟着走：这个实例从老组挪到新组（空串那一头是"未分组"，
    // 它不是组，不进计数）。
    if (!m_assignedGroupId.isEmpty()) {
        m_memberCounts[m_assignedGroupId] = qMax(0, m_memberCounts.value(m_assignedGroupId) - 1);
    }
    m_assignedGroupId = groupId;
    if (!groupId.isEmpty()) {
        m_memberCounts[groupId] = m_memberCounts.value(groupId) + 1;
    }

    // 把单选钮的选中状态摆正。**只摆状态、不重建那排控件** —— 这段代码可能是从
    // 某个单选钮自己的 toggled 里进来的，这时候把发出信号的控件删掉是未定义行为。
    // m_updatingAssignment 挡的是"摆状态又触发一遍 toggled"的回环。
    if (QAbstractButton *button = m_assignmentGroup->button(m_assignmentIds.indexOf(groupId))) {
        m_updatingAssignment = true;
        button->setChecked(true);
        m_updatingAssignment = false;
    }
    refreshCounts();
}

void GroupDialog::rebuildGroupList() {
    // 重建之后把选中项停在原来那个组上（按 id 找回）：改个名、删一个别的组之后
    // 选中项跳到第一行，用户就得重新找一遍。
    const QString previous = selectedGroupId();

    {
        const QSignalBlocker blocker(m_groupList);
        m_groupList->clear();
        for (const Group &group : m_groups) {
            auto *item = new QListWidgetItem(
                groupItemText(group.name, m_memberCounts.value(group.id)), m_groupList);
            item->setData(Qt::UserRole, group.id);
        }
        const int row = rowOfGroup(previous);
        if (row >= 0) {
            m_groupList->setCurrentRow(row);
        }
    }
    // 选中项是在挡着信号的时候摆的，那两条 currentRowChanged 收不到，
    // 两个按钮的可用性得在这里收尾。
    refreshActionButtons();
}

void GroupDialog::refreshCounts() {
    // 只改文字，不重建列表（重建会把选中项和滚动位置都抖一下）。
    const QSignalBlocker blocker(m_groupList);
    for (int row = 0; row < m_groupList->count() && row < m_groups.size(); ++row) {
        const Group &group = m_groups.at(row);
        m_groupList->item(row)->setText(groupItemText(group.name, m_memberCounts.value(group.id)));
    }
}

void GroupDialog::rebuildAssignment() {
    // 整排重画（理由见 .hpp）：先清空旧的，再按"未分组 + 每个组"各来一条。
    //
    // NOTE: 这里是**立即 delete** 而不是 deleteLater()。走到这一段的调用链是
    //       "某个按钮被点了" 或 "构造函数"，没有一条是从这排单选钮自己的信号里进来的
    //       （那条路只摆状态、不重建，见 setAssignedGroupId），所以现在删它们是安全的；
    //       用 deleteLater() 反而会让旧按钮多活一个事件循环、在新的那排上面画一帧。
    const QList<QAbstractButton *> old = m_assignmentGroup->buttons();
    for (QAbstractButton *button : old) {
        m_assignmentGroup->removeButton(button);
        delete button;
    }
    m_assignmentIds.clear();

    const auto addChoice = [this](const QString &text, const QString &groupId) {
        auto *button = new QRadioButton(text, m_choices);
        // objectName 带下标（"assignmentChoice0/1/…"），验证工程按它取控件 ——
        // 单选钮的文字就是组名，而组名是用户起的、随时会变，拿它当身份标识必错。
        button->setObjectName(QStringLiteral("assignmentChoice%1").arg(m_assignmentIds.size()));
        m_assignmentIds.append(groupId);
        m_assignmentGroup->addButton(button, m_assignmentIds.size() - 1);
        m_choiceLayout->addWidget(button);
        button->setChecked(groupId == m_assignedGroupId);
        connect(button, &QRadioButton::toggled, this, [this, groupId](bool checked) {
            // m_updatingAssignment：这是 setAssignedGroupId() 在摆状态，不是用户点的。
            if (checked && !m_updatingAssignment) {
                setAssignedGroupId(groupId);
            }
        });
    };

    m_updatingAssignment = true;
    addChoice(ungroupedText(), QString());
    for (const Group &group : m_groups) {
        addChoice(group.name, group.id);
    }
    m_updatingAssignment = false;
}

void GroupDialog::refreshActionButtons() {
    const bool hasSelection = !selectedGroupId().isEmpty();
    m_renameButton->setEnabled(hasSelection);
    m_removeButton->setEnabled(hasSelection);
}
