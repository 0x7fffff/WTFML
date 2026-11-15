/**
 * @file InstanceFilesPageFilter.cpp
 * @brief 文件页的两个筛选条件：顶上的搜索框、以及「全部 / 启用 / 禁用 / 可更新」那一档。
 *
 * 【为什么单独一个翻译单元】
 *
 * 这一片的主语是"这一页现在筛着什么、哪几条算中"：
 *
 * - 两个条件本身：`m_searchText` / `m_stateFilter`，以及"这份名单里有哪些条有新版本"
 *   （`m_outdatedPaths` + `m_updateChecked`）；
 * - 两套控件（不开批量那几页是下拉框、开了批量那四页是四颗带条数的按钮）的填与摆；
 * - 判据 `entryMatchesFilter()` —— 状态那一栏和搜索框总共就这几个条件，它读的正是上面
 *   那三位，所以和设条件的那几条必须在一个文件里。
 *
 * 【三条反复出现的规矩】
 *
 * 一、**判据只有 `stateFilterVisible()` 一处**：这一档摆不摆得出来（「全部」恒在 / 这一
 *    页能不能筛更新 / 一条都没有就收起来 / 不开批量时「启用」那些不收）。条件**不许停在
 *    一档摆不出来的东西上** —— 否则屏幕上一条不剩，而没有任何一格亮着；
 * 二、填控件时**挡信号**：`addItem` / `setChecked` 都会绕回设条件那几条，而这一趟本来就
 *    要把它们摆对；
 * 三、条件一变就 `rebuildRows()`（列表是筛剩下的那一份，"条件与列表对不上"没有别的东西
 *    会自己纠正）。例外只有 `setUpdateResult()` 里那句"名单本身没变就不重排"。
 */

#include "InstanceFilesPage.hpp"

#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>

// ---------- 搜索 + 状态筛选----------

InstanceFilesPage::StateFilter InstanceFilesPage::filterAt(const QComboBox *combo, int index) {
    if (combo == nullptr || index < 0) {
        return StateFilter::All;
    }
    const QVariant data = combo->itemData(index);
    if (!data.isValid()) {
        return StateFilter::All;
    }
    switch (data.toInt()) {
    case 1:
        return StateFilter::Enabled;
    case 2:
        return StateFilter::Disabled;
    case 3:
        return StateFilter::Update;
    default:
        break;
    }
    return StateFilter::All;
}

void InstanceFilesPage::fillStateCombo() {
    if (m_stateCombo == nullptr) {
        return;
    }
    // 填的时候把信号挡着：`addItem` 每一次都会动 currentIndex，而那一发会绕到
    // setStateFilter() 上去（构造函数里那会儿还没建完，applyState() 会摸到空控件）。
    // 填完自己把该亮的那一格摆回去就够了。
    const QSignalBlocker blocker(m_stateCombo);
    m_stateCombo->clear();
    QList<StateFilter> filters{StateFilter::All, StateFilter::Enabled, StateFilter::Disabled};
    if (m_canFilterUpdates) {
        filters.append(StateFilter::Update);
    }
    int wanted = 0;
    for (int i = 0; i < filters.size(); ++i) {
        m_stateCombo->addItem(stateFilterText(filters.at(i)),
                              static_cast<int>(filters.at(i)));
        if (filters.at(i) == m_stateFilter) {
            wanted = i;
        }
    }
    // 「现在选着的那一档已经不在这几格里了」那一档由 setCanFilterUpdates() 上游兜掉
    // （它会把条件退回「全部」）—— 这里不再自己判一次，免得同一个规矩有两处出处。
    m_stateCombo->setCurrentIndex(wanted);
}

int InstanceFilesPage::stateFilterCount(StateFilter filter) const {
    switch (filter) {
    case StateFilter::All:
        return static_cast<int>(m_entries.size());
    case StateFilter::Enabled:
    case StateFilter::Disabled: {
        const bool wantDisabled = filter == StateFilter::Disabled;
        int count = 0;
        for (const Entry &entry : m_entries) {
            if (entry.disabled == wantDisabled) {
                ++count;
            }
        }
        return count;
    }
    case StateFilter::Update: {
        // 禁用/启用**改过名**的那几条自然不在 `m_outdatedPaths` 里（那份名单是按路径逐字
        // 记的），和 entryMatchesFilter() 是同一个口径。
        int count = 0;
        for (const Entry &entry : m_entries) {
            if (m_outdatedPaths.contains(entry.path)) {
                ++count;
            }
        }
        return count;
    }
    }
    return 0;
}

bool InstanceFilesPage::stateFilterVisible(StateFilter filter) const {
    if (m_worldLayout && filter != StateFilter::All) return false;
    // 「全部」恒在（别的档全收起来时，剩下的那一颗也得有个东西亮着）。
    if (filter == StateFilter::All) {
        return true;
    }
    // 这一页根本没有「可更新」那一档（见 setCanFilterUpdates）。
    if (filter == StateFilter::Update && !m_canFilterUpdates) {
        return false;
    }
    // * 没查过 ⇒ 这一颗**收着**（页面上确实没有任何东西说"有更新"，但那是对的 ——
    // 还没问过，谁也说不出口）；哪一条有新版本改由**那一行自己的 ↑**
    // 来说（见 makeItem 里那个箭头），不靠这一颗筛子。
    // * 查过了、一条没有 ⇒ 也收着（下面那条"空档收起来"）。
    // * 查过了、有几条 ⇒ 露着，写「可更新(N)」。
    //
    // ⚠ 于是 `m_updateChecked` 在这一页上**不是"露不露"的判据**（它只管提示语那一
    // 句，见 applyState），别再拿它来判一次 —— 判据是"没查过就藏起来"。
    // **一条都没有的那一档收起来**（「全部」恒在）。那四颗自己写着条数，「禁用(0)」是
    // 一颗点下去什么都没有的按钮 —— 收起来既省地方、也少一个"看着能按其实没用"的键。
    //
    // 这一条**只管开了批量的那四页**：没开批量的那几页摆的是下拉框，而它**不写条数**
    // —— 那里少一格读起来像出了错（那几页只有三格、行也不多）。
    if (!m_batchMode) {
        return true;
    }
    // **一条禁用都没有时，「启用」那一档也收起来**。理由和底下那条"空档收起来"是同一
    // 个：一条禁用都没有 ⇒ 启用的那些**就是全部**，「启用(N)」和「全部(N)」是同一份名单
    // —— 一颗按下去什么都不筛的按钮。
    //
    // 这是一条**不对称**的规矩（和底下那条操作条上的「启用/禁用」逐字同一套）：反过来
    // "全是禁用"那一档，「禁用(N)」**照样露着**。两半各自都对得上它的用处：
    // * 一条禁用都没有 → 启用/禁用这两个筛子都没意义（禁用那一档由下面那句自己收）；
    // * 全是禁用 → 用户**确实**可能想"只看禁用的那些"，这一档得留着。
    // ⚠ 判据只有这一处（别在 fillStateButtons() 里再判一遍 —— 那正是 .hpp 里那条
    // NOTE 说的"第二个出处"）。
    if (filter == StateFilter::Enabled && stateFilterCount(StateFilter::Disabled) == 0) {
        return false;
    }
    return stateFilterCount(filter) > 0;
}

void InstanceFilesPage::ensureStateFilterVisible() {
    if (stateFilterVisible(m_stateFilter)) {
        return;
    }
    // 条件**不许停在一档这一页摆不出来的东西上**：留着的话屏幕上是一条都不剩的列表，
    // 而两套控件里没有任何一格/一颗亮着 —— 用户找不到是哪儿把他的列表弄没了。
    m_stateFilter = StateFilter::All;
    // 下拉框那一格（不开批量的那几页走的正是它）也跟着摆回来。四颗那一套的"哪一颗亮着"
    // 不在这儿摆 —— fillStateButtons() 按 m_stateFilter 摆一遍（唯一出处）。
    if (m_stateCombo != nullptr) {
        const QSignalBlocker blocker(m_stateCombo);
        for (int i = 0; i < m_stateCombo->count(); ++i) {
            if (filterAt(m_stateCombo, i) == m_stateFilter) {
                m_stateCombo->setCurrentIndex(i);
                break;
            }
        }
    }
}

void InstanceFilesPage::fillStateButtons() {
    if (m_stateButtons.isEmpty()) {
        return;
    }
    for (int i = 0; i < m_stateButtons.size(); ++i) {
        const auto filter = static_cast<StateFilter>(i);
        QPushButton *button = m_stateButtons.at(i);
        // 文字一律是「那些字 + (条数)」——**有没有依据这一层不在这儿管**：没查过更新的时候
        // 「可更新」这一颗整颗收着（见 stateFilterVisible() 里那段），屏幕上根本读不到这个
        // 数，所以不必也不该在这儿再分一种写法。
        button->setText(stateFilterButtonText(filter, stateFilterCount(filter)));
        // 露不露：一条都没有的那一档收起来、不能筛更新的页上「可更新」那一颗不摆。
        // 判据只有 stateFilterVisible() 一处（见 .hpp 里那段）—— 别在这儿再判一遍。
        button->setVisible(stateFilterVisible(filter));
        // 哪一颗亮着 = "现在筛的是哪一档"，唯一的判据是 m_stateFilter。
        // 挡信号：`setChecked` 在**同一组里**会把别的那几颗顶下去，而这一趟本来就要
        // 逐颗摆对；不挡的话"摆第一颗"会绕回 setStateFilter() → 又一次 rebuildRows()。
        if (button->isChecked() != (filter == m_stateFilter)) {
            const QSignalBlocker blocker(button);
            button->setChecked(filter == m_stateFilter);
        }
    }
}

void InstanceFilesPage::setCanFilterUpdates(bool canFilterUpdates) {
    if (m_canFilterUpdates == canFilterUpdates) {
        return;
    }
    m_canFilterUpdates = canFilterUpdates;
    // 那一档没了、而条件正好是它 ⇒ 退回「全部」。
    // 判据收到 `stateFilterVisible()` 一处了（"这一档摆不摆得出来"现在有
    // 三条：全部恒在 / 这一页能不能筛更新 / 一条都没有就收起来），退档那一发跟着它走。
    // **必须排在 fillStateCombo() 前面**：下拉框那一格是按 `m_stateFilter` 摆的。
    ensureStateFilterVisible();
    fillStateCombo();
    // 重列一遍把列表放回来（**必须重列**：不重列的话屏幕上还是上一档筛剩下的那几条，
    // 而那一栏/那一颗里亮着的是「全部」—— 列表和条件对不上，且没有任何东西会自己纠正它）。
    // 它顺带会把那四颗按钮的文字（条数）和选中态一起摆对（见 fillStateButtons）。
    rebuildRows();
}

void InstanceFilesPage::setSearchText(const QString &text) {
    if (m_searchText == text) {
        return;
    }
    m_searchText = text;
    if (m_search != nullptr && m_search->text() != text) {
        // 从外面写（验证工程、或者"把条件清回来"）的时候把那一格也摆成一样 —— 但**别让
        // 它再回来叫一遍**（那一发会绕成第二次 rebuildRows）。
        const QSignalBlocker blocker(m_search);
        m_search->setText(text);
    }
    rebuildRows();
}

void InstanceFilesPage::setStateFilter(StateFilter filter) {
    // 这一档现在摆不出来的时候（这一页没有「可更新」那一档，或者那一档一条都没有、
    // 已经收起来了）落到「全部」—— 条件**不许停在一档这一页摆不出来的东西上**，否则
    // 屏幕上没有任何一格/一颗亮着，用户找不到是哪儿把他的列表弄没了。
    // 判据只有 `stateFilterVisible()` 一处：下拉框和四颗按钮两条路最后都走到这儿。
    if (!stateFilterVisible(filter)) {
        filter = StateFilter::All;
    }
    if (m_stateFilter == filter) {
        return;
    }
    m_stateFilter = filter;
    // 下拉框那一格也跟着摆（用户点的是它、外面调的是这一边，两条路都得对上）。
    if (m_stateCombo != nullptr) {
        const QSignalBlocker blocker(m_stateCombo);
        for (int i = 0; i < m_stateCombo->count(); ++i) {
            if (filterAt(m_stateCombo, i) == filter) {
                m_stateCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    // 四颗按钮那一套的选中态**不在这儿摆** —— rebuildRows() → fillStateButtons() 会按
    // m_stateFilter 摆一遍（两套控件的"哪一颗亮着"只有那一处出处）。
    rebuildRows();
}

void InstanceFilesPage::setUpdateResult(const QStringList &outdatedPaths, bool checked) {
    const QSet<QString> incoming(outdatedPaths.cbegin(), outdatedPaths.cend());
    // 这份名单**每一行都在吃**（那一行名字后面的 ↑ 就是它，见 makeItem
    // 里那段）—— 于是它一变就得把行重列一遍。
    // 判据取"**名单本身变没变**"而不是一刀切重列：主程序每查完一趟都会往这一页递一份
    // （哪怕结果和上一趟一模一样），一刀切就是白重排一次上百行的表。
    const bool changed = (m_outdatedPaths != incoming) || (m_updateChecked != checked);
    m_outdatedPaths = incoming;
    m_updateChecked = checked;
    // 这份名单**三处**在吃：行里那个 ↑、顶上那四颗的字（`可更新(126)`）、以及悬浮条上
    // 「更新」那一颗的灰化（选中的里面有没有一条可更新）。所以无论下面重不重列，
    // 这几处都得跟着重算。
    fillStateButtons();
    // 「可更新」那一档下这一趟检测**改变的是列表内容本身**（筛的就是这份名单）⇒ 那一档
    // 下无论变没变都得重列。
    if (changed || m_stateFilter == StateFilter::Update) {
        rebuildRows();
    } else {
        updateSelectionBar();
    }
}

bool InstanceFilesPage::entryMatchesFilter(const Entry &entry) const {
    // ---------- 状态那一栏 ----------
    switch (m_stateFilter) {
    case StateFilter::Enabled:
        if (entry.disabled) {
            return false;
        }
        break;
    case StateFilter::Disabled:
        if (!entry.disabled) {
            return false;
        }
        break;
    case StateFilter::Update:
        // 这一条在**上一趟更新检测查出来有新版本**的那份名单里。判据是路径**逐字相等**
        // （两边都是 `pathRole()` 那一格）—— 禁用/启用改了名之后路径也变了，于是就
        // 自然不在名单里了，那正是对的（改过名的那一条，上一趟的结论不作数）。
        if (!m_outdatedPaths.contains(entry.path)) {
            return false;
        }
        break;
    case StateFilter::All:
        break;
    }

    // ---------- 上面那条搜索框 ----------
    const QString needle = m_searchText.trimmed();
    if (needle.isEmpty()) {
        return true;
    }
    // 比**两样**：界面上那一格写的（可能挂着「（已禁用）」那四个字 —— 用户看着它找），
    // 以及盘上那个文件名（禁用着的那条带着 `.disabled`，用户手里那个文件就是这么叫的）。
    // 子串、不分大小写：`sodium.jar` 敲 `SOD`、`odium`、`.JAR` 都该中。
    return entry.displayName.contains(needle, Qt::CaseInsensitive) ||
           entry.raw.contains(needle, Qt::CaseInsensitive);
}
