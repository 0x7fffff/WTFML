/**
 * @file InstanceFilesPage.hpp
 * @brief 「实例设置 → 文件管理」那几页共用的**一个目录的列表页**（模组 / 资源包 /
 *        数据包 / 光影包 / 原理图 / 存档）。
 * @author 0x7fffff
 * @date 2026-09-26
 * @note 「文件管理」那八页里有六页是同一个形状：**列一个目录，一行一个条目，
 *       上面一排（路径 + 下载新的 / 刷新 / 打开文件夹），行尾两颗按钮（启用·禁用 / 删除）**。
 *       差别只有五条：
 *         - 列出什么（存档只列文件夹，别的文件文件夹都列）；
 *         - 能不能启用·禁用（只有模组 / 资源包 / 光影包 —— 游戏会自己扫这三个目录）；
 *         - 能不能删（存档不行，见下面 setCanDelete() 那段）；
 *         - 顶上有没有那颗「下载新 X」（只有下载页里真有对应一栏的那四页有，
 *           见 setDownloadText()）；
 *         - 名字点不点得动（认得出是哪个资源的那几条点一下进它自己的那一页，
 *           见 setResourceForPath()）。
 *       这五条都是 setter，所以六页共用这一个控件，不写六份。
 *
 * 【2026-09-28 又多了两排：一条搜索 + 一个状态选择】
 *
 * 用户原话：「模组等的下载新/刷新/打开文件夹的上方加一个搜索，三个按钮的下方一个选择
 * 全部/启用/禁用/可更新」。所以正文从上到下是：**搜索框 → 那一排按钮 → 状态选择 →
 * 列表**（搜索在上、选择在下，是用户点名要的位置，不是摆错了）。
 *
 * 两样都是**筛这一页的列表**，不改盘上任何东西：
 *   - 搜索匹配的是"这一条在界面上叫什么"（以及盘上那个文件名），子串、不分大小写；
 *   - 状态那一栏 = 全部 / 启用 / 禁用（+ 可更新，见 setCanFilterUpdates）。
 *
 * 【这一层只**显示**，不碰盘】
 *
 * 和 InstanceSettingsDialog 同一条约定：它列目录（读），但**删除 / 改名一个字节都不动**
 * —— 那两件事分别发 toggleRequested() / deleteRequested()，由 MainWindow 去问一句、
 * 去写盘、再回头喊 refresh()。理由和「导出启动脚本」那条一样：弹话要走 OverlayHost
 * （InlineMessage 的每一个入口都要它），而这一层手上没有；而且 ui/ 不 include src/，
 * 它连 AppPaths 都够不着。
 *
 * 【为什么每行是一个真控件，而不是自己画】
 *
 * 仓库里别的列表（实例树、版本树、资源表）都是「QListWidget + 代理自己画行 + 自己
 * hit-test 认下那一发」。这一页**故意不那样**：文件列表的行数可能几十上百，每行两颗
 * 按钮又要悬停变色，用代理画等于把「鼠标落在哪颗按钮上」这件事重写一遍（那套东西
 * 在 InstanceGroupDelegate 里踩过的坑有注释在）。setItemWidget() 让 Qt 去做命中测试，
 * 代价是行多的时候慢一点 —— 而一个 mods 文件夹通常就是几十个 jar，够用。
 */
#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>

class QAbstractButton;
class ElidedLabel;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QToolButton;
class QTimer;

class InstanceFilesPage : public QWidget {
    Q_OBJECT

public:
    /// 这一页列什么。**只有存档那一页不是 FilesAndDirs**。
    enum class Mode {
        FilesAndDirs,  ///< 文件 + 文件夹（一个资源包 / 数据包 / 光影包都可以是一个目录）
        DirsOnly,      ///< 只列文件夹（一个存档就是一个目录，saves 底下那些文件不是存档）
    };

    explicit InstanceFilesPage(QWidget *parent = nullptr);

    /// 列什么（见 Mode）。默认 FilesAndDirs。
    void setMode(Mode mode);

    /// 行尾要不要那颗「启用 / 禁用」。**只有游戏会自己扫的那三个目录给 true**
    /// （模组 / 资源包 / 光影包）—— 数据包里那些要自己搬进存档、原理图要 Litematica
    /// 主动去读、存档根本不是一个文件，改了名都只会让用户以为生效了。
    void setCanToggle(bool canToggle);

    /// 行尾要不要那颗「删除」。**存档那一页给 false**：存档是用户手里唯一那份，
    /// 启动器没有副本、也没有回收站，误删一个玩了几百小时的世界是找不回来的。
    /// 别的几页给 true —— 那些东西都能从下载页再下一份。
    void setCanDelete(bool canDelete);

    /// 行尾要不要那支笔（一次「编辑」）。用户 2026-09-28 原话：「右边画一支笔意为编辑，
    /// 和基岩版一样」。**只有存档那一页给 true**。
    ///
    /// 给的这一页同时还多做一件事：**目录行的「大小 · 时间」改成真的**（见 .cpp 里
    /// fillDirectoryStats()）。这两件事是一套的 —— 存档页要的就是"一个世界有多大、
    /// 上次什么时候玩的"，而别的几页那些目录（`mods/mcef-libraries/` 之类）算一个字节
    /// 数出来没有任何意义，还不如那个破折号。
    void setCanEdit(bool canEdit);

    /// 顶上那颗「导入世界」（只有存档那一页给）。用户 2026-09-28 原话：「顶端加个导入
    /// 世界按钮」。
    ///
    /// @param text 按钮上写的字。**空串 = 这一页没有这颗按钮**（和 setDownloadText 一样）。
    /// @param tip  悬停提示。
    ///
    /// NOTE: 和「下载新 X」一样，这一层**只发信号**：挑哪个目录、拷到哪儿、要不要问一句
    ///       重名 —— 全在 MainWindow（这一层够不着 AppPaths，也开不了系统那个选择框）。
    void setImportText(const QString &text, const QString &tip);

    /// 顶上那颗「批量编辑」。**今天是个占位**（用户原话：「（这个先做占位）」）——
    /// 按钮摆在那儿、点下去 MainWindow 会说一句"还没做"。
    ///
    /// NOTE: 判据和 setImportText() 一样是"文字空不空"（不是另开一个 bool）—— 这一页是
    ///       同一段循环搭出来的六页，多一位状态就多一处"哪几页有"的知识。
    void setBatchText(const QString &text, const QString &tip);

    /// 目录空着的时候那一句话（每一页不一样：模组说"去下载页装一个"，
    /// 存档说"进游戏存一个"）。默认给一句通用的。
    void setEmptyText(const QString &text);

    /// 顶上那颗「下载新 X」—— 只有下载页里有对应一栏的那四页给（模组 / 资源包 / 数据包 /
    /// 光影，见 InstanceSettingsDialog::filesDownloadText()）。用户 2026-09-27 原话：
    /// 「在实例设置里模组 资源包 数据包 光影包里添加下载新光影，按了以后导向下载页对应类别」。
    ///
    /// @param text 按钮上写的字。**空串 = 这一页没有这颗按钮**（原理图在下载页里没有对应的
    ///        那一栏、存档不是"下载"来的、配置文件根本不在下载页里）。
    /// @param tip  悬停提示（说清按下去会去哪儿 —— 它要**离开这一页**）。
    ///
    /// NOTE: 这一层**不知道去哪儿**：它只发 downloadRequested()，切选项卡和挑那一栏是
    ///       MainWindow 的事（这一层既够不着选项卡，也不该知道"下载页左栏有几栏"）。
    /// NOTE: 它和「刷新」「打开文件夹」同处顶上那一排 ⇒ 这一页「用不上」时（setUnavailable()）
    ///       跟着一起收起来。这是有意的：光影包那一页正在说"这个实例装不了光影"的时候，
    ///       把人送去下载页找光影包是**指错路**（该先装 Iris，那是模组那一栏的事）。
    void setDownloadText(const QString &text, const QString &tip);

    // ---------- 搜索 + 状态筛选（2026-09-28，见文件头）----------

    /// 这一页每一页都有的四个状态档（那一栏里的一格）。**「可更新」要 setCanFilterUpdates(true)
    /// 才出现**（见那一处的理由）。
    enum class StateFilter {
        All,       ///< 全部
        Enabled,   ///< 启用（盘上没有 `.disabled` 后缀的那些）
        Disabled,  ///< 禁用
        Update,    ///< 可更新（这一趟更新检测查出来有新版本的那些）
    };

    /// 搜索框里写什么（空串 = 不筛）。**给验证工程和"把条件清回来"用** —— 用户敲字走的是
    /// 那条 QLineEdit 自己的路（见 .cpp）。
    void setSearchText(const QString &text);
    QString searchText() const { return m_searchText; }

    /// 那一栏里选哪一档。**唯一入口**（下拉框的 currentIndexChanged 也接到这儿）。
    void setStateFilter(StateFilter filter);
    StateFilter stateFilter() const { return m_stateFilter; }

    /// 状态那一栏里**要不要有「可更新」**。只有"能查更新"的那几页给 true（下载页里真有
    /// 对应一栏的那四页，见 setDownloadText）—— 原理图 / 存档 / 配置文件那几页上的文件
    /// 根本不在任何资源平台的清单里，多一格永远选不出东西的筛选项等于摆一个坏键。
    ///
    /// NOTE: 换这个开关时，如果正选着「可更新」而它要被拿掉，会**退回「全部」**（不是
    ///       留着一个已经不存在的条件 —— 那样屏幕上是一条都不剩的列表）。
    void setCanFilterUpdates(bool canFilterUpdates);

    /// 这一趟更新检测的结果（见 MainWindow::finishUpdateCheck / ModDependencies::judgeUpdate）。
    ///
    /// @param outdatedPaths **查出来有新版本**的那些条目的绝对路径（= `pathRole()` 那一格，
    ///        禁用着的那条带着 `.disabled`）。这一份是**整份**，不是追加。
    /// @param checked       这个实例查过没有。**「没查过」和「查过、没有」是两件事**：
    ///        前者选「可更新」时该说"先点一下检查更新"，后者该说"都是最新的"。
    ///
    /// NOTE: 这一层只认路径 ⇒ 它在"盘上那一条现在叫什么"这件事上跟 refresh() 同一条口径
    ///       （两边都是 `pathRole()`），所以禁用/启用之后路径变了、这一条也就不在表里了
    ///       —— 那正是对的：改过名之后上一趟查出来的结论本来就不作数了。
    void setUpdateResult(const QStringList &outdatedPaths, bool checked);

    /// 这一页看哪个目录（绝对路径）。**由调用方算好递进来** —— 目录名是 MC 的约定，
    /// 仓库里已经有唯一一份（src/download/ResourceCatalog::folderForKind），
    /// 这一层不抄第二份。
    void setDirectory(const QString &dir);
    QString directory() const { return m_directory; }

    /// 这一页**暂时用不上**（装了 Iris / OptiFine 才谈得上光影包，装了 Litematica 才谈得上
    /// 原理图）。非空 = 整页换成这一句解释（列表和那排按钮一起收起来 —— 那时候连
    /// 「打开文件夹」都容易让人以为里面的东西会有用）。空串 = 恢复正常。
    void setUnavailable(const QString &reason);

    /// 重新列一遍目录。切到这一页时、以及 MainWindow 动过盘之后都会调它。
    void refresh();

    /// 给一条行换上**真图标**（资源作者传的那张：模组图标 / 资源包封面，2026-09-27）。
    ///
    /// 由 MainWindow 递进来（它才知道怎么按文件哈希反查出这是哪个资源、又怎么走
    /// ResourceStore 那套图标缓存）—— 这一层不认识网络、也不认识"图标是哪来的"。
    ///
    /// @param path 这一条的**绝对路径**（就是 item 上 pathRole() 那一格）。
    /// @param icon 那张图。**空 pixmap = 这条没有图标**，那一格退回占位图
    ///        （不是画一个空白：空白和"还没查完"在屏幕上是同一个样子）。
    /// @return 认不认得出这一条（false = 这一页上没有这个路径 —— 用户可能刚刷新过、
    ///         或者已经删了它。**不是错误**，调用点什么都不用做）。
    ///
    /// NOTE: 递进来的每一张都**按路径记在页上**（`m_icons`），所以「刷新」之后再摆出来的
    ///       那一行还是这张图 —— 见 refresh() 里那段。空 pixmap 也记（它记的是"这一条问过
    ///       了、没有"），所以刷新之后不会又退回占位图再等一次回答。
    bool setIconForPath(const QString &path, const QPixmap &icon);

    /// 这一条**认出来了是哪个资源** ⇒ 名字点一下能进它自己的那一页（2026-09-27）。
    ///
    /// ★ 用户原话：「除了模组以外的几个都是点击模组名称自动进入下载界面（**前提是匹配
    ///   上了**）」—— 所以"点得动"这件事本身就是"这一条认出来了"的信号（不是每一行都
    ///   能点，点了没反应的那些才是最难用的界面）。
    ///
    /// @param path 这一条的**绝对路径**（就是 item 上 pathRole() 那一格）。
    /// @param tip  悬停提示（说清点下去会去哪儿 —— 它要**离开这一页**）。
    ///        **空串 = 这一条没认出来**：名字就是一行字，点不动、也不摆出点得动的样子。
    ///        ★ 空串这一支**不是"没话说"**（2026-09-27）：同一个路径上换了内容之后，
    ///          上一趟认出来的那个资源就不作数了，得把那一格已经戴上的链接**摘掉** ——
    ///          留着的话那一行还是个链接的样子、点下去什么都不发生。所以空串这一支
    ///          只在"这一格本来记着账"的时候办事（见 .cpp 里 `hadLink` 那一段）。
    /// @return 同 setIconForPath()：这一页上现在有没有这个路径（false 不是错误 ——
    ///         用户可能刚刷新过、或者已经删了它）。
    ///
    /// NOTE: 和 setIconForPath() 一样**按路径记账**（`m_resourceTips`），所以「刷新」之后
    ///       重列出来的那一行照样是能点的 —— 身份是反查来的（读盘 + 联网，几秒），
    ///       不能跟着重列再问一遍。
    /// NOTE: 这一层**不知道点了会到哪儿**，它只发 resourceRequested(path)：去哪一页、
    ///       开不开得成，都由 MainWindow 说了算（"这个文件是哪个资源"那张表在它手上）。
    bool setResourceForPath(const QString &path, const QString &tip);

    // ---------- 给验证工程用的访问器 ----------
    //
    // 条目上挂的东西走 item->data()，**唯一出处是下面两个角色**：
    //   pathRole()     = 绝对路径（发给 MainWindow 的就是它）
    //   disabledRole() = 这一条现在是不是禁用着（`X.jar.disabled`）
    // 行尾那两颗按钮按**行号**取 —— 行号会随 refresh() 变，所以别跨 refresh 存着。
    QListWidget *list() const { return m_list; }
    /// 行尾那个「启用 / 禁用」**开关**（2026-09-27 起它是 `ToggleSwitch`，从前是一颗写着
    /// 「禁用」/「启用」的 QToolButton）。**对外只当它是个 `QAbstractButton`** —— 调用方
    /// 要的就是"点它"和"它现在开着没有"（`click()` / `isChecked()`），不必知道它是画出来的。
    /// NOTE: 存着它的类型是 ToggleSwitch 的话，`InstanceFilesPage.hpp` 就得把那个头拖进来，
    ///       而这一页的每一个使用者（实例设置对话框、四个验证工程）都得跟着编一遍。
    QAbstractButton *toggleButtonAt(int row) const;
    /// 行尾那颗垃圾桶（**字形画的，没有文字**：文字仍然给着，只是 QToolButton 默认
    /// `ToolButtonIconOnly` 不画它 —— 验证工程按 `text()` 认它是干什么的）。
    QToolButton *deleteButtonAt(int row) const;
    /// 行尾那支笔（同上：字形画的，`text()` 里写着「编辑」）。**没给 setCanEdit(true)
    /// 的页上每一行都是 nullptr**。保存档页的话 `toggleButtonAt()` / `deleteButtonAt()`
    /// 反过来恒是 nullptr（那两档这一页都没开）。
    QToolButton *editButtonAt(int row) const;
    QPushButton *refreshButton() const { return m_refresh; }
    QPushButton *openFolderButton() const { return m_openFolder; }
    /// 顶上那两颗（只有存档那一页露着，见 setImportText / setBatchText）。
    QPushButton *importButton() const { return m_import; }
    QPushButton *batchButton() const { return m_batch; }
    /// 第 @p row 行那一格**名字**（ElidedLabel，对外当个 QLabel）。验证工程按它量
    /// "认出来的那几条长什么样"（颜色 / 光标 / 悬停提示），以及往它上面发一下鼠标松开、
    /// 看这一页发不发 resourceRequested()。
    QLabel *nameLabelAt(int row) const;
    /// 顶上那颗「下载新 X」。**没接过 setDownloadText() 的页上它是藏着的** —— 验证工程量
    /// 的也是它"露不露"（`isVisibleTo(这一页)`），不是"存不存在"（它每一页都有，只是空着）。
    QPushButton *downloadButton() const { return m_download; }
    QLabel *noticeLabel() const { return m_notice; }
    /// 上面那条搜索框（见 setSearchText）。验证工程靠它核"敲几个字之后列表剩几条"。
    QLineEdit *searchEdit() const { return m_search; }
    /// 那一栏（全部 / 启用 / 禁用 / 可更新）。验证工程按 `currentText()` 核它现在选着哪一档，
    /// 以及**有「可更新」的那几页是哪几页**（`count()`）。
    QComboBox *stateCombo() const { return m_stateCombo; }
    /// 那一栏左边那个标签（写「显示」）。**没接过筛选项的页上整排都是藏着的** —— 量的
    /// 是 `isVisibleTo(这一页)`。
    QLabel *stateLabel() const { return m_stateLabel; }
    /// 上面那一排最左边那格（写的是这一页在看哪个目录）。文字由 refresh() 灌 ——
    /// 验证工程按它核"切到哪一页、那一格就写哪个目录"这一条。
    /// NOTE: 定义在 .cpp 里（不在这儿就地写）：它那一格的真身份是 ElidedLabel，
    ///       就地写就得把 ElidedLabel.hpp 拖进每一个用到这一页的地方 —— 对外仍旧
    ///       只当它是个 `QLabel`（调用方要的是 setText / text）。
    QLabel *pathLabel() const;

    static int pathRole() { return Qt::UserRole; }
    static int disabledRole() { return Qt::UserRole + 1; }

    /// 禁用的写法：把后缀**接在完整文件名后面**（`X.jar` → `X.jar.disabled`），
    /// 启用就是把它去掉。**不是**改扩展名也不是移到别的目录 —— 游戏扫 mods 的时候
    /// 只认 `.jar` 结尾，所以这么改一定扫不到；而用户自己一眼看得出原来是什么文件。
    static QString disabledSuffix() { return QStringLiteral(".disabled"); }
    /// 界面上那条被禁用的条目叫什么（原名 + 这四个字）。
    static QString disabledMark() { return QStringLiteral("（已禁用）"); }

    /// @p bytes 的可读写法（1.5 KB / 2.0 MB …）。**唯一出处** —— 验证工程也拿它核对。
    static QString formatSize(qint64 bytes);

    // ---------- objectName（验证工程靠它们认控件） ----------
    static QString listName() { return QStringLiteral("instanceFilesList"); }
    static QString pathLabelName() { return QStringLiteral("instanceFilesPath"); }
    /// 上面那条搜索框、下面那一栏状态选择（+ 它左边那个标签）。各自只有一个。
    static QString searchEditName() { return QStringLiteral("instanceFilesSearchEdit"); }
    static QString stateComboName() { return QStringLiteral("instanceFilesStateCombo"); }
    static QString stateLabelName() { return QStringLiteral("instanceFilesStateLabel"); }
    static QString refreshButtonName() { return QStringLiteral("instanceFilesRefreshButton"); }
    static QString openFolderButtonName() {
        return QStringLiteral("instanceFilesOpenFolderButton");
    }
    /// 顶上那颗「下载新 X」（名字和上面两颗同处一排，认法也一样是 findChild）。
    static QString downloadButtonName() { return QStringLiteral("instanceFilesDownloadButton"); }
    static QString noticeLabelName() { return QStringLiteral("instanceFilesNotice"); }
    /// 行首那个**名字**（ElidedLabel）。一页里有很多个、名字都一样 —— 取法和下面那两颗
    /// 一样按行取（`list()->itemWidget(item)->findChild<QLabel*>(nameLabelName())`）。
    /// 验证工程靠它核三件事：名字装不下时是**省略号**收尾（不是硬裁）、省略是从
    /// **中间**截的（尾巴上挂着「（已禁用）」那四个字，截尾巴等于把状态先说没了），
    /// 以及它**确实拿到了宽度**（大于 nameLabelMinWidth()；曾经它被挤成过 0 宽，
    /// 一整列名字全看不见 —— 见 InstanceFilesPage.cpp::makeRow 里那段）。
    static QString nameLabelName() { return QStringLiteral("instanceFilesNameLabel"); }

    /// 名字那一格**至少**要有这么宽（px）。名字那一格是这一行的主要内容，宁可让这一行
    /// 溢出被裁，也不许它缩到看不见。
    static int nameLabelMinWidth() { return 120; }

    /// 名字那一格上记着"这是哪一条"的**动态属性**（`QObject::property`）。
    ///
    /// 只给这一页自己的 eventFilter() 用：鼠标落在某一格名字上时，得知道那是哪一条
    /// （见 makeRow / eventFilter）。**不是给外面读的** —— 行是哪一条的唯一出处是
    /// `item->data(pathRole())`。
    static const char *rowPathProperty() { return "instanceFilePath"; }

    /// 名字底下那一行小字（大小 · 时间，ElidedLabel）。和名字同一列 —— 名字在上、
    /// 它在下（从前这两样和那两颗按钮挤在同一行里，字号一大名字就没地方了）。
    static QString metaLabelName() { return QStringLiteral("instanceFilesMetaLabel"); }
    /// 行尾那两颗。**一页里有很多颗、名字都一样** —— 认它们要按行取（见上面那个
    /// toggleButtonAt()），从页上 findChild 拿到的只是第一行那一颗。
    static QString toggleButtonName() { return QStringLiteral("instanceFilesToggleButton"); }
    static QString deleteButtonName() { return QStringLiteral("instanceFilesDeleteButton"); }
    /// 行尾那支笔。和上面那两颗**同一个取法**（一页里很多支、名字都一样 ⇒ 按行取，
    /// 见 editButtonAt()）。
    static QString editButtonName() { return QStringLiteral("instanceFilesEditButton"); }
    /// 顶上那两颗（「导入世界」/「批量编辑」）。和 downloadButtonName() 同处一排、同一个
    /// 认法；**没接过 setImportText / setBatchText 的页上它们是藏着的**（和「下载新 X」
    /// 一样：每一页都建，露不露由文字说了算）。
    static QString importButtonName() { return QStringLiteral("instanceFilesImportButton"); }
    static QString batchButtonName() { return QStringLiteral("instanceFilesBatchButton"); }

    /// 行首那一格**图标**（QLabel，里面是一张 pixmap，见 setIconForPath()）。
    /// 和上面那两颗一样：一页里有很多个、名字都一样 ⇒ 按行取。
    /// 验证工程靠它核两件事：图标回来之后**这一格真换上了**（pixmap 变了），以及
    /// 六页里**每一行都有**这一格（有图标和没图标的两行才会左右对得齐）。
    static QString iconLabelName() { return QStringLiteral("instanceFilesIconLabel"); }

    static QString refreshText() { return QStringLiteral("刷新"); }
    static QString openFolderText() { return QStringLiteral("打开文件夹"); }
    static QString toggleOnText() { return QStringLiteral("禁用"); }
    static QString toggleOffText() { return QStringLiteral("启用"); }
    static QString deleteText() { return QStringLiteral("删除"); }
    /// 行尾那支笔。和 deleteText() 一样**只活在 text() / 悬停提示里**（图标是画的，
    /// QToolButton 默认不画文字）—— 验证工程按它认那一格是干什么的。
    static QString editText() { return QStringLiteral("编辑"); }
    /// 目录行的「大小 · 时间」还没算出来的那一格写什么（见 fillDirectoryStats()）。
    static QString statsPendingText() { return QStringLiteral("正在统计…"); }

    // ---------- 搜索 / 筛选那两排上的字（2026-09-28）----------
    //
    // 和上面那几串一样**只活在这儿**：下拉框那四格、搜索框的灰字、以及"筛完一条不剩"
    // 那三句话都由这一组说了算，验证工程也按它们核对（写死两份的话，改了字有一处对不上
    // 就是一条假绿）。
    static QString searchPlaceholder() { return QStringLiteral("搜索"); }
    static QString stateLabelText() { return QStringLiteral("显示"); }
    /// 那一栏里四格各写什么。**顺序就是下拉框里的顺序**（见 .cpp 构造那段）。
    static QString stateFilterText(StateFilter filter) {
        switch (filter) {
        case StateFilter::Enabled:
            return QStringLiteral("启用");
        case StateFilter::Disabled:
            return QStringLiteral("禁用");
        case StateFilter::Update:
            return QStringLiteral("可更新");
        case StateFilter::All:
            break;
        }
        return QStringLiteral("全部");
    }
    /// 筛完一条都不剩时那句（搜索词或状态把这一页筛空了）。**和"目录本来就是空的"那句
    /// 是两回事**（见 m_emptyText）：这一句底下那一排筛选还露着，用户正要靠它改回来。
    static QString noMatchText() { return QStringLiteral("没有符合条件的条目。"); }
    /// 选着「可更新」、但**这个实例还没查过更新**（见 setUpdateResult 的 checked）。
    /// 这句话得说清下一步去哪儿按 —— 不说的话用户只会以为"筛坏了"。
    static QString updateNotCheckedText() {
        return QStringLiteral("还没有查过更新 —— 到「概览」那一栏点一下「检查更新」，"
                              "查完回来这里就能按「可更新」筛。");
    }
    /// 选着「可更新」、查过了、这一页里一条要更新的都没有。
    static QString updateNoneText() {
        return QStringLiteral("查过了，这一页里的文件都没有新版本。");
    }
    /// 目录不存在 / 是空的时候那一句（每一页都可以自己盖掉，见 setEmptyText）。
    static QString emptyText() { return QStringLiteral("这个文件夹还是空的。"); }

signals:
    /// 用户点了「打开文件夹」，或者行首那颗（两种都开同一个目录）。
    /// **这一层不自己开**：开目录是 QDesktopServices 的事，而失败要弹一句 —— 那句话
    /// 得挂在覆盖层上（见文件头那段）。
    void openFolderRequested(const QString &path);

    /// 用户**点了一下某一条的名字**，而这一条是认得出是哪个资源的（见 setResourceForPath()）。
    /// @param path 这一条的绝对路径（**不是**资源身份 —— 身份在 MainWindow 那张表里）。
    /// **这一层不说去哪儿**：接的人负责把这一页收掉、开那个资源自己的那一页。
    /// NOTE: 没认出来的那几条**一个字都不会发**（名字就是一行字，点它跟点行身一样）。
    void resourceRequested(const QString &path);

    /// 用户点了顶上那颗「下载新 X」。**这一层不说去哪儿**（它只认得这一页的目录，
    /// 见 setDownloadText()）—— 接的人负责切到下载页对应的那一栏。
    void downloadRequested();

    /// 用户点了某一条行尾的「禁用」/「启用」。@param path 是**现在**的绝对路径
    /// （禁用着的那一条给的是带 `.disabled` 的那个）—— 加后缀还是去后缀由接的人判。
    void toggleRequested(const QString &path);

    /// 用户点了某一条行尾的「删除」。**这一层不问那句"真的要删吗"**，接的人问。
    void deleteRequested(const QString &path);

    /// 用户点了顶上那颗「导入世界」。**这一层不问"从哪儿导、导成什么名字"** —— 它连
    /// AppPaths 都够不着（见文件头那段），挑目录和拷文件全在 MainWindow。
    void importRequested();

    /// 用户点了顶上那颗「批量编辑」。**今天是个占位**：接的人负责说那句"还没做"。
    void batchEditRequested();

    /// 用户点了某一条行尾的「编辑」（那支笔）。@param path 是这一条的绝对路径
    /// （= `item->data(pathRole())`）。
    ///
    /// NOTE: 这一层**不说"编辑"是什么**（改名？编辑游戏规则？导出？）—— "一个世界能
    ///       做哪几件事"是 MainWindow 的知识，而且它会变（今天只有两条，将来会多）。
    ///       这一层只负责说"用户点了这一条的那支笔"。
    void editRequested(const QString &path);

    /// 用户**打开了**某一条（双击，或者选中之后按回车）。@param isDir 这一条是不是目录
    /// —— 目录和文件要的东西不一样（目录是"进去看"，文件是"打开它"）。
    ///
    /// NOTE: 这一发是**从「配置文件」那一页起才有的**（2026-09-27）。别的几页接不接都行
    ///       —— 不接就是从前那样（双击没反应），不是错误。
    /// NOTE: 判 `isDir` 用的是**这一条的路径现问一次 QFileInfo**，不是记在条目上的一个
    ///       角色：路径本身是权威（refresh() 也是这么判的），而且这一条随时可能刚被改过名。
    ///       真去问一次盘比多存一位可能过期的位可靠。
    void activateRequested(const QString &path, bool isDir);

protected:
    /// 名字那一格上那一发鼠标松开（见 setResourceForPath()）。装在**每一格名字**上
    /// （makeRow 里 installEventFilter），判据是"这一条记着账没有"。
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// 目录里的一条（**筛之前**的那一份）。`refresh()` 从盘上读一次存这儿，建行和筛选都
    /// 只读它 —— 于是"敲一个字就重筛一遍"不用再问一次盘（见 .cpp 里 refresh /
    /// rebuildRows 那段）。
    struct Entry {
        QString displayName;  ///< 界面上那一格写的（改名之后 + 「（已禁用）」）
        QString raw;          ///< 盘上那个名字（`X.jar.disabled`）
        QString path;         ///< 绝对路径（= pathRole() 那一格）
        bool isDir = false;
        bool disabled = false;
        qint64 size = 0;
        QDateTime modified;
    };

    /// 造一条行：**左边一列两行字**（名字一行、大小 · 时间一行，见 nameLabelName() /
    /// metaLabelName()）+ 尾巴上那两颗按钮，已经按状态摆好。
    QWidget *makeRow(const QString &shownName, const QString &path, bool isDir, bool isDisabled,
                     qint64 size, const QDateTime &modified);

    /// 四个状态（正常 / 空目录 / **筛完一条不剩** / 用不上）之间切换可见性。refresh()、
    /// rebuildRows() 和 setUnavailable() 都走这一处，免得几条路上摆得不一样。
    ///
    /// 「顺便收起来的」只有两种：**这一页用不上**、以及**目录本来就空**（那两种情况下
    /// 搜索/状态栏都没有可筛的东西）。筛完一条不剩时那两排**留着** —— 用户正要靠它们
    /// 把条件改回来。
    void applyState();

    /// 把 `m_entries` 按现在这两条筛选条件**重列一遍**（先 clear 再逐条建行）。
    ///
    /// 【为什么筛选 = 重列，而不是"把不中的那几行藏起来"】
    ///
    /// 这一页的行是 `setItemWidget()` 摆上去的**真控件**，"藏"要么走 `setHidden()`（隐藏
    /// 条目**不会**自动把那个行控件一起收起来 —— 它会留在原地，于是筛完屏幕上还是那些行）、
    /// 要么自己挨个 setVisible（那就等于把五行状态散在第二处）。重列走的是同一条老路
    /// （`refresh()` 一直在这么干），代价是每敲一个字重建一次行 —— 一个目录几十上百条，
    /// 这个代价当场就回来了。**换来的是一条：屏幕上剩下的每一条都真的过了筛选。**
    ///
    /// NOTE: 只重列**不读盘**（读盘那一步在 refresh() 里，存进 m_entries）—— 敲字时
    ///       "盘上有没有变"不是这一趟要回答的问题，用户按「刷新」才是。
    void rebuildRows();

    /// 这一条过不过现在这两条筛选条件（搜索词 + 状态栏）。
    bool entryMatchesFilter(const Entry &entry) const;

    /// 状态栏里第 @p index 格对应哪一档（那一格的数据里存的就是它，`Qt::UserRole`）。
    static StateFilter filterAt(const QComboBox *combo, int index);

    /// 按 `m_canFilterUpdates` 把状态栏那几格（重新）摆好，并把"现在选着哪一档"落回
    /// `m_stateFilter`。构造函数和 setCanFilterUpdates() 都走这一处。
    void fillStateCombo();

    /// 这一条那一格该摆什么：真图标在 `m_icons` 里就摆它，否则摆占位图（形状按 isDir 分，
    /// 见 .cpp 里 placeholderIcon）。makeRow() 和 setIconForPath() 都走这一处 ——
    /// 两处各写一遍的话，"没查到"那一档迟早会有一处漏掉、退成空白。
    QPixmap iconForPath(const QString &path, bool isDir, const QColor &placeholderColor) const;

    /// 把「正在统计…」那一格换成真的数（一个世界的字节数 + 上次游玩时间）。
    /// **一趟只算一行**，算完还有剩的就再排一趟 —— 理由写在 .cpp 里（一个装着几百个世界
    /// 的 saves/ 一次算完会让界面卡住十几秒，而那是页面已经摆出来之后卡住）。
    void fillDirectoryStats();

    Mode m_mode = Mode::FilesAndDirs;
    bool m_canToggle = false;
    bool m_canDelete = false;
    /// 行尾那支笔（「编辑」）露不露。**今天只有「存档」那一页为真**（见 setCanEdit()）。
    /// NOTE: 它同时还是"这一行的小字里画真数还是画一个 『—』"的开关 —— 那一页每一行都是
    ///       一个世界目录，统计大小/最后游玩时间是那一页专有的（别的那几页全是文件，大小
    ///       本来就有）。
    bool m_canEdit = false;
    QString m_emptyText;
    QString m_directory;
    /// 用不上的原因（空串 = 用得上）。见 setUnavailable()。
    QString m_unavailable;
    /// 上一次 refresh() 之后**目录里**是不是一条都没有（还没过筛选这一层）。**只由
    /// refresh() 写**（见 applyState()）。
    bool m_empty = true;
    /// 目录里有东西、但**这一趟筛完一条都不剩**（搜索词或状态栏干的）。**只由
    /// rebuildRows() 写**。和 m_empty 分开：前者底下要露着筛选那两排（用户正靠它改回来），
    /// 后者整块收起来（没东西可筛）。
    bool m_filteredEmpty = false;

    /// 目录里那些条目，**筛之前**的那一份（见 Entry / rebuildRows()）。
    QList<Entry> m_entries;

    /// 现在搜索框里写着什么（setSearchText / 用户敲字都写它）。**筛的是它**，不是控件上
    /// 此刻的文字 —— 免得"这一页在筛什么"这件事有两个出处。
    QString m_searchText;
    /// 状态栏里现在选着哪一档。默认「全部」。
    StateFilter m_stateFilter = StateFilter::All;
    /// 状态栏里要不要有「可更新」（见 setCanFilterUpdates）。**默认给**：能筛更新的那几页
    /// 是多数，不开的那三页由对话框点名关掉（宁可多一格"永远筛不出东西"的，也不要因为
    /// 忘了打开而少一格 —— 后者没人看得出来）。
    bool m_canFilterUpdates = true;

    /// 查出来**有新版本**的那些条目的路径（见 setUpdateResult）。在表里 = 这一条可更新。
    /// NOTE: 空表 + `m_updateChecked` 为假 = "还没查过"，那是两件事（见 updateNotCheckedText）。
    QSet<QString> m_outdatedPaths;
    /// 这个实例查过更新没有（见 setUpdateResult）。
    bool m_updateChecked = false;

    /// 已经拿到的**真图标**，按 `pathRole()` 那一格存（见 setIconForPath()）。
    ///
    /// 【为什么必须由这一页自己记着】
    ///
    /// `refresh()` 是 `m_list->clear()` + 重新建行 —— 挂在这一页上的图全跟着没了。而
    /// 「刷新」和「启用/禁用一条」都会走它（禁用就是改文件名，落盘之后要重列），图标却是
    /// 反查来的（读盘 + 联网，几秒），**不能跟着重来一遍**。记在这儿，重列出来的那一行
    /// 直接就是对的图。
    ///
    /// NOTE: **空的也存**（值是一张空 pixmap）：它记的是"这一条问过了、没有"，
    ///       和"还没问"是两件事（见 setIconForPath 的 NOTE）。
    /// NOTE: 这一份**跟着这一页活**（对话框一关就没了）—— 跨对话框的缓存是 MainWindow
    ///       那一份的事（按 路径+大小+时间 存，见 requestInstanceFileIcons 那段）。
    QHash<QString, QPixmap> m_icons;

    /// 认出来了是哪个资源的那几条，按 `pathRole()` 那一格存**悬停提示**（见
    /// setResourceForPath()）。在表里 = 这一条的名字点得动。
    ///
    /// NOTE: 和 m_icons 同一档、同一个理由：`refresh()` 是 `m_list->clear()` + 重新建行，
    ///       而身份是反查来的（几秒），不能跟着重列再来一遍。刷新之前记着的，重列出来
    ///       的那一行直接就是能点的。
    /// NOTE: 只记**认出来了**的那些（空 tip 不记账）—— 记一条空的是"这一条点不动"，
    ///       而那本来就是默认状态。
    QHash<QString, QString> m_resourceTips;

    /// **最上面那一排**：搜索框（2026-09-28，用户点名要摆在按钮那排**上方**）。
    /// 目录本来就空 / 这一页用不上的时候整排收起来（没东西可筛）。
    QWidget *m_searchRow = nullptr;
    QLineEdit *m_search = nullptr;
    /// 上面那一排（路径 + 两颗按钮）。「用不上」的时候整排一起收起来。
    QWidget *m_pathRow = nullptr;
    /// **按钮那排下面那一排**：「显示」+ 状态栏（用户原话是「三个按钮的下方一个选择」）。
    QWidget *m_stateRow = nullptr;
    QLabel *m_stateLabel = nullptr;
    QComboBox *m_stateCombo = nullptr;
    /// 是 ElidedLabel（从中间省略，见 .cpp 里构造那段的理由）；对外仍旧只当它是个 QLabel
    /// （见 pathLabel() 为什么定义在 .cpp 里）。
    ElidedLabel *m_pathLabel = nullptr;
    QPushButton *m_refresh = nullptr;
    QPushButton *m_openFolder = nullptr;
    /// 顶上那颗「下载新 X」（见 setDownloadText()）。**每一页都建**（同一个形状搭出来的），
    /// 没接文字的那几页是藏着的。
    QPushButton *m_download = nullptr;
    /// 顶上那两颗（「导入世界」/「批量编辑」）。同上：每一页都建，没接文字就藏着。
    QPushButton *m_import = nullptr;
    QPushButton *m_batch = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_notice = nullptr;

    /// 等着算大小/时间的那些目录行：路径 → 那一行的「大小 · 时间」那一格。
    ///
    /// 【为什么不能在建行的时候顺手算出来】
    /// 目录的字节数要**递归遍历**整棵树（`saves/<世界>/region/*.mca` 动辄几十万个文件），
    /// 而 `refresh()` 是在"切到这一页"那一瞬间同步跑的 —— 在里面算等于每开一次实例设置都
    /// 卡住几秒到十几秒。所以建行时先摆一句「正在统计…」，真正的数由定时器在**事件循环
    /// 转起来之后**一格一格补上。
    ///
    /// NOTE: `QPointer` 而不是裸指针：`refresh()` 是 `m_list->clear()` + 重新建行，那些
    ///       标签会被一起删掉，而定时器可能正好排在下一趟。悬空的话是段错误（不是"数字
    ///       没显示出来"）。
    QHash<QString, QPointer<QLabel>> m_statLabels;
    /// 还没算的那些（按轮到的先后）。**和上面那张表分开**：表是"哪些行要算"，这个是
    /// "还剩哪些没算"——算完的那些在表里留着（它们那一格已经写好了，不用再动），
    /// 从队里去掉就行。队列空 = 这一趟统计做完了。
    QStringList m_statQueue;
    /// 排下一行的定时器（单发、0 毫秒：下一个事件循环转过来就干一行）。
    QTimer *m_statTimer = nullptr;
};
