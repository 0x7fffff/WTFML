/**
 * @file InstanceFilesPageRows.cpp
 * @brief `InstanceFilesPage` 里**列表那一行**这一摊：建行、行尾那几格、以及它们各自的数据。
 *
 * 【这一摊的状态】
 *
 * 一份「这一条是什么」的账 —— `m_icons` / `m_resourceTips`（按 `identityPath()` 存）、
 * `m_rowsBuilt` / `m_shownIdentities`，加上 `m_list` 里那些由 `setItemWidget()` 挂着的
 * 行控件本身。它们必须同进同退：`refresh()` 是 `clear()` + 重建，行控件整个换掉，而图标和
 * 详情是异步问回来的（读盘 + 联网，几秒）—— 账要是跟着行一起没了，重列出来的那一行会先
 * 退回占位图再等一次回答。
 *
 * 所以这一份账、以及**只读写这一份账**的那几条（`rebuildRows()` / `makeRow()` /
 * `iconForPath()` / `setIconForPath()` / `setResourceForPath()` / `applyDetailButtonState()`
 * / `setRowDetailVisible()` / `syncRowHover()` / 那几个 `…At(row)`）留在一起。类没有拆 ——
 * 同一个类、定义分散在两个翻译单元，验证工程一条断言都不用改。
 *
 * 不在这儿：扫目录（`refresh()`）、筛选、统计、选择与批量操作、悬浮操作条、事件过滤。
 * 那些读的是同一份账，但它们是**另一件事**（"盘上有什么" / "用户选了哪几条"），
 * 分别留在外壳与 `InstanceFilesPageSelection.cpp`。
 */

#include "InstanceFilesPage.hpp"

#include "Animations.hpp"
#include "ElidedLabel.hpp"
#include "InstanceFilesPageInternals.hpp"
#include "MainWindowUI.hpp"
#include "Theme.hpp"
#include "ToggleSwitch.hpp"
#include "VersionIcons.hpp"

#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPalette>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace InstanceFilesPageInternals;

namespace {

/// 比正文字号小一档 —— **行里那第二行小字**专用的（现在只有「大小 / 时间」在用）。
///
/// 档位是 `MainWindowUI::versionDateFontDelta()`，和版本树那两行式的行**同一档**
/// （VersionTreeDelegate::paint 里给 rowSubTextRole() 那行算字号用的就是它）：
/// 实例设置里这一页和版本树是并排的两种列表，行高也都是 46，字号差一档就不像一套东西。
/// 两行一样大的话，"上面一行名字、下面一行细节"看着只像一行被折了。
///
/// 顺带也是**行高够用**的前提：46 px 那一格里要装两行（19 + 2 + 15 上下），
/// 第二行照正文那么大就顶到边了。
void makeSmallLine(QLabel *label) {
    if (label == nullptr) {
        return;
    }
    QFont font = label->font();
    font = Theme::textFont(font, Theme::TextRole::Caption);
    label->setFont(font);
}

/// 某一条**没有真图标**时那一格摆什么（见 makeRow 里那段）。
///
/// 文件夹和文件分开：文件夹的形状（folderMark）是它**本来的**长相、不是"没查到"，
/// 而文件那一片用拼图（见 VersionIcons::puzzleMark 那段为什么是它）。
QPixmap placeholderIcon(bool isDir, const QColor &color) {
    const int size = MainWindowUI::instanceFileIconSize();
    if (isDir) {
        return VersionIcons::folderMark(size, color);
    }
    return VersionIcons::puzzleMark(size, color);
}

/// 把**接口给的那张真图**缩到这一格的大小。
///
/// `QLabel::setPixmap()` **不缩放**：它按 pixmap 本来的大小画，超出控件的那一块被裁掉。
/// 而作者传的那张图本来就不是这一格的大小 —— Modrinth 那条 `_96.webp` 是 96 px，
/// `icon.png` 那条更是**原图**（几百 px 的都有），而这一格只有
/// `MainWindowUI::instanceFileIconSize()`（32）。不缩放的话每一张真图标都只露出左上角
/// 那一小块，看着像"图裂了"。
///
/// NOTE: 这个毛病**只有真图标真的加载出来了**才看得见 —— 占位图本来就是照这一格的尺寸画的
/// （见 placeholderIcon），一个像素都不差。
///
/// NOTE: `KeepAspectRatio` 而不是拉满 —— 资源包封面不是方的，拉满会变形。
/// （`setScaledContents(true)` 那个开关的意思恰好就是"拉满"，所以不能用它。）
/// 缩放走 `SmoothTransformation`：这是给眼睛看的图，不是拿来逐像素比对的。
/// NOTE: **本来就装得下的图一个像素都不动**：放大只会糊（图标真有 16 px 的），
/// 而且小图原样交给 QLabel 时它按对齐方式摆在那一格里（见 makeRow 里的 AlignCenter）。
QPixmap scaleToIconSlot(const QPixmap &icon) {
    const int slot = MainWindowUI::instanceFileIconSize();
    if (icon.isNull() || (icon.width() <= slot && icon.height() <= slot)) {
        return icon;
    }
    return icon.scaled(slot, slot, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

/// 目录**自己带着的那张小图**：`<目录>/icon.png`。没有、读不出来就是空 pixmap。
///
/// NOTE: 那条文件名是**游戏的约定**，不是我们发明的：大小写按它写的 `icon.png` 认
/// （Windows 上不区分，别的系统上区分，照原样写最保险）。
/// NOTE: 不去认"这是不是一个真存档"（比如查 `level.dat` 在不在）—— 摆一张图而已，
/// 目录里有那张图就画。多一道判断就多一处"哪些目录算数"的规则要跟着游戏版本改。
/// NOTE: 只有**目录**才看这个（见 iconForPath 里 `isDir` 那个闸）：文件自己有哈希那条路。
QPixmap directoryThumbnail(const QString &dirPath) {
    const QString candidate = dirPath + QStringLiteral("/icon.png");
    if (!QFileInfo::exists(candidate)) {
        return QPixmap();
    }
    // `icon.png` 是**世界里带进来的**（共享存档里那一份也长这样），当外部输入看：
    // 先问头里声明的尺寸、超了不解。原版那张是 64×64，这里留到 1024 —— 再大的一律
    // 当"读不出来"，调用方退回文件夹字形。
    constexpr int kMaxSide = 1024;
    constexpr qint64 kMaxPixels = 1024LL * 1024;
    QImageReader reader(candidate);
    const QSize pixels = reader.size();
    if (!pixels.isValid() || pixels.width() <= 0 || pixels.height() <= 0
        || pixels.width() > kMaxSide || pixels.height() > kMaxSide
        || qint64(pixels.width()) * pixels.height() > kMaxPixels) {
        return QPixmap();
    }
    // 读不出来的（坏文件、不是张图）回空的那张，调用方自然退回文件夹字形 —— 不弹错、
    // 也不留一格里半张烂图。
    const QImage decoded = reader.read();
    if (decoded.isNull()) {
        return QPixmap();
    }
    return QPixmap::fromImage(decoded);
}

}  // namespace

void InstanceFilesPage::rebuildRows() {
    if (m_list == nullptr) {
        return;
    }
    // 把"现在最上面那一行是谁"记下来（见 .hpp 里那两个函数那段）。这一句
    // 排在最前面 —— 下面 `clear()` 一过就什么都不剩了。`refresh()` 那条路（点一下开关
    // 走的就是它）**已经先记过一遍**，它那一趟到这里时表是空的，所以这个函数看见空表
    // 就不动手（否则会把刚记下的那一份覆盖掉）。
    rememberScrollAnchor();
    ensureStateFilterVisible();
    // 这一段（clear + 逐条建行）**把信号挡着** —— 见 .hpp 里 m_selectedPaths
    // 那段：`clear()` 会发一次 itemSelectionChanged，建行时按路径还的选择也会发。
    // 不挡的话那一发会拿"还没建完的列表"去重算 m_selectedPaths，等于**把用户的选择在
    // 自己重列的过程中抹掉**（而重列正是"筛一下搜索""禁用一个文件"必经的那一步）。
    // ⚠ 挡住之后，重列末尾必须**自己**把账和那条操作条摆对（下面那两句）。
    //
    // 身份**只在"那一条刚刚改过名"时才拿来还选择**。身份是把 `.disabled`
    // 抹掉算的（见 `identityPath`），于是 `X.jar` 和 `X.jar.disabled` **同时在场**时
    // 两行同身份 —— 重列不能把"用户只点了一条"的选择变成两条（搜索框敲一个字、批量
    // 开关跑完之后那一次重列，都会走到这里），否则紧接着一次「删除选中项」就把用户
    // **从来没选过**的那一份也删掉了（不可逆）。两份准账，各管一件事：
    // · `presentPaths` —— 盘上还在的所有路径（**含被筛掉的**）。某个旧路径还在
    // ⇒ 那不是改名，是它暂时被筛出去了；
    // · `visibleIdentityCounts` —— 这一趟**看得见**的条目里每个身份有几条。不止一条
    // ⇒ 身份本身分不出"是哪一条"，不许拿它替用户选。
    QSet<QString> presentPaths;
    QHash<QString, int> visibleIdentityCounts;
    presentPaths.reserve(m_entries.size());
    for (const Entry &entry : m_entries) {
        presentPaths.insert(entry.path);
        if (entryMatchesFilter(entry)) {
            ++visibleIdentityCounts[identityPath(entry.path)];
        }
    }
    QSet<QString> renamedIdentities;
    renamedIdentities.reserve(m_selectedPaths.size());
    for (const QString &path : m_selectedPaths) {
        if (!presentPaths.contains(path)) {
            // 只有**这一页刚发出去的启用/禁用**（`m_pendingIdentityMigration`）
            // 才把"旧路径消失"解释成改名。删除 / 移动也是消失 —— 删除 `X.jar` 之后
            // `X.jar.disabled` 会自己变成选中的，下一次「删除选中」就删掉用户没选过的文件。
            if (m_pendingIdentityMigration.contains(identityPath(path))) {
                renamedIdentities.insert(identityPath(path));
            }
        }
    }
    // 这一趟攒下来的两样东西（下面逐行填，出了循环再用）：
    // · `freshRows` —— 这一趟**新冒出来**的那几行控件（上一趟没摆过它们，见那一小段）；
    // · `shownIdentities` —— 这一趟摆出来的**全部**身份。**不能就地往 `m_shownIdentities`
    // 里写**：它是下面判"新不新"的那份底账，边判边写等于每一行都判成旧的。
    QList<QWidget *> freshRows;
    QSet<QString> shownIdentities;
    {
        const QSignalBlocker blocker(m_list);
        // clear() 会连同 setItemWidget() 挂上去的那些行控件一起删掉 —— 统计队列/表跟着作废
        // （理由逐字同 refresh() 里那一段）。
        m_list->clear();
        m_statQueue.clear();
        m_statLabels.clear();

        // 「先把 item 全排好，再逐条挂行控件」—— 两件事**不能夹在一起做**。
        //
        // `setItemWidget()` 挂上一条之后，视图后面每一次 `addItem()` 都要替**已经挂着的
        // 那一摞**重算一遍几何；夹在一起写的话第 k 条 item 的插入要给前面 k-1 个行控件各算
        // 一遍，整表就是平方级。实测 1000 行：夹着写 1.39 s，先排完再挂 0.29 s（同一版
        // 二进制 A/B；前面那三档对照在 docs/P2_LARGE_LIST_PAINT_20261031.md）。所以这个
        // 循环只管"item 和行控件都建出来、配好对"，挂的事等整表排完一趟做完。
        QList<QPair<QListWidgetItem *, QWidget *>> pendingRows;
        for (const Entry &entry : m_entries) {
            if (!entryMatchesFilter(entry)) {
                continue;
            }
            auto *item = new QListWidgetItem(QString(), m_list);
            item->setData(pathRole(), entry.path);
            item->setData(disabledRole(), entry.disabled);
            // 行高和实例树 / 版本树同一档：一页里换行高不会错，但"我们自己的列表"看着该是一个东西。
            item->setSizeHint(QSize(0, MainWindowUI::versionTreeRowHeight()));
            m_list->addItem(item);
            QWidget *row = makeRow(entry.displayName, entry.path, entry.isDir,
                                   entry.disabled, entry.size, entry.modified);
            const QString identity = identityPath(entry.path);
            if (!m_shownIdentities.contains(identity)) {
                freshRows.append(row);
            }
            shownIdentities.insert(identity);
            // 选择按**身份**还回去，不是按盘上那个名字（`identityPath`，
            // 见 .hpp 里 m_selectedPaths 那段）—— 用户点一下行尾那个开关，MainWindow 就把
            // `X.jar` 改成了 `X.jar.disabled` 再回头重列，按名字比的话这一行**当场掉出
            // 选择**。盘上还是同一个文件、用户手里选的就是"这一条"，身份一比就还上了。
            // 身份只在**这一条是刚才那一条改过名**时才替用户选 ——
            // ① 路径逐字对得上（没改名的那一趟，最常见）；
            // ② 或者旧名字已经不在盘上了（= 真的改了名），并且这一页里同身份只有
            // 它一条（`X.jar` 和 `X.jar.disabled` 同时在场时身份分不出是谁，
            // 那就一条都不替用户选，见上面那两份准账）。
            const bool keptSelected =
                m_selectedPaths.contains(entry.path)
                || (renamedIdentities.contains(identity)
                    && visibleIdentityCounts.value(identity) == 1);
            if (m_batchMode && keptSelected) {
                item->setSelected(true);
            }
            pendingRows.append(qMakePair(item, row));
        }
        for (const auto &pair : pendingRows) {
            m_list->setItemWidget(pair.first, pair.second);
        }
    }
    // 「同页筛选/列表变化：只动画**新增**、删除和状态，
    // **不重播整页**」那一半的正面。
    // · **只淡新冒出来的那几行**（`Anim::rowAppear()`，120 ms，只有淡没有抬）——
    // 整页一个像素都不动，更不重播换页那一发（那一条现在由"筛一下不会调到
    // pageAppear()"这个结构本身保证：全仓 `pageAppear()` 的调用点只有换页那几处）。
    // · **整批换掉就不淡**：新来的超过 `rowAppearLimit()`（一页几十行一起淡看着是整页在闪）
    // ⇒ 一行都不淡。
    // · **第一趟不淡**：那是这一页头一次铺满，前头正跑着换页那一发（`m_rowsBuilt`）。
    // · 「删除」这一半**没做**：删掉的那些行是连控件一起销毁的，要淡它得先把那个控件
    // 留活到动画跑完（`clear()` 会一起删），那是另一套（见 FEATURES 里记的那一句）。
    if (m_rowsBuilt && !freshRows.isEmpty() && freshRows.size() <= Anim::rowAppearLimit()) {
        for (QWidget *row : freshRows) {
            Anim::rowAppear(row);
        }
    }
    m_shownIdentities = shownIdentities;
    m_rowsBuilt = true;
    // 这一趟真摆出来的那些路径 —— **只有"筛完一条不剩"那一个判据**在用它
    // （选择那份账由 syncSelectedPaths() 从列表里现问一遍，见下面那一段）。
    QStringList shownPaths;
    shownPaths.reserve(m_list->count());
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (item != nullptr) {
            shownPaths.append(item->data(pathRole()).toString());
        }
    }

    // 这一趟已经消费掉那几张"刚点过开关"的身份了（见 .hpp 里
    // `m_pendingIdentityMigration` 那段）。清掉 —— 下一次重列是别的原因（删除、搜索、
    // 统计回来了…），那时一个身份都不该拿"改过名"还选择。开关失败（盘上没改成）时
    // 这里也照样清：旧路径还在、根本没走进改名那一支，登记本来就用不上。
    m_pendingIdentityMigration.clear();

    syncSelectedPaths();

    // 「目录本来就是空的」和「筛完一条不剩」是两件事（见 m_filteredEmpty）：前者底下那
    // 两排筛选整块收起来，后者留着让用户把条件改回来。所以这里只在"目录里有东西"时
    // 才把后者置起来。
    m_filteredEmpty = !m_empty && shownPaths.isEmpty();

    // 顶上那四颗的字（条数）和亮着的态 —— 每一样都在这一趟重算（见 fillStateButtons）。
    fillStateButtons();
    // 可见性 + 底下那条悬浮操作条的显隐/灰化（applyState 末尾会喊 updateSelectionBar）。
    applyState();
    updateInfoPanel();

    // 光标**此刻正压在某一行上**时，把那一行的「详情」露出来。
    // 为什么要补这一发：重列把行控件整个换掉了，而鼠标没动过 —— Qt 不会对着一个新控件
    // 补发 Enter（它只在"鼠标底下那个控件变了"的时候发），于是用户刚点完开关、光标还停在
    // 那一行上，那一行的「详情」会一直藏着，直到他把鼠标挪一下。见 syncRowHover()。
    //
    // ⚠ **这一句必须排到下一趟事件循环**，不能就地喊：上面刚把行整个换掉
    // （`clear()` + 逐条 `addItem()` + `setItemWidget()`），而"光标底下是哪个控件"
    // 要的是**视图已经排好版**的位置 —— 此刻那一趟排版还欠着（Qt 自己用一个 0 毫秒的
    // 定时器补，见 `QAbstractItemViewPrivate::doDelayedItemsLayout()`），新行控件的几何
    // 还是还没算出来的样子。就地喊的话问到的是一份旧的/空的几何，露出来的可能是**别**
    // 那一行的「详情」（或者谁都没露）—— 而两样都不报错。0 毫秒的定时器按登记的先后
    // 发，欠着的那一趟排版登记在我们前面，所以落到这一句时几何已经是对的。
    // **滚回原来那个位置**也排在这一趟里（见 .hpp 里 scrollAnchor 那段）——
    // 理由和上面逐字相同：滚动条能滚多远要等排版算完才知道，就地摆的话，摆到一半就会
    // 被那个还没算出来的上限截住（症状是"滚回了一大半、就是差一点点"）。摆在
    // syncRowHover() 之前：那一句问的是"光标底下是哪个控件"，它要的正是摆好之后的坐标。
    QTimer::singleShot(0, this, [this] {
        restoreScrollAnchor();
        syncRowHover();
    });

    // 目录行的「大小 / 时间」要递归遍历才拿得到，**排在行都摆出来之后**（见 .cpp 顶部
    // 那段和 fillDirectoryStats）—— 所以这一页是先出来、数字后到。
    if (m_statTimer != nullptr && !m_statQueue.isEmpty()) {
        m_statTimer->start();
    }
}

QWidget *InstanceFilesPage::makeRow(const QString &shownName, const QString &path, bool isDir,
                                    bool isDisabled, qint64 size, const QDateTime &modified) {
    auto *row = new QWidget(m_list);
    row->setProperty(rowPathProperty(), path);
    row->installEventFilter(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(8);

    // ---------- 最左一格：这一条的图标 ----------
    //
    // 资源作者传的那张图（模组图标 / 资源包封面）由 MainWindow 反查之后
    // 递进来（见 setIconForPath()），这一层只负责摆。**先摆一张占位图** —— 真图标是
    // 异步来的（要读盘、要联网），而那几秒里这一格不能是空的：空白和"这一条查不到
    // 图标"在屏幕上是同一个样子，而这两件事不一样。
    //
    // NOTE: 占位图**按"是不是文件夹"分两种**（文件夹一律是文件夹的形状，那是它本来的
    // 长相，不是"没查到"），文件一律是拼图（见 VersionIcons::puzzleMark 那段）。
    // NOTE: 这一格的宽度是**固定**的（不是 sizeHint 撑着）：真图标回来时换上去的 pixmap
    // 尺寸和占位图一样，但"让布局重新算一遍"这件事本身不该发生 —— 一列几十行
    // 同时重排会让整张表抖一下。
    // **这一页没有图就不建这一格**（见 setRowIconsVisible()）：`hide()` 不算数，
    // 藏起来的控件仍然占着布局里那一格，名字照样少一截。
    if (m_rowIconsVisible) {
        auto *icon = new QLabel(row);
        icon->setObjectName(iconLabelName());
        icon->setFixedSize(MainWindowUI::instanceFileIconSize(),
                           MainWindowUI::instanceFileIconSize());
        // 居中对齐：真图标缩过之后就是**这一格那么大**（正方形、正好铺满），摆哪儿都一样；
        // 而"本来就比这一格小"的那些（16 px 的小图，见 scaleToIconSlot）不居中的话会贴左上角
        // —— QLabel 默认是 AlignLeft｜AlignVCenter，横向就偏了。
        icon->setAlignment(Qt::AlignCenter);
        // 颜色取**次要那一档**（和「大小 / 时间」那行小字同一档）：这一列是配角，用正文色会让
        // 整张表看着比名字那一列还重。取的是**这一行自己的调色板**（不是全局那一份）——
        // 一页里每一行都在同一个窗口里，取到的值一样，但"这一行怎么画"跟着这一行走。
        // 真图标要是已经拿到了就**直接摆那一条的**（`m_icons`，见 setIconForPath 的 NOTE）——
        // 「刷新」重建出来的那一行不该先退成占位图再等一次回答。
        icon->setPixmap(iconForPath(path, isDir, row->palette().color(QPalette::PlaceholderText)));
        icon->setToolTip(QDir::toNativeSeparators(path));
        layout->addWidget(icon);
    }

    // ---------- 左边那半：**名字一行、大小 / 时间一行** ----------
    //
    // 名字和「大小 / 时间」分两行排：三样挤在一行时，大小 / 时间那一截在 150% 缩放的
    // 字号下要 300 px，而行宽只有 450 上下 —— 名字那一格会被挤成 **0 宽**，一整列的
    // 名字全看不见（`fabric-api-0.119.3+1.21.4.jar` 这类名字在盘上是常态）。
    // 行高本来就是 46 px（versionTreeRowHeight()），
    // 装两行字够 —— 摆成上下两行，名字那一格就能拿到整幅宽度（按钮除外）。
    auto *textColumn = new QWidget(row);
    auto *textLayout = new QVBoxLayout(textColumn);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    // 名字那一格是**会省略号收尾**的（ElidedLabel，不是普通 QLabel）：普通 QLabel 在控件
    // 比文字窄的时候是**硬裁**的 —— 最后一个字被切掉一半，看着像这一条的名字就是那串
    // 半截的字。真名字有 47 个字符（约 650 px），这一格给不了那么宽。
    //
    // 从**中间**截（不是尾巴）：这一串两头都有用 —— 前头是哪个 mod，尾巴上是 `.jar`
    // 那一截；而被禁用的那几条尾巴上还挂着「（已禁用）」四个字，
    // 从尾巴截掉恰好把「这一条是关着的」这个最要紧的信息先扔了。
    //
    // 名字尾巴上那颗 ↑（"这一条查出来有新版本"）**是这一格字符串的一部分**
    // （见 .hpp 里 updateArrowText() 那段 —— 为什么不另起一格：名字那一格是
    // `QSizePolicy::Ignored` 的，它会把整行余下的宽度全吃掉，"名字后面紧挨着"在那种
    // 布局里做不到，箭头只会被推到行的最右边）。拼成一个字符串之后：
    // · "紧挨着名字"是结构保证的（就是同一串字）；
    // · "**和名字一个字号**"也是结构保证的（同一格、同一个字体）；
    // · 省略号那一套不用改：`ElideMiddle` 两头都留，尾巴上那颗 ↑（和「（已禁用）」）
    // 不会被省略掉。
    auto *name = new ElidedLabel(textColumn);
    name->setObjectName(nameLabelName());
    name->setText(nameTextWithUpdateMark(shownName, m_outdatedPaths.contains(path)));
    name->setElideMode(Qt::ElideMiddle);
    // 名字长的时候让它自己缩（`Ignored` = 别拿它的 sizeHint 去撑这一行），
    // 但不许被挤成 0：这一格是这一行的主要内容。整条路径挂在 tooltip 上，
    // 名字被截掉的那一截还能看全（省略号只说明"这里短了"，短的是什么只有它说得清）。
    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    name->setMinimumWidth(nameLabelMinWidth());
    name->setToolTip(QDir::toNativeSeparators(path));
    if (isDisabled) {
        // 被禁用的一整行**名字也灰掉**：光靠名字后面那四个字，扫一眼看不出这一条是关着的。
        makeSecondary(name);
        // NOTE: 只划名字这一格 —— 底下那行「大小 / 时间」不动（它是这一条的规格，
        // 没有"关着还是开着"可言；跟着划掉会读成"这一行整个作废了"）。
        // NOTE: 走 `setFont()` 而不是样式表 —— 这一页的字体本来就都由代码摆
        // （makeSmallLine 同一条路），`strikeOut` 也没有第二个读者。
        QFont struckFont = name->font();
        struckFont.setStrikeOut(true);
        name->setFont(struckFont);
    }
    textLayout->addWidget(name);

    // 「12.3 MB / 12:00」。文件夹没有有意义的大小（Windows 上 info.size() 对
    // 目录给的是目录项自己的字节数，不是里面东西的总和），给一个破折号比给一个假数字好。
    // 这一行也会省略（尾巴那头）：字号大 / 窗口窄的时候它比这一格还长，硬裁同样是"看着
    // 像画坏了"。它的 hint 不封顶 —— 它是下面那行小字，让名字那一格先拿够。
    const bool wantsStats = m_canEdit && isDir;
    QString meta;
    if (wantsStats) {
        meta = statsPendingText();
    } else {
        meta = isDir ? QStringLiteral("—") : formatSize(size);
        if (modified.isValid()) {
            meta += QStringLiteral(" / ") + modified.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        }
    }
    auto *metaLabel = new ElidedLabel(textColumn);
    metaLabel->setObjectName(metaLabelName());
    metaLabel->setText(meta);
    metaLabel->setElideMode(Qt::ElideRight);
    makeSecondary(metaLabel);
    makeSmallLine(metaLabel);
    textLayout->addWidget(metaLabel);
    if (wantsStats) {
        // 登记的是**这一格**，不是行号：统计是一格一格异步补上来的，这中间用户完全可能
        // 点过「刷新」（行整个重建过）—— 存行号的话，补上去的是**别人**那一格。
        // （· 每行只有这一格，所以它同时也是这一行。）
        m_statLabels.insert(path, metaLabel);
        m_statQueue.append(path);
    }

    layout->addWidget(textColumn, 1);

    if (m_batchMode) {
        // ---------- 行尾那颗「详情」----------
        //
        // 这一格的次序与显隐：
        // ① 这一格在开关**前面** ⇒ 行尾是 `[详情][开关][删除]`；
        // ② 它**只在鼠标悬停这一行的时候才露出来** —— 建出来时是藏着的，显隐由
        // eventFilter 里 Enter/Leave 那一支管（见 .hpp 里 setRowDetailVisible 那段）。
        //
        // NOTE: **每一行都建这一格**（哪怕认不出资源），认不出来的那些 `setEnabled(false)`
        // —— 一列下来三格左右要对得齐，而"有的行少一格"会让名字那一列宽窄不一。
        // 灰着而不是不摆，读起来是"这一条没有详情页"，不是"这里漏画了"。
        // NOTE: 判据只有一条：`m_resourceTips` 里有这一条 = 认出来了
        // （见 setResourceForPath）。
        auto *detail = new QToolButton(row);
        detail->setObjectName(detailButtonName());
        detail->setAutoRaise(true);
        const int glyph = MainWindowUI::instanceFileIconSize();
        detail->setIcon(VersionIcons::actionIcon(VersionIcons::Action::Details));
        detail->setIconSize(QSize(glyph, glyph));
        // 文字仍然给着（虽然不画）：读出这一格是"详情"这件事**只剩这一个出口**
        // （悬停提示、无障碍，以及验证工程按它核对"这一颗是干什么的"）。
        detail->setText(detailText());
        applyDetailButtonState(detail, path);
        // 悬停才露（②）：藏起来的时候**占位留着**（`setRetainSizeWhenHidden`）——
        // 不留的话鼠标一进一出整行都要重排一遍（名字那一格跟着一伸一缩），而这一格正好
        // 夹在名字和开关中间，抖起来最扎眼。
        // ⚠ 藏着的控件不是鼠标目标，所以**不影响命中测试**：右键照样落到这一行身上
        // （见 eventFilter 里那一支）。
        QSizePolicy detailPolicy = detail->sizePolicy();
        detailPolicy.setRetainSizeWhenHidden(true);
        detail->setSizePolicy(detailPolicy);
        detail->setVisible(false);
        connect(detail, &QToolButton::clicked, this, [this, path] { emit detailRequested(path); });
        layout->addWidget(detail);
    }

    if (m_canToggle && !isDir) {
        // NOTE: 开关的位置：它排在**详情那颗后面**（行尾三格是
        // `[详情][开关][删除]`）。没开批量那几页
        // 没有详情那一格，它照旧是倒数第二格。
        // 一列下来各格都对得齐。**只给"游戏会自己扫的那三个目录里的文件"**
        // （见 setCanToggle）。
        auto *toggle = new ToggleSwitch(row);
        toggle->setObjectName(toggleButtonName());
        // **开着 = 游戏扫得到它**（正常文件名）、关着 = 名字后面挂着 `.disabled`。
        // 方向不能反：开关向右 = "生效中"是这个控件在别处的意思（设置页那些开关也是），
        // 而这一条恰好是最要紧的一条 —— 反了的话"全开"看着像"全关"。
        toggle->setChecked(!isDisabled);
        // 说"改成哪个名字"要用**盘上那个真名字**（QFileInfo::fileName），不是行上那串
        // 显示名 —— 禁用着的那一条显示名后面挂着「（已禁用）」，拼进去就成了一串谁也不认的名字。
        toggle->setToolTip(isDisabled
                               ? tr("%1所选文件。").arg(toggleOffText())
                               : tr("%1所选文件，保留原文件。")
                                     .arg(toggleOnText()));
        connect(toggle, &ToggleSwitch::clicked, this, [this, path] { requestToggle(path); });
        layout->addWidget(toggle);
    }

    if (m_canEdit && isDir) {
        // NOTE: 和垃圾桶同一个做法 —— **字形是画的**（VersionIcons，全仓一个图片文件都
        // 没有），尺寸取和行首图标一样的那一档、颜色取次要那一档，所以一列下来
        // 两处行尾控件左右对得齐、深浅也一样。文字仍然给着（`text()` = 「编辑」，
        // 画不画是 QToolButton 的 ToolButtonIconOnly 说了算）：悬停提示、无障碍、
        // 以及验证工程"这一格是干什么的"都只剩这一个出口。
        // NOTE: 它排在垃圾桶**后面**（这一页两档不会同时开：存档页 canDelete=false，
        // 别的页 canEdit=false），所以不会有"两页之间那一列左右不齐"的问题。
        auto *edit = new QToolButton(row);
        edit->setObjectName(editButtonName());
        edit->setAutoRaise(true);
        const int glyph = MainWindowUI::instanceFileIconSize();
        edit->setIcon(VersionIcons::actionIcon(VersionIcons::Action::Edit));
        edit->setIconSize(QSize(glyph, glyph));
        edit->setText(editText());
        edit->setToolTip(tr("编辑存档名称和游戏规则。"));
        connect(edit, &QToolButton::clicked, this, [this, path] { emit editRequested(path); });
        layout->addWidget(edit);
    }

    if (m_canDelete) {
        auto *remove = new QToolButton(row);
        remove->setObjectName(deleteButtonName());
        remove->setAutoRaise(true);
        // 字形是**画的**（VersionIcons，全仓一个图片文件都没有），尺寸取和图标那一格
        // 一样的一档 —— 行尾两颗按钮和行首那枚图标同一个大小，看着才是一套的。
        // 删除保留主题危险色，禁用时由公共图标引擎改用禁用色。
        const int glyph = MainWindowUI::instanceFileIconSize();
        remove->setIcon(VersionIcons::actionIcon(VersionIcons::Action::Delete));
        remove->setIconSize(QSize(glyph, glyph));
        // 文字仍然给着（虽然不画）：读出这一格是"删除"这件事**只剩这一个出口**
        // （悬停提示、无障碍，以及验证工程按它核对"这一颗是干什么的"）。
        remove->setText(deleteText());
        remove->setToolTip(isDir ? tr("删除文件夹及其内容。")
                                 : tr("删除文件。"));
        connect(remove, &QToolButton::clicked, this, [this, path] { emit deleteRequested(path); });
        layout->addWidget(remove);
    }

    return row;
}

QAbstractButton *InstanceFilesPage::toggleButtonAt(int row) const {
    if (m_list == nullptr) {
        return nullptr;
    }
    QListWidgetItem *item = m_list->item(row);
    QWidget *rowWidget = item != nullptr ? m_list->itemWidget(item) : nullptr;
    return rowWidget != nullptr ? rowWidget->findChild<ToggleSwitch *>(toggleButtonName()) : nullptr;
}

QToolButton *InstanceFilesPage::deleteButtonAt(int row) const {
    if (m_list == nullptr) {
        return nullptr;
    }
    QListWidgetItem *item = m_list->item(row);
    QWidget *rowWidget = item != nullptr ? m_list->itemWidget(item) : nullptr;
    return rowWidget != nullptr ? rowWidget->findChild<QToolButton *>(deleteButtonName())
                                : nullptr;
}

QToolButton *InstanceFilesPage::detailButtonAt(int row) const {
    if (m_list == nullptr) {
        return nullptr;
    }
    QListWidgetItem *item = m_list->item(row);
    QWidget *rowWidget = item != nullptr ? m_list->itemWidget(item) : nullptr;
    // 没开 setBatchMode(true) 的页上每一行都没有这一格 ⇒ 这里恒是 nullptr（见 .hpp 那段）。
    return rowWidget != nullptr ? rowWidget->findChild<QToolButton *>(detailButtonName()) : nullptr;
}

QToolButton *InstanceFilesPage::editButtonAt(int row) const {
    if (m_list == nullptr) {
        return nullptr;
    }
    QListWidgetItem *item = m_list->item(row);
    QWidget *rowWidget = item != nullptr ? m_list->itemWidget(item) : nullptr;
    // 这一行的路径要**对得上**才认：那一格可能压根没建（文件行、或者没开 setCanEdit 的
    // 页），findChild 那时给的是 nullptr —— 和"有这一格但是别人那一行的"是两回事。
    // （不做额外的路径核对：一行里只可能有一支笔，`findChild` 找的就是这一行自己的。）
    return rowWidget != nullptr ? rowWidget->findChild<QToolButton *>(editButtonName()) : nullptr;
}

QLabel *InstanceFilesPage::nameLabelAt(int row) const {
    if (m_list == nullptr) {
        return nullptr;
    }
    QListWidgetItem *item = m_list->item(row);
    QWidget *rowWidget = item != nullptr ? m_list->itemWidget(item) : nullptr;
    return rowWidget != nullptr ? rowWidget->findChild<QLabel *>(nameLabelName()) : nullptr;
}

QPixmap InstanceFilesPage::iconForPath(const QString &path, bool isDir,
                                       const QColor &placeholderColor) const {
    // `contains` 而不是"取出来看不看是空"：**空 pixmap 也是一条回答**（"这一条问过了、
    // 没有"），它和"还没问"在屏幕上都是占位图，但只有前者不该再问一次（见 m_icons 那段）。
    const QString key = identityPath(path);
    if (m_icons.contains(key)) {
        const QPixmap icon = m_icons.value(key);
        if (!icon.isNull()) {
            // 缩到这一格的大小（见 scaleToIconSlot 那段）。`m_icons` 里存的是**原样那张**
            // —— 这一层不篡改别人给的东西（验证工程按"存进去的 == 拿出来的"核对过），
            // 缩放只发生在"往控件上摆"这一步。
            return scaleToIconSlot(icon);
        }
    }
    // NOTE: 判据是**这个目录里有没有那张图**，不是"这一页是不是存档页"：文件夹自带缩略图
    // 这个约定本来就是按目录说的，哪个目录里头放了就画哪个（`mods/mcef-libraries/`
    // 那种没有的照旧退回文件夹字形）。按页判断反而要多一处"哪一页算"的知识。
    // NOTE: 不缓存。这一格每一趟 refresh() 只问一次（iconForPath 只在 makeRow 里调），
    // 而那张图只有十来 KB、decode 一次是微秒级；真按 mtime 键住一份，反倒多出一处
    // "世界存了一次档、图变了"时要记得清的地方。
    if (isDir) {
        const QPixmap own = directoryThumbnail(path);
        if (!own.isNull()) {
            return scaleToIconSlot(own);
        }
    }
    return placeholderIcon(isDir, placeholderColor);
}

bool InstanceFilesPage::setIconForPath(const QString &path, const QPixmap &icon) {
    if (path.isEmpty()) {
        return false;
    }
    // 这一页没有图标栏（见 setRowIconsVisible）：记也不用记 —— 没人会来读那一份账。
    if (!m_rowIconsVisible) {
        return false;
    }
    // **先记账、再找行**：这一条现在在不在屏幕上不影响"这一条问过了"。顺序反过来的话，
    // 图标回来的那一刻用户正好点了「刷新」（那几毫秒里这一页是没有行的），这一条就永远
    // 回到"还没问"——而 MainWindow 那边不会再问第二遍。
    m_icons.insert(identityPath(path), icon);
    if (m_list == nullptr) {
        return false;
    }
    // 按**路径**找回那一行（不按行号）：图标是一张一张异步回来的，这中间用户完全可能
    // 点过「刷新」、删过一条、或者这一页被重建过 —— 存行号的话，那时候换上去的是**别人**
    // 的图标（而且不报错）。路径是这一行的身份（见 pathRole()）。
    //
    // NOTE: 找不到不是错误：那一行已经不在了（用户刚删了它）。什么都不用做。
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (item == nullptr || item->data(pathRole()).toString() != path) {
            continue;
        }
        QWidget *rowWidget = m_list->itemWidget(item);
        if (rowWidget == nullptr) {
            return false;
        }
        auto *label = rowWidget->findChild<QLabel *>(iconLabelName());
        if (label == nullptr) {
            return false;
        }
        // 空 pixmap = 这条查不到图标 ⇒ **退回占位图**（不是清成空白：空白和"还没查完"
        // 在屏幕上是同一个样子，而一列里有的有图有的空白，看着像这一行漏画了）。
        const bool isDir = QFileInfo(path).isDir();
        label->setPixmap(iconForPath(path, isDir, label->palette().color(
                                                     QPalette::PlaceholderText)));
        return true;
    }
    return false;
}

bool InstanceFilesPage::setResourceForPath(const QString &path, const QString &tip) {
    if (path.isEmpty()) {
        return false;
    }
    // 空 tip 有两种意思，**两种都要办**（见 .hpp 那段）：
    // ① 这一条**从来**没认出来过 ⇒ 什么都不用做（"点不动"是默认状态，不是一条要摆出来
    // 的状态；记一条空的等于给每一行都记一笔账）；
    // ② 上一趟认出来了、这一趟没有（同一个路径上换了内容）⇒ 必须把那一格从"点得动"
    // 退回"点不动"（`setEnabled(false)`），否则那一颗按钮看着能按、按下去什么都不
    // 发生。**一个「看着能按、按下去没反应」的按钮比灰着更糟。**
    // 两件事靠 `remove()` 的返回值分开：删掉了 = 走过 ② 那一支。
    // 这一份账也按 `identityPath(path)` 记（理由逐字同 setIconForPath）——
    // 不然点一下「禁用」，那一行的详情按钮会跟着变灰（"这一条认不出来了"），而它认出来的
    // 正是同一个资源。
    const QString key = identityPath(path);
    const bool hadLink = m_resourceTips.remove(key) > 0;
    if (tip.isEmpty() && !hadLink) {
        return false;
    }
    // 和 setIconForPath() **同一个顺序、同一个理由**：先记账、再找行 —— 图标/身份回来的
    // 那一刻用户正好点了「刷新」（那几毫秒里这一页没有行）时，这一条得记住"认出来了"，
    // 否则「刷新」之后那一格又变成灰的了，而 MainWindow 那边不会再推第二遍。
    // （空 tip 那一支上面已经 remove 过了，这里不用再补一次插入。）
    if (!tip.isEmpty()) {
        m_resourceTips.insert(key, tip);
    }
    if (m_list == nullptr) {
        return false;
    }
    // 按**路径**找回那一行（不按行号）：身份是一批一批异步回来的，这中间用户完全可能点过
    // 「刷新」、删过一条 —— 存行号的话，那时候点上去的是**别人**的资源页（而且不报错）。
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (item == nullptr || item->data(pathRole()).toString() != path) {
            continue;
        }
        QWidget *rowWidget = m_list->itemWidget(item);
        // NOTE: 找不到不是错误：那一行已经不在了（用户刚删了它）。什么都不用做 ——
        // 账已经记下，下一趟 refresh() 摆出来的那一行自然是能点的。
        if (rowWidget == nullptr) {
            return false;
        }
        auto *button = rowWidget->findChild<QToolButton *>(detailButtonName());
        if (button == nullptr) {
            // 这一页没开批量（没有那一格）—— 什么都不用做。回头那一趟 refresh() 重列出来的
            // 行自然是对的（账上面已经记好了）。
            return false;
        }
        applyDetailButtonState(button, path);
        return true;
    }
    return false;
}

void InstanceFilesPage::applyDetailButtonState(QToolButton *button, const QString &path) const {
    if (button == nullptr) {
        return;
    }
    // 判据只有一处：`m_resourceTips` 里有这一条 = 认出来了（见 setResourceForPath）。
    // 取的是 `identityPath(path)` 那一格（见 .hpp 里那段）—— 禁用/启用只改了名字，
    // 那一行认出来的还是同一个资源。
    const auto tip = m_resourceTips.constFind(identityPath(path));
    const bool known = tip != m_resourceTips.constEnd();
    button->setEnabled(known);
    button->setToolTip(known ? tip.value()
                             : tr("未识别资源，暂无详情。"));
}


void InstanceFilesPage::setRowDetailVisible(QWidget *rowWidget, bool visible) {
    if (rowWidget == nullptr) {
        return;
    }
    // 一次找一格：这一行里能叫 detailButtonName() 的只有那颗 ⓘ（开关是 ToggleSwitch、
    // 垃圾桶和笔各是别的名字）。没开批量的那几页这一格**根本不存在** ⇒ findChild 给
    // nullptr，这就是"这一页没有这一档"的全部处理。
    if (auto *button = rowWidget->findChild<QToolButton *>(detailButtonName())) {
        button->setVisible(visible);
    }
}

void InstanceFilesPage::syncRowHover() {
    // 重列把行控件整个换掉了，而鼠标**可能没动过** —— Qt 只在"鼠标底下那个控件变了"的时候
    // 发 Enter/Leave，对着一个刚建出来、光标正好压在上面的控件**不会**补发 Enter。症状：
    // 用户点一下行尾那个开关（那一发正好会让 MainWindow 改名 + 重列），光标原地不动，那一行
    // 的「详情」就一直藏着，直到他把鼠标挪一下。
    //
    // 判据不用 `underMouse()`：那个位是 Qt 在 Enter/Leave 那一趟顺手摆的，而这一趟正是
    // "Enter 没来过"的那种情况。判据是**当场问一句"光标底下现在是哪个控件"**
    // （`QApplication::widgetAt` 走的是真正的命中测试，还能顺带把"光标其实在另一扇窗上面"
    // 也排除掉）—— 它确实是"此刻在不在这一行上"的权威答案。
    //
    // 认"哪一行"**顺着爹链往上找**（凭据就是那个动态属性），不问 `itemAt()`：
    // `itemAt()` 要的是视图已经算好的行位置，而这一句的调用方（rebuildRows）正好是在
    // 刚把行换掉、那一趟排版还欠着的时刻喊的（所以它排了个 0 毫秒的定时器，见那儿）。
    // 顺着爹链找只依赖"这个控件现在挂在谁底下"，天然不受行位置准不准的影响；而且
    // `widgetAt()` 问不到行控件时（光标在行之间的空白、在那条悬浮操作条上、或者几何
    // 还没落地）这一趟自然什么都不做 —— 不会露错一行。
    if (m_list == nullptr || !m_batchMode) {
        return;
    }
    QWidget *viewport = m_list->viewport();
    if (viewport == nullptr) {
        return;
    }
    QWidget *under = QApplication::widgetAt(QCursor::pos());
    if (under == nullptr) {
        return;
    }
    for (QWidget *w = under; w != nullptr; w = w->parentWidget()) {
        if (!w->property(rowPathProperty()).isValid()) {
            continue;
        }
        // 认出来的这一层要是**这一页列表里的行**才算：`setItemWidget()` 正是把行控件挂在
        // viewport 底下的（见 .hpp 里 eventFilter 那段），别处就算有人挂上同一个属性名也
        // 不该亮起来。
        if (w->parentWidget() == viewport) {
            setRowDetailVisible(w, true);
        }
        return;
    }
}
