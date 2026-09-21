/**
 * @file GroupDialog.hpp
 * @brief 「分组…」对话框：管理组清单（新建/重命名/删除）＋ 挑当前实例归到哪个组。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 进参出参都是**值**，本文件不 include 任何 src/ 下的头 —— ui/ 一直不依赖业务层
 *       （ui_check 那个工程只编 ui/MainWindowUI.cpp 就能跑，就是靠这条）。所以这里的
 *       Group 是它自己的一份，和 InstanceGroupStore::Group 逐字段相同、由 MainWindow
 *       在两边转换。代价是那六行转换，换来的是"界面层不知道文件长什么样"。
 *
 * 【约定：对话框只改自己手里那份清单，不碰磁盘】
 *
 * 用户点「确定」之前，磁盘上的 wtfml.folders 一个字节都不能动 —— 这是所有对话框的
 * 共同约定（"只搭控件、值由调用方灌"）。所以这里存的是 m_groups 这份**工作副本**，
 * 确定之后由 MainWindow 把它和原来那份比一比，不一样才去写盘。
 *
 * 【约定：按钮那半和干活那半分开】
 *
 * 「新建…」要弹出 QInputDialog 问名字、「删除」要 QMessageBox::question 确认，两个
 * 都是**模态**的，在 offscreen 的验证工程里驱动不了（会一直等）。所以真正的逻辑在
 * createGroup() / renameGroup() / deleteGroup() 这几个 public 槽里，按钮那条路只是
 * "问一句 → 调它们"。和 MainWindow::onRenameInstanceClicked() / applyRename() 的
 * 拆法完全一样，理由也一样：这一半里藏着真会错的东西（重名怎么办、删组要不要连
 * 实例一起删），它必须验得了。
 */
#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QButtonGroup;
class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;
class QWidget;

class GroupDialog : public QDialog {
    Q_OBJECT

public:
    /// 一个分组。和 InstanceGroupStore::Group 逐字段相同（见文件头）。
    struct Group {
        QString id;
        QString name;

        friend bool operator==(const Group &, const Group &) = default;
    };

    /**
     * @brief 构造对话框。
     * @param groups 现有的组清单（顺序就是界面上的顺序）。
     * @param memberCounts 每个组里有多少个实例（按组 id）。缺的键当 0。
     *                     只用来显示「组名 (N)」和"删除时有多少实例会变成未分组"。
     * @param currentGroupId 这个实例现在归哪个组；空串 = 未分组（或那个组已经没了）。
     * @param parent 父窗口。
     */
    GroupDialog(const QList<Group> &groups, const QHash<QString, int> &memberCounts,
                const QString &currentGroupId, QWidget *parent = nullptr);

    /// 用户摆好的最终组清单。没改过的话就是进参那一份。
    QList<Group> groups() const { return m_groups; }

    /// 这个实例最终归哪个组；空串 = 未分组。
    QString assignedGroupId() const { return m_assignedGroupId; }

public slots:
    /// 新建一个组。名字为空、或者和已有的组重名时**不建**，弹一个警告框说明原因。
    /// @return 真的建了返回 true。
    bool createGroup(const QString &name);

    /// 把某个组改名。@return 改成功了返回 true。
    ///
    /// NOTE: 组改名**不动归属** —— 归属挂的是组的 id，不是名字。id 不变，所以
    ///       "给组改个名"不会把组里的实例抖出去。这正是 id 和名字分开的理由。
    bool renameGroup(const QString &groupId, const QString &name);

    /// 删掉一个组。组里的实例**一个都不会被删**，它们只是变成未分组；
    /// 正在编辑的这个实例如果就在这个组里，它也跟着变成未分组。
    /// @return 删掉了返回 true（找不到这个组时返回 false）。
    bool deleteGroup(const QString &groupId);

    /// 把"当前实例归到哪个组"改掉（界面上就是点那排单选钮）。
    /// @param groupId 空串 = 未分组。
    void setAssignedGroupId(const QString &groupId);

    // ---------- 控件的 objectName ----------
    // 验证工程靠这些名字认控件，别改回按条目文本去认（组名是用户起的，随时会变）。
    static QString groupListName() { return QStringLiteral("groupList"); }
    static QString addGroupButtonName() { return QStringLiteral("addGroupButton"); }
    static QString renameGroupButtonName() { return QStringLiteral("renameGroupButton"); }
    static QString removeGroupButtonName() { return QStringLiteral("removeGroupButton"); }
    static QString assignmentBoxName() { return QStringLiteral("assignmentBox"); }
    static QString confirmButtonName() { return QStringLiteral("groupDialogConfirmButton"); }
    static QString cancelButtonName() { return QStringLiteral("groupDialogCancelButton"); }
    /// 「未分组」那一条单选的文字。**没有组的时候它也是选中的那一条**（空串归属）。
    static QString ungroupedText() { return QStringLiteral("未分组"); }

    /// 组在列表里的那行文字。
    /// @param name 组名。@param count 组里有多少个实例。
    ///
    /// NOTE: 个数放在名字里（而不是单开一列）是因为这个对话框很窄，多一列会把组名
    ///       挤成省略号 —— 而组名才是用户认这个组的东西。
    static QString groupItemText(const QString &name, int count) {
        return QStringLiteral("%1 (%2)").arg(name).arg(count);
    }

    /// 这个实例现在归哪个组的单选钮。
    /// @param index 0 是「未分组」，1.. 依次是 groups() 里的那些。
    QWidget *assignmentButtonAt(int index) const;

private:
    /// 重画左边那张组列表（组清单变了之后调用）。选中项按 id 保住。
    void rebuildGroupList();
    /// 只把组列表里的实例数刷新一遍，不重建（重建会把选中项和滚动位置抖一下）。
    void refreshCounts();
    /// 重画右边那排单选钮。**整排重建**而不是增删某一个：单选钮的"选中"状态是
    /// 互斥的，零散地增删要同时照顾互斥组、按钮组的 id 映射和展开宽度，比整排重画
    /// 更容易错，而这里最多几十个控件。
    ///
    /// NOTE: 只有**组清单**变了才调它。改归属（setAssignedGroupId）只摆已有那排的
    ///       选中状态 —— 那可能是从某个单选钮自己的 toggled 里进来的，那时候重建
    ///       等于把正在发信号的那个控件删掉。
    void rebuildAssignment();
    /// 「重命名」「删除」能不能点：要有选中的组。列表的选中项一变就调。
    void refreshActionButtons();
    /// 当前选中的是哪个组（没选中返回空串）。列表上选中的那一行对应的组 id。
    QString selectedGroupId() const;
    /// 组 id 对应的组名；没有这个组时返回空串。
    QString groupName(const QString &groupId) const;
    /// 组 id 在 m_groups 里的下标；没有返回 -1。
    int rowOfGroup(const QString &groupId) const;
    /// 组名合不合法。@param exceptGroupId 重名比较时跳过的那一个（改自己的名字时
    /// 不能和自己撞）。合法返回空串，否则返回给用户看的一句原因。
    QString validateGroupName(const QString &rawName, const QString &exceptGroupId) const;

    QList<Group> m_groups;
    /// 工作副本的实例数。用户在对话框里改归属时跟着调 —— 于是"删组时有多少实例会
    /// 变成未分组"那句话说的是**此刻界面上**的状态，不是打开对话框那一刻的。
    QHash<QString, int> m_memberCounts;
    QString m_assignedGroupId;

    QListWidget *m_groupList = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_renameButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QWidget *m_assignmentBox = nullptr;
    /// 那排单选钮的容器。重画只清空它，外面的弹簧因此不会被堆成好几个。
    QWidget *m_choices = nullptr;
    QVBoxLayout *m_choiceLayout = nullptr;
    QButtonGroup *m_assignmentGroup = nullptr;
    /// 单选钮下标 → 组 id（下标 0 是「未分组」，对应空串）。
    QStringList m_assignmentIds;
    /// true = 此刻正在用代码摆单选钮的选中状态，那几条 toggled 不算用户操作。
    bool m_updatingAssignment = false;
};
