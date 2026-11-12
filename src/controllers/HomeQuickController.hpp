#pragma once

#include "../manage/LaunchHistory.hpp"
#include "../../ui/MainWindowUI.hpp"

#include <QIcon>
#include <QList>
#include <QString>

/// 主页收藏/最近两排的纯视图模型组装器。
/// MainWindow 只收集当前实例快照；去重、排序、丝带身份都在这里决定（实例带不补空槽）。
class HomeQuickController {
public:
    struct Instance {
        QString id;
        QString name;
        QIcon icon;
        bool favorite = false;
        QString version;
    };

    struct Rows {
        QList<MainWindowUI::HomeQuickItem> current;
        QList<MainWindowUI::HomeQuickItem> favorites;
        QList<MainWindowUI::HomeQuickItem> recent;
    };

    static Rows build(const QList<Instance> &installed,
                      const QList<LaunchHistory::Record> &history,
                      const QString &currentId, int favoriteCount, int recentCount);
};
