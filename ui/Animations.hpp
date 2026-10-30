/**
 * @file Animations.hpp
 * @brief 界面动效的**唯一出处**：按下按钮、切换页面、展开/折叠。
 * @note 只做下面这几件事，别的什么都不做。
 *
 * 【这一层做这几件事，一件都不多】
 *
 * 1. **按下按钮**（`installPressAnimation()`）：往整个应用挂一个事件过滤器，谁被按下去就
 * 给谁压暗一档、松开再弹回来。**没有第二处** —— src/ + ui/ 里每一个 `new QPushButton`
 * 一个字都不用改（这也是为什么走事件过滤器而不是做一个 `WtfmlButton` 子类：那样得逐处
 * 改，而且漏一处就是"有些按钮按下去没反应"）。
 * 2. **切换页面**（`pageAppear()` / `overlayAppear()`）：新那一页淡进来，顺带从下面抬
 * 一小段。
 * 3. **展开/折叠**（`animateExpansion()`）：`QTreeView::setAnimated(true)`，交给 Qt 自己
 * 放那一段动画。
 * 4. **这一行是新来的**（`rowAppear()`）：同页里列表变了一趟（筛一下、禁一个）时，
 * **只有新冒出来的那几行**淡进来 —— 整页**不**重播。
 * 5. **进度条追值**（`smoothProgress()`）：把一格一格的下载进度追成连续的；真数字倒退、
 * 或者总量（范围）变了，原地落到真值上（同一条）。
 *
 * 【★ 硬要求：不许有元素超出】
 *
 * 这是唯一的验收线：**只要不要有元素超出**，所以这一层给自己上了一条死规矩：
 *
 * * **除了上拉面板那一个例外，一个像素的几何都不动**。淡入淡出是把控件先渲染到一张
 * pixmap 上、再按不透明度贴回来（`QGraphicsEffect` 那条路）—— 控件自己的 `geometry()`
 * 从头到尾没变过，布局也没被重新算过。
 * * 那个例外是底栏那两块上拉面板（`slideUp()`）：它们是**顶层窗口**（`Qt::Popup`），
 * "从下面滑上来"就是它们的 `pos()` 在动，而它们本来就是一块独立的窗口 —— 动它不会把
 * 任何东西挤出容器，动完也一定停在算好的那个点上（`finished` 里再 `move()` 一次兜底）。
 * * 自绘的那两份（`AnimEffect::draw()` 与换页用的 `PageCurtain::paintEvent()`）**各自钉了
 * 一条裁剪**：画之前先按自己那一块 `setClipRect(..., IntersectClip)`，就算哪天平移量算
 * 错了，也画不到自己那一块之外。
 * * 页面抬起来那一段（`pageRiseDistance()`）是**把内容整块往下挪着画**，所以是**上面**
 * 多出一条、下面少一条，而不是内容溢出到页面外 —— 那一条裁剪在这里同样管着。
 *
 * 【为什么不走调色板 / 不走样式表】
 *
 * 样式表里已经有 `:pressed` 那一档颜色（`Theme::widgetStyleSheet()`），它是**瞬间**生效的；
 * 样式表的属性动画（`qproperty-…`）要求控件自己有那个 `Q_PROPERTY`，等于还是得逐处改。
 * 这一层动的是"渲染完的那张图"，所以和样式表、和调色板都不打架 —— 底下的按下色照旧生效，
 * 这一层只是**让这个变化看起来是"按下去"而不是"跳一下"**。
 */

#pragma once

#include <QPoint>
#include <QRect>
#include <functional>

class QApplication;
class QColor;
class QProgressBar;
class QTreeView;
class QWidget;

namespace Anim {

// —— 时长与档位：**唯一出处**（要改节奏就改这儿，别在调用点写数）——

/// 按下去那一下多快（毫秒）。
constexpr int pressInMs() { return 140; }
/// 松开之后弹回来多快。
constexpr int pressOutMs() { return 150; }
/// 换页那一下（淡入 + 上抬）。
///
/// 页面进入使用 250 ms ease-out，与覆盖层和面板保持一致。
constexpr int appearMs() { return 250; }
/// 整块覆盖那一层（`OverlayHost`）淡进来那一下。**和换页同一个量级**（同一类转场）。
constexpr int overlayMs() { return 250; }
/// 底栏上拉面板滑上来那一下。
constexpr int panelMs() { return 250; }
/// 滚动条**冒出来**那一下。
/// 和换页同一个量级 —— 它得跟页面那一下一起收住，单独更快或更慢都会看成"两件事"。
constexpr int scrollBarMs() { return 180; }
/// 上拉面板的起手位比落点低多少像素。
constexpr int panelSlideDistance() { return 16; }
/// 换页时内容从下面抬多少像素。
constexpr int pageRiseDistance() { return 8; }

/// 「同页里列表变了一趟」时，**新冒出来的那一行**淡进来多快（毫秒）。
///
/// 「同页筛选/列表变化：只动画新增、删除和状态，**不重播整页**」。
/// 所以这一位比换页短一档 —— 它是"这一行是新来的"，不是"一整页换过去了"。
/// 抬位移一概没有：行在列表里，往下挪着画会盖到相邻那一行上（见 `Anim::rowAppear()`）。
constexpr int rowAppearMs() { return 150; }

/// 进度条**追值**那一发多快（毫秒）。
///
/// 「下载进度使用平滑追值，但任务真实进度倒退或总量变化时**必须
/// 如实更新**」。追值只是"别一格一格地跳"，不是"把真数字藏起来" —— 判据那两条（倒退 / 总量
/// 变了）原地落到真值上，见 `Anim::smoothProgress()`。
constexpr int progressMs() { return 240; }

/// 「一拍」是多少毫秒 —— **按帧走**的那几发（换页 / 覆盖层 / 滚动条）用它。
constexpr int stepMs() { return 16; }

/// Page content begins faintly visible, then fades in over the upward transition.
/// Keep a small nonzero start to avoid a blank first frame while the page is populated.
constexpr qreal pageStartOpacity() { return 0.12; }

// 换页**没有干等**：起手直接淡（总时长就是 `appearMs()`）。"不要瞬间铺上"靠的是
// 那一段缓动 + `pageRiseDistance()` 的上抬 —— 干等出来的"不是瞬间"，观感上就是
// "卡了一下"，而且中间那一段是空的，比瞬间铺上更难看。
//
// NOTE: 垫底色那一层（`PageCurtain::start()` 的 `backdrop`）**留着**，它管的是另一件事：
// 不垫的话那些**不透明的子控件视口**（列表/树的 viewport）就成了"谁都不画"的一格，
// 屏幕上留着上一页的像素。见 `PageCurtain::paintEvent()` 与 `pageStartOpacity()`。
// NOTE: 起手那一位钉在 `pageStartOpacity()`（不是 0）。
constexpr int pressDepthPercent() { return 12; }

/// **这是"用户那把开关"**（`setEnabled()` 写的、盘上记着的那一位）。真正"这一趟要不要
/// 动"问的仍然是 `enabled()` —— 它多盖了一层系统那一档（见 `platformReducedMotion()`）。
bool enabled();
void setEnabled(bool on);

/// 用户那把开关自己现在是什么状态（和 `enabled()` 的差别：**不看系统那一档**）。
/// 设置页那个勾选框读的是它 —— 系统开了"减少动态效果"的时候，用户自己的勾**不该**被
/// 悄悄勾掉（他换个系统设置就发现自己的开关被动过了）。
bool userEnabled();
/// 把用户那把开关存到 `QSettings`（键 `Look/Animations`）并从那儿读回来。
/// 键**不在** `Theme/` 底下：`Theme::save()` 会整组 `remove("Theme")` 再重写，
/// 挂在那个前缀下会被"改一次主题色"顺手抹掉。`src/main.cpp` 启动时调一次 `load()`。
void load();
void save();

/// 系统那一档「减少动态效果」。
///
/// Windows 上读的是 `SPI_GETCLIENTAREAANIMATION`（设置 → 辅助功能 → 视觉效果 →
/// 「动画效果」那个开关；它是**这一条**而不是 `SPI_GETANIMATION`，后者管的是窗口最小化/
/// 最大化那类动画）。读不到、不是 Windows、或者系统说"没关"—— 一律当**没开**（`false`）：
/// 用户的界面**默认有动效**，这是这一层的默认姿态，不能因为读不到一个系统
/// 设置就把它变成另一副样子。
bool platformReducedMotion();
/// 去问一次系统（上面那个函数的实现体）。**只有 `src/main.cpp` 在产品里调它**：
/// 验证工程永远别调 —— 那会把"这台机器上系统设置是什么"变成判据的一部分（同一份代码在
/// 两台机器上跑出两种结果）。
void refreshPlatformReducedMotion();
/// 直接摆这一位（给验证工程模拟"系统要求减少动态效果"，不去碰真系统设置）。
void setPlatformReducedMotion(bool on);

/// 给整个应用装上"按下按钮"那个事件过滤器。**幂等**（`Theme::apply()` 会被调好几次，
/// 这里也一样可能被调好几次）。`src/main.cpp` 调它一次，验证工程不调（它们不要动效）。
bool installPressAnimation(QApplication &app);

/// 滚动条**露面**的那一瞬间：从全透明淡进来。
/// 调用方是 `installScrollBarAnimation()` 挂的那个事件过滤器（见它上面那段）；别处不用喊。
void scrollBarAppear(QWidget *bar);

/**
 * @brief 给整个应用装上"滚动条一露面就淡进来"那个事件过滤器（`QEvent::Show`）。
 *
 * 滚动条是**按需出现**的（内容不够长它就不在），而 Qt 出现的那一下是硬切 —— 页面在淡、它在"啪"地跳出来。
 * 这一条把它压进同一段节奏里。
 *
 * 和按下那一份同一个做法（挂 `QApplication`、**从不吞事件**）：全仓每一处滚动区都不用改。
 * **幂等**；`src/main.cpp` 调它一次，验证工程不调（它们不要动效）。
 */
bool installScrollBarAnimation(QApplication &app);

/**
 * @brief 换页：新那一块淡进来，**里面的内容**再从下面抬 `pageRiseDistance()`。
 * @param widget **要淡的那一块**（顶上那一档就是内容区整个 `QTabWidget`）。
 *
 * **抬的是内容，框一个像素都不动**：抬的是页面**里面那一块**
 * （`ui/Animations.cpp` 里的 `liftTarget()`：选项卡 ⇒ `currentWidget()`，
 * 滚动区/列表/树 ⇒ `viewport()`，剩下的 ⇒ 就是它自己），
 * 然后用一块**平铺在它上面的幕布**（`PageCurtain`，一个普通 `QWidget`，不是效果器）
 * 把这**一块的定格快照**按"往下偏一点 + 半透明"画出来。
 *
 * 为什么不直接给整块挂效果器 —— 一句话：**`QGraphicsEffect` 管不了
 * 它下面的子控件**。效果器只重画**它自己那一层**，而列表/树的 viewport 是**不透明的
 * 子控件**、自己开一块绘制区域：效果器把首帧的合成结果留在屏幕上，整段动画里它们**一个
 * 像素都不动**，等效果一摘才"啪"地归位 —— 量出来的就是那一块**始终停在起手位置**
 * （+`pageRiseDistance()`、不透明度恒为 `pageStartOpacity()`）。所以"抬得小一点"治不了，
 * 只能换组件：幕布自己画、走普通绘制路径，没有谁能"没跟上"。
 *
 * 于是这条函数**照样只管淡那一块**（卡片一起淡），只是"抬"只落在内容上。代价：被抬的
 * 那一块**连它自己的内框一起在动**（幕布盖住的范围就是它）—— 顶层那张卡片的外框不动，
 * 页面**内部**的卡片跟着内容走。
 *
 * 内层那几层换页（下载页右栏、设置页右栏…）传的就是它们**当前那一页**：那一页没有单独
 * 画的框，抬的仍然是整块。
 *
 * 淡的同时，那一块**底下先垫一层底色**（而且这一层**不跟着一起淡**）。
 * 垫它是为了整发淡的每一帧 + 内容抬起来让出来的那一条：内容从
 * `pageStartOpacity()` 淡到 1 的这一路上都不是全实，控件底下那些**不透明子控件**
 * （列表/树的视口）自己又不去画 —— 不垫的话它们那一格就是"谁都不画"（屏幕上是上一帧
 * 留下的像素、`grab()` 出来是一块黑），半透明地压上去只会更花；而抬起来那一条露的
 * 就是这一层。来龙去脉写在 `PageCurtain::paintEvent()` 里。
 * 垫哪一档颜色由调用方说（两参数那一份）：**内容抬起来那一条露的就是它**，所以垫错
 * 不报错、症状只是那一条颜色不对（比如卡片上横着一条偏灰的带子）。
 *
 * NOTE: 换页那一层是**嵌套**的（内层那几页也在 `QTabWidget` 里面）。同一时刻两层都在动
 * 只是"两件事撞在一起"，画面仍然是一层贴一张图，不会有元素跑出自己那一块。
 *
 * 这一发淡的**是一张定住的图**：起手照一次，之后每一帧都拿那一张（`PageCurtain` 里那
 * 一段）。理由：换页那一刻往往正是"内容还在往里灌"的时候 —— 典型是实例设置里「服务器
 * 列表」那一页（进页就自动测速，结果一个一个回来：行从一行字长成两行、favicon 补上、
 * 树跟着长高、滚动条冒出来）。现照现淡的话，**这一发淡的每一帧都不是同一张画面**，
 * 看着就是"动画卡卡的"。定住之后这一发只剩 blit，中途进来的
 * 数据照样落到控件上、只是要等它跑完才看得见（当前 `appearMs()` 是 180 ms）。
 *
 * NOTE: 照的时机是"第一帧真的画出来"那一刻 —— 换页那一刻先发的布局请求排在绘制请求
 * 前面（`UpdateRequest` 是低优先级），所以照到的是**已经摆好版**的那一页。
 */
void pageAppear(QWidget *widget);

/**
 * @brief 同上，只是**底下垫什么底色**由调用方指定（上面那一份垫的是顶层窗口的
 * `QPalette::Window`）。
 * @param backdrop 起手那一帧（以及淡的过程中）这一块地方垫什么颜色。
 *
 * 垫的是**这一块平时铺的底**：内层那几层（"左栏换一栏 → 右栏整块换"）那些页都活在一层
 * 滚动区里、视口铺的是 `QPalette::Base`；顶上那一档是**内容区那张卡片**，Fusion 画的
 * 就是 `QPalette::Base`（浅色下 `#fcfcfc`）。垫错不报错、也不会有东西越界 —— 症状只是
 * 这一整块偏灰（`#f0f0f0` 对 `#ffffff`）贴在页面正中，看着像"这一页闪了一下"。
 */
void pageAppear(QWidget *widget, const QColor &backdrop);

/// 整块覆盖的那一层（`OverlayHost`）淡进来。**只淡、不抬** —— 它是"整块盖住内容区"的，
/// 抬起来会在边上露出一条没盖住的缝。
void overlayAppear(QWidget *widget);

/// 原子地更新布局，再用前后合成快照过渡；不推迟真实状态、不拉伸控件。
/// surface 内的 region 为空时使用整块区域。隐藏、缩放及连续切换会中止旧动画。
void layoutTransition(QWidget *surface, const std::function<void()> &change,
                      bool expanding = true, const QRect &region = QRect());
void cancelLayoutTransition(QWidget *surface);

/// 展开/折叠动画。**唯一出处**：所有会展开折叠的树/表都在自己搭好之后调它一次。
/// 关掉动效时它把这一位关回去（等于回到 Qt 的出厂行为：瞬间展开）。
void animateExpansion(QTreeView *view);

/// 底栏那两块上拉面板：从落点下面 `panelSlideDistance()` 滑上来。
/// 调用方先把面板 `show()` 出来（顺序见 `BottomBarPanel::popUp()`）。
void slideUp(QWidget *window, const QPoint &target);

/// 往下弹的那一档（主页右上那张账号卡片下面那块面板）：从落点**上面**同样一格滑下来。
/// 和 `slideUp()` 是同一条动效的两头（同时长、同缓动、同收尾兜底），只有起手位在另一边。
void slideDown(QWidget *window, const QPoint &target);

/**
 * @brief 「这一行是新来的」：把刚摆上去的**一行**淡进来（`rowAppearMs()`）。
 *
 * 「同页筛选/列表变化：只动画**新增**、删除和状态，
 * **不重播整页**」。调用方是**列表自己**（见 `InstanceFilesPage::rebuildRows()`）——
 * 它知道哪几行是新冒出来的，这一层不知道（也不该知道）。
 *
 * **只有"淡"，没有"抬"**：行是挨着摆的，往下挪着画会盖到相邻那一行上（换页那一发敢抬，
 * 是因为幕布盖着的是**整块**内容、上下都是它自己）。所以这一发连幕布都不用，
 * 直接挂效果器 —— 效果器不碰几何，见下面【硬要求】那一条。
 *
 * **整片换掉就不动画**：调用方先数一遍"新来的有几行"，超过 `rowAppearLimit()` 就一行都
 * 不淡（那是"这一页换了一批东西"，不是"多了几行"；几十行一起淡看着是整页在闪）。
 * 这也顺手把"进页第一趟"挡在外面（那一趟每一行都是新的）。
 *
 * 关掉动效时**原地返回**（一个效果都不挂）—— 验证工程跑起来没有动效。
 */
void rowAppear(QWidget *row);

/// 「一次最多淡几行」。超过这个数就一行都不淡（见 `rowAppear()` 与 `rowAppearMs()`）。
constexpr int rowAppearLimit() { return 20; }

/**
 * @brief 进度条**追值**：把 `bar` 的一格一格往 `value` 追过去（`progressMs()`）。
 *
 * 「下载进度使用平滑追值，**但任务真实进度倒退或总量变化时必须
 * 如实更新**」。所以这里有四条规矩，一条都不能少：
 *
 * 1. **总量变了**（`maximum()` 和上一次不一样，含第一次调用）⇒ **原地落到真值**。
 * 下载批次换了一批就是"这一根条子换了个人"，追值会把新批次的起手拖成一条慢慢爬的线。
 * 2. **倒退**（`value < 当前显示的那一格`）⇒ **原地落到真值**。真进度倒退是**情报**
 * （重试了、跳过校验了），拿动画盖住它等于撒谎。
 * 3. **这一格现在是 0**（`当前显示的那一格 <= 0`）⇒ **原地落到真值**。这一条是上面
 * "还回来"那条路带出来的：启动借走这一格时把它摆成了不确定态（`setRange(0,0)`），
 * **那一下没经过本函数**，所以记下的总量还是旧的那个 —— 还回来时总量"看着没变"，
 * 可条子自己已经被钳回 0 了，照追值算就是从 0 慢慢爬到"其实早就下到那儿了"的那个
 * 数字（用户等启动那几秒回来，看见的是下载在**重头爬**）。从 0 起手一律不追，
 * 反正"这一批的第一个真数字"本来就该直接看见。
 * 4. 其余（往前）⇒ 从当前那一格追到 `value`，`OutCubic`，**收尾一定落在 `value` 上**
 * （不是"落在它附近"）。
 *
 * 全仓**所有**对这个进度条的写入（包括复位成 0、包括"还回来"那一趟）都走它 ——
 * 掺一个 `setValue()` 进去的话，正在追的那一发会在下一拍把那一句盖掉。
 *
 * 关掉动效时就是一句 `setValue()`。
 */
void smoothProgress(QProgressBar *bar, int value);

}  // namespace Anim
