/**
 * @file ServerPropertiesForm.cpp
 * @brief 常用字段表 + 值域 + 合回文件（形状与取舍见头文件）。
 */

#include "server/ServerPropertiesForm.hpp"

#include <QHash>
#include <QSet>

namespace ServerPropertiesForm {

namespace {

/// 表里那一格的显示名（`Choice` 那档拿它拼"只能是这几个之一"那句话）。
QString choiceNames(const Field &field) {
    QStringList names;
    names.reserve(field.choices.size());
    for (const Choice &choice : field.choices) {
        names.append(choice.name);
    }
    return names.join(QStringLiteral(" / "));
}

/// 一行小字的候选表（`Choice` 那几档）。
QList<Choice> choicesOf(const QList<QPair<QString, QString>> &pairs) {
    QList<Choice> out;
    out.reserve(pairs.size());
    for (const QPair<QString, QString> &pair : pairs) {
        out.append(Choice{pair.first, pair.second});
    }
    return out;
}

/// Windows 上不能当**目录名 / 文件名**用的那几个字符（和 `ServerRegistry::sanitizeId()` 那张
/// 表同源；这一层是"收不收"，那一层是"洗一洗"，所以两处都写着同一个字符集是有意的）。
bool hasIllegalPathChar(const QString &value) {
    for (const QChar ch : value) {
        if (ch == QLatin1Char('/') || ch == QLatin1Char('\\') || ch == QLatin1Char(':')
            || ch == QLatin1Char('*') || ch == QLatin1Char('?') || ch == QLatin1Char('"')
            || ch == QLatin1Char('<') || ch == QLatin1Char('>') || ch == QLatin1Char('|')
            || ch < QChar(0x20)) {
            return true;
        }
    }
    return false;
}

/// 这一串能不能当一个整数（**严格**：`toInt()` 会把 `"1e3"`、`"10.0"` 静默读成 0，那种
/// "帮你改一改"在这儿是不能接受的，见 `accepts()` 的说明）。
bool strictInt(const QString &text, qlonglong *out) {
    QString body = text;
    bool negative = false;
    if (body.startsWith(QLatin1Char('-')) || body.startsWith(QLatin1Char('+'))) {
        negative = body.startsWith(QLatin1Char('-'));
        body = body.mid(1);
    }
    if (body.isEmpty()) {
        return false;
    }
    for (const QChar ch : body) {
        if (ch < QLatin1Char('0') || ch > QLatin1Char('9')) {
            return false;
        }
    }
    bool ok = false;
    const qlonglong value = body.toLongLong(&ok);
    if (!ok) {
        return false;
    }
    if (out != nullptr) {
        *out = negative ? -value : value;
    }
    return true;
}

/// 开关那几档认得的写法（用户在文件里手写 `yes` / `1` 都是常见的事 —— Minecraft 自己写
/// 出来的一律是 `true`/`false`，但这一格**读**的是用户可能手改过的文件）。
bool switchValue(const QString &raw, bool *out) {
    const QString lowered = raw.trimmed().toLower();
    if (lowered == QStringLiteral("true") || lowered == QStringLiteral("1")
        || lowered == QStringLiteral("yes") || lowered == QStringLiteral("on")) {
        if (out != nullptr) {
            *out = true;
        }
        return true;
    }
    if (lowered == QStringLiteral("false") || lowered == QStringLiteral("0")
        || lowered == QStringLiteral("no") || lowered == QStringLiteral("off")) {
        if (out != nullptr) {
            *out = false;
        }
        return true;
    }
    return false;
}

}  // namespace

const QList<Field> &commonFields() {
    // 顺序就是界面上从上到下的顺序：**先日常**（简介 / 端口 / 人数 / 难度…）、再**规则**
    // （正版验证 / 白名单 / PVP…）、最后**性能**（视距那几项）。前两组是用户建完服就想去
    // 调的，最后那几项是"卡了才来调"的。
    static const QList<Field> fields = {
        // ---------- 日常 ----------
        // `preview = true`：这一格下面挂一条"照游戏里画出来"的样子（`§` 码、两行）。
        Field{QStringLiteral("motd"), QStringLiteral("服务器简介"),
              QStringLiteral("服务器列表里显示的那一行字"),
              Kind::Text, QStringLiteral("A Minecraft Server"), 0, 0, {}, false, true},
        Field{QStringLiteral("server-port"), QStringLiteral("端口"),
              QStringLiteral("别人连进来用的端口；路由器上要把它转发出去"),
              Kind::Number, QStringLiteral("25565"), 1, 65535, {}, false},
        Field{QStringLiteral("max-players"), QStringLiteral("最多在线人数"),
              QStringLiteral("同时最多几个人在线，第 21 个人进来会被挡回去"),
              Kind::Number, QStringLiteral("20"), 1, 1000, {}, false},
        Field{QStringLiteral("difficulty"), QStringLiteral("难度"),
              QStringLiteral("怪物的伤害、饥饿掉得多快，就看这一档"),
              Kind::Choice, QStringLiteral("easy"), 0, 0,
              choicesOf({{QStringLiteral("peaceful"), QStringLiteral("和平")},
                         {QStringLiteral("easy"), QStringLiteral("简单")},
                         {QStringLiteral("normal"), QStringLiteral("普通")},
                         {QStringLiteral("hard"), QStringLiteral("困难")}}),
              false},
        Field{QStringLiteral("gamemode"), QStringLiteral("默认游戏模式"),
              QStringLiteral("新玩家第一次进来时是哪一档（之后改自己的就行）"),
              Kind::Choice, QStringLiteral("survival"), 0, 0,
              choicesOf({{QStringLiteral("survival"), QStringLiteral("生存")},
                         {QStringLiteral("creative"), QStringLiteral("创造")},
                         {QStringLiteral("adventure"), QStringLiteral("冒险")},
                         {QStringLiteral("spectator"), QStringLiteral("旁观")}}),
              false},
        Field{QStringLiteral("hardcore"), QStringLiteral("极限模式"),
              QStringLiteral("开着的话死一次就进不去这个世界了"),
              Kind::Switch, QStringLiteral("false"), 0, 0, {}, false},
        Field{QStringLiteral("level-name"), QStringLiteral("世界目录名"),
              QStringLiteral("世界存在这个文件夹里（改它就等于换一个世界）"),
              Kind::Text, QStringLiteral("world"), 0, 0, {}, true},
        Field{QStringLiteral("level-seed"), QStringLiteral("地图种子"),
              QStringLiteral("世界第一次生成时用；已经生成好的世界改它没有用"),
              Kind::Text, QString(), 0, 0, {}, false},
        // ---------- 资源包（§9.7：选本地 zip、算 SHA-1；地址得用户自己托管）----------
        Field{QStringLiteral("resource-pack"), QStringLiteral("资源包地址"),
              QStringLiteral("别人连得到的一个 http(s) 链接；本机文件路径别人打不开"),
              Kind::Text, QString(), 0, 0, {}, false},
        // `zipPick = true`：这一格旁边摆一颗「选择本地 zip…」，按 SHA-1 填进来。
        Field{QStringLiteral("resource-pack-sha1"), QStringLiteral("资源包 SHA-1"),
              QStringLiteral("这个 zip 的 SHA-1；可以用右边那颗按钮从本地 zip 算出来"),
              Kind::Text, QString(), 0, 0, {}, false, false, true},
        // ---------- 规则 ----------
        Field{QStringLiteral("online-mode"), QStringLiteral("正版验证"),
              QStringLiteral("开着只让正版账号进来；关掉之后谁都能冒名顶替别人的名字"),
              Kind::Switch, QStringLiteral("true"), 0, 0, {}, false},
        Field{QStringLiteral("white-list"), QStringLiteral("白名单"),
              QStringLiteral("只有名单上的人能进来（名单要在控制台里用 whitelist add 加）"),
              Kind::Switch, QStringLiteral("false"), 0, 0, {}, false},
        Field{QStringLiteral("pvp"), QStringLiteral("玩家互相伤害"),
              QStringLiteral("关掉之后玩家之间打不动（怪物还是照打）"),
              Kind::Switch, QStringLiteral("true"), 0, 0, {}, false},
        Field{QStringLiteral("allow-flight"), QStringLiteral("允许飞行"),
              QStringLiteral("关着的时候，客户端里被认出在飞会被踢出去"),
              Kind::Switch, QStringLiteral("false"), 0, 0, {}, false},
        Field{QStringLiteral("allow-nether"), QStringLiteral("允许去下界"),
              QStringLiteral("关掉之后下界传送门不生效"),
              Kind::Switch, QStringLiteral("true"), 0, 0, {}, false},
        Field{QStringLiteral("spawn-monsters"), QStringLiteral("刷怪"),
              QStringLiteral("关了世界里就不生成怪物（和平难度下本来也不刷）"),
              Kind::Switch, QStringLiteral("true"), 0, 0, {}, false},
        Field{QStringLiteral("enable-command-block"), QStringLiteral("允许命令方块"),
              QStringLiteral("命令方块和命令方块矿车能不能用"),
              Kind::Switch, QStringLiteral("false"), 0, 0, {}, false},
        Field{QStringLiteral("spawn-protection"), QStringLiteral("出生点保护半径"),
              QStringLiteral("出生点周围这么多格，非管理员破坏不了（0 = 关掉）"),
              Kind::Number, QStringLiteral("16"), 0, 1000, {}, false},
        // ---------- 性能 ----------
        Field{QStringLiteral("view-distance"), QStringLiteral("视距"),
              QStringLiteral("一个人能看多远；调大很吃内存和带宽（默认 10 就够十个玩家）"),
              Kind::Number, QStringLiteral("10"), 3, 32, {}, false},
        Field{QStringLiteral("simulation-distance"), QStringLiteral("模拟距离"),
              QStringLiteral("多远之内的生物和方块会真的动起来"),
              Kind::Number, QStringLiteral("10"), 3, 32, {}, false},
        Field{QStringLiteral("player-idle-timeout"), QStringLiteral("挂机踢出（分钟）"),
              QStringLiteral("多久不动就把人踢下线；0 = 永远不踢"),
              Kind::Number, QStringLiteral("0"), 0, 1440, {}, false},
    };
    return fields;
}

const Field *fieldFor(const QString &key) {
    for (const Field &field : commonFields()) {
        if (field.key.compare(key, Qt::CaseInsensitive) == 0) {
            return &field;
        }
    }
    return nullptr;
}

QStringList knownKeys() {
    QStringList keys;
    keys.reserve(commonFields().size());
    for (const Field &field : commonFields()) {
        keys.append(field.key);
    }
    return keys;
}

QString valueOf(const ServerModel::Properties &properties, const Field &field) {
    // `value()` 自己带缺省：这一项文件里没有 ⇒ 回 `fallback`（那一格为什么不能用空串，
    // 见 `Field::fallback` 那段）。
    return properties.value(field.key, field.fallback).trimmed();
}

bool isInFile(const ServerModel::Properties &properties, const Field &field) {
    return properties.contains(field.key);
}

bool accepts(const Field &field, const QString &raw, QString *normalized, QString *problem) {
    if (normalized != nullptr) {
        normalized->clear();
    }
    if (problem != nullptr) {
        problem->clear();
    }
    const auto fail = [problem](const QString &text) {
        if (problem != nullptr) {
            *problem = text;
        }
        return false;
    };

    const QString trimmed = raw.trimmed();
    // 换行会把这一项切成两行 —— 落盘之后那一份 `server.properties` 就再也解不回来了
    // （下面那半截会变成一个**没有值的键**）。四种控件一律挡在这儿。
    if (trimmed.contains(QLatin1Char('\n')) || trimmed.contains(QLatin1Char('\r'))) {
        return fail(QObject::tr("这一格里不能换行。"));
    }

    switch (field.kind) {
    case Kind::Text:
        if (field.dirName) {
            if (trimmed.isEmpty()) {
                return fail(QObject::tr("世界目录名不能是空的 —— 世界总得存在某个文件夹里。"));
            }
            if (trimmed == QStringLiteral(".") || trimmed == QStringLiteral("..")) {
                return fail(QObject::tr("「%1」不能当目录名。").arg(trimmed));
            }
            if (hasIllegalPathChar(trimmed)) {
                return fail(QObject::tr("世界目录名里不能有 / \\ : * ? \" < > | 这类字符"
                                        "（它要当文件夹名用）。"));
            }
        }
        if (normalized != nullptr) {
            *normalized = trimmed;
        }
        return true;

    case Kind::Number: {
        qlonglong value = 0;
        if (!strictInt(trimmed, &value)) {
            return fail(QObject::tr("这里要填一个整数（%1 ~ %2）。")
                            .arg(field.min)
                            .arg(field.max));
        }
        if (value < field.min || value > field.max) {
            return fail(QObject::tr("超出范围了：这一项只收 %1 ~ %2（游戏自己的界）。")
                            .arg(field.min)
                            .arg(field.max));
        }
        if (normalized != nullptr) {
            *normalized = QString::number(value);
        }
        return true;
    }

    case Kind::Switch: {
        bool on = false;
        if (!switchValue(trimmed, &on)) {
            return fail(QObject::tr("这一项只有开和关两档。"));
        }
        if (normalized != nullptr) {
            *normalized = on ? QStringLiteral("true") : QStringLiteral("false");
        }
        return true;
    }

    case Kind::Choice: {
        for (const Choice &choice : field.choices) {
            if (choice.token.compare(trimmed, Qt::CaseInsensitive) == 0) {
                if (normalized != nullptr) {
                    // 回**表里拼的那个词**（不是用户敲的那一串）：这样 `NORMAL` 这种大小写
                    // 写歪的输入落盘之后也是游戏认得的那一个。
                    *normalized = choice.token;
                }
                return true;
            }
        }
        return fail(QObject::tr("只能是这几个之一：%1。").arg(choiceNames(field)));
    }
    }
    return fail(QObject::tr("这一项不知道该怎么收。"));
}

Values valuesOf(const ServerModel::Properties &properties, const QList<Field> &fields) {
    Values values;
    values.reserve(fields.size());
    for (const Field &field : fields) {
        values.append({field.key, valueOf(properties, field)});
    }
    return values;
}

int applyEdits(ServerModel::Properties &properties, const Values &edits) {
    int changed = 0;
    for (const QPair<QString, QString> &edit : edits) {
        const Field *field = fieldFor(edit.first);
        if (field == nullptr) {
            continue;  // 表里没有的键不写（这一层只认这十几项）
        }
        QString normalized;
        QString problem;
        if (!accepts(*field, edit.second, &normalized, &problem)) {
            // 值不合法就**跳过**、绝不写进去：这一条是给"调用方忘了先过 `accepts()`"准备的
            // 兜底（界面上要在保存之前就把话说出来，而不是等到写盘那一步）。
            continue;
        }
        if (properties.setValue(field->key, normalized)) {
            ++changed;
        }
    }
    return changed;
}

QList<ValueProblem> problemsIn(const ServerModel::Properties &properties) {
    QList<ValueProblem> problems;
    for (const Field &field : commonFields()) {
        if (!isInFile(properties, field)) {
            continue;  // 缺省值按定义就是合法的（见那个函数头那段 NOTE）
        }
        const QString raw = properties.value(field.key);
        QString normalized;
        QString problem;
        if (!accepts(field, raw, &normalized, &problem)) {
            problems.append(ValueProblem{field.key, field.label, raw, problem});
        }
    }
    return problems;
}

QStringList duplicateKeys(const ServerModel::Properties &properties) {
    // 键 → 见到过的值（小写键做身份：Java 的 Properties 键是大小写敏感的，但用户手写出来的
    // `Server-Port` 十有八九是想写 `server-port` —— 那种情形报出来更有用）。
    QStringList order;
    QHash<QString, QStringList> seen;
    for (const ServerModel::Properties::Line &line : properties.lines) {
        if (line.key.isEmpty()) {
            continue;
        }
        const QString lowered = line.key.toLower();
        if (!seen.contains(lowered)) {
            order.append(lowered);
        }
        seen[lowered].append(line.value);
    }
    QStringList duplicates;
    for (const QString &key : order) {
        const QStringList values = seen.value(key);
        if (values.size() < 2) {
            continue;
        }
        bool differing = false;
        for (const QString &value : values) {
            if (value != values.first()) {
                differing = true;
                break;
            }
        }
        if (differing) {
            duplicates.append(key);
        }
    }
    return duplicates;
}

QStringList unknownKeys(const ServerModel::Properties &properties) {
    const QStringList known = knownKeys();
    QSet<QString> loweredKnown;
    for (const QString &key : known) {
        loweredKnown.insert(key.toLower());
    }
    QStringList unknown;
    QSet<QString> seen;
    for (const ServerModel::Properties::Line &line : properties.lines) {
        if (line.key.isEmpty()) {
            continue;
        }
        const QString lowered = line.key.toLower();
        if (loweredKnown.contains(lowered) || seen.contains(lowered)) {
            continue;
        }
        seen.insert(lowered);
        unknown.append(line.key);
    }
    return unknown;
}

QString worldDirName(const ServerModel::Properties &properties) {
    const Field *field = fieldFor(QStringLiteral("level-name"));
    if (field == nullptr) {
        return QString();
    }
    QString normalized;
    QString problem;
    // 值不合法时回**空串**而不是硬拼一个 `world`：调用方要靠"空"这件事去拒绝备份 / 恢复
    // （拿一个看不懂的值去拼路径，等着的就是"备份到了别的目录里"这种事故）。
    if (!accepts(*field, valueOf(properties, *field), &normalized, &problem)) {
        return QString();
    }
    return normalized;
}

}  // namespace ServerPropertiesForm
