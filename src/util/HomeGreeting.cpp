#include "util/HomeGreeting.hpp"

namespace HomeGreeting {

QString text(const QDateTime &now, const QDate &lastOpened, bool hasInstances, const QString &name) {
    // 没记录但有实例 = 升级上来的老用户，不算首次，往下走时段那一条。
    if (!lastOpened.isValid() && !hasInstances) return QStringLiteral("欢迎来到 WTFML。");
    QString lead;
    if (lastOpened.isValid() && lastOpened.daysTo(now.date()) >= kLongAbsenceDays) {
        lead = QStringLiteral("好久不见");
    } else {
        const int hour = now.time().hour();
        lead = hour < 5    ? QStringLiteral("夜深了")
               : hour < 11 ? QStringLiteral("早上好")
               : hour < 13 ? QStringLiteral("中午好")
               : hour < 18 ? QStringLiteral("下午好")
               : hour < 23 ? QStringLiteral("晚上好")
                           : QStringLiteral("夜深了");
    }
    const QString who = name.trimmed();
    return who.isEmpty() ? lead + QStringLiteral("。") : lead + QStringLiteral("，") + who + QStringLiteral("。");
}

}  // namespace HomeGreeting
