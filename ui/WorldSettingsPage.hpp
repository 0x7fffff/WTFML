/**
 * @file WorldSettingsPage.hpp
 * @brief 「实例设置 → 文件管理 → 存档」点行尾那支笔之后那一块：这个世界的信息、几个开关、
 *        以及**游戏规则表**（一条一行，值就在行上改）。
 * @author 0x7fffff
 * @date 2026-09-28
 * @note 用户 2026-09-28 原话：「世界根据其存档文件在存档设置（那支笔）里面编辑游戏规则等，
 *       打开之前弹出坏档警告并提供备份按钮」。这一块就是那句话里"在存档设置里面编辑"。
 *
 * 【它和「配置文件」那一页里那个编辑器的关系】
 *
 * 同一个位置、同一个做法：**嵌在文件页里面的第二个格子**（因为 `OverlayHost::openPage()`
 * 在已经有人占着那一层时返回 nullptr，开不出第二个整页覆盖层，见 InstanceSettingsDialog
 * 里 `m_configStack` 那一段）。点笔 = 切到第二格，点「返回」/ 按 Esc = 切回世界列表。
 *
 * 差别在这儿：配置文件那一页给的是一个**纯文本编辑框**（不解析、只帮你找到那一行），
 * 而这一页给的是**表单** —— 因为世界规则这件事的 schema 我们**有**
 * （`level.dat` 是一棵 NBT 树，`GameRules` 就是一张"名字 → 字符串"的表，盘上写得清清楚楚），
 * 和模组配置那种"schema 只活在模组的 Java 代码里"（见 ConfigFileEditorPage 文件头）不一样。
 *
 * 【这一层不碰盘、也不解 NBT】
 *
 * 和 `InstanceSettingsDialog`、`ConfigFileEditorPage` 同一条约定：读盘、写盘、弹话都是
 * MainWindow 的事。更要紧的一条是**`ui/` 不许 include `src/`** —— 所以这一页既拿不到
 * `WorldFiles::WorldInfo` 也拿不到 `NbtTree::GameRule`，它摆的东西全在下面那两个**只用
 * Qt 类型**的结构里（`Rule` / `WorldView`）。从盘上那份翻译过来、以及把页面上改过的翻译
 * 回去，都是 MainWindow 那一段的事。
 *
 * NOTE: 这个翻译**故意不省**（两边的字段名是一一对应的，看着像在抄写）：省掉它就得让
 *       `ui/` 去 include `src/`，那正是这个仓库唯一一条不许越的界线。抄错一处的话
 *       验证工程量的是**页面上那个控件里的值**，会当场红。
 *
 * 【"只改已有的规则，不发明新的"】
 *
 * 这张表里一行一行**全是从盘上读出来的**（`Data/GameRules` 里已有的那些）。这一页没有
 * 「添加规则」——理由写在 `NbtTree::setGameRule()` 那一段里：往一个玩了几百小时的世界里
 * 塞一条游戏不认识的规则不叫编辑。要加规则请进游戏用 `/gamerule`。底下那行小字说了这件事。
 */
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QWidget>

class QAbstractButton;
class ElidedLabel;
class QComboBox;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class ToggleSwitch;

class WorldSettingsPage : public QWidget {
    Q_OBJECT

public:
    /// 一条规则的值是什么形状 —— **决定行尾那一格画成什么**（开关 / 只能填数字的框 /
    /// 随便填的框）。三档和 `NbtTree::ValueKind` 一一对应（那边在 src/，这一层够不着）。
    enum class Kind {
        Bool,  ///< `"true"` / `"false"` ⇒ 拨动开关
        Int,   ///< 整数 ⇒ 只收 `-?数字` 的输入框
        Text,  ///< 别的 ⇒ 随便填
    };

    /// 一条规则摆在这一页上的样子。
    struct Rule {
        QString name;
        QString value;  ///< 盘上那一串（Bool 那两档是 `"true"` / `"false"`）
        Kind kind = Kind::Text;
    };

    /**
     * @brief 这一页上摆的所有东西。**只用 Qt 类型**（见文件头）。
     *
     * 每一位都带一个 `hasXxx`，和 `WorldFiles::WorldInfo` 一样，理由是同一个：**盘上没有的
     * 项不发明**。`hasXxx` 为 false 的那一格在界面上是**灰的**（点不动），而不是拿一个默认
     * 值摆在那儿等用户把它写进去。
     */
    struct WorldView {
        QString dirName;              ///< 这一页看的是哪一个存档文件夹
        bool hasLevelName = false;    ///< 世界名（`level.dat` 里的那一个）
        QString levelName;
        bool hasVersionName = false;  ///< 游戏版本（存档建立时那一版）
        QString versionName;
        bool hasFormat = false;       ///< 存档格式号（`DataVersion`）
        QString formatText;
        bool hasLastPlayed = false;   ///< 上次游玩（毫秒时间戳）
        qint64 lastPlayedMs = 0;
        bool gzipped = true;          ///< 盘上那份是 gzip 包着的（不是的话底下那行小字会说）
        bool hasAllowCommands = false;
        bool allowCommands = false;
        bool hasHardcore = false;
        bool hardcore = false;
        bool hasDifficulty = false;
        int difficulty = 2;           ///< 0 和平 ~ 3 困难
        bool hasRules = false;        ///< `Data/GameRules` **是一张表**（空表也算有；不是表的算没有）
        QList<Rule> rules;
    };

    explicit WorldSettingsPage(QWidget *parent = nullptr);

    /// 摆一个世界的设置（点那支笔时由 InstanceSettingsDialog 调）。
    void setWorld(const WorldView &view);

    /// 摆成"读不出来"那一档：整页只留那句原因 + 「返回」「打开文件夹」「备份一份」。
    ///
    /// NOTE: 这一档存在的理由是**不要留下一个死胡同** —— 用户看到「这个存档可能坏了」那句
    ///       警告之后，总得有个地方落下去（至少能看见原因、能备份、能打开那个文件夹）。
    void setUnreadable(const QString &dirName, const QString &reason);
    /// 现在是不是"读不出来"那一档。
    bool isUnreadable() const { return m_unreadable; }

    /// 页面上**现在**这一份（用户改过的就是它）。`setWorld()` 之后没动过的话，返回的和
    /// 摆进去的那一份等价。
    WorldView editedView() const;

    /// 摆上去之后**用户动过没有**（和 `editedView()` 配套的那一位）。
    ///
    /// ★ 存在的理由只有一个：**覆盖层页眉上那颗返回箭头**（`dialog->reject()`）按不到
    ///   MainWindow 那套"重读一遍盘再比"的判据 —— 那一头没有盘、也不该去碰盘。所以这里
    ///   自己记一条基线（`setWorld()` 和 `markSaved()` 各记一次），拿台账对一下就说得出
    ///   话。和 `ConfigFileEditorPage::isDirty()` 是同一件事的两份实现。
    ///
    /// NOTE: 基线**不能**拿 `m_view` 顶替 —— `markSaved()` 不改 `m_view`（存完之后那份快照
    ///       还是刚打开时的样子），于是"存过再关"会被判成"改过还没存"，白问一句。
    bool isDirty() const;

    /// 这个文件**留过备份没有**（有 ⇒ 「备份一份」旁边的说明改口，见 refreshNote()）。
    ///
    /// NOTE: 由 MainWindow 在打开时告诉它（这一层不碰盘）。和 `ConfigFileEditorPage::
    ///       setBackupAvailable()` 同一件事：进这一页**同时清掉**上一条的答案。
    void setBackupAvailable(bool available);

    /// 写盘**成功了** —— 底下那行字换成「已保存（时:分:秒）」。由 MainWindow 调回来
    /// （它是唯一知道写成了没有的人）。和「配置文件」那个编辑器同一个做法：**没有"写失败了"
    /// 的那一半**，写不进去是挡住用户操作的事，走 MainWindow 的弹窗。
    void markSaved();

    // ---------- 名字（验证工程按这些 objectName 找控件）----------
    static QString pageName();
    static QString titleLabelName();
    static QString infoLabelName();
    static QString nameEditName();
    static QString allowCommandsName();
    static QString hardcoreName();
    static QString difficultyName();
    static QString ruleFilterName();
    static QString ruleListName();
    static QString ruleNameLabelName();
    static QString ruleToggleName();
    static QString ruleEditorName();
    static QString noteLabelName();
    static QString saveButtonName();
    static QString backupButtonName();
    static QString backButtonName();
    static QString openFolderButtonName();
    /// 那一堆"读不出来"的行（整块藏起来/露出来就是它）。
    static QString bodyName();

    static QString backText();
    static QString saveText();
    static QString backupText();
    static QString openFolderText();
    static QString ruleFilterPlaceholder();
    /// 这一页**没有**「添加规则」——底下那行小字说明为什么不给加。
    static QString noAddHintText();
    /// 筛选之后一条都不剩。
    static QString noMatchText();
    /// 这个世界里一条规则都没有（筛选框是空的）。
    static QString noRulesText();
    /// 存档里那份**不是** gzip 包着的（底下那行小字多一句）。
    static QString notGzippedText();

    /// 难度 0~3 的中文名。**和 `WorldFiles::difficultyName()` 逐字一致**（一处是给
    /// 界面看的、一处是给验证工程看的，两边都断言得到）。
    static QString difficultyOptionText(int difficulty);

    // ---------- 给验证工程用的访问器 ----------
    QLineEdit *nameEdit() const { return m_nameEdit; }
    ToggleSwitch *allowCommandsSwitch() const;
    ToggleSwitch *hardcoreSwitch() const;
    QComboBox *difficultyCombo() const { return m_difficulty; }
    QLineEdit *ruleFilter() const { return m_ruleFilter; }
    QListWidget *ruleList() const { return m_ruleList; }
    QLabel *noteLabel() const { return m_note; }
    QLabel *titleLabel() const { return m_title; }
    QPushButton *saveButton() const { return m_save; }
    QPushButton *backButton() const { return m_back; }
    QPushButton *backupButton() const { return m_backup; }
    QPushButton *openFolderButton() const { return m_openFolder; }
    /// 行尾那个开关（只有 `Kind::Bool` 的行有；别的行是 nullptr）。
    QAbstractButton *ruleToggleAt(int row) const;
    /// 行尾那个输入框（`Kind::Int` / `Kind::Text` 的行才有）。
    QLineEdit *ruleEditorAt(int row) const;
    /// 行上那一格名字里写的是哪一条规则（认条目的唯一出处，和 `pathRole()` 一个做法）。
    static int ruleNameRole();

signals:
    /// 用户点了「保存」。**这一层不带任何值出去** —— MainWindow 用 `editedView()` 取
    /// （它是唯一知道该往哪儿写、写之前要留备份的人）。
    void saveRequested();
    /// 用户点了「备份一份」。
    void backupRequested();
    /// 用户点了「打开文件夹」。
    void openFolderRequested();
    /// 用户想回到世界列表（点了「返回」，或者按了 Esc）。切格是 InstanceSettingsDialog
    /// 那两格的事，这一层不自己切。
    void backRequested();

protected:
    /// Esc = 返回世界列表。**得在这一层接住**：不然它会冒到「实例设置」那个 QDialog 上，
    /// 那一下关掉的是整个实例设置页（和 ConfigFileEditorPage 同一个坑）。
    void keyPressEvent(QKeyEvent *event) override;
    /// ★ **焦点只能在这一刻补**（`setParent()` 会清焦点，见 OverlayHost 那一段）——
    ///   切到这一格时把光标放进第一格能打字的地方（世界名）。
    void showEvent(QShowEvent *event) override;

private:
    /// 按 `m_view.rules` 重摆一遍那张表（筛选之后也走它）。**重建行**，不是藏行 ——
    /// 行里的内容是 `setItemWidget()` 摆的真控件（和 InstanceFilesPage 一样，藏不掉）。
    ///
    /// ★ 重建会把行里那些真控件（连同用户刚打的字）销毁，所以**重建之前先把它们收回来**
    ///   （`harvestEdits()`）。`harvestFromRows = false` 只有 `setWorld()` 一路用 ——
    ///   那一下换的是世界，上一个世界页面上的值不能收进这一份里。见 .cpp 里那段 ★★。
    void rebuildRuleRows(bool harvestFromRows = true);
    /// 把现在摆出来的那些行里的值收回 `m_view.rules`（重建之前那一步，见上）。
    void harvestEdits();
    /// 底下那行小字回到"现在该说什么"。
    void refreshNote();
    /// 行里那一格值现在是什么（按控件类型取）。
    QString valueAt(int row) const;
    /// 这一行过不过搜索框里那串字。
    bool ruleMatches(const Rule &rule) const;
    /// 读不出来那一档该露什么、藏什么。
    void applyUnreadableState();
    /// 「保存」给不给点。**判据只有一条**：现在摆出来的那些输入框有没有被清空的
    /// （开关永远不会空）—— 一个规则的值被清空后存下去，等于把那条规则写成一个空串，
    /// 游戏那边会当成坏值用默认的。用户多半是手滑，不是那个意思。
    void refreshSaveEnabled();

    WorldView m_view;
    bool m_unreadable = false;
    QString m_unreadableReason;
    bool m_backupAvailable = false;
    QString m_savedAt;  ///< 非空 = 刚存成功过（底下那行小字要写它）
    /// `isDirty()` 的基线（`setWorld()` 与 `markSaved()` 各记一次，见那个函数的 NOTE）。
    /// 存的是 `signatureOf(view)` 那一串，不是 `WorldView` 本身 —— `WorldView` 没有
    /// `operator==`，而且这里要的是"等不等"这一个答案。
    QString m_baselineSignature;
    /// 规则名 → 现在摆在第几行（**只记摆出来的那些**，被筛掉的进不来）。`editedView()`
    /// 靠它把"用户改过的那一格"收回去 —— 没进这张表的那些保持原值。
    QHash<QString, int> m_ruleRows;

    QLabel *m_title = nullptr;
    QLabel *m_info = nullptr;
    QWidget *m_body = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    ToggleSwitch *m_allowCommands = nullptr;
    ToggleSwitch *m_hardcore = nullptr;
    QComboBox *m_difficulty = nullptr;
    QLineEdit *m_ruleFilter = nullptr;
    QListWidget *m_ruleList = nullptr;
    QLabel *m_note = nullptr;
    QPushButton *m_back = nullptr;
    QPushButton *m_backup = nullptr;
    QPushButton *m_openFolder = nullptr;
    QPushButton *m_save = nullptr;
};
