/**
 * @file WorldSettingsPage.cpp
 * @brief 实现 WorldSettingsPage：世界信息 + 三个开关 + 一张游戏规则表。
 * @author 0x7fffff
 * @date 2026-09-28
 * @note 见 WorldSettingsPage.hpp 的说明。这里只留实现上的坑。
 */

#include "WorldSettingsPage.hpp"

#include <QAbstractButton>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
// 摆难度那一格要**先挡上信号**（理由见 setWorld() 里那段）。
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include "ElidedLabel.hpp"
#include "MainWindowUI.hpp"
#include "ToggleSwitch.hpp"

namespace {

/// 规则表里那一行有多高。取名那一格**上下留一点**（46 是仓库里行高的常量，见
/// MainWindowUI::versionTreeRowHeight()）—— 行里是个开关，太挤了开关会贴边。
int ruleRowHeight() {
    return MainWindowUI::versionTreeRowHeight();
}

/**
 * @brief 规则表那一列的条目代理：**选中态 / 当前项 / 悬停态一个都不画**。
 *
 * ★★ 2026-09-28，为什么需要它：这一页每一行都是一个自己摆的控件（开关 / 输入框），列表
 *    本身**没有**"选中 / 当前"这回事 —— 构造里也写了 `setSelectionMode(NoSelection)`。
 *    可是这拦不住它：`addItem()` 之后、视图**第一次布局**的那一下，Qt 会把第 0 行设成
 *    **当前项**（实测：`setWorld()` 那一刻 `currentRow()` 还是 -1，`resize()` 之后就变成
 *    0 了 —— 所以"摆完内容顺手清一下当前项"是**没用**的，下一次布局它又回来），而 Fusion
 *    画当前项用的是一道**竖渐变**（第 0 行整行 #e7f2f9，越往下越深）。屏幕上看着就是
 *    "这一条被选中了"，而它并没有。
 *
 *    所以不跟"什么时候被设上的"较劲，从**画的那一侧**掐掉：把这个状态抹掉再交给样式。
 *    和 `VersionTreeDelegate::paint()` 里那句 `opt.state &= ~QStyle::State_MouseOver`、
 *    `InstanceGroupDelegate::paint()` 里同一句是同一个做法（那两处也是同一个理由：
 *    视图给的状态不是这一列想要的样子）。
 *
 * NOTE: 这一列**故意**没有悬停高亮 —— 行里是开关和输入框，鼠标划过整行变个色说明不了
 *       任何事（和实例树 / 版本树那种"一行可以点开"的列表不一样）。
 */
class RuleItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        QStyleOptionViewItem opt = option;
        opt.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus |
                       QStyle::State_MouseOver);
        QStyledItemDelegate::paint(painter, opt, index);
    }
};

/// 那一列标签（「允许作弊」这种）的宽度。**钉死**（不是让它自己撑）：不钉的话，
/// 四个标签各占各的宽度，右边那列控件就左右对不齐了。
constexpr int kLabelWidth = 76;

/// 一个 `WorldView` 的"台账"（`isDirty()` 就是拿它比等不等）。
///
/// ★ 为什么不逐个字段比：那一套要写三遍（名字 / 三个开关 / 几十条规则），而这个文件里
///   已经有 `editedView()` 这条"从控件里收一份出来"的路了 —— 拿它收一份、和基线比对，
///   一处就够。
///
/// NOTE: 每一段都带**长度前缀**（"长度:内容;"），不能图省事直接拼 —— 规则名和规则值都是
///       用户可以任意改的字符串，直接拼的话「a」+「bc」和「ab」+「c」会拼成一模一样的串，
///       于是"改过没有"答错。
QString signatureOf(const WorldSettingsPage::WorldView &view) {
    QString out;
    const auto put = [&out](const QString &text) {
        out += QString::number(text.size());
        out += QLatin1Char(':');
        out += text;
        out += QLatin1Char(';');
    };
    put(view.levelName);
    out += view.allowCommands ? QLatin1Char('1') : QLatin1Char('0');
    out += view.hardcore ? QLatin1Char('1') : QLatin1Char('0');
    put(QString::number(view.difficulty));
    for (const WorldSettingsPage::Rule &rule : view.rules) {
        put(rule.name);
        put(rule.value);
    }
    return out;
}

/// 搭一格"标签 + 控件"的行，返回那一行（标签宽度统一，控件靠右留 stretch）。
QHBoxLayout *makeOptionRow(QWidget *parent, const QString &labelText, QWidget *control,
                           const QString &tip) {
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    auto *label = new QLabel(labelText, parent);
    label->setFixedWidth(kLabelWidth);
    if (!tip.isEmpty()) {
        label->setToolTip(tip);
    }
    row->addWidget(label);
    row->addWidget(control);
    row->addStretch(1);
    return row;
}

}  // namespace

WorldSettingsPage::WorldSettingsPage(QWidget *parent) : QWidget(parent) {
    setObjectName(pageName());

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    // ---------- 顶上那一排：返回 | 标题 | 右边三颗 ----------
    auto *top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(8);
    m_back = new QPushButton(backText(), this);
    m_back->setObjectName(backButtonName());
    connect(m_back, &QPushButton::clicked, this, &WorldSettingsPage::backRequested);
    top->addWidget(m_back);

    m_title = new ElidedLabel(this);
    m_title->setObjectName(titleLabelName());
    // 标题里是**存档文件夹 + 世界名**，两头都有用的信息 ⇒ 从中间省略
    // （和文件管理那几页的行名同一条理由，见 ElidedLabel::setElideMode 的 NOTE）。
    static_cast<ElidedLabel *>(m_title)->setElideMode(Qt::ElideMiddle);
    {
        QFont titleFont = m_title->font();
        titleFont.setBold(true);
        m_title->setFont(titleFont);
    }
    top->addWidget(m_title, 1);

    m_openFolder = new QPushButton(openFolderText(), this);
    m_openFolder->setObjectName(openFolderButtonName());
    connect(m_openFolder, &QPushButton::clicked, this,
            [this] { emit openFolderRequested(); });
    top->addWidget(m_openFolder);

    m_backup = new QPushButton(backupText(), this);
    m_backup->setObjectName(backupButtonName());
    m_backup->setToolTip(tr("把这个世界的 level.dat 原样留一份（写之前也会自动留）。"));
    connect(m_backup, &QPushButton::clicked, this, &WorldSettingsPage::backupRequested);
    top->addWidget(m_backup);

    m_save = new QPushButton(saveText(), this);
    m_save->setObjectName(saveButtonName());
    connect(m_save, &QPushButton::clicked, this, &WorldSettingsPage::saveRequested);
    top->addWidget(m_save);

    root->addLayout(top);

    // ---------- 正文（"读不出来"那一档整块藏起来）----------
    m_body = new QWidget(this);
    m_body->setObjectName(bodyName());
    auto *body = new QVBoxLayout(m_body);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(8);

    m_info = new QLabel(m_body);
    m_info->setObjectName(infoLabelName());
    m_info->setWordWrap(true);
    // 「次要信息」那一档颜色：和仓库里别处一样，问调色板要，不写死灰度
    // （见 MainWindowUI 里 palette 那一路）。
    m_info->setForegroundRole(QPalette::PlaceholderText);
    body->addWidget(m_info);

    // 世界名：可编辑，但**盘上没有这一格时它是灰的**（见 WorldView 那一段）。
    m_nameEdit = new QLineEdit(m_body);
    m_nameEdit->setObjectName(nameEditName());
    body->addLayout(makeOptionRow(
        m_body, tr("世界名"), m_nameEdit,
        // ★ 星号在界面上是原样画出来的，重点一律用「」（2026-09-28）。
        tr("level.dat 里记着的那个名字。1.13 起游戏列表显示的是「文件夹名」，这一个"
           "只在旧存档、以及部分工具里看得见。")));

    m_allowCommands = new ToggleSwitch(m_body);
    m_allowCommands->setObjectName(allowCommandsName());
    m_allowCommands->setToolTip(tr("打开之后，这个世界里能用指令（单机也要开作弊）。"));
    body->addLayout(makeOptionRow(m_body, tr("允许作弊"), m_allowCommands, QString()));

    m_hardcore = new ToggleSwitch(m_body);
    m_hardcore->setObjectName(hardcoreName());
    m_hardcore->setToolTip(tr("极限模式：死一次这个世界就不能再玩了。"));
    body->addLayout(makeOptionRow(m_body, tr("极限模式"), m_hardcore, QString()));

    m_difficulty = new QComboBox(m_body);
    m_difficulty->setObjectName(difficultyName());
    for (int level = 0; level <= 3; ++level) {
        m_difficulty->addItem(difficultyOptionText(level), level);
    }
    body->addLayout(makeOptionRow(m_body, tr("难度"), m_difficulty, QString()));

    // ---------- 游戏规则 ----------
    auto *rulesRow = new QHBoxLayout;
    rulesRow->setContentsMargins(0, 0, 0, 0);
    rulesRow->setSpacing(8);
    auto *rulesLabel = new QLabel(tr("游戏规则"), m_body);
    rulesLabel->setFixedWidth(kLabelWidth);
    rulesRow->addWidget(rulesLabel);
    m_ruleFilter = new QLineEdit(m_body);
    m_ruleFilter->setObjectName(ruleFilterName());
    m_ruleFilter->setPlaceholderText(ruleFilterPlaceholder());
    m_ruleFilter->setClearButtonEnabled(true);
    connect(m_ruleFilter, &QLineEdit::textChanged, this, [this] { rebuildRuleRows(); });
    rulesRow->addWidget(m_ruleFilter, 1);
    body->addLayout(rulesRow);

    m_ruleList = new QListWidget(m_body);
    m_ruleList->setObjectName(ruleListName());
    // 一行一个自己摆的控件 ⇒ 不用选中态、不要焦点框、不要自带滚动条之外的任何装饰。
    m_ruleList->setSelectionMode(QAbstractItemView::NoSelection);
    m_ruleList->setFocusPolicy(Qt::NoFocus);
    m_ruleList->setAlternatingRowColors(false);
    m_ruleList->setUniformItemSizes(true);
    m_ruleList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // ★ 选中 / 当前项 / 悬停一个都不画（理由见上面 RuleItemDelegate 那段 ★★ —— 光靠
    //   NoSelection 拦不住第 0 行被当成"当前项"染一道竖渐变）。
    m_ruleList->setItemDelegate(new RuleItemDelegate(m_ruleList));
    body->addWidget(m_ruleList, 1);

    root->addWidget(m_body, 1);

    // ---------- 底下那行小字 ----------
    m_note = new QLabel(this);
    m_note->setObjectName(noteLabelName());
    m_note->setWordWrap(true);
    m_note->setForegroundRole(QPalette::PlaceholderText);
    root->addWidget(m_note);

    // 一开始没摆世界：底下那句"还没打开"和"读不出来"是两回事，但这一页永远不会以
    // 空着的样子露出来（MainWindow 一定先 setWorld / setUnreadable 才切到这一格），
    // 所以这里只摆一个中性的初始文案。
    m_savedAt.clear();
    // ★ `isDirty()` 那条基线**一开始就要有值**（空串 = 和"一个空 WorldView"不一样，
    //   于是"没摆过世界的一页"会被判成改过）。不摆这一笔的后果不是"少问一句"，而是
    //   **反过来**：只建了页、还没 setWorld 的那点地方（验证工程里单独 new 出来的
    //   对话框就是）按返回箭头会关不掉 —— 界面上表现为"这颗键按下去没反应"。
    m_baselineSignature = signatureOf(WorldView());
    refreshNote();
    refreshSaveEnabled();
}

// ---------------------------------------------------------------------------
// 名字与文案
// ---------------------------------------------------------------------------

QString WorldSettingsPage::pageName() {
    return QStringLiteral("worldSettingsPage");
}
QString WorldSettingsPage::titleLabelName() {
    return QStringLiteral("worldSettingsTitle");
}
QString WorldSettingsPage::infoLabelName() {
    return QStringLiteral("worldSettingsInfo");
}
QString WorldSettingsPage::nameEditName() {
    return QStringLiteral("worldSettingsNameEdit");
}
QString WorldSettingsPage::allowCommandsName() {
    return QStringLiteral("worldSettingsAllowCommands");
}
QString WorldSettingsPage::hardcoreName() {
    return QStringLiteral("worldSettingsHardcore");
}
QString WorldSettingsPage::difficultyName() {
    return QStringLiteral("worldSettingsDifficulty");
}
QString WorldSettingsPage::ruleFilterName() {
    return QStringLiteral("worldSettingsRuleFilter");
}
QString WorldSettingsPage::ruleListName() {
    return QStringLiteral("worldSettingsRuleList");
}
QString WorldSettingsPage::ruleNameLabelName() {
    return QStringLiteral("worldSettingsRuleName");
}
QString WorldSettingsPage::ruleToggleName() {
    return QStringLiteral("worldSettingsRuleToggle");
}
QString WorldSettingsPage::ruleEditorName() {
    return QStringLiteral("worldSettingsRuleEditor");
}
QString WorldSettingsPage::noteLabelName() {
    return QStringLiteral("worldSettingsNote");
}
QString WorldSettingsPage::saveButtonName() {
    return QStringLiteral("worldSettingsSaveButton");
}
QString WorldSettingsPage::backupButtonName() {
    return QStringLiteral("worldSettingsBackupButton");
}
QString WorldSettingsPage::backButtonName() {
    return QStringLiteral("worldSettingsBackButton");
}
QString WorldSettingsPage::openFolderButtonName() {
    return QStringLiteral("worldSettingsOpenFolderButton");
}
QString WorldSettingsPage::bodyName() {
    return QStringLiteral("worldSettingsBody");
}

QString WorldSettingsPage::backText() {
    return QStringLiteral("返回");
}
QString WorldSettingsPage::saveText() {
    return QStringLiteral("保存");
}
QString WorldSettingsPage::backupText() {
    return QStringLiteral("备份一份");
}
QString WorldSettingsPage::openFolderText() {
    return QStringLiteral("打开文件夹");
}
QString WorldSettingsPage::ruleFilterPlaceholder() {
    return QStringLiteral("搜索规则");
}
QString WorldSettingsPage::noAddHintText() {
    // ★ 2026-09-28：这句原来是拿 `**已经有**` 强调的 —— 屏幕上会把那两个星号**原样画
    //   出来**（界面上的重点用「」表示，仓库里就这一条规矩）。10j2 那张截图上看得一清二楚。
    return QStringLiteral("这一页只改这个世界里「已经有」的规则 —— 要加新规则请进游戏用 "
                          "「/gamerule」。（往一个玩了几百小时的世界里塞一条游戏不认识的"
                          "规则，不该由启动器来做。）");
}
QString WorldSettingsPage::noMatchText() {
    return QStringLiteral("没有名字里有这几个字的规则。");
}
QString WorldSettingsPage::noRulesText() {
    return QStringLiteral("这个世界里还没有任何游戏规则（进游戏玩一次就会写进去）。");
}
QString WorldSettingsPage::notGzippedText() {
    // ★ 同上：`**不是**` 那两个星号会原样画在屏幕上，改成「不是」。
    return QStringLiteral("注意：盘上这份 level.dat「不是」gzip 包着的（正常存档都是）。"
                          "我们按原样写回去，不替你改形状。");
}

QString WorldSettingsPage::difficultyOptionText(int difficulty) {
    switch (difficulty) {
    case 0:
        return QStringLiteral("和平");
    case 1:
        return QStringLiteral("简单");
    case 2:
        return QStringLiteral("普通");
    case 3:
        return QStringLiteral("困难");
    default:
        return QString();
    }
}

// ---------------------------------------------------------------------------
// 摆内容
// ---------------------------------------------------------------------------

void WorldSettingsPage::setWorld(const WorldView &view) {
    m_view = view;
    m_unreadable = false;
    m_unreadableReason.clear();
    m_savedAt.clear();
    m_backupAvailable = false;
    m_baselineSignature = signatureOf(view);

    m_ruleFilter->clear();

    m_title->setText(tr("世界设置：%1").arg(view.dirName));
    m_nameEdit->setText(view.levelName);
    m_nameEdit->setEnabled(view.hasLevelName);
    m_allowCommands->setChecked(view.allowCommands);
    m_allowCommands->setEnabled(view.hasAllowCommands);
    m_hardcore->setChecked(view.hardcore);
    m_hardcore->setEnabled(view.hasHardcore);
    {
        // 摆难度**不能发 currentIndexChanged**（现在没接槽，但摆内容时发信号是个坑：
        // 以后谁接上去就会在"刚打开"的时候被叫一次）。先挡上。
        const QSignalBlocker blocker(m_difficulty);
        const int index = m_difficulty->findData(view.difficulty);
        m_difficulty->setCurrentIndex(index >= 0 ? index : 0);
        m_difficulty->setEnabled(view.hasDifficulty);
    }

    // 那行信息：把几件"只读的事实"拼成一句。**缺的那几位不写**（不拿 0 / 空串顶替）。
    QStringList facts;
    if (view.hasVersionName) {
        facts.append(tr("游戏版本 %1").arg(view.versionName));
    }
    if (view.hasFormat) {
        facts.append(tr("存档格式 %1").arg(view.formatText));
    }
    if (view.hasLastPlayed && view.lastPlayedMs > 0) {
        const QDateTime when = QDateTime::fromMSecsSinceEpoch(view.lastPlayedMs);
        facts.append(tr("上次游玩 %1").arg(when.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
    }
    m_info->setText(facts.join(QStringLiteral("　·　")));

    // `false`：这一下换的是**世界**，不能把上一个世界页面上的值收进这一份里（见 .cpp 里
    // rebuildRuleRows 的 ★）。
    rebuildRuleRows(false);
    applyUnreadableState();
    refreshNote();
    refreshSaveEnabled();
}

void WorldSettingsPage::setUnreadable(const QString &dirName, const QString &reason) {
    m_view = WorldView();
    m_view.dirName = dirName;
    m_unreadable = true;
    m_unreadableReason = reason;
    m_savedAt.clear();
    m_backupAvailable = false;
    m_title->setText(tr("这个世界可能坏了：%1").arg(dirName));
    m_ruleList->clear();
    m_ruleRows.clear();
    applyUnreadableState();
    refreshNote();
    refreshSaveEnabled();
}

void WorldSettingsPage::applyUnreadableState() {
    // 读不出来那一档：**整块正文藏起来**（它摆的那些值一个都不作数），顶上只留
    // 「返回」「打开文件夹」「备份一份」—— 这三颗在这一档里正是最该做的事。
    m_body->setVisible(!m_unreadable);
    m_save->setVisible(!m_unreadable);
}

void WorldSettingsPage::setBackupAvailable(bool available) {
    m_backupAvailable = available;
    refreshNote();
}

void WorldSettingsPage::markSaved() {
    m_savedAt = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    // ★ 基线跟着往前挪一格 —— 存下去的就是页面上这一份（`saveWorldSettings()` 是拿
    //   `editedView()` 写盘的）。不挪的话，用户"存完再按返回箭头"会被问一句
    //   「改过的东西还没存」，而那句话此刻是假的。
    m_baselineSignature = signatureOf(editedView());
    refreshNote();
}

bool WorldSettingsPage::isDirty() const {
    // "读不出来"那一档**永远不算改过**：那一档整块正文都藏着，一个格子都动不了
    // （`applyUnreadableState()` 把它 setVisible(false)），没什么可丢的。
    if (m_unreadable) {
        return false;
    }
    return signatureOf(editedView()) != m_baselineSignature;
}

// ---------------------------------------------------------------------------
// 那张规则表
// ---------------------------------------------------------------------------

int WorldSettingsPage::ruleNameRole() {
    return Qt::UserRole;
}

ToggleSwitch *WorldSettingsPage::allowCommandsSwitch() const {
    return m_allowCommands;
}

ToggleSwitch *WorldSettingsPage::hardcoreSwitch() const {
    return m_hardcore;
}

void WorldSettingsPage::harvestEdits() {
    // 把**现在摆出来的那些行**里的值收回 `m_view.rules` 里。
    //
    // ★★ 为什么必须有这一步（2026-09-28，加 Z16 的时候当场红的）：`rebuildRuleRows()` 是
    //   `m_ruleList->clear()` + 重建 —— 行里那些开关和输入框是 `setItemWidget()` 摆进去的真
    //   控件，`clear()` 会把它们连同用户刚打的字一起销毁；而这一页**每次改搜索框都会重建
    //   一遍**。于是"改一条规则 → 顺手搜一下别的 → 存"这条路原来会把刚改的那条**悄悄丢回
    //   盘上那个值**（`editedView()` 对被筛掉的行是回 `m_view` 里取，而 `m_view` 一直是刚
    //   打开时那一份）。屏幕上一点异常都没有，用户存完再进来才发现某一条没改上。
    //
    //   规矩：**控件里的值就是这一页的真值**，重建之前先收回来（`m_view` 是"摆出来的那一份
    //   的台账"，`editedView()` 从控件里取值、把台账里没露面的那些原样带出去）。
    if (m_unreadable) {
        return;
    }
    for (Rule &rule : m_view.rules) {
        const int row = m_ruleRows.value(rule.name, -1);
        if (row >= 0) {
            rule.value = valueAt(row);
        }
    }
}

void WorldSettingsPage::rebuildRuleRows(bool harvestFromRows) {
    // ★ 只有"同一个世界上重摆一遍"（改搜索框）才收回改动。`setWorld()` 那一路传 false ——
    //   那一下 `m_view` 刚换成**另一个世界**，而 `m_ruleRows` 里还是上一个世界的行号，
    //   收一遍会把新世界里碰巧同名的规则改写成上一个世界页面上的值。
    if (harvestFromRows) {
        harvestEdits();
    }
    m_ruleList->clear();
    m_ruleRows.clear();

    // 一条都没有 / 一条都没筛出来：摆**一行不能点的提示**，而不是留一片空白 ——
    // 空白看着像"还没读完"，而这两种情况都是"读完了，就是这个结果"。
    if (!m_unreadable && m_view.rules.isEmpty()) {
        auto *item = new QListWidgetItem(m_ruleList);
        item->setText(noRulesText());
        item->setFlags(Qt::NoItemFlags);
        item->setSizeHint(QSize(0, ruleRowHeight()));
        refreshSaveEnabled();
        return;
    }

    int shown = 0;
    for (const Rule &rule : m_view.rules) {
        if (!ruleMatches(rule)) {
            continue;
        }
        auto *item = new QListWidgetItem(m_ruleList);
        item->setData(ruleNameRole(), rule.name);
        item->setSizeHint(QSize(0, ruleRowHeight()));

        auto *row = new QWidget(m_ruleList);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(6, 0, 6, 0);
        layout->setSpacing(8);

        auto *name = new ElidedLabel(row);
        name->setObjectName(ruleNameLabelName());
        name->setElideMode(Qt::ElideRight);
        name->setText(rule.name);
        name->setToolTip(rule.name);
        layout->addWidget(name, 1);

        if (rule.kind == Kind::Bool) {
            auto *toggle = new ToggleSwitch(row);
            toggle->setObjectName(ruleToggleName());
            // 「true」以外的任何写法都不算开（大小写按盘上那份原样比，和
            // NbtTree::boolValue() 一条规矩）。
            toggle->setChecked(rule.value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            layout->addWidget(toggle);
        } else {
            auto *edit = new QLineEdit(row);
            edit->setObjectName(ruleEditorName());
            if (rule.kind == Kind::Int) {
                // 只能填整数：**在这里挡住**比"存下去再由游戏报错"好 —— 游戏那边对一个
                // 读不成数字的 gamerule 是用默认值，屏幕上一点异常都没有。
                edit->setValidator(new QRegularExpressionValidator(
                    QRegularExpression(QStringLiteral("-?\\d{1,18}")), edit));
            }
            edit->setText(rule.value);
            connect(edit, &QLineEdit::textChanged, this,
                    [this] { refreshSaveEnabled(); });
            layout->addWidget(edit, 1);
        }

        m_ruleList->addItem(item);
        m_ruleList->setItemWidget(item, row);
        m_ruleRows.insert(rule.name, shown);
        ++shown;
    }

    if (shown == 0) {
        auto *item = new QListWidgetItem(m_ruleList);
        item->setText(noMatchText());
        item->setFlags(Qt::NoItemFlags);
        item->setSizeHint(QSize(0, ruleRowHeight()));
    }
    refreshSaveEnabled();
}

bool WorldSettingsPage::ruleMatches(const Rule &rule) const {
    const QString needle = m_ruleFilter->text().trimmed();
    if (needle.isEmpty()) {
        return true;
    }
    // 只搜名字。规则名是全英文小写驼峰（`keepInventory`），用户记的也是它；
    // 值那一列不参与 —— 拿 "true" 去搜会一次筛出几十条，那不是搜索是噪音。
    return rule.name.contains(needle, Qt::CaseInsensitive);
}

QAbstractButton *WorldSettingsPage::ruleToggleAt(int row) const {
    if (row < 0 || row >= m_ruleList->count()) {
        return nullptr;
    }
    QWidget *host = m_ruleList->itemWidget(m_ruleList->item(row));
    return host == nullptr ? nullptr : host->findChild<QAbstractButton *>(ruleToggleName());
}

QLineEdit *WorldSettingsPage::ruleEditorAt(int row) const {
    if (row < 0 || row >= m_ruleList->count()) {
        return nullptr;
    }
    QWidget *host = m_ruleList->itemWidget(m_ruleList->item(row));
    return host == nullptr ? nullptr : host->findChild<QLineEdit *>(ruleEditorName());
}

QString WorldSettingsPage::valueAt(int row) const {
    if (QAbstractButton *toggle = ruleToggleAt(row)) {
        return toggle->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (QLineEdit *edit = ruleEditorAt(row)) {
        return edit->text().trimmed();
    }
    return QString();
}

void WorldSettingsPage::refreshSaveEnabled() {
    if (m_unreadable) {
        return;
    }
    bool ok = true;
    for (int row = 0; row < m_ruleList->count(); ++row) {
        QLineEdit *edit = ruleEditorAt(row);
        if (edit != nullptr && edit->text().trimmed().isEmpty()) {
            ok = false;
            break;
        }
    }
    m_save->setEnabled(ok);
    m_save->setToolTip(ok ? QString()
                          : tr("有一条规则的值是空的 —— 填上再存（空值等于把它写成坏值）。"));
}

WorldSettingsPage::WorldView WorldSettingsPage::editedView() const {
    WorldView view = m_view;
    if (m_unreadable) {
        return view;
    }
    if (view.hasLevelName) {
        view.levelName = m_nameEdit->text();
    }
    if (view.hasAllowCommands) {
        view.allowCommands = m_allowCommands->isChecked();
    }
    if (view.hasHardcore) {
        view.hardcore = m_hardcore->isChecked();
    }
    if (view.hasDifficulty && m_difficulty->currentIndex() >= 0) {
        view.difficulty = m_difficulty->currentData().toInt();
    }

    // 规则**按盘上的顺序走一遍**（`m_view.rules` 就是那个顺序），只有"现在摆出来过的"
    // 那些才去控件里取值。被筛掉的那些拿 `m_view` 里的那一份 —— 那一份是**上一次重建
    // 之前收上来的**（`rebuildRuleRows()` → `harvestEdits()`），所以它带着用户在这页上
    // 改过的值，不是"筛掉时读到的东西"。挨着这一段的 ★★ 在 rebuildRuleRows() 上头。
    view.rules.clear();
    for (const Rule &rule : m_view.rules) {
        Rule edited = rule;
        const int row = m_ruleRows.value(rule.name, -1);
        if (row >= 0) {
            edited.value = valueAt(row);
        }
        view.rules.append(edited);
    }
    return view;
}

// ---------------------------------------------------------------------------
// 底下那行字 / 事件
// ---------------------------------------------------------------------------

void WorldSettingsPage::refreshNote() {
    if (m_unreadable) {
        m_note->setText(tr("%1\n这个世界现在只能看和备份 —— 读不出它的存档内容，"
                           "所以我们不往上写任何东西。")
                            .arg(m_unreadableReason));
        return;
    }
    QStringList lines;
    if (!m_savedAt.isEmpty()) {
        lines.append(tr("已保存（%1）。写之前的那一份留在备份里。").arg(m_savedAt));
    } else {
        lines.append(tr("改完按「保存」。写下去之前会自动留一份原来的 level.dat。"));
    }
    if (m_backupAvailable) {
        lines.append(tr("这个世界的备份已经有了（「备份一份」会再留一份新的）。"));
    }
    lines.append(noAddHintText());
    if (!m_view.gzipped) {
        lines.append(notGzippedText());
    }
    m_note->setText(lines.join(QLatin1Char('\n')));
}

void WorldSettingsPage::keyPressEvent(QKeyEvent *event) {
    // Esc = 返回世界列表（理由见 .hpp 里那一段：不然它会冒到整个实例设置上去）。
    if (event->key() == Qt::Key_Escape) {
        emit backRequested();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void WorldSettingsPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // ★ 焦点只能在这一刻补（切到这一格时 Qt 会把焦点给新露面的控件，而这一页最想让人
    //   动的就是那张表）。不补的话按 Tab 得从顶上「返回」一颗一颗挪下来。
    if (!m_unreadable) {
        m_ruleList->setFocus();
    }
}
