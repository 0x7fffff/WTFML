/**
 * @file SettingsPageParts.hpp
 * @brief 设置页专属的几件部件：分组、行、滑块页签、「高级」展开行、「已保存」提示。
 *
 * 给 `MainWindowSettingsPage.cpp` 用。样式、尺寸、动效都按设计稿 r2（tokens-r2.md §1 §6 §7）：
 * 分组 = 小标题 + 一层极淡底，行之间 2px 缝；行 = 左标签右 220px 控件列，行宽 < 600px 时上下叠放。
 * 这里不写任何输入类控件的样式（那归 Theme.cpp）。
 */
#pragma once

#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>

class QLabel;
class QVBoxLayout;
class QGridLayout;
class QHBoxLayout;
class QPropertyAnimation;
class QVariantAnimation;
class QTimer;

namespace SettingsParts {

/// 行宽（= 表单宽）窄于这个数时，行改上下叠放。
constexpr int kStackBelow = 600;
/// 右侧控件列宽。
constexpr int kControlColumn = 220;
/// 叠放时控件最宽。
constexpr int kStackedControlMax = 320;
/// 表单（页头与各分组）最宽。
constexpr int kFormMaxWidth = 720;

/// 控件在行里怎么摆。
enum class Placement {
    Column,   ///< 右侧固定 220 列，控件铺满（下拉、输入框、滑块 + 数值）
    Natural,  ///< 右侧按自身大小靠右（开关、按钮、数字框）
    Wide,     ///< 标签一行、控件整行铺在下面（内存条、多行文本、字段胶囊）
};

struct RowSpec {
    QString label;
    QString help;  ///< 常驻说明；默认不写
    QString tip;   ///< 标签旁 ⓘ 的悬停内容；空则不画
    Placement placement = Placement::Column;
};

/// 一行。窄于 kStackBelow 时在 resizeEvent 里把控件挪到标签下面。
class SettingsRow : public QWidget {
    Q_OBJECT
public:
    SettingsRow(const RowSpec &spec, QWidget *control, QWidget *parent);
    bool stacked() const { return m_stacked; }
    QLabel *label() const { return m_label; }
    QWidget *control() const { return m_control; }

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void setStacked(bool stacked);
    void place();

    RowSpec m_spec;
    QWidget *m_left = nullptr;
    QLabel *m_label = nullptr;
    QWidget *m_cell = nullptr;
    QWidget *m_control = nullptr;
    QGridLayout *m_grid = nullptr;
    bool m_stacked = false;
    qreal m_hover = 0;
    QVariantAnimation *m_hoverAnim = nullptr;
};

/// 一个分组：小标题（16px 半粗系统字）+ 下面一列行。行自己画底，所以行间 2px 缝透出窗口。
class SettingsGroup : public QWidget {
    Q_OBJECT
public:
    SettingsGroup(const QString &title, QWidget *parent);
    SettingsRow *addRow(const RowSpec &spec, QWidget *control);
    /// 标题行右侧的文字按钮（「恢复默认」「Java 管理…」）。
    void setHeaderAction(QWidget *action);
    QString title() const { return m_title; }
    QWidget *rowsHost() const { return m_rows; }
    /// 在 `host` 里，@p row 是第几个可见行 / 一共几个可见行；给行定圆角用。
    static void position(const QWidget *row, int *index, int *count);

private:
    QString m_title;
    QHBoxLayout *m_header = nullptr;
    QWidget *m_rows = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
};

/// 分段页签：选中淡底滑块 200ms 滑动，不画条。
class SlidingTabs : public QWidget {
    Q_OBJECT
public:
    explicit SlidingTabs(QWidget *parent = nullptr);
    int addTab(const QString &text);
    int count() const { return m_texts.size(); }
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
    /// 第 i 个页签的矩形（测试与滑块用）。
    QRect tabRect(int index) const;
    /// 滑块当前矩形（动画中是中间值）。
    QRectF indicatorRect() const { return m_indicator; }

signals:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    int indexAt(const QPoint &pos) const;
    QStringList m_texts;
    int m_current = 0;
    int m_hover = -1;
    QRectF m_indicator;
    QVariantAnimation *m_anim = nullptr;
};

/// 折叠体的开合动效：body 的最大高度 0 ↔ 内容高，200ms OutCubic；每一帧当场把版面排完，
/// 标题行和已经看见的内容全程不位移。「高级」展开行（ExpandSection）和各页自己的折叠行共用这一份。
/// 动效关着（Anim::enabled()）或 body 还没显示出来时直接切换。body 要有自己的布局。
/// `fillsSpace`：body 在父布局里带 stretch、展开后要撑满剩余高度（世界设置页的存档文件框）时给 true，
/// 终点高度改为「摊开后版面实际给它的高度」，不然动画停在内容高、放开上限那一下会再跳一截。
class FoldMotion : public QObject {
    Q_OBJECT
public:
    explicit FoldMotion(QWidget *body, bool fillsSpace = false);
    bool open() const { return m_open; }
    /// 开合 body。已经在目标状态时什么也不做。
    void setOpen(bool open, bool animate = true);

signals:
    /// 开合落定（动画跑完，或直接切换完）：body 的显隐与最大高度已经是终态。
    void settled(bool open);

private:
    QWidget *m_body = nullptr;
    QPropertyAnimation *m_anim = nullptr;
    bool m_open = false;
    bool m_fillsSpace = false;
};

/// 「高级」展开行：整行是按钮（Theme::makeFoldRow），右侧写「展开 / 收起」，不画箭头；内容高度 200ms 展开。
class ExpandSection : public QWidget {
    Q_OBJECT
public:
    ExpandSection(const QString &title, QWidget *parent);
    /// 往里加分组 / 行。
    QVBoxLayout *bodyLayout() const { return m_bodyLayout; }
    bool expanded() const { return m_expanded; }
    void setExpanded(bool expanded, bool animate = true);
    QWidget *header() const { return m_header; }
    QWidget *body() const { return m_body; }

signals:
    void toggled(bool expanded);

private:
    QWidget *m_header = nullptr;
    QWidget *m_body = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    FoldMotion *m_motion = nullptr;
    bool m_expanded = false;
};

/// 页面右上角的「已保存」：180ms 淡入，停留 1.2s 后淡出。
class SavedToast : public QWidget {
public:
    explicit SavedToast(QWidget *parent);
    void ping();
    qreal opacity() const { return m_opacity; }
    /// 把自己摆到 parent 右上角（parent 尺寸变了时调）。
    void anchor();
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void animateTo(qreal target, int ms);
    qreal m_opacity = 0;
    QTimer *m_hold = nullptr;
    QVariantAnimation *m_anim = nullptr;
};

/// 多行文本失焦（或 Ctrl+Enter）时发 committed()：JVM 参数那一格「失焦即存」。
class CommitOnFocusOut : public QObject {
    Q_OBJECT
public:
    explicit CommitOnFocusOut(QWidget *edit);
signals:
    void committed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

/// 给 root 底下的每个会改设置的控件接上「用户改过 → ping」：
/// 下拉（activated）、可勾选按钮（clicked）、滑块（松手 / 键盘）、单行输入与数字框（改过再失焦 / 回车）、多行文本（committed）。
void watchSaved(QWidget *root, const std::function<void()> &ping);

}  // namespace SettingsParts
