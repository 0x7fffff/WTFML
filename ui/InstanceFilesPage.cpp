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
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>  // isCurrentPage()：往上找最外面那个堆叠容器
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

/// 名字那一格上"上链接之前那个颜色"存在哪儿（见 makeLink / unmakeLink）。
///
/// NOTE: 走动态属性而不是在页上再开一张 `QHash<QLabel*, QColor>`：色和那一格是**同生共死**
///       的（行一重建，键就悬空了），而 Qt 的控件本来就能挂自己的一份数据。
constexpr char kLinkPreviousColorProperty[] = "instanceFileLinkPreviousColor";

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

/// 把一格名字弄成**能点的样子**：调色板里"链接"那一档的颜色 + 手指光标 + 悬停提示。
///
/// ★ 2026-09-27 补（见 setResourceForPath()）。三样都是**看得见的承诺**，缺一样用户就
///   不知道该不该点：颜色和光标说"这一格能点"，悬停提示说"点了会去哪儿"（这一下要
///   **离开这一页**，不说清楚就是把人突然送走）。
///
/// NOTE: 颜色走 `QPalette::Link`（调色板里"能点的文字"那一档），**不自己写死一个蓝** ——
///       和这一页别处取 PlaceholderText / Highlight 是同一条规矩（见 makeSecondary）。
/// NOTE: 悬停提示是**两行**：上面那句是"点下去会去哪儿"，下面那行是这个文件的**完整
///       路径**（名字那一格装不下时是从中间省略的，尾巴上看不全 —— 那条路径本来就是
///       这一格原来的提示，换成链接不该把它弄丢）。
void makeLink(QLabel *label, const QString &tip, const QString &path) {
    if (label == nullptr) {
        return;
    }
    QPalette palette = label->palette();
    // ★ 先把"原来那个颜色"存下来（见 unmakeLink()）：这一格可能已经被 makeSecondary()
    //   染成次要色（被禁用那几条），摘掉的时候必须还回**它自己**那个颜色。
    label->setProperty(kLinkPreviousColorProperty,
                       palette.color(QPalette::WindowText));
    palette.setColor(QPalette::WindowText, palette.color(QPalette::Link));
    label->setPalette(palette);
    label->setCursor(Qt::PointingHandCursor);
    label->setToolTip(tip + QLatin1Char('\n') + QDir::toNativeSeparators(path));
}

/// 把 makeLink() 那三样**摘掉**：颜色、光标、提示都回到"这一行只是个普通文件"的样子。
///
/// ★ 2026-09-27 补。有这一支是因为"认出来了"这件事**会变回去**：同一个路径上换了一个文件
///   （大小 / 修改时间变了 ⇒ 缓存钥匙变了）之后那一趟可能就认不出来了，而名字那一格还
///   顶着上一个资源的链接 —— 点下去什么都不发生（MainWindow 那边按新的钥匙查不到身份，
///   见 openInstanceFileResource()）。**一个「看着能点、点下去没反应」的名字比点不动更糟。**
///
/// NOTE: 颜色是**存下来再还回去**的，不是一律设回"正文色"：被禁用的那几条名字本来是
///       次要色（见 makeRow 里 makeSecondary 那句），一律设回正文色等于把"这一条关着"
///       那半句话擦掉。
/// NOTE: 光标用 `unsetCursor()`（回到"继承父控件"那一档），不是显式设成箭头 —— 箭头只是
///       恰好等于默认值，哪一天这一页换了光标策略，显式箭头就成了第三处知识。
void unmakeLink(QLabel *label, const QString &path) {
    if (label == nullptr) {
        return;
    }
    const QVariant previous = label->property(kLinkPreviousColorProperty);
    if (previous.isValid()) {
        QPalette palette = label->palette();
        palette.setColor(QPalette::WindowText, previous.value<QColor>());
        label->setPalette(palette);
        label->setProperty(kLinkPreviousColorProperty, QVariant());
    }
    label->unsetCursor();
    // 提示回到 makeRow 里那一句（名字那一格**本来就**挂着完整路径 —— 它从中间省略，
    // 尾巴上看不全）：摘掉链接不等于把那条路径也弄丢，所以这里不是"清空提示"。
    label->setToolTip(QDir::toNativeSeparators(path));
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
    outer->addWidget(m_list, 1);

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
    // 现在选着的那一档**可能已经不在里面了**（`setCanFilterUpdates(false)` 把「可更新」
    // 拿掉的那一刻）：那就退回「全部」——留着的话屏幕上是一条都不剩的列表，而那一栏里
    // 没有任何一格亮着，用户找不到是哪儿把他的列表弄没了。
    if (filters.at(wanted) != m_stateFilter) {
        m_stateFilter = StateFilter::All;
        wanted = 0;
    }
    m_stateCombo->setCurrentIndex(wanted);
}

void InstanceFilesPage::setCanFilterUpdates(bool canFilterUpdates) {
    if (m_canFilterUpdates == canFilterUpdates) {
        return;
    }
    m_canFilterUpdates = canFilterUpdates;
    fillStateCombo();
    // 那一格没了、而条件正好是它 ⇒ fillStateCombo() 已经把条件退回「全部」了，这一趟
    // 重列就是把列表放回来（**必须重列**：不重列的话屏幕上还是上一档筛剩下的那几条，
    // 而那一栏里亮着的是「全部」—— 列表和条件对不上，且没有任何东西会自己纠正它）。
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
    rebuildRows();
}

void InstanceFilesPage::setUpdateResult(const QStringList &outdatedPaths, bool checked) {
    m_outdatedPaths = QSet<QString>(outdatedPaths.cbegin(), outdatedPaths.cend());
    m_updateChecked = checked;
    // 只有"正选着「可更新」"这一档会因为这个结果改变列表内容 —— 别的三档下这一趟检测
    // 改变不了任何一行，白重列一遍（一个目录上百行）不值当。
    if (m_stateFilter == StateFilter::Update) {
        rebuildRows();
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

void InstanceFilesPage::refresh() {
    // 这一趟真的列了 ⇒ `setDirectory()` 那一笔欠账清了（**摆在最前**：下面任何一条早退
    // 路都不该把这一位留在"还欠着"上 —— 那会让判据以为这一页没列过）。
    m_refreshPending = false;
    if (m_list == nullptr) {
        return;
    }
    // 上面那一排那一格写"A 在看哪个目录"。**在目录存在性判断之前**就写：目录还不存在
    // （第一次开 mods/ 之前它本来就不在盘上）也得让人看见路径 —— 那时候这一页正是
    // "这个目录是空的/不存在"，路径恰恰是最该看见的东西。
    // 分隔符统一成 Windows 那种反斜杠：这是给用户看的字，不是喂给接口的路径。
    const QString shownDir = QDir::toNativeSeparators(m_directory);
    m_pathLabel->setText(shownDir);
    // 装不下时是从中间省略的，两头那几段还能看；整条路径靠悬停提示给（见构造里那段 NOTE）。
    m_pathLabel->setToolTip(shownDir);
    // clear() 会连同 setItemWidget() 挂上去的那些行控件一起删掉（列表持有它们）。
    m_list->clear();
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
    // clear() 会连同 setItemWidget() 挂上去的那些行控件一起删掉 —— 统计队列/表跟着作废
    // （理由逐字同 refresh() 里那一段）。
    m_list->clear();
    m_statQueue.clear();
    m_statLabels.clear();

    int shown = 0;
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
        ++shown;
    }
    // ★ 「目录本来就是空的」和「筛完一条不剩」是两件事（见 m_filteredEmpty）：前者底下那
    //   两排筛选整块收起来，后者留着让用户把条件改回来。所以这里只在"目录里有东西"时
    //   才把后者置起来。
    m_filteredEmpty = !m_empty && shown == 0;

    applyState();

    // 目录行的「大小 · 时间」要递归遍历才拿得到，**排在行都摆出来之后**（见 .cpp 顶部
    // 那段和 fillDirectoryStats）—— 所以这一页是先出来、数字后到。
    if (m_statTimer != nullptr && !m_statQueue.isEmpty()) {
        m_statTimer->start();
    }
}

QWidget *InstanceFilesPage::makeRow(const QString &shownName, const QString &path, bool isDir,
                                    bool isDisabled, qint64 size, const QDateTime &modified) {
    auto *row = new QWidget(m_list);
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
    // ★ 名字上那一发（2026-09-27）：装在这一格上的 eventFilter 认的是"这一条记着账没有"
    //   （见 setResourceForPath()），所以**每一格都装** —— 身份是异步回来的，
    //   那时候行早就建好了，装晚一格就等于那一条永远点不动。
    //   属性记的是"这一格是哪一条"：鼠标落在哪一格上，就得知道那是哪一行。
    name->setProperty(rowPathProperty(), path);
    name->installEventFilter(this);
    // 认出来了的那几条：名字是个**链接**（颜色 + 手指光标 + 悬停提示）。
    // ★ 这一句必须排在 makeSecondary() **之后** —— 被禁用那几条的名字本来是次要色，
    //   而"这一条点得动"比"这一条关着"更该看得见（关着这件事还有开关和「（已禁用）」
    //   那四个字在说）。
    const auto tip = m_resourceTips.constFind(path);
    if (tip != m_resourceTips.constEnd()) {
        makeLink(name, tip.value(), path);
    }
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

    if (m_canToggle && !isDir) {
        // ★ 2026-09-27：从"两颗文字按钮（禁用 / 启用）"换成**一个拨动开关**
        //   （用户原话：「启用和禁用使用开关的那种形式」）。文字按钮说的是"点下去会发生
        //   什么"，开关说的是"现在是什么状态" —— 一列下来扫一眼就知道哪几个是关着的。
        //
        // NOTE: 开关的位置**固定在行尾倒数第二格**（删除那颗在最后）—— 一列下来两列按钮
        //       各自对齐。**只给"游戏会自己扫的那三个目录里的文件"**（见 setCanToggle）。
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

    m_pathRow->setVisible(!unavailable);
    m_list->setVisible(!unavailable && !empty && !filtered);
    m_notice->setVisible(unavailable || empty || filtered);
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
        m_notice->setText(m_updateChecked ? updateNoneText() : updateNotCheckedText());
        return;
    }
    m_notice->setText(noMatchText());
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
    if (m_icons.contains(path)) {
        const QPixmap icon = m_icons.value(path);
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
    m_icons.insert(path, icon);
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
    // ★ 空 tip 有两种意思，**两种都要办**（见 .hpp 和 unmakeLink() 那两段）：
    //   ① 这一条**从来**没认出来过 ⇒ 什么都不用做（"点不动"是默认状态，不是一条要摆出来
    //      的状态；记一条空的等于给每一行都记一笔账）；
    //   ② 上一趟认出来了、这一趟没有（同一个路径上换了内容）⇒ 必须把上一趟戴上的链接
    //      **摘掉**，否则那一行一直是个链接的样子、点下去什么都不发生。
    //   两件事靠 `remove()` 的返回值分开：删掉了 = 走过 ② 那一支。
    const bool hadLink = m_resourceTips.remove(path) > 0;
    if (tip.isEmpty() && !hadLink) {
        return false;
    }
    // 和 setIconForPath() **同一个顺序、同一个理由**：先记账、再找行 —— 图标/身份回来的
    // 那一刻用户正好点了「刷新」（那几毫秒里这一页没有行）时，这一条得记住"认出来了"，
    // 否则「刷新」之后的那个名字又变成点不动的了，而 MainWindow 那边不会再推第二遍。
    // （空 tip 那一支上面已经 remove 过了，这里不用再补一次插入。）
    if (!tip.isEmpty()) {
        m_resourceTips.insert(path, tip);
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
        QLabel *label = rowWidget->findChild<QLabel *>(nameLabelName());
        if (tip.isEmpty()) {
            unmakeLink(label, path);
        } else {
            makeLink(label, tip, path);
        }
        return true;
    }
    return false;
}

bool InstanceFilesPage::eventFilter(QObject *watched, QEvent *event) {
    // 名字那一格上**松开左键** ⇒ 这一条要是认得出是哪个资源，请上面那一层开它自己的
    // 那一页（见 setResourceForPath()）。
    //
    // NOTE: 判据是"这一条记着账没有"，不是"这一格长得像不像链接"：颜色和光标是画给人看的，
    //       这一发才是真动作；拿长相当判据的话，改一次样式就会静默改掉行为。
    // NOTE: 只有**松开**才算一下。按下就动手的话，用户本来只想把这一行选中、手指在名字上
    //       抖一下，就已经进那一页了。
    // NOTE: 吃掉的也只有这一下松开（返回 true）。press 照旧往上冒 —— 列表该选中的照样
    //       选中；双击那条路走的是另一发事件（itemActivated，见构造里那段 NOTE），
    //       一个字都没动。
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            const QString path = watched->property(rowPathProperty()).toString();
            if (!path.isEmpty() && m_resourceTips.contains(path)) {
                emit resourceRequested(path);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
