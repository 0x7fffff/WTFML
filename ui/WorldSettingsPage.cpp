/**
 * @file WorldSettingsPage.cpp
 * @brief 实现 WorldSettingsPage：世界这一行的三件事 + 一个「修改存档文件」的折叠框 +
 *        一张游戏规则表（一条一行，值就在行上改）。
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
#include <QPair>
// 摆难度那一格要**先挡上信号**（理由见 setWorld() 里那段）。
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShowEvent>
#include <QSignalBlocker>
// 正文收尾那根弹簧（见 m_bodyTail 那一段）。
#include <QEvent>
#include <QSpacerItem>
#include <QStyledItemDelegate>
#include <QTimer>   // 搜索框那一发防抖（见 kRuleFilterDelayMs）
#include <QToolButton>
#include <QVBoxLayout>

#include "ElidedLabel.hpp"
#include "GameRuleNames.hpp"   // 规则那一格名字上盖的那层官方中文（见 rebuildRuleRows）
#include "Theme.hpp"   // 頂上那四颗按钮的身份（见下面那几行）
#include "MainWindowUI.hpp"
#include "ToggleSwitch.hpp"

namespace {

/// 规则表里那一行有多高。取名那一格**上下留一点**（46 是仓库里行高的常量，见
/// MainWindowUI::versionTreeRowHeight()）—— 行里是个开关，太挤了开关会贴边。
int ruleRowHeight() {
    return MainWindowUI::versionTreeRowHeight();
}

/// 搜索框改字之后**等手停下来**这么久（ms）才重摆规则行。
///
/// ★ 2026-10-10：原来是一改字就同步重摆一遍，而重摆 = 把每一条命中的规则**重新 new 一遍
///   控件**（`QListWidgetItem` + `QWidget` + `QHBoxLayout` + 两三个子控件，Int 那几行还各带
///   一个 `QRegularExpressionValidator(new QRegularExpression(...))`）。实测 50 行 1.67 ms/次，
///   而一次输入会连着来好几发。同类的"每次按键重算"在仓库里都有这道闸：`ConfigFileEditorPage`
///   是 250 ms 的 `kIndexRebuildDelayMs`，底栏进度是 100 ms 的节流。
constexpr int kRuleFilterDelayMs = 200;

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
    // 「← 返回」和「打开文件夹」是往回走 / 开个窗（一般性）。
    Theme::markButton(m_back, Theme::ButtonKind::Neutral);
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

    // ---------- 这个世界能做的三件事（2026-09-29：原先那支笔上的小菜单搬进来了）----------
    //
    // 用户原话：「改文件的设置上方是重命名 打开文件夹 删除世界（弹窗确认）」。三颗都在这儿，
    // **一个字节的盘都不碰**：改名和删除是 MainWindow 的事（它要弹输入框 / 确认框、
    // 还要在成功之后把这一页的名字摆回来或者干脆收掉这一页）。
    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);

    m_rename = new QPushButton(renameText(), this);
    m_rename->setObjectName(renameButtonName());
    m_rename->setToolTip(tr("改这个存档的文件夹名。游戏的世界列表里显示的也是它。"));
    Theme::markButton(m_rename, Theme::ButtonKind::Neutral);
    connect(m_rename, &QPushButton::clicked, this, &WorldSettingsPage::renameRequested);
    actions->addWidget(m_rename);

    m_openFolder = new QPushButton(openFolderText(), this);
    m_openFolder->setObjectName(openFolderButtonName());
    Theme::markButton(m_openFolder, Theme::ButtonKind::Neutral);
    connect(m_openFolder, &QPushButton::clicked, this,
            [this] { emit openFolderRequested(); });
    actions->addWidget(m_openFolder);

    m_delete = new QPushButton(deleteText(), this);
    m_delete->setObjectName(deleteButtonName());
    m_delete->setToolTip(tr("把这个世界连同里面的东西从盘上删掉（会先问一句）。"));
    // ★ 红：用户点名这一档叫「不可撤销（如修改存档，删除实例）」—— 这一颗删的就是一整个
    //   存档目录（里面的 region 是几十上百 MB 的游玩记录，删了拿不回来）。
    Theme::markButton(m_delete, Theme::ButtonKind::Danger);
    connect(m_delete, &QPushButton::clicked, this, &WorldSettingsPage::deleteRequested);
    actions->addWidget(m_delete);

    actions->addStretch(1);
    root->addLayout(actions);

    m_backup = new QPushButton(backupText(), this);
    m_backup->setObjectName(backupButtonName());
    m_backup->setToolTip(tr("把这个世界的 level.dat 原样留一份（写之前也会自动留）。"));
    // 「备份」= 多出一份（一般性）。
    Theme::markButton(m_backup, Theme::ButtonKind::Neutral);
    connect(m_backup, &QPushButton::clicked, this, &WorldSettingsPage::backupRequested);

    m_save = new QPushButton(saveText(), this);
    m_save->setObjectName(saveButtonName());
    // ★ 一律红：用户点名这一档叫「不可撤销（如**修改存档**，删除实例）」——
    //   这一颗写的就是存档里那份 level.dat。上面确实会自动留一代备份
    //   （那是"能挽回来"的保障），但对用户而言这仍然是一次**向着已有存档**
    //   的写入 —— 和右边那颗「备份」（多出一份）正好一红一黑。
    Theme::markButton(m_save, Theme::ButtonKind::Danger);
    connect(m_save, &QPushButton::clicked, this, &WorldSettingsPage::saveRequested);

    root->addLayout(top);

    // ---------- 正文（"读不出来"那一档整块藏起来）----------
    m_body = new QWidget(this);
    m_body->setObjectName(bodyName());
    m_bodyLayout = new QVBoxLayout(m_body);
    QVBoxLayout *body = m_bodyLayout;   // 这一段里那颗指针用得太多，另起一个短名字
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(8);

    m_info = new QLabel(m_body);
    m_info->setObjectName(infoLabelName());
    m_info->setWordWrap(true);
    // 「次要信息」那一档颜色：和仓库里别处一样，问调色板要，不写死灰度
    // （见 MainWindowUI 里 palette 那一路）。
    m_info->setForegroundRole(QPalette::PlaceholderText);
    body->addWidget(m_info);

    // ---------- 「修改存档文件」那个折叠框（默认收起）----------
    //
    // 用户原话：「修改文件的设置放在一个类似版本下载那里的收起来的框里，默认收起，展开是
    // 弹出备份警告」。做法和「实例设置 → 启动」那一页的「高级选项」逐字一样（`QToolButton`
    // 可勾选 + 箭头 + 文字在图标旁边），只是这一颗点下去**不自己开门**：它先发
    // `fileEditingRequested()`，由 MainWindow 弹完那句备份警告，再拿 `setFileEditingOpen()`
    // 把结果摆回来（见那两个函数的说明）。
    //
    // 框里是**这一页上会去动 `<存档目录>/level.dat` 的所有东西**：世界名 / 允许作弊 /
    // 极限模式 / 难度四格，加上底下的游戏规则那一列。
    //
    // ★ 2026-09-29 用户点名的第二件事：「下面的 gamerules 不会改坏吗，如果可能会的话也放到
    //   那个框里去」。答案是不会"另算一档"——**它们改的是同一份文件**（游戏规则就存在
    //   `level.dat` 的 `Data/GameRules` 里，见 .hpp 顶上那段文件结构），所以风险、备份那一句
    //   警告、"要不要动文件"这个决定**全都是同一件事**。原来按"游戏里 `/gamerule` 能不能改"
    //   把这两拨分开摆是错的切法：**切法该按"动的是哪份文件"**，动 `level.dat` 的一律收进来。
    m_fileToggle = new QToolButton(m_body);
    m_fileToggle->setObjectName(fileToggleName());
    m_fileToggle->setText(fileToggleText());
    m_fileToggle->setCheckable(true);
    m_fileToggle->setChecked(false);
    m_fileToggle->setArrowType(Qt::RightArrow);
    m_fileToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_fileToggle->setToolTip(tr("世界名 / 允许作弊 / 极限模式 / 难度 / 游戏规则 —— 这些全都写在"
                                "存档文件里，改之前会先问你一句。"));
    body->addWidget(m_fileToggle, 0, Qt::AlignLeft);

    m_fileBody = new QWidget(m_body);
    m_fileBody->setObjectName(fileBodyName());
    auto *fileBody = new QVBoxLayout(m_fileBody);
    fileBody->setContentsMargins(0, 0, 0, 0);
    fileBody->setSpacing(8);
    // ★ 默认收起：这是"改存档文件"那几格，用户没点开之前一个都不该看见。
    m_fileBody->setVisible(false);

    // 世界名：★ 2026-09-29 起**不再是灰的**（用户原话「并允许修改值」）—— 盘上没有这一格
    // 时它照样能改，只是 tooltip 会说明"现在写的是默认值，改了才会写进去"（见
    // refreshFileHints()）。
    m_nameEdit = new QLineEdit(m_fileBody);
    m_nameEdit->setObjectName(nameEditName());
    fileBody->addLayout(makeOptionRow(
        m_fileBody, tr("世界名"), m_nameEdit,
        // ★ 星号在界面上是原样画出来的，重点一律用「」（2026-09-28）。
        tr("level.dat 里记着的那个名字。1.13 起游戏列表显示的是「文件夹名」，这一个"
           "只在旧存档、以及部分工具里看得见。")));

    m_allowCommands = new ToggleSwitch(m_fileBody);
    m_allowCommands->setObjectName(allowCommandsName());
    fileBody->addLayout(makeOptionRow(m_fileBody, tr("允许作弊"), m_allowCommands,
                                      tr("打开之后，这个世界里能用指令（单机也要开作弊）。")));

    m_hardcore = new ToggleSwitch(m_fileBody);
    m_hardcore->setObjectName(hardcoreName());
    fileBody->addLayout(makeOptionRow(m_fileBody, tr("极限模式"), m_hardcore,
                                      tr("极限模式：死一次这个世界就不能再玩了。")));

    m_difficulty = new QComboBox(m_fileBody);
    m_difficulty->setObjectName(difficultyName());
    for (int level = 0; level <= 3; ++level) {
        m_difficulty->addItem(difficultyOptionText(level), level);
    }
    // ★ 刚建出来的下拉框停在**第一项**（= 0 和平），而"还没摆过世界"的那一份 `WorldView()`
    //   里写的是 `difficulty = 2`（普通）。两个对不上 ⇒ `editedView()` 收上来的这一份和
    //   底下那句 `m_baselineSignature = signatureOf(WorldView())` 不一样 ⇒ **这一页还没摆过
    //   任何世界就被判成"改过"**。
    //
    //   后果不是"少问一句"，而是**反过来**：`InstanceSettingsDialog::reject()` 会拿
    //   `isDirty()` 挡下顶上那颗返回箭头，而它那一头接着的是"改过还没保存，丢不丢"那一问
    //   —— 用户什么都没改，却关不掉实例设置（见 .hpp 里 reject() 那一段）。所以一开始就
    //   把它摆到默认那一档上（`m_view.difficulty` 恒是 0~3 里的一个，`findData()` 必中）。
    m_difficulty->setCurrentIndex(m_difficulty->findData(m_view.difficulty));
    fileBody->addLayout(makeOptionRow(m_fileBody, tr("难度"), m_difficulty,
                                      tr("0 和平 / 1 简单 / 2 普通 / 3 困难。")));

    connect(m_fileToggle, &QAbstractButton::toggled, this, [this](bool open) {
        // ★ 先把样子摆出来（框开/收 + 箭头翻面），再发信号问那一句 —— 不先摆的话，用户按
        //   下去屏幕上什么都不动（警告还在弹），像按了个坏按钮。
        //   MainWindow 那一头有两种回话：答应 ⇒ `setFileEditingOpen(true)`（和现在一样）、
        //   反悔 ⇒ `setFileEditingOpen(false)`（把框收回去，那一下会把这个钮也拨回来）。
        m_fileBody->setVisible(open);
        m_fileToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        // ★ 正文收尾那根弹簧也要跟着翻（见 `m_bodyTail` 那一段：收着的时候不管它，上面两样
        //   会被均分到整块正文里）。
        updateBodyTail(open);
        // ★ 底下那行小字里有一句是**说那个列表怎么来的**（`rulesSourceHint()`）—— 框收着的
        //   时候那一列根本不在屏幕上，说它等于让用户找一样看不见的东西。所以这一句跟着
        //   框的开合走（`refreshNote()` 里判的是 `m_fileToggle->isChecked()`）。
        refreshNote();
        emit fileEditingRequested(open);
    });

    // ---------- 世界设置（游戏规则）----------
    //
    // ★ 2026-09-29：这一段**整个搬进了上面那个折叠框**（原来是 `m_body` / `body`）。
    //   理由见 `m_fileToggle` 那一段 ★：规则也写在 `level.dat` 里，和上面四格是**同一档
    //   风险、同一份文件、同一句警告**，所以摆在一起、一起收着。
    //   搬动只改了 parent 与 `addLayout/addWidget` 的接收者（`m_body`/`body` → `m_fileBody`/
    //   `fileBody`），控件本身、行代理、重建逻辑一个字没动。
    fileBody->addSpacing(6); // 另起一段：和上面那四格拉开一点
    auto *rulesRow = new QHBoxLayout;
    rulesRow->setContentsMargins(0, 0, 0, 0);
    rulesRow->setSpacing(8);
    auto *rulesLabel = new QLabel(rulesSectionText(), m_fileBody);
    // 小标题比上面那几行的标签长一点（「世界设置（游戏规则）」），左边那一栏放不下就让它
    // 自己撑开 —— 这一行是"另起一段"，不和上面那四格的标签对齐。
    rulesRow->addWidget(rulesLabel);
    m_ruleFilter = new QLineEdit(m_fileBody);
    m_ruleFilter->setObjectName(ruleFilterName());
    m_ruleFilter->setPlaceholderText(ruleFilterPlaceholder());
    m_ruleFilter->setClearButtonEnabled(true);
    // 改字之后**等手停下来**再摆（见 kRuleFilterDelayMs 那一段 ★）：`setWorld()` 里那一发
    // `clear()` 也会走到这儿，所以那边摆完内容要再把计时器停一次（不然会白重摆一遍）。
    m_ruleFilterTimer = new QTimer(this);
    m_ruleFilterTimer->setSingleShot(true);
    m_ruleFilterTimer->setInterval(kRuleFilterDelayMs);
    connect(m_ruleFilterTimer, &QTimer::timeout, this, [this] { rebuildRuleRows(); });
    connect(m_ruleFilter, &QLineEdit::textChanged, this, [this] {
        if (m_ruleFilterTimer != nullptr) {
            m_ruleFilterTimer->start();
        }
    });
    rulesRow->addWidget(m_ruleFilter, 1);
    fileBody->addLayout(rulesRow);

    m_ruleList = new QListWidget(m_fileBody);
    m_ruleList->setObjectName(ruleListName());
    // ★★ 挂的是**视口**不是列表自己：要认的就是"视口被滚动条挤窄了"那一发，而那一发只落在
    //   视口上。为什么必须接住它见 `eventFilter()` 那一段 ★★（行会被视口切掉半个）。
    m_ruleList->viewport()->installEventFilter(this);
    // 一行一个自己摆的控件 ⇒ 不用选中态、不要焦点框、不要自带滚动条之外的任何装饰。
    m_ruleList->setSelectionMode(QAbstractItemView::NoSelection);
    m_ruleList->setFocusPolicy(Qt::NoFocus);
    m_ruleList->setAlternatingRowColors(false);
    m_ruleList->setUniformItemSizes(true);
    m_ruleList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // ★ 选中 / 当前项 / 悬停一个都不画（理由见上面 RuleItemDelegate 那段 ★★ —— 光靠
    //   NoSelection 拦不住第 0 行被当成"当前项"染一道竖渐变）。
    m_ruleList->setItemDelegate(new RuleItemDelegate(m_ruleList));
    // ★ 撑头的那一份在**框里面**：`fileBody` 里是"四行固定高 + 搜索行 + 规则表(1)"，
    //   `body` 里是「说明 / 折叠钮 / 框(1)」。两级都把剩下的高度给表格，框收起来时这一串
    //   自然塌掉（`m_fileBody` 一隐藏，`body` 里就只剩说明和那颗钮）。
    fileBody->addWidget(m_ruleList, 1);

    body->addWidget(m_fileBody, 1);

    // ★★ 正文收尾那根弹簧（2026-09-29，用户原话「没有展开的时候排版太诡异了修一下」）。
    //
    //   框收着的时候正文里活着的只剩「那一行事实」和上面那颗折叠钮，而 `QVBoxLayout` 在
    //   没有 stretch 可用的时候会把多出来的高度**均分**给它们 —— 屏幕上就是"那行事实飘在
    //   正中间、那颗钮掉到最底下贴着说明"，看着像页坏了。这根弹簧收着时一个人吃掉全部多余
    //   高度（两样都贴顶），摊开时让给规则表（那一份 stretch 才是 1）。
    //
    //   `updateBodyTail()` 是唯一改它 stretch 的地方（两条路都要叫它）。
    m_bodyTail = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    m_bodyTailIndex = m_bodyLayout->count();
    m_bodyLayout->addSpacerItem(m_bodyTail);
    updateBodyTail(false);   // 默认就是收着的

    root->addWidget(m_body, 1);

    // ---------- 底下那行小字 ----------
    m_note = new QLabel(this);
    m_note->setObjectName(noteLabelName());
    m_note->setWordWrap(true);
    m_note->setForegroundRole(QPalette::PlaceholderText);
    root->addWidget(m_note);

    // ---------- 最底下那一排：备份一份 | 保存 ----------
    //
    // 这两颗是**整页**的两件动作（不是"这个世界"的三件事，所以不摆在上面那一排）：
    // 「备份一份」抄的是盘上那份 level.dat，「保存」写的是页面上改过的这几格。
    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->setSpacing(8);
    bottom->addStretch(1);
    bottom->addWidget(m_backup);
    bottom->addWidget(m_save);
    root->addLayout(bottom);

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
QString WorldSettingsPage::ruleDefaultMarkName() {
    return QStringLiteral("worldSettingsRuleDefaultMark");
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
QString WorldSettingsPage::renameButtonName() {
    return QStringLiteral("worldSettingsRenameButton");
}
QString WorldSettingsPage::deleteButtonName() {
    return QStringLiteral("worldSettingsDeleteButton");
}
QString WorldSettingsPage::fileToggleName() {
    return QStringLiteral("worldSettingsFileToggle");
}
QString WorldSettingsPage::fileBodyName() {
    return QStringLiteral("worldSettingsFileBody");
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
QString WorldSettingsPage::renameText() {
    return QStringLiteral("重命名…");
}
QString WorldSettingsPage::deleteText() {
    return QStringLiteral("删除世界…");
}
QString WorldSettingsPage::ruleFilterPlaceholder() {
    return QStringLiteral("搜索规则");
}
QString WorldSettingsPage::fileToggleText() {
    return QStringLiteral("修改存档文件");
}
QString WorldSettingsPage::rulesSectionText() {
    return QStringLiteral("世界设置（游戏规则）");
}
QString WorldSettingsPage::noMatchText() {
    return QStringLiteral("没有名字里有这几个字的规则。");
}
QString WorldSettingsPage::emptyRulesText() {
    return QStringLiteral("一条规则都没有（连启动器认识的那些也没有）—— 这不该发生。");
}
QString WorldSettingsPage::rulesSourceHint(bool hasRules) {
    // ★ 这一段是"这一列是怎么来的"的说明书：用户原话里的「把所有的都列出来」要有个交代
    //   —— 不然用户会以为**屏幕上每一条**都是他这个世界里本来就有的。
    if (hasRules) {
        return QStringLiteral("列表里是启动器认识的全部规则：存档里有的用存档里的值，"
                              "存档里还没有的那几条用游戏默认值（名字旁边写着「默认」）。"
                              "改过哪一条，才会把哪一条写进存档。");
    }
    return QStringLiteral("这个存档里还没有「游戏规则」那一格（工具做出来的、还没进过的档"
                          "常常是这样）—— 下面每一条摆的都是游戏的默认值，改过哪一条才会"
                          "把哪一条写进存档。");
}
QString WorldSettingsPage::defaultedHintText() {
    return QStringLiteral("这个存档里现在没有这一项 —— 屏幕上摆的是游戏的默认值，"
                          "改过才会写进去。");
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
    // 上一个世界那一发还没落地的"重摆"到此为止 —— 它要收的是**上一个世界**的行（`m_ruleRows`
    // 里还是那个世界的行号，见下面 `rebuildRuleRows(false)` 那一段 ★）。
    if (m_ruleFilterTimer != nullptr) {
        m_ruleFilterTimer->stop();
    }
    m_view = view;
    // ★ 「打开这一页时摆的是什么」那一份台账。它和 `m_view` **是两回事**：`m_view` 会被
    //   `harvestEdits()` 跟着用户改的值往前挪，而 `deltaFromOpened()` 要的是"刚打开那一刻"
    //   这个不会动的基准（见 .hpp 里 `Delta` 那一段）。
    m_opened = view;
    m_unreadable = false;
    m_unreadableReason.clear();
    m_savedAt.clear();
    m_backupAvailable = false;
    m_baselineSignature = signatureOf(view);

    m_ruleFilter->clear();

    m_title->setText(tr("世界设置：%1").arg(view.dirName));
    m_nameEdit->setText(view.levelName);
    m_allowCommands->setChecked(view.allowCommands);
    m_hardcore->setChecked(view.hardcore);
    {
        // 摆难度**不能发 currentIndexChanged**（现在没接槽，但摆内容时发信号是个坑：
        // 以后谁接上去就会在"刚打开"的时候被叫一次）。先挡上。
        const QSignalBlocker blocker(m_difficulty);
        const int index = m_difficulty->findData(view.difficulty);
        m_difficulty->setCurrentIndex(index >= 0 ? index : 0);
    }
    // ★ 2026-09-29：这四格**一律可动**（用户原话「并允许修改值」）。原来 `hasXxx = false`
    //   的时候它们是被 `setEnabled(false)` 灰掉的，现在改成只在 tooltip 上说实话 ——
    //   见 refreshFileHints()，以及那一句本身（`defaultedHintText()`）。
    refreshFileHints();
    // 每进一个世界，那个折叠框都**从收起开始**：它是"改文件"那一档，不该一进来就摊着。
    setFileEditingOpen(false);

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
    // 上面那句 `m_ruleFilter->clear()` 会发一次 textChanged ⇒ 把计时器带起来。这一趟已经
    // 照新世界摆过了，那一发再来一次是白摆。
    if (m_ruleFilterTimer != nullptr) {
        m_ruleFilterTimer->stop();
    }
    applyUnreadableState();
    refreshNote();
    refreshSaveEnabled();
}

void WorldSettingsPage::setUnreadable(const QString &dirName, const QString &reason) {
    // 同上：悬着的那一发重摆不该落到"读不出来的世界"上（它会拿 m_view.rules 那空的一份
    // 去重摆 —— 屏幕上没差别，但那个 m_ruleRows 的账对不上）。
    if (m_ruleFilterTimer != nullptr) {
        m_ruleFilterTimer->stop();
    }
    m_view = WorldView();
    m_opened = m_view;
    m_view.dirName = dirName;
    m_unreadable = true;
    m_unreadableReason = reason;
    m_savedAt.clear();
    m_backupAvailable = false;
    m_title->setText(tr("这个世界可能坏了：%1").arg(dirName));
    m_ruleList->clear();
    m_ruleRows.clear();
    // 这一档里那个折叠框**整个收起来**（连那颗钮一起藏，见 applyUnreadableState()）——
    // 读不出存档内容，"改文件"这件事就无从谈起。
    setFileEditingOpen(false);
    applyUnreadableState();
    refreshNote();
    refreshSaveEnabled();
}

void WorldSettingsPage::applyUnreadableState() {
    // 读不出来那一档：**整块正文藏起来**（它摆的那些值一个都不作数，连"改文件"那颗折叠钮
    // 也是正文的一部分），顶上只留「返回」「重命名」「打开文件夹」「删除世界」「备份一份」
    // —— 这几颗在这一档里正是最该做的事（改不了内容，但这个世界还在盘上，该能改名、能删）。
    m_body->setVisible(!m_unreadable);
    m_save->setVisible(!m_unreadable);
    m_backup->setVisible(true);
}

void WorldSettingsPage::refreshFileHints() {
    // 四格各自的 tooltip：「盘上有这一项」⇒ 说清它是什么；「盘上没有」⇒ 加上
    // `defaultedHintText()` 那句实话（屏幕上摆的是默认值，改了才会写进去）。
    //
    // ★ 和"世界名"那一格的小字（`makeOptionRow` 的 `tip`，挂在**标签**上）分工不同：
    //   标签上那句说的是"这一格是什么"，这里说的是"这一格现在盘上有没有"。
    const auto tip = [](const QString &what, bool inFile) {
        return inFile ? what : what + QLatin1Char('\n') + defaultedHintText();
    };
    const QString nameWhat =
        tr("level.dat 里记着的那个名字。1.13 起游戏列表显示的是「文件夹名」。");
    m_nameEdit->setToolTip(tip(nameWhat, m_view.hasLevelName));
    m_allowCommands->setToolTip(
        tip(tr("打开之后，这个世界里能用指令（单机也要开作弊）。"), m_view.hasAllowCommands));
    m_hardcore->setToolTip(
        tip(tr("极限模式：死一次这个世界就不能再玩了。"), m_view.hasHardcore));
    m_difficulty->setToolTip(
        tip(tr("0 和平 / 1 简单 / 2 普通 / 3 困难。"), m_view.hasDifficulty));
}

void WorldSettingsPage::setDirName(const QString &dirName) {
    // 只动标题上那个名字（`WorldFiles::renameWorld()` 只改目录名、不碰 `level.dat`，
    // 所以页面上别的值一个都不用重读 —— 见 .hpp 里那段 NOTE）。
    m_view.dirName = dirName;
    // 「读不出来」那一档的标题是另一种说法（那句"这个世界可能坏了"），也得跟着改。
    if (m_unreadable) {
        m_title->setText(tr("这个世界可能坏了：%1").arg(dirName));
    } else {
        m_title->setText(tr("世界设置：%1").arg(dirName));
    }
    // `signatureOf()` 里**没有** `dirName` ⇒ `isDirty()` 和基线都不动：用户在改名之前
    // 改过的那几格还是"改过还没存"。
}

void WorldSettingsPage::setFileEditingOpen(bool open) {
    // ★★ 那句 `QSignalBlocker` **不能删**（见 .hpp 里那段）：不挡的话这一句会顺着
    //    `toggled` 再发一次 `fileEditingRequested()` —— 用户点一下那个钮，警告弹两次，
    //    第二次是在他**刚答应完之后**弹的（屏幕上看着像"这按钮点不完"）。
    const QSignalBlocker blocker(m_fileToggle);
    m_fileToggle->setChecked(open);
    m_fileToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    m_fileBody->setVisible(open);
    // 正文收尾那根弹簧（见 `m_bodyTail` 那段 —— 这一条路和用户点那颗钮那一条路都要叫它）。
    updateBodyTail(open);
    // ★ 那行小字里有一句跟着框的开合走（`rulesSourceHint()`，理由见 `refreshNote()` 那段）。
    //   这一条路被 `QSignalBlocker` 挡着，所以**得自己叫一次** —— 不然主程序答完那句警告、
    //   把框摆开之后，底下那句"这一列是怎么来的"要等到用户自己再点一下那颗钮才说得出口。
    refreshNote();
}

void WorldSettingsPage::updateBodyTail(bool open) {
    // ★★ 改的是 stretch 不是"藏/露"（见 .hpp 里 `m_bodyTail` 那一段）：
    //
    //   * 框**收着**（open = false）⇒ 弹簧 stretch = 1。规则表那一项是空的（控件藏着的项
    //     在 `QVBoxLayout` 里会被跳过），所以这一份 stretch 就是全场唯一，多余高度全归它
    //     ⇒ 那行事实和那颗钮都贴顶（用户 2026-09-29 点名要修的就是这个）。
    //   * 框**摊开**（open = true）⇒ 弹簧 stretch = 0，让位给规则表（`addWidget(m_fileBody,
    //     1)` 那一份）。留着 1 的话两边会各分一半，规则表白矮一半。
    //
    // ★ `setStretch()` 是按下标说话的（`QBoxLayout` 那一层没有"给 spacer 设 stretch"的
    //   重载），下标是构造里刚记下来的那一个，这里不许另算一遍。
    m_bodyLayout->setStretch(m_bodyTailIndex, open ? 0 : 1);
}

bool WorldSettingsPage::fileEditingOpen() const {
    return m_fileToggle->isChecked();
}

QAbstractButton *WorldSettingsPage::fileToggle() const {
    return m_fileToggle;
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
    // ★ `deltaFromOpened()` 那条基准**也要往前挪**：刚写下去的就是页面上这一份，所以
    //   "现在盘上是什么"已经等于"屏幕上是什么"了。不挪的话，用户存完**再存一次**
    //   （或者存完接着改一条无关的规则）会把刚存过的那几格又写一遍 —— 不是错，但那一次
    //   写盘会顺着 `saveWorldSettings()` 再留一代备份，白白吃掉一格代数。
    m_opened = editedView();
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
        item->setText(emptyRulesText());
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

        // 「盘上还没有这一条」得让人看得出来（见 Rule::inFile 的 NOTE）：名字前面挂一枚
        // 「默认」小标记，名字和值那两格的 tooltip 上再说一遍。不标的话，用户会以为
        // "屏幕上摆的"就是"这个存档本来的样子"—— 而这一列现在是**全集**。
        //
        // ★ 2026-09-29 起这一串（英文规则名）**只出现在 tooltip 里** —— 行上那一格盖的是
        //   官方中文名（见下面 `setText()` 那儿）。英文名不能丢：它才是写进存档的那个键，
        //   也是用户在游戏里 `/gamerule` 要打的那个词。
        const QString ruleTip =
            rule.inFile ? rule.name
                        : rule.name + QLatin1Char('\n') + defaultedHintText();

        if (!rule.inFile) {
            auto *mark = new QLabel(tr("默认"), row);
            mark->setObjectName(ruleDefaultMarkName());
            mark->setToolTip(defaultedHintText());
            // 「次要信息」那一档颜色（和上面那行 facts 同一把尺子）。
            mark->setForegroundRole(QPalette::PlaceholderText);
            layout->addWidget(mark);
        }

        auto *name = new ElidedLabel(row);
        name->setObjectName(ruleNameLabelName());
        name->setElideMode(Qt::ElideRight);
        // ★ 用户 2026-09-29 原话：「游戏规则的英文拿一层对应的官方中文覆盖上去」。盖的这层
        //   是 `GameRuleNames`（官方 zh_cn.json 里那条）——**认不出来就原样回英文**，所以
        //   模组写的规则、比那张表还新的规则不会变成空白。
        name->setText(GameRuleNames::displayName(rule.name));
        name->setToolTip(ruleTip);
        layout->addWidget(name, 1);

        if (rule.kind == Kind::Bool) {
            auto *toggle = new ToggleSwitch(row);
            toggle->setObjectName(ruleToggleName());
            toggle->setToolTip(ruleTip);
            // 「true」以外的任何写法都不算开（大小写按盘上那份原样比，和
            // NbtTree::boolValue() 一条规矩）。
            toggle->setChecked(rule.value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            layout->addWidget(toggle);
        } else {
            auto *edit = new QLineEdit(row);
            edit->setObjectName(ruleEditorName());
            edit->setToolTip(ruleTip);
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
    // 只搜名字，但**两个名字都算**：屏幕上那一格盖的是官方中文（`keepInventory` 那一行
    // 写的是「死亡后保留物品栏」），用户记的可能就是屏幕上那个词；而英文名是游戏里
    // `/gamerule` 要打的那个词，也不能搜不到。
    //
    // 值那一列不参与 —— 拿 "true" 去搜会一次筛出几十条，那不是搜索是噪音。
    const QString display = GameRuleNames::displayName(rule.name);
    return rule.name.contains(needle, Qt::CaseInsensitive) ||
           display.contains(needle, Qt::CaseInsensitive);
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
    // ★ 2026-09-29：这四格**不再按 `hasXxx` 分岔**（原来"盘上没有这一项就把它灰掉、也不
    //   收它的值"）。现在控件一律可动，所以一律收 —— 盘上没有的那几格，屏幕上是默认值，
    //   用户没动的话这一份和 `setWorld()` 摆进去的那一份**逐字相同**，MainWindow 那边
    //   拿"打开时那份"一比就知道"这一格他没碰"，于是**一个字节都不写**（见
    //   `MainWindow::applyWorldViewTo()`）。"不发明"那条纪律因此没有丢，只是从"控件灰着"
    //   换成了"写盘前逐格比一遍"。
    view.levelName = m_nameEdit->text();
    view.allowCommands = m_allowCommands->isChecked();
    view.hardcore = m_hardcore->isChecked();
    if (m_difficulty->currentIndex() >= 0) {
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

WorldSettingsPage::Delta WorldSettingsPage::deltaFromOpened() const {
    Delta delta;
    if (m_unreadable) {
        // 这一档整块正文都藏着，一个格子都动不了 ⇒ 差集一定是空的。**不靠"下面那几比
        // 恰好相等"**：那一档 `m_opened` 和 `m_view` 都是空的、控件里也是空的，看着也相等，
        // 但那是在搭一个巧合。
        return delta;
    }
    const WorldView now = editedView();

    // 四格：逐项和"打开时那一份"比。**相等就是没动过** —— 盘上没有这一项时屏幕上摆的是
    // 默认值，用户没碰它，那一位和 `m_opened` 里那一份逐字相同，于是这里一个字节都不写
    // （"不发明"那条纪律的落点，见 .hpp 里 `Delta` 那一段）。
    if (now.levelName != m_opened.levelName) {
        delta.nameChanged = true;
        delta.nameValue = now.levelName;
    }
    if (now.allowCommands != m_opened.allowCommands) {
        delta.allowCommandsChanged = true;
        delta.allowCommandsValue = now.allowCommands;
    }
    if (now.hardcore != m_opened.hardcore) {
        delta.hardcoreChanged = true;
        delta.hardcoreValue = now.hardcore;
    }
    if (now.difficulty != m_opened.difficulty) {
        delta.difficultyChanged = true;
        delta.difficultyValue = now.difficulty;
    }

    // 规则：按**名字**在"打开时那一份"里找同一条来比（不按下标 —— `editedView()` 的规则
    // 顺序和 `m_opened` 一样，但那是 `setWorld()` 摆进去的那一份，靠下标就把这件事变成
    // 一条不成文的约定）。
    QHash<QString, QString> openedValues;
    for (const Rule &rule : m_opened.rules) {
        openedValues.insert(rule.name, rule.value);
    }
    for (const Rule &rule : now.rules) {
        const auto it = openedValues.constFind(rule.name);
        if (it == openedValues.constEnd() || it.value() != rule.value) {
            delta.rules.append(qMakePair(rule.name, rule.value));
        }
    }
    return delta;
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
    // ★ 2026-09-29：原来这里是那句「这个存档里还没有游戏规则，进游戏玩一次就会写进去」
    //   （`noAddHintText()`）—— 现在那一列是**全集**，所以这句换成了"这一列是怎么来的"
    //   的说明书（`rulesSourceHint()`，盘上有没有那一格各说一句）。
    // ★ 2026-09-29 第二版：那一列**搬进了折叠框**（规则改的也是 level.dat，见构造函数里
    //   `m_fileToggle` 那一段），所以这句只在**框开着**的时候说 —— 框收着的时候屏幕上没有
    //   那个列表，说它等于让用户去找一样看不见的东西。开合那道 `toggled` 会重跑一次这一段。
    if (m_fileToggle->isChecked()) {
        lines.append(rulesSourceHint(m_view.hasRules));
    }
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

bool WorldSettingsPage::eventFilter(QObject *watched, QEvent *event) {
    // ★★ 视口一变（滚动条露出来 / 收起来、页宽变了）就让视图**按这个新视口重摆一遍** ——
    //   理由见 .hpp 里那个 ★★（视图那份"每一行在哪、多宽"的缓存停在旧视口上，Qt 自己
    //   不会再回来重算，行尾那颗开关就会被视口切掉半个）。
    if (watched == m_ruleList->viewport() && event->type() == QEvent::Resize) {
        m_ruleList->doItemsLayout();
    }
    return QWidget::eventFilter(watched, event);
}

void WorldSettingsPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // ★ 焦点只能在这一刻补（切到这一格时 Qt 会把焦点给新露面的控件，而这一页最想让人
    //   动的就是那张表）。不补的话按 Tab 得从顶上「返回」一颗一颗挪下来。
    if (!m_unreadable) {
        m_ruleList->setFocus();
    }
}
