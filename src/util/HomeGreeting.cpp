#include "util/HomeGreeting.hpp"
#include <QCoreApplication>

namespace HomeGreeting {

QString text(const QDateTime &now, const QDate &lastOpened, bool hasInstances, const QString &name) {
    // 没记录但有实例 = 升级上来的老用户，不算首次，往下走时段那一条。
    if (!lastOpened.isValid() && !hasInstances) return QCoreApplication::translate("HomeGreeting", "欢迎来到 WTFML");
    QString lead;
    if (lastOpened.isValid() && lastOpened.daysTo(now.date()) >= kLongAbsenceDays) {
        lead = QCoreApplication::translate("HomeGreeting", "好久不见");
    } else {
        const int hour = now.time().hour();
        lead = hour < 5    ? QCoreApplication::translate("HomeGreeting", "夜深了")
               : hour < 11 ? QCoreApplication::translate("HomeGreeting", "早上好")
               : hour < 13 ? QCoreApplication::translate("HomeGreeting", "中午好")
               : hour < 18 ? QCoreApplication::translate("HomeGreeting", "下午好")
               : hour < 23 ? QCoreApplication::translate("HomeGreeting", "晚上好")
                           : QCoreApplication::translate("HomeGreeting", "夜深了");
    }
    const QString who = name.trimmed();
    // 不带标点：问候和名字之间一个空格，界面按字号把这一格画宽（见 MainWindowUI 的大标题）。
    return who.isEmpty() ? lead : lead + QLatin1Char(' ') + who;
}

}  // namespace HomeGreeting


