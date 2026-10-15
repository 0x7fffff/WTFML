#pragma once

#include "../manage/LaunchHistory.hpp"
#include "../../ui/MainWindowUI.hpp"

#include <QIcon>
#include <QList>
#include <QString>

/// 主页收藏/最近两排的纯视图模型组装器。
/// MainWindow 只收集当前实例快照；去重、排序、丝带身份和空槽都在这里决定。
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
