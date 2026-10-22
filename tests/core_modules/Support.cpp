#include "Support.hpp"

#include <cstdio>

int runDirectoryLockProbeIfRequested(const QStringList &arguments) {
    // 判据跟原来 `verify_instances` 的 main 逐字同源：`<程序名> --directory-lock-probe <目录>`。
    if (arguments.size() < 3 || arguments.at(1) != QStringLiteral("--directory-lock-probe")) {
        return -1;
    }
    DirectorySessionLock::enabled = true;
    if (!DirectorySessionLock::acquire(arguments.at(2))) {
        return 73;
    }
    if (arguments.contains(QStringLiteral("--hold"))) {
        std::puts("LOCKED");
        std::fflush(stdout);
        QThread::sleep(60);  // 父进程会杀掉它；父进程自己死了也有上界。
    }
    return 0;
}
