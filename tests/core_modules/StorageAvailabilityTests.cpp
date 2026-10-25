#include "Support.hpp"
#include "util/StorageAvailability.hpp"

#include <QStorageInfo>
#include <QTemporaryDir>

/**
 * @file StorageAvailabilityTests.cpp
 * @brief 「这块盘还在不在」那一句判据：正常盘不许误报，盘没了必须说得出话。
 *
 * 掉盘本身在自动测试里造不出来（真拔一块盘不能是回归夹具）。这里钉的是两件能做到的事：
 * ① 正常的卷、以及**还没建出来**的落点目录，一律判「可用」—— 否则每次下载都会先弹一句
 * "磁盘不可用"；② 一个本机确实不存在的盘符，判「不可用」且话里点名是磁盘，
 * 而不是回一句泛泛的失败。
 */
void runStorageAvailabilityTests()
{
    section("ZK1. 掉盘时有没有一句能看懂的话");

    QTemporaryDir fixture;
    check(fixture.isValid(), "ZK1 临时目录夹具建出来了", fixture.path());
    if (!fixture.isValid()) {
        return;
    }

    const QString reasonForTemp = StorageAvailability::unavailableReason(fixture.path());
    check(reasonForTemp.isEmpty(), "ZK1 正常目录所在的卷判「可用」（不许天天误报）", reasonForTemp);

    // 下载/导入的落点经常是"还没建出来的目录"：`versions/<新实例>`、`saves/<新世界>`。
    // 判据必须往上找到最近一个存在的祖先去问卷，不能因为"这个目录不存在"就报不可用。
    const QString pendingPath = fixture.filePath(QStringLiteral("还没建的子目录/更深一层"));
    const QString reasonForPending = StorageAvailability::unavailableReason(pendingPath);
    check(reasonForPending.isEmpty(),
          "ZK1 路径还没建出来时按最近的上级卷判（下载落点就是这种形状）", reasonForPending);

    // 找一个本机确实不存在的盘符。A/B 是老式软驱，从 Z 往 D 找，正常机器不会 23 个字母全占满；
    // 万一真占满了就跳过下面两条，并把跳过原因打出来（不冒充通过）。
    QString missingRoot;
    for (int letter = 'Z'; letter >= 'D'; --letter) {
        const QString root = QChar(letter) + QStringLiteral(":/");
        if (!QStorageInfo(root).isValid()) {
            missingRoot = root;
            break;
        }
    }
    check(!missingRoot.isEmpty(), "ZK1 本机至少有一个空盘符可以当「盘不在了」的夹具", missingRoot);
    if (missingRoot.isEmpty()) {
        return;
    }

    const QString missingPath = missingRoot + QStringLiteral("wtfml-不存在的落点");
    const QString reasonForMissing = StorageAvailability::unavailableReason(missingPath);
    check(!reasonForMissing.isEmpty(),
          "ZK1 盘符不在了就判「不可用」，不许回空串（空串 = 界面上什么都不弹）",
          reasonForMissing);
    check(reasonForMissing.contains(QStringLiteral("磁盘")),
          "ZK1 那句话说的是磁盘，不是一句泛泛的失败（用户要知道去插线，不是去改设置）",
          reasonForMissing);
}
