#include "Support.hpp"

void runUpdateRollbackTests() {
    section("ZU. 更新中心：更新前的轻量回滚点（§7.1）");

    const QString originalDir = AppPaths::minecraftDir();
    QTemporaryDir temp;
    check(temp.isValid(), "ZU0 临时数据根目录建出来了");
    if (!temp.isValid()) {
        return;
    }
    AppPaths::setMinecraftDir(temp.path());

    const QString instanceId = QStringLiteral("1.20.1-forge-47.2.0");
    // 实例 id 里带 `/`（`isKnownGameVersion()` 那一档放行的名字）：这一层得把它换成
    // 合法的一段，否则这一份回滚点会落到别的目录里去。
    const QString slashyId = QStringLiteral("20w14∞/test");
    const QString instanceDir = temp.path() + QStringLiteral("/versions/") + instanceId;
    const QString modsDir = instanceDir + QStringLiteral("/mods");
    QDir().mkpath(modsDir);
    QDir().mkpath(instanceDir + QStringLiteral("/shaderpacks"));

    const auto writeAt = [](const QString &path, const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(bytes);
        }
    };
    const auto readAt = [](const QString &path) {
        QByteArray bytes;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            bytes = file.readAll();
        }
        return bytes;
    };

    const QString jei = modsDir + QStringLiteral("/jei.jar");
    const QString shader = instanceDir + QStringLiteral("/shaderpacks/光影.zip");
    writeAt(jei, QByteArrayLiteral("JEI-1.0-OLD"));
    writeAt(shader, QByteArrayLiteral("SHADER-OLD"));

    // ---------- ZU1. 抄一份 ----------
    const QStringList relative{
        QStringLiteral("mods/jei.jar"),
        QStringLiteral("shaderpacks/光影.zip"),
        QStringLiteral("mods/新加的.jar"),  // 更新新添的那个：当时**不在**
    };
    QString problem;
    const UpdateRollback::Snapshot snapshot =
        UpdateRollback::create(instanceDir, instanceId, relative, &problem);
    check(snapshot.isValid(), "ZU1 回滚点摆成了", problem);
    check(snapshot.fileCount() == 2, "★ ZU1 清单里「当时在」的是 2 个（第三个当时不在）",
          QString::number(snapshot.fileCount()));
    check(snapshot.addedCount() == 1,
          "★★ ZU1 当时**不在**的那一个也进了清单（撤销时要把更新新添的那份删掉）",
          QString::number(snapshot.addedCount()));
    check(snapshot.totalBytes() == 11 + 10,
          "ZU1 大小是**数出来的**（JEI-1.0-OLD 11 + SHADER-OLD 10）",
          QString::number(snapshot.totalBytes()));
    // 落点：`<数据目录>/cache/update-rollback/<实例id>/`，原件摆在 `files/` 底下、相对路径
    // 原样保留（"这份回滚点是哪个文件的"一眼看得出）。
    check(snapshot.dir == UpdateRollback::snapshotDir(instanceId) &&
              snapshot.dir.startsWith(temp.path() + QStringLiteral("/cache/update-rollback/")),
          "ZU1 落在 `<数据目录>/cache/update-rollback/<实例id>/`", snapshot.dir);
    check(QFileInfo::exists(snapshot.dir + QStringLiteral("/files/mods/jei.jar")) &&
              QFileInfo::exists(snapshot.dir + QStringLiteral("/files/shaderpacks/光影.zip")),
          "★ ZU1 原件照着**相对路径**摆在 `files/` 底下（还原不用另记一张映射表）");
    check(QFileInfo::exists(snapshot.dir + QStringLiteral("/") + UpdateRollback::manifestName()),
          "ZU1 那张清单也在（没有它这一份回滚点用不了）");

    // ---------- ZU2. 上手改文件，再退回去 ----------
    writeAt(jei, QByteArrayLiteral("JEI-2.0-NEW"));
    writeAt(shader, QByteArrayLiteral("SHADER-NEW"));
    writeAt(modsDir + QStringLiteral("/新加的.jar"), QByteArrayLiteral("BRAND-NEW"));
    {
        const UpdateRollback::RestoreResult result =
            UpdateRollback::restore(snapshot, instanceDir);
        check(result.ok() && result.restored == 2 && result.removed == 1,
              "★ ZU2 退回去：换回来 2 个、删掉 1 个（更新新添的那一份）",
              QStringLiteral("restored=%1 removed=%2 failed=%3")
                  .arg(result.restored)
                  .arg(result.removed)
                  .arg(result.failed));
        check(readAt(jei) == QByteArrayLiteral("JEI-1.0-OLD"),
              "★★ ZU2 退回来的是**内容**（不只是「文件在那儿」）", QString::fromUtf8(readAt(jei)));
        check(readAt(shader) == QByteArrayLiteral("SHADER-OLD"),
              "ZU2 那个中文路径的文件也退回来了");
        check(!QFileInfo::exists(modsDir + QStringLiteral("/新加的.jar")),
              "★★ ZU2 更新新添的那一个**消失**了（「这个文件原来不在」也是一件事）");
    }

    // ---------- ZU3. 只留最新那一份 ----------
    //
    // 再摆一份（这一次少了 JEI）：上一趟那份要**整个**没掉 —— 留着的话，撤销按钮说的
    // 和用户手上那份东西就对不上了。
    {
        const UpdateRollback::Snapshot second = UpdateRollback::create(
            instanceDir, instanceId, QStringList{QStringLiteral("shaderpacks/光影.zip")});
        check(second.isValid() && second.entries.size() == 1,
              "ZU3 第二份回滚点摆成了（只记了 1 条）");
        const UpdateRollback::Snapshot back = UpdateRollback::read(instanceId);
        check(back.isValid() && back.entries.size() == 1,
              "★★ ZU3 盘上**只剩最新那一份**（上一趟那份整个删掉了）",
              QString::number(back.entries.size()));
        check(!QFileInfo::exists(back.dir + QStringLiteral("/files/mods/jei.jar")),
              "ZU3 上一趟那份的原件跟着没了");
    }

    {
        const QString dir = UpdateRollback::snapshotDir(instanceId);
        check(QDir().rename(dir, dir + QStringLiteral(".previous")),
              "ZU3 interrupted preparation retains prior snapshot");
        check(UpdateRollback::read(instanceId).isValid(),
              "ZU3 orphaned previous snapshot remains readable before retry");
        QDir().mkpath(dir + QStringLiteral("/files"));
        writeAt(dir + QStringLiteral("/files/incomplete"), QByteArrayLiteral("partial"));
        const auto retry = UpdateRollback::create(instanceDir, instanceId,
            QStringList{QStringLiteral("shaderpacks/光影.zip")}, nullptr, true);
        check(retry.isValid() && UpdateRollback::discard(instanceId)
                  && UpdateRollback::read(instanceId).isValid(),
              "ZU3 rejected retry after interrupted copy preserves last good rollback");
    }

    // ---------- ZU4. 读 / 撤干净 / 清单读不动 ----------
    {
        bool ok = false;
        const UpdateRollback::Snapshot read = UpdateRollback::read(instanceId, &ok);
        check(ok && read.isValid() && read.instanceId == instanceId,
              "ZU4 读得动那一份，而且说得清它是哪个实例的", read.instanceId);
        check(read.createdAt.isValid(), "ZU4 时间戳也回来了（界面那句话说「什么时候留的」）");

        check(UpdateRollback::discard(instanceId), "ZU4 撤掉了");
        check(!QDir(UpdateRollback::snapshotDir(instanceId)).exists(),
              "★ ZU4 整个目录都没了（不是只删清单）");
        bool noneOk = false;
        UpdateRollback::read(instanceId, &noneOk);
        check(!noneOk, "★★ ZU4 撤完之后读不到「能用的那一份」（那正是「撤销」那颗按钮藏起来的判据）");
        check(UpdateRollback::discard(instanceId),
              "ZU4 本来就没有再撤一次也不算错（幂等）");

        // 清单读不动 = 这一份回滚点用不了：**数据不全的东西不能拿去还原**。
        const QString dir = UpdateRollback::snapshotDir(instanceId);
        QDir().mkpath(dir + QStringLiteral("/files/mods"));
        writeAt(dir + QStringLiteral("/files/mods/jei.jar"), QByteArrayLiteral("JEI-1.0-OLD"));
        writeAt(dir + QStringLiteral("/") + UpdateRollback::manifestName(),
                 QByteArrayLiteral("{ 这不是 json"));
        QDir().mkpath(instanceDir + QStringLiteral("/mods"));
        writeAt(jei, QByteArrayLiteral("JEI-2.0-NEW"));
        bool brokenOk = false;
        QString brokenProblem;
        const UpdateRollback::Snapshot broken =
            UpdateRollback::read(instanceId, &brokenOk, &brokenProblem);
        check(!brokenOk && !broken.isValid() && !brokenProblem.isEmpty(),
              "★★ ZU4 清单读不动 ⇒ **不算「有一份」**（残缺的回滚点不能拿去还原）", brokenProblem);
        // 而且这时候**一个文件都不许动**：调用方拿到的 invalid 快照去做 restore 是没人拦的，
        // 所以这一条说的是"读侧不撒谎"。
        check(readAt(jei) == QByteArrayLiteral("JEI-2.0-NEW"),
              "ZU4 读那一趟本身一个字节都没碰");
        UpdateRollback::discard(instanceId);
    }

    // ---------- ZU5. 路径的合法性（我们自己算错的那些） ----------
    //
    // 清单里的路径是我们按目录走出来的；真出现一个不合法的，那是**上面那一层算错了**，
    // 当场拒绝比往实例目录外面写/删东西强。
    {
        QString badProblem;
        const UpdateRollback::Snapshot bad = UpdateRollback::create(
            instanceDir, instanceId,
            QStringList{QStringLiteral("../外面.jar"), QStringLiteral("mods/好的.jar")},
            &badProblem);
        check(!bad.isValid() && !badProblem.isEmpty(),
              "★★ ZU5 带 `..` 的路径 ⇒ **整份拒绝**（不就地跳过、也不往外面写）", badProblem);

        // 实例 id 里带 `/` 的那一档：目录名要换成合法的一段，但**账上记的还是原来的 id**。
        QString slashyProblem;
        const UpdateRollback::Snapshot slashy = UpdateRollback::create(
            instanceDir, slashyId, QStringList{QStringLiteral("mods/jei.jar")}, &slashyProblem);
        check(slashy.isValid(), "ZU5 实例 id 里带 `/` 也能摆一份", slashyProblem);
        check(slashy.instanceId == slashyId,
              "★ ZU5 账上记的还是**原来的 id**（改成合法的那一段只是目录名）", slashy.instanceId);
        check(slashy.dir.startsWith(temp.path() + QStringLiteral("/cache/update-rollback/")) &&
                  !slashy.dir.contains(QStringLiteral("20w14∞/test")),
              "★ ZU5 而目录名里那个 `/` 被换掉了（它得是个合法的一段）", slashy.dir);
        // 清洗是**多对一**的（`a/b` 和 `a\b` 都变成 `a-b`），所以末尾还挂了原来 id 的
        // 哈希：两个实例共用一份回滚点的后果是"还原的时候把 A 的旧文件写进 B 里"。
        check(UpdateRollback::snapshotDir(QStringLiteral("a/b")) !=
                  UpdateRollback::snapshotDir(QStringLiteral("a\\b")),
              "★★ ZU5 清洗之后**还是一一对应**（两个实例不许共用一份回滚点）");
        bool slashyOk = false;
        const UpdateRollback::Snapshot slashyBack = UpdateRollback::read(slashyId, &slashyOk);
        check(slashyOk && slashyBack.instanceId == slashyId,
              "★★ ZU5 按原来的 id 读得回来（存和读用的是同一个换算）");
        UpdateRollback::discard(slashyId);
    }

    AppPaths::setMinecraftDir(originalDir);
    check(AppPaths::minecraftDir() == originalDir, "ZU9 数据根目录换回来了");
}

