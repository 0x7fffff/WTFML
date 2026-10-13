/**
 * @file InlineMessage.cpp
 * @brief Implements the QMessageBox stand-ins (see InlineMessage.hpp for the whole picture).
 * @author 0x7fffff
 * @date 2026-09-25
 */

#include "InlineMessage.hpp"

#include <QCheckBox>
#include <QDebug>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>

#include "MainWindowUI.hpp"
#include "OverlayHost.hpp"
#include "Theme.hpp"

namespace {
/// `setList()` 那张列表最多露几行（再多就在列表内部滚）。见 setList() 里那段 ★。
constexpr int kListVisibleRows = 6;
}  // namespace

InlineMessageBox::InlineMessageBox(MessageKind kind, const QString &title, const QString &text,
                                   const QStringList &buttons, int defaultIndex,
                                   int destructiveIndex, QWidget *parent)
    : QDialog(parent), m_kind(kind) {
    setObjectName(QStringLiteral("inlineMessageBox"));
    // 它自己**从来不是窗口**（覆盖层摆上去之前会先把 Qt::Dialog 摘掉），但键盘事件得能到
    // 它身上 —— Esc = 拒绝、回车打默认按钮，这两条都是 QDialog 白给的，前提是焦点在它这儿。
    // 对"不是窗口"的 QDialog 来说焦点策略默认是 NoFocus，所以这一句不能省。
    setFocusPolicy(Qt::StrongFocus);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    m_titleLabel = new QLabel(title, this);
    m_titleLabel->setObjectName(titleLabelName());
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    if (kind == MessageKind::Critical) {
        // 只有真出错了（"删除游戏文件夹失败"这种）染红，其余三档一律正文色 ——
        // 红是稀缺资源，随手一句"没有选中实例"也是红的，红就不管用了。
        // 红**借用设置页那句"名字不能用"的红**（同一个出处），免得界面上有两种红。
        QPalette titlePalette = m_titleLabel->palette();
        titlePalette.setColor(QPalette::WindowText,
                              MainWindowUI::installNameErrorColor(titlePalette));
        m_titleLabel->setPalette(titlePalette);
    }
    layout->addWidget(m_titleLabel);

    m_textLabel = new QLabel(text, this);
    m_textLabel->setObjectName(textLabelName());
    m_textLabel->setWordWrap(true);
    // 正文允许选中复制：正文里常带路径 / 版本号 / 报错原文，手抄一条路径是不现实的
    // （"Java 版本对不上"那个框早就这么干了）。
    m_textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // 宽度**钉死**。它同时管两件事：所有提示一样宽（用户点名要的），以及"这段字换几行"
    // 是个确定的数 —— 卡片的高矮才算得准（指望 heightForWidth 一路穿过两层布局传上来，
    // 是会失手的那种）。
    m_textLabel->setFixedWidth(OverlayHost::promptContentWidth());
    layout->addWidget(m_textLabel);

    QWidget *row = new QWidget(this);
    row->setObjectName(buttonRowName());
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);
    rowLayout->addStretch(1);
    // 一颗都没给 = "就说一句"，补一颗「确定」。**默认按钮也是它**（回车 = 知道了我走了）。
    const QStringList labels = buttons.isEmpty() ? QStringList{tr("确定")} : buttons;
    for (int i = 0; i < labels.size(); ++i) {
        auto *button = new QPushButton(labels.at(i), row);
        button->setObjectName(buttonName(i));
        if (i == defaultIndex) {
            button->setDefault(true);
        }
        // 三档颜色按**位置**分，不按文字（"取消"这三个字认不得 —— 按钮表是调用方给的，
        // 上面那张世界编辑菜单里就有四颗都叫"做什么"的）：
        //   - `destructiveIndex` 那一颗 = 危险红（用户 2026-09-28 点名的"不可撤销"那一档）；
        //   - 默认那一颗 = 确认性（**不挂属性**就是主题色，见 Theme::ButtonKind）；
        //   - 其余 = 一般性（黑）。
        //
        // ★ 这条规矩的前提是"调用方标对了 `destructiveIndex`"—— 在此之前它**只用来记账**、
        //   一颗都没标过（见下面那条改动的说明）。真正的闸一直是 `defaultIndex`（危险那两颗
        //   一律不给默认：误按回车不该把事办了），颜色只是第二道。
        if (i == destructiveIndex) {
            Theme::markButton(button, Theme::ButtonKind::Danger);
        } else if (i != defaultIndex) {
            Theme::markButton(button, Theme::ButtonKind::Neutral);
        }
        connect(button, &QPushButton::clicked, this, [this, i] {
            m_clicked = i;
            accept();
        });
        rowLayout->addWidget(button);
    }
    layout->addWidget(row);
}

QLineEdit *InlineMessageBox::setLineEdit(const QString &initial) {
    if (m_lineEdit != nullptr) {
        m_lineEdit->setText(initial);  // 已经插过一行了：只换内容，不再插第二行。
        return m_lineEdit;
    }
    auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
    Q_ASSERT(layout != nullptr);  // ctor 里那个布局是本类的实现细节，不是可选的。
    m_lineEdit = new QLineEdit(initial, this);
    m_lineEdit->setObjectName(lineEditName());
    // 和正文一样**钉住宽度**：卡片的高矮才算得准（理由见 ctor 里那段）。
    m_lineEdit->setFixedWidth(OverlayHost::promptContentWidth());
    // 正文里常带路径，这一格也一样让人能选中复制（和正文那个标签同一个取法）。
    m_lineEdit->setContextMenuPolicy(Qt::DefaultContextMenu);
    // 插在**按钮行前面** —— ctor 里那个布局的顺序是「标题 / 正文 / 按钮行」，
    // 所以"最后一个"就是按钮行。（写成 `count() - 1` 而不是写死 2：将来正文上面再加
    // 一行什么，这一句不用跟着改。）
    layout->insertWidget(layout->count() - 1, m_lineEdit);
    // NOTE: 这里**不**叫 setFocus() —— 叫了也不管用（`setParent()` 之后会被清掉，
    //       见 showEvent() 那段）。真正定光标的是 `showEvent()` 里那一下。
    return m_lineEdit;
}

QString InlineMessageBox::lineEditText() const {
    return m_lineEdit != nullptr ? m_lineEdit->text() : QString();
}

QListWidget *InlineMessageBox::setList(const QStringList &items, int initialRow) {
    auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
    Q_ASSERT(layout != nullptr);  // 同 setLineEdit()：布局是本类的实现细节。
    if (m_list == nullptr) {
        m_list = new QListWidget(this);
        m_list->setObjectName(listName());
        // 和正文/输入框一样**钉住宽度**（理由见 ctor 里那段）。
        m_list->setFixedWidth(OverlayHost::promptContentWidth());
        // 不要横向滚动条：这一张列表里放的是实例名/文件夹名，长了让它自己截（和列表页
        // 那些行的做法一致），横着滚会让卡片多出一条从来没用的东西。
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // 插在**按钮行前面**（同 setLineEdit 那一句，写成 count()-1 不写死下标）。
        layout->insertWidget(layout->count() - 1, m_list);
    }
    m_list->clear();
    m_list->addItems(items);
    if (initialRow >= 0 && initialRow < m_list->count()) {
        m_list->setCurrentRow(initialRow);
    }
    // 高度**不跟条数长**：最多露 kListVisibleRows 行，再多就在列表内部滚。
    // NOTE: 行高问 `sizeHintForRow(0)` —— 它才是"这个列表画一行要多少"的出处（字体、
    //       内边距、将来可能有的行装饰都在里面）；拿 fontMetrics().height() 自己算，
    //       第一行就会被切掉一点。取不到（一行都没有）时退回字体那一档。
    const int rowHeight = m_list->count() > 0 ? m_list->sizeHintForRow(0)
                                             : fontMetrics().height() + 8;
    const int rows = qBound(1, items.size(), kListVisibleRows);
    m_list->setFixedHeight(rows * rowHeight + 2 * m_list->frameWidth() + 2);
    return m_list;
}

int InlineMessageBox::listRow() const {
    return m_list != nullptr ? m_list->currentRow() : -1;
}

int InlineMessageBox::listVisibleRows() {
    return kListVisibleRows;
}

void InlineMessageBox::setNote(const QString &text) {
    if (text.isEmpty()) {
        // 空串 = **不补**。已经补过一行的话把它撤掉：否则"这次不带附注"会留着一个空标签，
        // 卡片底下多出一段白白的高度。
        if (m_noteLabel != nullptr) {
            delete m_noteLabel;
            m_noteLabel = nullptr;
        }
        return;
    }
    if (m_noteLabel == nullptr) {
        auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
        Q_ASSERT(layout != nullptr);  // 同 setLineEdit()：布局是本类的实现细节。
        m_noteLabel = new QLabel(this);
        m_noteLabel->setObjectName(noteLabelName());
        m_noteLabel->setWordWrap(true);
        // 和正文一样钉住宽度：卡片的高矮才算得准（理由见 ctor 里那段）。
        m_noteLabel->setFixedWidth(OverlayHost::promptContentWidth());
        m_noteLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

        // 小一号 + 浅一档。字号在**这份调色板自己的字体**上算（不是写死点数）：
        // 系统字号调大调小、或者以后换主题，这一行都跟着正文一起动，不会变成"正文 11pt
        // 而附注还是 10pt"那种歪掉的比例。
        QFont noteFont = m_noteLabel->font();
        if (noteFont.pointSizeF() > 0) {
            noteFont.setPointSizeF(qMax(1.0, noteFont.pointSizeF() - 1.0));
        } else if (noteFont.pixelSize() > 0) {
            noteFont.setPixelSize(qMax(1, noteFont.pixelSize() - 1));
        }
        m_noteLabel->setFont(noteFont);
        // 浅一档的颜色走 QPalette::PlaceholderText —— 全仓"比正文浅一档的次要操作"就是它
        // （版本树那个取消小叉、导航按钮那几颗没选中的，用的都是同一个）。
        // NOTE: 这里要**先 setFont 再取调色板**，理由和别处那个坑一样：调色板是从字体上算的。
        QPalette notePalette = m_noteLabel->palette();
        notePalette.setColor(QPalette::WindowText, notePalette.color(QPalette::PlaceholderText));
        m_noteLabel->setPalette(notePalette);

        // 插在**按钮行前面**：布局顺序是「标题 / 正文 / 按钮行」，所以"最后一个"就是按钮行。
        // 写成 `count() - 1` 而不是写死下标，和 setLineEdit() 同一个理由（将来上面再加一行，
        // 这两处都不用跟着改）。
        layout->insertWidget(layout->count() - 1, m_noteLabel);
    }
    m_noteLabel->setText(text);
}

QString InlineMessageBox::noteText() const {
    return m_noteLabel != nullptr ? m_noteLabel->text() : QString();
}

QPushButton *InlineMessageBox::buttonAt(int index) const {
    if (index < 0) {
        return nullptr;
    }
    if (auto *row = findChild<QWidget *>(buttonRowName())) {
        return row->findChild<QPushButton *>(buttonName(index));
    }
    return nullptr;
}

QCheckBox *InlineMessageBox::setCheckBox(const QString &text, int acceptButtonIndex) {
    if (m_checkBox == nullptr) {
        auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
        Q_ASSERT(layout != nullptr);  // 同 setLineEdit()：布局是本类的实现细节。
        m_checkBox = new QCheckBox(text, this);
        m_checkBox->setObjectName(checkBoxName());
        // **一开始不勾**（我们不替任何人预勾）。这一句是多余的，但写出来是为了让"谁把它
        // 改成 setChecked(true) 会让整个承诺作废"这件事在代码里看得见。
        m_checkBox->setChecked(false);
        // 插在**按钮行前面**（写成 count()-1 不写死下标，同 setLineEdit 那一句）。
        layout->insertWidget(layout->count() - 1, m_checkBox);

        if (QPushButton *accept = buttonAt(acceptButtonIndex)) {
            // ★ 这是这一格真正的用途（见头文件）：没勾之前那一颗是灰的。
            //   绑法走 `toggled`，而且**当场按现在的状态摆一次** —— 只接信号不摆初值的话，
            //   那个按钮会以"能按"的样子出现，直到用户手一抖勾一下再取消才变灰。
            accept->setEnabled(m_checkBox->isChecked());
            connect(m_checkBox, &QCheckBox::toggled, accept, &QPushButton::setEnabled);
        }
    } else {
        m_checkBox->setText(text);
    }
    return m_checkBox;
}

bool InlineMessageBox::checkBoxChecked() const {
    return m_checkBox != nullptr && m_checkBox->isChecked();
}

QLabel *InlineMessageBox::setLink(const QString &text, const QString &url) {
    if (m_linkLabel == nullptr) {
        auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
        Q_ASSERT(layout != nullptr);  // 同 setLineEdit()：布局是本类的实现细节。
        m_linkLabel = new QLabel(this);
        m_linkLabel->setObjectName(linkName());
        m_linkLabel->setWordWrap(true);
        // 和正文一样钉住宽度：卡片的高矮才算得准（理由见 ctor 里那段）。
        m_linkLabel->setFixedWidth(OverlayHost::promptContentWidth());
        // ★ `false` —— 见头文件那段：点了只发链接激活那条信号，开浏览器是调用方的事
        //   （那一层要写日志、开不了还要弹一句）。改成 true 会绕过这两个线索。
        m_linkLabel->setOpenExternalLinks(false);
        // 光靠鼠标悬停变手型还不够：这一行得能被键盘 Tab 到、能用回车激活（Tab 到之后按
        // 回车，QLabel 自己会把 linkActivated 发出来）。所以是 TextBrowserInteraction，
        // 不是只给鼠标用的 TextSelectableByMouse。
        m_linkLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
        m_linkLabel->setFocusPolicy(Qt::StrongFocus);
        m_linkLabel->setTextFormat(Qt::RichText);
        connect(m_linkLabel, &QLabel::linkActivated, this,
                [this](const QString &url) { emit linkActivated(url); });
        // 插在**按钮行前面**（写成 count()-1 不写死下标，同 setLineEdit 那一句）。
        layout->insertWidget(layout->count() - 1, m_linkLabel);
    }
    m_linkUrl = url;
    // ★ 用 `toHtmlEscaped()` 把显示的那几个字转义掉 —— 它是**内容**，不是标记。不转的话
    //   带 `&` 或 `<` 的一句话会被 Qt 当成富文本吃掉一段（比如"允许与<服务器>"那种）。
    m_linkLabel->setText(
        QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), text.toHtmlEscaped()));
    return m_linkLabel;
}

QString InlineMessageBox::linkUrl() const {
    return m_linkLabel != nullptr ? m_linkUrl : QString();
}

void InlineMessageBox::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if (m_lineEdit == nullptr) {
        return;
    }
    // 理由见头文件里那一段（setParent 会清焦点、QDialog 会把焦点交给默认按钮）。
    m_lineEdit->setFocus(Qt::OtherFocusReason);
    // 预填的内容整段选中 —— 老那一档（QInputDialog::getText）就是这么干的：用户想整段换掉
    // 时直接打字就行，不用先全选一遍。改一两个字的人点一下/按方向键也能取消选中。
    m_lineEdit->selectAll();
}

namespace InlineMessage {

int ask(OverlayHost *host, MessageKind kind, const QString &title, const QString &text,
        const QStringList &buttons, int defaultIndex, int destructiveIndex,
        bool dismissOnOutsideClick, const QString &note) {
    if (host == nullptr) {
        // 没有覆盖层可摆（构造中途、或者验证工程单独测某个控件）。**不许静默吞掉** ——
        // "该说的话没说"是最难查的那一类故障。写一行日志，调用方照旧拿到"什么都没选"。
        qWarning("InlineMessage: 没有 OverlayHost，这条提示没有显示（%s）", qUtf8Printable(title));
        return -1;
    }
    if (host->promptOpen()) {
        // 提示叠提示不做（见 OverlayHost::openPrompt 里那段）。这一条比上面那条更少见，
        // 但一样**不许静默吞掉**：调用方拿到的是"什么都没选"，而那和"用户按了 Esc"分不开 ——
        // 所以至少要在日志里留下痕迹，不然"那条下载失败的消息去哪了"没法查。
        qWarning("InlineMessage: 已经有一条提示开着，这一条没有显示（%s）",
                 qUtf8Printable(title));
        return -1;
    }
    InlineMessageBox box(kind, title, text, buttons, defaultIndex, destructiveIndex);
    // 那行小字**在 execPrompt 之前**补上：卡片摆上去之后几何就定了，之后再插一行会让
    // 它开在原来的高度上、把按钮行挤出边框。
    box.setNote(note);
    host->execPrompt(&box, dismissOnOutsideClick);
    return box.clickedIndex();
}

void information(OverlayHost *host, const QString &title, const QString &text) {
    // 纯报信：点框外面 / 按 Esc 都算"知道了"。
    ask(host, MessageKind::Information, title, text, QStringList(), 0, -1, true);
}

void warning(OverlayHost *host, const QString &title, const QString &text) {
    ask(host, MessageKind::Warning, title, text, QStringList(), 0, -1, true);
}

void critical(OverlayHost *host, const QString &title, const QString &text) {
    ask(host, MessageKind::Critical, title, text, QStringList(), 0, -1, true);
}

bool question(OverlayHost *host, const QString &title, const QString &text, const QString &yesText,
              const QString &noText, bool defaultYes, bool destructive) {
    const QStringList buttons{yesText, noText};
    const int index = ask(host, MessageKind::Question, title, text, buttons, defaultYes ? 0 : 1,
                          destructive ? 0 : -1, false);
    return index == 0;
}

QString getText(OverlayHost *host, const QString &title, const QString &label,
                const QString &initial, bool *ok) {
    if (ok != nullptr) {
        *ok = false;
    }
    if (host == nullptr) {
        // 和 ask() 一样**不许静默吞掉**：调用方会拿到空串 + ok=false（= "用户取消了"），
        // 于是"那个名字改没改"这种事在界面上就是"什么也没发生" —— 只有日志能说清为什么。
        qWarning("InlineMessage: 没有 OverlayHost，这个「问一句名字」没有显示（%s）",
                 qUtf8Printable(title));
        return QString();
    }
    if (host->promptOpen()) {
        // 提示叠提示不做（见 OverlayHost::openPrompt 里那段）。理由同 ask()。
        qWarning("InlineMessage: 已经有一条提示开着，这个「问一句名字」没有显示（%s）",
                 qUtf8Printable(title));
        return QString();
    }
    // 按钮表就是 `{确定, 取消}`、默认是**确定**（回车 = 认下这个名字，和 QInputDialog
    // 一样：那一档的默认按钮也是 Ok）。`破坏性`那一格不给 —— 没有危险动作。
    // NOTE: 这里不能用光秃秃的 `tr(...)` —— 这是一个自由函数，没有 QObject 那个 tr。
    //       走 QObject::tr 拿到的是同一个翻译上下文（"QObject"），和 InlineMessageBox
    //       自己那颗默认的「确定」不是同一个上下文；本仓没有 .ts 文件，所以两处现在都是
    //       原字面量，但**要改名就两处一起改**（这一句就是给那种时候留的路标）。
    InlineMessageBox box(MessageKind::Question, title, label,
                         QStringList{QObject::tr("确定"), QObject::tr("取消")}, 0, -1);
    box.setLineEdit(initial);
    host->execPrompt(&box, false);
    const bool accepted = box.clickedIndex() == 0;
    if (ok != nullptr) {
        *ok = accepted;
    }
    return box.lineEditText();
}

bool pickFromList(OverlayHost *host, const QString &title, const QString &text,
                  const QStringList &items, int initialRow, int *rowOut) {
    if (rowOut != nullptr) {
        *rowOut = -1;
    }
    if (items.isEmpty()) {
        // 见 .hpp：一行都没有的列表框长得和"有东西可挑"一模一样，**不弹**。
        // 这一路也写一行日志 —— 调用方本该自己先判空，走到这儿说明那一处漏了。
        qWarning("InlineMessage: 清单是空的，这个「挑一个」没有显示（%s）", qUtf8Printable(title));
        return false;
    }
    if (host == nullptr) {
        qWarning("InlineMessage: 没有 OverlayHost，这个「挑一个」没有显示（%s）",
                 qUtf8Printable(title));
        return false;
    }
    if (host->promptOpen()) {
        qWarning("InlineMessage: 已经有一条提示开着，这个「挑一个」没有显示（%s）",
                 qUtf8Printable(title));
        return false;
    }
    // 按钮表和 getText() 逐字一样（`{确定, 取消}`、默认确定）—— 那一档也是
    // "认下这个选择"。破坏性那一格不给：挑一个目标不是危险动作。
    InlineMessageBox box(MessageKind::Question, title, text,
                         QStringList{QObject::tr("确定"), QObject::tr("取消")}, 0, -1);
    box.setList(items, initialRow);
    host->execPrompt(&box, false);
    if (box.clickedIndex() != 0) {
        return false;
    }
    const int row = box.listRow();
    if (row < 0) {
        // 点了确定但一行都没选中（用户可以 Ctrl+点一下把选中项取消掉）。这**不算选中了
        // 第 0 行** —— 替用户挑一个，比让他再选一次糟得多。
        return false;
    }
    if (rowOut != nullptr) {
        *rowOut = row;
    }
    return true;
}

}  // namespace InlineMessage
