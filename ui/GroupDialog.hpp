/**
 * @file GroupDialog.hpp
 * @brief 「分组设置…」对话框：管**分组本身** —— 有哪些组、叫什么、什么顺序、默认展开还是收着。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 【它不管实例】
 *
 *       2026-09-25 之前这张窗里还有一半是"这个实例归到哪个组"（一排单选钮）。用户原话：
 *       「下方的分组设置不再是针对单个实例的设置，而是对现有分组数目/名称等的设置（恢复
 *       默认分组设置，给组重命名，给组排列展示顺序，默认展开/关闭等）」—— 于是那半整个
 *       没了，把实例放进某个组这件事搬到**组行行尾那颗齿轮**上（那里的第一项「把选中的
 *       实例移入分组…」，见 MainWindowUI::openGroupPicker()）。所以这张窗现在**不读**当前
 *       选中的实例，也不认得任何实例。
 *
 * 【进参出参都是值，本文件不 include 任何 src/ 下的头】
 *
 *       ui/ 一直不依赖业务层（ui_check 那个工程只编 ui/ 就能跑，就是靠这条）。所以这里的
 *       Group 是它自己的一份，和 InstanceGroupStore::Group 逐字段相同、由 MainWindow 在
 *       两边转换。代价是那几行转换，换来的是"界面层不知道文件长什么样"。
 *
 * 【约定：对话框只改自己手里那份清单，不碰磁盘】
 *
 * 用户点「确定」之前，磁盘上的 wtfml.folders 一个字节都不能动 —— 这是所有对话框的共同
 * 约定（"只搭控件、值由调用方灌"）。所以这里存的是 m_groups 这份**工作副本**，确定之后由
 * MainWindow 把它和原来那份比一比，不一样才去写盘。
 *
 * 【约定：按钮那半和干活那半分开】
 *
 * 「新建…」要弹出 QInputDialog 问名字、「删除」要 QMessageBox::question 确认、「恢复默认
 * 分组设置…」也要确认，三个都是**模态**的，在 offscreen 的验证工程里驱动不了（会一直等）。
 * 所以真正的逻辑在 createGroup() / renameGroup() / deleteGroup() / moveGroupUp() /
 * moveGroupDown() / setGroupExpanded() / restoreDefaults() 这几个 public 槽里，按钮那条路
 * 只是"问一句 → 调它们"。和 MainWindow::onRenameInstanceClicked() / applyRename() 的拆法
 * 完全一样，理由也一样：这一半里藏着真会错的东西（重名怎么办、删组要不要连实例一起删、
 * 顺序换到边界上怎么办），它必须验得了。
 */
#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QWidget;

class GroupDialog : public QDialog {
    Q_OBJECT

public:
    /// 一个分组。和 InstanceGroupStore::Group 逐字段相同（见文件头）。
    struct Group {
        QString id;
        QString name;
        /// 这一组在实例树里**画成展开的还是收着的**（用户原话里的「默认展开/关闭」）。
        ///
        /// NOTE: 它既是"默认"也是"当前" —— **只有这一份状态**（全文在
        ///       InstanceGroupStore::Group 那段）。这张窗里的勾选框改的就是它，
        ///       而且改完要等用户按「确定」才落盘（对话框不碰磁盘那条约定）。
        bool expanded = true;

        friend bool operator==(const Group &, const Group &) = default;
    };

    /**
     * @brief 构造对话框。
     * @param groups 现有的组清单（顺序就是界面上的顺序，也是树上的顺序）。
     * @param memberCounts 每个组里有多少个实例（按组 id）。缺的键当 0。
     *                     只用来显示「组名 (N)」和"删这个组会有多少实例回到未分组"。
     * @param defaultGroups 五个默认分组（"恢复默认"要换成的就是它们）。
     *                     顺序就是界面上从上到下的顺序，名字用来拼确认框里那句话。
     * @param defaultCounts 恢复默认**之后**每个默认组里会有多少个实例（按组 id）。
     *                     这一格是调用方算出来的：恢复默认会把用户自己建的组全拆掉，
     *                     那些实例回到"没表过态"，于是按版本类型自动归类 —— 落到哪一组
     *                     只有 MainWindow 那边算得出来（defaultGroupIdFor()）。
     *                     缺的键当 0。
     * @param parent 父窗口。
     */
    GroupDialog(const QList<Group> &groups, const QHash<QString, int> &memberCounts,
                const QList<Group> &defaultGroups, const QHash<QString, int> &defaultCounts,
                QWidget *parent = nullptr);

    /// 用户摆好的最终组清单。没改过的话就是进参那一份。
    QList<Group> groups() const { return m_groups; }

public slots:
    /// 新建一个组。名字为空、或者和已有的组重名时**不建**，弹一个警告框说明原因。
    /// @return 真的建了返回 true。
    bool createGroup(const QString &name);

    /// 把某个组改名。@return 改成功了返回 true。
    ///
    /// NOTE: 组改名**不动归属** —— 归属挂的是组的 id，不是名字。id 不变，所以
    ///       "给组改个名"不会把组里的实例抖出去。这正是 id 和名字分开的理由。
    bool renameGroup(const QString &groupId, const QString &name);

    /// 删掉一个组。组里的实例**一个都不会被删**，它们只是变成未分组。
    /// @return 删掉了返回 true（找不到这个组时返回 false）。
    bool deleteGroup(const QString &groupId);

    /// 把某个组在清单里**往上挪一格**（用户原话里的「给组排列展示顺序」）。
    /// 已经在最上面、或者找不到这个组时**什么都不做**，返回 false。
    bool moveGroupUp(const QString &groupId);

    /// 同上，往下挪一格。
    bool moveGroupDown(const QString &groupId);

    /// 把某个组**直接挪到最上面**（用户原话：「按住 shift 时点击就直接放最顶上/底下」）。
    /// 已经在最上面、或者找不到这个组时什么都不做，返回 false。
    ///
    /// NOTE: 和 moveGroupUp() 拆成两个槽（不是给那个加个 bool 参数）：**置顶不是"挪很多
    ///       格"**，它一步到位、和现在排第几无关；而且这样验证工程能直接驱动这一档
    ///       —— 界面那条路（按住 Shift 点按钮）读的是 QApplication::keyboardModifiers()，
    ///       那个在 offscreen 下摆不出来。
    bool moveGroupToTop(const QString &groupId);

    /// 同上，**直接挪到最下面**。
    bool moveGroupToBottom(const QString &groupId);

    /// 改某一组的「默认展开」。
    ///
    /// NOTE: 摆勾选框的**那一下**会发一次 toggled，那一次不该被当成用户点的（那会绕回去
    ///       再改一遍状态）；挡它的是 m_updatingExpanded，和以前那排单选钮的
    ///       m_updatingAssignment 同一个路数。
    void setGroupExpanded(const QString &groupId, bool expanded);

    /// 把整份清单换成五个默认分组（用户原话里的「恢复默认分组设置」）。
    ///
    /// NOTE: 这是**工作副本**上的动作，和别的槽一样等「确定」才落盘 —— 半路按「取消」
    ///       就等于什么都没发生。实例一个都不会被删：自己建的那些组分没了之后，里面的
    ///       实例回到"没表过态"，下一次刷新按版本类型自动归类（见 InstanceGroupStore::
    ///       restoreDefaultGroups() 那段，两边的说法必须一致）。
    /// @return 换了返回 true（清单本来就等于默认那一份时也返回 true，只是没变）。
    bool restoreDefaults();

    // ---------- 控件的 objectName ----------
    // 验证工程靠这些名字认控件，别改回按条目文本去认（组名是用户起的，随时会变）。
    static QString groupListName() { return QStringLiteral("groupList"); }
    static QString addGroupButtonName() { return QStringLiteral("addGroupButton"); }
    static QString renameGroupButtonName() { return QStringLiteral("renameGroupButton"); }
    static QString removeGroupButtonName() { return QStringLiteral("removeGroupButton"); }
    static QString moveUpButtonName() { return QStringLiteral("moveGroupUpButton"); }
    static QString moveDownButtonName() { return QStringLiteral("moveGroupDownButton"); }
    /// 「默认展开」那个勾选框（按选中那一组的状态亮着）。
    static QString expandedCheckName() { return QStringLiteral("groupExpandedCheck"); }
    static QString restoreDefaultsButtonName() {
        return QStringLiteral("restoreDefaultsButton");
    }
    static QString confirmButtonName() { return QStringLiteral("groupDialogConfirmButton"); }
    static QString cancelButtonName() { return QStringLiteral("groupDialogCancelButton"); }

    /// 组名的长度上限。比实例目录名的 64 短，因为组名只出现在界面上（不进路径）、
    /// 而且它和实例数一起挤在同一行里。超过这个长度的名字只会被省略号截掉，
    /// 挡下来是为了让用户当场知道"名字太长了"，而不是过一会儿才发现看不全。
    ///
    /// NOTE: 组行行尾那颗齿轮里的「重命名这一组…」也归它管（那是另一条改名的路，
    ///       MainWindow::renameGroupTo()）—— 两个入口必须是同一把尺子，不然"从这个口
    ///       进去能起 40 个字的名字、从那个口进不去"。
    static int maxGroupNameLength() { return 32; }

    /// 组在列表里的那行文字。
    /// @param name 组名。@param count 组里有多少个实例。
    ///
    /// NOTE: 个数放在名字里（而不是单开一列）是因为这个对话框很窄，多一列会把组名
    ///       挤成省略号 —— 而组名才是用户认这个组的东西。
    /// NOTE: 树上那个组头写的也是这一句（MainWindowUI::groupItemText() 转发的就是它）。
    static QString groupItemText(const QString &name, int count) {
        return QStringLiteral("%1 (%2)").arg(name).arg(count);
    }

private:
    /// 重画左边那张组列表（组清单变了之后调用）。选中项按 id 保住。
    void rebuildGroupList();
    /// 「重命名」「删除」「上移」「下移」能不能点，以及「默认展开」勾选框怎么摆。
    /// 列表的选中项一变就调（勾选框那一半顺手在这儿对齐）。
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
    /// 工作副本的实例数。只用来看「组名 (N)」和"删这个组有多少实例回到未分组"。
    QHash<QString, int> m_memberCounts;
    /// 五个默认分组（"恢复默认"要换成的那一份）。
    QList<Group> m_defaultGroups;
    /// 恢复默认之后每个默认组里会有多少实例（见进参那段）。
    QHash<QString, int> m_defaultCounts;
    /// true = 此刻正在用代码摆勾选框的状态，那一次 toggled 不算用户操作。
    bool m_updatingExpanded = false;

    QListWidget *m_groupList = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_renameButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_moveUpButton = nullptr;
    QPushButton *m_moveDownButton = nullptr;
    QCheckBox *m_expandedCheck = nullptr;
    QPushButton *m_restoreButton = nullptr;
};
