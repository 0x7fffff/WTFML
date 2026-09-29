/**
 * @file OverlayHost.cpp
 * @brief Implements the in-window overlay layer (see OverlayHost.hpp for the whole picture).
 * @author 0x7fffff
 * @date 2026-09-25
 */

#include "OverlayHost.hpp"
#include "Animations.hpp"  // 整块覆盖这一层的淡入（2026-09-29）
#include "Theme.hpp"   // 顶上那颗「← 返回」的身份（一般性 = 黑字黑边）

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QEventLoop>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace {

/// 把控件"现在长什么样"抓成一张图 —— **不用 `grab()`**。
///
/// 两个理由，都是这个仓库踩过的（见 ui_check 里 captureWidget() 那段）：`grab()` 出来的是
/// **物理像素**（本机 150% 缩放：一张 627x294 的图配一个 418x196 的控件），拿它当底图铺回去
/// 会被缩一次、糊一层；而且 `grab()` 走窗口的后备存储，文字带 ClearType 亚像素抗锯齿。
/// 往 QImage/QPixmap 上 `render()` 两个问题一起没有，尺寸还跟控件一致。
QPixmap snapshot(QWidget *widget) {
    QPixmap pixmap(widget->size());
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    widget->render(&painter);
    return pixmap;
}

/// 页眉那一行的左右留白、以及内容四周留的那一圈。
/// NOTE: 和 `MainWindowUI` 里页面用的那圈边距是**同一个数**（kPageMargin = 9）——
///       覆盖层是一整个"页面"，留白和别的页面不一样的话，进设置再退出来会看着跳一下。
constexpr int kOverlayPageMargin = 9;

/// 卡片（整页里那张 / 小提示那张）那一块布的调色板：底出厂是 `Base`（用户单独改过就是那个）。
QPalette cardChromePalette() {
    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Window, Theme::partBackground(Theme::Part::Card));
    return palette;
}

}  // namespace

OverlayHost::OverlayHost(QWidget *anchor, QWidget *parent) : QWidget(parent), m_anchor(anchor) {
    setObjectName(overlayHostName());
    // **不进任何布局**：一进布局就会把锚点挤走（那是"分成上下两半"，不是"浮在上面"）。
    // 几何全靠 relayout()。所以这一条要是被谁删了，症状是"覆盖层一开，页面被压扁"。
    setVisible(false);

    // 还回来的内容先寄存在这儿（见 closeOverlay 的 NOTE）。
    m_parking = new QWidget(this);
    m_parking->setObjectName(QStringLiteral("overlayParking"));
    m_parking->setVisible(false);

    // ---------- 页眉（常驻：返回箭头 + 标题） ----------
    m_pageFrame = new QWidget(this);
    m_pageLayout = new QVBoxLayout(m_pageFrame);
    m_pageLayout->setContentsMargins(0, 0, 0, 0);
    m_pageLayout->setSpacing(0);

    QWidget *header = new QWidget(m_pageFrame);
    header->setFixedHeight(pageHeaderHeight());
    QHBoxLayout *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(kOverlayPageMargin, 0, kOverlayPageMargin, 0);
    headerLayout->setSpacing(10);
    m_backButton = new QPushButton(backButtonText(), header);
    m_backButton->setObjectName(overlayBackButtonName());
    m_backButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_backButton->setToolTip(tr("返回。这一页上没保存的改动会丢掉。"));
    // 「← 返回」= 往回走（一般性那一档）—— 全应用每一页顶上那一颗都是它。
    Theme::markButton(m_backButton, Theme::ButtonKind::Neutral);
    headerLayout->addWidget(m_backButton);
    m_titleLabel = new QLabel(header);
    m_titleLabel->setObjectName(overlayTitleLabelName());
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch(1);
    m_pageLayout->addWidget(header);

    m_pageContent = new QWidget(m_pageFrame);
    m_pageContent->setObjectName(overlayPageContentName());
    m_pageContentLayout = new QVBoxLayout(m_pageContent);
    // 留白和别的页面一样是那一圈 9 px（见 kOverlayPageMargin）。
    m_pageContentLayout->setContentsMargins(kOverlayPageMargin, kOverlayPageMargin,
                                            kOverlayPageMargin, kOverlayPageMargin);
    m_pageContentLayout->setSpacing(0);

    // 整页那一档外面套一层滚动区。**不是"以防万一"**：设置页是按它自己的尺寸设计的
    // （实例设置 600x480），而内容区多大是主窗口说了算 —— 窗口一小，页脚那排
    // 「取消 / 确定」就被切在下面，用户怎么点都点不着，而那正是"这个页面打不开"。
    // 外面套一层，任何窗口尺寸下它都能滚到底。
    auto *pageScroll = new QScrollArea(m_pageFrame);
    pageScroll->setObjectName(overlayPageScrollName());
    pageScroll->setWidgetResizable(true);
    pageScroll->setFrameShape(QFrame::NoFrame);
    pageScroll->setWidget(m_pageContent);
    m_pageScroll = pageScroll;
    m_pageLayout->addWidget(pageScroll, 1);
    m_pageFrame->setVisible(false);
    // 内容变了要自己催滚动条重算，见 eventFilter 里 m_pageContent 那一段。
    m_pageContent->installEventFilter(this);

    // 内容塞进来 / 拿走的时候卡片得重新量一次高矮。**必须有这一条**：`openPrompt()` 只把
    // 宿主摆好，内容是在它**返回之后**才 addWidget 进去的（调用点如此，`execEmbedded`
    // 也如此），那会儿卡片还按"空内容"算过一次高。没有这一条的症状是"提示框的高度只够
    // 装下第一行，剩下的字被裁掉"。
    m_promptContent = new QWidget(this);
    m_promptContent->setObjectName(overlayPromptContentName());
    m_promptContentLayout = new QVBoxLayout(m_promptContent);
    m_promptContentLayout->setContentsMargins(0, 0, 0, 0);
    m_promptContentLayout->setSpacing(0);
    m_promptContent->installEventFilter(this);

    // ---------- 提示那块的卡片（常驻，里面装着上面那个内容容器） ----------
    // 卡片自己画底和边：这样"嵌进来的对话框"和"我们自己搭的提示面板"看起来是同一张卡，
    // 不会一个有线一个没线。
    QFrame *card = new QFrame(this);
    card->setObjectName(overlayPromptHostName());
    card->setFrameShape(QFrame::StyledPanel);
    card->setAutoFillBackground(true);
    // 卡片底色出厂是 `Base`（和输入框/列表里那层一样），2026-09-28 起归
    // `Theme::Part::Card` 管（用户能把卡片单独换一个色）。
    // NOTE: 基色取**应用调色板**而不是卡片自己的：卡片身上就写着算好的那一份，
    //       第二次进来再取一次就是"在改过的基色上再算一遍"（TitleBar 那边同一个坑）。
    card->setPalette(cardChromePalette());
    // 卡片是**常驻**的（构造里建一次、跨提示复用）⇒ 主题一换得自己重算一遍。
    Theme::follow(card, [card]() { card->setPalette(cardChromePalette()); });
    m_promptLayout = new QVBoxLayout(card);
    // 卡片自己留 18 px，内容容器**不再留**（两圈叠起来的话内容就被挤窄，而"提示有多宽"
    // 是有唯一出处的（promptContentWidth()），挤一下就不守恒了）。
    m_promptLayout->setContentsMargins(promptPadding(), promptPadding(), promptPadding(),
                                       promptPadding());
    m_promptLayout->setSpacing(0);
    m_promptLayout->addWidget(m_promptContent, 1);
    card->setVisible(false);
    m_promptCard = card;
    // 卡片自己也被盯上：里面那些子控件的 sizeHint 是**晚一步**才定的（标签钉了宽度、
    // 但它自己的高度要等尺寸约束算完），那一步会以 LayoutRequest 的形状落到卡片身上。
    card->installEventFilter(this);

    connect(m_backButton, &QPushButton::clicked, this, &OverlayHost::backPressed);

    if (m_anchor != nullptr) {
        // 锚点一动（窗口缩放、标签栏换行、页面换掉）几何就得重算 —— 覆盖层不归布局管，
        // 没人会替它做这件事。
        m_anchor->installEventFilter(this);
        if (auto *tabs = qobject_cast<QTabWidget *>(m_anchor)) {
            // 翻页了：整页那一档要么"摆出来"要么"收着"（见头文件那段），提示那一档换底图。
            connect(tabs, &QTabWidget::currentChanged, this,
                    [this](int) { onAnchorTabChanged(); });
        }
    }
}

QRect OverlayHost::contentRect() const {
    QWidget *host = parentWidget();
    if (m_anchor == nullptr || host == nullptr) {
        return QRect();
    }
    // 内容区 = 锚点里**当前那一页**占的那块。取页控件自己的几何是公开 API ——
    // 不去掏 QTabWidget 内部那个堆叠控件的名字（`qt_tabwidget_stackedwidget` 是实现
    // 细节，抄它等于把 Qt 升级的风险引进界面布局）。
    if (auto *tabs = qobject_cast<QTabWidget *>(m_anchor)) {
        if (QWidget *page = tabs->currentWidget()) {
            return QRect(page->mapTo(host, QPoint(0, 0)), page->size());
        }
        // 一页都还没加（构造中途）：先按整个锚点算，第一次 relayout 就纠回来了。
    }
    return QRect(m_anchor->mapTo(host, QPoint(0, 0)), m_anchor->size());
}

OverlayHost *OverlayHost::forWidget(QWidget *widget) {
    for (QWidget *w = widget; w != nullptr; w = w->parentWidget()) {
        if (auto *host = qobject_cast<OverlayHost *>(w)) {
            return host;
        }
    }
    return nullptr;
}

QWidget *OverlayHost::coveredPage() const {
    if (auto *tabs = qobject_cast<QTabWidget *>(m_anchor)) {
        return tabs->currentWidget();
    }
    return nullptr;
}

void OverlayHost::releaseCoveredPage() {
    if (m_disabledPage != nullptr) {
        m_disabledPage->setEnabled(true);
        m_disabledPage = nullptr;
    }
}

int OverlayHost::anchorTabIndex() const {
    if (auto *tabs = qobject_cast<QTabWidget *>(m_anchor)) {
        return tabs->currentIndex();
    }
    return -1;
}

bool OverlayHost::onOwnersTab() const {
    const int current = anchorTabIndex();
    if (current < 0 || m_pageOwnerTab < 0) {
        // 锚点不是选项卡 / 压根没有页面（纯提示那一档）：没有"别的选项卡"这回事。
        return true;
    }
    return current == m_pageOwnerTab;
}

bool OverlayHost::parked() const {
    return m_mode == Mode::Page && !onOwnersTab();
}

void OverlayHost::applyVisibility() {
    // 整页那一档**只在开它的那颗选项卡上露面**。切到别的选项卡上就"收着"：不关、不销毁
    // —— 这一页正卡在 `execPage()` 的嵌套事件循环里，收掉了调用方会一直等下去，而用户要的
    // 「再点回来还是那一个界面（连滚到哪儿都在）」也就没了（见头文件那段）。
    const bool pageShown = (m_mode == Mode::Page) && onOwnersTab();
    // 提示那一档不跟着选项卡走：它是要人回答的一句话，收起来 = 让调用方在用户看不见的
    // 地方干等。切页时它换的只是底图（见 refreshBackdrop）。
    const bool promptShown = (m_mode == Mode::Prompt);

    if (m_pageFrame != nullptr) {
        if (m_pageFrame->isVisible() && !pageShown) {
            // 要收起来了：**先记一笔焦点在页里哪个控件上**。Qt 的焦点是跟着"看得见"走的
            // （父控件一藏起来就清掉），回来时没人给它摆回去 —— 症状是"切一趟选项卡，
            // 我正在打字的那个格子丢了焦点"（字还在，光标没了）。
            QWidget *focus = QApplication::focusWidget();
            m_pageFocus = (focus != nullptr && m_pageFrame->isAncestorOf(focus)) ? focus : nullptr;
        }
        m_pageFrame->setVisible(pageShown);
        if (pageShown && m_pageFocus != nullptr && m_pageFrame->isAncestorOf(m_pageFocus)) {
            m_pageFocus->setFocus();
        }
    }
    if (m_promptCard != nullptr) {
        const bool wasShown = m_promptCard->isVisible();
        m_promptCard->setVisible(promptShown);
        // 提示那一档：整块**本来就露着头**（比如连着开第二个提示）时，只让新那一块自己
        // 淡进来。整块还没露头的情况归下面那一条（整块一起淡，不然一里一外淡两次、
        // 快的那个看着像"闪"）。
        if (promptShown && !wasShown && isVisible()) {
            Anim::overlayAppear(m_promptCard);
        }
    }
    const bool wasVisible = isVisible();
    setVisible(promptShown || pageShown);
    if (isVisible() && !wasVisible) {
        // 2026-09-29：整块覆盖的那一层淡进来（只淡、不抬 —— 它是"盖住内容区"的，
        // 抬起来边上会露出一条没盖住的缝）。规矩见 ui/Animations.hpp。
        Anim::overlayAppear(this);
    }
    if (isVisible()) {
        // 浮在上面这一条**每回露面都得重申**：锚点换了一页，新那一页在子控件次序上可能
        // 压过我们（Qt 新加的子控件默认排在最后，也就是"最上面"）。
        raise();
    }
    retargetCoveredPage();
    relayout();
}

QPixmap OverlayHost::captureBackdrop() const {
    // 要变暗的那一块**现在**是谁：整页那一档**正摆在眼前**时是那一层自己（它才是盖在
    // 底下那些内容上面的东西），其余情况都是当前选项卡那一页。
    //
    // NOTE: 判据里那半句 `onOwnersTab()` 不能少。在这之前 refreshBackdrop() 写的是
    //       `if (m_overPage) return;` —— 它把"底下压着一页"和"那一页正铺在屏幕上"当成
    //       了同一件事，而页面**收着**（用户在别的选项卡上逛）恰恰把这两件事分开了：
    //       用户在新选项卡上看到的会是一张**上一个选项卡那一页**的变暗快照。
    if (m_overPage && onOwnersTab() && m_pageFrame != nullptr) {
        return snapshot(m_pageFrame);
    }
    QWidget *page = coveredPage();
    return page != nullptr ? snapshot(page) : QPixmap();
}

void OverlayHost::refreshBackdrop() {
    // 换选项卡 / 那一页收起来 / 又摆出来 —— 底下那一块**换了**，重拍一张。
    //
    // NOTE: 这里以前是一句 `if (m_overPage) return;`，理由是"底下那一页一直没动过"。
    //       那个理由在"页摆在眼前"时成立，在"页收着"时不成立：**收着**的意思是那一页
    //       此刻不在屏幕上（见头文件那段），底下压着的是当前选项卡自己的内容。所以现在
    //       和 openPrompt() 共用同一个判据（captureBackdrop）。
    m_backdrop = captureBackdrop();
    update();
}

void OverlayHost::onAnchorTabChanged() {
    if (m_mode == Mode::Prompt) {
        refreshBackdrop();
    }
    applyVisibility();
}

void OverlayHost::setPromptBlockedWidget(QWidget *widget) {
    if (m_promptBlockedWidget == widget) {
        return;
    }
    // 换人之前先把上一位放开：不然它会被永久禁用（而这位的禁用状态本来就是"临时的"）。
    if (m_promptBlockedWidget != nullptr) {
        m_promptBlockedWidget->setEnabled(true);
    }
    m_promptBlockedWidget = widget;
    retargetCoveredPage();
}

void OverlayHost::retargetCoveredPage() {
    releaseCoveredPage();
    // ★ 2026-09-27：**提示**那一档还要额外按住一块（底栏）—— 它不在这一层底下，见
    //   setPromptBlockedWidget() 那段说明（不按住的症状是"点了没反应、也没有一个字解释"）。
    //   这一句必须在下面那个 `if (!isVisible()) return;` **之前**：提示收掉的那一刻这一层
    //   就不可见了，放在后面的话底栏永远解不开禁用。
    if (m_promptBlockedWidget != nullptr) {
        m_promptBlockedWidget->setEnabled(m_mode != Mode::Prompt);
    }
    // 判据是"**现在真摆在眼前**"（不是"开着"）：整页那一档收在别的选项卡上的时候，
    // 它没盖住任何东西 —— 用户正要用下面那一页，这时候按住它就是"界面活着但点不动"。
    if (!isVisible()) {
        return;
    }
    QWidget *page = coveredPage();
    if (page != nullptr) {
        // 被盖住的那一页要**按住**。覆盖层浮在上面能吃掉鼠标，但**键盘吃不下** ——
        // 按 Tab 照样能把焦点挪到下面那些控件上，接着按空格就等于在用户看不见的地方
        // 点了一下。禁用是唯一一处能让"看不见 = 点不着"成立的开关。
        //
        // NOTE: 禁用会改那一页的调色板，正常情况下"变灰"是看得见的 —— 这里不要紧：
        //       页面型那一档它被整块盖住，提示型那一档上面盖的是**开之前拍下来的底图**
        //       （见 openPrompt），禁用之后的外观根本没机会露面。
        page->setEnabled(false);
        m_disabledPage = page;
    }
}

void OverlayHost::releaseContent(QVBoxLayout *content) {
    if (content == nullptr) {
        return;
    }
    while (content->count() > 0) {
        QLayoutItem *item = content->takeAt(0);
        if (QWidget *widget = item->widget()) {
            // **不删**，还回去：调用方那些对话框是栈对象，必须让它在自己的作用域里正常
            // 析构。这里要是把它当子控件删了，回到调用方就是二次释放。
            widget->hide();
            widget->setParent(m_parking);
        }
        delete item;
    }
}

QWidget *OverlayHost::openPage(const QString &title, int ownerTab) {
    if (m_mode != Mode::None) {
        // 一层只放得下一个页面（见头文件那段）。返回 nullptr，调用方当"没开成"。
        // NOTE: 页面**收着**的时候（用户在别的选项卡上逛）也算"有人占着这一层"，
        //       照样是 nullptr —— 那一位没被释放，收起来的页面只是看不见而已。
        return nullptr;
    }
    m_mode = Mode::Page;
    // 这一页是**哪一颗选项卡**的：它决定这一页在哪儿露面（见头文件那段）。
    //
    // ★ 调用方说得出口（`ownerTab != -1`）时**以它为准**，不再看当前选项卡是谁 ——
    //   有些页面的"家"是定死的，和用户从哪条路进来无关（「实例设置」永远归「实例」那一颗）。
    //   取当前选项卡的症状见 openPage() 的 doc。
    m_pageOwnerTab = ownerTab >= 0 ? ownerTab : anchorTabIndex();
    m_pageFocus = nullptr;
    m_titleLabel->setText(title);
    applyVisibility();
    if (m_backButton != nullptr) {
        m_backButton->setFocus();
    }
    return m_pageContent;
}

QWidget *OverlayHost::openPrompt(bool dismissOnOutsideClick) {
    if (m_mode == Mode::Prompt) {
        // 提示叠提示：不做（外面那一层正卡在自己的事件循环里，把它顶掉就是死等）。
        // 这一条在外面会被 InlineMessage::ask() 当成"没问成"（返回 -1）。
        return nullptr;
    }
    const bool overPage = (m_mode == Mode::Page);
    m_overPage = overPage;
    m_dismissOnOutsideClick = dismissOnOutsideClick;

    // "四周变暗"要能**看见**下面那一层 —— 而覆盖层和下面那一页是**兄弟**：在这上面
    // 半透明地画一层，合的是 `centralWidget` 的底，不是页面的像素（Qt 重绘一个子控件
    // 时不会连带重绘它的兄弟）。所以这里把下面那一块**拍下来**当底图，画的时候先铺底图
    // 再压暗。一次开只拍一张，代价就是一次页面渲染。
    //
    // NOTE: 拍的是"开之前"的样子，所以底下那些会自己动的东西（下载进度）在提示开着的
    //       期间是**冻住的**。提示期间下面本来也点不动，冻住不算问题；反过来"抓完再禁用"
    //       这个顺序要紧 —— 颠倒过来拍到的就是变灰之后的样子了。
    //
    // NOTE: 底下压着一页、而那一页**正收在别的选项卡上**的时候（用户切走之后又冒出来一条
    //       提示），拍的是**现在这一页**：那一页此刻不露面，要变暗的是用户正看着的这块。
    //       `m_overPage` 照旧是 true —— 提示收掉之后还是回到那一页（它只是收着）。
    m_backdrop = captureBackdrop();

    m_mode = Mode::Prompt;
    // 整页那一层就此收起来（不是"藏起来就行" —— 藏起来之后底下那些控件还能收到键盘焦点
    // 和 Tab）。收的是**显示**，`m_mode` 说"现在最上面是提示"。
    applyVisibility();
    if (m_promptContent != nullptr) {
        m_promptContent->setFocus();
    }
    return m_promptContent;
}

void OverlayHost::closeOverlay() {
    if (m_mode == Mode::Prompt) {
        // 收最上面那一层：提示。底下压着一页的话，那页**还在**。
        releaseContent(m_promptContentLayout);
        m_dismissOnOutsideClick = false;
        m_backdrop = QPixmap();
        if (m_overPage) {
            m_overPage = false;
            m_mode = Mode::Page;
            // 回到那一页（它可能是收在别的选项卡上的 —— 那就还是收着，见 applyVisibility）。
            applyVisibility();
            // 焦点：优先摆回"提示盖上来之前那一格"（applyVisibility 已经做过一遍），
            // 没有记录在案的就按老规矩把它摆到返回箭头上（键盘一进来就归页眉管）。
            if (m_pageFocus == nullptr && m_backButton != nullptr && !parked()) {
                m_backButton->setFocus();
            }
            return;
        }
        m_mode = Mode::None;
        applyVisibility();
        return;
    }
    if (m_mode == Mode::Page) {
        releaseContent(m_pageContentLayout);
        m_mode = Mode::None;
        m_pageOwnerTab = -1;
        m_pageFocus = nullptr;
        applyVisibility();
        return;
    }
}

bool OverlayHost::dismiss() {
    if (m_mode == Mode::Prompt) {
        // 提示那一档：和"按 Esc / 点框外面"**同一发**（`execEmbedded` 接它去 reject，
        // 那些自己搭提示的调用方也接它）。
        emit promptDismissed();
        return true;
    }
    if (m_mode == Mode::Page) {
        if (!onOwnersTab()) {
            // 收在别的选项卡上：用户看不见它，这一发不该落在它头上（不然就是"我在主页
            // 点了一下主页，下载页那个详情页自己没了"）。
            return false;
        }
        // 页面那一档：和"点返回箭头"**同一发**。★ 这里**绝不能改成 `closeOverlay()`** ——
        // 用 `execPage()` 开出来的那一页正卡在自己的嵌套事件循环里，`closeOverlay()` 只把
        // 外壳收了、一个信号都不发 ⇒ 调用方一直等下去（`MainWindow::openResourceDetailPage`
        // 那一段的 `m_resourcePage.open` 永远归不了 false）。mw_check 里那句"收的必须是
        // 返回箭头（= reject），不能是 closeOverlay()"说的就是这一件事。
        emit backPressed();
        return true;
    }
    return false;
}

void OverlayHost::relayout() {
    setGeometry(contentRect());
    if (m_pageFrame != nullptr) {
        m_pageFrame->setGeometry(rect());
    }
    if (m_promptCard == nullptr) {
        return;
    }
    // 宽度**恒定**、只夹高度：用户要的那句「不会让界面出现尺寸不一样的弹窗」就是这一条。
    // 窄一点就成了另一种弹窗，矮一点只是内容滚。
    const int width = qMin(promptWidth(), qMax(0, this->width() - 2 * promptMargin()));
    const int available = qMax(0, height() - 2 * promptMargin());
    const int wanted = qMin(m_promptCard->sizeHint().height(), available);
    // 一样就不动：这一条是**防循环**的 —— 改尺寸会让卡片里那套布局重算，而重算又会
    // 以 LayoutRequest 的形状回到上面 eventFilter 里（那边还会再叫一次 relayout）。
    if (m_promptCard->width() != width || m_promptCard->height() != wanted) {
        m_promptCard->setFixedWidth(width);
        m_promptCard->setFixedHeight(wanted);
    }
    m_promptCard->move((this->width() - m_promptCard->width()) / 2,
                       (this->height() - m_promptCard->height()) / 2);
}

int OverlayHost::execPage(QDialog *dialog, const QString &title, int ownerTab) {
    return execEmbedded(dialog, title, true, false, ownerTab);
}

void OverlayHost::setPageTitle(const QString &title) {
    if (m_titleLabel != nullptr) {
        m_titleLabel->setText(title);
    }
}

QString OverlayHost::pageTitle() const {
    // 页眉上那行字就是"现在开着的是哪一页"的**唯一出处**（`openPage()` / `execPage()`
    // 灌进来、`setPageTitle()` 改的就是它）—— 这儿不另存一份成员，存两份迟早对不上。
    return m_titleLabel != nullptr ? m_titleLabel->text() : QString();
}

int OverlayHost::execPrompt(QDialog *dialog, bool dismissOnOutsideClick) {
    return execEmbedded(dialog, QString(), false, dismissOnOutsideClick);
}

int OverlayHost::execEmbedded(QDialog *dialog, const QString &title, bool asPage,
                              bool dismissOnOutsideClick, int ownerTab) {
    if (dialog == nullptr) {
        return QDialog::Rejected;
    }

    // ① 先摘掉"窗口"身份。QDialog 只要带着 Qt::Dialog 就是**顶层窗口** —— 构造时给个
    //    parent 只是"属于谁"（Windows 上的 owner），不是"画在谁里面"。塞进布局之前必须
    //    先把它降级成普通子控件，否则它照旧另开一个窗，那正是这一整轮要消掉的东西。
    dialog->hide();
    dialog->setParent(m_parking, Qt::Widget);

    // ② 摆上去。开不出来（已经有页面/已经有提示）时就此打住：`dialog` 只是挂回了寄存处，
    //    调用方看到的是一句"取消了"，和用户自己按了取消是同一件事。
    QWidget *area = asPage ? openPage(title, ownerTab) : openPrompt(dismissOnOutsideClick);
    if (area == nullptr || area->layout() == nullptr) {
        return QDialog::Rejected;
    }
    area->layout()->addWidget(dialog);
    dialog->setVisible(true);
    // 内容到位了，卡片按真实内容再量一次（`openPrompt()` 那一次量的时候里面还是空的）。
    relayout();

    // ③ 卡住调用方，形状和 `dialog.exec()` 一模一样 —— 一百来个调用点一行一行换过去时
    //    语义一个字不变（QMessageBox::exec() 本来就是嵌套事件循环，可重入性的风险面
    //    和以前完全相同，没有变差）。
    QEventLoop loop;
    int result = QDialog::Rejected;
    QMetaObject::Connection backConnection;
    if (asPage) {
        // 返回箭头 = **reject**（不保存离开）。对话框自己那对「保存 / 取消」照旧在它身上，
        // 点它们走的是它自己的 accept()/reject()。
        backConnection = connect(this, &OverlayHost::backPressed, &loop,
                                 [dialog] { dialog->reject(); });
    }
    // ★ 这一发**只有提示那一层接**（`asPage == false`）。信号的名字和它的 doc 说的都是
    //   "最上面那条提示被点外面/按 Esc 关掉了"，而 `mousePressEvent()` / `keyPressEvent()`
    //   也只在 `m_mode == Mode::Prompt` 时才发它 —— 页面那层永远等不到这一发，接了就是接了个
    //   不该有的东西。
    //
    //   原来的形状是**两层都接**，症状是"点一下提示框外面，底下那一页跟着一起没了"：
    //   提示叠在页上的时候，页那一次的 `execEmbedded` 还卡在 `loop.exec()` 里、它那条连接
    //   还活着 ⇒ 这一发 `emit` 让两条 lambda 同时跑，页的对话框被 `reject()`。
    //   `QDialog::done()` 里**没有可见性判断**（只有一层 `QPointer` 保护），所以哪怕
    //   那一页此刻正被提示盖着、`m_pageFrame` 是收着的，照样 `finished(Rejected)` ⇒ 页那次
    //   `loop.quit()` ⇒ `execEmbedded` 返回 Rejected。用户看到的是"我只点关了一条提示，
    //   刚编辑了半天的分组设置全丢了"（GroupDialog 那条重名警告就叠在它自己这一页上）。
    //
    //   NOTE: 返回箭头那条（`backConnection`）**没有**这个问题：`backPressed` 只有那六颗
    //         按钮会发，而页面被提示盖住时页眉整个是收着的，点不到。
    QMetaObject::Connection dismissConnection;
    if (!asPage) {
        dismissConnection =
            connect(this, &OverlayHost::promptDismissed, &loop, [dialog] { dialog->reject(); });
    }
    const QMetaObject::Connection finishedConnection =
        connect(dialog, &QDialog::finished, &loop, [&result, &loop](int code) {
            result = code;
            loop.quit();
        });

    loop.exec();

    disconnect(backConnection);
    // 页面那一层压根没接（见上面那段），空连接交给 disconnect 是白白一句。
    if (dismissConnection) {
        disconnect(dismissConnection);
    }
    disconnect(finishedConnection);
    // 收**这一层**：提示收掉之后，底下压着的那一页还在（那是它的调用方在等的）。
    closeOverlay();
    return result;
}

bool OverlayHost::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_anchor) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
            relayout();
            break;
        default:
            break;
        }
    } else if (watched == m_pageContent) {
        // 整页那一档的内容长高/变矮了，滚动条要跟着重算。
        //
        // NOTE: 这一条**必须自己发**。QScrollArea 只在 `QEvent::LayoutRequest` **送到它
        //       自己身上**的时候才重算滚动条（`QScrollArea::event`），而内容容器的
        //       `updateGeometry()` 是发给它**父亲**的（视口）—— 那一步之后就没有下文了。
        //       不补这一发的话，症状是"设置页比内容区高，页脚那排按钮永远滚不到"
        //       （ui_check 的 O 段量得到：容器高 == 视口高、可滚 0）。
        if (m_pageScroll != nullptr && !m_pageScroll->isVisible()) {
            return QWidget::eventFilter(watched, event);  // 没在显示，不用算
        }
        switch (event->type()) {
        case QEvent::ChildAdded:
            // 塞进来的东西**必须能看见**。Qt 的规矩是"显式 hide 过的会一直是藏着的"，
            // 而调用方给的多半正是"上次关掉时被 hide 过"的那个对话框（「账号与外观」
            // 就是长驻的那一个）。不翻出来的症状是**页面开了、里面是空的，而且不报错** ——
            // 更阴的是布局里那个 `QWidgetItem::isEmpty()` 会因此返回 true（Qt 认的就是
            // `isHidden()` 这一位），于是这个控件连"有多高"都不算数：ui_check 的 O 段
            // 就是这么量到"容器高 494 = 视口高 494、可滚 0"的。
            //
            // ★ 必须**排队**再 show()，不能当场调 —— 这一发 ChildAdded 是在
            //   `QWidget::setParent()` 肚子里发出来的，setParent 收尾时会把"调用方显式
            //   hide 过"的那一位重新按回去，当场那一发 show() 会被它吃掉（量到的样子是
            //   `isVisible()==1 而 isHidden()==1`，两个都真）。排队到下一轮事件循环，
            //   setParent 已经走完了，这一发才作数。
            //
            // NOTE: 只管**直接子控件** —— 直系子控件就是"这一页的内容"，而页面内部自己
            //       藏起来的那些（实例设置里默认折叠的「高级选项」）是孙子，碰不到。
            if (auto *child = qobject_cast<QWidget *>(static_cast<QChildEvent *>(event)->child())) {
                QMetaObject::invokeMethod(child, [child]() { child->show(); }, Qt::QueuedConnection);
            }
            Q_FALLTHROUGH();
        case QEvent::ChildRemoved:
        case QEvent::LayoutRequest:
            if (m_pageScroll != nullptr) {
                // 排队发（不是当场调）：内容和布局还在拼，此刻算出来的尺寸是半成品。
                QApplication::postEvent(m_pageScroll, new QEvent(QEvent::LayoutRequest));
            }
            break;
        default:
            break;
        }
    } else if (watched == m_promptContent || watched == m_promptCard) {
        // 内容变了、卡片里那套布局要重算了 —— 提示那一档的高矮必须跟着走（见构造里那段
        // NOTE：`openPrompt()` 返回之后内容才进去）。`relayout()` 只在尺寸真的变了的时候
        // 才动控件，所以这一条不会自己叫自己。
        switch (event->type()) {
        case QEvent::ChildAdded:
        case QEvent::ChildRemoved:
        case QEvent::LayoutRequest:
            if (m_mode == Mode::Prompt) {
                relayout();
            }
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void OverlayHost::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    if (m_mode == Mode::Page) {
        // 整页：铺一层**不透明**的底。不是为了好看 —— 是"盖住"这件事得由像素兑现：
        // 下面那一页还在（只是被按住了），不铺的话它的字会从页框没盖到的地方露出来。
        painter.fillRect(rect(), palette().color(QPalette::Window));
        return;
    }
    if (m_mode != Mode::Prompt) {
        return;
    }
    // 提示：先铺底图（开之前拍下来的那一层），再压一层半透明黑。用户要的是"四周变暗"，
    // 不是"四周消失"。
    if (!m_backdrop.isNull()) {
        painter.drawPixmap(rect(), m_backdrop);
    } else {
        painter.fillRect(rect(), palette().color(QPalette::Window));
    }
    painter.fillRect(rect(), scrimColor());
}

void OverlayHost::mousePressEvent(QMouseEvent *event) {
    // 落在"这一层自己"身上的按下 = 点了卡片外面（卡片是子控件，点在它上面根本到不了这儿）。
    if (m_mode == Mode::Prompt && m_dismissOnOutsideClick) {
        emit promptDismissed();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void OverlayHost::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && m_mode == Mode::Prompt) {
        // Esc 是"离开"的通用键，**不问 m_dismissOnOutsideClick** —— 那一格管的是
        // "点外面"，而按 Esc 是明确表态（各对话框本来就是这么接的：Esc = reject）。
        emit promptDismissed();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
