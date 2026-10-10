/**
 * @file InstanceFilesPage.cpp
 * @brief InstanceFilesPage 的实现（见 .hpp 里那段"六页共用一个形状"）。
 * @author 0x7fffff
 * @date 2026-09-26
 */
#include "InstanceFilesPage.hpp"

#include "ElidedLabel.hpp"
#include "Theme.hpp"   // 上面那两颗一般性的按钮（黑字黑边）
#include "MainWindowUI.hpp"
#include "ToggleSwitch.hpp"
#include "VersionIcons.hpp"

#include <QComboBox>
#include <QCursor>     // syncRowHover()：光标此刻压在哪儿
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>  // 滚动那一趟要重摆「详情」那一格（见构造里那段）
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>  // isCurrentPage()：往上找最外面那个堆叠容器
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

/// 底下那条悬浮操作条离 viewport **下缘**多远（px）。留一点：贴着边看着像被切了。
///
/// NOTE: 它只影响"离底边多远"，横向是居中的（见 repositionSelectionBar）。
constexpr int kSelectionBarMargin = 12;
/// 那条操作条四周的留白、以及它里面控件之间的间距（px）。
constexpr int kSelectionBarPadding = 8;
constexpr int kSelectionBarSpacing = 8;

/// 比正文浅一档的那个颜色。**全仓唯一去处是 `QPalette::PlaceholderText`**（见
/// InlineMessage::setNote() 上面那段 NOTE）—— 这里只是把它挪到"路径"和"大小·时间"
/// 这两处次要文字上，不自己写死一个灰。
void makeSecondary(QLabel *label) {
    if (label == nullptr) {
        return;
    }
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);
}

/// 比正文字号小一档 —— **行里那第二行小字**专用的（现在只有「大小 · 时间」在用）。
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
    font.setPointSize(qMax(6, font.pointSize() + MainWindowUI::versionDateFontDelta()));
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
/// ★ 2026-09-27 补。用户报「图标虽然加载了，但是只显现了左上角的一小部分」，根子就在
///   `QLabel::setPixmap()` **不缩放**：它按 pixmap 本来的大小画，超出控件的那一块被裁掉。
///   而作者传的那张图本来就不是这一格的大小 —— Modrinth 那条 `_96.webp` 是 96 px，
///   `icon.png` 那条更是**原图**（几百 px 的都有），而这一格只有
///   `MainWindowUI::instanceFileIconSize()`（32）。于是每一张真图标都只露出左上角那一小块，
///   看着像"图裂了"。
///
///   NOTE: 这个毛病**只有真图标真的加载出来了**才看得见 —— 占位图本来就是照这一格的尺寸画的
///         （见 placeholderIcon），一个像素都不差。所以它和「模组的图标没有正确加载」是
///         前后脚冒出来的：先修好"地址取不回来"（那条 `<id>/icon.png` 只是老项目的别名），
///         图一到位，这一格立刻露出第二个毛病。
///
/// NOTE: `KeepAspectRatio` 而不是拉满 —— 资源包封面不是方的，拉满会变形。
///       （`setScaledContents(true)` 那个开关的意思恰好就是"拉满"，所以不能用它。）
///       缩放走 `SmoothTransformation`：这是给眼睛看的图，不是拿来逐像素比对的。
/// NOTE: **本来就装得下的图一个像素都不动**：放大只会糊（图标真有 16 px 的），
///       而且小图原样交给 QLabel 时它按对齐方式摆在那一格里（见 makeRow 里的 AlignCenter）。
QPixmap scaleToIconSlot(const QPixmap &icon) {
    const int slot = MainWindowUI::instanceFileIconSize();
    if (icon.isNull() || (icon.width() <= slot && icon.height() <= slot)) {
        return icon;
    }
    return icon.scaled(slot, slot, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

/// 目录**自己带着的那张小图**：`<目录>/icon.png`。没有、读不出来就是空 pixmap。
///
/// ★ 2026-09-27 补。用户报「存档文件的图像没获取到」。存档那一页列的全是**目录**
///   （`saves/<世界名>/`）—— 目录没有哈希、也不在任何平台的清单里，所以"按内容反查身份"
///   那条路对它们**永远**是空手而归（`MainWindow::instanceIconCandidatePaths()` 连
///   `saves/` 都不列，理由写在那边：一个存档不是一个能反查的资源）。
///   可是**世界自己就带着缩略图**：游戏每次存世界都会往存档目录里写一张 `icon.png`
///   （就是进游戏时"选择世界"那一屏上画的那张；实测用户盘上 5 个存档一个不落）。
///   所以这一格的图根本不用问网络，盘上就有。
///
/// NOTE: 那条文件名是**游戏的约定**，不是我们发明的：大小写按它写的 `icon.png` 认
///       （Windows 上不区分，别的系统上区分，照原样写最保险）。
/// NOTE: 不去认"这是不是一个真存档"（比如查 `level.dat` 在不在）—— 摆一张图而已，
///       目录里有那张图就画。多一道判断就多一处"哪些目录算数"的规则要跟着游戏版本改。
/// NOTE: 只有**目录**才看这个（见 iconForPath 里 `isDir` 那个闸）：文件自己有哈希那条路。
/// 一个目录**里面所有东西**的字节数（递归；目录项自己那几十个字节不算，那和用户在资源
/// 管理器里看到的"大小"对不上）。
///
/// ★ 2026-09-28 加。用户原话：「存档那一栏每个世界都显示存档大小和最后游玩时间」。
///
/// NOTE: 为什么这件事落在 ui/ 这一层：它要的只是"一个目录有多大"，用的是 Qt 自己的目录
///       遍历 —— 不碰 AppPaths、也不认识 src/ 里任何一条规矩。这一层唯一那条硬线是
///       "不 include src/、不算路径"，这一件事两条都没踩（和它已经在用的 `QFileInfo::size()`
///       / `QDir::entryInfoList()` 同一档）。
/// NOTE: 带 `QDir::Hidden | QDir::System`：资源管理器报的大小是**算上隐藏文件**的，漏掉
///       的话启动器报的数会比用户自己能核对的那个小，而且小得毫无规律（`.DS_Store` 那种
///       是几百字节，但 `session.lock` 之类在存档目录里到处都是）。
/// NOTE: **不跟符号链接/junction 走**（判据是 `isSymLink() || isJunction()` 两条一起，理由
///       见底下那一段）。实例的 `saves/` 底下挂一个 junction 是有的（用户把存档挪到别的
///       盘上），跟着走过去会把**那一头**的整个目录算进来 —— 而那一头可能正是别的世界里
///       的一份，于是这个数会莫名其妙地翻几倍。
/// NOTE: 这是一个**同步**的递归遍历，几十万文件的存档要几百毫秒。调用它的地方只有
///       fillDirectoryStats()，而那一趟一次只算一行（理由写在那边）。
///
/// ★ 2026-09-29 改成**自己递归**（原先用 `QDirIterator(QDirIterator::Subdirectories)`）。
///   这不是换一种写法，而是判据必须自己握着 —— 原来那版里"看到链接就 `continue`"是
///   **不生效的**：
///
///   ① **junction 不是符号链接**。Windows 上 `mklink /J` 出来的目录 junction，
///      `QFileInfo::isSymLink()` 是 **false**、只有 `isJunction()` 是 true。Qt 自己是这么
///      分的：`qfilesystemmetadata_p.h:364-370` —— `IO_REPARSE_TAG_SYMLINK` → `LinkType`，
///      `IO_REPARSE_TAG_MOUNT_POINT`（junction）→ **只有** `JunctionType`。仓库在
///      `src/manage/FolderManager.cpp` 里也量过同一件事（那儿的 `isLinkToElsewhere()`
///      就是两个条件一起判）。
///   ② **`continue` 拦不住下潜**。QDirIterator 拒绝进一个子目录只有一条判据
///      （Qt 6.10.1 `qdirlisting.cpp:384` 的 `checkAndPushDirectory()`：不开
///      `FollowDirSymlinks` 时看 `isSymLink()`）。junction 不是符号链接 ⇒ 它照进不误，
///      而**进去之后每个文件都是一条独立的结果**，`continue` 只挡住了它自己那一格
///      （目录项，`size()` 是 0）—— 数照样翻倍，只是翻得看不出来。
///
///   所以改成本地递归：**下潜前**自己判两个条件，判完再决定进不进。
///
/// NOTE: 这里**故意不 include `src/manage/FolderManager.hpp`**：ui/ 这一层不 include src/
///       （不碰 AppPaths、也不认识 src/ 里任何一条规矩，见上面那 NOTE）。所以那两个条件是
///       **照抄**的，抄的只有"两条一起判"这一件事；将来要改，两处一起改
///       （搜 `isLinkToElsewhere`）。
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
///       退游戏时最后那一下不一定落在那上面，而且它是"锁"不是"存档"。用 level.dat 和
///       PCL 以及大多数启动器一致，用户拿它跟游戏自己的"选择世界"那一屏对得上。
/// NOTE: level.dat 不在（或者读不了时间）时退回**目录自己的时间** —— 给一个近似的值
///       比给一个空的好，而且那种目录本来就"不是一个完整的存档"。
QDateTime worldLastPlayed(const QString &dirPath) {
    const QFileInfo level(dirPath + QStringLiteral("/level.dat"));
    if (level.exists()) {
        return level.lastModified();
    }
    return QFileInfo(dirPath).lastModified();
}

QPixmap directoryThumbnail(const QString &dirPath) {
    const QString candidate = dirPath + QStringLiteral("/icon.png");
    if (!QFileInfo::exists(candidate)) {
        return QPixmap();
    }
    // 读不出来的（坏文件、不是张图）回空的那张，调用方自然退回文件夹字形 —— 不弹错、
    // 也不留一格里半张烂图。
    return QPixmap(candidate);
}

}  // namespace

InstanceFilesPage::InstanceFilesPage(QWidget *parent) : QWidget(parent) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(8);

    // ---------- 最上面那一排：一条搜索框（2026-09-28）----------
    //
    // 用户原话是「模组等的下载新/刷新/打开文件夹的**上方**加一个搜索」⇒ 这一排在那一排
    // 按钮**之上**（下面还有一排状态选择，在按钮之下 —— 那个位置也是用户点名要的）。
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
        // ★ 有个"清除"小叉（和「配置文件」那一页左边的条目筛选同一个写法）：这一条是
        //   **立刻生效**的筛选，而筛完之后那一格按「刷新」是清不掉的（刷新只重读目录）
        //   —— 没有小叉的话用户得自己全选删掉，而那正是"以为筛坏了"的那一刻。
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
    // ★ 2026-09-27：这一格从前**一个字都没有**（建出来、上了色、给了宽度策略，就是没人
    //   setText）—— 上面那一排看着是「…… [刷新] [打开文件夹]」，用户不知道这一页在看哪个
    //   目录，而这一页叫「文件管理」。文字由 refresh() 灌（目录是从 setDirectory() 来的，
    //   只有那一处知道）。
    //   用 ElidedLabel 而不是普通 QLabel：路径一定比这一格长（实例名本身能到 64 个字），
    //   普通 QLabel 装不下是**硬裁**（最后一级目录名被切掉一半），而这一段文字两头都有用
    //   （盘符那头说明是哪个游戏目录，尾巴那头是 mods / saves 哪一页）⇒ 从**中间**省略。
    m_pathLabel = new ElidedLabel(m_pathRow);
    m_pathLabel->setObjectName(pathLabelName());
    m_pathLabel->setElideMode(Qt::ElideMiddle);
    makeSecondary(m_pathLabel);
    // ★ 路径可能很长（实例名本身就能到 64 个字），而它这一格**不许**把整页顶宽：
    //   横向按 Ignored 算 = 布局完全不管它要多大，只给它剩下来的地方，多出来的字被裁掉。
    //   （和实例页左栏那条折行 QLabel 顶大整窗 sizeHint 是同一个坑的另一面 —— 那边是
    //   不能让它要宽度，这边是不能让它给宽度。）
    m_pathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_pathLabel->setMinimumWidth(0);
    // NOTE: 这里从前挂着 `setTextInteractionFlags(Qt::TextSelectableByMouse)`（"路径能选能复
    //       制"）。换 ElidedLabel 之后那个开关等于没开：文字是 paintEvent 自己画的，选中
    //       高亮画不出来 —— 用户拖一遍什么反应都没有，比一开始就不给还费解。整条完整路径
    //       改由**悬停提示**给（见 refresh() 里 setToolTip 那句），那是这一格现在唯一
    //       "看得全"的路。
    pathRow->addWidget(m_pathLabel, 1);

    // ★ 顶上那颗「下载新 X」（2026-09-27）。摆在**路径右边、刷新左边**：它是这一页上
    //   「往里加东西」的那一颗，比刷新/打开文件夹更靠前；而路径仍旧贴着最左边 —— 那一格带
    //   的是"这一页在看哪儿"的身份，把它挪开，这一排就没有主语了。
    //
    // NOTE: 一开始是藏着的（没有文字 = 没有这一颗，见 setDownloadText()）。**每一页都建**
    //       它（这几页是同一段循环搭出来的，只多一个 if 反而要多一处"哪几页有"的知识）。
    m_download = new QPushButton(m_pathRow);
    m_download->setObjectName(downloadButtonName());
    m_download->setVisible(false);
    connect(m_download, &QPushButton::clicked, this, &InstanceFilesPage::downloadRequested);
    pathRow->addWidget(m_download);

    // ★ 2026-09-28：顶上那两颗（只有存档那一页露着，见 setImportText / setBatchText）。
    //   摆在**路径右边、下载/刷新左边**：和「下载新 X」同一档 —— 它们也是"这一页上的
    //   动作"，够不着这一页在看哪个目录那件事。
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

    m_refresh = new QPushButton(refreshText(), m_pathRow);
    m_refresh->setObjectName(refreshButtonName());
    Theme::markButton(m_refresh, Theme::ButtonKind::Neutral);
    // 刷新是**这一页自己的事**（重新列一遍目录，只有读），不用绕到 MainWindow 去。
    connect(m_refresh, &QPushButton::clicked, this, &InstanceFilesPage::refresh);
    pathRow->addWidget(m_refresh);

    m_openFolder = new QPushButton(openFolderText(), m_pathRow);
    m_openFolder->setObjectName(openFolderButtonName());
    Theme::markButton(m_openFolder, Theme::ButtonKind::Neutral);
    connect(m_openFolder, &QPushButton::clicked, this,
            [this] { emit openFolderRequested(m_directory); });
    pathRow->addWidget(m_openFolder);

    outer->addWidget(m_pathRow);

    // ---------- 按钮那排的下面：一个状态选择（2026-09-28）----------
    //
    // 用户原话：「三个按钮的**下方**一个选择 全部/启用/禁用/可更新」。四格的顺序就是它们
    // 写在这儿的样子（`stateFilterText()` 是那些字的唯一出处）。
    //
    // ★ 「可更新」这一格**不是每一页都有**（见 setCanFilterUpdates）：它靠的是一趟更新
    //   检测的结果，而只有下载页里真有对应一栏的那四页查得动（原理图 / 存档 / 配置文件
    //   里的文件根本不在任何资源平台的清单里）。
    m_stateRow = new QWidget(this);
    {
        auto *row = new QHBoxLayout(m_stateRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        // 左边那个标签：光一个「全部」摆在那儿，用户不知道它管的是列表的哪一半。
        // （和下载页那一排筛选的下拉框一样，每一格前面都有个标签。）
        // ★ 2026-10-09：**只有没开批量的那几页还看得到它** —— 开了批量的页摆的是右边
        //   那四颗按钮，而那四颗自己就写着「全部(352) 启用(351) …」，再加一个「显示」
        //   是多余的（用户点名要的那张 PCL 截图上也没有这个标签）。
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

        // ★ 2026-10-09：**四颗横向按钮**（用户原话：「显示全部/可更新…那个下拉框变成横向
        //   四个按钮（类似 pcl）」）。和上面那个下拉框**互相显隐**（见 applyState），
        //   两套说的是同一件事（`m_stateFilter` 是唯一出处）。
        //
        // NOTE: **四颗都建着**，不能筛更新的那几页把「可更新」那一颗藏起来（见
        //       fillStateButtons）—— 和「下载新 X」「导入世界」同一套做法：建/不建会让
        //       "这一页该有什么"散成两处知识。
        // NOTE: `setAutoExclusive(true)` = 这四颗是一组单选（同一父控件即一组）。
        //       **光靠它还不够**：它保证的是"点已经亮着的那一颗不会把它点灭"，而"点一颗
        //       就换条件"那一发走的是下面那个 clicked。
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
                button->setToolTip(tr("只显示「%1」的那些条目。").arg(stateFilterText(filter)));
                // 一般性那一档（黑字黑边）：这一排是"筛成什么样"，不是这一页的主语，
                // 不该和顶上那几颗主行动按钮抢眼睛（和「刷新」「打开文件夹」同一档）。
                Theme::markButton(button, Theme::ButtonKind::Neutral);
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
    // ★ 2026-09-27：「打开这一条」的出口（见 .hpp 里 activateRequested）。
    //
    //   用 itemActivated 而不是 itemDoubleClicked：前者是 Qt 说的「用户激活了这一条」
    //   —— 双击**和**选中之后按回车都会发它（各平台按自己的习惯），而那正是"打开一个
    //   文件"该有的两种手势。
    //
    //   NOTE: 这一条能成立有一个**前提**：每一行的内容是 setItemWidget() 摆上去的一个
    //         真控件，而它对鼠标事件是"不处理"的（QWidget 默认把 mousePressEvent
    //         ignore 掉）⇒ 事件往上冒到 viewport，列表才收得到这一发。同一件事的另一个
    //         表现是**这些行一直点得中、选中高亮也一直透得上来**（那一层控件是透明的）。
    //         哪一天有人让行控件去 accept 鼠标事件（比如给它自己接个点击），**这一发就会
    //         静默失效** —— 双击没反应，不报错。ui_check 里有一条合成双击的断言钉着它。
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

    // ★ 2026-10-09：「选中了哪几条」的**唯一记账处**（见 .hpp 里 m_selectedPaths 那段）。
    //   用户点行身、Ctrl+A、条上那三颗"改选择"的按钮 —— 最后都会走到这一发上。
    //   NOTE: 它**不看是哪一路来的**：所以这一处是唯一出处，不必在每条路上各记一遍。
    // ★ 2026-10-10：记账那一趟本身收进了 syncSelectedPaths() —— 重列末尾要办的正是同一件事
    //   （那一段把信号挡着，只能自己喊），两处各写一遍就是第二份真相。
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        syncSelectedPaths();
        updateSelectionBar();
    });
    // 那条悬浮操作条的父控件是 `m_list` ⇒ 它（或者它的 viewport）变大小了得自己重算位置
    // （见 eventFilter）。
    if (m_list->viewport() != nullptr) {
        m_list->viewport()->installEventFilter(this);
    }
    // ★★ 2026-10-10：**滚动也要重摆一遍「详情」那一格**（见文件头【第四批】②）。
    //   滚轮一动，光标底下那一行往往就换人了 —— 而 Qt **不会**因为滚动补发 Enter/Leave
    //   （滚的是 viewport 的孩子，鼠标一个像素都没动，见 scrollContentsBy 那段）⇒ 那一格会
    //   留在**已经不光标底下的**那一行上，直到用户动一下鼠标才自己纠正。所以每一趟滚动之后：
    //   先全收起来，再按"光标底下现在是谁"露一次（`syncRowHover()` 用的正是那个权威判据）。
    //   NOTE: 收的时候是**全列表**走一遍 —— 一页几十上百行，每一句对一个本来就没露着的控件
    //         都是白改（`setVisible(false)` 探一下就走），这点代价和"滚一格留一个错格"比不值一提。
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
    outer->addWidget(m_list, 1);

    // ---------- 底下那条**悬浮**操作条（2026-10-09）----------
    //
    // ★ 父控件是 **`m_list` 本身（2026-10-10 起；在那之前是 `m_list->viewport()`，为什么
    //   非换不可见下面那一发 ★★）**、**不进任何布局**：进了布局就变成"列表下面另起一行"，
    //   而用户点名要的是「在列表下方的位置（不是表尾而是悬浮）」。位置由
    //   repositionSelectionBar() 算，重算的时机是 viewport 的 Resize（见 eventFilter）。
    //
    // ★★ 2026-10-10：父控件**从 `m_list->viewport()` 换成 `m_list`**（用户原话：「选中以后
    //   出现下方框，但是滚动滚轮以后框会跟着跑了」）。根子在 Qt 那边：
    //   `QAbstractItemViewPrivate::scrollContentsBy()`（`qabstractitemview_p.h`）滚的是
    //   `viewport->scroll(dx, dy)`，而 **`QWidget::scroll()` 是"连孩子一起挪"的**
    //   （`QWidgetPrivate::scroll_sys()` 第一句就是 `scrollChildren(dx, dy)`，Qt 6 里那句
    //   注释写着 QTBUG-138381）—— 于是每滚一格，这条浮层就跟着挪一格，滚到头就飘出视口。
    //   挂在 `m_list` 底下就**从结构上没有这个毛病**：被滚的只有 viewport 那一棵，
    //   而它是 `m_list` 的**兄弟**，不是它的孩子。
    //   ⚠ 代价是坐标要多加一个 `viewport->pos()`（见 repositionSelectionBar），因为
    //     现在算出来的还是 viewport 坐标系里的位置。压在上面靠 `raise()`（applyState 里）。
    //
    // NOTE: 它**一直建着**（哪怕这一页没开批量）—— 显不显由 updateSelectionBar() 说了算。
    //       建/不建会让"这一页有没有这一条"变成第二处知识，而且 `setBatchMode(true)` 是
    //       在构造之后才叫的（`InstanceSettingsDialog` 那一轮 setter），那时候再补建控件
    //       就得把这一整段搬进一个函数里。
    m_selectionBar = new QWidget(m_list);
    m_selectionBar->setObjectName(selectionBarName());
    m_selectionBar->setVisible(false);
    // 底色：**压在列表上面的一块浮层**，所以走 `Part::Panel`（那一档的出厂色比窗口底色再深
    // 一档，正是"从底下那堆东西里分出来"）。**不写死颜色** —— 主题一换跟着走。
    // ★ 为什么不是 `Part::Card`：那一档的出厂色就是 `Base`，而列表的底**也是** `Base` ——
    //   同色等于这条操作条根本看不出来（只有按钮浮在行上面）。Panel 才是"压上来的一层"。
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
        // ★ 这一格**要多少给多少**（2026-10-09）：这一条**不进布局、宽度是
        //   repositionSelectionBar() 现算的**，窗口窄的时候得压得住 —— 而承接压缩的该是
        //   那七颗按钮（它们少几个像素也就是边距窄一点），不是这句「已选择 N 个文件」
        //   （它被压了就是少一个字，屏幕上是「已选择 2 个文」，而且 Qt **不给 QLabel
        //   省略号**，看着像写错了）。`Fixed` 就是"按 sizeHint 给，不多也不少"。
        //   NOTE: 七颗的 `minimumWidth` 已经在下面那一段放到 0 了（不给就压不动）。
        m_selectionCount->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        m_selectionCount->setMinimumWidth(0);
        bar->addWidget(m_selectionCount);

        for (int i = 0; i < selectionActionCount(); ++i) {
            const auto action = static_cast<SelectionAction>(i);
            auto *button = new QPushButton(selectionActionText(action), m_selectionBar);
            button->setObjectName(selectionButtonName(action));
            // ★ 「窄窗口下这一条怎么让位」（2026-10-09）：给每一颗一个**按自己那两个字的
            //   宽度算出来的下限**（`QPushButton::minimumSizeHint()` 默认把 `padding: 6px`
            //   那 12 + 边框 2 也算进去，比这个还大 —— 七颗一加起来就顶得这一条比视口还宽，
            //   于是布局开始按比例压，**被压掉的是最左边那句「已选择 N 个文件」**，屏幕上
            //   看着像"字写错了"）。压到"刚好放得下这两个字"为止就有了下限，再窄也不会
            //   把字截掉，而多出来的那点宽度按 Qt 的规矩平分给七颗。
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
                    emit batchToggleRequested(selectedPathList(), disable);
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
                    for (int row = 0; row < m_list->count(); ++row) {
                        QListWidgetItem *item = m_list->item(row);
                        if (item != nullptr) {
                            item->setSelected(!item->isSelected());
                        }
                    }
                });
                break;
            case SelectionAction::ClearAll:
                button->setToolTip(selectionPickTip(action));
                connect(button, &QPushButton::clicked, m_list, &QListWidget::clearSelection);
                break;
            case SelectionAction::Delete:
                button->setToolTip(tr("把选中的这些从盘上删掉（只问一次，不是挨个问）。"));
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

    // ★ 2026-10-09：四颗按钮一建出来就先摆一次（条数都是 0）。**不摆的话，「可更新」那一颗
    //   的显隐要等到第一次 refresh 才落地**：`setCanFilterUpdates(true)`（那四页收到的正是
    //   这一个值）看到"没变"就直接返回了，不摆一遍就一直是"建出来时那个样子"。
    fillStateButtons();

    // 摆到"目录还没接上"那一档：构造函数里 setDirectory() 还没被叫过，这会儿既没东西可列、
    // 也没得可点，露出一个空框比说一句话更让人以为坏了。
    applyState();
}

void InstanceFilesPage::setMode(Mode mode) {
    m_mode = mode;
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
    // ★ 多选 ⇄ 单选（见 .hpp 里 setBatchMode 那段）。**Ctrl+A 通不通就由这一句说了算**：
    //   `QAbstractItemView::keyPressEvent()` 里那一发 `QKeySequence::SelectAll` 是**照发不误**的
    //   （判据只有 `selectionMode() != NoSelection`），真正拦住它的是 `selectAll()` 自己那句
    //   "`SingleSelection` 什么都不做" —— 单选的视图上那一发就是**什么都不发生**，这正是
    //   不开批量的那几页该有的样子。
    //
    // ★★ 2026-10-10：**`ExtendedSelection` 换成 `MultiSelection`**（用户原话：「点击的时候
    //   处于选中，再点一次才是取消，在选中的状态下点击其它的就是选中多个文件」）。
    //   这两种模式的分别**不是"能不能多选"，而是"要不要按住 Ctrl"**：
    //     · `ExtendedSelection` —— 不按修饰键点第二行是 `ClearAndSelect`，**把上一条抹掉**；
    //       要加选得按住 Ctrl（或者 Shift 拉一段）。用户要的不是这个。
    //     · `MultiSelection` —— 每一发左键都是 `Toggle`（见 Qt 的
    //       `QAbstractItemViewPrivate::multiSelectionCommand()`：按下时给
    //       `Toggle|selectionBehaviorFlags()`，已在选中的那一条等到**松开**才翻），
    //       也就是**点一下选中、再点一下取消、点别的是加进去**，一个修饰键都不用。
    //   ★ 代价（知道就好，用户没要）：Shift 拉一段那次不再有用；点在**空白处**也不清选择
    //     （`multiSelectionCommand` 对无效行回 `NoUpdate`）—— 清空交给操作条上那颗「取消」。
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
    // ★ 2026-09-29：**看不见的那几页先不列**（用户原话：「点击实例进入设置的时候会卡一下」）。
    //   开一次实例设置会把七个目录一次灌进来（`setFileDirs()`），而用户此刻看的是「概览」
    //   那一页 —— 七页各列一遍目录、逐条 `stat`、逐条建行（每行一个真控件），
    //   全是白做的。这一趟欠着，等它被摆到台前那一刻再补：
    //   `InstanceSettingsDialog::applyPageKey()` 在 `setCurrentIndex()` 之后一定会喊
    //   `refresh()`（那是"切到这一页就当场重列一遍"那条规矩，本来就在）。
    //
    // NOTE: **欠账这件事只跟"看不看得见"有关，跟数据安不安全无关**：那一份份后到的数据
    //       （图标 `m_icons` / 能点开的身份 `m_resourceTips` / 可更新的名单 `m_outdatedPaths`）
    //       全都是**先记账、再找行**（见 setIconForPath() 那几段的 NOTE）—— 这一页没有行的时候
    //       账照样记着，下一趟 refresh() 摆出来的那一行自然是对的。
    if (!isCurrentPage()) {
        m_refreshPending = true;
        return;
    }
    refresh();
}

bool InstanceFilesPage::isCurrentPage() const {
    // 往上找**最外面**那个堆叠容器，看它现在摆的是不是我（或者我的祖先）。
    // - 取最外面那一个：问的是"用户在实例设置里看的是哪一页"，那一层就是
    //   `InstanceSettingsDialog::m_stack`。往里那几层（`save` / `config` 自己套的两格堆叠
    //   —— 列表 / 编辑器）不是这个问题：取最近的话，摆着「概览」时那两页**照样**会被判成
    //   "当前"，欠账那一套对它们就白设了（而这两个目录正是行最多的那几个之一）。
    // - 判据是"当前那一格是不是我或我的祖先"：这一页外面还套着一层 `QScrollArea`
    //   （见 `InstanceSettingsDialog::addPage()`），拿 `==` 比会永远为假。
    // ★ 不认 `isVisible()`：offscreen 下（三个验证工程）那个函数**恒为 false**，在"还没
    //   show 出来"的窗口里也恒为 false —— 拿它当判据等于这一页永远不列。
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

// ---------- 搜索 + 状态筛选（2026-09-28）----------

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
    // ★ 填的时候把信号挡着：`addItem` 每一次都会动 currentIndex，而那一发会绕到
    //   setStateFilter() 上去（构造函数里那会儿还没建完，applyState() 会摸到空控件）。
    //   填完自己把该亮的那一格摆回去就够了。
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
    // 「全部」恒在（用户 2026-10-10 那句的后半截：「…全部的默认显示」）。
    if (filter == StateFilter::All) {
        return true;
    }
    // 这一页根本没有「可更新」那一档（见 setCanFilterUpdates）。
    if (filter == StateFilter::Update && !m_canFilterUpdates) {
        return false;
    }
    // ★★ 2026-10-10：**一条都没有的那一档收起来**（用户原话：「没有可更新的和禁用的/
    //   启用的就默认隐藏那个类别，全部的默认显示」）。那四颗自己写着条数，「禁用(0)」是
    //   一颗点下去什么都没有的按钮 —— 收起来既省地方、也少一个"看着能按其实没用"的键。
    //
    // ★ 这一条**只管开了批量的那四页**：没开批量的那几页摆的是下拉框，而它**不写条数**
    //   —— 那里少一格读起来像出了错（用户说的是那四颗，那几页本来也只有三格、行也不多）。
    if (!m_batchMode) {
        return true;
    }
    // ★★ 2026-10-10（第三批）：**一条禁用都没有的时候，「启用」这一档不该露**。
    //
    //   用户原话：「没有禁用模组时不显示 启用 分类」。理由和底下那条"空档收起来"是同一
    //   个：一条禁用都没有 ⇒ 启用的那些**就是全部**，「启用(N)」和「全部(N)」是同一份名单
    //   —— 一颗按下去什么都不筛的按钮。
    //
    //   ★ 这是一条**不对称**的规矩（和底下那条操作条上的「启用/禁用」逐字同一套）：反过来
    //     "全是禁用"那一档，「禁用(N)」**照样露着**。两半各自都对得上它的用处：
    //       * 一条禁用都没有 → 启用/禁用这两个筛子都没意义（禁用那一档由下面那句自己收）；
    //       * 全是禁用       → 用户**确实**可能想"只看禁用的那些"，这一档得留着。
    //   ⚠ 判据只有这一处（别在 fillStateButtons() 里再判一遍 —— 那正是 .hpp 里那条
    //     NOTE 说的"第二个出处"）。
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
        button->setText(stateFilterButtonText(filter, stateFilterCount(filter)));
        // 露不露：一条都没有的那一档收起来、不能筛更新的页上「可更新」那一颗不摆。
        // 判据只有 stateFilterVisible() 一处（见 .hpp 里那段）—— 别在这儿再判一遍。
        button->setVisible(stateFilterVisible(filter));
        // 哪一颗亮着 = "现在筛的是哪一档"，唯一的判据是 m_stateFilter。
        // ★ 挡信号：`setChecked` 在**同一组里**会把别的那几颗顶下去，而这一趟本来就要
        //   逐颗摆对；不挡的话"摆第一颗"会绕回 setStateFilter() → 又一次 rebuildRows()。
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
    // ★ 2026-10-10：判据收到 `stateFilterVisible()` 一处了（"这一档摆不摆得出来"现在有
    //   三条：全部恒在 / 这一页能不能筛更新 / 一条都没有就收起来），退档那一发跟着它走。
    //   **必须排在 fillStateCombo() 前面**：下拉框那一格是按 `m_stateFilter` 摆的。
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
    // ★ 判据只有 `stateFilterVisible()` 一处：下拉框和四颗按钮两条路最后都走到这儿。
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
    m_outdatedPaths = QSet<QString>(outdatedPaths.cbegin(), outdatedPaths.cend());
    m_updateChecked = checked;
    // ★ 这份名单**两处**在吃：顶上那四颗的字（`可更新(126)`）和悬浮条上「更新」那一颗的
    //   灰化（选中的里面有没有一条可更新）。所以无论下面重不重列，这两处都得跟着重算。
    fillStateButtons();
    // 只有"正选着「可更新」"这一档会因为这个结果改变列表内容 —— 别的三档下这一趟检测
    // 改变不了任何一行，白重列一遍（一个目录上百行）不值当。
    if (m_stateFilter == StateFilter::Update) {
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
        m_scrollAnchor.identity = identityPath(item->data(pathRole()).toString());
        // 它当时离视口顶端多少像素。**按行滚动**（这一页用的就是 Qt 默认的
        // `ScrollPerItem`）时行总是对齐在顶端 ⇒ 这个数恒为 0，还原走的是"摆回那一行"那条
        // 路（见 restoreScrollAnchor 里那段 ★★）。
        m_scrollAnchor.delta = rect.top();
        return;
    }
    // 一行都没露（视口比一行还矮，或者列表是空的）：没有锚点。
    m_scrollAnchor.identity.clear();
    m_scrollAnchor.delta = 0;
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
        //
        // ★★ 2026-10-10（ui_check 的 U10M 当场抓到的）：**两种滚动模式的单位不一样**，
        //   混着算的后果是"整条列表被顶到底"。
        //    * `ScrollPerItem`（**Qt 的默认，这一页用的就是它**）：滚动条的值 = **第几行**，
        //      屏幕上一行一行地跳。这种模式下 `visualItemRect(...).top()` 给的是**像素**
        //      （第 26 行在值是 0 的时候是 1196）—— 拿它去加一个"行号"，得出来的数远超
        //      滚动范围，`setValue()` 静默钳到最大值：屏幕上成了"跳到最后一行"，
        //      而用户报的那个毛病是"跳回最上面"，**方向正好是反的**（所以从前那种"改完
        //      看着像好了"最容易骗人）。这种模式下行总是对齐在视口顶端（`delta` 恒为 0），
        //      还原就是把值摆回**那一行**。
        //    * `ScrollPerPixel`：值就是像素，那就照原来的算法把差值加上去 ——
        //      连"最上面那一行被切掉了小半截"都还原得了。
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
    m_scrollAnchor = ScrollAnchor();
}

void InstanceFilesPage::refresh() {
    // 这一趟真的列了 ⇒ `setDirectory()` 那一笔欠账清了（**摆在最前**：下面任何一条早退
    // 路都不该把这一位留在"还欠着"上 —— 那会让判据以为这一页没列过）。
    m_refreshPending = false;
    if (m_list == nullptr) {
        return;
    }
    // ★★ 2026-10-10：**在下面那个 clear() 之前**把"现在最上面那一行是谁"记下来 ——
    //   点一下开关（禁用/启用）走的就是这一条路，用户手还停在他刚才看的那一行上，重列
    //   之后不该把他扔回顶端（见 .hpp 里 scrollAnchor 那段）。
    rememberScrollAnchor();
    // 上面那一排那一格写"A 在看哪个目录"。**在目录存在性判断之前**就写：目录还不存在
    // （第一次开 mods/ 之前它本来就不在盘上）也得让人看见路径 —— 那时候这一页正是
    // "这个目录是空的/不存在"，路径恰恰是最该看见的东西。
    // 分隔符统一成 Windows 那种反斜杠：这是给用户看的字，不是喂给接口的路径。
    const QString shownDir = QDir::toNativeSeparators(m_directory);
    m_pathLabel->setText(shownDir);
    // 装不下时是从中间省略的，两头那几段还能看；整条路径靠悬停提示给（见构造里那段 NOTE）。
    m_pathLabel->setToolTip(shownDir);
    // clear() 会连同 setItemWidget() 挂上去的那些行控件一起删掉（列表持有它们）。
    // ★ 2026-10-09：**这一发也得把信号挡着**（真机踩到）。`clear()` 会发一次
    //   `itemSelectionChanged`，而那一发接的是「重算 m_selectedPaths」—— 此刻表已经空了，
    //   于是**用户的选择在这一句上就被抹平**；紧接着 rebuildRows() 想按路径把选择还回去，
    //   手里那份账却已经是空的，什么都还不上。屏幕上：点「启用」把一条改回来了，那一行
    //   却没选上、底下那条悬浮操作条也一起消失（真机 + 日志双重确认）。
    //   `rebuildRows()` 自己那段 blocker 挡的是**它那一次** clear —— 挡不住这里这一发，
    //   这正是「注释说防住了、其实漏在外面」那一类。两处都得挡。
    {
        const QSignalBlocker blocker(m_list);
        m_list->clear();
    }
    // ★ 统计那一趟跟着作废（见 m_statLabels 那段）：上面这一句把上一趟那些标签一起删了，
    //   队列里还没轮到的那几个路径指向的是已经不存在的控件。**先清队列再清表**，而且
    //   两样都要清 —— 只清表的话，下一趟的 fillDirectoryStats() 会拿着一个悬空路径去
    //   找行（找不到，白跑一趟）。
    m_statQueue.clear();
    m_statLabels.clear();
    // 先当空的，下面每加一条翻过来 —— 这样"目录不存在"和"目录是空的"自然走同一条路。
    m_empty = true;
    // ★ 2026-09-28：这一趟是**重新读盘**，所以那一份"筛之前"的条目表也整个重来 ——
    //   建行那一步（rebuildRows）只读它（见 .hpp 里 Entry 那段）。
    m_entries.clear();

    const QDir dir(m_directory);
    if (m_directory.isEmpty() || !dir.exists()) {
        m_filteredEmpty = false;
        // ★ 2026-10-09：这一条早退路**到不了 rebuildRows()**，而"把选择收窄到摆出来的那些行"
        //   那一步就在 rebuildRows() 里 —— 不在这儿自己清，目录一消失（比如把整个 mods/
        //   删了，或者切到别的实例）的话 `m_selectedPaths` 会原地留着上一次的路径，
        //   底下那条「已选择 N 个文件」就报着一个**屏幕上根本没有的**数。
        m_selectedPaths.clear();
        applyState();
        return;
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

        const QString raw = info.fileName();
        // 禁用：只有"游戏会自己扫的那三个目录"里的**文件**才有这一档。目录不给 —— 把一个
        // 文件夹改名成 `xxx.disabled` 会连它里面的东西一起从游戏眼里消失，那不是
        // "禁用这个包"该有的效果（而且 mods/ 下那些目录根本不是 mod，比如 mcef-libraries）。
        const bool disabled = m_canToggle && !isDir &&
                              raw.endsWith(disabledSuffix(), Qt::CaseInsensitive);
        const QString shown = disabled ? raw.left(raw.size() - disabledSuffix().size()) : raw;
        const QString path = info.absoluteFilePath();
        // 「这一条叫什么」——**盘上的名字**去掉那个后缀，被禁用的再挂上四个字。
        // ★ 从前这串是拼给 `QListWidgetItem` 的，行里那一格是**转手**从 `item->text()` 拿的
        //   （`makeRow(item->text(), …)`）；条目不给文字之后（见下）这一格就得自己算出来，
        //   否则那四个字会**整条从界面上消失** —— 名字后面不再有任何东西说明这一条是关着的，
        //   而灰掉的名字色在浅色主题下几乎看不出来（ui_check 的 U10c/U10k 当场抓到了这一处）。
        const QString displayName = disabled ? shown + disabledMark() : shown;

        // ★ 2026-09-27：条目**故意不给文字**（从前给的是这一条的名字）。
        //   `QListWidgetItem` 上的文字是**视图代理画的**，而这一行的内容是一个用
        //   setItemWidget() 摆上去的真控件；那个控件是**透明的**（要这样，选中高亮才从
        //   底下透出来），所以代理画的字会从它底下露出来 —— 屏幕上是**同一个名字画两遍**，
        //   一行字的位置和控件里那个名字差着半行，看着像重影（用户 2026-09-27 截图报的
        //   「模组名称出现了两次」）。文字由 makeRow() 那一格全权负责（名字在
        //   nameLabelName() 上，那才是"这一条叫什么"的唯一出处）。
        //
        // NOTE: 别改回 `new QListWidgetItem(文字, m_list)` —— 那样重影立刻回来，而
        //       ui_check 的 U10k 那几条（量"一行里有几段墨"）会当场红。
        //
        // ★ 2026-09-28：**建行这一步挪进 rebuildRows() 了** —— 筛选用的是同一份活
        //   （敲一个字就重列一遍），两处各写一遍的话"筛完之后屏幕上剩什么"迟早有一处
        //   忘了带上。这里只攒条目。
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

void InstanceFilesPage::rebuildRows() {
    if (m_list == nullptr) {
        return;
    }
    // ★★ 2026-10-10：把"现在最上面那一行是谁"记下来（见 .hpp 里那两个函数那段）。这一句
    //   排在最前面 —— 下面 `clear()` 一过就什么都不剩了。`refresh()` 那条路（点一下开关
    //   走的就是它）**已经先记过一遍**，它那一趟到这里时表是空的，所以这个函数看见空表
    //   就不动手（否则会把刚记下的那一份覆盖掉）。
    rememberScrollAnchor();
    // ★★ 2026-10-10：**建行之前**先把条件从一档已经摆不出来的按钮上退回来（见
    //   .hpp 里 ensureStateFilterVisible 那段）。为什么非在这儿不可：下面那一趟正是
    //   按 `m_stateFilter` 筛的，而"这一档还有没有内容"刚刚变了（用户点了「禁用」、
    //   或者更新检测刚回来）—— 条件在半路上变了的话，屏幕上留下的和亮着的那一颗
    //   就对不上，而没有别的东西会自己纠正它。放在这一句之前，下面的 fillStateButtons()
    //   自然会把"哪一颗亮着"跟着摆对。
    ensureStateFilterVisible();
    // ★ 2026-10-09：这一段（clear + 逐条建行）**把信号挡着** —— 见 .hpp 里 m_selectedPaths
    //   那段：`clear()` 会发一次 itemSelectionChanged，建行时按路径还的选择也会发。
    //   不挡的话那一发会拿"还没建完的列表"去重算 m_selectedPaths，等于**把用户的选择在
    //   自己重列的过程中抹掉**（而重列正是"筛一下搜索""禁用一个文件"必经的那一步）。
    //   ⚠ 挡住之后，重列末尾必须**自己**把账和那条操作条摆对（下面那两句）。
    //
    // ★★ 2026-10-10：把"上一趟选着的那些"先换算成**身份**（一趟只算一次，下面逐行比）。
    //   为什么不是拿 `m_selectedPaths` 直接比：那里面记的是**盘上真那个名字**，而点一下
    //   开关就会把它换掉（见下面还选择那一段）。
    QSet<QString> selectedIdentities;
    selectedIdentities.reserve(m_selectedPaths.size());
    for (const QString &path : m_selectedPaths) {
        selectedIdentities.insert(identityPath(path));
    }
    {
        const QSignalBlocker blocker(m_list);
        // clear() 会连同 setItemWidget() 挂上去的那些行控件一起删掉 —— 统计队列/表跟着作废
        // （理由逐字同 refresh() 里那一段）。
        m_list->clear();
        m_statQueue.clear();
        m_statLabels.clear();

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
            m_list->setItemWidget(item, makeRow(entry.displayName, entry.path, entry.isDir,
                                                entry.disabled, entry.size, entry.modified));
            // ★★ 2026-10-10：选择按**身份**还回去，不是按盘上那个名字（`identityPath`，
            //   见 .hpp 里 m_selectedPaths 那段）—— 用户点一下行尾那个开关，MainWindow 就把
            //   `X.jar` 改成了 `X.jar.disabled` 再回头重列，按名字比的话这一行**当场掉出选择**
            //   （用户原话：「修复点击开关的时候会取消选中的问题」）。盘上还是同一个文件、
            //   用户手里选的就是"这一条"，身份一比就还上了。
            //   ⚠ 已知道的边界（故意的，见 identityPath 那段）：`X.jar` 和 `X.jar.disabled`
            //     **同时存在**时两行的身份一样 ⇒ 选中一条会把两条一起选上。那种情况本身少见，
            //     不为它把这一层拆细。
            if (m_batchMode && selectedIdentities.contains(identityPath(entry.path))) {
                item->setSelected(true);
            }
        }
    }
    // 这一趟真摆出来的那些路径 —— ★ 2026-10-10 起**只有"筛完一条不剩"那一个判据**在用它
    // （选择那份账改由 syncSelectedPaths() 从列表里现问一遍，见下面那一段）。
    QStringList shownPaths;
    shownPaths.reserve(m_list->count());
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (item != nullptr) {
            shownPaths.append(item->data(pathRole()).toString());
        }
    }

    // ★★ 2026-10-10：账**从列表里现问一遍**（见 syncSelectedPaths）。上面按身份还回去的
    //   那些行此刻真正选着的就是它们，而账上要记的是**盘上真那个名字** —— 刚被改过名的
    //   那一条，从此记的是 `X.jar.disabled`（底下那条操作条要拿去办事的就是它）。
    //   这一句同时也把"这一趟没摆出来的那些"自然排除掉了（老代码那段"收窄到 shownSet"
    //   办的就是这件事，"已选择 N 个文件"说的永远是屏幕上看得见的那 N 个）。
    syncSelectedPaths();

    // ★ 「目录本来就是空的」和「筛完一条不剩」是两件事（见 m_filteredEmpty）：前者底下那
    //   两排筛选整块收起来，后者留着让用户把条件改回来。所以这里只在"目录里有东西"时
    //   才把后者置起来。
    m_filteredEmpty = !m_empty && shownPaths.isEmpty();

    // 顶上那四颗的字（条数）和亮着的态 —— 每一样都在这一趟重算（见 fillStateButtons）。
    fillStateButtons();
    // 可见性 + 底下那条悬浮操作条的显隐/灰化（applyState 末尾会喊 updateSelectionBar）。
    applyState();

    // ★ 2026-10-10：光标**此刻正压在某一行上**时，把那一行的「详情」露出来。
    //   为什么要补这一发：重列把行控件整个换掉了，而鼠标没动过 —— Qt 不会对着一个新控件
    //   补发 Enter（它只在"鼠标底下那个控件变了"的时候发），于是用户刚点完开关、光标还停在
    //   那一行上，那一行的「详情」会一直藏着，直到他把鼠标挪一下。见 syncRowHover()。
    //
    //   ⚠ **这一句必须排到下一趟事件循环**，不能就地喊：上面刚把行整个换掉
    //     （`clear()` + 逐条 `addItem()` + `setItemWidget()`），而"光标底下是哪个控件"
    //     要的是**视图已经排好版**的位置 —— 此刻那一趟排版还欠着（Qt 自己用一个 0 毫秒的
    //     定时器补，见 `QAbstractItemViewPrivate::doDelayedItemsLayout()`），新行控件的几何
    //     还是还没算出来的样子。就地喊的话问到的是一份旧的/空的几何，露出来的可能是**别**
    //     那一行的「详情」（或者谁都没露）—— 而两样都不报错。0 毫秒的定时器按登记的先后
    //     发，欠着的那一趟排版登记在我们前面，所以落到这一句时几何已经是对的。
    //   ★★ 2026-10-10：**滚回原来那个位置**也排在这一趟里（见 .hpp 里 scrollAnchor 那段）——
    //     理由和上面逐字相同：滚动条能滚多远要等排版算完才知道，就地摆的话，摆到一半就会
    //     被那个还没算出来的上限截住（症状是"滚回了一大半、就是差一点点"）。摆在
    //     syncRowHover() 之前：那一句问的是"光标底下是哪个控件"，它要的正是摆好之后的坐标。
    QTimer::singleShot(0, this, [this] {
        restoreScrollAnchor();
        syncRowHover();
    });

    // 目录行的「大小 · 时间」要递归遍历才拿得到，**排在行都摆出来之后**（见 .cpp 顶部
    // 那段和 fillDirectoryStats）—— 所以这一页是先出来、数字后到。
    if (m_statTimer != nullptr && !m_statQueue.isEmpty()) {
        m_statTimer->start();
    }
}

QWidget *InstanceFilesPage::makeRow(const QString &shownName, const QString &path, bool isDir,
                                    bool isDisabled, qint64 size, const QDateTime &modified) {
    auto *row = new QWidget(m_list);
    // ★★ 2026-10-10：行控件上挂**两样**（见 .hpp 里 rowPathProperty() / eventFilter 那两段）：
    //   ① 一个动态属性，写的是这一条的绝对路径 —— 事件过滤器靠它认"这一发来自哪一行"
    //      （列表里另一个孩子是那条悬浮操作条，它没有这一格）；
    //   ② 事件过滤器本身：这一行的 Enter/Leave（详情那一格的显隐）和右键（开详情页）。
    row->setProperty(rowPathProperty(), path);
    row->installEventFilter(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(8);

    // ---------- 最左一格：这一条的图标 ----------
    //
    // ★ 2026-09-27：资源作者传的那张图（模组图标 / 资源包封面）由 MainWindow 反查之后
    //   递进来（见 setIconForPath()），这一层只负责摆。**先摆一张占位图** —— 真图标是
    //   异步来的（要读盘、要联网），而那几秒里这一格不能是空的：空白和"这一条查不到
    //   图标"在屏幕上是同一个样子，而这两件事不一样。
    //
    // NOTE: 占位图**按"是不是文件夹"分两种**（文件夹一律是文件夹的形状，那是它本来的
    //       长相，不是"没查到"），文件一律是拼图（见 VersionIcons::puzzleMark 那段）。
    // NOTE: 这一格的宽度是**固定**的（不是 sizeHint 撑着）：真图标回来时换上去的 pixmap
    //       尺寸和占位图一样，但"让布局重新算一遍"这件事本身不该发生 —— 一列几十行
    //       同时重排会让整张表抖一下。
    auto *icon = new QLabel(row);
    icon->setObjectName(iconLabelName());
    icon->setFixedSize(MainWindowUI::instanceFileIconSize(),
                       MainWindowUI::instanceFileIconSize());
    // 居中对齐：真图标缩过之后就是**这一格那么大**（正方形、正好铺满），摆哪儿都一样；
    // 而"本来就比这一格小"的那些（16 px 的小图，见 scaleToIconSlot）不居中的话会贴左上角
    // —— QLabel 默认是 AlignLeft｜AlignVCenter，横向就偏了。
    icon->setAlignment(Qt::AlignCenter);
    // 颜色取**次要那一档**（和「大小 · 时间」那行小字同一档）：这一列是配角，用正文色会让
    // 整张表看着比名字那一列还重。取的是**这一行自己的调色板**（不是全局那一份）——
    // 一页里每一行都在同一个窗口里，取到的值一样，但"这一行怎么画"跟着这一行走。
    // 真图标要是已经拿到了就**直接摆那一条的**（`m_icons`，见 setIconForPath 的 NOTE）——
    // 「刷新」重建出来的那一行不该先退成占位图再等一次回答。
    icon->setPixmap(iconForPath(path, isDir, row->palette().color(QPalette::PlaceholderText)));
    icon->setToolTip(QDir::toNativeSeparators(path));
    layout->addWidget(icon);

    // ---------- 左边那半：**名字一行、大小·时间一行** ----------
    //
    // 用户 2026-09-26 报的「实例设置里模组列表模组名称显示存在错误」，根子在这儿：
    // 从前这三样是**一行**里排的（名字 + 「12.3 MB · 2026-09-26 12:00」+ 两颗按钮）。
    // 那个大小·时间那一截在 150% 缩放的字号下要 300 px，而行宽只有 450 上下 ——
    // 于是名字那一格被挤成 **0 宽**，一整列的名字全看不见（`fabric-api-0.119.3+1.21.4.jar`
    // 这类名字在盘上是常态）。行高本来就是 46 px（versionTreeRowHeight()），
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
    auto *name = new ElidedLabel(textColumn);
    name->setObjectName(nameLabelName());
    name->setText(shownName);
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
    }
    // ★ 2026-10-09：名字那一格**不再是链接**了 —— "点名字进详情页"整个退场（用户原话：
    //   「把点击条目进入对应详情页的功能变成一个详情按钮，放到开关和删除中间」，以及
    //   「单击改为选中」）。所以这一格现在只是个名字：颜色由 isDisabled 说了算，光标是
    //   箭头，单击这一下留给列表去选中这一行。
    //   认出来的那几条改由**行尾那颗详情按钮**说 —— 见下面那一格。
    textLayout->addWidget(name);

    // 「12.3 MB · 2026-09-26 12:00」。文件夹没有有意义的大小（Windows 上 info.size() 对
    // 目录给的是目录项自己的字节数，不是里面东西的总和），给一个破折号比给一个假数字好。
    // 这一行也会省略（尾巴那头）：字号大 / 窗口窄的时候它比这一格还长，硬裁同样是"看着
    // 像画坏了"。它的 hint 不封顶 —— 它是下面那行小字，让名字那一格先拿够。
    //
    // ★ 2026-09-28：**存档那一页的目录行**（m_canEdit，见 setCanEdit）也要真数字，而那个
    //   数得递归遍历才拿得到 ⇒ 先摆一句「正在统计…」，真正的值由 fillDirectoryStats()
    //   在事件循环转起来之后补上。这一格同时**登记进 m_statLabels**（那一趟靠它找回这一行）。
    const bool wantsStats = m_canEdit && isDir;
    QString meta;
    if (wantsStats) {
        meta = statsPendingText();
    } else {
        meta = isDir ? QStringLiteral("—") : formatSize(size);
        if (modified.isValid()) {
            meta += QStringLiteral(" · ") + modified.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
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
        // ---------- 行尾那颗「详情」（2026-10-09）----------
        //
        // ★ 2026-10-09：用户原话「把点击条目进入对应详情页的功能变成一个详情按钮，放到开关和
        //   删除中间」。它就是从前"点名字进详情页"那一发的新家 —— 名字退了场，这一颗接过来。
        //
        // ★★ 2026-10-10 两处一起改（用户原话：「详情和启用/禁用换个顺序，且详情只在悬停时
        //   显现，右键点击时也可以进入详情页，此时保留上一页的选择」）：
        //     ① 这一格挪到开关**前面** ⇒ 行尾从此是 `[详情][开关][删除]`
        //        （从前是 `[开关][详情][删除]`）；
        //     ② 它**只在鼠标悬停这一行的时候才露出来** —— 建出来时是藏着的，显隐由
        //        eventFilter 里 Enter/Leave 那一支管（见 .hpp 里 setRowDetailVisible 那段）。
        //
        // NOTE: **每一行都建这一格**（哪怕认不出资源），认不出来的那些 `setEnabled(false)`
        //       —— 一列下来三格左右要对得齐，而"有的行少一格"会让名字那一列宽窄不一。
        //       灰着而不是不摆，读起来是"这一条没有详情页"，不是"这里漏画了"。
        // NOTE: 判据和从前那一发**逐字相同**：`m_resourceTips` 里有这一条 = 认出来了
        //       （见 setResourceForPath）。
        auto *detail = new QToolButton(row);
        detail->setObjectName(detailButtonName());
        detail->setAutoRaise(true);
        const int glyph = MainWindowUI::instanceFileIconSize();
        detail->setIcon(QIcon(VersionIcons::infoMark(
            glyph, row->palette().color(QPalette::PlaceholderText))));
        detail->setIconSize(QSize(glyph, glyph));
        // 文字仍然给着（虽然不画）：读出这一格是"详情"这件事**只剩这一个出口**
        // （悬停提示、无障碍，以及验证工程按它核对"这一颗是干什么的"）。
        detail->setText(detailText());
        applyDetailButtonState(detail, path);
        // ★★ 悬停才露（②）：藏起来的时候**占位留着**（`setRetainSizeWhenHidden`）——
        //   不留的话鼠标一进一出整行都要重排一遍（名字那一格跟着一伸一缩），而这一格正好
        //   夹在名字和开关中间，抖起来最扎眼。
        //   ⚠ 藏着的控件不是鼠标目标，所以**不影响命中测试**：右键照样落到这一行身上
        //     （见 eventFilter 里那一支）。
        QSizePolicy detailPolicy = detail->sizePolicy();
        detailPolicy.setRetainSizeWhenHidden(true);
        detail->setSizePolicy(detailPolicy);
        detail->setVisible(false);
        connect(detail, &QToolButton::clicked, this, [this, path] { emit detailRequested(path); });
        layout->addWidget(detail);
    }

    if (m_canToggle && !isDir) {
        // ★ 2026-09-27：从"两颗文字按钮（禁用 / 启用）"换成**一个拨动开关**
        //   （用户原话：「启用和禁用使用开关的那种形式」）。文字按钮说的是"点下去会发生
        //   什么"，开关说的是"现在是什么状态" —— 一列下来扫一眼就知道哪几个是关着的。
        //
        // NOTE: 开关的位置：★ 2026-10-10 起它排在**详情那颗后面**（行尾三格是
        //       `[详情][开关][删除]`，用户原话「详情和启用/禁用换个顺序」；从前是
        //       `[开关][详情][删除]`）。没开批量那几页没有详情那一格，它照旧是倒数第二格。
        //       一列下来各格都对得齐。**只给"游戏会自己扫的那三个目录里的文件"**
        //       （见 setCanToggle）。
        auto *toggle = new ToggleSwitch(row);
        toggle->setObjectName(toggleButtonName());
        // **开着 = 游戏扫得到它**（正常文件名）、关着 = 名字后面挂着 `.disabled`。
        // 方向不能反：开关向右 = "生效中"是这个控件在别处的意思（设置页那些开关也是），
        // 而这一条恰好是最要紧的一条 —— 反了的话"全开"看着像"全关"。
        toggle->setChecked(!isDisabled);
        // 说"改成哪个名字"要用**盘上那个真名字**（QFileInfo::fileName），不是行上那串
        // 显示名 —— 禁用着的那一条显示名后面挂着「（已禁用）」，拼进去就成了一串谁也不认的名字。
        const QString fileName = QFileInfo(path).fileName();
        toggle->setToolTip(isDisabled
                               ? tr("%1：把名字改回去，游戏又能扫到它了。").arg(toggleOffText())
                               : tr("%1：把名字改成 %2%3，游戏就扫不到它了（文件还在）。")
                                     .arg(toggleOnText(), fileName, disabledSuffix()));
        connect(toggle, &ToggleSwitch::clicked, this, [this, path] { emit toggleRequested(path); });
        layout->addWidget(toggle);
    }

    if (m_canEdit && isDir) {
        // ★ 2026-09-28：行尾那支笔（用户原话「右边画一支笔意为编辑，和基岩版一样」）。
        //
        // NOTE: 和垃圾桶同一个做法 —— **字形是画的**（VersionIcons，全仓一个图片文件都
        //       没有），尺寸取和行首图标一样的那一档、颜色取次要那一档，所以一列下来
        //       两处行尾控件左右对得齐、深浅也一样。文字仍然给着（`text()` = 「编辑」，
        //       画不画是 QToolButton 的 ToolButtonIconOnly 说了算）：悬停提示、无障碍、
        //       以及验证工程"这一格是干什么的"都只剩这一个出口。
        // NOTE: 它排在垃圾桶**后面**（这一页两档不会同时开：存档页 canDelete=false，
        //       别的页 canEdit=false），所以不会有"两页之间那一列左右不齐"的问题。
        auto *edit = new QToolButton(row);
        edit->setObjectName(editButtonName());
        edit->setAutoRaise(true);
        const int glyph = MainWindowUI::instanceFileIconSize();
        edit->setIcon(QIcon(VersionIcons::pen(
            glyph, row->palette().color(QPalette::PlaceholderText))));
        edit->setIconSize(QSize(glyph, glyph));
        edit->setText(editText());
        // ★ 2026-09-28：这一格里现在最实的一件事是**改游戏规则**（读的是这个存档自己的
        //   level.dat），改名 / 开文件夹退到后面去了 —— 三样都写全，用户才知道点下去
        //   不止能改名。
        edit->setToolTip(tr("编辑这个世界：改游戏规则、改名、打开它的文件夹。"));
        connect(edit, &QToolButton::clicked, this, [this, path] { emit editRequested(path); });
        layout->addWidget(edit);
    }

    if (m_canDelete) {
        // ★ 2026-09-27：从一颗写着「删除」的文字按钮换成**一个垃圾桶字形**
        //   （用户原话：「删除还是垃圾桶」）。它仍然是行尾最后一格、仍然要点一下才问
        //   "真的要删吗"（那句在 MainWindow 里）。
        auto *remove = new QToolButton(row);
        remove->setObjectName(deleteButtonName());
        remove->setAutoRaise(true);
        // 字形是**画的**（VersionIcons，全仓一个图片文件都没有），尺寸取和图标那一格
        // 一样的一档 —— 行尾两颗按钮和行首那枚图标同一个大小，看着才是一套的。
        // 颜色用次要那一档：删除是行里的一个动作，不是正文（而且它是**危险**动作这件事
        // 由点下去那句问话来说，不靠一个红图标在列表里一直喊）。
        const int glyph = MainWindowUI::instanceFileIconSize();
        remove->setIcon(QIcon(VersionIcons::trash(
            glyph, row->palette().color(QPalette::PlaceholderText))));
        remove->setIconSize(QSize(glyph, glyph));
        // 文字仍然给着（虽然不画）：读出这一格是"删除"这件事**只剩这一个出口**
        //（悬停提示、无障碍，以及验证工程按它核对"这一颗是干什么的"）。
        remove->setText(deleteText());
        remove->setToolTip(isDir ? tr("把这个文件夹连同里面的东西一起删掉。")
                                 : tr("把这个文件从盘上删掉。"));
        connect(remove, &QToolButton::clicked, this, [this, path] { emit deleteRequested(path); });
        layout->addWidget(remove);
    }

    return row;
}

QLabel *InstanceFilesPage::pathLabel() const {
    // 只在**这里**把 ElidedLabel 当成 QLabel 交出去（见 .hpp 里那段）：那一格的确是
    // ElidedLabel，但调用方要的只有 setText / text，不必知道它是怎么画出来的。
    return m_pathLabel;
}

void InstanceFilesPage::applyState() {
    const bool unavailable = !m_unavailable.isEmpty();
    const bool empty = !unavailable && m_empty;
    // ★ 第四档（2026-09-28）：目录里有东西、但这一趟筛完一条不剩。
    const bool filtered = !unavailable && !empty && m_filteredEmpty;

    // 搜索框 / 状态那一栏：**这一页用不上、或者目录本来就空**的时候整块收起来（没有
    // 可筛的东西，露着两排空控件等于摆两个坏键）。筛完一条不剩时**留着** —— 用户正要
    // 靠它们把条件改回来，这时候把它们收掉是最糟的一刻。
    const bool filtersUseful = !unavailable && !m_empty;
    if (m_searchRow != nullptr) {
        m_searchRow->setVisible(filtersUseful);
    }
    if (m_stateRow != nullptr) {
        m_stateRow->setVisible(filtersUseful);
    }
    // ★ 那一排里**下拉框和四颗按钮互相显隐**（2026-10-09，见 .hpp 里 m_stateRow 那段）：
    //   开了批量的页摆那四颗（连「显示」那个标签一起收掉 —— 那四颗自己就写着「全部(352)」），
    //   没开的页一丝不变。
    if (m_stateLabel != nullptr) {
        m_stateLabel->setVisible(!m_batchMode);
    }
    if (m_stateCombo != nullptr) {
        m_stateCombo->setVisible(!m_batchMode);
    }
    if (m_stateButtonsRow != nullptr) {
        m_stateButtonsRow->setVisible(m_batchMode);
    }

    m_pathRow->setVisible(!unavailable);
    m_list->setVisible(!unavailable && !empty && !filtered);
    m_notice->setVisible(unavailable || empty || filtered);
    // ★ 底下那条悬浮操作条跟着这一页的可见性走（它压在列表上，"列表都收起来了还浮着
    //   一条操作条"是最难解释的一种画面）。判据要的是**上面这一句刚摆好的** m_list 的
    //   可见性 ⇒ 必须排在这一句之后。
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
        // ★ 2026-10-10：「可更新」选得上、而这一趟筛下来一条不剩，从这一天起**只剩
        //   **下拉框那几页走得到（那几页不写条数、也不收空档，见 stateFilterVisible()
        //   里那句 `if (!m_batchMode) return true;`）。
        //
        //   NOTE: 开了批量那四页走不到这儿 —— 「可更新(N)」这一颗在 N == 0 时**整颗收着**，
        //         而且它一旦收起来，选中的条件会被 ensureStateFilterVisible() 退回到「全部」。
        //         也就是说这四个字（`updateNotCheckedText()` / `updateNoneText()`）在那四页
        //         上现在是**够不着的最后一层兜底**：留着是因为"一颗都露着"这件事是上面
        //         那个判据说了算，哪天它松了（比如查完更新那一趟把条数算错了），这一句仍然
        //         是唯一说得清"是没查过还是查了没有"的地方。别顺手删。
        m_notice->setText(m_updateChecked ? updateNoneText() : updateNotCheckedText());
        return;
    }
    m_notice->setText(noMatchText());
}

// ---------- 底下那条悬浮操作条（2026-10-09）----------

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

QStringList InstanceFilesPage::selectedPathList() const {
    // 走**列表里那一行的先后**，不是 `m_selectedPaths` 那个 QSet 的顺序：这是一份要交给
    // MainWindow 去逐条办事的名单，按屏幕上从上到下的次序报出去，出错时日志里那一串
    // 也跟用户看到的一样。不在表里的（理论上没有——见 rebuildRows 那段收窄）跳过。
    QStringList paths;
    if (m_list != nullptr) {
        for (int row = 0; row < m_list->count(); ++row) {
            QListWidgetItem *item = m_list->item(row);
            if (item == nullptr || !item->isSelected()) {
                continue;
            }
            const QString path = item->data(pathRole()).toString();
            if (!path.isEmpty()) {
                paths.append(path);
            }
        }
    }
    return paths;
}

void InstanceFilesPage::updateSelectionBar() {
    if (m_selectionBar == nullptr) {
        return;
    }
    // 露出来的条件**三条一起**：开了批量、选中了至少一条、列表正露着。
    // ★ 最后那一条不是多余的：目录空 / 这一页用不上时列表整块收起来了，而"选中"这件事
    //   还压在 m_selectedPaths 上（我**不清它** —— 用户切一眼目录再切回来，选择该还在）。
    const bool show = m_batchMode && !m_selectedPaths.isEmpty() && m_list->isVisibleTo(this);

    // ---------- 灰化规则（用户 2026-10-09 原话，见 .hpp 里那段）----------
    bool anyOutdated = false;
    bool anyEnabled = false;
    bool anyDisabled = false;
    for (const QString &path : m_selectedPaths) {
        if (m_outdatedPaths.contains(path)) {
            anyOutdated = true;
        }
        const Entry *entry = entryForPath(path);
        if (entry == nullptr) {
            continue;
        }
        if (entry->disabled) {
            anyDisabled = true;
        } else {
            anyEnabled = true;
        }
    }
    const auto buttonFor = [this](SelectionAction action) {
        return selectionButtonAt(static_cast<int>(action));
    };
    const auto setEnabled = [&buttonFor](SelectionAction action, bool on) {
        if (QPushButton *button = buttonFor(action)) {
            button->setEnabled(on);
        }
    };
    setEnabled(SelectionAction::Update, anyOutdated);
    // 「禁用 / 启用」两颗在**没有开关的那一页**（数据包）上不是灰的 —— 是**藏着**
    // （见 .hpp 里 SelectionAction 那段的 NOTE）。所以这里先按"这一页有没有开关"决定摆不摆，
    // 再决定灰不灰。
    //
    // ★★ 2026-10-10 用户原话：「修改禁用/启用状态时改成**没有禁用的时候不显示启用**，但是
    //   **没有启用（全是禁用）的时候保持显示禁用**」。也就是这两颗**故意不一样**：
    //     · 「启用」：选中的里面一条禁用着的都没有 ⇒ **整颗收起来**（按下去本来也无事可做，
    //       而且它此刻正是灰的 —— 与其摆一颗按不动的，不如不摆）；
    //     · 「禁用」：**恒露着**，哪怕选中的全都已经禁用着（那一颗只是灰着，不收）。
    //   NOTE: 判据取的是**选中集**（`anyDisabled` / `anyEnabled`）而不是整页的条数
    //         （`stateFilterCount`，那是顶上那四颗筛选按钮的口径）—— 这条操作条办的一直是
    //         "选中的这些"，它上面每一颗的灰化也是同一个口径。
    if (QPushButton *button = buttonFor(SelectionAction::Disable)) {
        button->setVisible(m_canToggle);
        button->setEnabled(m_canToggle && anyEnabled);
    }
    if (QPushButton *button = buttonFor(SelectionAction::Enable)) {
        button->setVisible(m_canToggle && anyDisabled);
        button->setEnabled(m_canToggle && anyDisabled);
    }
    // 全选 / 反选 / 取消 / 删除四颗恒可用（它们要么只动"选中的集合"，要么在按下去之后
    // 还会问一句 —— 灰着反而让用户以为删不了）。
    // NOTE: 「删除」也**选中了才在**（它在这条上，而这条只在有选中时露出来）。

    if (m_selectionCount != nullptr) {
        m_selectionCount->setText(selectionCountText(m_selectedPaths.size()));
    }
    m_selectionBar->setVisible(show);
    if (show) {
        // ★ 2026-10-10：父控件换成 `m_list` 之后，这一条**结构上就在整棵 viewport 之上**
        //   （行控件是 viewport 的孩子，而这颗是 viewport 的**兄弟**、还排在它后面）。
        //   这一发 `raise()` 仍然留着 —— 它现在是"防别的兄弟插进来"，比从前那件事便宜
        //   得多，而且这一行的成本是零（不 raise 只是少一层保险，看不出症状的那种）。
        m_selectionBar->raise();
        repositionSelectionBar();
    }
}

void InstanceFilesPage::repositionSelectionBar() {
    if (m_selectionBar == nullptr || m_list == nullptr) {
        return;
    }
    QWidget *viewport = m_list->viewport();
    if (viewport == nullptr) {
        return;
    }
    // ★ 位置**自己算**：这条操作条不进布局（进了布局就变成"列表下面另起一行"，而用户要
    //   的是"浮在列表上"，见 .hpp 里 eventFilter 那段）。算的是**居中 + 离下缘
    //   kSelectionBarMargin**：PCL 那一条就在这个位置。
    // NOTE: 用 `sizeHint()` 而不是 `width()/height()`：它这会儿可能还没被 show 过
    //       （geometry 是 0×0），而 sizeHint 是布局现算的，一开始就是对的。
    const QSize hint = m_selectionBar->sizeHint();
    // ★ 2026-10-09（ui_shot 的 10d2 量出来的）：**不许比视口还宽**。七颗按钮 + 那句
    //   「已选择 N 个文件」的自然宽度在默认窗口那一档（内容区 649）是 719 —— 不夹这一下
    //   的话条子从右边溢出 70 像素，最后那颗「删除」正好被切掉半截，而"看着有、按不着"
    //   是最难查的那种。夹完之后里面的 QHBoxLayout 会把每一格压到放得下（每一格的
    //   `minimumWidth` 已经在构造里放到 0 了），文字按 Qt 的规矩省略。
    const int roomiest = qMax(0, viewport->width() - 2 * kSelectionBarMargin);
    const int width = roomiest > 0 ? qMin(hint.width(), roomiest) : hint.width();
    // ★ 2026-10-10：父控件是 `m_list` 不是 viewport ⇒ 位置要多加一个 viewport 的左上角
    //   （算出来的这一份仍然是 **viewport 坐标系**里的：居中、离下缘 kSelectionBarMargin）。
    const QPoint origin = viewport->pos();
    const int x = origin.x() + (viewport->width() - width) / 2;
    const int y = origin.y() + viewport->height() - hint.height() - kSelectionBarMargin;
    // 横竖都夹在 [0, …] 里：列表窄/矮的那一档（窗口拉到最小）会算出负数，摆到负坐标上
    // 就是"这一条被窗口左边切掉半截"。
    m_selectionBar->setGeometry(qMax(0, x), qMax(0, y), width, hint.height());
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

QPushButton *InstanceFilesPage::stateButtonAt(StateFilter filter) const {
    const int index = static_cast<int>(filter);
    if (index < 0 || index >= m_stateButtons.size()) {
        return nullptr;
    }
    return m_stateButtons.at(index);
}

QPushButton *InstanceFilesPage::selectionButtonAt(int action) const {
    if (action < 0 || action >= m_selectionButtons.size()) {
        return nullptr;
    }
    return m_selectionButtons.at(action);
}

QRect InstanceFilesPage::selectionBarGeometry() const {
    return m_selectionBar != nullptr ? m_selectionBar->geometry() : QRect();
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

void InstanceFilesPage::fillDirectoryStats() {
    // 一趟只算一行，算完自己排下一趟。**不是** while 循环一次算完：
    //   - 一个世界要递归遍历整棵树（几十万个文件是真有的），十几二十个世界连着算就是
    //     十几秒 —— 而这时候行**已经摆在屏幕上了**，用户看到的是一个点了没反应的界面；
    //   - 一次一行的话，事件循环在中间转过去，界面一直活着，数字一行一行地冒出来
    //     （"在算"这件事本身也就看得见了）。
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
        // 拼接方式**逐字和上面那个文件行一样**（「12.3 MB · 2026-09-26 12:00」）：
        // 同一张表里两种行的小字长得一样，才不会让人以为这一列有两个意思。
        QString meta = formatSize(bytes);
        if (played.isValid()) {
            meta += QStringLiteral(" · ") + played.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        }
        label->setText(meta);
    }

    if (!m_statQueue.isEmpty()) {
        m_statTimer->start();
    }
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
    //
    // ★★ 2026-10-10：查的是 `identityPath(path)` —— 禁用/启用只改了名字，图标还是那一张
    //   （见 .hpp 里 identityPath 那段；用户报的「模组禁用以后图标没了」就指着这一句）。
    const QString key = identityPath(path);
    if (m_icons.contains(key)) {
        const QPixmap icon = m_icons.value(key);
        if (!icon.isNull()) {
            // ★ 缩到这一格的大小（见 scaleToIconSlot 那段）。`m_icons` 里存的是**原样那张**
            //   —— 这一层不篡改别人给的东西（验证工程按"存进去的 == 拿出来的"核对过），
            //   缩放只发生在"往控件上摆"这一步。
            return scaleToIconSlot(icon);
        }
    }
    // ★ 目录**自己身上带着的那张图**（2026-09-27 补，用户报「存档文件的图像没获取到」）。
    //   存档那一页列的全是**目录**（`saves/<世界名>/`），而目录没有哈希、也不在任何平台的
    //   清单里，所以"反查身份"那条路**永远**拿不到它们的图 —— `MainWindow::
    //   instanceIconCandidatePaths()` 连 `saves/` 都不列（那不是能反查的资源）。
    //   可是**世界自己带着缩略图**：游戏每次保存都会往存档目录里写一张 `icon.png`
    //   （进游戏时"选择世界"那一屏上那张小图就是它；实测用户盘上 5 个存档一个不落）。
    //   于是这一格的图根本不用问网络 —— 盘上就有。
    //
    // NOTE: 判据是**这个目录里有没有那张图**，不是"这一页是不是存档页"：文件夹自带缩略图
    //       这个约定本来就是按目录说的，哪个目录里头放了就画哪个（`mods/mcef-libraries/`
    //       那种没有的照旧退回文件夹字形）。按页判断反而要多一处"哪一页算"的知识。
    // NOTE: 不缓存。这一格每一趟 refresh() 只问一次（iconForPath 只在 makeRow 里调），
    //       而那张图只有十来 KB、decode 一次是微秒级；真按 mtime 键住一份，反倒多出一处
    //       "世界存了一次档、图变了"时要记得清的地方。
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
    // **先记账、再找行**：这一条现在在不在屏幕上不影响"这一条问过了"。顺序反过来的话，
    // 图标回来的那一刻用户正好点了「刷新」（那几毫秒里这一页是没有行的），这一条就永远
    // 回到"还没问"——而 MainWindow 那边不会再问第二遍。
    //
    // ★★ 2026-10-10：记在 `identityPath(path)` 上（见 .hpp 里那段）—— MainWindow 是按
    //   **盘上真那个名字**推过来的（它手里那份路径表是它自己列目录列出来的），而那一位
    //   在禁用/启用之后会变；这一层记的是"这一条是哪个资源"，跟名字无关。
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
    // ★ 空 tip 有两种意思，**两种都要办**（见 .hpp 那段）：
    //   ① 这一条**从来**没认出来过 ⇒ 什么都不用做（"点不动"是默认状态，不是一条要摆出来
    //      的状态；记一条空的等于给每一行都记一笔账）；
    //   ② 上一趟认出来了、这一趟没有（同一个路径上换了内容）⇒ 必须把那一格从"点得动"
    //      退回"点不动"（`setEnabled(false)`），否则那一颗按钮看着能按、按下去什么都不
    //      发生。**一个「看着能按、按下去没反应」的按钮比灰着更糟。**
    //   两件事靠 `remove()` 的返回值分开：删掉了 = 走过 ② 那一支。
    // ★★ 2026-10-10：这一份账也按 `identityPath(path)` 记（理由逐字同 setIconForPath）——
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
        //       账已经记下，下一趟 refresh() 摆出来的那一行自然是能点的。
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
    // ★ 取的是 `identityPath(path)` 那一格（见 .hpp 里那段）—— 禁用/启用只改了名字，
    //   那一行认出来的还是同一个资源。
    const auto tip = m_resourceTips.constFind(identityPath(path));
    const bool known = tip != m_resourceTips.constEnd();
    button->setEnabled(known);
    button->setToolTip(known ? tip.value()
                             : tr("这一条认不出是哪个资源，所以没有详情页可开。"
                                  "（自己改过的、或者平台清单里没有的那些都会是这样。）"));
}

bool InstanceFilesPage::eventFilter(QObject *watched, QEvent *event) {
    // 列表的 viewport 变大小了 ⇒ 底下那条悬浮操作条要重算位置（见 .hpp 里那段）。
    //
    // NOTE: 那条操作条**不在布局里**（在布局里就成了"列表下面另起一行"，而用户要的是
    //       "浮在列表上"，见 repositionSelectionBar）—— 所以它的位置没有人帮着算，
    //       只有这一发能告诉我们"该算了"。
    // NOTE: `watched == m_list->viewport()` 那一句里 `viewport()` 可能为 null（这一页还没
    //       建到那一步），所以先判 m_list。
    if (m_list != nullptr && event->type() == QEvent::Resize &&
        watched == m_list->viewport()) {
        repositionSelectionBar();
    }

    // ---------- 行控件上那三发（★ 2026-10-10 加）----------
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
            // ★★ 右键这一发 = 开这一条的详情页（用户原话：「右键点击时也可以进入详情页，
            //   此时保留上一页的选择」）。**只认"认得出资源"的那些** —— 判据和这一行那格
            //   ⓘ 的可用性逐字相同（`m_resourceTips`，见 applyDetailButtonState）。
            //
            // 【为什么必须把它吃掉，而不是让视图也看见这一发】
            //   ① 视图收到右键会自己动当前项/选中态（Qt 为了让右键菜单有个目标，会先把
            //      鼠标底下那一条选中），而用户点名要「此时保留上一页的选择」；
            //   ② 这一页没有右键菜单，放它过去也没有第二个读者。
            //   ⚠ `accept()` 那一句**不能省**：Qt 那个"被忽略了就往上冒"的循环判的是
            //     `res && isAccepted()` —— 事件过滤器只 return true（不 accept）的话，
            //     这一发照样会被送到 viewport 上（见 .hpp 里 eventFilter 那段）。
            //   ⚠ `path` 必须在 emit **之前**取出来：接的人会把这一页收掉去开详情页，
            //     回来之后 `watched` 说不定已经不在了。
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

void InstanceFilesPage::syncSelectedPaths() {
    // 「选中了哪几条」的账**只从列表里现在真选着的那些行问一遍** —— 见 .hpp 里
    // m_selectedPaths 那段：它是那一位的唯一出处（`itemSelectionChanged` 里那一支是
    // 同一个口径，只不过那一支是"用户点了"，这一支是"我们重列完了"）。
    // 重列那段把信号挡着，所以重列之后必须**自己**把账摆对 —— 就是这一句。
    m_selectedPaths.clear();
    if (m_list == nullptr || !m_batchMode) {
        return;
    }
    const QList<QListWidgetItem *> selected = m_list->selectedItems();
    for (QListWidgetItem *item : selected) {
        if (item == nullptr) {
            continue;
        }
        const QString path = item->data(pathRole()).toString();
        if (!path.isEmpty()) {
            m_selectedPaths.insert(path);
        }
    }
}

void InstanceFilesPage::syncRowHover() {
    // 重列把行控件整个换掉了，而鼠标**可能没动过** —— Qt 只在"鼠标底下那个控件变了"的时候
    // 发 Enter/Leave，对着一个刚建出来、光标正好压在上面的控件**不会**补发 Enter。症状：
    // 用户点一下行尾那个开关（那一发正好会让 MainWindow 改名 + 重列），光标原地不动，那一行
    // 的「详情」就一直藏着，直到他把鼠标挪一下。
    //
    // 判据不用 `underMouse()`：那个位是 Qt 在 Enter/Leave 那一趟顺手摆的，而这一趟正是
    // "Enter 没来过"的那种情况。改成**当场问一句"光标底下现在是哪个控件"**
    // （`QApplication::widgetAt` 走的是真正的命中测试，还能顺带把"光标其实在另一扇窗上面"
    // 也排除掉）—— 它确实是"此刻在不在这一行上"的权威答案。
    //
    // ★ 认"哪一行"**顺着爹链往上找**（凭据就是那个动态属性），不问 `itemAt()`：
    //   `itemAt()` 要的是视图已经算好的行位置，而这一句的调用方（rebuildRows）正好是在
    //   刚把行换掉、那一趟排版还欠着的时刻喊的（所以它排了个 0 毫秒的定时器，见那儿）。
    //   顺着爹链找只依赖"这个控件现在挂在谁底下"，天然不受行位置准不准的影响；而且
    //   `widgetAt()` 问不到行控件时（光标在行之间的空白、在那条悬浮操作条上、或者几何
    //   还没落地）这一趟自然什么都不做 —— 不会露错一行。
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
