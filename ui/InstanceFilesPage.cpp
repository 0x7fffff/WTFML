/**
 * @file InstanceFilesPage.cpp
 * @brief InstanceFilesPage 的实现（见 .hpp 里那段"六页共用一个形状"）。
 */
#include "InstanceFilesPage.hpp"

#include "ElidedLabel.hpp"
#include "InstanceFilesPageInternals.hpp"
#include "MainWindowUI.hpp"  // WheelGuard：状态下拉框不许被滚轮改
#include "Theme.hpp"         // 上面那两颗一般性的按钮（黑字黑边）

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPalette>
#include <QPushButton>
#include <QScrollBar>  // 滚动那一趟要重摆「详情」那一格（见构造里那段）
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>  // isCurrentPage()：往上找最外面那个堆叠容器
#include <QTimer>
#include <QVBoxLayout>

using namespace InstanceFilesPageInternals;

namespace {

/// 那条操作条四周的留白、以及它里面控件之间的间距（px）。
constexpr int kSelectionBarPadding = 8;
constexpr int kSelectionBarSpacing = 8;

/// 一个目录**里面所有东西**的字节数（递归；目录项自己那几十个字节不算，那和用户在资源
/// 管理器里看到的"大小"对不上）。
///
/// NOTE: 为什么这件事落在 ui/ 这一层：它要的只是"一个目录有多大"，用的是 Qt 自己的目录
/// 遍历 —— 不碰 AppPaths、也不认识 src/ 里任何一条规矩。这一层唯一那条硬线是
/// "不 include src/、不算路径"，这一件事两条都没踩（和它已经在用的 `QFileInfo::size()`
/// / `QDir::entryInfoList()` 同一档）。
/// NOTE: 带 `QDir::Hidden | QDir::System`：资源管理器报的大小是**算上隐藏文件**的，漏掉
/// 的话启动器报的数会比用户自己能核对的那个小，而且小得毫无规律（`.DS_Store` 那种
/// 是几百字节，但 `session.lock` 之类在存档目录里到处都是）。
/// NOTE: **不跟符号链接/junction 走**（判据是 `isSymLink() || isJunction()` 两条一起，理由
/// 见底下那一段）。实例的 `saves/` 底下挂一个 junction 是有的（用户把存档挪到别的
/// 盘上），跟着走过去会把**那一头**的整个目录算进来 —— 而那一头可能正是别的世界里
/// 的一份，于是这个数会莫名其妙地翻几倍。
/// NOTE: 这是一个**同步**的递归遍历，几十万文件的存档要几百毫秒。调用它的地方只有
/// fillDirectoryStats()，而那一趟一次只算一行（理由写在那边）。
///
/// 这里**自己递归**，不用 `QDirIterator(QDirIterator::Subdirectories)`：判据必须自己
/// 握着 —— 那条路上"看到链接就 `continue`"是**不生效的**：
///
/// ① **junction 不是符号链接**。Windows 上 `mklink /J` 出来的目录 junction，
/// `QFileInfo::isSymLink()` 是 **false**、只有 `isJunction()` 是 true。Qt 自己是这么
/// 分的：`qfilesystemmetadata_p.h:364-370` —— `IO_REPARSE_TAG_SYMLINK` → `LinkType`，
/// `IO_REPARSE_TAG_MOUNT_POINT`（junction）→ **只有** `JunctionType`。仓库在
/// `src/manage/FolderManager.cpp` 里也量过同一件事（那儿的 `isLinkToElsewhere()`
/// 就是两个条件一起判）。
/// ② **`continue` 拦不住下潜**。QDirIterator 拒绝进一个子目录只有一条判据
/// （Qt 6.10.1 `qdirlisting.cpp:384` 的 `checkAndPushDirectory()`：不开
/// `FollowDirSymlinks` 时看 `isSymLink()`）。junction 不是符号链接 ⇒ 它照进不误，
/// 而**进去之后每个文件都是一条独立的结果**，`continue` 只挡住了它自己那一格
/// （目录项，`size()` 是 0）—— 数照样翻倍，只是翻得看不出来。
///
/// 所以这里走本地递归：**下潜前**自己判两个条件，判完再决定进不进。
///
/// NOTE: 这里**故意不 include `src/manage/FolderManager.hpp`**：ui/ 这一层不 include src/
/// （不碰 AppPaths、也不认识 src/ 里任何一条规矩，见上面那 NOTE）。所以那两个条件是
/// **照抄**的，抄的只有"两条一起判"这一件事；将来要改，两处一起改
/// （搜 `isLinkToElsewhere`）。
qint64 sumFilesUnder(const QString &path, int depth) {
    // 深度兜底：真能转起来的那两条路（符号链接、junction）上面已经堵死了，所以这一位
    // 理论上走不到 —— 留着是为了"万一还有第三种重解析点"时别把栈撑爆（崩了比少报更糟）。
    // 256 级远在真实目录树之外（Windows 路径上限 260 个字符），不会变成"少算"的常态。
    if (depth > 256) {
        return 0;
    }
    const QDir dir(path);
    // 只要目录和文件（不要 `QDir::Drives` —— 那不是"这个目录里面有什么"）。
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDir::NoSort);
    qint64 total = 0;
    for (const QFileInfo &info : entries) {
        if (info.isSymLink() || info.isJunction()) {
            continue;  // 链接本身、以及它那头的一切，都不算（见上面那段）
        }
        if (info.isDir()) {
            total += sumFilesUnder(info.absoluteFilePath(), depth + 1);
        } else if (info.isFile()) {
            total += info.size();
        }
    }
    return total;
}

qint64 directoryBytes(const QString &dirPath) { return sumFilesUnder(dirPath, 0); }

/// 一个世界"上次是什么时候玩的"：`level.dat` 的修改时间。
///
/// 【为什么是 level.dat，不是目录自己的时间】
/// 目录的时间只在**增删条目**时变（存档里加了一个区块文件、删了一个），用户连着玩三个
/// 小时、存档内容一直在变，而那个时间可能一动不动 —— 拿它当"最后游玩时间"是错的。
/// `level.dat` 是游戏**每次保存世界**都会重写的那个文件（关卡名、种子、游戏规则都在
/// 里面），它的时间才是"上一次落盘"。
///
/// NOTE: `session.lock` 的时间戳在游玩期间一直在动，看起来更准，但**它只在玩的时候准**：
/// 退游戏时最后那一下不一定落在那上面，而且它是"锁"不是"存档"。用 level.dat 和
/// PCL 以及大多数启动器一致，用户拿它跟游戏自己的"选择世界"那一屏对得上。
/// NOTE: level.dat 不在（或者读不了时间）时退回**目录自己的时间** —— 给一个近似的值
/// 比给一个空的好，而且那种目录本来就"不是一个完整的存档"。
QDateTime worldLastPlayed(const QString &dirPath) {
    const QFileInfo level(dirPath + QStringLiteral("/level.dat"));
    if (level.exists()) {
        return level.lastModified();
    }
    return QFileInfo(dirPath).lastModified();
}

}  // namespace

InstanceFilesPage::InstanceFilesPage(QWidget *parent) : QWidget(parent) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(8);

    // ---------- 最上面那一排：一条搜索框----------
    //
    // 搜索框在「下载新 / 刷新 / 打开文件夹」那一排按钮**之上**，状态选择在那一排**之下**。
    //
    // 作用范围是**这一页的列表**：`mods/` 底下几十上百个 jar，找一个名字靠眼睛扫是最费的
    // 一件事，而这一页每一趟都是全列的。筛的是"这一条在界面上叫什么"（以及盘上那个文件名
    // ——禁用着的那条盘上带着 `.disabled`，用户手里那个文件确实是这么叫的），
    // 子串、不分大小写（`Sodium.jar` 敲 `sod`、`odium`、`.JAR` 都该中）。
    m_searchRow = new QWidget(this);
    {
        auto *row = new QHBoxLayout(m_searchRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        m_search = new QLineEdit(m_searchRow);
        m_search->setObjectName(searchEditName());
        m_search->setPlaceholderText(searchPlaceholder());
        // 有个"清除"小叉（和「配置文件」那一页左边的条目筛选同一个写法）：这一条是
        // **立刻生效**的筛选，而筛完之后那一格按「刷新」是清不掉的（刷新只重读目录）
        // —— 没有小叉的话用户得自己全选删掉，而那正是"以为筛坏了"的那一刻。
        m_search->setClearButtonEnabled(true);
        // 立刻生效，不走延时器：它只是把行重列一遍，不读盘、不改正文（和 ConfigFileEditorPage
        // 里那条条目筛选同理）。一个目录几十上百条，重建的代价当场就回来了。
        connect(m_search, &QLineEdit::textChanged, this,
                [this](const QString &text) { setSearchText(text); });
        row->addWidget(m_search, 1);
    }
    outer->addWidget(m_searchRow);

    // ---------- 上面那一排：路径 + 刷新 + 打开文件夹 ----------
    m_pathRow = new QWidget(this);
    auto *pathRow = new QHBoxLayout(m_pathRow);
    pathRow->setContentsMargins(0, 0, 0, 0);
    pathRow->setSpacing(8);
    // 这一格写这一页在看哪个目录：文字由 refresh() 灌（目录从 setDirectory() 来，
    // 只有那一处知道）。没有它，上面那一排就是「…… [刷新] [打开文件夹]」，用户
    // 不知道这一页在看哪个目录，而这一页叫「文件管理」。
    // 用 ElidedLabel 而不是普通 QLabel：路径一定比这一格长（实例名本身能到 64 个字），
    // 普通 QLabel 装不下是**硬裁**（最后一级目录名被切掉一半），而这一段文字两头都有用
    // （盘符那头说明是哪个游戏目录，尾巴那头是 mods / saves 哪一页）⇒ 从**中间**省略。
    m_pathLabel = new ElidedLabel(m_pathRow);
    m_pathLabel->setObjectName(pathLabelName());
    m_pathLabel->setElideMode(Qt::ElideMiddle);
    makeSecondary(m_pathLabel);
    // 路径可能很长（实例名本身就能到 64 个字），而它这一格**不许**把整页顶宽：
    // 横向按 Ignored 算 = 布局完全不管它要多大，只给它剩下来的地方，多出来的字被裁掉。
    // （和实例页左栏那条折行 QLabel 顶大整窗 sizeHint 是同一个坑的另一面 —— 那边是
    // 不能让它要宽度，这边是不能让它给宽度。）
    m_pathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_pathLabel->setMinimumWidth(0);
    // NOTE: 这里不挂 `setTextInteractionFlags(Qt::TextSelectableByMouse)`：ElidedLabel
    // 的文字是 paintEvent 自己画的，选中高亮画不出来 —— 用户拖一遍什么反应都没有，
    // 比不提供还费解。整条完整路径由**悬停提示**给（见 refresh() 里 setToolTip 那句），
    // 那是这一格唯一"看得全"的路。
    pathRow->addWidget(m_pathLabel, 1);

    // 顶上那颗「下载新 X」。摆在**路径右边、刷新左边**：它是这一页上
    // 「往里加东西」的那一颗，比刷新/打开文件夹更靠前；而路径仍旧贴着最左边 —— 那一格带
    // 的是"这一页在看哪儿"的身份，把它挪开，这一排就没有主语了。
    //
    // NOTE: 构造时是藏着的（没有文字 = 没有这一颗，见 setDownloadText()）。**每一页都建**
    // 它（这几页是同一段循环搭出来的，只多一个 if 反而要多一处"哪几页有"的知识）。
    m_download = new QPushButton(m_pathRow);
    m_download->setObjectName(downloadButtonName());
    m_download->setVisible(false);
    connect(m_download, &QPushButton::clicked, this, &InstanceFilesPage::downloadRequested);
    pathRow->addWidget(m_download);

    // 顶上那两颗（只有存档那一页露着，见 setImportText / setBatchText）。
    // 摆在**路径右边、下载/刷新左边**：和「下载新 X」同一档 —— 它们也是"这一页上的
    // 动作"，够不着这一页在看哪个目录那件事。
    m_batch = new QPushButton(m_pathRow);
    m_batch->setObjectName(batchButtonName());
    // 「批量编辑」= 打开另一层（一般性）；旁边那两颗「下载新 X」「导入世界」是确认性。
    Theme::markButton(m_batch, Theme::ButtonKind::Neutral);
    m_batch->setVisible(false);
    connect(m_batch, &QPushButton::clicked, this, &InstanceFilesPage::batchEditRequested);
    pathRow->addWidget(m_batch);

    m_import = new QPushButton(m_pathRow);
    m_import->setObjectName(importButtonName());
    m_import->setVisible(false);
    connect(m_import, &QPushButton::clicked, this, &InstanceFilesPage::importRequested);
    pathRow->addWidget(m_import);

    // 顶上那颗「加入选中的世界」（只有存档那一页露着，见 setJoinText）。
    // 摆在「导入世界」右边、刷新左边 —— 和它左边那几颗同一档（"这一页上的动作"）。
    // **不带对象名去查状态**：它发的是"哪一条路径"（见 .hpp 里 joinRequested 那段），
    // 这一层不掺和"那个版本认不认这个参数"。
    m_join = new QPushButton(m_pathRow);
    m_join->setObjectName(joinButtonName());
    m_join->setVisible(false);
    connect(m_join, &QPushButton::clicked, this,
            [this] { emit joinRequested(currentPath()); });
    pathRow->addWidget(m_join);

    m_refresh = new QPushButton(refreshText(), m_pathRow);
    m_refresh->setObjectName(refreshButtonName());
    Theme::markButton(m_refresh, Theme::ButtonKind::Ghost);
    // 刷新是**这一页自己的事**（重新列一遍目录，只有读），不用绕到 MainWindow 去。
    connect(m_refresh, &QPushButton::clicked, this, &InstanceFilesPage::refresh);
    pathRow->addWidget(m_refresh);

    m_openFolder = new QPushButton(openFolderText(), m_pathRow);
    m_openFolder->setObjectName(openFolderButtonName());
    Theme::markButton(m_openFolder, Theme::ButtonKind::Ghost);
    connect(m_openFolder, &QPushButton::clicked, this, [this] {
        const QStringList dirs = scanDirectories();
        emit openFolderRequested(dirs.isEmpty() ? m_directory : dirs.first());
    });
    pathRow->addWidget(m_openFolder);

    outer->addWidget(m_pathRow);

    // ---------- 路径那一排的下面：「结构/原理图」那一页那几排「来源」----------
    //
    // 两排下拉框**横着并排：上面一排是存档的，下面一排是投影的**。
    // 结构方块那半边跟着存档走、投影那半边跟着实例走（见 .hpp 里 setStructureSources 那段）
    // ⇒ 这一页要能同时列两个来源，屏幕上就得先有"看哪几个来源"这一件事。
    //
    // NOTE: 这个容器**每一页都建**（和上面那几颗按钮同一个理由：这几页是同一段循环搭出来的，
    // 多一个 if 就多一处"哪几页有"的知识），但**里面一排都不摆**、自己也藏着 ——
    // 只有「结构/原理图」那一页的调用方会调 setStructureSources()。别的七页因此
    // 一个像素都不变。
    m_structureRows = new QWidget(this);
    m_structureRows->setVisible(false);
    m_structureLayout = new QVBoxLayout(m_structureRows);
    m_structureLayout->setContentsMargins(0, 0, 0, 0);
    m_structureLayout->setSpacing(6);
    outer->addWidget(m_structureRows);

    // ---------- 按钮那排的下面：一个状态选择----------
    //
    // 状态选择在那一排按钮的**下方**，四格是全部 / 启用 / 禁用 / 可更新。顺序就是它们
    // 写在这儿的样子（`stateFilterText()` 是那些字的唯一出处）。
    //
    // 「可更新」这一格**不是每一页都有**（见 setCanFilterUpdates）：它靠的是一趟更新
    // 检测的结果，而只有下载页里真有对应一栏的那四页查得动（原理图 / 存档 / 配置文件
    // 里的文件根本不在任何资源平台的清单里）。
    m_stateRow = new QWidget(this);
    {
        auto *row = new QHBoxLayout(m_stateRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        // 左边那个标签：光一个「全部」摆在那儿，用户不知道它管的是列表的哪一半。
        // （和下载页那一排筛选的下拉框一样，每一格前面都有个标签。）
        // **只有没开批量的那几页还看得到它** —— 开了批量的页摆的是右边
        // 那四颗按钮，而那四颗自己就写着「全部(352) 启用(351) …」，再加一个「显示」
        // 是多余的。
        m_stateLabel = new QLabel(stateLabelText(), m_stateRow);
        m_stateLabel->setObjectName(stateLabelName());
        makeSecondary(m_stateLabel);
        row->addWidget(m_stateLabel);

        m_stateCombo = new QComboBox(m_stateRow);
        m_stateCombo->setObjectName(stateComboName());
        // 滚轮守护：**这一栏和别的下拉框一样会被鼠标轮子误伤**（列表里滚动时指针划过它，
        // 条件就被改了，而屏幕上是"列表自己少了一半"）。全仓同一处（见 WheelGuard）。
        WheelGuard::install(m_stateCombo);
        connect(m_stateCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
            setStateFilter(filterAt(m_stateCombo, index));
        });
        // 四格摆进去（信号在这条路上被挡着，见 fillStateCombo）。
        fillStateCombo();
        row->addWidget(m_stateCombo);

        // **四颗横向按钮**（全部 / 启用 / 禁用 / 可更新）。和上面那个下拉框
        // **互相显隐**（见 applyState），
        // 两套说的是同一件事（`m_stateFilter` 是唯一出处）。
        //
        // NOTE: **四颗都建着**，不能筛更新的那几页把「可更新」那一颗藏起来（见
        // fillStateButtons）—— 和「下载新 X」「导入世界」同一套做法：建/不建会让
        // "这一页该有什么"散成两处知识。
        // NOTE: `setAutoExclusive(true)` = 这四颗是一组单选（同一父控件即一组）。
        // **光靠它还不够**：它保证的是"点已经亮着的那一颗不会把它点灭"，而"点一颗
        // 就换条件"那一发走的是下面那个 clicked。
        m_stateButtonsRow = new QWidget(m_stateRow);
        {
            auto *buttons = new QHBoxLayout(m_stateButtonsRow);
            buttons->setContentsMargins(0, 0, 0, 0);
            buttons->setSpacing(8);
            for (int i = 0; i < 4; ++i) {
                const auto filter = static_cast<StateFilter>(i);
                auto *button = new QPushButton(m_stateButtonsRow);
                button->setObjectName(stateButtonName(filter));
                button->setCheckable(true);
                button->setAutoExclusive(true);
                button->setText(stateFilterButtonText(filter, 0));
                button->setToolTip(tr("仅显示%1。").arg(stateFilterText(filter)));
                // 一般性那一档（黑字黑边）：这一排是"筛成什么样"，不是这一页的主语，
                // 不该和顶上那几颗主行动按钮抢眼睛（和「刷新」「打开文件夹」同一档）。
                Theme::markButton(button, Theme::ButtonKind::Neutral);
                // 点一下 = 把条件落到这一档上。**没有别的副作用** —— 这一颗只筛条件，
                // 不发起更新检测（没查过的时候这一颗根本摆不出来，也就没有"点它"这回事）。
                connect(button, &QPushButton::clicked, this,
                        [this, filter] { setStateFilter(filter); });
                m_stateButtons.append(button);
                buttons->addWidget(button);
            }
        }
        row->addWidget(m_stateButtonsRow);
        // 靠左：这一排只管"筛成什么样"，不是这一页的主语（右边留给将来别的东西）。
        row->addStretch(1);
    }
    outer->addWidget(m_stateRow);

    // ---------- 列表 ----------
    m_list = new QListWidget(this);
    m_list->setObjectName(listName());
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setUniformItemSizes(true);
    // 行是**真控件**（见 .hpp 里那段"为什么每行是一个真控件"），宽度由它自己说了算；
    // 不许出现横向滚动条，否则那一排按钮会跟着来回滑。
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 用 itemActivated 而不是 itemDoubleClicked：前者是 Qt 说的「用户激活了这一条」
    // —— 双击**和**选中之后按回车都会发它（各平台按自己的习惯），而那正是"打开一个
    // 文件"该有的两种手势。
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item == nullptr) {
            return;
        }
        const QString path = item->data(pathRole()).toString();
        if (path.isEmpty()) {
            return;
        }
        emit activateRequested(path, QFileInfo(path).isDir());
    });

    // 「选中了哪几条」的**唯一记账处**（见 .hpp 里 m_selectedPaths 那段）。
    // 用户点行身、Ctrl+A、条上那三颗"改选择"的按钮 —— 最后都会走到这一发上。
    // NOTE: 它**不看是哪一路来的**：所以这一处是唯一出处，不必在每条路上各记一遍。
    // 记账那一趟本身收进了 syncSelectedPaths() —— 重列末尾要办的正是同一件事
    // （那一段把信号挡着，只能自己喊），两处各写一遍就是第二份真相。
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        m_actionPanelDismissed = false;
        syncSelectedPaths();
        updateSelectionBar();
        updateInfoPanel();
    });
    // 同一发信号再挂一条 —— 「加入选中的世界」那颗的灰/亮只跟"有没有选中
    // 一条"走（见 updateJoinEnabled()）。**单独一条连接**而不是塞进上面那个 lambda：
    // 那一条讲的是批量管理那一套（只在那四页上谈得上），这一条在存档那一页上。
    connect(m_list, &QListWidget::itemSelectionChanged, this,
            [this] { updateJoinEnabled(); });
    // 那条悬浮操作条的父控件是 `m_list` ⇒ 它（或者它的 viewport）变大小了得自己重算位置
    // （见 eventFilter）。
    if (m_list->viewport() != nullptr) {
        m_list->viewport()->installEventFilter(this);
    }
    // **滚动也要重摆一遍「详情」那一格**（见文件头【行尾三格的次序】②）。
    // 滚轮一动，光标底下那一行往往就换人了 —— 而 Qt **不会**因为滚动补发 Enter/Leave
    // （滚的是 viewport 的孩子，鼠标一个像素都没动，见 scrollContentsBy 那段）⇒ 那一格会
    // 留在**已经不光标底下的**那一行上，直到用户动一下鼠标才自己纠正。所以每一趟滚动之后：
    // 先全收起来，再按"光标底下现在是谁"露一次（`syncRowHover()` 用的正是那个权威判据）。
    // NOTE: 收的时候是**全列表**走一遍 —— 一页几十上百行，每一句对一个本来就没露着的控件
    // 都是白改（`setVisible(false)` 探一下就走），这点代价和"滚一格留一个错格"比不值一提。
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (!m_batchMode || m_list == nullptr) {
            return;
        }
        for (int row = 0; row < m_list->count(); ++row) {
            QListWidgetItem *item = m_list->item(row);
            setRowDetailVisible(item != nullptr ? m_list->itemWidget(item) : nullptr, false);
        }
        syncRowHover();
    });
    m_selectionTools = new QWidget(this);
    m_selectionTools->setObjectName(QStringLiteral("modSelectionTools"));
    m_selectionTools->hide();
    outer->addWidget(m_selectionTools);
    m_contentHost = new QWidget(this);
    m_contentHost->setMinimumWidth(0);
    auto *contentLayout = new QHBoxLayout(m_contentHost);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addWidget(m_list);
    m_contentHost->installEventFilter(this);
    outer->addWidget(m_contentHost, 1);

    // ---------- 列表下面那条"这一份是什么"----------
    //
    // 原理图 / 结构那一页要给元数据、方块统计和那句版本兼容提示。
    // **这一层算不出那些**（要读 gzip NBT、要认 Litematica 的位打包，全在 src/ 里；
    // 这一层不 include src/）—— 它只摆一格标签，选中一条文件时发 infoRequested()，
    // 由接的人算好再 `setInfoText()` 递回来。
    //
    // 默认藏着：别的七页一个像素都不变（`setInfoPanelEnabled(true)` 才露）。
    m_info = new QLabel(this);
    m_info->setObjectName(infoLabelName());
    m_info->setWordWrap(true);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_info->setVisible(false);
    makeSecondary(m_info);
    auto *infoRow = new QHBoxLayout;
    infoRow->addWidget(m_info, 1);
    m_infoDetails = new QPushButton(tr("方块详情"), this);
    m_infoDetails->setObjectName(QStringLiteral("instanceSchematicDetails"));
    Theme::markButton(m_infoDetails, Theme::ButtonKind::Neutral);
    m_infoDetails->hide();
    infoRow->addWidget(m_infoDetails);
    outer->addLayout(infoRow);
    connect(m_infoDetails, &QPushButton::clicked, this, [this] {
        if (!m_infoPath.isEmpty()) emit infoDetailsRequested(m_infoPath);
    });

    // 连点合并：在一个文件上停 150 ms 才真去读。读一份大投影要几百毫秒（gzip 还要
    // 起一次 PowerShell 解压），用户快速翻行时不该每一行都读一遍。
    m_infoTimer = new QTimer(this);
    m_infoTimer->setSingleShot(true);
    m_infoTimer->setInterval(150);
    connect(m_infoTimer, &QTimer::timeout, this, [this] {
        if (!m_infoPanelEnabled || m_infoPath.isEmpty()) {
            return;
        }
        emit infoRequested(m_infoPath);
    });

    // ---------- 底下那条**悬浮**操作条----------
    //
    // 父控件是 **`m_list` 本身**、**不进任何布局**：进了布局就变成"列表下面另起一行"，
    // 而这一条要**悬浮在列表下缘**。位置由 repositionSelectionBar() 算，重算的时机是
    // viewport 的 Resize（见 eventFilter）。
    //
    // 父控件必须是 `m_list`，不能是 `m_list->viewport()`：挂在 viewport 底下的话，
    // 滚动滚轮时这一条会跟着跑。根子在 Qt 那边：
    // `QAbstractItemViewPrivate::scrollContentsBy()`（`qabstractitemview_p.h`）滚的是
    // `viewport->scroll(dx, dy)`，而 **`QWidget::scroll()` 是"连孩子一起挪"的**
    // （`QWidgetPrivate::scroll_sys()` 第一句就是 `scrollChildren(dx, dy)`，Qt 6 里那句
    // 注释写着 QTBUG-138381）—— 于是每滚一格，这条浮层就跟着挪一格，滚到头就飘出视口。
    // 挂在 `m_list` 底下就**从结构上没有这个毛病**：被滚的只有 viewport 那一棵，
    // 而它是 `m_list` 的**兄弟**，不是它的孩子。
    // ⚠ 代价是坐标要多加一个 `viewport->pos()`（见 repositionSelectionBar），因为
    // 现在算出来的还是 viewport 坐标系里的位置。压在上面靠 `raise()`（applyState 里）。
    //
    // NOTE: 它**一直建着**（哪怕这一页没开批量）—— 显不显由 updateSelectionBar() 说了算。
    // 建/不建会让"这一页有没有这一条"变成第二处知识，而且 `setBatchMode(true)` 是
    // 在构造之后才叫的（`InstanceSettingsDialog` 那一轮 setter），那时候再补建控件
    // 就得把这一整段搬进一个函数里。
    m_selectionBar = new QWidget(m_list);
    m_selectionBar->setObjectName(selectionBarName());
    m_selectionBar->setVisible(false);
    // 底色：**压在列表上面的一块浮层**，所以走 `Part::Panel`（那一档的出厂色比窗口底色再深
    // 一档，正是"从底下那堆东西里分出来"）。**不写死颜色** —— 主题一换跟着走。
    // 为什么不是 `Part::Card`：那一档的出厂色就是 `Base`，而列表的底**也是** `Base` ——
    // 同色等于这条操作条根本看不出来（只有按钮浮在行上面）。Panel 才是"压上来的一层"。
    m_selectionBar->setAutoFillBackground(true);
    m_selectionBar->setPalette([]{ // 现算一份：`QApplication::palette()` 是当前主题那一份
        QPalette barPalette = QApplication::palette();
        barPalette.setColor(QPalette::Window, Theme::partBackground(Theme::Part::Panel));
        return barPalette;
    }());
    {
        auto *bar = new QHBoxLayout(m_selectionBar);
        bar->setContentsMargins(kSelectionBarPadding, kSelectionBarPadding,
                                kSelectionBarPadding, kSelectionBarPadding);
        bar->setSpacing(kSelectionBarSpacing);
        m_selectionCount = new QLabel(m_selectionBar);
        m_selectionCount->setObjectName(selectionCountName());
        m_selectionCount->setText(selectionCountText(0));
        // 这一格**要多少给多少**：这一条**不进布局、宽度是
        // repositionSelectionBar() 现算的**，窗口窄的时候得压得住 —— 而承接压缩的该是
        // 那七颗按钮（它们少几个像素也就是边距窄一点），不是这句「已选择 N 个文件」
        // （它被压了就是少一个字，屏幕上是「已选择 2 个文」，而且 Qt **不给 QLabel
        // 省略号**，看着像写错了）。`Fixed` 就是"按 sizeHint 给，不多也不少"。
        // NOTE: 七颗的 `minimumWidth` 已经在下面那一段放到 0 了（不给就压不动）。
        m_selectionCount->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        m_selectionCount->setMinimumWidth(0);
        bar->addWidget(m_selectionCount);

        for (int i = 0; i < selectionActionCount(); ++i) {
            const auto action = static_cast<SelectionAction>(i);
            auto *button = new QPushButton(selectionActionText(action), m_selectionBar);
            button->setObjectName(selectionButtonName(action));
            // 「窄窗口下这一条怎么让位」：给每一颗一个**按自己那两个字的
            // 宽度算出来的下限**（`QPushButton::minimumSizeHint()` 默认把 `padding: 6px`
            // 那 12 + 边框 2 也算进去，比这个还大 —— 七颗一加起来就顶得这一条比视口还宽，
            // 于是布局开始按比例压，**被压掉的是最左边那句「已选择 N 个文件」**，屏幕上
            // 看着像"字写错了"）。压到"刚好放得下这两个字"为止就有了下限，再窄也不会
            // 把字截掉，而多出来的那点宽度按 Qt 的规矩平分给七颗。
            button->setMinimumWidth(button->fontMetrics().horizontalAdvance(
                                        selectionActionText(action)) +
                                    14);  // 6*2 padding + 1*2 border —— 和 Theme 里那份 QSS 一致
            // 三档颜色的分工：**更新 = 主题色那一颗**（它是这条上"正经要做的事"），
            // **删除 = 红的那一颗**（危险，"点下去会没"），其余一般性（黑字黑边）。
            // 走 `Theme::markButton()` —— 全仓按钮外观的唯一出处，不许自己写颜色。
            Theme::markButton(button, action == SelectionAction::Update  ? Theme::ButtonKind::Accent
                                      : action == SelectionAction::Delete ? Theme::ButtonKind::Danger
                                                                          : Theme::ButtonKind::Neutral);
            switch (action) {
            case SelectionAction::Update:
                button->setToolTip(selectionUpdateTip());
                connect(button, &QPushButton::clicked, this, [this] {
                    emit batchUpdateRequested(selectedPathList());
                });
                break;
            case SelectionAction::Disable:
            case SelectionAction::Enable: {
                const bool disable = action == SelectionAction::Disable;
                button->setToolTip(selectionToggleTip(disable));
                connect(button, &QPushButton::clicked, this, [this, disable] {
                    const QStringList paths = selectedTogglePaths(disable);
                    requestToggle(paths, disable);
                });
                break;
            }
            case SelectionAction::SelectAll:
                button->setToolTip(selectionPickTip(action));
                // 全选 = `selectAll()`：它选的就是"列表里现在有的那些行"，而列表里现在有的
                // 正是**筛出来的那些**（见 rebuildRows）。选择一变，
                // itemSelectionChanged 会把账和这一条一起刷新。
                connect(button, &QPushButton::clicked, m_list, &QListWidget::selectAll);
                break;
            case SelectionAction::InvertAll:
                button->setToolTip(selectionPickTip(action));
                connect(button, &QPushButton::clicked, this, [this] {
                    if (m_list->count() == 0) return;
                    // One selection-model transaction emits one change. Updating
                    // each row separately recomputed the entire action panel N times.
                    const QItemSelection range(m_list->model()->index(0, 0),
                        m_list->model()->index(m_list->count() - 1, 0));
                    m_list->selectionModel()->select(range,
                        QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
                });
                break;
            case SelectionAction::ClearAll:
                button->setToolTip(selectionPickTip(action));
                connect(button, &QPushButton::clicked, m_list, &QListWidget::clearSelection);
                break;
            case SelectionAction::Delete:
                button->setToolTip(tr("删除选中文件，执行前确认。"));
                connect(button, &QPushButton::clicked, this, [this] {
                    emit batchDeleteRequested(selectedPathList());
                });
                break;
            case SelectionAction::Count:
                break;
            }
            m_selectionButtons.append(button);
            bar->addWidget(button);
        }
    }
    // 这一条**常驻**（对话框一开就在那儿）⇒ 主题换了得自己重算底色（`Theme::follow` 那段
    // NOTE 说得很清楚：现建的那些不用登记，常驻的必须登记）。
    Theme::follow(m_selectionBar, [this] {
        QPalette barPalette = QApplication::palette();
        barPalette.setColor(QPalette::Window, Theme::partBackground(Theme::Part::Panel));
        m_selectionBar->setPalette(barPalette);
    });

    // ---------- 空 / 用不上时那一段话 ----------
    m_notice = new QLabel(this);
    m_notice->setObjectName(noticeLabelName());
    m_notice->setWordWrap(true);
    m_notice->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    makeSecondary(m_notice);
    outer->addWidget(m_notice, 1);

    // 目录统计那一趟的节拍器（见 fillDirectoryStats / m_statLabels 那段）。单发 + 0 毫秒：
    // 每排一次就在**下一个事件循环**里干一行，干完自己再排下一次。
    m_statTimer = new QTimer(this);
    m_statTimer->setSingleShot(true);
    m_statTimer->setInterval(0);
    connect(m_statTimer, &QTimer::timeout, this, &InstanceFilesPage::fillDirectoryStats);

    fillStateButtons();

    // 摆到"目录还没接上"那一档：构造函数里 setDirectory() 还没被叫过，这会儿既没东西可列、
    // 也没得可点，露出一个空框比说一句话更让人以为坏了。
    applyState();
}

void InstanceFilesPage::setMode(Mode mode) {
    m_mode = mode;
    refresh();
}

void InstanceFilesPage::setRowIconsVisible(bool visible) {
    m_rowIconsVisible = visible;
    refresh();
}

void InstanceFilesPage::setCanToggle(bool canToggle) {
    m_canToggle = canToggle;
    refresh();
}

void InstanceFilesPage::setCanDelete(bool canDelete) {
    m_canDelete = canDelete;
    refresh();
}

void InstanceFilesPage::setCanEdit(bool canEdit) {
    m_canEdit = canEdit;
    refresh();
}

void InstanceFilesPage::setBatchMode(bool batchMode) {
    if (m_batchMode == batchMode) {
        return;
    }
    m_batchMode = batchMode;
    // 多选 ⇄ 单选（见 .hpp 里 setBatchMode 那段）。**Ctrl+A 通不通就由这一句说了算**：
    // `QAbstractItemView::keyPressEvent()` 里那一发 `QKeySequence::SelectAll` 是**照发不误**的
    // （判据只有 `selectionMode() != NoSelection`），真正拦住它的是 `selectAll()` 自己那句
    // "`SingleSelection` 什么都不做" —— 单选的视图上那一发就是**什么都不发生**，这正是
    // 不开批量的那几页该有的样子。
    //
    // **用 `MultiSelection`，不用 `ExtendedSelection`**。
    // 这两种模式的分别**不是"能不能多选"，而是"要不要按住 Ctrl"**：
    // · `ExtendedSelection` —— 不按修饰键点第二行是 `ClearAndSelect`，**把上一条抹掉**；
    // 要加选得按住 Ctrl（或者 Shift 拉一段）。这里要的不是这个。
    // · `MultiSelection` —— 每一发左键都是 `Toggle`（见 Qt 的
    // `QAbstractItemViewPrivate::multiSelectionCommand()`：按下时给
    // `Toggle|selectionBehaviorFlags()`，已在选中的那一条等到**松开**才翻），
    // 也就是**点一下选中、再点一下取消、点别的是加进去**，一个修饰键都不用。
    // 代价：Shift 拉一段不再有用；点在**空白处**也不清选择
    // （`multiSelectionCommand` 对无效行回 `NoUpdate`）—— 清空交给操作条上那颗「取消」。
    m_list->setSelectionMode(batchMode ? QAbstractItemView::MultiSelection
                                       : QAbstractItemView::SingleSelection);
    if (!batchMode) {
        // 关掉的时候把选择清干净：不清的话 `m_selectedPaths` 里压着一份"用户根本看不见的
        // 选中"（那一列按钮也全没了），哪一天再开回来会突然冒出来几条亮着的行。
        m_list->clearSelection();
        m_selectedPaths.clear();
    }
    // 详情按钮的建/不建是 makeRow 说了算 ⇒ 得重列一遍（这一趟不读盘，只重建行）。
    rebuildRows();
}

void InstanceFilesPage::setEmptyText(const QString &text) {
    m_emptyText = text;
    applyState();
}

void InstanceFilesPage::setImportText(const QString &text, const QString &tip) {
    if (m_import == nullptr) {
        return;
    }
    m_import->setText(text);
    m_import->setToolTip(text.isEmpty() ? QString() : tip);
    m_import->setVisible(!text.isEmpty());
}

void InstanceFilesPage::setJoinText(const QString &text, const QString &tip) {
    if (m_join == nullptr) {
        return;
    }
    m_join->setText(m_worldLayout && !text.isEmpty() ? tr("加入世界") : text);
    m_join->setVisible(!text.isEmpty());
    m_joinTip = text.isEmpty() ? QString() : tip;
    updateJoinEnabled();
}

void InstanceFilesPage::updateJoinEnabled() {
    if (m_join == nullptr) {
        return;
    }
    // 「加入」要有**一个明确的世界**才谈得上 —— 那只有选中那一行能给（这一页不是批量那四页，
    // 选择模式是单选）。灰着比"按下去什么也不发生"诚实（和服务器列表那两颗同一条规矩）。
    //
    // NOTE: 这一页上它只在**存档**那一页露着（见 setJoinText()），而别的几页上它也照旧跟
    // 着选中状态走 —— 藏着的按钮 enabled 是什么都不影响屏幕上任何东西。
    const bool hasWorld = !currentPath().isEmpty();
    m_join->setEnabled(hasWorld);
    if (!m_joinTip.isEmpty()) {
        m_join->setToolTip(hasWorld ? m_joinTip : joinSelectFirstHint());
    }
}

void InstanceFilesPage::setBatchText(const QString &text, const QString &tip) {
    if (m_batch == nullptr) {
        return;
    }
    m_batch->setText(text);
    m_batch->setToolTip(text.isEmpty() ? QString() : tip);
    m_batch->setVisible(!text.isEmpty());
}

void InstanceFilesPage::setDownloadText(const QString &text, const QString &tip) {
    if (m_download == nullptr) {
        return;
    }
    m_download->setText(text);
    m_download->setToolTip(text.isEmpty() ? QString() : tip);
    // 空串 = 这一页没有这颗按钮（见 .hpp）。**藏起来**而不是"不建"：这几页是同一段循环
    // 搭出来的，建/不建会让"这一页该有什么"散成两处知识。露不露由 isVisibleTo() 量得出来，
    // 用户看到的也是同一件事。
    m_download->setVisible(!text.isEmpty());
}

void InstanceFilesPage::setDirectory(const QString &dir) {
    m_directory = dir;
    // NOTE: **欠账这件事只跟"看不看得见"有关，跟数据安不安全无关**：那一份份后到的数据
    // （图标 `m_icons` / 能点开的身份 `m_resourceTips` / 可更新的名单 `m_outdatedPaths`）
    // 全都是**先记账、再找行**（见 setIconForPath() 那几段的 NOTE）—— 这一页没有行的时候
    // 账照样记着，下一趟 refresh() 摆出来的那一行自然是对的。
    if (!isCurrentPage()) {
        m_refreshPending = true;
        return;
    }
    refresh();
}

void InstanceFilesPage::setStructureSources(const QList<StructureSource> &sources) {
    m_structureSources = sources;
    // 整块重建那几个下拉框（排数会变：这个实例上有几个存档有结构，是**每一趟现算**的）。
    // 那些控件都挂在这一格底下 ⇒ 删子控件就够了（`m_structureCombos` 里那几把是裸指针，
    // 下面这一句之后一个都不作数，必须一起清掉）。
    m_structureCombos.clear();
    m_structureTitles.clear();
    if (m_structureLayout != nullptr) {
        while (QLayoutItem *item = m_structureLayout->takeAt(0)) {
            if (QWidget *w = item->widget()) {
                // **先藏再排删除**：`deleteLater()` 要等下一趟事件循环，而这一会儿那一排
                // 还挂在原来的位置上（几何没变）——屏幕上就是"上一趟那一排闪一下"。
                w->hide();
                w->deleteLater();
            }
            delete item;
        }
    }
    // 两个排头占的宽度**统一成最宽的那一个**（下面每一格都拿这一份），这样两排的下拉框
    // 左边对得齐 —— 一列下来歪一格是最显眼的那种不齐。
    // 这一句量的是**递进来的那些 title**，不是一个写死的词：「存档」/「投影」当前同宽
    // （两格全角），但"排头一样宽"是这两句话凑巧的结果，不是这套排版的保证 ——
    // 照当前那两个字去量的话（`horizontalAdvance(structureWorldTitle())`），哪一天某一排
    // 的排头多一个字，它那一排的下拉框就整体右移一格，屏幕上就是两排没对齐。
    // `makeSecondary()` 改的只是颜色、字号没动 ⇒ 拿这一页的 fontMetrics() 量是准的。
    int structureTitleWidth = 0;
    const QFontMetrics titleMetrics = fontMetrics();
    for (const StructureSource &source : m_structureSources) {
        structureTitleWidth =
            qMax(structureTitleWidth, titleMetrics.horizontalAdvance(source.title));
    }
    for (int row = 0; row < m_structureSources.size(); ++row) {
        const StructureSource &source = m_structureSources.at(row);
        auto *line = new QWidget(m_structureRows);
        auto *layout = new QHBoxLayout(line);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        auto *title = new QLabel(source.title, line);
        title->setObjectName(structureTitleName(row));
        makeSecondary(title);
        title->setMinimumWidth(structureTitleWidth + 4);
        layout->addWidget(title);
        auto *combo = new QComboBox(line);
        combo->setObjectName(structureComboName(row));
        // 一个来源一行都没有 ⇒ 摆一行**灰的**，写「无可用的结构」。它**不是**一个
        // 可选中的值：选中它就等于没选，
        // 于是这一排对下面那份列表一个条目都不贡献。
        if (source.labels.isEmpty()) {
            combo->addItem(structureNoneText());
            combo->setEnabled(false);
        } else {
            combo->addItems(source.labels);
        }
        // 换一个来源 = 换一份要列的东西 ⇒ 当场重列（和「刷新」同一件事，只是目录换了）。
        // NOTE: 填 items 那一会儿**不发这一发**（那时 row 还没进 m_structureCombos，
        // 下面这个 lambda 里那句查找自然落空 —— 但那不足以当防护，所以填之前
        // 先把 connect 摆好是**错的**）。这里的次序是：先填、再 connect。
        connect(combo, &QComboBox::currentIndexChanged, this, [this, row](int) {
            if (row >= 0 && row < m_structureCombos.size()) {
                refresh();
            }
        });
        layout->addWidget(combo, 1);
        m_structureLayout->addWidget(line);
        m_structureTitles.append(title);
        m_structureCombos.append(combo);
    }
    // 摆完当场列一遍（和 setDirectory 同一个口径：看不见的那几页先欠着）。
    if (!isCurrentPage()) {
        m_refreshPending = true;
        return;
    }
    refresh();
}

QString InstanceFilesPage::selectedStructurePath(int row) const {
    if (row < 0 || row >= m_structureSources.size() || row >= m_structureCombos.size()) {
        return QString();
    }
    const StructureSource &source = m_structureSources.at(row);
    const QComboBox *combo = m_structureCombos.at(row);
    if (combo == nullptr || source.paths.size() != source.labels.size()) {
        return QString();
    }
    const int index = combo->currentIndex();
    // 「无可用的结构」那一行：labels 本来就是空的 ⇒ 上面那一句已经返回了。
    if (index < 0 || index >= source.paths.size()) {
        return QString();
    }
    return source.paths.at(index);
}

QStringList InstanceFilesPage::scanDirectories() const {
    if (m_structureSources.isEmpty()) {
        // 别的七页：就是那一个目录。
        return m_directory.isEmpty() ? QStringList() : QStringList{m_directory};
    }
    QStringList dirs;
    for (int row = 0; row < m_structureSources.size(); ++row) {
        const QString path = selectedStructurePath(row);
        if (!path.isEmpty() && !dirs.contains(path)) {
            dirs.append(path);
        }
    }
    return dirs;
}

bool InstanceFilesPage::isCurrentPage() const {
    // 往上找**最外面**那个堆叠容器，看它现在摆的是不是我（或者我的祖先）。
    // - 取最外面那一个：问的是"用户在实例设置里看的是哪一页"，那一层就是
    // `InstanceSettingsDialog::m_stack`。往里那几层（`save` / `config` 自己套的两格堆叠
    // —— 列表 / 编辑器）不是这个问题：取最近的话，摆着「概览」时那两页**照样**会被判成
    // "当前"，欠账那一套对它们就白设了（而这两个目录正是行最多的那几个之一）。
    // - 判据是"当前那一格是不是我或我的祖先"：这一页外面还套着一层 `QScrollArea`
    // （见 `InstanceSettingsDialog::addPage()`），拿 `==` 比会永远为假。
    // 不认 `isVisible()`：offscreen 下（三个验证工程）那个函数**恒为 false**，在"还没
    // show 出来"的窗口里也恒为 false —— 拿它当判据等于这一页永远不列。
    const QStackedWidget *stack = nullptr;
    for (const QWidget *w = this; w != nullptr; w = w->parentWidget()) {
        if (const auto *found = qobject_cast<const QStackedWidget *>(w)) {
            stack = found;
        }
    }
    if (stack == nullptr) {
        return true;  // 不在堆叠容器里：没有"当前页"这回事（单独用这个控件的那一档）
    }
    const QWidget *current = stack->currentWidget();
    return current == this || (current != nullptr && current->isAncestorOf(this));
}

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
    // 来说（见 makeRow 里那个箭头），不靠这一颗筛子。
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
    // 这份名单**每一行都在吃**（那一行名字后面的 ↑ 就是它，见 makeRow
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

void InstanceFilesPage::setUnavailable(const QString &reason) {
    m_unavailable = reason;
    applyState();
}

QString InstanceFilesPage::formatSize(qint64 bytes) {
    // 单位一律走字面量而不是 tr()：它们是量纲不是话（换语言也不该跟着换），而且验证工程
    // 按这几句核对 —— 那几条断言得有一个**不会随语言变**的基准。
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0) {
        return QStringLiteral("%1 KB").arg(kb, 0, 'f', 1);
    }
    const double mb = kb / 1024.0;
    if (mb < 1024.0) {
        return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
    }
    return QStringLiteral("%1 GB").arg(mb / 1024.0, 0, 'f', 1);
}

void InstanceFilesPage::rememberScrollAnchor() {
    if (m_list == nullptr) {
        return;
    }
    // 空表（刚 clear 过、或者这一页本来就没东西）**不动手** —— 见 .hpp 里那段：这一句会
    // 被叫两次（refresh() 在 clear 之前、rebuildRows() 在它自己的 clear 之前），第二次
    // 看见的正是空表，记下去的话就把第一份真锚点抹了。
    if (m_list->count() == 0) {
        return;
    }
    m_scrollAnchor = captureScrollPosition();
}

InstanceFilesPage::ScrollPosition InstanceFilesPage::captureScrollPosition() const {
    ScrollPosition position;
    if (m_list == nullptr || m_list->count() == 0) {
        return position;
    }
    // "最上面那一行" = 第一行**至少露出一个像素**的。用 `itemAt(0, 0)` 不行：视口最上面
    // 那一个像素可能落在行与行之间的空档里（那一句会回 nullptr，锚点就没了）。
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (item == nullptr) {
            continue;
        }
        const QRect rect = m_list->visualItemRect(item);
        // 排在前面的行（滚上去的那些）整个在视口上方 ⇒ `bottom() <= 0`。
        if (rect.bottom() <= 0) {
            continue;
        }
        position.identity = identityPath(item->data(pathRole()).toString());
        // 它当时离视口顶端多少像素。**按行滚动**（这一页用的就是 Qt 默认的
        // `ScrollPerItem`）时行总是对齐在顶端 ⇒ 这个数恒为 0，还原走的是"摆回那一行"那条
        // 路（见 restoreScrollAnchor 里那段 ★★）。
        position.delta = rect.top();
        return position;
    }
    // 一行都没露（视口比一行还矮，或者列表是空的）：没有位置。
    return position;
}

InstanceFilesPage::ScrollPosition InstanceFilesPage::scrollPosition() const {
    // 现算，不读 `m_scrollAnchor` —— 见 .hpp 里这一段（用户点详情按钮那一刻它多半空着）。
    return captureScrollPosition();
}

void InstanceFilesPage::setScrollPosition(const ScrollPosition &position) {
    // 只写这一位、**不**顺手去摆滚动条：这一刻这一页还没列过（调用点在 showPage()
    // 之前），表是空的，摆也没处摆。真正摆那一下仍旧是 restoreScrollAnchor() 的事
    // （rebuildRows() 末尾那个 0 毫秒定时器里），它读的就是这一位。
    m_scrollAnchor = position;
}

void InstanceFilesPage::restoreScrollAnchor() {
    if (m_list == nullptr || m_scrollAnchor.identity.isEmpty()) {
        return;
    }
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (item == nullptr) {
            continue;
        }
        if (identityPath(item->data(pathRole()).toString()) != m_scrollAnchor.identity) {
            continue;
        }
        // 那一行**现在**在哪儿（此刻滚动条还在重列之后的位置上，通常是 0）。
        QScrollBar *bar = m_list->verticalScrollBar();
        if (m_list->verticalScrollMode() == QAbstractItemView::ScrollPerItem) {
            bar->setValue(row);
        } else {
            bar->setValue(bar->value() + m_list->visualItemRect(item).top() -
                          m_scrollAnchor.delta);
        }
        break;
    }
    // 锚点是一次性的：这一趟还完了就丢掉。留着的话，下面任何一次"没有重建列表的重排"
    // 都可能莫名其妙地把它再还一遍（比如用户自己滚了之后又触发一次 applyState）。
    m_scrollAnchor = ScrollPosition();
}

void InstanceFilesPage::refresh() {
    // 这一趟真的列了 ⇒ `setDirectory()` 那一笔欠账清了（**摆在最前**：下面任何一条早退
    // 路都不该把这一位留在"还欠着"上 —— 那会让判据以为这一页没列过）。
    m_refreshPending = false;
    if (m_list == nullptr) {
        return;
    }
    rememberScrollAnchor();
    // 上面那一排那一格写"A 在看哪个目录"。**在目录存在性判断之前**就写：目录还不存在
    // （第一次开 mods/ 之前它本来就不在盘上）也得让人看见路径 —— 那时候这一页正是
    // "这个目录是空的/不存在"，路径恰恰是最该看见的东西。
    // 分隔符统一成 Windows 那种反斜杠：这是给用户看的字，不是喂给接口的路径。
    //
    // 这一趟要读的目录**可能不止一个**（「结构/原理图」那一页那两个来源，
    // 见 scanDirectories()）。那几排下拉框自己就写着这一页在看哪些来源，「路径」这一格
    // 于是把**它们现在选中的那几个**一起报出来（一眼看得全"这一页正在列哪几个文件夹"）。
    // 别的七页永远只有一个 ⇒ 那边走的就是这一条。
    const QStringList dirs = scanDirectories();
    const QString shownDir = QDir::toNativeSeparators(dirs.join(QStringLiteral("   /   ")));
    m_pathLabel->setText(shownDir);
    // 装不下时是从中间省略的，两头那几段还能看；整条路径靠悬停提示给（见构造里那段 NOTE）。
    m_pathLabel->setToolTip(shownDir);
    // clear() 会连同 setItemWidget() 挂上去的那些行控件一起删掉（列表持有它们）。
    // **这一发也得把信号挡着**（真机踩到）。`clear()` 会发一次
    // `itemSelectionChanged`，而那一发接的是「重算 m_selectedPaths」—— 此刻表已经空了，
    // 于是**用户的选择在这一句上就被抹平**；紧接着 rebuildRows() 想按路径把选择还回去，
    // 手里那份账却已经是空的，什么都还不上。屏幕上：点「启用」把一条改回来了，那一行
    // 却没选上、底下那条悬浮操作条也一起消失（真机 + 日志双重确认）。
    // `rebuildRows()` 自己那段 blocker 挡的是**它那一次** clear —— 挡不住这里这一发，
    // 这正是「注释说防住了、其实漏在外面」那一类。两处都得挡。
    {
        const QSignalBlocker blocker(m_list);
        m_list->clear();
    }
    // 统计那一趟跟着作废（见 m_statLabels 那段）：上面这一句把上一趟那些标签一起删了，
    // 队列里还没轮到的那几个路径指向的是已经不存在的控件。**先清队列再清表**，而且
    // 两样都要清 —— 只清表的话，下一趟的 fillDirectoryStats() 会拿着一个悬空路径去
    // 找行（找不到，白跑一趟）。
    m_statQueue.clear();
    m_statLabels.clear();
    // 先当空的，下面每加一条翻过来 —— 这样"目录不存在"和"目录是空的"自然走同一条路。
    m_empty = true;
    m_entries.clear();

    // 一个都不存在（或者压根没有要读的目录）⇒ 走下面这条早退路。
    bool anyExists = false;
    for (const QString &one : dirs) {
        if (QDir(one).exists()) {
            anyExists = true;
            break;
        }
    }
    if (!anyExists) {
        m_filteredEmpty = false;
        m_selectedPaths.clear();
        applyState();
        return;
    }

    // **这一层循环服务「结构/原理图」那一页**（见 scanDirectories()）。
    // 别的七页 dirs 里只有一个元素 ⇒ 走的是同一条路。一个来源不见了（用户把那个存档
    // 删了）就跳过它接着列别的 —— 不要因为一个来源不在盘上就把整页判成空的。
    for (const QString &one : dirs) {
        const QDir dir(one);
        if (!dir.exists()) {
            continue;
        }
        // 不设 QDir::Hidden：点开头的文件（.DS_Store 那一类）不该占一行。而 `.disabled` 是
        // **后缀**不是前缀，所以被禁用的那些照旧列得出来。
        const QFileInfoList entries =
            dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                              QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

        for (const QFileInfo &info : entries) {
            const bool isDir = info.isDir();
            if (m_mode == Mode::DirsOnly && !isDir) {
                // 存档那一页：saves 底下的 level.dat / session.lock 那些不是存档，一个都不列。
                continue;
            }
            if (m_mode == Mode::FilesOnly && isDir) {
                continue;
            }

            const QString raw = info.fileName();
            // 禁用：只有"游戏会自己扫的那三个目录"里的**文件**才有这一档。目录不给 —— 把一个
            // 文件夹改名成 `xxx.disabled` 会连它里面的东西一起从游戏眼里消失，那不是
            // "禁用这个包"该有的效果（而且 mods/ 下那些目录根本不是 mod，比如 mcef-libraries）。
            const bool disabled = m_canToggle && !isDir &&
                                  raw.endsWith(disabledSuffix(), Qt::CaseInsensitive);
            const QString shown = disabled ? raw.left(raw.size() - disabledSuffix().size()) : raw;
            const QString path = info.absoluteFilePath();
            // 「这一条叫什么」——**盘上的名字**去掉那个后缀，被禁用的再挂上四个字。
            // 条目不给文字（见下），所以这一格得自己算出来，否则那四个字会**整条从界面上
            // 消失** —— 名字后面不再有任何东西说明这一条是关着的，而灰掉的名字色在浅色
            // 主题下几乎看不出来。
            const QString displayName = disabled ? shown + disabledMark() : shown;

            // 条目**故意不给文字**。`QListWidgetItem` 上的文字是**视图代理画的**，而这一行
            // 的内容是一个用 setItemWidget() 摆上去的真控件；那个控件是**透明的**（要这样，
            // 选中高亮才从底下透出来），所以代理画的字会从它底下露出来 —— 屏幕上是
            // **同一个名字画两遍**，一行字的位置和控件里那个名字差着半行，看着像重影。
            // 文字由 makeRow() 那一格全权负责（名字在 nameLabelName() 上，那才是"这一条
            // 叫什么"的唯一出处）。
            Entry entry;
            entry.displayName = displayName;
            entry.raw = raw;
            entry.path = path;
            entry.isDir = isDir;
            entry.disabled = disabled;
            entry.size = info.size();
            entry.modified = info.lastModified();
            m_entries.append(entry);
            m_empty = false;
        }

    }

    // 建行 + 过筛选 + 摆可见性 + 起统计定时器，全在那一处（见 .hpp 里 rebuildRows 那段）。
    rebuildRows();
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

QLabel *InstanceFilesPage::pathLabel() const {
    // 只在**这里**把 ElidedLabel 当成 QLabel 交出去（见 .hpp 里那段）：那一格的确是
    // ElidedLabel，但调用方要的只有 setText / text，不必知道它是怎么画出来的。
    return m_pathLabel;
}

// ---------- 列表下面那条"这一份是什么"----------
//
// 这一层只记账、只摆字（理由见 .hpp 里那一段）：读 gzip NBT、认 Litematica 的位打包
// 都是 src/ 的事，由接的人算好递回来。

void InstanceFilesPage::setInfoPanelEnabled(bool enabled) {
    m_infoDetails->setVisible(enabled);
    if (m_infoPanelEnabled == enabled) {
        return;
    }
    m_infoPanelEnabled = enabled;
    if (!enabled) {
        // 关掉 = 这一页一个字都不留（不是只藏起来：再打开时得从"正在读取…"重新来）。
        if (m_infoTimer != nullptr) {
            m_infoTimer->stop();
        }
        m_infoPath.clear();
        if (m_info != nullptr) {
            m_info->clear();
            m_info->setToolTip(QString());
            m_info->setVisible(false);
        }
        return;
    }
    updateInfoPanel();
}

void InstanceFilesPage::setInfoText(const QString &path, const QString &text,
                                    const QString &toolTip) {
    if (m_info == nullptr || !m_infoPanelEnabled) {
        return;
    }
    // 过期答案（理由见 .hpp）：用户已经换到别的行上了，这一份讲的不是屏幕上选着的那条。
    if (path != m_infoPath) {
        return;
    }
    m_info->setText(text);
    m_info->setToolTip(toolTip);
    m_info->setVisible(!text.isEmpty());
}

void InstanceFilesPage::updateInfoPanel() {
    if (m_info == nullptr || !m_infoPanelEnabled) {
        return;
    }
    // 判据：**正好选了一条、而且它是文件**（选两个、选一个目录、一条没选 —— 都不谈"这一份"）。
    QString path;
    bool isDir = false;
    if (m_list != nullptr) {
        const QList<QListWidgetItem *> selected = m_list->selectedItems();
        if (selected.size() == 1 && selected.first() != nullptr) {
            path = selected.first()->data(pathRole()).toString();
            const Entry *entry = entryForPath(path);
            isDir = entry != nullptr && entry->isDir;
        }
    }
    m_infoDetails->setEnabled(!path.isEmpty() && !isDir);
    if (path.isEmpty() || isDir) {
        if (m_infoTimer != nullptr) {
            m_infoTimer->stop();
        }
        m_infoPath.clear();
        m_info->clear();
        m_info->setToolTip(QString());
        m_info->setVisible(false);
        return;
    }
    // 已经在为这一条等答案 / 已经摆着它的答案 ⇒ 不重复读盘（重列、开筛选都会走到这儿）。
    if (path == m_infoPath) {
        return;
    }
    m_infoPath = path;
    m_info->setText(infoPendingText());
    m_info->setToolTip(QString());
    // 列表本身藏着的时候（目录空 / 筛完一条不剩）不露 —— 那种画面下不可能有选中的一行，
    // 这里只是兜底：applyState() 也会把它收起来。
    m_info->setVisible(true);
    if (m_infoTimer != nullptr) {
        m_infoTimer->start();
    }
}

void InstanceFilesPage::setWorldLayout(QPushButton *backupButton) {
    if (m_worldLayout) return;
    m_worldLayout = true;
    setStateFilter(StateFilter::All);
    setCompactHeader(backupButton);
    m_search->setPlaceholderText(tr("搜索存档名称"));
    m_join->setText(tr("加入世界"));
    applyState();
}

void InstanceFilesPage::setCompactHeader(QPushButton *leadingAction) {
    if (m_compactHeader) return;
    m_compactHeader = true;
    auto *actions = qobject_cast<QHBoxLayout *>(m_pathRow->layout());
    actions->removeWidget(m_pathLabel);
    if (leadingAction) actions->insertWidget(0, leadingAction);
    actions->insertStretch(actions->indexOf(m_join) + 1, 1);
    auto *search = qobject_cast<QHBoxLayout *>(m_searchRow->layout());
    search->setStretchFactor(m_search, 2);
    search->addWidget(m_pathLabel, 1);
    auto *outer = qobject_cast<QVBoxLayout *>(layout());
    outer->removeWidget(m_pathRow);
    outer->insertWidget(0, m_pathRow);
    applyState();
}

void InstanceFilesPage::applyState() {
    const bool unavailable = !m_unavailable.isEmpty();
    const bool empty = !unavailable && m_empty;
    // 第四档：目录里有东西、但这一趟筛完一条不剩。
    const bool filtered = !unavailable && !empty && m_filteredEmpty;

    // 搜索框 / 状态那一栏：**这一页用不上、或者目录本来就空**的时候整块收起来（没有
    // 可筛的东西，露着两排空控件等于摆两个坏键）。筛完一条不剩时**留着** —— 用户正要
    // 靠它们把条件改回来，这时候把它们收掉是最糟的一刻。
    const bool filtersUseful = !unavailable && !m_empty;
    if (m_searchRow != nullptr) {
        // The mod header also owns installation/maintenance actions, useful even
        // before the first mod is installed.
        m_searchRow->setVisible(filtersUseful || (m_actionPanelEnabled && !unavailable));
    }
    if (m_stateRow != nullptr) {
        const bool choice = stateFilterVisible(StateFilter::Enabled) ||
            stateFilterVisible(StateFilter::Disabled) || stateFilterVisible(StateFilter::Update);
        m_stateRow->setVisible(filtersUseful && !m_worldLayout && (!m_compactHeader || choice));
    }
    // 那一排里**下拉框和四颗按钮互相显隐**（见 .hpp 里 m_stateRow 那段）：
    // 开了批量的页摆那四颗（连「显示」那个标签一起收掉 —— 那四颗自己就写着「全部(352)」），
    // 没开的页一丝不变。
    if (m_stateLabel != nullptr) {
        m_stateLabel->setVisible(!m_batchMode);
    }
    if (m_stateCombo != nullptr) {
        m_stateCombo->setVisible(!m_batchMode);
    }
    if (m_stateButtonsRow != nullptr) {
        m_stateButtonsRow->setVisible(m_batchMode);
    }

    m_pathRow->setVisible(!unavailable && !m_actionPanelEnabled);
    if (m_structureRows != nullptr) {
        m_structureRows->setVisible(!unavailable && !m_structureSources.isEmpty());
    }
    m_list->setVisible(!unavailable && !empty && !filtered);
    m_notice->setVisible(unavailable || empty || filtered);
    if (m_info != nullptr && (unavailable || empty || filtered)) {
        if (m_infoTimer != nullptr) {
            m_infoTimer->stop();
        }
        m_infoPath.clear();
        m_info->clear();
        m_info->setToolTip(QString());
        m_info->setVisible(false);
    }
    // 底下那条悬浮操作条跟着这一页的可见性走（它压在列表上，"列表都收起来了还浮着
    // 一条操作条"是最难解释的一种画面）。判据要的是**上面这一句刚摆好的** m_list 的
    // 可见性 ⇒ 必须排在这一句之后。
    updateSelectionBar();
    if (unavailable) {
        m_notice->setText(m_unavailable);
        return;
    }
    if (empty) {
        m_notice->setText(m_emptyText.isEmpty() ? emptyText() : m_emptyText);
        return;
    }
    // 筛完一条不剩：说清**是筛选把它筛没的**（不是目录空了），而且选着「可更新」时还要
    // 分清"还没查过"和"查过了没有"—— 那两件事的下一步完全不同。
    if (m_stateFilter == StateFilter::Update) {
        // NOTE: 开了批量那四页**够不着这儿**：（ 一整天里这一档被来回摆过两次，
        // 最后落在下面这条上）「可更新」那一颗在**没查过**的时候整颗收着
        // （见 stateFilterVisible() 里那段），而且 setStateFilter() 撞上一颗摆不出来
        // 的档会把条件退回「全部」⇒ 这一档在那一页上要么没被选中、要么已经查过了。
        // 所以下面这句在那儿仍是"够不着的兜底"，真正走得到它的是**摆下拉框的那几页**。
        //
        // NOTE: 查完之后走不到这儿 —— 「可更新(N)」在 N == 0 时**整颗收着**，而且它一旦
        // 收起来，选中的条件会被 ensureStateFilterVisible() 退回到「全部」。
        m_notice->setText(m_updateChecked ? updateNoneText() : updateNotCheckedText());
        return;
    }
    m_notice->setText(noMatchText());
}

// ---------- 底下那条悬浮操作条----------

const InstanceFilesPage::Entry *InstanceFilesPage::entryForPath(const QString &path) const {
    if (path.isEmpty()) {
        return nullptr;
    }
    for (const Entry &entry : m_entries) {
        if (entry.path == path) {
            return &entry;
        }
    }
    return nullptr;
}

QString InstanceFilesPage::currentPath() const {
    if (m_list == nullptr) {
        return QString();
    }
    // 用视图自己那份"现在选着什么"（不是 currentItem：那一格在**重列之后**可能还指着旧
    // 位置，而 selectedItems() 是重建时按路径还回去之后的结果，和 selectedPathList()
    // 同一个口径）。
    const QList<QListWidgetItem *> selected = m_list->selectedItems();
    if (selected.isEmpty() || selected.first() == nullptr) {
        return QString();
    }
    return selected.first()->data(pathRole()).toString();
}

QPushButton *InstanceFilesPage::stateButtonAt(StateFilter filter) const {
    const int index = static_cast<int>(filter);
    if (index < 0 || index >= m_stateButtons.size()) {
        return nullptr;
    }
    return m_stateButtons.at(index);
}

void InstanceFilesPage::fillDirectoryStats() {
    // 一趟只算一行，算完自己排下一趟。**不是** while 循环一次算完：
    // - 一个世界要递归遍历整棵树（几十万个文件是真有的），十几二十个世界连着算就是
    // 十几秒 —— 而这时候行**已经摆在屏幕上了**，用户看到的是一个点了没反应的界面；
    // - 一次一行的话，事件循环在中间转过去，界面一直活着，数字一行一行地冒出来
    // （"在算"这件事本身也就看得见了）。
    if (m_statQueue.isEmpty()) {
        return;
    }
    const QString path = m_statQueue.takeFirst();

    // 行可能已经不在屏幕上了：上面那句 takeFirst 之后、这一格找到之前，用户完全可能点了
    // 「刷新」或者删掉了一条。**找不到不是错误**（那一行已经不在了，没什么可写的）。
    const QPointer<QLabel> label = m_statLabels.value(path);
    if (!label.isNull()) {
        const qint64 bytes = directoryBytes(path);
        const QDateTime played = worldLastPlayed(path);
        // 拼接方式**逐字和上面那个文件行一样**（「12.3 MB · 12:00」）：
        // 同一张表里两种行的小字长得一样，才不会让人以为这一列有两个意思。
        QString meta = formatSize(bytes);
        if (played.isValid()) {
            meta += QStringLiteral(" / ") + played.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        }
        label->setText(meta);
    }

    if (!m_statQueue.isEmpty()) {
        m_statTimer->start();
    }
}

bool InstanceFilesPage::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_contentHost && event->type() == QEvent::Resize)
        repositionSelectionBar();
    // 列表的 viewport 变大小了 ⇒ 底下那条悬浮操作条要重算位置（见 .hpp 里那段）。
    //
    // NOTE: 那条操作条**不在布局里**（在布局里就成了"列表下面另起一行"，而用户要的是
    // "浮在列表上"，见 repositionSelectionBar）—— 所以它的位置没有人帮着算，
    // 只有这一发能告诉我们"该算了"。
    // NOTE: `watched == m_list->viewport()` 那一句里 `viewport()` 可能为 null（这一页还没
    // 建到那一步），所以先判 m_list。
    if (m_list != nullptr && event->type() == QEvent::Resize &&
        watched == m_list->viewport()) {
        repositionSelectionBar();
    }

    // ---------- 行控件上那三发 ----------
    //
    // 认"这一发来自哪一行"靠行控件上那个**动态属性**（makeRow 里 `setProperty`，名字见
    // .hpp 里 rowPathProperty）—— 列表底下的另一个孩子是那条悬浮操作条，它身上没有这一格，
    // 于是同一段代码自动把它排除在外，不必再写一份"哪些控件算行"的名单。
    if (watched != nullptr && watched->property(rowPathProperty()).isValid()) {
        auto *rowWidget = static_cast<QWidget *>(watched);
        switch (event->type()) {
        case QEvent::Enter:
            // 鼠标进了这一行 ⇒ 露出这一行的「详情」。**进的是行里哪一个孩子也算** —— Qt 的
            // Enter 是往祖先那一串发的（见 .hpp 里 setRowDetailVisible 那段）。
            setRowDetailVisible(rowWidget, true);
            break;
        case QEvent::Leave:
            setRowDetailVisible(rowWidget, false);
            break;
        case QEvent::MouseButtonPress: {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::RightButton) {
                break;
            }
            // 右键这一发 = 开这一条的详情页，并**保留上一页的选择**。**只认"认得出
            // 资源"的那些** —— 判据和这一行那格 ⓘ 的可用性逐字相同（`m_resourceTips`，
            // 见 applyDetailButtonState）。
            //
            // 【为什么必须把它吃掉，而不是让视图也看见这一发】
            // ① 视图收到右键会自己动当前项/选中态（Qt 为了让右键菜单有个目标，会先把
            // 鼠标底下那一条选中），而这里要保留上一页的选择；
            // ② 这一页没有右键菜单，放它过去也没有第二个读者。
            // ⚠ `accept()` 那一句**不能省**：Qt 那个"被忽略了就往上冒"的循环判的是
            // `res && isAccepted()` —— 事件过滤器只 return true（不 accept）的话，
            // 这一发照样会被送到 viewport 上（见 .hpp 里 eventFilter 那段）。
            // ⚠ `path` 必须在 emit **之前**取出来：接的人会把这一页收掉去开详情页，
            // 回来之后 `watched` 说不定已经不在了。
            const QString path = watched->property(rowPathProperty()).toString();
            if (m_batchMode && m_resourceTips.contains(identityPath(path))) {
                emit detailRequested(path);
                event->accept();
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
