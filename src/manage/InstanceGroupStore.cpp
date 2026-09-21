/**
 * @file InstanceGroupStore.cpp
 * @brief Implements instance grouping (read/write <数据目录>/wtfml.folders).
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 文件格式、键名与容错规则都写在 .hpp 里，这里只实现。
 */

#include "InstanceGroupStore.hpp"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

#include "../util/AppPaths.hpp"

namespace {

/// 文件里认的格式版本。将来格式真变了（比如组要加颜色、要能嵌套）就 +1，
/// 而**不认识的版本一律当读不懂**（见 ensureLoaded()）。
constexpr int kVersion = 1;

const QString kVersionField = QStringLiteral("version");
const QString kGroupsField = QStringLiteral("groups");
const QString kMembersField = QStringLiteral("members");
const QString kIdField = QStringLiteral("id");
const QString kNameField = QStringLiteral("name");

}  // namespace

InstanceGroupStore::InstanceGroupStore(QObject *parent) : QObject(parent) {}

QString InstanceGroupStore::newGroupId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void InstanceGroupStore::ensureLoaded() const {
    const QString dir = AppPaths::minecraftDir();
    if (m_loadedFor == dir) {
        return;
    }
    // 先认下"这一份我已经看过了"，后面无论哪条失败路径返回，都不会每调一次访问器
    // 就重读一遍盘（读不懂的文件尤其要防这个 —— 那是每次访问都失败一次的写法）。
    m_loadedFor = dir;
    m_root = QJsonObject();
    m_unreadableReason.clear();

    const QString path = AppPaths::instanceGroupFilePath();
    QFile file(path);
    if (!file.exists()) {
        // 常态：还没分过组。不是异常，也别打日志。
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Instance group file exists but cannot be opened:" << path;
        m_unreadableReason = tr("%1 打不开，这次的改动没有保存。").arg(path);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Failed to parse instance group file" << path << ":"
                   << parseError.errorString();
        m_unreadableReason =
            tr("%1 的内容不是合法的 JSON，为避免覆盖掉你写的东西，这次没有改动它。").arg(path);
        return;
    }

    const QJsonObject root = doc.object();
    const int version = root.value(kVersionField).toInt(0);
    if (version != kVersion) {
        // 读的时候当空（不猜旧/新格式的差别），但**写的时候拒绝**：写回去等于把一个
        // 更新版本写的文件降级成 v1，丢的是我们看不懂的那些字段。今天只可能是
        // 用户手改或者文件坏了 —— 两种都不该被我们悄悄抹掉。
        qWarning() << "Instance group file has an unknown version" << version << ":" << path;
        m_unreadableReason =
            tr("%1 是更新版本的启动器写的（格式版本 %2），这一版读不懂，没有改动它。")
                .arg(path)
                .arg(version);
        return;
    }

    m_root = root;
}

QList<InstanceGroupStore::Group> InstanceGroupStore::groups() const {
    ensureLoaded();

    QList<Group> result;
    const QJsonArray array = m_root.value(kGroupsField).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        const QString id = object.value(kIdField).toString().trimmed();
        // 没有 id 的条目光看名字认不出是哪一个（改名、重名都认不出来），丢掉。
        if (id.isEmpty()) {
            continue;
        }
        // id 重复只留第一个。两个组共用一个 id 的话，成员表里那个归属到底指谁
        // 是说不清的，而"说不清"在界面上表现为"点哪个组都跳到第一个组去"。
        const auto duplicate = std::find_if(result.constBegin(), result.constEnd(),
                                            [&id](const Group &group) { return group.id == id; });
        if (duplicate != result.constEnd()) {
            continue;
        }
        Group group;
        group.id = id;
        // 名字是给用户看的，为空时退回 id：界面上至少还认得出来是哪一个组，
        // 而空名字在树里就是一行空白。
        const QString name = object.value(kNameField).toString().trimmed();
        group.name = name.isEmpty() ? id : name;
        result.append(group);
    }
    return result;
}

QString InstanceGroupStore::groupIdOf(const QString &instanceKey) const {
    if (instanceKey.isEmpty()) {
        return QString();
    }
    ensureLoaded();

    const QString groupId =
        m_root.value(kMembersField).toObject().value(instanceKey).toString().trimmed();
    if (groupId.isEmpty()) {
        return QString();
    }
    const QList<Group> known = groups();
    const auto found = std::find_if(known.constBegin(), known.constEnd(),
                                    [&groupId](const Group &group) { return group.id == groupId; });
    return found == known.constEnd() ? QString() : groupId;
}

bool InstanceGroupStore::replaceGroups(const QList<Group> &groups, QString *errorOut) {
    ensureLoaded();
    // 成员表整个留着，由 writeThrough() 按新的组清单清掉指向已删组的那些。
    return writeThrough(groups, m_root.value(kMembersField).toObject(), errorOut);
}

bool InstanceGroupStore::assign(const QString &instanceKey, const QString &groupId,
                                QString *errorOut) {
    const auto fail = [errorOut](const QString &reason) {
        if (errorOut != nullptr) {
            *errorOut = reason;
        }
        return false;
    };

    // 空键是调用方的 bug（比如拿着一个"当前没选中"的空 id 过来），不是用户操作失误。
    // 静默写进去的后果是文件里多一条谁都认不出来的归属，所以直接拒。
    if (instanceKey.isEmpty()) {
        return fail(tr("内部错误：没有指定要给哪个实例分组。"));
    }

    ensureLoaded();

    if (!groupId.isEmpty()) {
        const QList<Group> known = groups();
        const auto found =
            std::find_if(known.constBegin(), known.constEnd(),
                         [&groupId](const Group &group) { return group.id == groupId; });
        if (found == known.constEnd()) {
            return fail(tr("找不到那个分组，可能刚刚被删掉了。"));
        }
    }

    QJsonObject members = m_root.value(kMembersField).toObject();
    if (groupId.isEmpty()) {
        members.remove(instanceKey);
    } else {
        members.insert(instanceKey, groupId);
    }
    return writeThrough(groups(), members, errorOut);
}

bool InstanceGroupStore::rekey(const QString &oldKey, const QString &newKey, QString *errorOut) {
    // 键没变（改名的只是大小写、或者压根没生成过新 id）就没什么可搬的。
    // 这里**不写盘**也是刻意的：一次改名不该平白多出一个 wtfml.folders。
    if (oldKey.isEmpty() || newKey.isEmpty() || oldKey == newKey) {
        return true;
    }
    ensureLoaded();

    QJsonObject members = m_root.value(kMembersField).toObject();
    if (!members.contains(oldKey)) {
        // 本来就没分组 —— 不写盘，理由同上。
        return true;
    }
    const QString groupId = members.value(oldKey).toString();
    members.remove(oldKey);
    members.insert(newKey, groupId);
    return writeThrough(groups(), members, errorOut);
}

bool InstanceGroupStore::writeThrough(const QList<Group> &groups, const QJsonObject &rawMembers,
                                      QString *errorOut) {
    const auto fail = [errorOut](const QString &reason) {
        if (errorOut != nullptr) {
            *errorOut = reason;
        }
        return false;
    };

    if (!m_unreadableReason.isEmpty()) {
        return fail(m_unreadableReason);
    }

    // 进参按**不可信数据**清洗，规则和读侧逐条对应。两边一致的意思是：一个值写进去
    // 再读回来必然逐字段不变（读侧那三条规范化在这里已经做完了）。
    QJsonArray array;
    QStringList keptIds;
    for (const Group &group : groups) {
        const QString id = group.id.trimmed();
        if (id.isEmpty() || keptIds.contains(id)) {
            continue;
        }
        keptIds.append(id);
        QJsonObject object;
        object.insert(kIdField, id);
        object.insert(kNameField, group.name.trimmed());
        array.append(object);
    }

    QJsonObject members;
    for (auto it = rawMembers.constBegin(); it != rawMembers.constEnd(); ++it) {
        const QString key = it.key();
        const QString groupId = it.value().toString();
        // 指向已经不存在的组的归属**一起清掉** —— 那个归属没有意义了。这就是
        // "删组只删组本身 + 指向它的成员条目，实例一个不动"那半句的实现。
        if (key.isEmpty() || groupId.isEmpty() || !keptIds.contains(groupId)) {
            continue;
        }
        members.insert(key, groupId);
    }

    // 以原来那个对象为底，只盖我们认识的三个键 —— 更新版本写的别的字段原样留着。
    m_root.insert(kVersionField, kVersion);
    m_root.insert(kGroupsField, array);
    m_root.insert(kMembersField, members);

    const QString path = AppPaths::instanceGroupFilePath();
    if (!QDir().mkpath(AppPaths::minecraftDir())) {
        return fail(tr("无法创建目录 %1。").arg(QDir::toNativeSeparators(AppPaths::minecraftDir())));
    }

    // QSaveFile：先写同目录的临时文件、commit() 时才改名覆盖。断电或崩溃只会留下
    // "旧内容完好"，绝不会留下半截 JSON —— 这个文件坏掉的后果是**全部分组一起失效**，
    // 比"这次没写成"糟得多。
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return fail(tr("无法写入 %1。").arg(QDir::toNativeSeparators(path)));
    }
    // 缩进输出：这是用户可能拿记事本打开看的文件（里面是他自己起的分组名）。
    file.write(QJsonDocument(m_root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return fail(tr("写入 %1 失败。").arg(QDir::toNativeSeparators(path)));
    }

    emit groupsChanged();
    return true;
}
