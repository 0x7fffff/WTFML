/**
 * @file VersionTreeDelegate.hpp
 * @brief 树上每一行长什么样：前面一个小图标 + 正文 + 下面那行小字 + 最右边的 >/V。
 * @note 为什么是代理（delegate）而不是"给每个条目塞一个自定义控件"：
 * 树里是 900 多条（正式版 103 + 快照 700+ + …），`setItemWidget` 那条路要给
 * 每一条都建一个真的 QWidget，光建起来就够卡；代理是**画**出来的，条目还是
 * QTreeWidgetItem，`UserRole` 里那个身份、展开状态、点击那条路全都原样不变。
 *
 * 这一层只管"画"：要知道的都在条目上（`DisplayRole` = 正文、`rowSubTextRole()` =
 * 第二行小字、`rowIconRole()` = 前面那个图标），一个业务判断都不做。
 *
 * **两棵树共用它**（版本树 + 安装页那棵加载器树）：条目给了 `rowIconRole()` 就画它
 * （没给时"版本号前面是草方块"那条规矩照旧），第二行小字两种行**都**看
 * `rowSubTextRole()`（空串就不画）。
 * 两棵树只有**一处**不一样：叶子那一层悬停时行尾写不写动词（见构造函数那个开关）。
 */

#pragma once

#include <QStyledItemDelegate>

class QPainter;

/// 版本树那棵树上挂的代理。
class VersionTreeDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    /// @param parent 视图（QTreeWidget / QTreeView）。
    /// @param leafArrow **叶子那一层**悬停时行尾写不写动词（`leafVerb()`；能展开的行
    /// 不受它管 —— 那些照写「展开 / 收起」）。不画任何箭头（体验反馈 8）。
    /// · 版本树：`true` —— 叶子是**版本号**，点它进安装页，悬停写「安装」；
    /// · 安装页那棵加载器树：`false` —— 叶子是那两档和具体版本号，点它们是**选中**、
    /// 底下没有下一页，行尾什么都不写。
    /// @param iconSize 前面那个图标画多大（像素，正方形）。**0 = 用默认值**
    /// （`MainWindowUI::versionTreeIconSize()`，18 px）—— 版本树和加载器树都用这个
    /// 默认值。下载页那几张表要的是"能看清的缩略图"，会显式传一个大一点的数。
    /// @param navRow 这一列是不是**侧栏那一列导航**。
    /// **默认 false** —— 版本树、加载器树、四张资源表、服务器那一列全都不传。
    /// · `false`：选中那一行是**整条行**铺满（底色由样式那两条原语画成"这一行自己的
    /// 底色混一档主题色"，不画指示条；墨跟着换成 `selectionPalette()`
    /// 那一份）。分支区那一截由本代理保底补涂（见 .cpp）。
    /// · `true`：**导航**（下载页左栏那六条分类、设置页左栏那五条）—— 色和上面
    /// 同一支（`Theme::selectionFill()/Ink()`），**形状**不同：内缩圆角块。
    /// 浅主题色背景 + 主题色深色文字，不画指示条；列表那一档也用同一支主题色。
    /// 为什么这一档只能由代理自己铺、不能写成 Qt 样式表里那几条 `::item` 规则：
    /// 这两个列表用的是**这个代理**，整行都是它
    /// 自己画的 ⇒ 样式表里 `QListView[wtfmlNav="sidebar"]::item:selected` 一条都
    /// 匹配不上那层底色（画出来的是我们这一支）。色本身仍然只有 Theme 那一处出处。
    explicit VersionTreeDelegate(QObject *parent = nullptr, bool leafArrow = true,
                                 int iconSize = 0, bool navRow = false);

    /// 叶子那一层悬停写不写动词（见构造函数）。验证工程用它钉住**产品那边真的传了 false**：
    /// 只验"传 false 就不写动词"是不够的，那条路自己也能通过。
    bool leafArrow() const { return m_leafArrow; }
    /// 叶子行悬停时行尾写的动词（默认「安装」：版本树点版本号进安装页）。
    void setLeafVerb(const QString &verb) { m_leafVerb = verb; }
    QString leafVerb() const { return m_leafVerb; }

    /// 这一列是不是"侧栏导航"那一档（见构造函数）。验证工程同样要能钉住**产品那边真的
    /// 开了这一档**（只验"开了就画成主题色"不成立 —— 那条路自己也能通过）。
    bool navRow() const { return m_navRow; }

    /// 这一行前面那个图标实际画多大（0 表示"用默认那个常数"，见构造函数）。
    int iconSize() const { return m_iconSize; }

    /// 这一行该按多大的图标留位置（**唯一出处**：paint 和 sizeHint 都读它，不然"画出来了"
    /// 和"留了几格"会各算各的，症状是图标压着文字或者右边多出一块空）。
    int rowIconSize() const;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /// 行尾那个「取消」小叉**占哪一格**（`row` 是整行的矩形，视口坐标）。只有条目上
    /// `rowClearRole()` 为真的行才画它、也只有那种行才有意义。
    ///
    /// NOTE: 这是**唯一出处** —— paint() 按它画、MainWindow 那边按它判断"这一下点中的
    /// 是不是那个叉"（见 MainWindow::eventFilter）。两边各算一份的话迟早错开，
    /// 而错开的形状是"看着点中了叉、其实点的是那一行"（那一行点一下是展开/收起，
    /// 于是用户以为取消了、其实只是把那一列收了起来）。
    static QRect clearRect(const QRect &row);

    /// 行尾那个**刷新按钮**占哪一格（`row` 是整行的矩形，视口坐标）。只有条目上
    /// `rowRefreshRole()` 为真的行才画它、也只有那种行才有意义。
    ///
    /// 下载页左栏那几个分类条目在用。
    /// 和 clearRect() 一样，**这一格也是唯一出处** —— paint() 按它画、MainWindow 那边
    /// 按它判断"这一下点中的是不是那个按钮"（见 MainWindow::eventFilter）。
    static QRect refreshRect(const QRect &row);

    /// 行尾那格**「展开 / 收起」文字**占哪一格（`row` 是整行的矩形，视口坐标）。
    ///
    /// 和上面两个不一样：这一格是代理**替 Qt 画的**（`isChannel || m_leafArrow` 那两档），
    /// 所以它一直在那儿 —— 定位只有这个函数一处。**下载页那四张资源表要按它认命中**：
    /// 那一层里"点行身"是"打开这个资源的详情页"、"点那格文字"才是"展开/收起"
    /// （见 MainWindow::eventFilter 里那一段）。
    ///
    /// NOTE: 返回的是**命中格**，比画出来的那格文字四周各宽 3 px（和 clearRect /
    /// refreshRect 一个规矩）—— 12 px 的点击目标太小了。paint() 按它画（居中），
    /// 所以"画出来的"和"点得到的"永远是同一处。
    ///
    /// NOTE: 代理不给**没有孩子的顶层行**写字（`isChannel && !hasChildren` 那一条直接
    /// return）。这一格在这些行上依然是"留出来的那块地方"—— 文字本来就没铺到那儿
    /// （paint() 里 textRight 一直扣着它），所以在那一格上按下算"点展开格"是自洽的。
    static QRect arrowRect(const QRect &row);

    /// 光标落在**行里哪个按钮**上 —— 落在就说那句话（`rowRefreshTipText()` /
    /// `rowClearTipText()`），没落在任何一个上就是空串。
    ///
    /// @param index 那一行的下标（角色挂在它上面）。
    /// @param row 整行的矩形（视口坐标）。
    /// @param pos 光标位置（**视口坐标**，和 `row` 同一套）。
    ///
    /// NOTE: 这是"行里的按钮"这套东西的**第二处几何读者**（第一处是 paint()、第三处是
    /// MainWindow::eventFilter 里认下那一下的判据）。将来再加行内按钮，几何仍然只在
    /// clearRect()/refreshRect() 里算，这儿只是"哪个角色对应哪一格"的一张表。
    ///
    /// Why 不走 `Qt::ToolTipRole`：那个是**整条行**的，鼠标扫过一列版本号会一路弹。
    /// 这条路只在"真落在那一小格上"才说话。条目自己写 `Qt::ToolTipRole` 的那条路还在
    /// —— helpEvent() 兜底给基类。
    static QString rowActionTip(const QModelIndex &index, const QRect &row, const QPoint &pos);

    /// 悬停提示。基类那个只看 `Qt::ToolTipRole`（整条行一句），这里先问一声
    /// rowActionTip()，问不到才交给基类。
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;

private:
    bool m_leafArrow = true;
    int m_iconSize = 0;
    bool m_navRow = false;
    QString m_leafVerb;
};
