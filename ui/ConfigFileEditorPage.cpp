/**
 * @file ConfigFileEditorPage.cpp
 * @brief ConfigFileEditorPage 的实现。
 * @author 0x7fffff
 * @date 2026-09-27
 * @note 见 .hpp 那一段（为什么是文本编辑器、为什么它是 QWidget 不是 QDialog、
 *       为什么这一层自己算要写的字节）。这里只补两处实现上的说明：行号槽那一套
 *       （本文件里唯一一处自己画的控件），以及 Esc 那一颗得在这一层接住的原因。
 */
#include "ConfigFileEditorPage.hpp"

#include <QDir>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QTime>
#include <QVBoxLayout>

#include "ElidedLabel.hpp"

namespace {

/// 编辑框至少这么高。这一页是嵌在「实例设置」那层滚动区里的（见 .hpp），
/// 而 `QPlainTextEdit` 自己那个 sizeHint 很小 —— 不顶一格的话，用户看到的是一条
/// 三行高的缝，"编辑一个文件"这件事就没地方下手了。
constexpr int kEditorMinHeight = 280;
/// 行号那一列数字右边留的空白（数字和正文之间那一条缝）。
constexpr int kGutterRightPad = 10;
/// 行号槽至少这么宽（行数只有个位数的时候也不至于贴着边）。
constexpr int kGutterMinWidth = 30;

class CodeEditor;

/// 行号槽那一格。**是一个真控件，不是画在编辑框上的** —— 它得自己占掉编辑框左侧那一条
/// 视口宽度（`setViewportMargins`），正文才会从它右边开始、滚到哪儿都不重叠。
class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor *editor);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    CodeEditor *m_editor = nullptr;
};

/// 带行号的纯文本编辑框。
///
/// NOTE: 用 `QPlainTextEdit` 而不是 `QTextEdit` —— 前者就是为**纯文本**设计的（没有富文本
///       那一套文档结构），长行和大文件都稳；而这一页要的正好是纯文本。
/// NOTE: **不换行**（`NoWrap`）。这一条不只是"看着像编辑器"，它还是行号槽成立的前提：
///       一个块正好占一个视觉行，所以第 n 个块的行号就画在第 n 行上。一换行，一个块占好几
///       个视觉行，行号就得跟着算折行 —— 那是另一件事，也更容易画错。
class CodeEditor : public QPlainTextEdit {
public:
    explicit CodeEditor(QWidget *parent = nullptr) : QPlainTextEdit(parent) {
        m_gutter = new LineNumberArea(this);
        setLineWrapMode(QPlainTextEdit::NoWrap);
        // 等宽字体：配置文件是缩进敏感的东西（TOML / JSON），比例字体下对齐全乱。
        setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        // 制表符按 **4 个空格宽**算。NeoForge 生成的那批 `.toml` 就是拿制表符缩进的
        // （`carryon-common.toml` 里 `[settings]` 底下那一层全是 `\t`），照 Qt 默认那个
        // 8 格宽画出来，同一份文件的缩进会比在别处看着深一倍。
        setTabStopDistance(4 * fontMetrics().horizontalAdvance(QLatin1Char(' ')));
        // ★ 这一页是**编辑器**：Tab 得是缩进，不是"焦点跳到下一个控件"。QPlainTextEdit
        //   默认就是这样（tabChangesFocus 默认 false）；写出来是因为这一条哪天被改掉，
        //   症状是"按 Tab 焦点跑了"，而用户只是想缩进一格。
        setTabChangesFocus(false);

        connect(this, &QPlainTextEdit::blockCountChanged, this,
                [this](int count) { updateLineNumberAreaWidth(count); });
        // 滚了、改了、视口变了：行号槽整个重画一遍。行号是"贴"在行上的，行一动它就得跟着动
        // （这也是 Qt 自带那份 line numbers 例子的做法）。
        connect(this, &QPlainTextEdit::updateRequest, this,
                [this](const QRect &rect, int dy) { updateLineNumberArea(rect, dy); });
        updateLineNumberAreaWidth(blockCount());
    }

    /// 行号槽要多宽：数字的位数决定。**`blockCount()` 一变大它就变宽**，而
    /// `updateLineNumberAreaWidth()` 会把这个宽度让给槽（`setViewportMargins`）。
    int gutterWidth() const {
        int digits = 1;
        int max = qMax(1, blockCount());
        while (max >= 10) {
            max /= 10;
            ++digits;
        }
        const int digitWidth = fontMetrics().horizontalAdvance(QLatin1Char('9'));
        return kGutterRightPad + qMax(kGutterMinWidth, digitWidth * digits + digitWidth);
    }

    void paintGutter(QPaintEvent *event) {
        QPainter painter(m_gutter);
        // 底色取 `AlternateBase`：那是调色板里"和正文底色差一档"的那一格，深浅两个主题下
        // 都成立（和 TitleBar 里 `Window.darker(105)` 是同一个意思，但这一格调色板里
        // 本来就有）。
        painter.fillRect(event->rect(), palette().color(QPalette::AlternateBase));
        painter.setPen(palette().color(QPalette::PlaceholderText));

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());
        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                painter.drawText(0, top, m_gutter->width() - kGutterRightPad,
                                 fontMetrics().height(), Qt::AlignRight,
                                 QString::number(blockNumber + 1));
            }
            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QPlainTextEdit::resizeEvent(event);
        const QRect cr = contentsRect();
        m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
    }

private:
    void updateLineNumberAreaWidth(int /*newBlockCount*/) {
        // 正文左边让出这么宽 —— `setViewportMargins` 说的就是"这一块是留给别人的"，
        // 于是正文从槽右边开始，滚到哪儿都不重叠。
        setViewportMargins(gutterWidth(), 0, 0, 0);
    }

    void updateLineNumberArea(const QRect &rect, int dy) {
        if (dy != 0) {
            m_gutter->scroll(0, dy);
        } else {
            m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
        }
        if (rect.contains(viewport()->rect())) {
            updateLineNumberAreaWidth(blockCount());
        }
    }

    LineNumberArea *m_gutter = nullptr;
};

LineNumberArea::LineNumberArea(CodeEditor *editor) : QWidget(editor), m_editor(editor) {}

QSize LineNumberArea::sizeHint() const { return QSize(m_editor->gutterWidth(), 0); }

void LineNumberArea::paintEvent(QPaintEvent *event) { m_editor->paintGutter(event); }

/// 比正文浅一档的那个颜色。**全仓唯一去处是 `QPalette::PlaceholderText`**（和
/// InstanceFilesPage.cpp 里那个同名的小工具同一个角色、同一个出处）。
void makeSecondary(QLabel *label) {
    if (label == nullptr) {
        return;
    }
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);
}

}  // namespace

ConfigFileEditorPage::ConfigFileEditorPage(QWidget *parent) : QWidget(parent) {
    setObjectName(pageName());
    // ★ 这一页**自己要拿到焦点**（用户进来就是要敲键盘改字），而它是嵌在两格子里被切出来
    //   的 —— 见 .hpp 里 showEvent() 那一 NOTE。
    setFocusPolicy(Qt::StrongFocus);

    auto *outer = new QVBoxLayout(this);
    // 边上不留白：这一块是**页里面的一格**（「配置文件」那一页的第二个格子），四周的留白
    // 由外面那一页给。留了的话，切过来切过去会看见内容整体缩一下 —— 而两格讲的是同一张
    // 列表的两种看法，位置该稳住。
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(10);

    // ---------- 上面一行：返回 + 这是哪个文件 + 打开文件夹 ----------
    auto *topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(8);

    // ★ 这一颗是必须的：覆盖层那颗返回箭头顶上顶着的是**整个「实例设置」**（点一下这一趟
    //   就没了），而用户在这儿的下一步多半是"看另一个文件"。所以这一页自己带一颗。
    m_back = new QPushButton(backText(), this);
    m_back->setObjectName(backButtonName());
    connect(m_back, &QPushButton::clicked, this, [this] { emit backRequested(); });
    topRow->addWidget(m_back);

    m_pathLabel = new ElidedLabel(this);
    m_pathLabel->setObjectName(pathLabelName());
    // 从**中间**截：这一串两头都有用 —— 前头是哪个实例、尾巴上才是文件名。
    //
    // NOTE: 这一格的真身份是 `ElidedLabel`，而 .hpp 里成员声明的是 `QLabel`（对外只要
    //       setText / text 那两下，和 InstanceFilesPage::pathLabel() 同一条规矩：别把
    //       ElidedLabel.hpp 拖进每一个用到这一页的地方）。所以在这儿转一次。
    static_cast<ElidedLabel *>(m_pathLabel)->setElideMode(Qt::ElideMiddle);
    makeSecondary(m_pathLabel);
    topRow->addWidget(m_pathLabel, 1);

    m_openFolder = new QPushButton(openFolderText(), this);
    m_openFolder->setObjectName(openFolderButtonName());
    // 这一层不自己开：开目录是 QDesktopServices 的事，而失败还要弹一句 —— 那得走覆盖层
    // （见文件头那段"这一层不碰盘"）。
    connect(m_openFolder, &QPushButton::clicked, this,
            [this] { emit openFolderRequested(m_path); });
    topRow->addWidget(m_openFolder);
    outer->addLayout(topRow);

    // ---------- 中间：编辑框 ----------
    m_editor = new CodeEditor(this);
    m_editor->setObjectName(editorName());
    m_editor->setPlaceholderText(tr("（这个文件是空的）"));
    m_editor->setMinimumHeight(kEditorMinHeight);
    outer->addWidget(m_editor, 1);

    // ---------- 底下：一句话 + 保存 ----------
    auto *bottomRow = new QHBoxLayout;
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->setSpacing(8);

    m_note = new QLabel(this);
    m_note->setObjectName(noteLabelName());
    m_note->setWordWrap(true);
    makeSecondary(m_note);
    bottomRow->addWidget(m_note, 1);

    m_save = new QPushButton(saveText(), this);
    m_save->setObjectName(saveButtonName());
    connect(m_save, &QPushButton::clicked, this, [this] {
        if (!m_hasFile) {
            return;
        }
        // 要写哪些字节**在这儿算**（行尾、BOM 都还完了，见 ConfigFileText::encode）——
        // 接的人只负责把它写到 m_path 上，不用再拼第二遍。
        emit saveRequested(m_path, ConfigFileText::encode(m_loaded, text()));
    });
    bottomRow->addWidget(m_save);
    outer->addLayout(bottomRow);

    // "还没摆过文件"那一档：编辑框只读、保存不画（真摆文件时 setFile 会把这两样按那一份
    // 文件重算一遍）。
    m_editor->setReadOnly(true);
    m_save->setVisible(false);
    refreshNote();
}

void ConfigFileEditorPage::setFile(const QString &path, const ConfigFileText::Loaded &loaded) {
    m_path = path;
    m_loaded = loaded;
    m_hasFile = true;

    m_pathLabel->setText(QDir::toNativeSeparators(path));
    m_pathLabel->setToolTip(QDir::toNativeSeparators(path));

    const bool editable = loaded.editable();
    m_editor->setReadOnly(!editable);
    // 摆正文：`setPlainText()` 会把"改过没有"重置回"没改"（Qt 的文档模型就是这么做的，
    // 2026-09-27 实测过）—— 不过 isDirty() 压根不看那一位，它是拿 m_loaded.text 现比的，
    // 所以这一句与它无关，写在这儿只是因为它是刚才量过的那件事。
    m_editor->setPlainText(loaded.text);
    // 从**顶上**开始（不留在上一次翻到的地方），并把光标摆在开头。
    m_editor->moveCursor(QTextCursor::Start);
    m_editor->ensureCursorVisible();

    // 只读时「保存」**不画**（不是画成灰的：灰按钮说的是"还没资格点"，而这里的问题是
    // "点了也没有意义"）。可编辑时才摆出来。
    m_save->setVisible(editable);
    m_save->setEnabled(true);
    refreshNote();
}

QString ConfigFileEditorPage::text() const {
    return m_editor == nullptr ? QString() : m_editor->toPlainText();
}

bool ConfigFileEditorPage::isDirty() const { return m_hasFile && text() != m_loaded.text; }

bool ConfigFileEditorPage::isEditable() const { return m_hasFile && m_loaded.editable(); }

QString ConfigFileEditorPage::path() const { return m_path; }

QPlainTextEdit *ConfigFileEditorPage::editor() const { return m_editor; }

int ConfigFileEditorPage::gutterWidth() const {
    // 行号槽是 CodeEditor 自己的事（见上面那个类）。`m_editor` 是它的基类指针，这一页
    // 只知道"它是个 QPlainTextEdit"（那样 .hpp 不必把 CodeEditor 拖进去），所以这儿
    // 按基类指针往下转一次。**这个向下转型一定成立**：构造函数里 new 的就是它。
    const auto *editor = static_cast<const CodeEditor *>(m_editor);
    return editor == nullptr ? 0 : editor->gutterWidth();
}

void ConfigFileEditorPage::markSaved() {
    if (!m_hasFile) {
        return;
    }
    // ★ 存过之后"读过那一份"就得换成**刚写下去的那一份**：`isDirty()` 是拿它比的，
    //   不换的话用户存完再点「返回」，还会被问一句"改了没保存"——而它明明刚存过。
    //
    // 换的东西就是把刚才那份字节解回来（`saveRequested` 里算的是同一份：同样的
    // `m_loaded` + 同样的正文，`encode()` 是纯函数），所以 `raw` 是盘上真实的那一份，
    // `text` 也逐字等于框里的正文 ⇒ `isDirty()` 归零。
    m_loaded = ConfigFileText::decode(ConfigFileText::encode(m_loaded, text()));
    m_note->setText(savedPrefix() + QTime::currentTime().toString(QStringLiteral("HH:mm:ss")) +
                    QStringLiteral("）"));
}

void ConfigFileEditorPage::refreshNote() {
    makeSecondary(m_note);
    if (!m_hasFile) {
        m_note->setText(QString());
        return;
    }
    if (!m_loaded.utf8Ok) {
        m_note->setText(readOnlyPrefix() +
                        tr("这个文件不是 UTF-8 编码，改它会把里面的字弄坏，所以这里只能看。"));
        return;
    }
    if (m_loaded.tooBig) {
        m_note->setText(readOnlyPrefix() +
                        tr("这个文件太大了（%1 MB 是这里给的上限），只给看不给改。")
                            .arg(ConfigFileText::editableByteLimit() / (1024 * 1024)));
        return;
    }
    m_note->setText(hintText());
}

void ConfigFileEditorPage::discardChanges() {
    if (!m_hasFile) {
        return;
    }
    // 退回读进来那一份 ⇒ isDirty() 变 false ⇒ 调用方接着切回列表。**改过的东西不许悄悄
    // 没掉**（见 .hpp）：那一问由 MainWindow 弹，用户答"不保存"才走到这儿。
    m_editor->setPlainText(m_loaded.text);
    refreshNote();
}

void ConfigFileEditorPage::keyPressEvent(QKeyEvent *event) {
    // ★ Esc 得在这一层接住（见 .hpp）。它从编辑框冒上来走到这儿，说明编辑框自己不认这个
    //   键 —— 那它就只该有"退回文件列表"这一个意思。
    if (event->key() == Qt::Key_Escape) {
        emit backRequested();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ConfigFileEditorPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // ★ 焦点只能在这一刻补（见 .hpp 那一 NOTE）：这一块是被堆叠切出来的，露脸的那一下
    //   焦点归了新露面的控件（或者还在刚才那张列表上）。**不补这一下，用户进来打不了
    //   字** —— 而"改这个文件"是这一页唯一的主要功能。
    if (m_editor != nullptr && isEditable()) {
        m_editor->setFocus(Qt::OtherFocusReason);
    }
}

// ---------- 那几行字、那几个名字的唯一出处 ----------

QString ConfigFileEditorPage::pageName() { return QStringLiteral("configFileEditorPage"); }
QString ConfigFileEditorPage::editorName() { return QStringLiteral("configFileEditor"); }
QString ConfigFileEditorPage::pathLabelName() { return QStringLiteral("configFilePath"); }
QString ConfigFileEditorPage::noteLabelName() { return QStringLiteral("configFileNote"); }
QString ConfigFileEditorPage::saveButtonName() { return QStringLiteral("configFileSaveButton"); }
QString ConfigFileEditorPage::openFolderButtonName() {
    return QStringLiteral("configFileOpenFolderButton");
}
QString ConfigFileEditorPage::backButtonName() { return QStringLiteral("configFileBackButton"); }

QString ConfigFileEditorPage::backText() { return QStringLiteral("返回"); }
QString ConfigFileEditorPage::saveText() { return QStringLiteral("保存"); }
QString ConfigFileEditorPage::openFolderText() { return QStringLiteral("打开文件夹"); }
QString ConfigFileEditorPage::readOnlyPrefix() { return QStringLiteral("只读："); }
QString ConfigFileEditorPage::hintText() {
    return QStringLiteral("改完点「保存」写到盘上。原来是什么行尾、有没有 BOM，都不动。");
}
QString ConfigFileEditorPage::savedPrefix() { return QStringLiteral("已保存（"); }
