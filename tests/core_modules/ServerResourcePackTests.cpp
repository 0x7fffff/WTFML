#include "Support.hpp"

void runServerResourcePackTests() {
    section("ZAG. 服务端资源包：条目表判据 / 真 zip 的 SHA-1");

    // ---------- ZAG0. 条目表像不像资源包 ----------
    {
        const ServerResourcePack::EntriesCheck good = ServerResourcePack::checkEntries(
            {QStringLiteral("pack.mcmeta"), QStringLiteral("assets/"),
             QStringLiteral("assets/minecraft/textures/block/stone.png")});
        check(good.ok && good.hasPackMeta && good.hasAssets && good.problem.isEmpty(),
              "★★ ZAG0 最外层有 `pack.mcmeta`、也有 `assets/` ⇒ 可以用",
              good.problem);

        // "一键压缩"那类工具会写反斜杠；zip 规范里条目名只用 `/`，但现实里有这种包。
        const ServerResourcePack::EntriesCheck slashed = ServerResourcePack::checkEntries(
            {QStringLiteral("pack.mcmeta"), QStringLiteral("assets\\minecraft\\sounds.json")});
        check(slashed.ok && slashed.hasAssets,
              "★ ZAG0 反斜杠的条目名照样认得出 assets/（不认的话这个包会被判成「不是资源包」）");

        // 最常见的错：整个文件夹一起压 —— 游戏只认最外层那一份。
        const ServerResourcePack::EntriesCheck nested = ServerResourcePack::checkEntries(
            {QStringLiteral("MyPack/pack.mcmeta"), QStringLiteral("MyPack/assets/x.png")});
        check(!nested.ok && nested.problem.contains(QStringLiteral("根目录"))
                  && nested.problem.contains(QStringLiteral("重新打包")),
              "★★ ZAG0 `pack.mcmeta` 套在文件夹里 ⇒ 不许用，而且要说清「重新压到最外层」",
              nested.problem);

        const ServerResourcePack::EntriesCheck missing = ServerResourcePack::checkEntries(
            {QStringLiteral("assets/minecraft/x.png")});
        check(!missing.ok && missing.problem.contains(QStringLiteral("pack.mcmeta")),
              "★★ ZAG0 没有 `pack.mcmeta` ⇒ 不是资源包", missing.problem);

        const ServerResourcePack::EntriesCheck dataPack = ServerResourcePack::checkEntries(
            {QStringLiteral("pack.mcmeta"), QStringLiteral("data/minecraft/loot_tables/x.json")});
        check(dataPack.ok && !dataPack.hasAssets,
              "ZAG0 只有 pack.mcmeta + data/ 的条目表本身判得过去（拦不拦是下一步的事）");

        const ServerResourcePack::EntriesCheck wrongKind = ServerResourcePack::checkEntries(
            {QStringLiteral("data/minecraft/loot_tables/x.json")});
        check(!wrongKind.ok && wrongKind.looksLikeDataPack
                  && wrongKind.problem.contains(QStringLiteral("数据包")),
              "★★ ZAG0 只有 data/ 没有 assets/ ⇒ 是**数据包**，那句说法要换成「放错地方」",
              wrongKind.problem);

        const ServerResourcePack::EntriesCheck empty = ServerResourcePack::checkEntries({});
        check(!empty.ok && !empty.problem.isEmpty(),
              "★ ZAG0 空压缩包 ⇒ 不许用，而且要有话说", empty.problem);
    }

    // ---------- ZAG1. 真 zip：inspect() 读得动、SHA-1 是对的 ----------
    {
        QTemporaryDir root;
        check(root.isValid(), "ZAG1 临时目录建得出来");
        if (!root.isValid()) {
            return;
        }
        const QString filesDir = root.path() + QStringLiteral("/files");
        check(QDir().mkpath(filesDir + QStringLiteral("/assets/minecraft")),
              "ZAG1 造得出一棵小目录");
        const auto writeFile = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) {
                return false;
            }
            return file.write(bytes) == bytes.size();
        };
        const QString metaPath = filesDir + QStringLiteral("/pack.mcmeta");
        const QString assetPath = filesDir + QStringLiteral("/assets/minecraft/x.txt");
        check(writeFile(metaPath, QByteArrayLiteral("{\"pack\":{\"pack_format\":15}}"))
                  && writeFile(assetPath, QByteArrayLiteral("hello")),
              "ZAG1 两份源文件写得出来");

        const QString packPath = root.path() + QStringLiteral("/pack.zip");
        QString error;
        const bool made =
            ZipUtil::createZip(packPath,
                               {{QStringLiteral("pack.mcmeta"), metaPath},
                                {QStringLiteral("assets/minecraft/x.txt"), assetPath}},
                               &error);
        check(made, "★ ZAG1 真打一个 zip（写侧就是导出整合包那一条 `createZip`）", error);
        if (made) {
            const ServerResourcePack::Inspect inspected = ServerResourcePack::inspect(packPath);
            QFile packFile(packPath);
            QByteArray bytes;
            if (packFile.open(QIODevice::ReadOnly)) {
                bytes = packFile.readAll();
            }
            const QString expected = FileHash::digestOf(bytes, FileHash::Algorithm::Sha1);
            check(inspected.ok && !expected.isEmpty() && inspected.sha1 == expected
                      && inspected.sha1.size() == 40
                      && inspected.sha1 == inspected.sha1.toLower(),
                  "★★ ZAG1 `inspect()` 算出的 SHA-1 就是这份 zip 的真摘要（小写 40 位）",
                  QStringLiteral("%1 / 应为 %2").arg(inspected.sha1, expected));
            check(inspected.bytes == bytes.size() && inspected.fileCount == 2,
                  "★ ZAG1 大小与文件数都对得上（目录条目不算文件）",
                  QStringLiteral("%1 字节 / %2 个文件")
                      .arg(inspected.bytes)
                      .arg(inspected.fileCount));
            check(ServerResourcePack::fileCountText(inspected) == QStringLiteral("2 个文件"),
                  "ZAG1 「N 个文件」那句话的唯一出处",
                  ServerResourcePack::fileCountText(inspected));

            // 同一份文件再查一遍：结果要一模一样（SHA-1 不是随机的）。
            const ServerResourcePack::Inspect again = ServerResourcePack::inspect(packPath);
            check(again.ok && again.sha1 == inspected.sha1,
                  "ZAG1 同一份 zip 查两遍给的摘要一样");
        }

        // 不是 zip 的那一份：改过扩展名的 rar / 坏文件都长这样。
        const QString junkPath = root.path() + QStringLiteral("/junk.zip");
        check(writeFile(junkPath, QByteArrayLiteral("this is not a zip")),
              "ZAG1 造一份「假 zip」");
        const ServerResourcePack::Inspect junk = ServerResourcePack::inspect(junkPath);
        check(!junk.ok && junk.problem.contains(QStringLiteral("ZIP")),
              "★★ ZAG1 不是 zip 的文件 ⇒ 明说「这不是一个 zip」，不是一句读失败", junk.problem);

        // 套在文件夹里的那一份：条目表能读出来，但判据得拦住（游戏认不出它）。
        const QString nestedPath = root.path() + QStringLiteral("/nested.zip");
        QString nestedError;
        const bool nestedMade = ZipUtil::createZip(
            nestedPath, {{QStringLiteral("MyPack/pack.mcmeta"), metaPath}}, &nestedError);
        if (nestedMade) {
            const ServerResourcePack::Inspect nested = ServerResourcePack::inspect(nestedPath);
            check(!nested.ok && nested.problem.contains(QStringLiteral("根目录")),
                  "★★ ZAG1 套了一层的 zip 在 `inspect()` 那一层就被拦住（不白算一遍 SHA-1）",
                  nested.problem);
        }

        const ServerResourcePack::Inspect missing =
            ServerResourcePack::inspect(root.path() + QStringLiteral("/nope.zip"));
        check(!missing.ok && missing.problem.contains(QStringLiteral("未找到文件")),
              "★ ZAG1 文件不在 ⇒ 那句话说的是「找不到」，不是「读不出来」", missing.problem);
    }
}

