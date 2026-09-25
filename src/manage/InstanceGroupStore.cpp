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
/// 组在实例树里展开/收着（用户 2026-09-25 要的「默认展开/关闭」）。
/// **文件里没有这一条 = 展开**（老文件、手写的文件都算展开）—— 见 groups() 里那一行。
const QString kExpandedField = QStringLiteral("expanded");
/// 这一组是不是**置顶组**（用户 2026-09-25 要的「设为置顶组」）。
/// **文件里没有这一条 = 不是置顶组**（老文件、手写的文件都不认领这个身份）—— 只有一条
/// 例外在读取口上：一个都没有时退回那个叫「置顶」的默认组，见 pinnedGroupId()。
const QString kPinnedField = QStringLiteral("pinned");

/// 五个默认组的 id。**写死的**、不是 UUID —— 理由见 .hpp 里 defaultGroupId() 那段。
/// 前面那个 `default-` 前缀是给用户看的（他拿记事本打开这份文件时一眼能认出哪几个是
/// 默认组），顺带保证它和 newGroupId() 生成的 UUID 在形式上就不可能撞。
const QString kDefaultIdPinned = QStringLiteral("default-pinned");
const QString kDefaultIdModdable = QStringLiteral("default-moddable");
const QString kDefaultIdVanilla = QStringLiteral("default-vanilla");
const QString kDefaultIdSnapshot = QStringLiteral("default-snapshot");
const QString kDefaultIdOld = QStringLiteral("default-old");

}  // namespace

InstanceGroupStore::InstanceGroupStore(QObject *parent) : QObject(parent) {}

QString InstanceGroupStore::newGroupId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString InstanceGroupStore::defaultGroupId(DefaultKind kind) {
    switch (kind) {
    case DefaultKind::Pinned:
        return kDefaultIdPinned;
    case DefaultKind::Moddable:
        return kDefaultIdModdable;
    case DefaultKind::Vanilla:
        return kDefaultIdVanilla;
    case DefaultKind::Snapshot:
        return kDefaultIdSnapshot;
    case DefaultKind::Old:
        return kDefaultIdOld;
    }
    return QString();
}

QString InstanceGroupStore::defaultGroupName(DefaultKind kind) {
    switch (kind) {
    case DefaultKind::Pinned:
        return QStringLiteral("置顶");
    case DefaultKind::Moddable:
        return QStringLiteral("可安装mod");
    case DefaultKind::Vanilla:
        return QStringLiteral("原版");
    case DefaultKind::Snapshot:
        return QStringLiteral("快照版");
    case DefaultKind::Old:
        return QStringLiteral("远古版");
    }
    return QString();
}

QList<InstanceGroupStore::Group> InstanceGroupStore::defaultGroups() {
    // 顺序就是 DefaultKind 那个枚举的顺序（用户给的那五个词的原样，置顶在最前）。
    const DefaultKind order[] = {DefaultKind::Pinned, DefaultKind::Moddable, DefaultKind::Vanilla,
                                DefaultKind::Snapshot, DefaultKind::Old};
    QList<Group> groups;
    for (const DefaultKind kind : order) {
        Group group;
        group.id = defaultGroupId(kind);
        group.name = defaultGroupName(kind);
        // 「置顶」这一组**就是**那个置顶组（只有它是），其余四个都不是。它和「默认展开」
        // 的关系是：展开那半是缺省（true），和以前那行花括号写法一模一样。
        //
        // NOTE: 种默认（seedDefaultGroups）和恢复默认（restoreDefaultGroups）**共用这一个
        //       函数**，所以"两处的置顶组必须是同一个"是结构上成立的，不是两处各写一遍。
        group.pinned = kind == DefaultKind::Pinned;
        groups.append(group);
    }
    return groups;
}

QString InstanceGroupStore::ungroupedMarker() {
    return QStringLiteral("ungrouped");
}

bool InstanceGroupStore::seedDefaultGroups(QString *errorOut) {
    ensureLoaded();

    // 文件在 = 这一份 .minecraft 已经分过组了（哪怕里面一个组都没有 —— 用户把默认那五个
    // 全删光了），一个字节都不写。理由见 .hpp 里那段 NOTE。
    if (QFile::exists(AppPaths::instanceGroupFilePath())) {
        return true;
    }
    // 成员表照原样带着走（文件不存在时它本来就是空的，这句只是为了和 replaceGroups()
    // 一个写法 —— 将来"文件不存在但内存里有成员"这种组合冒出来时不会丢东西）。
    return writeThrough(defaultGroups(), m_root.value(kMembersField).toObject(), errorOut);
}

bool InstanceGroupStore::restoreDefaultGroups(QString *errorOut) {
    // 只是 replaceGroups() 的一个有名字的说法（两个后果写在 .hpp 里）。
    // **不走 seedDefaultGroups()**：那个在文件已经存在时一个字节都不写，而这里要的
    // 恰恰是"把现在这份清单换掉"—— 用户按的就是"恢复默认"。
    return replaceGroups(defaultGroups(), errorOut);
}

bool InstanceGroupStore::isAssigned(const QString &instanceKey) const {
    if (instanceKey.isEmpty()) {
        return false;
    }
    ensureLoaded();
    // 只看**有没有这一条**（值非空），不看那个组还在不在 —— 理由见 .hpp 里那段 NOTE。
    return !m_root.value(kMembersField).toObject().value(instanceKey).toString().isEmpty();
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
        // **缺省是展开**（toBool 的缺省值）：这一条是 2026-09-25 才有的，之前写的文件
        // 里一个都没有，而那时界面上组一律是展开的 —— 缺省值就把老文件读成了老样子。
        group.expanded = object.value(kExpandedField).toBool(true);
        // **缺省是不置顶**（toBool 的缺省值）：和上面那一格同一个写法，来路也一样 ——
        // 这一条是 2026-09-25 才有的，之前写的文件里一个都没有。**类型不对也当 false**
        // （toBool 对"字符串 true"这种返回的也是缺省值），理由和 expanded 那行一样：
        // 认不出来的东西就按"没这一位"处理，绝不猜。
        group.pinned = object.value(kPinnedField).toBool(false);
        result.append(group);
    }

    // ★ 归一化：**置顶组最多一个**。这份文件是可以被手改的（记事本里多写一行
    //   `"pinned": true` 就行），多出来那些必须在这儿清掉 —— 不归一化的话，"进哪一个"
    //   就变成了"文件里谁排在前面"决定，用户改一下组的顺序（或者我们自己某次写盘的顺序
    //   变了），那颗上箭头就把实例送进另一个组去了，而界面上什么都没变。
    //   留第一个，和上面"id 重复只留第一个"同一条规矩：读侧是唯一一道能收口的地方，
    //   写侧（setGroupPinned）设的时候也会清，但那只管我们自己写的那条路。
    bool seenPinned = false;
    for (Group &group : result) {
        if (!group.pinned) {
            continue;
        }
        if (seenPinned) {
            group.pinned = false;
            continue;
        }
        seenPinned = true;
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

QString InstanceGroupStore::pinnedGroupId() const {
    const QList<Group> known = groups();
    // 第一条出口：真有组认领了这个身份（groups() 已经保证最多一个）。
    for (const Group &group : known) {
        if (group.pinned) {
            return group.id;
        }
    }
    // 第二条出口（迁移路）：一个都没有 ⇒ 退回那个叫「置顶」的默认组 —— 只要它还在清单里。
    // 老用户的文件里没有 pinned 这一格（2026-09-25 才有的），而他手上那个「置顶」组从
    // 第一天起就摆在那儿；不退回的话，升级上来的人得自己去「分组设置」里勾一下，那颗
    // 上箭头才有着落 —— 而"置顶"这个名字本身就说明了它该是哪一个。
    //
    // NOTE: 这里**一个字节都不写盘**（完整理由见 .hpp 里那段 NOTE）：迁移是"读的时候
    //       算出来"的，不是"顺手把用户的文件改写一遍"。所以退回这件事是可重复的 ——
    //       用户下次打开还是同一个答案，文件也还是他自己写的那样。
    const QString fallback = defaultGroupId(DefaultKind::Pinned);
    const auto found =
        std::find_if(known.constBegin(), known.constEnd(),
                     [&fallback](const Group &group) { return group.id == fallback; });
    // 第三条出口：连「置顶」组都被删了（用户把五个默认组删光了，或者自己建的几个组都不
    // 打算当置顶组）⇒ 空串。调用方拿到空串就什么都不做 —— 那颗上箭头此刻没有去处。
    return found == known.constEnd() ? QString() : fallback;
}

bool InstanceGroupStore::replaceGroups(const QList<Group> &groups, QString *errorOut) {
    ensureLoaded();
    // 成员表整个留着，由 writeThrough() 按新的组清单清掉指向已删组的那些。
    return writeThrough(groups, m_root.value(kMembersField).toObject(), errorOut);
}

bool InstanceGroupStore::setGroupExpanded(const QString &groupId, bool expanded,
                                          QString *errorOut) {
    const auto fail = [errorOut](const QString &reason) {
        if (errorOut != nullptr) {
            *errorOut = reason;
        }
        return false;
    };

    // 空 id 是调用方的 bug（拿一个空的组 id 过来），不是用户操作失误。静默放过的话，
    // 表现是"点了那一下没反应"，而真正的原因（谁传了个空的）就查不出来了。
    if (groupId.isEmpty()) {
        return fail(tr("内部错误：没有指定要改哪个分组的展开状态。"));
    }

    ensureLoaded();
    if (!m_unreadableReason.isEmpty()) {
        return fail(m_unreadableReason);
    }

    QList<Group> list = groups();
    const auto found = std::find_if(list.begin(), list.end(),
                                    [&groupId](const Group &group) { return group.id == groupId; });
    if (found == list.end()) {
        return fail(tr("找不到那个分组，可能刚刚被删掉了。"));
    }
    if (found->expanded == expanded) {
        // 已经就是这个状态了（比如双击那两发里第二发又点了一次）：不写盘。
        return true;
    }
    found->expanded = expanded;
    // notify = false：这一格改的是"界面刚刚自己摆好的那个状态"，重灌整棵树只会把选择、
    // 滚动位置和别的组的展开状态一起抖一下（理由见 .hpp 里那段 NOTE）。
    return writeThrough(list, m_root.value(kMembersField).toObject(), errorOut, false);
}

bool InstanceGroupStore::setGroupPinned(const QString &groupId, bool pinned, QString *errorOut) {
    const auto fail = [errorOut](const QString &reason) {
        if (errorOut != nullptr) {
            *errorOut = reason;
        }
        return false;
    };

    // 空 id 是调用方的 bug（拿一个空的组 id 过来），不是用户操作失误 —— 和
    // setGroupExpanded() 那段同一个判据、同一句话的形状。
    if (groupId.isEmpty()) {
        return fail(tr("内部错误：没有指定要改哪个分组的置顶状态。"));
    }

    ensureLoaded();
    if (!m_unreadableReason.isEmpty()) {
        return fail(m_unreadableReason);
    }

    QList<Group> list = groups();
    const auto found = std::find_if(list.begin(), list.end(),
                                    [&groupId](const Group &group) { return group.id == groupId; });
    if (found == list.end()) {
        return fail(tr("找不到那个分组，可能刚刚被删掉了。"));
    }
    if (found->pinned == pinned) {
        // 已经就是这个状态了：不写盘（和 setGroupExpanded() 那条空操作同一条规矩）。
        return true;
    }
    // ★ 置顶组**只能有一个**：一句把"目标那一组"和"别的所有组"一起摆好 ——
    //   pinned == true 时只有目标那一组留下 true；pinned == false 时全清（含目标自己）。
    //   写出去的那一份**本来就干净**，不靠读侧那套归一化兜底：归一化只在读盘时清内存里
    //   那一份，写盘用的正是这里这份 list，靠它的话文件里会留着"两个都是 true"。
    for (Group &group : list) {
        group.pinned = pinned && group.id == groupId;
    }
    // notify = false：这一格读出来的时候用（那颗上箭头点下去才问"送去哪一组"），没有谁在
    // 重灌树那一下读它，为它重建一次整棵树只会把选择和滚动位置抖一下（完整理由见 .hpp）。
    return writeThrough(list, m_root.value(kMembersField).toObject(), errorOut, false);
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

    // 文件读不懂这条**先判**（2026-09-24 挪上来的）。它排在"找不到那个分组"后面时，
    // 真正的因由（文件坏了 / 是更新版本写的，这一版拒绝动它）会被挡在后面说不出口：
    // 那份读不懂的文件让 groups() 返回空表，于是用户一路都被告知"找不到那个分组，
    // 可能刚刚被删掉了"—— 而分组一个都没删，是这一版压根没读进来。
    // 写侧本来就会用同一个理由拒（见 writeThrough() 开头那道闸），所以这里提前说，
    // 报的就是同一句话。
    if (!m_unreadableReason.isEmpty()) {
        return fail(m_unreadableReason);
    }

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
    // 空串 = 用户明确选了「未分组」→ 写的是那个**保留值**，不是"把这一条删掉"。
    // 两者在界面上是同一格，在文件里必须分得开：删掉这一条的话，MainWindow 会认为
    // 用户从来没表过态，下一次刷新就按版本类型把它归类回去了（见 ungroupedMarker()）。
    members.insert(instanceKey, groupId.isEmpty() ? ungroupedMarker() : groupId);
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
                                      QString *errorOut, bool notify) {
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
        // 一律写出来（哪怕值是 true）：读侧的缺省值只负责"老文件读成展开"，
        // 写侧不靠缺省 —— 两处都省的话，"文件里没有这一条"就有两种来路说不清了。
        object.insert(kExpandedField, group.expanded);
        // 同上：置顶组这一位也**一律写出来**（哪怕值是 false）。读侧那个缺省值只负责把
        // 老文件读成"没有置顶组"，写侧不靠缺省 —— 和上面那一句同一条规矩。
        object.insert(kPinnedField, group.pinned);
        array.append(object);
    }

    QJsonObject members;
    for (auto it = rawMembers.constBegin(); it != rawMembers.constEnd(); ++it) {
        const QString key = it.key();
        // 值**先 trim**（2026-09-25 修）。读侧（groupIdOf）读出来的是 trim 过的值，而下面
        // 那句 keptIds.contains(groupId) 比的是**清洗过的**组 id —— 两边不一致时，手改过的
        // `"1.20.1": " default-mod "`（记事本里多打两个空格就会这样）在界面上是一条正常
        // 归属，而**任何一次写盘**（改别的实例的分组就会重写整个文件）都会把它当成"指向
        // 一个不存在的组"悄悄丢掉：用户下次打开就成了"这个实例怎么又回未分组了"，
        // 而且没有任何提示。上面那句"规则和读侧逐条对应"说的就是这件事。
        const QString groupId = it.value().toString().trimmed();
        // 指向已经不存在的组的归属**一起清掉** —— 那个归属没有意义了。这就是
        // "删组只删组本身 + 指向它的成员条目，实例一个不动"那半句的实现。
        //
        // NOTE: ungroupedMarker() 那个保留值不在 keptIds 里，但它是一条**正常**的归属
        //       （用户明确选了未分组），必须原样留着 —— 漏掉这一句的话，用户一按「确定」
        //       它就被清掉，然后按版本类型归类回去。
        if (key.isEmpty() || groupId.isEmpty()) {
            continue;
        }
        if (groupId != ungroupedMarker() && !keptIds.contains(groupId)) {
            continue;
        }
        members.insert(key, groupId);
    }

    // 以原来那个对象为底，只盖我们认识的三个键 —— 更新版本写的别的字段原样留着。
    //
    // NOTE: 盖的是**副本 candidate**，不是 m_root（2026-09-24 改的）。这三句原来排在
    //       mkpath / open / commit 之前，而下面三条失败路径都不回滚 —— 于是"写盘失败"
    //       这件事会先在内存里生效（groups() / groupIdOf() 读的正是 m_root），接着**另一次
    //       成功的写盘**会把这份幽灵状态一起提交：「告知失败的操作在稍后静默生效」。
    //       内存状态只在确实落盘成功之后才认账。
    QJsonObject candidate = m_root;
    candidate.insert(kVersionField, kVersion);
    candidate.insert(kGroupsField, array);
    candidate.insert(kMembersField, members);

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
    file.write(QJsonDocument(candidate).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return fail(tr("写入 %1 失败。").arg(QDir::toNativeSeparators(path)));
    }

    // 到这里才认账：内存里那份 = 刚落到盘上的那份（见上面 candidate 那段 NOTE）。
    m_root = candidate;
    if (notify) {
        emit groupsChanged();
    }
    return true;
}
