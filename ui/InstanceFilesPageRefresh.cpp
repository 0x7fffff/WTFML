/**
 * @file InstanceFilesPageRefresh.cpp
 * @brief 把目录列成条目：`refresh()`。这一页的"扫描"全在这里。
 *
 * 【为什么单独一个翻译单元】
 *
 * `m_entries` 是这一页的数据源（建行、筛选、统计、还有"这一条是哪个文件"的账，全从它
 * 读），而它只在 `refresh()` 里被重写。这一片于是是这个状态集合的唯一产地：
 *
 * - 路径那一格写"这一页正在列哪几个文件夹"（「结构 / 原理图」那一页不止一个）；
 * - 清表、清统计队列（**先清队列再清表**，只清表的话下一趟会拿着悬空路径白跑）；
 * - 三种模式（全部 / 只要目录 / 只要文件）、禁用后缀那一档；
 * - 列完交给 `rebuildRows()`：建行 + 过筛选 + 摆可见性 + 起统计定时器都在那一处。
 *
 * 【几条踩过的坑】
 *
 * - `m_list->clear()` **必须挡信号**：它会发一次 `itemSelectionChanged`，而那一发接的是
 *   重算 `m_selectedPaths` —— 此刻表已空，用户的选择在这一句上就被抹平，`rebuildRows()`
 *   想按路径还回去时手里那份账已经空了；
 * - 条目**故意不给文字**：行里是一个 `setItemWidget()` 摆上去的透明控件，代理画的字会从
 *   它底下露出来，屏幕上是同一个名字画两遍；
 * - 一个来源不见了就跳过它接着列别的 —— 不要因为一个存档被删就把整页判成空的。
 */

#include "InstanceFilesPage.hpp"

#include "ElidedLabel.hpp"
#include "InstanceFilesPageInternals.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QSignalBlocker>

void InstanceFilesPage::refresh() {
    QElapsedTimer refreshClock;
    refreshClock.start();
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
    if (refreshClock.elapsed() > 40) {
        qInfo() << "[UI timing] files page refresh" << m_directory << m_entries.size()
                << "entries," << refreshClock.elapsed() << "ms";
    }
}



