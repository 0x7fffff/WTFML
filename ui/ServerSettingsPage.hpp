/**
 * @file ServerSettingsPage.hpp
 * @brief 「服务端设置」那一整页（coding plan §6.3 第二阶段 / 里程碑 M4-4）：常用设置那张表单、
 *        原始文本编辑、世界备份、体检。
 * @author 0x7fffff
 * @date 2026-10-13
 *
 * 【它是干什么的】
 *
 * 一个服务端装好、跑起来之后，日常要动的东西全在这一页上：`server.properties` 里那十几个
 * 旋钮（§6.3 点名的"常用字段表单 + 原始文本编辑"）、世界备份（"世界备份和定时备份"里的前
 * 一半）、体检（"这也对不对劲"那一遍）。
 *
 * 【为什么这一页有**两种**编辑方式，它们怎么不打起来】
 *
 * §6.3 两句是并列的：**表单**（`server-port` 那一格只收 1~65535）和**原始文本**（整份文件
 * 一个字节不少地摆出来）。两种都要能原地往返，于是"以谁为准"必须只有一条规矩 ——
 *
 *   * 这一页手里**始终有一份原文**（`rawText()`），它是**唯一**会被写回盘上的东西；
 *   * 表单里改过的那几项是**叠在原文上的补丁**（`changedOnly()`），不是"另存一份配置"；
 *   * 保存 = 原文 + 补丁（`MainWindow` 那一段：`parseProperties` → `applyEdits` → 写盘）。
 *
 * 这么切之后，两边都不会吃掉对方：用户只在表单里改了端口就保存 ⇒ 文件里别的行（注释、他
 * 手写的别的键、键的顺序、行尾）一个字节都不动；用户只在文本框里删了一行就保存 ⇒ 表单那
 * 一格都没被动过（补丁是空的）。
 *
 * ★ **切页那一下由 `MainWindow` 接**（`sectionChanged`）：从「常用设置」切到「原始文本」时
 *   把补丁先合进文本框（用户看得见自己刚改的那一项），从「原始文本」切回表单时按新原文重摆
 *   表单并把基线重设到那一份。这一页自己**不做这件事** —— 它不认 `server.properties` 的
 *   语法（`ui/` 不许 include `src/`），那一步的解析和拼字全在 `MainWindow`。
 *
 * 【原始文本是"原样"的：这一页不重排、不格式化】
 *
 * 文本框里摆的就是盘上那一份字节（行尾按它自己的来）。服务端自己写出来的那份文件带一大段
 * `#` 注释，用户多半还想留着它们 —— 所以这一页**不解析、不重排、不"整理"**，和「配置文件」
 * 那一页（`ConfigFileEditorPage`）是同一条规矩。
 *
 * 【服务端在跑的时候这一页是只读的】
 *
 * 服务端**停下来的那一刻会把它内存里那份 `server.properties` 写回盘上**（它会把自己不认得
 * 的键都补上、把值规范化）—— 这时候改文件等于白改，用户按了保存、看着成功了，服务端一停
 * 又变回去了。所以 `setEditable(false, 原因)` 把这一页整个锁上，并**明说为什么**（摆一句
 * 话，不是把按钮悄悄变灰）。备份 / 恢复那两颗同理：它正开着世界里的文件在写。
 *
 * 【这一层只摆控件，判据一条都不在这儿】
 *
 * `ui/` **不 include `src/`**（见 `GroupDialog.hpp` 文件头那段老规矩）：哪几个字段、值域
 * 多少、哪些值是合法的、体检该报哪几条，全由 `MainWindow` 从那几张表翻好喂进来
 * （`setFields()` / `setCheckRows()`）。这一页做到最后只有一个职责：把用户改了什么原样报
 * 出去（`changedOnly()` / `rawText()` / 各种 `*Requested` 信号）。
 */

#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class ToggleSwitch;

class ServerSettingsPage : public QDialog {
    Q_OBJECT

public:
    /// 左栏那四页（**顺序就是左栏那一列的顺序**）。
    enum class Section {
        Common = 0,  ///< 常用设置（表单）
        RawText,     ///< 原始文本（整份 `server.properties`）
        Backups,     ///< 世界备份
        Check,       ///< 体检
    };

    /// 一格表单摆成什么控件（和 `ServerPropertiesForm::Kind` 一一对应；**翻译在
    /// `MainWindow` 那一段**，这一层只认自己的这个枚举）。
    enum class Kind {
        Text,    ///< 输入框
        Number,  ///< 数字框（`min`~`max`）
        Switch,  ///< 拨动开关
        Choice,  ///< 下拉框（`choiceTokens` / `choiceNames` 按下标对齐）
    };

    /// 表单里的一行（**全是显示用的字符串 + 值域**，由翻译点算好）。
    struct FieldView {
        QString key;    ///< 身份（`server.properties` 里那个键）—— **认人用它，不认文字**
        QString label;  ///< 行首那个中文名
        QString hint;   ///< 右边那行小字（"为什么默认是这个"）
        Kind kind = Kind::Text;
        int min = 0;
        int max = 0;
        QStringList choiceTokens;  ///< 下拉框每一项**写进文件**的那个词
        QStringList choiceNames;   ///< 下拉框每一项**显示**的那个词（和上面按下标对齐）
    };

    /// 表单上的一项值（**顺序跟着字段表走**；`ui/` 自己的类型，见文件头那条界线）。
    using Values = QList<QPair<QString, QString>>;

    /// 备份列表里的一行。**全是现成的字符串**（时间 / 大小 / 说明都在翻译点那边拼好）——
    /// 读一份备份的清单要起一次 PowerShell，所以那一趟不在这一页里做（见 `BackupFiles`）。
    struct BackupRow {
        QString path;   ///< 这一份的 zip 路径（身份）
        QString when;   ///< "2026-10-13 14:08"
        QString size;   ///< "12.4 MB"
        QString scope;  ///< "含安全快照" / "这个存档的整个文件夹" 那一句
        bool safety = false;  ///< 是还原之前自动做的那一份（行上要看得出来）
    };

    /// 体检单上的一行。
    struct CheckRow {
        int severity = 2;  ///< 0 = 要处理、1 = 提醒、2 = 说明（和 `ServerCheck::Severity` 对齐）
        QString title;
        QString detail;
        /// 这一条的出路（`ServerCheck::actionToken()` 那个记号）；空 = 这一条没有能按的东西。
        QString actionToken;
    };

    explicit ServerSettingsPage(QWidget *parent = nullptr);

    /// 覆盖层页眉上那行字（`OverlayHost::execPage()` 的标题）。
    static QString windowTitle() { return QStringLiteral("服务端设置"); }

    /// 顶上那行大字：这一页说的是**哪一个**服务端（显示名，不是目录名）。
    void setServerName(const QString &name);
    /// 状态那行小字（`ServerModel::statusName()` 的结论 + 在跑时那句"现在改不了"）。
    void setStatusText(const QString &text);

    // ---------- ① 常用设置 ----------
    /// 摆那十几行（**每次开页都重新喂**：同一页对象会被复用）。会**清掉**上一次的表单，
    /// 调用方接着要调一次 `setValues()`。
    void setFields(const QList<FieldView> &fields);
    /// 摆当前值，**并把"改过"的基线重设到这一份**（这一句同时也是 `clearChanged()`）。
    void setValues(const Values &values);
    /// 表单上现在是什么（不管改没改过）。
    Values values() const;
    /// **用户改过**的那几项（和基线比，值不一样的才算）。保存时报的就是它。
    Values changedOnly() const;
    /// 重设基线（= 现在这一份算"没改过"）。
    void clearChanged();
    /// 这一页能不能改（服务端在跑的时候不能）。@p reason 非空时那句话会摆在保存带左边。
    void setEditable(bool editable, const QString &reason);

    // ---------- ② 原始文本 ----------
    /// 摆一份原文。**同时把"用户动过文本框"那个记号清掉**（这是程序摆进来的那一份）。
    void setRawText(const QString &text);
    QString rawText() const;
    /// 用户在这个文本框里动过吗（**程序摆进来的那一份不算**）。
    bool rawEdited() const;
    /**
     * @brief 把"用户动过文本框"那个记号**打上**（内容已经由外面摆进来了）。
     *
     * 只有一种用法：`MainWindow` 把表单里改过的那几项**合进**文本框那一刻
     * （从「常用设置」切到「原始文本」那一下）。它走的是 `setRawText()`，而那一句按规矩要
     * 把记号清掉（"这是程序摆的"）—— 可合进去的**是用户自己刚改的东西**，清掉就等于告诉
     * 用户"你什么都没改"（保存带会说"没改过什么"，`changedOnly()` 那边又还压着几条补丁）。
     */
    void markRawEdited();

    // ---------- ③ 世界备份 ----------
    void setBackupRows(const QList<BackupRow> &rows);
    /// 现在选中的那一份 zip（没选回空串）。
    QString selectedBackup() const;
    /// 备份那一段顶上那句说明（世界在哪儿、备份落在哪儿 —— 由翻译点拼好）。
    void setBackupNote(const QString &text);

    // ---------- ④ 体检 ----------
    void setCheckRows(const QList<CheckRow> &rows);
    /// 顶上那句总括（`ServerCheck::summaryText()` 的结论）。
    void setCheckSummary(const QString &text);

    // ---------- 底下那条保存带 ----------
    /// 「保存」那颗能不能点 + 左边那行小字。**由 `MainWindow` 现算现喂**
    /// （"改过几项""在跑所以改不了""刚刚存过"这几句话是产品措辞，不在这一层）。
    void setSaveState(bool enabled, const QString &note);

    /// 切到某一页（**不发 `sectionChanged`** —— 这是程序摆的，不是用户点的）。
    void setSection(Section section);
    Section section() const;

signals:
    /// 用户点了左栏另一页（**切页之前**发；`MainWindow` 要借这一下把两边的编辑合起来）。
    void sectionChanged(int section);
    /**
     * @brief 用户在**表单**或者**原始文本**里动了东西（每一下都发）。
     *
     * `MainWindow` 借它重算保存带那句话（"改过 3 项""还动过原始文本"…），然后回头喊一次
     * `setSaveState()` —— 那些字是产品措辞，不在这一层（见 `setSaveState()` 那段）。
     *
     * ★ 程序摆值（`setValues()` / `setRawText()`）**不发**这一个 —— 那不是"用户改了"。
     * ★ 接它的那一头要**便宜**：敲一个字就发一次。
     */
    void edited();
    void saveRequested();
    void revertRequested();
    void backupNowRequested();
    void restoreBackupRequested(const QString &zipPath);
    void deleteBackupRequested(const QString &zipPath);
    void openBackupFolderRequested();
    void recheckRequested();
    /// 体检单上某一条的出路（记号见 `ServerCheck::actionToken()`）。
    void actionRequested(const QString &token);

private:
    /// 摆一行表单（按 `FieldView::kind` 选控件）。
    void addFieldRow(const FieldView &field);
    /// 那一格的控件现在是什么值（字符串形态 —— 开关回 `true`/`false`，下拉框回 token）。
    QString editorValue(const FieldView &field) const;
    /// 把值摆进那一格的控件（**不发"用户改过"那一路信号**：摆的是基线）。
    void setEditorValue(const FieldView &field, const QString &value);
    /// 现在改了哪几项（和基线比）。**唯一出处** —— `changedOnly()` 和保存带都读它。
    Values dirtyValues() const;
    /// 重刷保存带那行小字和「保存」那颗的灰亮（**不发信号**：这一层只管显示）。
    void refreshSaveBar();
    /// 备份那一页那两颗"要用到选中那一份"的按钮的灰亮（没选 / 只读 ⇒ 点不动）。
    void refreshBackupActions();
    /// 体检单上那一条的出路按钮要摆哪些（按出现过的记号去重、顺序按第一次出现）。
    void refreshCheckActions();
    QWidget *fieldEditor(const QString &key) const;
    QTreeWidgetItem *backupItemAt(int row) const;

    // ---------- 控件 ----------
    QLabel *m_titleLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QListWidget *m_sectionList = nullptr;
    QStackedWidget *m_stack = nullptr;

    QScrollArea *m_formScroll = nullptr;
    QWidget *m_formBody = nullptr;
    /// 表单那一竖排（弹簧在**收尾**，新加的行走它前面 —— 同实例设置那条老规矩）。
    QVBoxLayout *m_formLayout = nullptr;

    QPlainTextEdit *m_rawEdit = nullptr;
    QLabel *m_rawNote = nullptr;

    QLabel *m_backupNote = nullptr;
    QTreeWidget *m_backupList = nullptr;
    QPushButton *m_backupNowButton = nullptr;
    QPushButton *m_restoreButton = nullptr;
    QPushButton *m_deleteBackupButton = nullptr;
    QPushButton *m_openBackupFolderButton = nullptr;

    QLabel *m_checkSummary = nullptr;
    QTreeWidget *m_checkList = nullptr;
    QWidget *m_checkActions = nullptr;
    QPushButton *m_recheckButton = nullptr;

    QPushButton *m_revertButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QLabel *m_saveNote = nullptr;

    // ---------- 状态 ----------
    QList<FieldView> m_fields;
    /// 体检单上那几条（摆进来的那一份）。`refreshCheckActions()` 按它去重出路按钮。
    QList<CheckRow> m_checkRows;
    /// 每一行那一格的控件（key → 控件）。认人认 key，不认行号。
    QHash<QString, QWidget *> m_editors;
    /// **基线**：`setValues()` 那一刻表单上的那一份（"改过没有"是拿它比的）。
    QHash<QString, QString> m_baseline;
    /// 用户动过文本框吗（`setRawText()` 会把它清掉）。
    bool m_rawEdited = false;
    /// 这一页现在能不能改（服务端在跑 = false）。
    bool m_editable = true;
    /// 上面那个"不能改"的原因（没在跑时是空的）。
    QString m_notEditableReason;
    /// `MainWindow` 喂进来的保存带那句话（**不是**这一层自己编的）。
    QString m_saveNoteText;
    /// 「保存」那颗能不能点（`setSaveState()` 说了算）。
    bool m_saveAllowed = false;
    /// 正在程序性地摆值（**这时候控件发出来的那些信号要丢掉** —— 它们不是"用户改了"）。
    bool m_settingValues = false;
    /// 正在程序性地摆原文（同上：`setPlainText()` 也会发 `textChanged`）。
    bool m_settingRaw = false;
    /// 正在程序性地切页（左栏那个 `currentRowChanged` 要丢掉，别当成用户点的）。
    bool m_settingSection = false;
    /**
     * @brief 盘上那一份的原文是不是 **CRLF** 行尾。
     *
     * ★ 文本框（`QTextDocument`）只认 `\n`：`setPlainText()` 会把 `\r\n` 拆成段落，
     *   `toPlainText()` 回来的是纯 `\n`。所以这一页把行尾**记在手边**：摆进来时统一成 `\n`，
     *   读出去时再按盘上那份的规矩补回去。少了它，用户只是把端口从 25565 改成 25566，
     *   保存下来那份文件的**每一行都换了行尾**（游戏自己写出来的是 CRLF）。
     *
     * NOTE: 一份**混着**两种行尾的文件会被统一成第一种（真出现那种文件的话）。服务端自己
     *       写出来的那份从来是统一的，所以这里不为它多留一条路。
     */
    bool m_rawCrlf = false;
};
