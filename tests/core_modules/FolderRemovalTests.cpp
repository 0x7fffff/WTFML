#include "Support.hpp"

void runFolderRemovalTests() {
    section("ZAN. 删一棵目录（FolderManager）：只读文件删得掉，链接那一头一个字节都不动");

    QTemporaryDir temp;
    check(temp.isValid(), "ZAN1 前提：临时目录建出来了", temp.errorString());
    if (!temp.isValid()) {
        return;
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
}

