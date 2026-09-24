/**
 * @file GameLogWindow.hpp
 * @brief 「游戏日志」窗：把某一份游戏进程自己的输出（stdout/stderr）实时显示出来。
 * @author 0x7fffff
 * @date 2026-09-24
 * @note 和 GroupDialog / AccountSkinDialog 同一条约定：**ui/ 不依赖业务层**。这里不 include
 *       任何 src/ 下的头，也不认识"会话"是什么 —— 它是"给我一行字我就写上去"的一块控件；
 *       谁来喂、喂哪一份、什么时候销毁，全在 MainWindow 里。
 *
 * 【为什么是一个独立的窗，而不是主页上的一块面板】
 *
 * 界面里**本来就没有日志面板**：2026-09 分选项卡时去掉了（那些话改走 Qt 的日志系统，
 * 落在 <数据目录>/logs/wtfml.log）。那件事对一个启动器是对的 —— 99% 的时间里日志是噪音。
 * 但"游戏起不来、想知道为什么"是真实需求，而那时候用户要的是**那一份游戏自己的**输出，
 * 不是启动器的。所以做成"按需开一个窗"，而不是把噪音请回主页。
 *
 * 【一份游戏一个窗】
 *
 * 多开是这个启动器明确要支持的事（见 LauncherManager 类头的说明），两三个实例的输出
 * 混在一个窗里等于没法看 —— 所以要开的窗不止一个，窗口标题里带实例名。
 *
 * 【关掉这个窗会发生什么】
 *
 * 什么都不发生：这只是个看输出的窗，游戏进程照跑（和「停止」按钮是两回事）。
 * 关掉之后**这一份会话**不再自动重开（见 MainWindow 里 m_gameLogDismissedSessions 的说明）。
 *
 * 【按级别染底色 + 顶上一排筛选（2026-09-24 加）】
 *
 * 游戏自己打的日志本来就带级别（`[main/INFO]` 那一格），只是以前一律当纯文本铺出来 ——
 * 于是"崩在哪一条"要靠眼睛在一屏灰字里找。现在按严重程度给**整行**染底色
 * （Debug 灰蓝 / Info 不染 / Warn 黄 / Error 红 / Fatal 深红配白字，见 levelBackground），
 * 顶上再摆一排带计数的开关：**开着的那颗就是这一档的颜色**（和正文那一行同一个色，不用读字
 * 就知道哪几档开着），点一下就只隐藏那一档、那颗也跟着回到原来那个灰，五档全关还有一颗
 * 「显示全部」兜底。
 *
 * 四件事值得先说清：
 *
 * - **染的是整行，从左沿到右沿**。这是**块**格式（`QTextBlockFormat::setBackground`），
 *   不是逐字的字符底色 —— 字符底色只染"字占的那一截"，一行比视口短的时候后半截就是白的
 *   （日志最后那几行、我们自己补的"游戏已退出"都短，看着就像没染上）。块底色铺满整行、
 *   不用往文本里塞空格，复制出去也不脏。INFO 故意**不染**：日志里绝大多数是它，全染上就
 *   成了一张色纸，Warn/Error 反而不显眼了。
 * - **数字数的是"还留在这里的行"**，不是"这一趟一共发生了多少次" —— 这个窗只留
 *   最后 maxLogBlocks() 行（见下），滚掉的那些不再算。计数跟着模型走，所以关掉某几档
 *   筛选时数字**不会变小**（那几行还在，只是没显示）。
 * - **认不出级别的行跟着上一行走**（崩栈的 `at ...` 续行、我们自己补的那句"游戏已退出"
 *   都属于上一条日志），第一行就认不出来时算 Info —— 于是五档的计数**加起来永远等于
 *   总行数，不会凭空少一截**。末尾那半行也按这条**先猜**一个（见 insertPendingIntoView），
 *   猜错了等换行到了会按真级别重摆一遍。
 * - **一次摆一整段，不是一行一次**（见 .cpp 里 insertRun）：文档操作是按**次数**收钱的，
 *   一份两万行的日志一行一次要半秒，攒成一段一次只要几十毫秒。
 *
 * 【2026-09-24 用户又提的四条】
 *
 * ① 「日志行与行之间加一条细线分隔一下」→ 见 .cpp 里 LogView（**得是自己画的**：
 *    QTextDocument 里没有"块的下边框"这一项）。② 「字号稍微大一点」→ logFont()，比系统那个
 *    等宽字体大 kLogFontPointDelta 号。③ 「允许这个窗口最大化最小化」→ 构造函数里那两行
 *    window flags（QDialog 默认只给一个关闭钮）。④ 「游戏正常以 0 退出就自动关闭日志界面」
 *    → autoCloseOnExit()，接线在 MainWindow::onGameFinished()。
 */

#pragma once

#include <QColor>
#include <QDialog>
#include <QFont>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

class GameLogWindow : public QDialog {
    Q_OBJECT

public:
    /**
     * 一条日志的级别。
     *
     * NOTE: **枚举顺序就是严重程度**（Debug 最轻 → Fatal 最重）。顶上那排筛选按钮照这个
     *       顺序摆，内部那几个 QVector（计数 / 是否显示 / 按钮指针）也照这个顺序索引 ——
     *       想加一档的话，除了枚举本身，`levelCount()` 和 levelBackground / levelName /
     *       filterButtonName 这几处一起加，别只改一半（验证工程里有"五档两两不同"那几条
     *       盯着文案与颜色，加漏一处会红）。
     *
     * 没有 Trace 这一档：它比 Debug 还轻，和"到底出了什么事"没关系，认出来一律归 Debug。
     */
    enum class LogLevel { Debug, Info, Warn, Error, Fatal };

    /// 有几档（循环/数组的上界）。
    static constexpr int levelCount() { return 5; }
    /// 下标 → 级别。越界回 Info（不 assert：这条路上不值得为一个越界把整个日志窗弄崩）。
    static LogLevel levelAt(int index);
    static int levelIndex(LogLevel level) { return int(level); }

    /**
     * @brief 造一个日志窗（**不显示**，由调用方 show()）。
     * @param instanceName 这一份游戏是哪个实例（只用于标题）。
     * @param parent 父窗口。**传 nullptr**（见下）。留在参数里只是为了那些"挂在别的窗
     *        底下"的用法，产品这条路不走。
     *
     * NOTE: 建出来就带 Qt::WA_DeleteOnClose —— 关掉即销毁。理由是这些窗会**留着一整份
     *       文本**（最多 maxLogBlocks() 行），藏起来不销毁的话，开过十个实例就白占十份
     *       内存。销毁之后由调用方那边把指针摘掉（见 MainWindow 里接 finished 的那一段）。
     *
     * 【为什么**不要**父窗口（2026-09-24 改）】
     *
     * 原来是 `new GameLogWindow(name, this)`（parent = 启动器主窗）。在 Windows 上，
     * "有主的窗"（owned window）**永远压在自己的主窗上面** —— 用户点启动器也翻不上来，
     * 于是日志窗等于把启动器挡死了（用户原话：「日志一定会显示在启动器前面，会遮挡」）。
     * 那是平台规则，不是可以调的风格，所以父窗口这件事本身就是病根。
     *
     * 去掉父窗口换来三件事：能点到后面去、能自己最小化、任务栏上有自己一格。代价是
     * "跟着启动器一起关掉"要自己接（见 MainWindow 析构里那一段）。
     */
    explicit GameLogWindow(const QString &instanceName, QWidget *parent = nullptr);

    /**
     * @brief 新窗摆哪儿：**优先贴在启动器旁边**（右 → 左 → 居中），不遮住它。
     * @param launcherFrame 启动器主窗的窗口矩形（全局坐标）。
     * @param windowSize 新窗的尺寸。
     * @param availableScreen 那一块屏幕的可用区域（`QScreen::availableGeometry()`，
     *        已经扣掉了任务栏）。
     * @param index 同一次开出来的第几个（同时开着两三份时错开一点，别叠成一摞）。
     * @return 新窗左上角该在的全局坐标（已经钳进那块屏幕里）。
     *
     * NOTE: 纯几何（不读屏幕、不读全局状态），所以验证工程能拿一组假矩形直接量它。
     *       左边右边都放不下（启动器拉满屏那种）时**居中** —— 那种情况躲不掉重叠，
     *       能不重叠的两种情况优先。
     */
    static QPoint placementFor(const QRect &launcherFrame, const QSize &windowSize,
                               const QRect &availableScreen, int index);

    /**
     * @brief 游戏这一趟退出之后，这个窗要不要**自己关掉**。
     * @param exitCode 退出码。
     * @param crashed 是不是异常终止（`QProcess::ExitStatus == CrashExit`）。
     * @return 正常退出**且**退出码为 0 时为真。
     *
     * 用户原话：「如果游戏正常以0退出就自动关闭日志界面」。那一趟没有任何可看的东西
     * （真出了事不会给 0），留着一扇只写着一行"已退出"的窗等于替用户留了个垃圾。
     *
     * NOTE: **崩溃（CrashExit）一律不关**，哪怕拿到的码正好是 0：崩溃那一趟正是用户要
     *       逐行看的时候（见 .cpp 里那行「游戏已崩溃退出」）。这也是这个函数收两个参数
     *       而不是一个的原因 —— 只看码的话 `CrashExit + 0`（Windows 上杀进程拿到的码）
     *       会被误判成"正常退出"。
     */
    static bool autoCloseOnExit(int exitCode, bool crashed);

    // ---------- 数据进 ----------

    /**
     * @brief 追加一段输出（游戏自己的换行和缩进都不动它）。
     *
     * 自动做的三件事：① **攒够一整行才认级别**（进程输出是按块来的，一个 chunk 可能断在
     * 行中间 —— 那半行先原样摆在末尾、不染底色，等换行到了再合成一条）；② 认出来之后按
     * 级别染底色（这一档被筛掉的话就不摆出来，但**照样计数**）；③ **本来就在最底下时**
     * 跟到底 —— 用户在往上翻（看刚才那几行报错）时不跟，那种"正在读的东西被新输出顶走"
     * 是这类窗最招人烦的地方。
     */
    void appendText(const QString &text);

    // ---------- 当场读控件 ----------

    /// 现在显示的全文（**被筛掉的不在里面**）。给验证工程用（产品代码不读它）。
    QString logText() const;

    /// 那一块文本控件（验证工程量几何/只读性用）。
    QPlainTextEdit *getLogView() const;
    QPushButton *getClearButton() const;
    QPushButton *getCloseButton() const;
    QLabel *getHintLabel() const;
    QToolButton *getFilterButton(LogLevel level) const;
    QPushButton *getShowAllButton() const;

    // ---------- 级别与筛选 ----------

    /// 模型里现在留着几行（含被筛掉的）。
    int lineCount() const { return m_lines.size(); }
    /// 这一档有几行（含被筛掉的）。
    int lineCountFor(LogLevel level) const;
    bool levelVisible(LogLevel level) const;
    /// 改一档的显示与否（会顺手把那颗按钮的勾也对上，然后重画）。给验证工程和「显示全部」用。
    void setLevelVisible(LogLevel level, bool visible);

    /**
     * @brief 认一行的级别；**认不出来时回 fallback**（调用方传"上一行的级别"）。
     *
     * 只在**行首**那一小段里认，两条规则：
     *  ① 行首那串**连着开头**的方括号里找：`[main/INFO]` 取斜杠后那一段，`[INFO]` 整段就是。
     *     只有连着开头的那几格算数 —— 消息体里出现的 `[ERROR]` 不算（`Failed to parse
     *     [ERROR] block` 是人家在说"解析 ERROR 这块失败了"，不是这条日志的级别）。
     *  ② 开头 kLevelScanHead 个字符里**带冒号**的裸词（`12:34:56 WARN: msg`，java.util.
     *     logging 那一套）。冒号是必须的：不然 `Loaded 3 INFO files` 会被认成 INFO。
     *
     * 认的表（大小写不分）：TRACE/DEBUG→Debug，INFO→Info，WARN/WARNING→Warn，
     * ERROR/SEVERE→Error，FATAL→Fatal。
     */
    static LogLevel classifyLine(const QString &line, LogLevel fallback);
    /// 规则 ② 的窗口（实测：原版那一行级别落在第 12 个字符上、Forge 那套（长线程名 + 长
    /// 时间戳）落在 45 上下 —— 48 是留了余量的）。
    static int levelScanHead() { return 48; }

    /// 一条没有换行的输出最多攒多少字符就当成一整条收下（理由见 .cpp）。
    static int maxPendingChars() { return 8192; }

    // ---------- 文案与名字（唯一出处） ----------

    /// 窗口标题。带实例名：同时开着好几份时，靠它认哪个是哪个。
    static QString windowTitleText(const QString &instanceName);
    /// 顶上那行灰字。
    static QString hintText();
    static QString clearButtonText();
    static QString closeButtonText();
    /// 还没有任何输出时文本控件里那句占位。
    static QString emptyPlaceholderText();
    /// 有输出、但五档全被筛掉时那句占位（和上面那句是**两句不同的话**，别合并）。
    static QString filteredOutPlaceholderText();

    /// 这一档在筛选按钮上写什么（级别名 + 计数）。
    static QString filterButtonText(LogLevel level, int count);
    /// 那一排开关上那句提示（顺带说明数字是什么）。
    static QString filterButtonTooltip();
    static QString showAllButtonText();

    /// 级别名。**用日志自己那套英文词**（不是"警告/错误"）：它要跟日志里的 `[main/WARN]`
    /// 对得上，用户看的正是那一格。
    static QString levelName(LogLevel level);

    /// 这一档的文字染什么底色。**Info 故意返回无效颜色**（= 不染，用窗口自己的底色）。
    static QColor levelBackground(LogLevel level);
    /// 这一档的文字用什么墨色。只有 Fatal 有（深红底上再写深灰字是看不清的）。
    static QColor levelForeground(LogLevel level);

    /**
     * @brief 顶上那颗开关注上这一档时填什么色（**关着**时一律是同一个灰）。
     *
     * 「开着」那一格就是 levelBackground —— **开关的颜色和正文那一行是同一个色**，不用读字
     * 就知道哪几档开着。Info 例外：它的正文底色就是我们自己的窗口底色（`levelBackground`
     * 返回无效色），没有颜色可借，于是一律用那个"原生的开态灰"（见 .cpp 里 kSwitchOnRgb）——
     * 硬编一个日志里不存在的颜色反而会让人以为"Info 是蓝的"。**开关的两种状态都由我们自己
     * 画**（理由见 .cpp 里 filterButtonStyle），所以这两个值必须有个确定的出处。
     *
     * 验证工程量的是**按钮上真画出来的像素**，比的就是这里。
     *
     * NOTE: 2026-09-24 用户说「info 的按钮再亮一点」，于是 Info 那一档不用 kSwitchOnRgb
     *       那个灰了，改用**正文区自己的底色**（量出来是 `#ffffff`，见 .cpp 里 kInfoOnRgb）
     *       —— 那既是最亮的一档，也正好是"Info 这一档的颜色"（它的整行底色就是这个）。
     */
    static QColor filterButtonBackground(LogLevel level, bool on);
    /// 合成开关上那几个字的墨色：Fatal 那档是白的，其余是黑的。
    static QColor filterButtonForeground(LogLevel level);

    // ---------- 正文那块文本的长相 ----------

    /**
     * @brief 正文用的字体：系统那个等宽字体，**再大一号**。
     *
     * 等宽是必须的（日志里有缩进、有对齐的模组列表，比例字体读起来是散的，见构造函数里
     * 那一段）；大一号是用户 2026-09-24 说的「字号稍微大一点」—— 日志一屏三十行、
     * 每行几十个字符，小一号的时候找关键字全靠凑近看。
     *
     * NOTE: 唯一出处。构造函数拿它 `setFont`，验证工程拿它跟**系统那个等宽字体**比
     *       （不是跟自己比 —— 那个比法是空的，改错了一起变）。
     */
    static QFont logFont();
    /// 大多少号（点）。`1.0` 是"稍微"：再大就一屏放不下几行了。
    static qreal logFontPointDelta() { return 1.0; }

    /**
     * @brief 行与行之间那条细线的颜色。
     *
     * 用户原话：「日志行与行之间加一条细线分隔一下」。**底色本身分不开相邻的两条 INFO**
     * （它们都不染），所以这条线的主要去处就是那里 —— 它得在白底上看得见（和白差 39*√3 ≈ 67），
     * 也得在 Debug 那个 `#eef2f6` 上看得见（差 45），同时不能深到像一条框线。
     */
    static QColor separatorColor();
    /// 那条线几像素高。1 = 真正的"细线"（2 就开始像表格了）。
    static int separatorHeight() { return 1; }

    /// 最多留多少行。**必须有上限**：一个挂了一百个模组的实例能吐几十万行，
    /// 不封顶的话开一晚上日志就把内存吃干净了。超出之后最老的整段丢掉。
    static int maxLogBlocks() { return 20000; }

    static QString logViewName() { return QStringLiteral("gameLogView"); }
    static QString clearButtonName() { return QStringLiteral("gameLogClearButton"); }
    static QString closeButtonName() { return QStringLiteral("gameLogCloseButton"); }
    static QString hintLabelName() { return QStringLiteral("gameLogHintLabel"); }
    static QString showAllButtonName() { return QStringLiteral("gameLogShowAllButton"); }
    static QString filterButtonName(LogLevel level);

private:
    /// 模型里的一行：级别 + 正文（**不带换行**，换行是摆出去的时候才加的）。
    struct LogLine {
        LogLevel level;
        QString text;
    };

    /// 全收起 → 全打开（顶上那颗「显示全部」）。
    void showAllLevels();
    /// 攒够一整行：认级别、进模型、记账、超上限就丢最老的。回它的级别。
    LogLevel rememberLine(const QString &text);
    /// 按现在的筛选从头重画一遍（切筛选那一下走这里）。
    void rebuildView();
    /**
     * @brief 把**同一档连续的几行**一次摆到末尾（末尾永远留一格空的当光标行）。
     *
     * **成片写**是这个窗快不快的关键：文档操作按**次数**收钱（量过，见 .cpp），所以
     * "一行一次"在长日志上就是"点一下卡半秒"。底色也一次盖一片（跨块选区的
     * `setBlockFormat` 是一次调用）。
     */
    void insertRun(LogLevel level, const QStringList &lines);
    /// 末尾那半行该按哪一档染：**按"上一行那一档"猜**（认不出级别的行就是跟着上一行走的）。
    /// 猜错了不要紧 —— 换行到了会按真级别重摆一遍。
    LogLevel pendingLevelGuess() const;
    /// 把"还没等到换行的那半行"摆在末尾（按猜的那一档染，见上面）。
    void insertPendingIntoView();
    /// 把上面那半行从视图里撤掉（收下它、或者要接着往后写时先撤）。
    void dropPendingFromView();
    /// 那一排按钮上的计数与勾重新对一遍。
    void updateFilterButtons();
    /// 空的时候该说哪句话（"还没有输出"还是"都被筛掉了"）。
    void refreshPlaceholder();

    QLabel *m_hintLabel = nullptr;
    QPlainTextEdit *m_view = nullptr;
    QPushButton *m_clearButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QPushButton *m_showAllButton = nullptr;

    QVector<LogLine> m_lines;          ///< 上限 maxLogBlocks()，超出丢最老的
    QString m_pending;                 ///< 还没等到换行的那半行（不属于任何一条）
    QVector<int> m_levelCounts;        ///< 每一档几条（**含被筛掉的**）
    QVector<bool> m_levelVisible;      ///< 每一档显不显示
    QVector<QToolButton *> m_filterButtons;
    /// 末尾那一格现在装的是"半行"吗（是的话下一次写正文之前要先把它撤掉）。
    bool m_viewLastBlockIsPending = false;
};
