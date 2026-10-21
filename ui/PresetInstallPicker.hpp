/**
 * @file PresetInstallPicker.hpp
 * @brief 安装页那张「预设」行点开的**紧凑弹窗**：四类各挑一份，看一眼它会动什么。
 *
 * 【它是什么、不是什么】
 *
 * coding plan §7.1 那张图里"正文四类切换"的那一块。它**只画**外面喂进来的值：
 * 「盘上有哪几份预设、用户现在选的是哪一份、这一份会动几项 / 跳过了几项 / 为什么」
 * 全是 `PresetController` 算好的（§4 那四类、以及 `unset` 这种旧混合预设都不在这一层认）。
 * 这个类一个字节的盘都不读 —— 和 `PresetManagePage` 同一条约定（`ui/` 不 include `src/`）。
 *
 * 【为什么是 QTabBar + QStackedWidget，不是四个分组铺开】
 *
 * 用户要的是"紧凑弹窗"（`OverlayHost::promptWidth()` = 480，和所有提示一样宽）。
 * 四类各一行下拉框 + 状态，铺开会把卡片撑得很高；而这张卡片的高度是"用户看着选"的
 * 那一块，越短越好。四类之间本来就是"切过去看"的关系，不是"同时填四格"。
 *
 * 【按需展开原因】
 *
 * 状态那一行只说一句结论（「会动 12 项」/「这一份读不出来」）。缺失、未知、跳过那几类
 * 原因装在下面那颗「查看计划与原因」后面 —— 平时收着，用户想看才展开。理由和别处的
 * "折叠框"一样：这些字是**给要追问的人**看的，不该在正常那一趟里抢注意力。
 *
 * 【空状态】
 *
 * 某一类盘上一份预设都没有时，那一页不摆一个空下拉框（用户点开它只会看到"不使用"，
 * 以为是自己选错了）—— 摆一句「这一类还没有预设」+ 一颗「前往预设管理」。
 * 按钮只发 `manageRequested()`，**去哪、怎么去**是调用方的事（这一层不认识主窗口）。
 */

#pragma once

#include <QColor>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTabBar;

class PresetInstallPicker : public QWidget {
    Q_OBJECT

public:
    /// 某一类里可以挑的一份预设。**只有 id 和名字** —— 数量/状态那一行是另一路
    /// （`setStatus()`），因为它要算过计划才知道。
    struct Option {
        QString id;
        QString name;
        /// 这一份现在还在不在盘上（被删掉 / 读不出来时 false）。
        /// 那种项**仍然要留在下拉框里**：§7.1 那句「已删除/损坏的所选项明确提示，
        /// 不静默换为另一项」—— 悄悄换成"不使用"或别的一份，用户下一次看到的是
        /// "我明明选过"。
        bool available = true;
    };

    /// 一类。`options` 不含「不使用」那一项（那一项每一页都有、是第 0 项）。
    struct Category {
        QString key;         ///< `presetCategoryKey()` 的串（回传时原样带回）
        QString title;       ///< 页签上那两个字（游戏设置 / 模组 / 资源包 / 数据包）
        QList<Option> options;
        /// 现在选中的那一份。空串 = 「不使用」。
        QString selectedId;
        /// 算好的状态行（空串 = 未选择；调用方会把「会动 N 项」这一类话填进来）。
        QString status;
        /// 「查看计划与原因」展开之后的那几行。
        QStringList reasons;
        /// 状态是不是"有问题"（读不出来 / 有段做不了 / 有覆盖要确认）。
        /// 只影响那一行字的颜色。
        bool problem = false;
    };

    explicit PresetInstallPicker(QWidget *parent = nullptr);

    /// 弹窗顶上那句「游戏版本 1.21.1 · Forge」（`loader` 为空 = 原版）。
    void setContext(const QString &gameVersion, const QString &loader);

    /// 把四类摆上去。**全量重建**（打开弹窗时调一次）。
    void setCategories(const QList<Category> &categories);

    /// 只换某一类的状态那几行（用户换了下拉框之后由调用方重算一次计划）。
    void setStatus(const QString &key, const QString &status, const QStringList &reasons,
                   bool problem);

    /// 某一类现在选的是哪一份（空串 = 不使用）。认不出的 key 也回空串。
    QString selectedId(const QString &key) const;

    /// 四类合起来现在选中了哪几份（**只含非空 id**，按页面上的次序）。
    QList<QPair<QString, QString>> selections() const;

    /// 选中了几类（摘要那句「已选择 N 类」用的数）。
    int selectedCount() const;

    /// 现在停在第几个页签（0..3）。给验证工程用。
    int currentTab() const;
    void setCurrentTab(int index);

    // ---------- objectName（验证工程靠它们认控件、量几何） ----------
    static QString contextLabelName() { return QStringLiteral("presetInstallContext"); }
    static QString tabBarName() { return QStringLiteral("presetInstallTabs"); }
    /// 第 index 类那一页里的下拉框（index 就是页面次序，不认标题文字）。
    static QString comboName(int index) {
        return QStringLiteral("presetInstallCombo%1").arg(index);
    }
    /// 第 index 类那一页里的状态行。
    static QString statusName(int index) {
        return QStringLiteral("presetInstallStatus%1").arg(index);
    }
    /// 第 index 类那一页里的「查看计划与原因」（只有 **有原因** 时才摆）。
    static QString reasonsButtonName(int index) {
        return QStringLiteral("presetInstallReasons%1").arg(index);
    }
    /// 展开之后那几行原因。
    static QString reasonsLabelName(int index) {
        return QStringLiteral("presetInstallReasonsText%1").arg(index);
    }
    /// 空状态那句「这一类还没有预设」。
    static QString emptyLabelName(int index) {
        return QStringLiteral("presetInstallEmpty%1").arg(index);
    }
    /// 空状态那颗「前往预设管理」。
    static QString manageButtonName(int index) {
        return QStringLiteral("presetInstallManage%1").arg(index);
    }

    /// 下拉框第 0 项的字（= "这一趟这一类不带预设"）。**唯一出处**：验证工程也读它。
    static QString noneText() { return QStringLiteral("不使用"); }
    /// 空状态那颗按钮的字。
    static QString manageText() { return QStringLiteral("前往预设管理"); }
    /// 展开原因那颗按钮的字。
    static QString reasonsText() { return QStringLiteral("查看计划与原因"); }

signals:
    /// 用户在下拉框里换了某一类（`presetId` 空串 = 改回「不使用」）。
    /// 由**用户**改出来的才发；`setCategories()` 灌数据那一趟不发（见 .cpp 里的
    /// `QSignalBlocker`，和 `PresetController::fillInstallSelector()` 同一个理由）。
    void selectionChanged(const QString &categoryKey, const QString &presetId);

    /// 空状态那颗「前往预设管理」被点了。去哪由调用方决定。
    void manageRequested();

private:
    struct Page {
        QString key;
        QComboBox *combo = nullptr;
        QLabel *status = nullptr;
        /// 状态行的正常墨色（建出来那一刻的 `WindowText`）。problem 那一档换红，换回来
        /// 必须回到这个值 —— 从"当前调色板"里拿会被上一次的红污染。
        QColor normalTextColor;
        QPushButton *reasonsButton = nullptr;
        QLabel *reasonsLabel = nullptr;
        QWidget *empty = nullptr;
        QLabel *emptyLabel = nullptr;
        QPushButton *manageButton = nullptr;
    };

    /// 建第 index 页；控件指针写进 `out`（**由调用方 append**，这样 `m_pages` 的下标
    /// 与页签 / `m_categories` 永远同一份次序）。
    QWidget *buildPage(int index, const Category &category, Page *out);
    void applyStatus(Page &page, const QString &status, const QStringList &reasons, bool problem);
    int indexOfKey(const QString &key) const;

    QLabel *m_context = nullptr;
    QTabBar *m_tabs = nullptr;
    QStackedWidget *m_stack = nullptr;
    QList<Category> m_categories;
    QList<Page> m_pages;
};
