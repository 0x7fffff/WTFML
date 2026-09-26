/**
 * @file InstanceFilesPage.cpp
 * @brief InstanceFilesPage 的实现（见 .hpp 里那段"六页共用一个形状"）。
 * @author 0x7fffff
 * @date 2026-09-26
 */
#include "InstanceFilesPage.hpp"

#include "ElidedLabel.hpp"
#include "MainWindowUI.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPalette>
#include <QPushButton>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

/// 比正文浅一档的那个颜色。**全仓唯一去处是 `QPalette::PlaceholderText`**（见
/// InlineMessage::setNote() 上面那段 NOTE）—— 这里只是把它挪到"路径"和"大小·时间"
/// 这两处次要文字上，不自己写死一个灰。
void makeSecondary(QLabel *label) {
    if (label == nullptr) {
        return;
    }
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);
}

/// 比正文字号小一档 —— **行里那第二行小字**专用的（现在只有「大小 · 时间」在用）。
///
/// 档位是 `MainWindowUI::versionDateFontDelta()`，和版本树那两行式的行**同一档**
/// （VersionTreeDelegate::paint 里给 rowSubTextRole() 那行算字号用的就是它）：
/// 实例设置里这一页和版本树是并排的两种列表，行高也都是 46，字号差一档就不像一套东西。
/// 两行一样大的话，"上面一行名字、下面一行细节"看着只像一行被折了。
///
/// 顺带也是**行高够用**的前提：46 px 那一格里要装两行（19 + 2 + 15 上下），
/// 第二行照正文那么大就顶到边了。
void makeSmallLine(QLabel *label) {
    if (label == nullptr) {
        return;
    }
    QFont font = label->font();
    font.setPointSize(qMax(6, font.pointSize() + MainWindowUI::versionDateFontDelta()));
    label->setFont(font);
}

}  // namespace

InstanceFilesPage::InstanceFilesPage(QWidget *parent) : QWidget(parent) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(8);

    // ---------- 上面那一排：路径 + 刷新 + 打开文件夹 ----------
    m_pathRow = new QWidget(this);
    auto *pathRow = new QHBoxLayout(m_pathRow);
    pathRow->setContentsMargins(0, 0, 0, 0);
    pathRow->setSpacing(8);
    // ★ 2026-09-27：这一格从前**一个字都没有**（建出来、上了色、给了宽度策略，就是没人
    //   setText）—— 上面那一排看着是「…… [刷新] [打开文件夹]」，用户不知道这一页在看哪个
    //   目录，而这一页叫「文件管理」。文字由 refresh() 灌（目录是从 setDirectory() 来的，
    //   只有那一处知道）。
    //   用 ElidedLabel 而不是普通 QLabel：路径一定比这一格长（实例名本身能到 64 个字），
    //   普通 QLabel 装不下是**硬裁**（最后一级目录名被切掉一半），而这一段文字两头都有用
    //   （盘符那头说明是哪个游戏目录，尾巴那头是 mods / saves 哪一页）⇒ 从**中间**省略。
    m_pathLabel = new ElidedLabel(m_pathRow);
    m_pathLabel->setObjectName(pathLabelName());
    m_pathLabel->setElideMode(Qt::ElideMiddle);
    makeSecondary(m_pathLabel);
    // ★ 路径可能很长（实例名本身就能到 64 个字），而它这一格**不许**把整页顶宽：
    //   横向按 Ignored 算 = 布局完全不管它要多大，只给它剩下来的地方，多出来的字被裁掉。
    //   （和实例页左栏那条折行 QLabel 顶大整窗 sizeHint 是同一个坑的另一面 —— 那边是
    //   不能让它要宽度，这边是不能让它给宽度。）
    m_pathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_pathLabel->setMinimumWidth(0);
    // NOTE: 这里从前挂着 `setTextInteractionFlags(Qt::TextSelectableByMouse)`（"路径能选能复
    //       制"）。换 ElidedLabel 之后那个开关等于没开：文字是 paintEvent 自己画的，选中
    //       高亮画不出来 —— 用户拖一遍什么反应都没有，比一开始就不给还费解。整条完整路径
    //       改由**悬停提示**给（见 refresh() 里 setToolTip 那句），那是这一格现在唯一
    //       "看得全"的路。
    pathRow->addWidget(m_pathLabel, 1);

    m_refresh = new QPushButton(refreshText(), m_pathRow);
    m_refresh->setObjectName(refreshButtonName());
    // 刷新是**这一页自己的事**（重新列一遍目录，只有读），不用绕到 MainWindow 去。
    connect(m_refresh, &QPushButton::clicked, this, &InstanceFilesPage::refresh);
    pathRow->addWidget(m_refresh);

    m_openFolder = new QPushButton(openFolderText(), m_pathRow);
    m_openFolder->setObjectName(openFolderButtonName());
    connect(m_openFolder, &QPushButton::clicked, this,
            [this] { emit openFolderRequested(m_directory); });
    pathRow->addWidget(m_openFolder);

    outer->addWidget(m_pathRow);

    // ---------- 列表 ----------
    m_list = new QListWidget(this);
    m_list->setObjectName(listName());
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setUniformItemSizes(true);
    // 行是**真控件**（见 .hpp 里那段"为什么每行是一个真控件"），宽度由它自己说了算；
    // 不许出现横向滚动条，否则那一排按钮会跟着来回滑。
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(m_list, 1);

    // ---------- 空 / 用不上时那一段话 ----------
    m_notice = new QLabel(this);
    m_notice->setObjectName(noticeLabelName());
    m_notice->setWordWrap(true);
    m_notice->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    makeSecondary(m_notice);
    outer->addWidget(m_notice, 1);

    // 摆到"目录还没接上"那一档：构造函数里 setDirectory() 还没被叫过，这会儿既没东西可列、
    // 也没得可点，露出一个空框比说一句话更让人以为坏了。
    applyState();
}

void InstanceFilesPage::setMode(Mode mode) {
    m_mode = mode;
    refresh();
}

void InstanceFilesPage::setCanToggle(bool canToggle) {
    m_canToggle = canToggle;
    refresh();
}

void InstanceFilesPage::setCanDelete(bool canDelete) {
    m_canDelete = canDelete;
    refresh();
}

void InstanceFilesPage::setEmptyText(const QString &text) {
    m_emptyText = text;
    applyState();
}

void InstanceFilesPage::setDirectory(const QString &dir) {
    m_directory = dir;
    refresh();
}

void InstanceFilesPage::setUnavailable(const QString &reason) {
    m_unavailable = reason;
    applyState();
}

QString InstanceFilesPage::formatSize(qint64 bytes) {
    // 单位一律走字面量而不是 tr()：它们是量纲不是话（换语言也不该跟着换），而且验证工程
    // 按这几句核对 —— 那几条断言得有一个**不会随语言变**的基准。
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0) {
        return QStringLiteral("%1 KB").arg(kb, 0, 'f', 1);
    }
    const double mb = kb / 1024.0;
    if (mb < 1024.0) {
        return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
    }
    return QStringLiteral("%1 GB").arg(mb / 1024.0, 0, 'f', 1);
}

void InstanceFilesPage::refresh() {
    if (m_list == nullptr) {
        return;
    }
    // 上面那一排那一格写"A 在看哪个目录"。**在目录存在性判断之前**就写：目录还不存在
    // （第一次开 mods/ 之前它本来就不在盘上）也得让人看见路径 —— 那时候这一页正是
    // "这个目录是空的/不存在"，路径恰恰是最该看见的东西。
    // 分隔符统一成 Windows 那种反斜杠：这是给用户看的字，不是喂给接口的路径。
    const QString shownDir = QDir::toNativeSeparators(m_directory);
    m_pathLabel->setText(shownDir);
    // 装不下时是从中间省略的，两头那几段还能看；整条路径靠悬停提示给（见构造里那段 NOTE）。
    m_pathLabel->setToolTip(shownDir);
    // clear() 会连同 setItemWidget() 挂上去的那些行控件一起删掉（列表持有它们）。
    m_list->clear();
    // 先当空的，下面每加一条翻过来 —— 这样"目录不存在"和"目录是空的"自然走同一条路。
    m_empty = true;

    const QDir dir(m_directory);
    if (m_directory.isEmpty() || !dir.exists()) {
        applyState();
        return;
    }

    // 不设 QDir::Hidden：点开头的文件（.DS_Store 那一类）不该占一行。而 `.disabled` 是
    // **后缀**不是前缀，所以被禁用的那些照旧列得出来。
    const QFileInfoList entries =
        dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                          QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo &info : entries) {
        const bool isDir = info.isDir();
        if (m_mode == Mode::DirsOnly && !isDir) {
            // 存档那一页：saves 底下的 level.dat / session.lock 那些不是存档，一个都不列。
            continue;
        }

        const QString raw = info.fileName();
        // 禁用：只有"游戏会自己扫的那三个目录"里的**文件**才有这一档。目录不给 —— 把一个
        // 文件夹改名成 `xxx.disabled` 会连它里面的东西一起从游戏眼里消失，那不是
        // "禁用这个包"该有的效果（而且 mods/ 下那些目录根本不是 mod，比如 mcef-libraries）。
        const bool disabled = m_canToggle && !isDir &&
                              raw.endsWith(disabledSuffix(), Qt::CaseInsensitive);
        const QString shown = disabled ? raw.left(raw.size() - disabledSuffix().size()) : raw;
        const QString path = info.absoluteFilePath();

        auto *item = new QListWidgetItem(disabled ? shown + disabledMark() : shown, m_list);
        item->setData(pathRole(), path);
        item->setData(disabledRole(), disabled);
        // 行高和实例树 / 版本树同一档：一页里换行高不会错，但"我们自己的列表"看着该是一个东西。
        item->setSizeHint(QSize(0, MainWindowUI::versionTreeRowHeight()));
        m_list->addItem(item);
        m_list->setItemWidget(item, makeRow(item->text(), path, isDir, disabled, info.size(),
                                            info.lastModified()));
        m_empty = false;
    }

    applyState();
}

QWidget *InstanceFilesPage::makeRow(const QString &shownName, const QString &path, bool isDir,
                                    bool isDisabled, qint64 size, const QDateTime &modified) {
    auto *row = new QWidget(m_list);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(8);

    // ---------- 左边那半：**名字一行、大小·时间一行** ----------
    //
    // 用户 2026-09-26 报的「实例设置里模组列表模组名称显示存在错误」，根子在这儿：
    // 从前这三样是**一行**里排的（名字 + 「12.3 MB · 2026-09-26 12:00」+ 两颗按钮）。
    // 那个大小·时间那一截在 150% 缩放的字号下要 300 px，而行宽只有 450 上下 ——
    // 于是名字那一格被挤成 **0 宽**，一整列的名字全看不见（`fabric-api-0.119.3+1.21.4.jar`
    // 这类名字在盘上是常态）。行高本来就是 46 px（versionTreeRowHeight()），
    // 装两行字够 —— 摆成上下两行，名字那一格就能拿到整幅宽度（按钮除外）。
    auto *textColumn = new QWidget(row);
    auto *textLayout = new QVBoxLayout(textColumn);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    // 名字那一格是**会省略号收尾**的（ElidedLabel，不是普通 QLabel）：普通 QLabel 在控件
    // 比文字窄的时候是**硬裁**的 —— 最后一个字被切掉一半，看着像这一条的名字就是那串
    // 半截的字。真名字有 47 个字符（约 650 px），这一格给不了那么宽。
    //
    // 从**中间**截（不是尾巴）：这一串两头都有用 —— 前头是哪个 mod，尾巴上是 `.jar`
    // 那一截；而被禁用的那几条尾巴上还挂着「（已禁用）」四个字，
    // 从尾巴截掉恰好把「这一条是关着的」这个最要紧的信息先扔了。
    auto *name = new ElidedLabel(textColumn);
    name->setObjectName(nameLabelName());
    name->setText(shownName);
    name->setElideMode(Qt::ElideMiddle);
    // 名字长的时候让它自己缩（`Ignored` = 别拿它的 sizeHint 去撑这一行），
    // 但不许被挤成 0：这一格是这一行的主要内容。整条路径挂在 tooltip 上，
    // 名字被截掉的那一截还能看全（省略号只说明"这里短了"，短的是什么只有它说得清）。
    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    name->setMinimumWidth(nameLabelMinWidth());
    name->setToolTip(QDir::toNativeSeparators(path));
    if (isDisabled) {
        // 被禁用的一整行**名字也灰掉**：光靠名字后面那四个字，扫一眼看不出这一条是关着的。
        makeSecondary(name);
    }
    textLayout->addWidget(name);

    // 「12.3 MB · 2026-09-26 12:00」。文件夹没有有意义的大小（Windows 上 info.size() 对
    // 目录给的是目录项自己的字节数，不是里面东西的总和），给一个破折号比给一个假数字好。
    // 这一行也会省略（尾巴那头）：字号大 / 窗口窄的时候它比这一格还长，硬裁同样是"看着
    // 像画坏了"。它的 hint 不封顶 —— 它是下面那行小字，让名字那一格先拿够。
    QString meta = isDir ? QStringLiteral("—") : formatSize(size);
    if (modified.isValid()) {
        meta += QStringLiteral(" · ") + modified.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    }
    auto *metaLabel = new ElidedLabel(textColumn);
    metaLabel->setObjectName(metaLabelName());
    metaLabel->setText(meta);
    metaLabel->setElideMode(Qt::ElideRight);
    makeSecondary(metaLabel);
    makeSmallLine(metaLabel);
    textLayout->addWidget(metaLabel);

    layout->addWidget(textColumn, 1);

    if (m_canToggle && !isDir) {
        auto *toggle = new QToolButton(row);
        toggle->setObjectName(toggleButtonName());
        toggle->setText(isDisabled ? toggleOffText() : toggleOnText());
        toggle->setAutoRaise(true);
        // 说"改成哪个名字"要用**盘上那个真名字**（QFileInfo::fileName），不是行上那串
        // 显示名 —— 禁用着的那一条显示名后面挂着「（已禁用）」，拼进去就成了一串谁也不认的名字。
        const QString fileName = QFileInfo(path).fileName();
        toggle->setToolTip(isDisabled ? tr("把名字改回去，游戏又能扫到它了。")
                                      : tr("把名字改成 %1%2，游戏就扫不到它了（文件还在）。")
                                            .arg(fileName, disabledSuffix()));
        connect(toggle, &QToolButton::clicked, this, [this, path] { emit toggleRequested(path); });
        layout->addWidget(toggle);
    }

    if (m_canDelete) {
        auto *remove = new QToolButton(row);
        remove->setObjectName(deleteButtonName());
        remove->setText(deleteText());
        remove->setAutoRaise(true);
        remove->setToolTip(isDir ? tr("把这个文件夹连同里面的东西一起删掉。")
                                 : tr("把这个文件从盘上删掉。"));
        connect(remove, &QToolButton::clicked, this, [this, path] { emit deleteRequested(path); });
        layout->addWidget(remove);
    }

    return row;
}

QLabel *InstanceFilesPage::pathLabel() const {
    // 只在**这里**把 ElidedLabel 当成 QLabel 交出去（见 .hpp 里那段）：那一格的确是
    // ElidedLabel，但调用方要的只有 setText / text，不必知道它是怎么画出来的。
    return m_pathLabel;
}

void InstanceFilesPage::applyState() {
    const bool unavailable = !m_unavailable.isEmpty();
    const bool empty = !unavailable && m_empty;

    m_pathRow->setVisible(!unavailable);
    m_list->setVisible(!unavailable && !empty);
    m_notice->setVisible(unavailable || empty);
    m_notice->setText(unavailable ? m_unavailable
                                  : (m_emptyText.isEmpty() ? emptyText() : m_emptyText));
}

QToolButton *InstanceFilesPage::toggleButtonAt(int row) const {
    if (m_list == nullptr) {
        return nullptr;
    }
    QListWidgetItem *item = m_list->item(row);
    QWidget *rowWidget = item != nullptr ? m_list->itemWidget(item) : nullptr;
    return rowWidget != nullptr ? rowWidget->findChild<QToolButton *>(toggleButtonName()) : nullptr;
}

QToolButton *InstanceFilesPage::deleteButtonAt(int row) const {
    if (m_list == nullptr) {
        return nullptr;
    }
    QListWidgetItem *item = m_list->item(row);
    QWidget *rowWidget = item != nullptr ? m_list->itemWidget(item) : nullptr;
    return rowWidget != nullptr ? rowWidget->findChild<QToolButton *>(deleteButtonName())
                                : nullptr;
}
