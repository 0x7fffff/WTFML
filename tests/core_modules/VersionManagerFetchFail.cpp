#include "Support.hpp"

int runVersionManagerFetchFail(QCoreApplication &app) {
    VersionManager vm;
    const QString bogus = QStringLiteral("this-version-does-not-exist-9999");
    int emissions = 0;
    bool finished = false;
    QString reportedId;
    QEventLoop loop;
    QObject::connect(&vm, &VersionManager::versionDetailsFetched, &app,
                     [&](const QString &id, const QJsonObject &json) {
                         ++emissions;
                         reportedId = id;
                         out() << "        versionDetailsFetched: id=" << id
                               << " jsonEmpty=" << json.isEmpty() << "\n";
                         finished = true;
                         loop.quit();
                     });

    section("H. 元数据死路");
    QTimer::singleShot(30000, &loop, [&]() {
        out() << "        30 秒超时\n";
        loop.quit();
    });
    vm.fetchVersionDetails(bogus);
    if (!finished) {
        loop.exec();
    }
    check(emissions == 1, "H1 拉取不存在的版本: 有且只有一个终态信号", QString::number(emissions));
    check(reportedId == bogus, "H1 终态信号带回了请求的版本号", reportedId);
    check(!vm.isDownloading(), "H1 之后 isDownloading() 为假（按钮不会永久变灰）");
    return g_failures == 0 ? 0 : 1;
}

