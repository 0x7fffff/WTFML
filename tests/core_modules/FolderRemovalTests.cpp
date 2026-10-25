#include "Support.hpp"
#include <thread>
#include <chrono>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>  // ZAO6：拿一个 `FileShare = 0` 的句柄当「游戏还开着」

void runFolderRemovalTests() {
    section("ZAN. 删一棵目录（FolderManager）：只读文件删得掉，链接那一头一个字节都不动");

    QTemporaryDir temp;
    check(temp.isValid(), "ZAN1 前提：临时目录建出来了", temp.errorString());
    if (!temp.isValid()) {
        return;
    }

    // 已空目录的临时共享句柄不能让本次复制暂存永久遗留；持续占用仍须失败。
    {
        const QString dir = temp.filePath(QStringLiteral("brief-directory-hold"));
        QDir().mkpath(dir);
        HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(dir.utf16()), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        check(handle != INVALID_HANDLE_VALUE, "ZAN0 取得短暂阻止目录删除的共享句柄");
        if (handle != INVALID_HANDLE_VALUE) {
            std::thread release([handle] {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
                CloseHandle(handle);
            });
            const bool removed = removeTreeWithoutFollowingLinks(dir);
            release.join();
            check(removed && !QFileInfo::exists(dir), "ZAN0 短暂句柄释放后有界清理空目录");
        }
        QDir().mkpath(dir);
        handle = CreateFileW(reinterpret_cast<LPCWSTR>(dir.utf16()), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            check(!removeTreeWithoutFollowingLinks(dir) && QFileInfo::exists(dir),
                  "ZAN0 持续占用不误报删除成功");
            CloseHandle(handle);
            check(removeTreeWithoutFollowingLinks(dir), "ZAN0 持续占用释放后仍可重试");
        }
    }

    // ---------- ZAN1. 只读的**单件文件** ----------
    {
        const QString path = temp.filePath(QStringLiteral("只读.txt"));
        const bool written = writeBytesAt(path, QByteArrayLiteral("ro"));
        const bool marked = QFile::setPermissions(
            path, QFileDevice::ReadOwner | QFileDevice::ReadUser | QFileDevice::ReadGroup |
                      QFileDevice::ReadOther);
        check(written && marked && !(QFileInfo(path).permissions() & QFileDevice::WriteUser),
              "★ ZAN1 前提：那一件真的落成了只读（没有写位）",
              QStringLiteral("写=%1 标=%2 权限=%3")
                  .arg(written)
                  .arg(marked)
                  .arg(int(QFileInfo(path).permissions())));
        check(removeTreeWithoutFollowingLinks(path) && !QFile::exists(path),
              "★★ ZAN1 只读文件**删得掉**（第一次失败后摘掉只读位再试一次）—— 这是"
              "「删不掉，多半是游戏占着」那句提示不再指错方向的根据");
    }

    // ---------- ZAN2. 一棵目录里躺着只读文件（嵌两层，走的是递归那一条路）----------
    {
        const QString root = temp.filePath(QStringLiteral("实例"));
        const QString nested = root + QStringLiteral("/mods/子目录");
        const bool made = QDir().mkpath(nested);
        const bool wroteNormal = writeBytesAt(root + QStringLiteral("/普通.txt"),
                                              QByteArrayLiteral("正常"));
        const bool wroteRo = writeBytesAt(nested + QStringLiteral("/只读.jar"),
                                          QByteArrayLiteral("只读"));
        QFile::setPermissions(nested + QStringLiteral("/只读.jar"),
                              QFileDevice::ReadOwner | QFileDevice::ReadUser);
        check(made && wroteNormal && wroteRo, "★ ZAN2 前提：那一棵树摆好了");
        // NOTE: 最后那一栏只报**路径**。`check()` 的几个实参求值次序是没保证的 —— 在这里再问
        // 一次 `QDir(root).exists()` 的话，问到的可能是"删之前"那一份，红的时候反而
        // 把人带偏。
        check(removeTreeWithoutFollowingLinks(root) && !QDir(root).exists(),
              "★★ ZAN2 整棵删干净了 —— 中间躺着一件只读文件也不该让「删实例」停在一半",
              QDir::toNativeSeparators(root));
        // 只读位**只有在第一次真的失败之后才动**：正常文件一次成功，权限一个字节都不碰。
        // 这一条只能验"删完之后没人回来把只读位留在别人的文件上"—— 那一件文件已经没了，
        // 所以真正钉住它的是上面那条"删干净了"（多一步摘位不会让结果不同，这里只为记下
        // 这条约定的位置）。
        check(!QFile::exists(nested + QStringLiteral("/只读.jar")),
              "★ ZAN2 那件只读文件本身也没了（不是只把目录拆了）");
    }

    // ---------- ZAN3. 本来就不在：算成功（删两次不会报错）----------
    {
        const QString gone = temp.filePath(QStringLiteral("压根没有/也没有.txt"));
        check(removeTreeWithoutFollowingLinks(gone),
              "★ ZAN3 删一个本来就不存在的东西回 true —— 调用方问的是「它还在不在」，"
              "不是「这一下有没有真干活」");
    }

    // ---------- ZAN4. 里面那根 junction：只摘链接，那一头一个字节都不动 ----------
    {
        const QString outside = temp.filePath(QStringLiteral("外面那一块盘"));
        const QString victim = temp.filePath(QStringLiteral("受害"));
        QDir().mkpath(outside);
        QDir().mkpath(victim);
        const bool outsideWritten =
            writeBytesAt(outside + QStringLiteral("/别人的存档.dat"), QByteArrayLiteral("别删我"));
        const bool victimWritten =
            writeBytesAt(victim + QStringLiteral("/自己的.txt"), QByteArrayLiteral("可以删"));
        const QString link = victim + QStringLiteral("/挂过来的目录");
        const bool madeLink =
            QProcess::execute(QStringLiteral("cmd"),
                              {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                               QDir::toNativeSeparators(link),
                               QDir::toNativeSeparators(outside)}) == 0 &&
            QFileInfo(link).isJunction();
        check(outsideWritten && victimWritten && madeLink,
              "★ ZAN4 前提：真造出一根 junction（mklink /J）");
        if (madeLink) {
            check(removeTreeWithoutFollowingLinks(victim) && !QDir(victim).exists(),
                  "★★ ZAN4 这一棵删掉了（链接本身也算删掉了）");
            check(QFile::exists(outside + QStringLiteral("/别人的存档.dat")) &&
                      QDir(outside).exists(),
                  "★★ ZAN4 链接那一头**一个字节都不动** —— 删实例不许牵连挂在外面的存档/"
                  "网盘目录（`QDir::removeRecursively()` 会跟进去）");
        }
        QDir(outside).removeRecursively();
    }

    // ==================================================================
    // ZAO. 用户点「删除」走的那一条：移进回收站（不是永久删除）
    // ==================================================================
    section("ZAO. 删除走回收站（moveToRecycleBin）：过得去、找不到就失败、链接不追");

    // ---------- ZAO1. 一件普通文件 ----------
    {
        const QString path = temp.filePath(QStringLiteral("回收站-文件.txt"));
        const bool written = writeBytesAt(path, QByteArrayLiteral("recycle me"));
        QString problem = QStringLiteral("还没开始");
        const bool moved = written && moveToRecycleBin(path, &problem);
        check(moved && !QFile::exists(path),
              "★★ ZAO1 单件文件移进回收站：调用成功，盘上那一件已经不在",
              problem);
        check(problem.isEmpty(), "★ ZAO1 成功时不带错误话术", problem);
    }

    // ---------- ZAO2. 一整棵目录（中间躺着一件只读文件）----------
    {
        const QString root = temp.filePath(QStringLiteral("回收站-实例"));
        const QString nested = root + QStringLiteral("/mods/子目录");
        const bool made = QDir().mkpath(nested);
        const bool wrote = writeBytesAt(nested + QStringLiteral("/只读.jar"),
                                        QByteArrayLiteral("ro"));
        QFile::setPermissions(nested + QStringLiteral("/只读.jar"),
                              QFileDevice::ReadOwner | QFileDevice::ReadUser);
        QString problem;
        const bool moved = made && wrote && moveToRecycleBin(root, &problem);
        check(moved && !QDir(root).exists(),
              "★★ ZAO2 整棵目录（含只读文件）一起进回收站 —— 盘上那个目录不见了",
              QStringLiteral("前提=%1 错误=%2").arg(made && wrote).arg(problem));
    }

    // ---------- ZAO3. 本来就不在：算成功（和 removeTree 同一条约定）----------
    {
        const QString gone = temp.filePath(QStringLiteral("压根没有/也没有.txt"));
        check(moveToRecycleBin(gone),
              "★ ZAO3 对一个本来就不存在的东西回 true —— 调用方问的是「它还在不在」");
    }

    // ---------- ZAO4. 树里那根 junction：先剥掉壳子，那一头一个字节都不动 ----------
    {
        const QString outside = temp.filePath(QStringLiteral("回收站-外面那一块盘"));
        const QString victim = temp.filePath(QStringLiteral("回收站-受害"));
        QDir().mkpath(outside);
        QDir().mkpath(victim);
        const bool outsideWritten =
            writeBytesAt(outside + QStringLiteral("/别人的存档.dat"), QByteArrayLiteral("别删我"));
        const bool victimWritten =
            writeBytesAt(victim + QStringLiteral("/自己的.txt"), QByteArrayLiteral("可以删"));
        const QString link = victim + QStringLiteral("/挂过来的目录");
        const bool madeLink =
            QProcess::execute(QStringLiteral("cmd"),
                              {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                               QDir::toNativeSeparators(link),
                               QDir::toNativeSeparators(outside)}) == 0 &&
            QFileInfo(link).isJunction();
        check(outsideWritten && victimWritten && madeLink,
              "★ ZAO4 前提：真造出一根 junction（mklink /J）");
        if (madeLink) {
            QString problem;
            const bool moved = moveToRecycleBin(victim, &problem);
            check(moved && !QDir(victim).exists(),
                  "★★ ZAO4 这一棵进了回收站（盘上没了）", problem);
            check(QFile::exists(outside + QStringLiteral("/别人的存档.dat")) &&
                      QDir(outside).exists(),
                  "★★ ZAO4 链接那一头**一个字节都不动** —— 回收站那一趟不许顺着 junction "
                  "把别人那块盘上的东西搬走");
        }
        QDir(outside).removeRecursively();
    }

    // ---------- ZAO5. 一棵全是链接的壳子：只摘壳子（不进回收站也没有关系）----------
    {
        const QString outside = temp.filePath(QStringLiteral("回收站-链接目标"));
        QDir().mkpath(outside);
        const QString link = temp.filePath(QStringLiteral("回收站-链接本身"));
        const bool made = QProcess::execute(QStringLiteral("cmd"),
                                            {QStringLiteral("/c"), QStringLiteral("mklink"),
                                             QStringLiteral("/J"), QDir::toNativeSeparators(link),
                                             QDir::toNativeSeparators(outside)}) == 0
                          && QFileInfo(link).isJunction();
        check(made, "★ ZAO5 前提：真造出一根 junction");
        if (made) {
            check(moveToRecycleBin(link) && !QFileInfo(link).exists() && QDir(outside).exists(),
                  "★★ ZAO5 链接本身只摘壳子：链接没了、目标目录还在");
        }
        QDir(outside).removeRecursively();
    }

    // ---------- ZAO6. 树里有一件文件被别的进程占着（共享冲突）----------
    {
        // 这就是"游戏还开着去删世界/删实例"在本进程里的等价物（`QFile` 打不开这种锁，
        // 所以走 Win32 的 `FileShare = 0`）。要验的是：删不干净就**照实报失败**，
        // 不许回 true 让界面说"删掉了"。
        const QString victim = temp.filePath(QStringLiteral("回收站-被占用"));
        QDir().mkpath(victim);
        const bool wrote = writeBytesAt(victim + QStringLiteral("/可以删.txt"),
                                        QByteArrayLiteral("released")) &&
                           writeBytesAt(victim + QStringLiteral("/占着的.dat"),
                                        QByteArrayLiteral("held"));
        const QString heldPath = victim + QStringLiteral("/占着的.dat");
        const HANDLE held =
            wrote ? CreateFileW(reinterpret_cast<LPCWSTR>(heldPath.utf16()),
                                GENERIC_READ | GENERIC_WRITE, /*dwShareMode=*/0, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)
                  : INVALID_HANDLE_VALUE;
        check(wrote && held != INVALID_HANDLE_VALUE,
              "★ ZAO6 前提：树里那件文件被 Win32 拿住了（不共享读写删）");
        if (held != INVALID_HANDLE_VALUE) {
            QString problem;
            const bool moved = moveToRecycleBin(victim, &problem);
            check(!moved && !problem.isEmpty(),
                  "★★ ZAO6 被占着时整棵进不了回收站，而且回 false + 一句原因", problem);
            // 内容这一趟读不了（我们自己那个句柄就是不共享读的，这正是"游戏开着"的样子）：
            // 占着的时候只验"还在原处、大小没变"，等放开之后再逐字节比。
            check(QFileInfo::exists(heldPath) && QFileInfo(heldPath).size() == 4,
                  "★★★ ZAO6 被占着的那一件还在原处、大小没变",
                  QStringLiteral("目录还在=%1 条目=[%2]")
                      .arg(QDir(victim).exists())
                      .arg(QDir(victim).entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
                               .join(QLatin1Char(','))));
            CloseHandle(held);
            check(readBytesAt(heldPath) == QByteArrayLiteral("held"),
                  "★★★ ZAO6 放开之后逐字节读回来还是原来那四个字节（失败那一趟没改写它）");
        }
        // 放开之后同一棵必须过得去：证明挡住它的确实是那把共享锁。
        {
            QString problem;
            const bool moved = moveToRecycleBin(victim, &problem);
            check(moved && !QDir(victim).exists(),
                  "★ ZAO6 放开之后那一棵照常进回收站", problem);
        }
    }
}

