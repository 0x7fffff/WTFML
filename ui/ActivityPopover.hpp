#pragma once
/**
 * @file ActivityPopover.hpp
 * @brief 活动中心浮动面板（设计稿 panels-tasks：约 400 宽，贴在左栏任务摘要上方弹出）。
 *
 * 只摆三段：**当前**（进度 + 步骤）、**等待中**（上移 / 下移 / 移除）、**已结束**（清除 / 重试）。
 * 筛选、搜索、历史与详情仍在整页活动中心里，从标题行「全部任务」进去。
 *
 * 底座是 GlassPopover（窗口里的玻璃子卡片）：点外面 / Esc / 再点一次任务摘要就收起。
 * 数据每次现问 ItemProvider，不缓存。
 */

#include "GlassPopover.hpp"
#include "TaskCenterPanel.hpp"

#include <functional>

class QVBoxLayout;

class ActivityPopover : public GlassPopover {
public:
    struct Handlers {
        TaskCenter::ItemProvider items;
        TaskCenter::RowActionHandler rowAction;
        TaskCenter::ClearFinishedHandler clearFinished;
        std::function<void(const QString &key)> rowActivated;
        std::function<void()> openAll;
    };

    ActivityPopover(QWidget *anchor, Handlers handlers);
    ~ActivityPopover() override;

    static QString objectNameValue() { return QStringLiteral("activityPopover"); }
    static QString openAllButtonName() { return QStringLiteral("activityPopoverOpenAll"); }
    static QString clearButtonName() { return QStringLiteral("activityPopoverClear"); }
    /// 行与行尾按钮的名字：`activity<部件>:<key>`，部件 = Row / MoveUp / MoveDown / Remove / Retry。
    static QString partName(const QString &part, const QString &key) {
        return QStringLiteral("activity%1:%2").arg(part, key);
    }
    /// 点开任务摘要之后从浮动面板进整页（测试与验收脚本用）；面板没开着就返回 false。
    static bool openAllIn(QWidget *root);
    static constexpr int kWidth = 400;
    /// 已结束那一段最多摆几行（更早的在「全部任务」里）。
    static constexpr int kFinishedLimit = 4;

    /// 照 ItemProvider 现在给的重画一遍，并重新贴到锚点上方。
    void refresh();

protected:
    void place() override;

private:
    void rebuild();
    QWidget *sectionHeader(const QString &title, QWidget *trailing);

    Handlers m_handlers;
    QWidget *m_body = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
};
