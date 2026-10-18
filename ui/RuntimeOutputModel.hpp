/**
 * @file RuntimeOutputModel.hpp
 * @brief 「运行输出」那一份**共用**的日志模型：认级别、留多少行、按档筛、按字搜、导出去。
 * @author 0x7fffff
 * @date 2026-10-18
 * @note coding plan §9.8「统一运行输出」。原来这套记账在 `GameLogWindow` 里长着，服务端
 *       控制台那边只有一块 `QPlainTextEdit` —— 两边各有各的行数上限、各有各的"怎么认级别"，
 *       而"搜索"两边都没有。这里把**与界面无关的那一半**抽出来：
 *
 *         · 一条日志是什么级别（classifyLine）；
 *         · 手里留多少行（capacity，超了丢最老的）；
 *         · 哪几档现在显示（setLevelVisible）；
 *         · 搜索词（setSearchText，大小写不分的子串）；
 *         · 视图最多显示几行（setDisplayLimit —— 和 capacity 不是一回事，见下）；
 *         · 导出要的那几份文本（allText / textFrom / visibleText）。
 *
 * 【为什么在 ui/ 里，而不是 src/】
 *
 * 它是"给控件摆字用"的那一层：按钮上的计数、行底下的颜色、搜索框里的那一句 —— 全都只服务
 * 界面。`ui/` 不 include `src/` 是全仓的老规矩，反过来让 `src/` 去 include `ui/` 也不是
 * 这一层的意思。所以它落在 `ui/`，但**不碰任何控件**（只 include Qt 的容器和字符串），
 * 两个页面各拿一份、各摆各的。
 *
 * 【两条上限不是一回事】
 *
 * - `capacity()` 是**内存**的闸：超出就丢最老的（服务端跑几天能吐几十万行）。
 * - `displayLimit()` 是**可读性**的闸：视图最多铺几行。「全部」时等于 capacity。
 *   切换这一档**一行都不丢**：切回大上限那些行当场回来。
 *
 * 这和 `GameLogWindow` 文件头里那段是同一个道理，只是现在两页共用同一份实现。
 *
 * 【这里不猜任何"结论"】
 *
 * 模型只认"这一行自己写着什么级别"。崩溃了没有、服务端起没起来**不在这里判断** ——
 * 那两件事分别归 `src/diag/` 和 `ServerRun::isReadyLine()`（§9.8 第三条）。认不出级别的
 * 行跟着上一行走（崩栈的 `at ...` 续行正该如此），但那只是**染色**的 fallback，不是结论。
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

class RuntimeOutputModel {
public:
    /**
     * 一条日志的级别。**枚举顺序就是严重程度**（Debug → Fatal），`GameLogWindow::LogLevel`
     * 用的是同一套顺序，两边靠 `static_cast` 对应（验证工程钉着"两套一一对应"）。
     */
    enum class Level { Debug, Info, Warn, Error, Fatal };

    /// 模型里的一行：级别 + 正文（不带换行）；`counted` = 是独立记录的首行。
    struct Line {
        Level level = Level::Info;
        QString text;
        bool counted = true;
    };

    /// 摆到视图上用的那一份（只留下渲染要的两个字段）。
    struct VisibleLine {
        Level level = Level::Info;
        QString text;
    };

    static constexpr int levelCount() { return 5; }
    /// 下标 → 级别。越界回 Info（不值得为一个越界把日志窗弄崩）。
    static Level levelAt(int index);
    static int levelIndex(Level level) { return int(level); }

    /// 认一行的级别；认不出来时回 fallback（调用方传"上一行的级别"）。
    /// 规则与 `GameLogWindow::classifyLine` 逐字相同（那个现在是转发到这里的薄壳）。
    static Level classifyLine(const QString &line, Level fallback);
    /// 规则 ② 的窗口（见 .cpp）。
    static int levelScanHead() { return 48; }

    explicit RuntimeOutputModel(int capacity = defaultCapacity());

    /// 默认上限：20000 行。和 `GameLogWindow::maxLogBlocks()` 是同一个数（那边转发到这里）。
    static int defaultCapacity() { return 20000; }

    // ---------- 容量 ----------

    /// 手里最多留几行（<= 0 按 1 算）。
    int capacity() const { return m_capacity; }
    /// 改上限：马上丢掉超出去的老行，并修正计数。
    void setCapacity(int capacity);

    // ---------- 收行 ----------

    /// 清空（正文、计数一起清；筛选勾和搜索词**不动** —— 那是"我现在想看什么"的偏好）。
    void clear();

    /// 收下一条已经完整的行（不含换行）。返回它的级别。
    Level addLine(const QString &text);

    const QVector<Line> &lines() const { return m_lines; }
    int lineCount() const { return m_lines.size(); }
    /// 这一档有几条**独立记录**（续行不重复计数，含被筛掉的记录）。
    int countFor(Level level) const;
    /// 上一行的级别（空模型回 Info）—— 认不出级别时的 fallback。
    Level lastLevel() const;

    // ---------- 筛选 ----------

    void setLevelVisible(Level level, bool visible);
    bool levelVisible(Level level) const;
    /// 搜索词。空串 = 不过滤；非空时**大小写不分的子串**匹配，和档位筛选取交集。
    void setSearchText(const QString &text);
    QString searchText() const { return m_searchText; }

    // ---------- 显示上限 ----------

    /// 视图最多铺几行（`0` = 全部）。**只影响视图**，模型一行不动。
    void setDisplayLimit(int limit);
    int displayLimit() const { return m_displayLimit; }

    /// 「显示多少行」那几档，**顺序就是界面上的顺序**；`0` 表示「全部」。
    /// 两页共用同一份（§9.8 抽出来的"行数上限"），文案也在这儿。
    static QVector<int> lineLimitChoices();
    static QString lineLimitText(int limit);
    /// 下拉框第 @p index 项在数据里存什么（`QComboBox::itemData`）。
    static int lineLimitOfIndex(int index);

    /// 这一行过不过现在的筛选（档位 ∧ 搜索）。
    bool matches(const Line &line) const;
    /// 同上，但手上只有级别和正文（流式追加时那一行还没进模型用这一条）。
    bool matches(Level level, const QString &text) const;
    /// 过筛的那些行，按时间顺序；超过显示上限时只留**最后** N 行。
    QVector<VisibleLine> visibleLines() const;
    /// 过筛的有几行（不受显示上限影响）。
    int matchingLineCount() const;

    // ---------- 导出 ----------

    /// 全部行，一行一句、末尾带换行。空模型回空串。
    QString allText() const;
    /// 只挑**不轻于** @p minLevel 的那些行（错误报告里那段"错误摘录"用它）。
    QString textFrom(Level minLevel) const;
    /// 只挑过了现在这道筛的那些行（用户屏幕上看到的那一份）。
    QString visibleText() const;

private:
    QVector<Line> m_lines;
    QVector<int> m_counts;      ///< 每一档几条独立记录（含被筛掉的）
    QVector<bool> m_visible;    ///< 每一档显不显示
    QString m_searchText;
    int m_capacity = defaultCapacity();
    int m_displayLimit = 0;
};
