/**
 * @file OverlayHost.hpp
 * @brief 主窗口内容区上面那一层：设置界面和各种提示都开在这里，**不再开独立窗口**。
 * @author 0x7fffff
 * @date 2026-09-25
 *
 * 【要解决的问题】
 * 到 2026-09-25 为止，这个启动器里凡是"设置"或"说一句"都是另开一个窗口（五个 QDialog
 * 子类 + 一百来处 QMessageBox）。用户给的方向是：**最终不出现独立窗口**（游戏日志窗除外），
 * 也就是
 *
 *   - 「实例设置」这种**一整个界面**的 → 把内容区整块覆盖掉，左上角给一颗返回箭头；
 *   - 「Java 版本对不上」「游戏还在运行」这种**小提示** → 在内容区中间开一块，四周变暗；
 *   - 而且**不许出现尺寸不一样的弹窗**（一会儿大一会儿小最难受）。
 *
 * 【这块地方在哪儿】
 * 主窗口 `centralWidget` 的纵向布局是 `[顶上那一条][tabWidget(stretch 1)][底栏一条]`
 * （见 MainWindowUI::setupUi）。所以"内容区"就是**那个 QTabWidget 里当前页占的那块**，
 * 覆盖层挂在它上面 —— **顶上那一条和底栏都露着**，也就是用户点名的那句
 * 「底下那一条不用覆盖」。
 *
 * NOTE: 用户原话里还有半句「顶上四个选项卡也不覆盖」——2026-09-25 起那四个选项卡已经
 *       **搬到顶上的条里**（`tabWidget->tabBar()->hide()` + 条上四颗导航按钮，见
 *       TitleBar.hpp 文件头），所以"露着"的那一份从标签栏换成了那条，规矩没变：
 *       四个功能始终点得到。
 *
 * 【为什么是"锚点的兄弟 + eventFilter 现算几何"，不是布局】
 * 覆盖层不能参与 `mainLayout` —— 一进布局就会把 `tabWidget` 挤走（那是"分成上下两半"，
 * 不是"浮在上面"）。它是锚点的兄弟、不归任何布局管，所以只能自己盯住锚点的
 * Resize/Move 重算几何（见 eventFilter）。这是这个类唯一一处"位置从哪来"的逻辑。
 *
 * 【两层：页面是一层，提示是另一层】
 * 提示可以**压在页面上面** —— 这一点必须成立：设置页开着的时候，后台一样会冒出来
 * "下载失败"这种话，而真窗口那会儿它是一个模态窗叠在另一个上面。所以状态是两台戏：
 * `m_mode` 说"现在显示的是哪一层"，`m_overPage` 说"提示那一档底下是不是还压着一页"。
 * `closeOverlay()` 收的是**最上面那一层**：提示收掉之后，底下那页还在。
 *
 * 【一层只能有一个页面】
 * `openPage()` 在已经有页面开着的时候返回 **nullptr**（提示那一档重叠同理，见
 * `ask()`）。今天能走到这儿的只有一条路：页面开着的时候又点了底栏那些**没被盖住**的
 * 按钮。真窗口那会儿两个窗是叠着开的，这里不做叠（那是"页面栈"，是另一个东西）。
 *
 * 【顶上那四颗导航按钮：切页 = 把这一层"收着"，不是关掉】
 * 用户原话：「顶端四个按钮点击以后自动到达对应选项卡的首页（比如说我现在正点开 sodium 的
 * 下载页面，现在我点击启动选项卡，直接切到那个页面，下载这个"弹窗"自动收起）」，以及
 * 「保留原来那个子页的状态，比如说我正在看 sodium 的下载，现在点击 启动，切到启动页，
 * 再点击 下载，那就回到下载 sodium 界面，在下载页点击 下载（顶上那个，不是下载确认键），
 * 那就回到下载页的首页（四个都适用）（包括向下滑动了多少也保留）」。
 *
 * 于是有两件事，性质完全不同：
 *
 * ① **切到别的选项卡 = 收着**（`parked()`）。整页那一档只在**开它的那颗选项卡**上露面，
 *    用户在别的选项卡上逛的时候它整个人是藏起来的 —— 不销毁、不 `reject`：这一页正卡在
 *    `execPage()` 的嵌套事件循环里，收掉了调用方会一直等下去，而"回到下载 sodium"这件事
 *    也就没了（滚到哪儿、勾了哪颗 chip 全在内存里活着）。切回来原样摆出来。
 * ② **点的就是现在这一颗 = 回到这个功能的首页**（`dismiss()`）。发的是"返回箭头"那一发/
 *    "Esc"那一发，和用户自己点它们一模一样（见 `dismiss()` 的注释：换成 `closeOverlay()`
 *    会让 `execPage()` 的调用方永远等下去）。
 *
 * NOTE: 提示那一档（`Mode::Prompt`）**不跟着选项卡收**。它是个要人回答的问题 ——
 *       收起来等于让调用方在用户看不见的地方干等，而且 `InlineMessage::ask()` 那些
 *       本来就靠"看得见才答得上"。用户切页时它只是**换一张底图**（见 refreshBackdrop）。
 *
 * NOTE: 一层还是只放得下一个页面：页面**收着**的时候（用户在别的选项卡上）再要一页照样
 *       是 nullptr —— 那一位没被释放。收着的页面看不见，这一点比以前更难察觉，记在这儿。
 */

#pragma once

#include <QColor>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>

class QDialog;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class OverlayHost : public QWidget {
    Q_OBJECT

public:
    /// 现在**显示在最上面**的是哪一种。**没有别处的状态**：这个枚举加下面那个
    /// `m_overPage` 就是"开着什么、怎么叠着"的全部。
    enum class Mode {
        None,
        /// 整页：铺满内容区，顶上一条「← 返回 + 标题」的页眉。
        Page,
        /// 提示：中间一块**宽度固定**的框，四周变暗。
        Prompt,
    };

    /**
     * @brief 构造。
     * @param anchor 内容区的界标 —— 主窗口那个 QTabWidget。覆盖层的几何每次都由它现算；
     *        它同时也是"覆盖层开着时要把哪一页按住不动"的出处（见 retargetCoveredPage()）。
     * @param parent 覆盖层挂在哪 —— 传 `centralWidget`（锚点的父控件）。
     */
    explicit OverlayHost(QWidget *anchor, QWidget *parent = nullptr);

    Mode mode() const { return m_mode; }
    bool isOpen() const { return m_mode != Mode::None; }
    bool pageOpen() const { return m_mode == Mode::Page; }
    bool promptOpen() const { return m_mode == Mode::Prompt; }

    /// 整页那一档**收着**了：开着，但用户现在在**别的选项卡**上，所以它不露面。
    /// **收着不是关了** —— 页对象、滚到哪儿、页上那些控件的状态一样不少（见头文件那段）。
    /// 提示那一档没有"收着"这回事（恒 false）。
    bool parked() const;

    /// 内容区（顶上那一条以下、底栏以上）在**本控件父窗口坐标系**里的矩形。
    /// 公开是给验证工程量的：覆盖层到底盖住了哪一块，只有这一个出处。
    QRect contentRect() const;

    /**
     * @brief 从某个控件往上找它现在所在的覆盖层；不在覆盖层里就返回 nullptr。
     *
     * 给**自己也是"一页"的那些对话框**用的（GroupDialog 就是）：它们要弹一句提示的时候
     * 手里只有 `this`，没有 MainWindow，也就够不着 `m_ui->overlay()`。而它们此时的
     * 父控件链正是 `页内容容器 → 滚动区 → 那一层 → centralWidget`，所以往上走就能找到
     * —— "这一页下面是谁在铺"这件事本来就只有一条路。
     *
     * NOTE: 找不到（独立跑某个对话框、或者还没挂上去）返回 nullptr，`InlineMessage::ask`
     *       会写一行日志并返回"什么都没选"（见那边）。**不要**在这里回退成开一个独立窗 ——
     *       那正是这一整套要消掉的东西。
     */
    static OverlayHost *forWidget(QWidget *widget);

    // ---------- 两种打开方式 ----------

    /**
     * @brief 打开一个"整页"视图：返回**内容容器**，调用方把自己那块东西塞进它的布局。
     *        页眉（返回箭头 + 标题）由覆盖层自己搭 —— 每个页面长得一样是要求的另一半。
     * @param ownerTab 这一页**归哪一颗选项卡**（它只在那一颗上露面，别的时候收着）。
     *        -1 = 现在这一颗（绝大多数调用方要的就是这个）。**不是 -1 的时候调用方
     *        有责任先把选项卡切过去**，否则这一页一开出来就是"收着"的 —— 用户按了
     *        一颗按钮，屏幕上什么都不出现。
     *
     * ★ 为什么要让调用方说得出口（2026-09-28）：页面归属默认取"开页那一刻的当前
     *   选项卡"，而有些页面**有自己的家**，跟用户从哪儿进的无关 —— 「实例设置」就是
     *   一例：从下载页那颗「← 返回实例设置」进来时当前选项卡是**下载**，于是那一页
     *   被记成下载页的页面。用户随后点「实例」那颗导航：它**收着**（看不见），而它占着
     *   覆盖层那一位 ⇒ 此后每次开实例设置都被"已经有一页开着"挡下，用户看到的是
     *   **点了没反应、还老被拽到下载页**（他得回下载页才找得回那一页）。
     *
     * @return 内容容器；**已经有一页开着的时候返回 nullptr**（这一层不做页面栈，
     *         调用方把 nullptr 当"没开成"处理）。
     */
    QWidget *openPage(const QString &title, int ownerTab = -1);

    /**
     * @brief 打开一个"提示"：返回**内容容器**（摆在中间那张卡片里），四周变暗。
     *        底下压着一页的时候，暗的是那一页。
     * @param dismissOnOutsideClick 点卡片外面算不算"关掉它"。**默认 false** ——
     *        "要不要停游戏"这种问话误点一下外面就当没问过，是危险的默认值。
     *        纯报信的那些传 true。（按 Esc 一律算关掉，不看这一格 —— 见 keyPressEvent。）
     * @return 内容容器；**提示叠提示的时候返回 nullptr**（见 `ask()`）。
     */
    QWidget *openPrompt(bool dismissOnOutsideClick = false);

    /**
     * @brief ★ 2026-09-27 加：**提示开着的时候要按住的那一块**（现在的调用方是底栏）。
     *
     * 为什么不能只靠"被盖住的那一页禁用"那一条：底栏**不在这一层底下** —— 覆盖层挂在
     * tabWidget 上（见 MainWindowUI 里那一句），底栏是它的兄弟、比它靠下，所以
     * `setEnabled(false)` 那条路（retargetCoveredPage）管不到它，它也不像被盖住的那一页
     * 那样"上面盖着一张底图、变灰看不见"。
     *
     * 不按住的症状是**静默失灵**：提示开着时底栏那两颗按钮照样点得开上拉面板，面板里点
     * 「改设置」→ openInstanceSettings() → overlayAcceptsNewPage() 被这一层挡住 → 那句
     * 解释为什么的话又要走 InlineMessage::ask()，而"提示叠提示不做"让它**直接返回 -1**
     * （只写一行 qWarning）⇒ 用户点了一下，屏幕上**什么都没发生**、也没有一个字解释。
     *
     * 只有**提示**那一档按（`Mode::Prompt`）：整页那一档底栏是用户要用的东西（停游戏那
     * 一行电源就在左面板里，见 ui/MainWindowUI 里那两块面板）。
     *
     * @param widget 要跟着提示一起禁用的控件（可为 nullptr = 不要这一档）。
     */
    void setPromptBlockedWidget(QWidget *widget);

    /**
     * @brief 收掉**最上面那一层**。
     *
     * - 提示那一档：提示收掉；底下压着页面的话**回到那一页**（页面还开着）；
     * - 页面那一档：整块收掉，回到没有覆盖层的状态。
     *
     * **内容容器里的东西一律"还回去"**（不删）—— 调用方那些对话框是栈对象，必须让它在
     * 自己作用域里正常析构。
     */
    void closeOverlay();

    /**
     * @brief 「回到这个功能的首页」：收掉最上面那一层，**当作用户自己按了返回箭头 / 按了
     *        Esc**。顶上那四颗导航按钮在"点的就是现在这一颗"的时候叫它（见头文件那段）。
     *
     * 为什么不直接 `closeOverlay()`：用 `execPage()` 开出来的那一页正卡在自己的嵌套事件
     * 循环里（`MainWindow::openResourceDetailPage` 就是这么等的），`closeOverlay()` 只把
     * 外壳收了，那一页一个信号都不发 ⇒ 调用方一直等下去、`m_resourcePage.open` 永远归不了
     * false。发 `backPressed()` 才是"点返回箭头"那一发（`execEmbedded` 接它去 `reject()`），
     * 提示那一档同理走 `promptDismissed()`。
     *
     * @return 真的收掉了才 true。**收着的页面**（用户现在在别的选项卡上）返回 false ——
     *         那一层他根本看不见，这一发不该落在它头上（不然是"我在主页点了一下主页，
     *         下载页那个详情页没了"）。
     */
    bool dismiss();

    /**
     * @brief 把 `dialog` 当"整页"跑起来，等到它 accept/reject（或者点了返回箭头）为止。
     *
     * 这是 `dialog.exec()` 的替身：原来那句开一个独立窗口，这句把同一个对话框**嵌进**
     * 内容区。之所以保留"卡住调用方"的形状（内部一个 QEventLoop），是为了让一百来个
     * 调用点一行一行地换过去时**语义一个字不变** —— QMessageBox::exec() 本来就是这么干的，
     * 所以可重入性的风险面和以前一模一样，没有变差。
     *
     * @param ownerTab 这一页归哪一颗选项卡，见 `openPage()` —— 传了非 -1 的调用方
     *        必须**先把选项卡切过去**。
     *
     * @return 和 `QDialog::exec()` 同义：Accepted / Rejected。开不出来（已经有页面了）
     *         也返回 Rejected —— 什么都没发生，和"用户取消了"对调用方是同一件事。
     * @note 返回箭头 = **reject**（不保存离开）。对话框自己那对按钮照旧在它自己身上。
     */
    int execPage(QDialog *dialog, const QString &title, int ownerTab = -1);

    /**
     * @brief 换掉页眉上那行标题（页面已经开着的时候用）。
     *
     * 开页那一刻标题是定死的（`openPage()` / `execPage()` 收的那一个），可**页面上那件事
     * 物自己会改名**：实例设置页里那一格改的就是实例名，改完之后页眉上那行字要是还写着
     * 老名字，用户看到的是"最高处写着一个已经不存在的实例，底下那一格却是新的"。
     *
     * NOTE: 只改字，不动几何（页眉的高度是定死的，标题长了也是它自己截断 —— 和开页那
     *       一刻完全一样）。
     */
    void setPageTitle(const QString &title);

    /// 页眉上现在那行标题（没有页时是空串）。
    ///
    /// 给**挡在门口的那一句提示**用的：用户点了个开不起来的页，那句话要说清"现在开着的是
    /// 哪一页"（见 `MainWindow::overlayAcceptsNewPage()`）—— 光说"还有一页开着"，用户在
    /// 别的选项卡上根本看不见它长什么样。
    /// NOTE: 定义在 .cpp 里（不在这儿就地写）：这一句要问 `QLabel::text()`，而这一层
    ///       只用得到 QLabel 这个名字（见上面那个前置声明）—— 就地写就得把 <QLabel>
    ///       拖进每一个用到覆盖层的地方，为一行取值不值得。
    QString pageTitle() const;

    /// 同上，但按"提示"摆（中间 + 四周变暗）。
    int execPrompt(QDialog *dialog, bool dismissOnOutsideClick = false);

    // ---------- objectName / 尺寸常量（验证工程靠它们认控件、量几何） ----------

    static QString overlayHostName() { return QStringLiteral("overlayHost"); }
    static QString overlayBackButtonName() { return QStringLiteral("overlayBackButton"); }
    static QString overlayTitleLabelName() { return QStringLiteral("overlayTitleLabel"); }
    /// 整页那一档装内容的容器（外面还套着滚动区）。
    static QString overlayPageContentName() { return QStringLiteral("overlayPageContent"); }
    /// 提示那一档装内容的容器（在中间那张卡片里）。
    static QString overlayPromptContentName() { return QStringLiteral("overlayPromptContent"); }
    static QString overlayPromptHostName() { return QStringLiteral("overlayPromptHost"); }
    static QString overlayPageScrollName() { return QStringLiteral("overlayPageScroll"); }

    /// 返回箭头的字。**带箭头的**（用户原话「左上角有返回箭头」），后面缀一个「返回」
    /// 是为了不作猜谜 —— 光一个 ← 在桌面软件里并不通用。
    static QString backButtonText() { return QStringLiteral("← 返回"); }

    /// 提示卡片的宽度。**唯一出处** —— 用户要的「不会让界面出现尺寸不一样的弹窗」就是
    /// 这一条：所有提示一样宽，只有高矮随字数走。
    static int promptWidth() { return 480; }

    /// 卡片里面那圈留白。
    static int promptPadding() { return 18; }

    /// 卡片里**内容**能用多宽 = 卡片宽 - 左右留白。
    /// 提示面板要拿它给自己的正文标签钉宽度：钉了之后"这段字换几行"就是一个确定的数，
    /// 卡片的 `sizeHint().height()` 才是准的（不钉的话得指望 heightForWidth 一路传上来，
    /// 那条路在嵌套布局里是会失手的那种）。
    static int promptContentWidth() { return promptWidth() - 2 * promptPadding(); }

    /// 提示框离内容区边缘至少留这么多（高度不够时先夹高度，不换宽度）。
    static int promptMargin() { return 24; }

    /// 页眉的高度（返回箭头那一行）。字号跟着系统默认字体走，所以这里只定高。
    static int pageHeaderHeight() { return 44; }

    /// 四周变暗那一层的颜色。半透明黑：**不写死具体灰**，盖在什么底色上都成立。
    static QColor scrimColor() { return QColor(0, 0, 0, 96); }

signals:
    /// 返回箭头被点了。`execPage()` 把它接到 reject 上；自己搭页面的调用方直接接。
    void backPressed();

    /// 提示被"点外面 / 按 Esc"关掉了（只有 `dismissOnOutsideClick` 那一档会发）。
    ///
    /// **只有提示那一层（`execPrompt` / `openPrompt`）该接它**：发这一发的两处
    /// （`mousePressEvent` / `keyPressEvent`）都钉着 `m_mode == Mode::Prompt`，页面那层
    /// 永远等不到。`execEmbedded` 早先两层都接，于是"点提示框外面 = 连底下那一页一起
    /// reject"（页面那一次的 `execEmbedded` 还卡在自己的事件循环里、连接还活着）。详见
    /// 实现里那段 ★ 注释。
    void promptDismissed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    /// 把尺寸/位置按当前模式摆一遍。锚点动了、自己开了关了，都走这一处。
    void relayout();
    /// 把"现在该露哪一层"摆一遍（宿主 / 页框 / 卡片三者的可见性 + 按住 + 几何）。
    /// **开、关、切选项卡都走这一处** —— 可见性只有这一个出处。
    void applyVisibility();
    /// 锚点换了一页（顶上那四颗导航按钮点了、或者别处翻的页）。
    void onAnchorTabChanged();
    /// 提示那一档换底图（锚点换页了、或者底下那一页收起来了）。
    void refreshBackdrop();
    /// 现在该拍的"底下那一块"是哪一块（提示那一档要变暗的东西）。**唯一出处**：
    /// `openPrompt()` 开的那一下和 `refreshBackdrop()` 换的那一下都走它 —— 两处各写一遍
    /// 的话，"页收着"这一档迟早会在其中一处漏掉（2026-09-27 之前 refreshBackdrop 就是这么错的）。
    QPixmap captureBackdrop() const;
    /// 开页那一刻的选项卡下标（锚点不是 QTabWidget 时 -1）。整页那一档只在它上面露面。
    int anchorTabIndex() const;
    /// 现在是不是在**开这一页的那颗选项卡**上（锚点不是 QTabWidget / 手上没有页面时恒真）。
    bool onOwnersTab() const;
    /// 覆盖层开着时，把"被盖住的那一页"按住（见实现里那段 NOTE）；关掉时放回去。
    void retargetCoveredPage();
    /// 把某个内容容器里装的东西**还回去**（不删）：见 closeOverlay 的 NOTE。
    void releaseContent(QVBoxLayout *content);
    /// 当前被盖住的那一页（锚点是 QTabWidget 时才有）。
    QWidget *coveredPage() const;
    /// 放掉"按住"的那一页。
    void releaseCoveredPage();
    /// `execPage` / `execPrompt` 共用的那一段（见实现）。
    int execEmbedded(QDialog *dialog, const QString &title, bool asPage, bool dismissOnOutside,
                     int ownerTab = -1);

    QWidget *m_anchor = nullptr;
    Mode m_mode = Mode::None;

    /// 整页那一档是哪一颗选项卡开出来的（`openPage()` 记的）。收着/露面就靠它和
    /// 锚点现在这一页比（见 onOwnersTab）。没有页面的时候是 -1。
    int m_pageOwnerTab = -1;

    /// 收起来那一刻焦点在页里哪个控件上（见 applyVisibility 里那段 NOTE）。
    /// QPointer：那一页关掉时里面那些控件是被"还回去"的（不删），但下一轮就可能真没了。
    QPointer<QWidget> m_pageFocus;

    /// 提示那一档：底下是不是还压着一页（`closeOverlay()` 靠它决定收完回到哪儿）。
    bool m_overPage = false;

    // 页眉那一套是**常驻**的（建一次，开关只切可见性）：每开一次建一套控件，
    // 验证工程就得重新 findChild 一次，而"页眉长得一样"这件事也没人保证了。
    QWidget *m_pageFrame = nullptr;
    QVBoxLayout *m_pageLayout = nullptr;
    QPushButton *m_backButton = nullptr;
    QLabel *m_titleLabel = nullptr;
    /// 整页那一档的滚动区。**必须有一个**：设置页是按它自己的尺寸设计的（实例设置
    /// 要 600x480），而内容区是主窗口说了算 —— 窗口一小，"确定"就被切在下面点不着了。
    QScrollArea *m_pageScroll = nullptr;
    /// 整页那一档装内容的容器。
    QWidget *m_pageContent = nullptr;
    QVBoxLayout *m_pageContentLayout = nullptr;

    /// 提示那张卡片（常驻，同上）。卡片自己画底和边 —— 于是"嵌进来的对话框"和
    /// "我们自己搭的提示面板"看起来是同一张卡。
    QWidget *m_promptCard = nullptr;
    QVBoxLayout *m_promptLayout = nullptr;
    /// 提示那一档装内容的容器。**和整页那一档是两个**：提示叠在页面上的时候，两边的
    /// 东西都得留在原地（共用一份的话，"把页面的内容搬进提示卡片"就把它从页面上摘走了）。
    QWidget *m_promptContent = nullptr;
    QVBoxLayout *m_promptContentLayout = nullptr;
    bool m_dismissOnOutsideClick = false;

    /// 提示开着时铺在最底下那张"开之前拍下来的样子"：底下是内容页时拍那一页，底下是
    /// 整页覆盖层时拍那一层。见 openPrompt 里那段 NOTE（覆盖层和内容页是**兄弟**，
    /// 半透明画一层合的是父控件的底，不是页面的像素）。
    QPixmap m_backdrop;

    /// 被盖住、暂时按住的页（QPointer：页面被换掉时别留下悬空的）。
    QPointer<QWidget> m_disabledPage;

    /// ★ 2026-09-27 加：提示开着时要跟着按住的控件（底栏）。见 setPromptBlockedWidget()。
    /// QPointer：底栏是 MainWindowUI 的成员，先于这一层析构的话别留下悬空指针。
    QPointer<QWidget> m_promptBlockedWidget;

    /// 还回来的内容先寄存在这儿：一个藏起来的空控件。**不能直接 setParent(nullptr)** ——
    /// 那会让它变回顶层窗口（Qt 里"没有父控件的 QWidget 就是窗口"），哪怕它一直是 hide 的，
    /// 也在"这一下会不会闪一下"这种事上赌运气。寄存到这儿就永远不会冒出来。
    QWidget *m_parking = nullptr;
};
