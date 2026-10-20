/**
 * @file LaunchHistory.cpp
 * @brief 「上一次启动」那本账的实现（形状与取舍见头文件）。
 * @author 0x7fffff
 * @date 2026-10-13
 */

#include "LaunchHistory.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "util/AppPaths.hpp"

namespace LaunchHistory {

QDateTime lastStartedAt(const QList<Record> &records, const QString &instanceId) {
    QDateTime latest;
    if (instanceId.isEmpty()) return latest;
    for (const auto &record : records) {
        if (record.instanceId == instanceId && record.startedAt.isValid()
            && (!latest.isValid() || record.startedAt > latest)) {
            latest = record.startedAt;
        }
    }
    return latest;
}
namespace {

/// 盘上那个格式标记。**写死一个字符串**而不是"看有没有哪个字段"：以后改格式时，旧文件在
/// 这里被挡住，而不是半信半疑地读成一堆空记录（和 `BackupFiles::formatTag()` 同一套）。
QString formatTag() {
    return QStringLiteral("wtfml-launch-history/1");
}

/// 那一层的名字（`{"format": …, "records": [...]}`）。
const char *kRecordsKey = "records";

}  // namespace

QString historyPath() {
    return AppPaths::minecraftDir() + QStringLiteral("/wtfml.history");
}

int historyLimit() {
    return 20;
}

QString resultKey(Result result) {
    switch (result) {
    case Result::Running:
        return QStringLiteral("running");
    case Result::NormalExit:
        return QStringLiteral("normalExit");
    case Result::Crash:
        return QStringLiteral("crash");
    case Result::StartFailed:
        return QStringLiteral("startFailed");
    }
    return QStringLiteral("running");
}

QString targetKey(Target target) {
    switch (target) {
    case Target::None:
        return QStringLiteral("none");
    case Target::World:
        return QStringLiteral("world");
    case Target::Server:
        return QStringLiteral("server");
    }
    return QStringLiteral("none");
}

Result resultFromKey(const QString &key) {
    if (key == QLatin1String("normalExit")) {
        return Result::NormalExit;
    }
    if (key == QLatin1String("crash")) {
        return Result::Crash;
    }
    if (key == QLatin1String("startFailed")) {
        return Result::StartFailed;
    }
    // 认不出来的词（以后加了一档、或者文件被人手改过）落到 `Running`：单条记录认不出来
    // 不该让整本账作废，而 `Running` 是这里唯一一档"什么都没有断言"的值。
    return Result::Running;
}

Target targetFromKey(const QString &key) {
    if (key == QLatin1String("world")) {
        return Target::World;
    }
    if (key == QLatin1String("server")) {
        return Target::Server;
    }
    return Target::None;
}

QByteArray toJson(const QList<Record> &records) {
    QJsonArray array;
    for (const Record &record : records) {
        QJsonObject item;
        item.insert(QStringLiteral("instanceId"), record.instanceId);
        item.insert(QStringLiteral("instanceName"), record.instanceName);
        // 空的**不写**（省得读的人以为它是个开关，和 BackupFiles 那边同一条规矩）。
        if (!record.accountKey.isEmpty()) {
            item.insert(QStringLiteral("account"), record.accountKey);
        }
        item.insert(QStringLiteral("target"), targetKey(record.target));
        if (!record.targetId.isEmpty()) {
            item.insert(QStringLiteral("targetId"), record.targetId);
        }
        if (!record.targetDisplayName.isEmpty()) {
            item.insert(QStringLiteral("targetName"), record.targetDisplayName);
        }
        item.insert(QStringLiteral("startedAt"),
                    record.startedAt.toString(Qt::ISODateWithMs));
        // `durationSeconds < 0` = 还没算出来（还在跑）。**照样写**这一格（写 -1）：读的人
        // 一眼看得出"这一条没结束"，比"字段不在"少一层歧义。
        item.insert(QStringLiteral("durationSeconds"), record.durationSeconds);
        item.insert(QStringLiteral("result"), resultKey(record.result));
        array.append(item);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), formatTag());
    root.insert(QLatin1String(kRecordsKey), array);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QList<Record> fromJson(const QByteArray &bytes, bool *ok) {
    if (ok != nullptr) {
        *ok = false;
    }
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return QList<Record>();
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != formatTag()) {
        return QList<Record>();
    }

    QList<Record> records;
    const QJsonArray array = root.value(QLatin1String(kRecordsKey)).toArray();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject item = value.toObject();
        Record record;
        record.instanceId = item.value(QStringLiteral("instanceId")).toString();
        record.instanceName = item.value(QStringLiteral("instanceName")).toString();
        record.accountKey = item.value(QStringLiteral("account")).toString();
        record.target = targetFromKey(item.value(QStringLiteral("target")).toString());
        record.targetId = item.value(QStringLiteral("targetId")).toString();
        record.targetDisplayName = item.value(QStringLiteral("targetName")).toString();
        record.startedAt = QDateTime::fromString(
            item.value(QStringLiteral("startedAt")).toString(), Qt::ISODateWithMs);
        record.durationSeconds = qint64(item.value(QStringLiteral("durationSeconds")).toDouble(-1));
        record.result = resultFromKey(item.value(QStringLiteral("result")).toString());
        // 认不出是谁（没有实例 id）或者没有时间的那一条**当它不在**：卡片上那两样都得有，
        // 留着只会让下面每一处都得判一次"这条是不是空的"。
        if (record.isValid()) {
            records.append(record);
        }
    }
    if (ok != nullptr) {
        *ok = true;
    }
    return records;
}

QList<Record> load(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QList<Record>();
    }
    const QByteArray bytes = file.readAll();
    file.close();
    return fromJson(bytes, nullptr);
}

bool save(const QString &path, const QList<Record> &records) {
    const QFileInfo info(path);
    if (!info.absoluteDir().exists() && !QDir().mkpath(info.absolutePath())) {
        return false;
    }
    // QSaveFile：写侧走临时文件 + 原子改名（和任务账本同一套）。这份文件是"每启动一次就
    // 重写一遍"的小文件，写到一半断电最多丢掉这一次。
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray bytes = toJson(records);
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

QList<Record> prepend(const QList<Record> &records, const Record &record) {
    QList<Record> out;
    out.append(record);
    const int limit = historyLimit();
    for (const Record &existing : records) {
        if (out.size() >= limit) {
            break;
        }
        out.append(existing);
    }
    return out;
}

Record latestPlayable(const QList<Record> &records) {
    Record best;
    for (const Record &record : records) {
        if (!record.isValid() || record.result == Result::StartFailed) {
            continue;
        }
        if (!best.isValid() || record.startedAt > best.startedAt) {
            best = record;
        }
    }
    return best;
}

QString whenText(const QDateTime &at, const QDateTime &now) {
    if (!at.isValid() || !now.isValid()) {
        return QString();
    }
    const QString clock = at.toString(QStringLiteral("HH:mm"));
    // ★ 按**日期**比，不按"差了多少秒"：23:59 和 00:01 之间只差两分钟，可那两分钟正好跨了
    //   一天 —— 写成"2 分钟前"没错，但卡片上那句要的是"哪一天"，`昨天 00:01` 才是用户
    //   心里那一句。
    const int days = at.date().daysTo(now.date());
    if (days == 0) {
        return clock;
    }
    if (days == 1) {
        return QStringLiteral("昨天 %1").arg(clock);
    }
    if (days == 2) {
        return QStringLiteral("前天 %1").arg(clock);
    }
    // 将来的时间（钟被改过 / 文件被人动过）落到这儿会写出一个未来的日子 —— 那也是**真的**
    // 那一格里的内容，比编一句"刚刚"强。
    return at.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString whenText(const QDateTime &at) {
    return whenText(at, QDateTime::currentDateTime());
}

QString targetLine(const Record &record) {
    switch (record.target) {
    case Target::World:
        return QStringLiteral("单人世界：%1").arg(
            record.targetDisplayName.isEmpty() ? record.targetId : record.targetDisplayName);
    case Target::Server:
        return QStringLiteral("服务器：%1").arg(
            record.targetDisplayName.isEmpty() ? record.targetId : record.targetDisplayName);
    case Target::None:
        break;
    }
    return QString();
}

QString resultNote(const Record &record) {
    switch (record.result) {
    case Result::Crash:
        return QStringLiteral("（上次是异常退出）");
    case Result::Running:
        return QStringLiteral("（还在玩）");
    case Result::StartFailed:
        return QStringLiteral("（上次没起来）");
    case Result::NormalExit:
        break;
    }
    return QString();
}

QString durationText(qint64 seconds) {
    if (seconds < 0) {
        return QString();
    }
    // ★ 出的是**整句**（「玩了 1 小时 12 分」），不是光一个数量：卡片上那一行是"目标 ·
    //   时长（结局）"拼出来的，量词不同（分钟 / 小时）时"玩了"两个字必须一起挪 ——
    //   让调用方去拼会拼出「玩了 不到 1 分钟」这种话。
    if (seconds < 60) {
        return QStringLiteral("玩了不到 1 分钟");
    }
    const qint64 minutes = seconds / 60;
    if (minutes < 60) {
        return QStringLiteral("玩了 %1 分钟").arg(minutes);
    }
    const qint64 hours = minutes / 60;
    const qint64 rest = minutes % 60;
    if (rest == 0) {
        return QStringLiteral("玩了 %1 小时").arg(hours);
    }
    return QStringLiteral("玩了 %1 小时 %2 分").arg(hours).arg(rest);
}

}  // namespace LaunchHistory
