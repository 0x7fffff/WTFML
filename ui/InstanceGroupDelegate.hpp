/**
 * @file InstanceGroupDelegate.hpp
 * @brief 实例页那张树里**分组头**那一行长什么样：粗体组名 +（右边）一颗齿轮 + 一个箭头。
 * @author 0x7fffff
 * @date 2026-09-25
 * @note 【为什么单独一个代理，不复用 VersionTreeDelegate】
 *
 *       VersionTreeDelegate 是"两行字 + 前面一个图标 + 右边一个箭头（带两个可选装饰）"那
 *       一族的画法，它的判据里"顶层 = 分类"（粗体、无图标、画箭头）是**按那四棵树**立的。
 *       而实例页这张树上顶层有两种东西：有分组时顶层是**组头**、没分组时顶层是**实例**
 *       （平铺，见 fillInstanceTree）—— 照那套判据画，平铺那一档每个实例都会变成粗体
 *       加箭头。所以这一层反过来判：**只有挂着 groupIdRole() 的行**才按"组行"画，
 *       其余（实例那几行）原地交给基类，一个像素都不动。
 *
 *       这一层只管"画"：要知道的都在条目上（`DisplayRole` = 「组名 (N)」、
 *       `groupIdRole()` 说明它是组行、`rowGroupGearRole()` 说明这一行有齿轮）。
 *       业务判断（这一组里几个实例、能不能改名）不在这儿。
 *
 *       【行里的按钮：几何的唯一出处就是下面那几个 static】
 *
 *       和 VersionTreeDelegate 的 clearRect()/refreshRect() 一个规矩：代理按它画、
 *       `MainWindow::eventFilter` 按它认下那一下、`helpEvent()` 按它决定弹不弹话。
 *       三处各算一份的话，错开的形状是"看着点中了齿轮、其实点的是那一行"（那一行点一下
 *       是展开/收起）。行尾从右往左排：展开箭头 / 齿轮。
 *
 *       【2026-09-25：那两颗「上移 / 下移」搬走了】
 *
 *       原来齿轮左边还有两颗实心三角（上下移这一组）。用户当天的原话：「这个点击一下就
 *       同时展开和调整了，其次这个箭头不应该在这里，而应该在 分组设置 里面，这里的删了，
 *       去分组设置里面加上，设置那个框下方的上移下移的按钮就删了」。于是这一层只剩齿轮，
 *       那两颗画在 `ui/GroupListDelegate` 里（那张表里行序就是组序，"能不能挪"是算出来的，
 *       不像这张树要写角色位）。
 *
 *       【2026-09-25 晚：实例行也在行尾长出一排三颗按钮】
 *
 *       以前这一层**只管组行**，实例行原样交给基类（一个像素都不画）。用户当天原话：
 *       「实例页面，实例设置也是放在每一条的右侧，鼠标悬停时右侧显示上箭头和垃圾桶，
 *       分别对应 置顶和删除实例，删除前需要弹窗提示，直接点空白区域（和左边图标版本名）
 *       进入实例设置」，追加答复「要，一颗齿轮常显」。于是**一行三颗，右起**：
 *
 *         [垃圾桶] [上箭头] [齿轮]      ← 最右边那颗是齿轮（定死，见 MainWindowUI 那段
 *           悬停     悬停    常显          注释里"为什么齿轮在最右边"那半）
 *
 *       * 齿轮**常显** = 打开这一条实例的设置 —— 和"点这一行任何别的地方"是同一件事，
 *         它只是给那件事一个看得见的入口；
 *       * 上箭头**悬停才画、且只在 `rowPinRole()` 为真的行上画** = 置顶。已经在置顶组里
 *         的那一条挪不动 ⇒ **不画**，不是画一颗灰的（"挪不动的那侧不画"这条先例在
 *         `ui/GroupListDelegate` 里）；
 *       * 垃圾桶**悬停才画** = 删除这一条实例（点下去还要过一次确认框，那一下由
 *         `MainWindow::eventFilter` 接）。
 *
 *       NOTE: 三颗按钮**没有**变成控件，还是"画出来的一格"—— 这一页跟下载页一样，
 *             行里的动作一律走 `MainWindow::eventFilter` 认坐标（见 MainWindowUI 里
 *             那段"这里不做右键菜单"的 NOTE）。
 *       NOTE: 组行那一半**一个像素都没动**（paint() 里两条分支各走各的），实例行那三颗
 *             是另起的一段；两处的几何互不借用。
 *
 *       【行高 = 版本树那个常数】
 *
 *       sizeHint() 对**每一行**（组行和实例行都算）都返回 versionTreeRowHeight()：
 *       用户 2026-09-25 原话「每个版本啥的上下高一点，像下载页那样」。所以"这一行里
 *       画得下多大的东西"和下载页是同一把尺子。
 */

#pragma once

#include <QStyledItemDelegate>

class QPainter;

/// 实例页那张树上挂的代理：只接管分组头那几行，实例行原样走基类。
class InstanceGroupDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit InstanceGroupDelegate(QObject *parent = nullptr);

    /// 行尾那颗**齿轮**的**命中格**占哪一格（`row` 是整行的矩形，视口坐标）。只有条目上
    /// `rowGroupGearRole()` 为真的行才画它、也只有那种行才有意义。
    ///
    /// NOTE: 形状和 `VersionTreeDelegate::clearRect()`（那个「取消」小叉）**逐字一样**：
    ///       命中格比画出来的字形大一圈（四周各留 3 px），位置是"紧挨着右边那个箭头
    ///       左边、中间留 6 px"。两处的差别只有边长来源（那边是 `versionTreeArrowSize()`，
    ///       这边是 `instanceGroupGearSize()`）。
    /// NOTE: 这一格**允许比一行高**（实例树的行高就是那一行字那么高）：它是"点得到的那一
    ///       圈"，不是画出来的那一圈。冒出去的那几点点不到别的东西 —— 认下那一下的是
    ///       `MainWindow::eventFilter`，而它先要求 `itemAt()` 落在这条行里。
    static QRect gearRect(const QRect &row);

    /// 那颗齿轮**画出来**的那一圈（比 gearRect() 小：那个是命中格，四周各留 3 px 余量）。
    ///
    /// NOTE: 它一定装在行高里面 —— 边长取 `min(instanceGroupGearSize(), 行高)`。2026-09-25
    ///       之前实例树没有行高常数（就是一行字那么高，十几像素），16 px 的齿轮不缩的话会
    ///       上下各冒出去一半（整屏重绘压到相邻那两条上、局部重绘被切成半颗）；那天起行高
    ///       和版本树一样了，所以这一缩平时不生效，留着是兜底。
    static QRect gearGlyphRect(const QRect &row);

    /// 最右边那个**箭头**占哪一格（收起时画 `>`、展开时画朝下的 V）。分组头一定画它 ——
    /// "点一下打开/关闭"就是这个箭头在说的那件事。**它同时也是画出来的那一圈**（12 px，
    /// 竖着恒在行内，理由见 .cpp）。
    static QRect chevronRect(const QRect &row);

    // ---------- 实例行行尾那一排三颗按钮（2026-09-25 晚，见文件头） ----------
    //
    // 布局是**定死**的（用户原话的次序，别改）：一行三颗，**右起**：齿轮 / 上箭头 /
    // 垃圾桶。三个 `...Rect()` 是**命中格**（比画出来的字形四周各大 `instanceRowActionMargin()`），
    // 三个 `...GlyphRect()` 才是真正 drawPixmap 的那一圈。和组行那两颗同一个规矩：
    // **几何的唯一出处就是这儿** —— 代理按它们画、`MainWindow::eventFilter` 按它们认下
    // 那一下、`helpEvent()` 按它们决定弹不弹话，三处不许各算一份（差一像素的症状就是
    // "看着点中了垃圾桶、其实点的是那一行"，而那一行点一下是进实例设置）。
    //
    // NOTE: 那一整排的几何是**一次算完**的（收在 .cpp 里那个私有的
    //       `instanceActionGlyphRects()`，三个格子一次返回）—— 三处各写一遍的写法只要
    //       改一处间距就会错位一格，而错位在屏幕上完全看不出来是哪一段写错了。
    // NOTE: 三个**命中格之间不许互相重叠**：间隔 6、四周 margin 3 ⇒ 正好相接（邻格共享
    //       一条边界、不叠 1 px）。写成 7 的话中间会漏一条点不到的空隙。

    /// 行尾那颗**齿轮**的命中格（最右边那一颗 = 这一行的锚，**常显**）。
    static QRect instanceGearRect(const QRect &row);
    /// 中间那颗**上箭头**的命中格。只有 `rowPinRole()` 为真的实例行才画它（已经在置顶组里
    /// 的挪不动 ⇒ 不画，不是画灰的）。
    static QRect instancePinRect(const QRect &row);
    /// 最里面那颗**垃圾桶**的命中格（离手最远 —— 它点下去不可逆，见 MainWindowUI 那段
    /// "为什么垃圾桶摆在最里面"）。
    static QRect instanceTrashRect(const QRect &row);

    /// 上面那三格**画出来**的那一圈（比命中格四周各小 `instanceRowActionMargin()`）。
    /// 边长还会照 `gearGlyphRect()` 那样按行高收一收（`min(边长, 行高)`）—— 兜底，行高
    /// 平时是 `versionTreeRowHeight()`（46），这一缩不生效。
    static QRect instanceGearGlyphRect(const QRect &row);
    static QRect instancePinGlyphRect(const QRect &row);
    static QRect instanceTrashGlyphRect(const QRect &row);

    /// 光标落在**行里某一个按钮**上就说那一句，没落在任何一个上就是空串。
    ///
    /// 组行那一半：只有那颗齿轮（`rowGroupGearTipText()`）。
    /// 实例行那一半：齿轮 → `rowInstanceSettingsTipText()`、上箭头 → `rowPinTipText()`
    /// （**只在 `rowPinRole()` 为真时** —— 没画出来的那一格不该说话）、垃圾桶 →
    /// `rowDeleteInstanceTipText()`。
    /// 和 VersionTreeDelegate::rowActionTip() 一个路数（那儿是上一族的第二处几何读者）：
    /// 一条一条往下判，文案常量全从 MainWindowUI 取，这儿不出现字面量。
    static QString rowActionTip(const QModelIndex &index, const QRect &row, const QPoint &pos);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /// 悬停提示。基类那个只看 `Qt::ToolTipRole`（整条行一句），这里先问一声 rowActionTip()，
    /// 问不到才交给基类。
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;

private:
    /// 实例行那三颗按钮的**字形**格子，从右往左**一次算完**（右起：齿轮 / 上箭头 / 垃圾桶）。
    ///
    /// 三个 `instanceXxxGlyphRect()` 和三个 `instanceXxxRect()` 都是它的薄壳（前者原样、
    /// 后者四周各加 `instanceRowActionMargin()`）—— 这就是上面那段"一次算完"说的那一处。
    ///
    /// NOTE: 右边缘取 `row.x() + row.width()`、**不用 `row.right()`** —— 后者等于
    ///       `left + width - 1`，用它当锚整排会往左挪一个像素，而这个"差一个像素"正好是
    ///       调用方（`eventFilter`）与代理对不齐的那种错。
    static void instanceActionGlyphRects(const QRect &row, QRect &gear, QRect &pin, QRect &trash);
};
