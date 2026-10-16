#include "InstanceEditorController.hpp"
#include "MainWindow.hpp"
#include "ConfigFileEditorPage.hpp"
#include "ResourcePackPage.hpp"
#include "manage/ResourcePackOptions.hpp"
#include "manage/VersionManager.hpp"
#include "ConfigFileText.hpp"
#include "InlineMessage.hpp"
#include "InstanceSettingsDialog.hpp"
#include "MainWindowUI.hpp"
#include "OverlayHost.hpp"
#include "WorldSettingsPage.hpp"
#include "manage/BackupFiles.hpp"
#include "manage/FolderManager.hpp"
#include "controllers/InstanceMaintenanceController.hpp"
#include "manage/WorldFiles.hpp"
#include "util/AppPaths.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QSaveFile>
#include <QJsonDocument>
#include <QVersionNumber>
#include <algorithm>

namespace {
constexpr int kConfigBackupGenerations = 20;

QList<ResourcePackPage::Pack> resourcePacksIn(const QString &dir) {
    QList<ResourcePackPage::Pack> packs;
    if (dir.isEmpty()) return packs;
    for (const auto &file : QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name)) {
        if (file.isSymLink()) continue;
        const bool folderPack = file.isDir() && QFileInfo::exists(file.absoluteFilePath() + QStringLiteral("/pack.mcmeta"));
        if (!folderPack && (!file.isFile() || file.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) != 0)) continue;
        ResourcePackPage::Pack pack{QStringLiteral("file/") + file.fileName(), file.fileName(), {}};
        if (!ResourcePackOptions::safeLocal(pack.id)) continue;
        pack.detail = QDir::toNativeSeparators(file.absoluteFilePath());
        if (folderPack) {
            QFile meta(file.absoluteFilePath() + QStringLiteral("/pack.mcmeta"));
            if (meta.open(QIODevice::ReadOnly) && meta.size() <= 1024 * 1024) {
                const auto object = QJsonDocument::fromJson(meta.read(1024 * 1024 + 1)).object()
                                        .value(QStringLiteral("pack")).toObject();
                const QString description = object.value(QStringLiteral("description")).toString();
                if (!description.isEmpty()) pack.detail += QLatin1Char('\n') + description;
                if (object.value(QStringLiteral("pack_format")).isDouble())
                    pack.detail += QStringLiteral("\npack_format: %1").arg(object.value(QStringLiteral("pack_format")).toInt());
            }
        }
        packs.append(pack);
    }
    return packs;
}

QString resourcePackFormatProblem(const QString &version, const ResourcePackOptions::Document &document) {
    for (const auto &id : document.selected) {
        if (!ResourcePackOptions::isLocal(id) && id.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
            return QStringLiteral("检测到旧版资源包标识，暂不自动转换，请在游戏中调整。");
    }
    // 已存在 file/ 标识可确认实例使用现代文件身份；空表则只对明确支持的正式版开放。
    if (std::any_of(document.selected.cbegin(), document.selected.cend(), ResourcePackOptions::isLocal)) return {};
    qsizetype suffix = 0;
    const auto number = QVersionNumber::fromString(version, &suffix);
    if (suffix != version.size() || number.isNull()
        || !((number.majorVersion() == 1 && number.minorVersion() >= 20) || number.majorVersion() >= 26))
        return QStringLiteral("未确认此版本的资源包标识格式；首版支持 1.20+ 正式版或已有 file/ 标识的实例。");
    return {};
}

WorldSettingsPage::WorldView worldViewOf(const QString &worldDir,
                                         const WorldFiles::LevelData &data) {
    WorldSettingsPage::WorldView view;
    view.dirName = QFileInfo(worldDir).fileName();
    view.gzipped = data.info.gzipped;
    view.hasLevelName = data.info.hasLevelName;
    view.levelName = data.info.levelName;
    view.hasVersionName = data.info.hasVersionName;
    view.versionName = data.info.versionName;
    view.hasFormat = data.info.hasDataVersion;
    view.formatText = data.info.hasDataVersion ? QString::number(data.info.dataVersion) : QString();
    view.hasLastPlayed = data.info.hasLastPlayed;
    view.lastPlayedMs = data.info.lastPlayedMs;
    view.hasAllowCommands = data.info.hasAllowCommands;
    view.allowCommands = data.info.allowCommands;
    view.hasHardcore = data.info.hasHardcore;
    view.hardcore = data.info.hardcore;
    view.hasDifficulty = data.info.hasDifficulty;
    view.difficulty = data.info.difficulty;
    view.hasRules = data.info.hasRules;
    QHash<QString, QString> onDisk;
    for (const NbtTree::GameRule &rule : data.info.rules) onDisk.insert(rule.name, rule.value);
    for (const NbtTree::GameRule &rule : WorldFiles::allGameRules(data.info.rules)) {
        WorldSettingsPage::Rule row;
        row.name = rule.name;
        row.value = rule.value;
        row.inFile = onDisk.contains(rule.name);
        switch (NbtTree::ruleKind(rule.value)) {
        case NbtTree::ValueKind::Bool: row.kind = WorldSettingsPage::Kind::Bool; break;
        case NbtTree::ValueKind::Int: row.kind = WorldSettingsPage::Kind::Int; break;
        case NbtTree::ValueKind::Text: row.kind = WorldSettingsPage::Kind::Text; break;
        }
        view.rules.append(row);
    }
    return view;
}

QList<NbtTree::GameRule> changedRules(const WorldSettingsPage::Delta &delta) {
    QList<NbtTree::GameRule> out;
    for (const QPair<QString, QString> &pair : delta.rules) out.append({pair.first, pair.second});
    return out;
}

void applyWorldViewTo(WorldFiles::LevelData *data, const WorldSettingsPage::Delta &delta) {
    WorldFiles::WorldInfo &info = data->info;
    info.rules.clear();
    if (delta.nameChanged) { info.hasLevelName = true; info.levelName = delta.nameValue; }
    if (delta.allowCommandsChanged) { info.hasAllowCommands = true; info.allowCommands = delta.allowCommandsValue; }
    if (delta.hardcoreChanged) { info.hasHardcore = true; info.hardcore = delta.hardcoreValue; }
    if (delta.difficultyChanged) { info.hasDifficulty = true; info.difficulty = delta.difficultyValue; }
}
} // namespace

InstanceEditorController::InstanceEditorController(MainWindow *owner) : QObject(owner), m_owner(owner) {}

void InstanceEditorController::refreshResourcePacks(InstanceSettingsDialog *dialog, const QString &instanceId) {
    if (dialog == nullptr || dialog->resourcePackPage() == nullptr) return;
    const QString dir = m_owner->instanceFileDirs(instanceId).value(QStringLiteral("resourcepack"));
    const QString root = QFileInfo(dir).absolutePath();
    const auto snapshot = ResourcePackOptions::read(root + QStringLiteral("/options.txt"));
    QString problem = snapshot.problem;
    if (problem.isEmpty()) problem = resourcePackFormatProblem(
        m_owner->m_versionManager->instanceGameVersion(instanceId), snapshot.document);
    if (dir.isEmpty() || !QDir(root).exists()) problem = tr("实例目录不可用。");
    if (m_owner->runningVersionIds().contains(instanceId)) problem = tr("此实例正在运行，请退出游戏后再调整资源包。");
    dialog->resourcePackPage()->setPacks(resourcePacksIn(dir), snapshot.document.selected,
                                        snapshot.document.bytes, snapshot.exists, problem);
}

void InstanceEditorController::saveResourcePacks(InstanceSettingsDialog *dialog, const QString &instanceId,
                                                const QStringList &ids, const QByteArray &expected, bool expectedExists) {
    if (dialog == nullptr) return;
    const QString dir = m_owner->instanceFileDirs(instanceId).value(QStringLiteral("resourcepack"));
    const QString root = QFileInfo(dir).absolutePath();
    QString problem;
    if (dir.isEmpty() || !QDir(root).exists()) problem = tr("实例目录不可用。");
    if (m_owner->runningVersionIds().contains(instanceId)) problem = tr("此实例正在运行，未修改资源包设置。");
    if (problem.isEmpty()) problem = resourcePackFormatProblem(
        m_owner->m_versionManager->instanceGameVersion(instanceId), ResourcePackOptions::parse(expected));
    QSet<QString> available;
    for (const auto &pack : resourcePacksIn(dir)) available.insert(pack.id);
    const QString backupRoot = AppPaths::minecraftDir() + QStringLiteral("/cache/resource-pack-backups/") + instanceId;
    if (problem.isEmpty()) ResourcePackOptions::save(root + QStringLiteral("/options.txt"), expected,
                                                    expectedExists, ids, available, backupRoot, &problem);
    // 无论成功失败，界面都重新读取事实；失败不留下一个看似已经生效的名单。
    refreshResourcePacks(dialog, instanceId);
    if (!problem.isEmpty()) InlineMessage::warning(m_owner->m_ui->overlay(), tr("资源包设置未保存"), problem);
    else m_owner->appendLog(tr("已保存资源包启用顺序：%1").arg(instanceId));
}

QString InstanceEditorController::latestConfigBackup(const QString &instanceId, const QString &path) const {
    return InstanceMaintenanceController::latestConfigBackup(
        instanceId, m_owner->instanceFileDirs(instanceId).value(QStringLiteral("config")), path);
}

void InstanceEditorController::pruneConfigBackups(const QString &instanceId, int keep) {
    InstanceMaintenanceController::pruneConfigBackups(instanceId, keep);
}

void InstanceEditorController::openConfigFile(InstanceSettingsDialog *dialog, const QString &path) {
    if (dialog == nullptr) {
        return;
    }
    // ★ **打开之前**先量一眼盘上那一份（见 ConfigFileText::Stamp）：保存前拿它比对，对不上
    //   就说明这中间有别人动过（游戏退出时写回 / 另一个编辑器 / 模组自己的迁移），那一趟
    //   不许静静盖掉。
    //
    // ★ 顺序要紧：量在**读之前**。反过来的话，万一有人正好在"我们读完"和"我们量"之间写了
    //   一次，量到的就是**新**那一份 —— 保存时会判成"没动过"，正好把那次改动盖掉。这个方向
    //   的错更贵，所以宁可把"读完就被改"判成"改过"（多问一句，用户点一下就过去了）。
    const QFileInfo onDisk(path);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        // 会挡住用户要做的事 ⇒ 必须弹出来（见 .hpp 那条硬约束）。多半是文件在那一下被
        // 别的东西独占着（游戏正开着、杀毒在扫），或者是权限。
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("打不开这个文件"),
                               m_owner->tr("%1\n\n%2").arg(QDir::toNativeSeparators(path),
                                                  file.errorString()));
        return;
    }
    const QByteArray raw = file.readAll();
    const QString readError = file.errorString();
    const bool readFailed = file.error() != QFileDevice::NoError;
    file.close();
    if (readFailed) {
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("读不完这个文件"),
                               m_owner->tr("%1\n\n%2").arg(QDir::toNativeSeparators(path), readError));
        return;
    }
    ConfigFileText::Stamp stamp;
    if (onDisk.exists()) {
        stamp.size = onDisk.size();
        stamp.mtimeMs = onDisk.lastModified().toMSecsSinceEpoch();
    }
    // 解码**不是解析**（这一页压根不认格式，见 ConfigFileText 那一段）：它只认 BOM、认
    // 行尾、认这一份是不是 UTF-8 —— 前两样要原样存着，改完写回去时一个字都不能变。
    dialog->openConfigEditor(path, ConfigFileText::decode(raw), stamp);
    // 这个文件留过备份没有 —— 那一格那颗「还原上一次」画不画就看这一位（**必须排在
    // openConfigEditor 之后**：setFile() 会把这一位清成 false）。
    const QString instanceId = m_owner->instanceIdOfPath(path);
    dialog->setConfigBackupAvailable(!m_owner->latestConfigBackup(instanceId, path).isEmpty());
}

void InstanceEditorController::saveConfigFile(InstanceSettingsDialog *dialog, const QString &path,
                                const QByteArray &bytes) {
    if (dialog == nullptr) {
        return;
    }
    ConfigFileEditorPage *editor = dialog->configFilePage();

    // ---------- 闸 ①：这个实例正开着游戏 ⇒ 不写 ----------
    //
    // ★ 这不是"劝一句"，是**不写**：游戏手里有一份配置的内存副本（模组启动时读进去的），
    //   它退出（或者用户在游戏里改一次设置）的时候会**整份写回**盘上 —— 现在写下去的东西
    //   会被静默盖掉，而用户看到的是「我改了没用」，回头只会以为这个启动器有毛病。判据和
    //   「游戏还在跑、窗口不许关」那一趟用的是同一份（runningVersionIds()，一个字都没另起）。
    const QString instanceId = m_owner->instanceIdOfPath(path);
    if (!instanceId.isEmpty() && m_owner->runningVersionIds().contains(instanceId)) {
        InlineMessage::warning(
            m_owner->m_ui->overlay(), m_owner->tr("这个实例正开着游戏"),
            m_owner->tr("%1\n\n游戏里那份配置是它自己读进去的，它退出（或者你在游戏里改一次设置）的"
               "时候会整份写回来，现在存下去的东西会被它盖掉。\n\n先把游戏关掉，再回来改。")
                .arg(QDir::toNativeSeparators(path)));
        return;
    }

    // ---------- 闸 ②：盘上那份还是我打开时那一份吗 ----------
    //
    // ★ 这里比的是 **大小 + 修改时间**（见 ConfigFileText::Stamp 那段，两样一起才够用），
    //   比的是"我打开它的时候"和"现在"—— 中间别人动过就必须问一句，**不许静静盖掉**。
    const QFileInfo now(path);
    ConfigFileText::Stamp onDisk;
    if (now.exists()) {
        onDisk.size = now.size();
        onDisk.mtimeMs = now.lastModified().toMSecsSinceEpoch();
    }
    // 打开那一刻**量到过**才比：两边都得是"量过的"才有意义（没量到的那一份是合成的、
    // 或者当时就量不动）。对不上 = 中间有人动过，或者文件干脆没了。
    if (editor != nullptr && editor->stamp().valid() && editor->stamp() != onDisk) {
        if (!now.exists()) {
            // 文件被删了（或者整个目录被挪走）。框里这一份是它最后的样子 —— 写下去等于把它
            // 重新建出来，所以也得问一句（用户可能正是为了"让它按默认值重生成"才删的）。
            const int picked = InlineMessage::ask(
                m_owner->m_ui->overlay(), MessageKind::Question, m_owner->tr("盘上那个文件没有了"),
                m_owner->tr("%1\n\n打开它的时候它还在，现在盘上已经没有了（被删掉、或者你把它挪走了）。"
                   "\n\n现在存下去，它会照你框里这一份重新建出来。")
                    .arg(QDir::toNativeSeparators(path)),
                {m_owner->tr("重新写出来")}, -1, -1, false);
            if (picked != 0) {
                return;  // 关掉 / Esc：盘上、框里都原样
            }
        } else {
            // ★ 三颗出口，而且**默认那颗是"什么都不做"**（`defaultIndex = -1`）：这是一件
            //   会毁掉别人改动的事，误按一个回车不该把它办了（和那几处危险动作同一个规矩）。
            const int picked = InlineMessage::ask(
                m_owner->m_ui->overlay(), MessageKind::Question, m_owner->tr("盘上那份被人改过了"),
                m_owner->tr("%1\n\n这份文件在你打开之后被别的东西改过（多半是游戏退出时写回，也可能"
                   "是另一个编辑器）。现在存下去，改它的那一次就没有了。")
                    .arg(QDir::toNativeSeparators(path)),
                {m_owner->tr("盖掉它"), m_owner->tr("读盘上那一份")}, -1, 0, false,
                m_owner->tr("「读盘上那一份」= 你在这儿改的那些丢掉，框里换成盘上现在的内容。"));
            if (picked == 1) {
                // 用户要的是盘上那一份：重读一遍（**不写盘**）。走的是和点开一个文件同一条路
                // —— 它顺手把 stamp 和"留过备份没有"都重新认一遍。
                m_owner->openConfigFile(dialog, path);
                return;
            }
            if (picked != 0) {
                return;  // 关掉 / Esc：什么都不做
            }
            // picked == 0（盖掉它）：往下走。**这一趟照样给盘上那一份留备份** —— 所以"盖掉"
            // 也不是不可逆的，这正是这一整套东西的用处。
        }
    }

    // ---------- 闸 ③：写之前把盘上那一份原样留一份 ----------
    //
    // ★ 备份的是**盘上那一份**（不是框里这一份）：这一页说的"上一次"是"我上一次按下保存
    //   之前的那一份"，所以留的就是马上要被盖掉的那个样子。于是「还原上一次」有一个确定
    //   的含义：取回来的永远是"我上一次保存之前的内容"。
    //
    // ★ 留不下就不写（不是"警告一句然后照写"）：这一整件事的前提是"改坏了能回去"，而这一页
    //   自己没有撤销（那一行字是页面上写着的）。备份写不进去还照写，就是把用户手上唯一一份
    //   配置丢进一个没有回退按钮的操作里 —— 而它挡住的是"这一次保存"，用户还能把框里的
    //   内容自己拷出来、或者去把那个目录的权限修好再来。
    bool backedUp = false;
    if (!instanceId.isEmpty() && now.exists()) {
        QFile source(path);
        QByteArray previous;
        if (source.open(QIODevice::ReadOnly)) {
            previous = source.readAll();
        }
        if (source.error() != QFileDevice::NoError) {
            InlineMessage::warning(
                m_owner->m_ui->overlay(), m_owner->tr("保存之前没能读到原来那一份"),
                m_owner->tr("%1\n\n读它读不完（%2），所以这一次没有存 —— 直接写下去的话，它原来是什么"
                   "样就再也找不回来了。")
                    .arg(QDir::toNativeSeparators(path), source.errorString()));
            return;
        }
        source.close();

        const QString root = AppPaths::configBackupDir();
        const QStringList existing =
            QDir(root + QLatin1Char('/') + instanceId).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        // 文件夹名 = 现在这一刻（见 ConfigFileText::backupFolderName()）：同一秒里存两次会挂
        // 一个 `-2`，**不覆盖**上一次那一份（覆盖的话"上一步"就悄悄变成了"这一步"）。
        const QString folder =
            ConfigFileText::backupFolderName(QDateTime::currentDateTime(), existing);
        const QString target = ConfigFileText::backupPathFor(
            root, instanceId, m_owner->instanceFileDirs(instanceId).value(QStringLiteral("config")), path,
            folder);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QSaveFile backup(target);
        bool ok = backup.open(QIODevice::WriteOnly);
        if (ok && backup.write(previous) != previous.size()) {
            ok = false;
        }
        if (ok && !backup.commit()) {
            ok = false;
        }
        if (!ok) {
            backup.cancelWriting();
            InlineMessage::warning(
                m_owner->m_ui->overlay(), m_owner->tr("保存之前没能留下原来那一份"),
                m_owner->tr("%1\n\n没能往这里写一份备份（%2）：\n%3\n\n所以这一次没有存 —— 这一页改错了"
                   "没有撤销，留着能回去的那一份比存下去要紧。")
                    .arg(QDir::toNativeSeparators(path), backup.errorString(),
                         QDir::toNativeSeparators(target)));
            return;
        }
        backedUp = true;
    }

    // ★ `QSaveFile` 而不是 `QFile`：它先往同一个目录下写一个临时文件，最后**原子地**改名
    //   过去。直接写的话，写到一半失败（盘满 / 被杀毒拦下 / 断电）会把用户手上那份配置
    //   毁成半截 —— 而这一页没有"恢复默认"（模组的默认值是它下次启动时自己重写一份，
    //   那要等它启动，也不保证每个模组都这么干）。
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("保存失败"),
                               m_owner->tr("%1\n\n%2").arg(QDir::toNativeSeparators(path),
                                                  file.errorString()));
        return;
    }
    if (file.write(bytes) != bytes.size()) {
        const QString reason = file.errorString();
        file.cancelWriting();
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("保存失败"),
                               m_owner->tr("%1\n\n%2").arg(QDir::toNativeSeparators(path), reason));
        return;
    }
    if (!file.commit()) {
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("保存失败"),
                               m_owner->tr("%1\n\n%2").arg(QDir::toNativeSeparators(path),
                                                  file.errorString()));
        return;
    }
    // 写成了才告诉那一格 —— 它那一句"已保存（时:分:秒）"就是**盘上真的变了**的意思，
    // 写失败的时候不许出现（那一档由上面那几句话负责）。
    if (editor != nullptr) {
        editor->markSaved();
        // ★ 把刚写下去那一份的样子记回去：不记的话，**用户连着存两次**，第二次会被自己
        //   上一次写下去的那一份判成"盘上被人改过了"（mtime 变了）—— 于是每一次保存都要
        //   多问一句"要盖掉吗"。它要的是刚写下去那个 Stat。
        const QFileInfo after(path);
        ConfigFileText::Stamp fresh;
        if (after.exists()) {
            fresh.size = after.size();
            fresh.mtimeMs = after.lastModified().toMSecsSinceEpoch();
        }
        editor->setStamp(fresh);
        // 这一颗按钮画不画**只有一条判据**（和打开文件那一刻问的是同一个函数）：刚刚留的
        // 那一份就在那儿，所以这一次存完它一定找得到；而"这一次没留成"（文件本来就不在盘上
        // 了）也不会把更早那些备份一起抹掉 —— 那些备份照样能取回来。
        //
        // NOTE: 用 `backedUp` 这一位是不行的：它只说"这一趟留了没有"，不说"有没有别的可回"。
        editor->setBackupAvailable(!m_owner->latestConfigBackup(instanceId, path).isEmpty());
    }
    if (backedUp) {
        m_owner->pruneConfigBackups(instanceId, kConfigBackupGenerations);
    }
}

void InstanceEditorController::showConfigHistory(InstanceSettingsDialog *dialog, const QString &path) {
    if (dialog == nullptr || dialog->configFilePage() == nullptr) return;
    auto *editor = dialog->configFilePage();
    if (editor->path() != path || !editor->isEditable()) return;
    const QString instanceId = m_owner->instanceIdOfPath(path);
    const QString configDir = m_owner->instanceFileDirs(instanceId).value(QStringLiteral("config"));
    const QStringList backups = InstanceMaintenanceController::configBackups(instanceId, configDir, path);
    if (backups.isEmpty()) {
        editor->setBackupAvailable(false);
        InlineMessage::information(m_owner->m_ui->overlay(), m_owner->tr("暂无历史版本"),
                                  m_owner->tr("这个配置文件没有可用的备份。保存修改前会自动保留原内容。"));
        return;
    }
    QStringList rows;
    for (const QString &backup : backups) {
        const QFileInfo file(backup);
        const QString generation = QDir(AppPaths::configBackupDir() + QLatin1Char('/') + instanceId)
                                       .relativeFilePath(backup).section(QLatin1Char('/'), 0, 0);
        const QDateTime when = QDateTime::fromString(generation.left(15), QStringLiteral("yyyyMMdd-HHmmss"));
        rows.append(m_owner->tr("%1 · %2 字节\n%3")
            .arg(when.isValid() ? when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : generation)
            .arg(file.size()).arg(QDir::toNativeSeparators(backup)));
    }
    auto *host = m_owner->m_ui->overlay();
    if (host == nullptr || host->promptOpen()) return;
    QString selectedBackup;
    {
        InlineMessageBox box(MessageKind::Question, m_owner->tr("配置历史版本"),
            m_owner->tr("原文件：%1\n\n从新到旧排列。选择后仅载入编辑器，确认内容再保存；取消不会改动正文或磁盘。")
                .arg(QDir::toNativeSeparators(path)),
            {m_owner->tr("载入编辑器"), m_owner->tr("取消")}, 0, -1);
        auto *list = box.setList(rows, 0);
        for (int i = 0; i < rows.size(); ++i) list->item(i)->setToolTip(rows.at(i));
        host->execPrompt(&box, false);
        const int row = box.listRow();
        if (box.clickedIndex() == 0 && row >= 0 && row < backups.size())
            selectedBackup = backups.at(row);
    }
    // 先销毁选择提示，后续草稿确认是另一条独立提示。
    if (!selectedBackup.isEmpty()) loadConfigBackup(dialog, path, selectedBackup);
}

void InstanceEditorController::restoreConfigFile(InstanceSettingsDialog *dialog, const QString &path) {
    if (dialog == nullptr) {
        return;
    }
    ConfigFileEditorPage *editor = dialog->configFilePage();
    if (editor == nullptr) {
        return;
    }
    const QString instanceId = m_owner->instanceIdOfPath(path);
    const QString latest = m_owner->latestConfigBackup(instanceId, path);
    if (latest.isEmpty()) {
        // 走到这儿说明"刚刚还有、点下去就没了"（用户自己把那个目录删了）。**不许什么都不说**
        // —— 用户按了按钮，总得有个交代；顺手把那一颗收回去。
        editor->setBackupAvailable(false);
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("找不到原来那一份了"),
                               m_owner->tr("这个文件的备份不在了（%1）。").arg(QDir::toNativeSeparators(
                                   AppPaths::configBackupDir())));
        return;
    }
    loadConfigBackup(dialog, path, latest);
}

void InstanceEditorController::loadConfigBackup(InstanceSettingsDialog *dialog, const QString &path,
                                                const QString &latest) {
    if (dialog == nullptr) return;
    auto *editor = dialog->configFilePage();
    if (editor == nullptr || editor->path() != path || !editor->isEditable()) return;
    const QString instanceId = m_owner->instanceIdOfPath(path);
    const QString configDir = m_owner->instanceFileDirs(instanceId).value(QStringLiteral("config"));
    // 选择器打开期间备份可能被清理或替换，载入前重新核对归属与路径边界。
    if (!InstanceMaintenanceController::configBackups(instanceId, configDir, path).contains(latest)) {
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("备份已不可用"),
                               m_owner->tr("这份历史版本已被移除或不再位于有效的备份目录中，请重新选择。"));
        return;
    }
    QFile file(latest);
    if (!file.open(QIODevice::ReadOnly)) {
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("打不开备份里的那一份"),
                               m_owner->tr("%1\n\n%2").arg(QDir::toNativeSeparators(latest),
                                                  file.errorString()));
        return;
    }
    if (file.size() > ConfigFileText::editableByteLimit()) {
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("备份太大，未载入"),
                               m_owner->tr("这份备份超过文本编辑上限，请通过文件管理器查看：\n%1")
                                   .arg(QDir::toNativeSeparators(latest)));
        return;
    }
    const QByteArray raw = file.readAll();
    const QString readError = file.errorString();
    const bool readFailed = file.error() != QFileDevice::NoError;
    file.close();
    if (readFailed) {
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("读不完备份里的那一份"),
                               m_owner->tr("%1\n\n%2").arg(QDir::toNativeSeparators(latest), readError));
        return;
    }
    const ConfigFileText::Loaded loaded = ConfigFileText::decode(raw);
    if (!loaded.utf8Ok) {
        // 那一份不是 UTF-8（原件当时就是）。**不摆进去**：框里这一份是 UTF-8，摆进去会变成
        // 一堆 U+FFFD，再一存就把那份备份的内容毁成问号了 —— 而它是用户唯一的退路。
        InlineMessage::warning(
            m_owner->m_ui->overlay(), m_owner->tr("那一份不是 UTF-8"),
            m_owner->tr("%1\n\n它里面有几个字节不是 UTF-8，摆进这个编辑框（只认 UTF-8）会把那些字弄坏。"
               "要它的话，直接拿这个文件替换过去：\n%2")
                .arg(QDir::toNativeSeparators(latest), QDir::toNativeSeparators(path)));
        return;
    }
    // "那份是什么时候的"取**备份文件自己的时间**（= 我们写下这一份的那一刻 = 那一次保存的
    // 时候），不是文件夹名里那一串 —— 两者本来一样，但这一份是盘上真有的那个时间。
    const QString when =
        QFileInfo(latest).lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (editor->isDirty() && !confirmDiscardConfigEdits()) return;
    editor->restoreContent(loaded.text, when);
}

bool InstanceEditorController::backupWorldLevelData(const QString &worldDir, QString *problemOut) {
    return InstanceMaintenanceController::backupWorldLevelData(
        worldDir, kConfigBackupGenerations, problemOut);
}

QString InstanceEditorController::latestWorldBackup(const QString &worldDir) const {
    return InstanceMaintenanceController::latestWorldBackup(worldDir);
}

void InstanceEditorController::showWorldSettingsPage(InstanceSettingsDialog *dialog, const QString &worldDir) {
    if (dialog == nullptr) {
        return;
    }
    WorldSettingsPage *page = dialog->worldSettingsPage();
    if (page == nullptr) {
        return;
    }
    const QString worldName = QFileInfo(worldDir).fileName();

    // ★ 量在**读之前**（理由和 m_owner->openConfigFile() 里那段逐字相同：反过来的话，正好卡在
    //   "读完"和"量"之间的那一次改动会被判成"没动过"，于是保存时被静静盖掉）。
    const QFileInfo before(WorldFiles::levelDataPath(worldDir));
    ConfigFileText::Stamp stamp;
    if (before.exists()) {
        stamp.size = before.size();
        stamp.mtimeMs = before.lastModified().toMSecsSinceEpoch();
    }

    WorldFiles::LevelData data;
    QString why;
    if (!WorldFiles::readLevelData(worldDir, &data, &why)) {
        // ★ 这就是「坏档」唯一的判据**本身**：读得出来就是好的、读不出来就是坏的。判据不
        //   另立一份（"看 level.dat 在不在""看大小对不对"那种）—— 用户最怕的那件事正是
        //   "文件看着在、游戏打不开"，而那件事只有真解一遍才知道。
        //   读不出来 ⇒ **照"读不出来"那一档开页**（页面顶上写「这个世界可能坏了：<目录>」、
        //   正文整块藏起来、只留改名 / 打开文件夹 / 删除 / 备份一份），把**原因**原样摆给
        //   用户看（他拿去搜、拿去问人都要它）。2026-09-29 起这里**不再往前补一问**（原来
        //   会弹「打开（只读）/ 先备份一份 / 取消」）—— 用户原话「点击笔的时候不要弹备份
        //   提示了」；备份仍然随时可做，就是页里那颗「备份一份」。
        m_owner->m_worldDir = worldDir;
        m_owner->m_worldStampSize = 0;
        m_owner->m_worldStampMtimeMs = 0;
        dialog->openWorldSettingsUnreadable(worldName, why);
        page->setBackupAvailable(!m_owner->latestWorldBackup(worldDir).isEmpty());
        return;
    }

    m_owner->m_worldDir = worldDir;
    m_owner->m_worldStampSize = stamp.size;
    m_owner->m_worldStampMtimeMs = stamp.mtimeMs;
    dialog->openWorldSettings(worldViewOf(worldDir, data));
    // ★ `setBackupAvailable()` **必须在 openWorldSettings() 之后**：`setWorld()` 会把
    //   "留过备份没有"这一位清掉（上一条世界的答案对这一个世界不作数）。
    page->setBackupAvailable(!m_owner->latestWorldBackup(worldDir).isEmpty());
}

void InstanceEditorController::leaveWorldSettings(InstanceSettingsDialog *dialog) {
    if (dialog == nullptr) {
        return;
    }
    WorldSettingsPage *page = dialog->worldSettingsPage();
    if (page == nullptr || dialog->worldSettingsUnreadable()) {
        dialog->closeWorldSettings();
        return;
    }

    // ★ 改过的东西不许悄悄没掉 —— 这一页和"配置文件"那一页是同一条规矩。
    //
    // ★★ 2026-09-28：判据是**页自己那本台账**（`WorldSettingsPage::isDirty()`），
    //    不是"拿页面上的值跟盘上那一份比一遍"。
    //
    //    上一版这里写的是后一种，而且注释里宣称"为了不为此多解一次 gzip"—— 那句话是假的：
    //    代码自己就在上面解了一遍（`readLevelData()`，一次 PowerShell、小一秒），而且这么
    //    比出来的东西**不是**"改过没有"：盘上那一份只要被别的什么东西动过（游戏在跑、
    //    用户自己删了又拉回来、别的工具碰过），页面上一格没改也会被判成"改过的东西还没存"
    //    —— 用户对着一个自己一个字都没动过的页面被问"要丢掉吗"。
    //
    //    台账那一位是现成的，而且**覆盖层页眉那颗返回箭头走的就是它**（见
    //    `InstanceSettingsDialog::reject()`）：两条出口用两个判据的话，同一个页面按不同的
    //    返回键会得到两个不同的答案 —— 那正是"有时候问、有时候不问"这种没人查得出来的毛病。
    const bool dirty = page->isDirty();

    if (dirty) {
        const int picked = InlineMessage::ask(
            m_owner->m_ui->overlay(), MessageKind::Question, m_owner->tr("改过的东西还没存"),
            m_owner->tr("「%1」上改的那几格还没写下去。现在就返回的话，改的那些就没了。")
                .arg(QFileInfo(m_owner->m_worldDir).fileName()),
            {m_owner->tr("返回并丢掉"), m_owner->tr("留下来继续改")}, /*defaultIndex=*/1, /*destructiveIndex=*/0,
            false);
        if (picked != 0) {
            return;  // 关掉 / Esc / 「留下来继续改」：都当"不走"
        }
    }
    dialog->closeWorldSettings();
    // 记着"现在看的是哪个世界"的这一位**跟着一起清掉**（.hpp 里写着"空串 = 没开着"，
    // 不清的话那一句就是假的）。
    m_owner->m_worldDir.clear();
    m_owner->m_worldStampSize = 0;
    m_owner->m_worldStampMtimeMs = 0;
}

void InstanceEditorController::saveWorldSettings(InstanceSettingsDialog *dialog) {
    if (dialog == nullptr) {
        return;
    }
    WorldSettingsPage *page = dialog->worldSettingsPage();
    if (page == nullptr || m_owner->m_worldDir.isEmpty()) {
        return;
    }
    const QString worldDir = m_owner->m_worldDir;
    const QString worldName = QFileInfo(worldDir).fileName();
    // ★ 用户**真动过**的那几格（"和打开这一页时那一份逐字比"）。存下去的就是它 —— 不是
    //   "页面上现在长什么样"：那四格一律可动，盘上没有的项在屏幕上摆的是默认值，整份写下去
    //   等于把几十个默认值塞进用户的存档（见 WorldSettingsPage::Delta 的说明）。
    const WorldSettingsPage::Delta delta = page->deltaFromOpened();

    // ---------- 闸 ①：这个实例正开着游戏 ⇒ 不写 ----------
    //
    // ★ 判据和"改配置文件"那一趟**用的是同一个**（`m_owner->instanceIdOfPath()` +
    //   `runningVersionIds()`）：游戏手里那份存档也是它自己读进去的，退出时整份写回来。
    //   比这一条更"精确"的做法是去问 `saves/<世界>/session.lock` —— **试过，判不出来**：
    //   游戏（Java）是用 `FileChannel.tryLock()` 锁它的，在 Windows 上那是 `LockFileEx`
    //   的**按字节范围锁**，它**不挡**别人 `CreateFile` 打开这个文件。于是"打得开 ⇒
    //   没人占着"这条推理是错的（实测：游戏开着的时候那个文件照样打得开、写得进）。
    //   真要判得自己调 `LockFileEx`，而它只覆盖"**这个**世界正开着"这一档 —— 而我们
    //   自己启动的那一份，`runningVersionIds()` 本来就说得比它准（它连"哪个实例"都知道）。
    const QString runningId = m_owner->instanceIdOfPath(worldDir);
    if (!runningId.isEmpty() && m_owner->runningVersionIds().contains(runningId)) {
        InlineMessage::warning(
            m_owner->m_ui->overlay(), m_owner->tr("这个实例正开着游戏"),
            m_owner->tr("「%1」所在的实例还开着游戏。\n\n游戏里那份存档是它自己读进去的，它退出"
               "（或者你在游戏里存一次档）的时候会整份写回来，现在写下去的东西会被它盖掉。"
               "\n\n先把游戏关掉，再回来改。")
                .arg(worldName));
        return;
    }

    // ---------- 闸 ②：盘上那份在你打开之后被改过吗 ----------
    const QFileInfo onDisk(WorldFiles::levelDataPath(worldDir));
    if (!onDisk.exists()) {
        // 走到这儿说明"刚刚还有、现在没了"（用户自己删了 / 挪走了）。**不许什么都不说**。
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("这个存档不见了"),
                               m_owner->tr("%1\n\n之前还在，现在盘上没有它了 —— 什么都没写。")
                                   .arg(QDir::toNativeSeparators(worldDir)));
        return;
    }
    const qint64 nowSize = onDisk.size();
    const qint64 nowMtimeMs = onDisk.lastModified().toMSecsSinceEpoch();
    // `m_owner->m_worldStampMtimeMs == 0` 就是"打开那一刻压根没量到"（文件不在），比不了 —— 和
    // `ConfigFileText::Stamp::valid()` 是同一条判据（见 .hpp 里那两位的说明）。
    if (m_owner->m_worldStampMtimeMs != 0 && (nowSize != m_owner->m_worldStampSize || nowMtimeMs != m_owner->m_worldStampMtimeMs)) {
        // ★ 三颗出口、默认那颗是"什么都不做"（会毁掉别人改动的事，误按回车不该办了它）。
        const int picked = InlineMessage::ask(
            m_owner->m_ui->overlay(), MessageKind::Question, m_owner->tr("盘上那份被人改过了"),
            m_owner->tr("「%1」的 level.dat 在你打开这一页之后被改过（游戏在跑？别的工具？）。"
               "\n\n现在存下去，改它的那一次就没有了。\n\n"
               "（不管是哪一档，我们**只改你在页面上动过的那几格**，别的原样留着。）")
                .arg(worldName),
            {m_owner->tr("照样存下去"), m_owner->tr("读盘上那一份")}, /*defaultIndex=*/-1, /*destructiveIndex=*/0,
            false);
        if (picked == 1) {
            // 用户要的是盘上那一份：重读重摆（**不写盘**）。
            m_owner->showWorldSettingsPage(dialog, worldDir);
            return;
        }
        if (picked != 0) {
            return;  // 关掉 / Esc：盘上、页面上都原样
        }
    }

    // ---------- 闸 ③：以**现在盘上那一份**为底，只盖上改过的几格 ----------
    WorldFiles::LevelData data;
    QString why;
    if (!WorldFiles::readLevelData(worldDir, &data, &why)) {
        InlineMessage::warning(
            m_owner->m_ui->overlay(), m_owner->tr("读不出这个存档了"),
            m_owner->tr("「%1」的存档内容读不出来：\n\n%2\n\n所以这一次没有存 —— 我们不会在一份"
               "读不出来的存档上写东西。")
                .arg(worldName, why));
        return;
    }
    applyWorldViewTo(&data, delta);
    // `MissingField::Create`：那四格现在**一律可动**，而老存档可能压根没有 `allowCommands`
    // 那一格（1.8 才加的）—— 用户改了它就建出来，而不是让这一次保存以"改不了"收场
    // （用户刚做完一个动作、屏幕上也答应了，然后告诉他存不下去，那是最坏的一种界面）。
    // ★ 只会碰到 `delta` 点名的那几项：`applyWorldInfo()` 仍然是**逐项看 `hasXxx`** 的，
    //   而 `applyWorldViewTo()` 只给用户真改过的那几项置上 `hasXxx`。
    if (!WorldFiles::applyWorldInfo(&data.document, data.info, &why,
                                    WorldFiles::MissingField::Create)) {
        // ★ **没写盘**（`applyWorldInfo()` 改的是内存里那棵树）。理由和"配置文件"那条路
        //   一致：把用户改的一项写成"看着成了、其实没写"最坏。
        InlineMessage::warning(
            m_owner->m_ui->overlay(), m_owner->tr("这一项改不了"),
            m_owner->tr("「%1」\n\n%2\n\n所以这一次没有存，盘上那份原样没动。").arg(worldName, why));
        return;
    }
    // 规则那一路单独走（**可以新增**，见 `applyChangedGameRules()`）：盘上本来没有这一条、
    // 或者它的值被改过的那些，就是 `delta.rules`。空的时候这一发是空转（一条都不写）。
    if (!WorldFiles::applyChangedGameRules(&data.document, changedRules(delta), &why)) {
        InlineMessage::warning(
            m_owner->m_ui->overlay(), m_owner->tr("这一条规则改不了"),
            m_owner->tr("「%1」\n\n%2\n\n所以这一次没有存，盘上那份原样没动。").arg(worldName, why));
        return;
    }

    // ---------- 闸 ④：写之前把盘上那一份原样留一份 ----------
    //
    // ★ 留不下就不写（不是"警告一句然后照写"）：这一整套的前提是"改坏了能回去"，而世界
    //   设置这一页没有撤销（底下那行字是页面上写着的）。
    QString backupProblem;
    if (!m_owner->backupWorldLevelData(worldDir, &backupProblem)) {
        InlineMessage::warning(
            m_owner->m_ui->overlay(), m_owner->tr("保存之前没能留下原来那一份"),
            m_owner->tr("「%1」\n\n%2\n\n所以这一次没有存。").arg(worldName, backupProblem));
        return;
    }

    // ---------- 写 ----------
    if (!WorldFiles::writeLevelData(worldDir, data, &why)) {
        InlineMessage::warning(m_owner->m_ui->overlay(), m_owner->tr("保存失败"),
                               m_owner->tr("「%1」\n\n%2").arg(worldName, why));
        return;
    }

    page->markSaved();
    // ★ 把刚写下去那一份的样子记回去：不记的话，**用户连着存两次**，第二次会被自己上一次
    //   写下去的那一份判成"盘上被人改过了"（mtime 变了）—— 于是每一次保存都要多问一句。
    const QFileInfo after(WorldFiles::levelDataPath(worldDir));
    if (after.exists()) {
        m_owner->m_worldStampSize = after.size();
        m_owner->m_worldStampMtimeMs = after.lastModified().toMSecsSinceEpoch();
    }
    page->setBackupAvailable(!m_owner->latestWorldBackup(worldDir).isEmpty());
    // 盘上变了（也可能只是 mtime）⇒ 让世界列表那一页重列一遍：行上那行小字是"大小 ·
    // 最后游玩时间"，而**最后游玩时间是从 level.dat 里读的**，刚写下去的那一份就是新的。
    dialog->refreshFilePages();
}

void InstanceEditorController::leaveConfigEditor(InstanceSettingsDialog *dialog) {
    if (dialog == nullptr) {
        return;
    }
    ConfigFileEditorPage *editor = dialog->configFilePage();
    if (editor == nullptr) {
        return;
    }
    // ★ 改过的东西**不许悄悄没掉**（那一问的字见 m_owner->confirmDiscardConfigEdits()）。
    if (editor->isDirty()) {
        if (!m_owner->confirmDiscardConfigEdits()) {
            return;
        }
        // 退回读进来那一份 —— 不退回的话，isDirty() 还是 true，用户下一次点「返回」又会被
        // 问一遍（"我不是刚说了丢掉吗"）。
        editor->discardChanges();
    }
    dialog->closeConfigEditor();
}

bool InstanceEditorController::confirmDiscardConfigEdits() {
    return InlineMessage::question(
        m_owner->m_ui->overlay(), m_owner->tr("改过还没保存"),
        m_owner->tr("这个文件刚才改的地方还没写到盘上，离开就会丢掉这些改动。\n\n要丢掉吗？"),
        m_owner->tr("丢掉改动"), m_owner->tr("留在这儿"), false);
}

bool InstanceEditorController::confirmDiscardWorldEdits() {
    return InlineMessage::question(
        m_owner->m_ui->overlay(), m_owner->tr("改过还没保存"),
        m_owner->tr("这个世界的游戏规则 / 那几格刚才改的地方还没写下去，关掉就丢了。\n\n要丢掉吗？"),
        m_owner->tr("丢掉改动"), m_owner->tr("留在这儿"), false);
}

