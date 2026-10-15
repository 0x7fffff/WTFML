/**
 * @file ToolboxPage.hpp
 * @brief 工具箱那一整页（底栏那把扳手 → 一片整块盖住内容区的页），五件小工具共用一页。
 * @author 0x7fffff
 * @date 2026-10-16
 * @note 用户 2026-10-16 点名的那一批：文件哈希校验、MOTD 文本生成器（含彩色文本那几种
 *       命令写法）、坐标换算（认 F3+C 复制来的那行 tp 命令）、服务器在线检测，再加上他
 *       补的「基于 mc 的铁砧惩罚机制做一个附魔顺序助手」。
 *
 * 【为什么五件工具在一个页里，而不是五片页】
 *
 * 这一页和「设置」那页是一个形状：**顶上五颗 tool chip，底下那五件工具各自一片、换着露**
 * （`QStackedWidget`）。理由也一样：这五件事互相之间没有次序、没有前后依赖，用户要的是
 * "随手拿一件用一下就走"，为它各开一页只会让底栏那把扳手变成五个入口。chip 那一排的写法
 * 照抄下载页四颗 chip / 文件页四颗筛选按钮（同一套 `Theme::markButton` 和选中判据）。
 *
 * 【这一页自己一根手指都不碰游戏目录】
 *
 * `ui/` **不 include `src/`**（全仓的老规矩）。所以这一页只干两件事：把用户敲的东西
 * **收成信号**发出去、把 `ToolboxController` 算好的**字符串**摆上去。算哈希、认坐标、
 * 拼 `tellraw`、排附魔顺序那些全在 `src/tools/` 里（纯函数，各自有判据在 tests/main.cpp）。
 *
 * 【"算"这件事为什么不在这一页里做】
 *
 * 哈希要读一个可能几百 MB 的文件（要进度、要能取消），服务器检测要联网（要超时），
 * 这两件都是**慢活**：它们必须由 controller 派到线程上去，页面上那颗按钮只负责发一句
 * "用户要算了"。另三件是纯字符串活，本来当场就能算，但为了"五件工具一个形状"，入口
 * 一律是同一条（发信号 → controller 算 → 回来填页面）。
 */

#pragma once

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QPixmap>

class QComboBox;
class QDoubleSpinBox;
class QDragEnterEvent;
class QDropEvent;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QWidget;

class ToolboxPage : public QDialog {
    Q_OBJECT

public:
    /// 五件工具。**这个次序就是顶上那排 chip 的次序**（`tools()` 是唯一出处）。
    enum class Tool { Anvil, Text, Coord, Server, Hash };

    /// 工具的键（底栏面板那五颗按钮、`ToolboxController::open()` 都用它认人）。
    static QString toolKey(Tool tool);
    /// 反着来。认不出来时 @p ok 写 false。
    static Tool toolFromKey(const QString &key, bool *ok = nullptr);
    /// 中文名（`附魔顺序助手`）。
    static QString toolName(Tool tool);
    /// 一句话说明（chip 的 tooltip、以及每一片顶上那行小字）。
    static QString toolSummary(Tool tool);
    /// 五件，**次序唯一出处**。
    static QList<Tool> tools();
    /// 这一页的标题（`OverlayHost::execPage` 顶上那一条写的字）。
    static QString windowTitle() { return QStringLiteral("工具箱"); }

    /// 验证工程认控件用的那几个名字（`@p tool` 那一档的 chip / 页 / 以及**底栏面板上
    /// 对应的那颗按钮** —— 后者长在 `MainWindowUI` 里，但名字跟着这张表走）。
    static QString toolChipName(Tool tool);
    static QString toolPanelName(Tool tool);
    static QString toolButtonName(Tool tool);

    explicit ToolboxPage(QWidget *parent = nullptr);

    /// 切到 @p tool（开页时也走它）。
    void showTool(Tool tool);
    Tool currentTool() const { return m_current; }

    // ================= 一、附魔顺序助手 =================

    /// 一行输入：一条附魔 + 它的等级 + 这一件是附魔书还是已经附过魔的装备 + 在铁砧上用过几次。
    struct AnvilRow {
        QString id;      ///< 附魔 id（下拉框里那一项的 data）
        int level = 1;
        bool isBook = true;
        int uses = 0;    ///< 在铁砧上加工过几次（前科 = `2^n − 1`，界面上的小字会写出来）
    };
    /// 算完之后摆上去的那一份。
    struct AnvilView {
        bool ok = false;
        QString error;        ///< 非空 = 算不了（表里没有那条附魔、超过 10 件…）
        QStringList steps;    ///< 每一步一行（`第 1 步：锋利 III 的书 → 锋利 IV 的书（3 级）`）
        QString summary;      ///< 一句总述（`一共 7 级，合完是锋利 V + 耐久 III`）
        QStringList notes;    ///< 要提醒的（超 39 级那一条等）
        bool anyTooExpensive = false;
    };

    /// 下拉框里那几项（id → 中文名）。**表在 `Anvil::enchantmentTable()`**，这一页不另写一份。
    void setEnchantmentChoices(const QList<QPair<QString, QString>> &idAndName);
    /// 现在表格里那几行（用户点了「算」）。
    QList<AnvilRow> anvilRows() const;
    /// 把结论摆上去。
    void setAnvilView(const AnvilView &view);

    // ================= 二、文本生成器 =================

    /// 色码 / 字形那一排上的一颗（**表在 `TextFormat` 里**：十六色那张表和五个格式码，
    /// 这一页一个字节都不抄）。
    struct PaletteEntry {
        QString code;     ///< 那个码位（`0`~`f` 是颜色、`l` / `o` / `n` / `m` / `k`、`r` 复位）
        QString name;     ///< 中文名（按钮的 tooltip）
        QString rgbHex;   ///< 颜色码的色（`#RRGGBB`）；格式码那几颗留空
        bool isColor = false;
    };

    /// 摆出那一排色码 / 字形按钮（`Label` 里那个 `source` 那一格要的工具条）。
    void setTextPalette(const QList<PaletteEntry> &entries);
    /// 用户粘进来的那串原文（可能带 `§` 码）。
    QString textSource() const;
    /// 把原文换成 @p text（「从 JSON 读回来」那一条把读出来的段写回这一格）。**会发
    /// `textSourceChanged()`** —— 正是要让 controller 照新内容重算一遍预览和三种输出。
    void setTextSource(const QString &text);
    /// 预览那一块（富文本 HTML；颜色和字形都由 controller 算好）。
    void setTextPreview(const QString &html);
    /// 三种输出：`§` 写法、JSON 组件、`/tellraw` 命令。
    void setTextOutputs(const QString &legacy, const QString &json, const QString &tellraw);
    /// 认不出来的那几种组件（`translate`、`#RRGGBB` 那种）——**不猜**，就把这句摆出来。
    void setTextNotice(const QString &notice);

    // ================= 三、坐标换算 =================

    /// 那一排维度（`主世界` / `下界` / `末地`；次序由 controller 给，页面上不写死）。
    void setDimensionChoices(const QStringList &names);
    /// 从维度下拉框里选的是第几项；坐标那三格；以及要不要把结果复制出去。
    int dimensionIndex() const;
    /// 三格坐标（用户敲的 / 从 tp 命令里认出来的都在里面）。
    double coordinate(int axis) const;  ///< @p axis：0 = x、1 = y、2 = z
    /// 把三格坐标与维度摆成"这一趟问的那一组"（认出来 tp 命令之后由 controller 调）。
    void setCoordinates(int dimensionIndex, double x, double y, double z);
    /// 结果那几行（`下界：x 10.00 y 64.00 z -9.50`、`区块 (0, -1) 一带`、`region 文件 r.0.-1.mca`…）。
    void setCoordinateRows(const QStringList &rows);
    /// 认 tp 命令那一格的结果：认出来了写回那三格与维度；没认出来就把原因摆出来。
    void setTeleportParse(bool ok, const QString &message);
    /// 「复制成 tp 命令」那一条：controller 拼好之后放进来，页面上那颗按钮直接拷它。
    void setTeleportCommand(const QString &command);

    // ================= 四、服务器在线检测 =================

    /// 要问的那个地址（`host:port`，没写端口按 25565 算 —— 那是 `ServerPing` 那边的事）。
    QString serverAddress() const;
    void setServerAddress(const QString &address);
    /// 结论。
    struct ServerView {
        bool ok = false;
        QString error;         ///< 非空 = 这次没问成（超时 / 没有应答 / 解析不了地址）
        bool timedOut = false; ///< 超时那一档（和"连上了但没答话"**分开**，见 ServerPing）
        QString address;       ///< 问的是哪个地址（回显一遍，免得用户看错行）
        QString motd;          ///< 那一行彩色 MOTD 的 HTML（这一页把它当富文本画）
        QString version;       ///< 版本名
        QString players;       ///< `3 / 20`
        QString latency;       ///< `26 ms`
        QPixmap icon;          ///< 服务器图标（64×64；没有就是空的）
    };
    void setServerView(const ServerView &view);
    /// 正在问 / 问完了（按钮那两颗跟着换）。
    void setServerBusy(bool busy);

    // ================= 五、文件哈希校验 =================

    /// 算法那三档（名字由 controller 从 `FileHash::algorithmName()` 拿，页面上不写死）。
    void setAlgorithmChoices(const QStringList &names);
    int algorithmIndex() const;
    /// 要算的那个文件（拖进来 / 浏览选 / 手敲都在这一格）。
    QString hashPath() const;
    void setHashPath(const QString &path);
    /// 用户粘进来的"已知值"（可以为空 = 只想看摘要）。
    QString hashExpected() const;
    /// 算出来的结论。
    struct HashView {
        bool ok = false;
        QString error;        ///< 非空 = 没算成（打不开 / 取消了）
        QString hex;          ///< 摘要本身
        QString sizeText;     ///< 文件多大
        QString verdict;      ///< 和已知值对照的那句话
        bool hasVerdict = false;
        bool matches = false;
        QString algorithm;    ///< 算的是哪一档
    };
    void setHashView(const HashView &view);
    /// 进度（`done` / `total`，一共那个是 -1 时写成"读了 X"）。
    void setHashProgress(qint64 done, qint64 total);
    void setHashBusy(bool busy);

signals:
    // ---- 附魔顺序助手 ----
    void anvilPlanRequested();
    // ---- 文本生成器 ----
    void textSourceChanged(const QString &text);
    void textJsonRequested(const QString &json);  ///< 用户点了「从 JSON 读回来」
    // ---- 坐标换算 ----
    void coordinateRequested(int dimensionIndex, double x, double y, double z);
    void teleportParseRequested(const QString &text);
    void teleportCommandRequested(int dimensionIndex, double x, double y, double z);
    // ---- 服务器在线检测 ----
    void serverCheckRequested(const QString &address);
    void serverCheckCancelRequested();
    // ---- 文件哈希校验 ----
    void hashRequested(const QString &path, int algorithmIndex, const QString &expected);
    void hashCancelRequested();

protected:
    /// 拖一个文件进来（只对"哈希校验"这一件有意义 —— 别的工具不吃文件）。
    /// 判据见 .cpp：当前不是哈希那一件、或者拖进来的不是本地文件，就原样交给基类。
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    /// 五片各自搭一次（构造函数里全搭好，切的时候只是换 `QStackedWidget` 那一格）。
    QWidget *buildAnvilPanel();
    QWidget *buildTextPanel();
    QWidget *buildCoordPanel();
    QWidget *buildServerPanel();
    QWidget *buildHashPanel();
    /// 顶上那排 chip（点一下 = showTool）。
    QWidget *buildChipRow();
    /// 附魔那一张表加一行（「加一行」和"构造时先摆一行"走的是同一段）。
    void appendAnvilRow();
    /// chip 的选中态（只认当前那一颗，和下载页四颗 chip 同一套判据）。
    void syncChips();

    Tool m_current = Tool::Anvil;
    QList<Tool> m_tools;
    QStackedWidget *m_stack = nullptr;
    QList<QPushButton *> m_chips;

    // 一、附魔顺序助手
    QTableWidget *m_anvilTable = nullptr;
    QList<QPair<QString, QString>> m_anvilChoices;
    QLabel *m_anvilResult = nullptr;
    QLabel *m_anvilError = nullptr;

    // 二、文本生成器
    QPlainTextEdit *m_textSource = nullptr;
    QLabel *m_textPreview = nullptr;
    QPlainTextEdit *m_textLegacy = nullptr;
    QPlainTextEdit *m_textJson = nullptr;
    QPlainTextEdit *m_textTellraw = nullptr;
    QLabel *m_textNotice = nullptr;
    QGridLayout *m_textPaletteLayout = nullptr;
    QWidget *m_textPaletteRow = nullptr;

    // 三、坐标换算
    QComboBox *m_coordDimension = nullptr;
    QDoubleSpinBox *m_coordX = nullptr;
    QDoubleSpinBox *m_coordY = nullptr;
    QDoubleSpinBox *m_coordZ = nullptr;
    QLineEdit *m_tpInput = nullptr;
    QLineEdit *m_tpCommand = nullptr;
    QLabel *m_tpNotice = nullptr;
    QLabel *m_coordResult = nullptr;

    // 四、服务器在线检测
    QLineEdit *m_serverInput = nullptr;
    QPushButton *m_serverCheck = nullptr;
    QPushButton *m_serverCancel = nullptr;
    QLabel *m_serverIcon = nullptr;
    QLabel *m_serverResult = nullptr;

    // 五、文件哈希校验
    QLineEdit *m_hashPath = nullptr;
    QComboBox *m_hashAlgorithm = nullptr;
    QLineEdit *m_hashExpected = nullptr;
    QPushButton *m_hashRun = nullptr;
    QPushButton *m_hashCancel = nullptr;
    QLabel *m_hashResult = nullptr;
    QLabel *m_hashProgress = nullptr;
    /// 算出来的那一串摘要（「复制」那颗拷的是它 —— 结果标签里画的带 HTML，不能整段拷）。
    QString m_hashHex;
};
