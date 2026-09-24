/**
 * @file AccountStore.cpp
 * @brief 多账号列表的实现（见 .hpp 里那三段说明）。
 * @author 0x7fffff
 * @date 2026-09-22
 */

#include "AccountStore.hpp"

#include "TokenStore.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>

namespace {
constexpr const char kAccountsField[] = "accounts";
constexpr const char kUuidField[] = "uuid";
constexpr const char kNameField[] = "name";
constexpr const char kSavedAtField[] = "saved_at";
// 那个账号两份凭据的**副本**（各是一份完整的 payload，格式和活动槽里那份一样）。
constexpr const char kMicrosoftField[] = "microsoft";
constexpr const char kMinecraftField[] = "minecraft";
// 离线账号的标志位。**只有它为 true 的项可以没有上面那两份副本** —— 见 addOffline()。
constexpr const char kOfflineField[] = "offline";

/// 读列表（整份 JSON）。
///
/// NOTE: **两种"读不到"必须分开**（所以有 @p readableOut）：盘上就没有这一格（真的还没有
///       任何账号 —— 这是空列表）和那一格在、但解不开（DPAPI 换了 Windows 账户或换机器
///       之后解不出来，或者值被写坏了）。以前两种都折成空数组，于是 captureActive() /
///       addOffline() 这两个"读-改-写"的写方会拿空数组当底子把整份列表覆盖成一项 ——
///       列表里其它账号那两份 refresh_token 副本一起没了（那些副本是**唯一**能切回去的
///       东西，抹掉就只剩重新登录一条路）。写方现在先看 readableOut，假就不写。
///       只当"列表里有没有这个人"用的读者（find/contains/nameOf/accounts/entryFor/activate）
///       继续忽略它 —— 解不开的时候它们本来就查不到人，行为跟以前一样。
QJsonArray readAccounts(bool *readableOut = nullptr) {
    bool readable = false;
    const QJsonObject root = TokenStore::readJsonChecked(TokenStore::accountsKey(), &readable);
    if (readableOut != nullptr) {
        *readableOut = readable;
    }
    if (!readable) {
        return QJsonArray();
    }
    return root.value(QLatin1String(kAccountsField)).toArray();
}

/// 列表读不出来时给用户看的那句话。三个写方（captureActive / addOffline / remove）共用一份，
/// 免得一处改了另一处还是旧话术。
QString unreadableListMessage() {
    return QStringLiteral(
        "已保存的账号列表读不出来：盘上那一格在，但解不开（换了 Windows 账户或者换了机器"
        "之后会这样）。现在写回去会把列表里其它账号一起抹掉，所以这次没有保存。"
        "想重新开始的话，先删掉那个键再试。");
}

bool writeAccounts(const QJsonArray &accounts, QString *errorOut) {
    QJsonObject root;
    root.insert(QLatin1String(kAccountsField), accounts);
    return TokenStore::writeJson(TokenStore::accountsKey(), root, errorOut);
}

/// 盘上活动槽里 Minecraft 那份 payload —— 只有它知道"当前账号是谁"。
/// 空对象 = 没登录（或者在的这份凭据不完整）。
QJsonObject activeMinecraftPayload() {
    const QJsonObject payload = TokenStore::readJson(TokenStore::minecraftKey());
    if (payload.value(QLatin1String(kUuidField)).toString().isEmpty()) {
        return QJsonObject();
    }
    return payload;
}

/// 列表里那一项（原始 JSON）。找不到返回空对象。
///
/// NOTE: 单独有这个函数是为了 activeUuid() —— 它需要"只看列表、不问当前是谁"，而
///       find() 自己会去问 activeUuid()（要填 isCurrent），拿 find() 取会无限递归。
QJsonObject entryFor(const QString &uuid) {
    if (uuid.isEmpty()) {
        return QJsonObject();
    }
    for (const QJsonValue &value : readAccounts()) {
        const QJsonObject item = value.toObject();
        if (item.value(QLatin1String(kUuidField)).toString() == uuid) {
            return item;
        }
    }
    return QJsonObject();
}

/// 把某个离线账号记成"当前"，并且**清空活动槽**（离开在线身份）。
/// 切换和添加两条路共用这一句，免得一处改了另一处忘。
void makeOfflineCurrent(const QString &uuid) {
    // **清空活动槽之前，先把里面那位在线账号收进列表**（2026-09-24 补的）。
    // 下面两句一执行，活动槽里那份 refresh_token 就在盘上没有了 —— 而它**可能根本不在
    // 列表里**：从没有多账号的版本升上来的用户就是这样（那时凭据只存活动槽），
    // captureActive() 也只在"登录成功"那一下被调过。少了这一句，点一下「添加离线账号」
    // 或是切到某个离线账号，那份唯一的凭据就被抹掉了，而且盘上再没有任何地方找得回来
    // （只能重走一遍浏览器登录）。文件头那句"当前那个在线账号不会丢：凭据还在列表里"
    // 靠的就是这一句成真。
    //
    // NOTE: 收不进去**不拦这次切换** —— 离线账号本来就没有凭据，切过去这条路不该被
    //       盘上的问题挡住；而收不进去的那几种情况都是"列表读不出来"，那时也不该
    //       拿活动槽里那份去覆盖它（captureActive 自己就是这么设计的，见它的实现）。
    QString captureError;
    if (!AccountStore::captureActive(&captureError)) {
        qWarning().noquote() << "切到离线账号之前，没能把当前账号收进列表：" << captureError;
    }
    TokenStore::clear(TokenStore::microsoftKey());
    TokenStore::clear(TokenStore::minecraftKey());
    QSettings settings;
    settings.setValue(TokenStore::currentUuidKey(), uuid);
    settings.sync();
}
}  // namespace

QList<SavedAccount> AccountStore::accounts() {
    const QString current = activeUuid();
    const QJsonArray array = readAccounts();

    QList<SavedAccount> result;
    result.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject item = value.toObject();
        SavedAccount account;
        account.uuid = item.value(QLatin1String(kUuidField)).toString();
        if (account.uuid.isEmpty()) {
            // 一项连 uuid 都没有（写坏了的旧值）就当它不存在 —— 列表里摆一个点不动的账号
            // 比少一项更难查。
            continue;
        }
        account.name = item.value(QLatin1String(kNameField)).toString();
        account.savedAt = item.value(QLatin1String(kSavedAtField)).toString();
        account.offline = item.value(QLatin1String(kOfflineField)).toBool();
        account.isCurrent = !current.isEmpty() && account.uuid == current;
        result.append(account);
    }
    return result;
}

bool AccountStore::contains(const QString &uuid) {
    SavedAccount account;
    return find(uuid, &account);
}

QString AccountStore::nameOf(const QString &uuid) {
    SavedAccount account;
    return find(uuid, &account) ? account.name : QString();
}

bool AccountStore::find(const QString &uuid, SavedAccount *out) {
    if (uuid.isEmpty()) {
        return false;
    }
    const QString current = activeUuid();
    for (const QJsonValue &value : readAccounts()) {
        const QJsonObject item = value.toObject();
        if (item.value(QLatin1String(kUuidField)).toString() != uuid) {
            continue;
        }
        if (out != nullptr) {
            out->uuid = uuid;
            out->name = item.value(QLatin1String(kNameField)).toString();
            out->savedAt = item.value(QLatin1String(kSavedAtField)).toString();
            out->offline = item.value(QLatin1String(kOfflineField)).toBool();
            out->isCurrent = !current.isEmpty() && uuid == current;
        }
        return true;
    }
    return false;
}

QString AccountStore::activeUuid() {
    // 第一档：活动槽里有凭据 —— 那份凭据是谁的，当前账号就是谁。在线账号只走这一档。
    const QString fromSlot = activeMinecraftPayload().value(QLatin1String(kUuidField)).toString();
    if (!fromSlot.isEmpty()) {
        return fromSlot;
    }
    // 第二档：活动槽是空的。这时候**只有离线账号**可能还是"当前"（它本来就没有凭据，
    // 清空活动槽正是"切到它"的一部分）。两个条件都要满足才认：
    //   ① `CurrentAccountUuid` 确实指向列表里的一项（不然那是个孤儿键 —— 列表都删空了
    //      它还留着）；② 那一项是离线的。
    // ② 是这里的关键：在线账号在活动槽空着的时候**必须**算"没登录"，不能因为记录键上
    // 还写着它就说"当前是某某" —— 那正好是 remove() 清空活动槽之后要避免的那个状态。
    QSettings settings;
    const QString recorded = settings.value(TokenStore::currentUuidKey()).toString();
    const QJsonObject entry = entryFor(recorded);
    if (entry.isEmpty() || !entry.value(QLatin1String(kOfflineField)).toBool()) {
        return QString();
    }
    return recorded;
}

bool AccountStore::captureActive(QString *errorOut) {
    const QJsonObject minecraft = activeMinecraftPayload();
    if (minecraft.isEmpty()) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("盘上没有可保存的登录凭据。");
        }
        return false;
    }
    const QJsonObject microsoft = TokenStore::readJson(TokenStore::microsoftKey());
    if (microsoft.value(QStringLiteral("refresh_token")).toString().isEmpty()) {
        // 微软那份才是"下次还能不能换出新令牌"的关键。只有一半的账号收进列表，将来切过去
        // 必然换不出令牌 —— 不如当场不收，让调用方看见这句。
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("微软令牌不完整，这份登录凭据没法保存。");
        }
        return false;
    }

    const QString uuid = minecraft.value(QLatin1String(kUuidField)).toString();
    const QString name = minecraft.value(QLatin1String(kNameField)).toString();

    QJsonObject entry;
    entry.insert(QLatin1String(kUuidField), uuid);
    entry.insert(QLatin1String(kNameField), name);
    entry.insert(QLatin1String(kSavedAtField), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    entry.insert(QLatin1String(kMicrosoftField), microsoft);
    entry.insert(QLatin1String(kMinecraftField), minecraft);

    // 已有同一个人就**就地更新**（换新令牌、改名字都算同一个人），否则追加到末尾。
    // NOTE: 读不出来就**什么都不写**（理由见 readAccounts()）：这一句是"读-改-写"，
    //       底子读错 = 把整份列表覆盖掉，而列表里存着别的账号唯一的一份 refresh_token 副本。
    bool readable = false;
    QJsonArray array = readAccounts(&readable);
    if (!readable) {
        if (errorOut != nullptr) {
            *errorOut = unreadableListMessage();
        }
        return false;
    }
    bool replaced = false;
    for (int i = 0; i < array.size(); ++i) {
        if (array.at(i).toObject().value(QLatin1String(kUuidField)).toString() == uuid) {
            array.replace(i, entry);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        array.append(entry);
    }

    if (!writeAccounts(array, errorOut)) {
        return false;
    }
    // 记下当前账号（`CurrentAccountUuid`）。NOTE: 它**读的时候不参与判断**（见 activeUuid()），
    // 只是留着让"上次用的是谁"在活动槽被清掉之后还查得到。
    QSettings settings;
    settings.setValue(TokenStore::currentUuidKey(), uuid);
    settings.sync();
    return true;
}

bool AccountStore::activate(const QString &uuid, QString *errorOut) {
    QJsonObject entry;
    for (const QJsonValue &value : readAccounts()) {
        const QJsonObject item = value.toObject();
        if (item.value(QLatin1String(kUuidField)).toString() == uuid) {
            entry = item;
            break;
        }
    }
    if (entry.isEmpty()) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("这个账号不在已保存的列表里。");
        }
        return false;
    }

    // 离线账号**没有**副本可写（它本来就没有凭据）：切过去 = 清空活动槽 + 记下当前是它。
    // 所以下面那两个"凭据不完整"的判断对它不适用，得先返回。
    if (entry.value(QLatin1String(kOfflineField)).toBool()) {
        makeOfflineCurrent(uuid);
        return true;
    }

    const QJsonObject microsoft = entry.value(QLatin1String(kMicrosoftField)).toObject();
    const QJsonObject minecraft = entry.value(QLatin1String(kMinecraftField)).toObject();
    if (microsoft.isEmpty() || minecraft.isEmpty()) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("这个账号保存的登录凭据不完整，请重新登录一次。");
        }
        return false;
    }

    // 顺序要紧：**先**写微软那份（刷新令牌的来路），再写 Minecraft 那份。反过来的话，
    // 万一第二步失败，盘上就是"新账号的启动令牌 + 旧账号的刷新令牌"—— 能启动一次，
    // 然后刷新出一个别人的令牌。宁可停在旧账号上。
    if (!TokenStore::writeJson(TokenStore::microsoftKey(), microsoft, errorOut)) {
        return false;
    }
    if (!TokenStore::writeJson(TokenStore::minecraftKey(), minecraft, errorOut)) {
        return false;
    }

    QSettings settings;
    settings.setValue(TokenStore::currentUuidKey(), uuid);
    settings.sync();
    return true;
}

bool AccountStore::remove(const QString &uuid) {
    // **先**问清"它是不是当前账号"，再动列表。顺序反过来的话，删离线账号那一条会漏掉
    // 收尾：离线账号的"当前"是靠列表里那一项证实的（见 activeUuid() 第二档），列表一改
    // 就问不出来了 —— 于是活动槽（本来就空的）和 `CurrentAccountUuid` 都不会被清，
    // 盘上留下一个指向不存在账号的孤儿键。
    const bool wasCurrent = activeUuid() == uuid;
    QJsonArray array = readAccounts();
    QJsonArray kept;
    bool found = false;
    for (const QJsonValue &value : array) {
        const QJsonObject item = value.toObject();
        if (item.value(QLatin1String(kUuidField)).toString() == uuid) {
            found = true;
            continue;
        }
        kept.append(item);
    }
    if (!found) {
        return false;
    }
    if (!writeAccounts(kept, nullptr)) {
        return false;
    }

    // 删的是当前账号 → 连活动槽一起清空（等于退出登录），理由见 .hpp 文件头。
    if (wasCurrent) {
        TokenStore::clear(TokenStore::microsoftKey());
        TokenStore::clear(TokenStore::minecraftKey());
        TokenStore::clear(TokenStore::currentUuidKey());
        qInfo().noquote() << "删掉了当前账号，已一并清除盘上的登录状态。";
    }
    return true;
}

bool AccountStore::logout() {
    // 本来就没登录：别去动盘上那三个键（清一遍是空操作，但返回 true 会让界面说一句
    // "已退出登录"，而用户看到的界面本来就是未登录 —— 那句话是假的）。
    if (activeUuid().isEmpty()) {
        return false;
    }
    // 清活动槽之前先把里面那份收进列表，理由见 .hpp —— 和 makeOfflineCurrent() 的写法一样，
    // 收不进去只留一行日志，不拦这次登出。
    QString captureError;
    if (!captureActive(&captureError)) {
        qWarning().noquote() << "登出之前，没能把当前账号收进列表：" << captureError;
    }
    TokenStore::clear(TokenStore::microsoftKey());
    TokenStore::clear(TokenStore::minecraftKey());
    TokenStore::clear(TokenStore::currentUuidKey());
    qInfo().noquote() << "已退出登录（账号仍然留在已保存的列表里）。";
    return true;
}

// ============================ 离线账号 ============================
//
// 一条离线账号 = 列表里的一项 + 一个名字，盘上没有它的任何凭据。"身份"就是那个 uuid，
// 而 uuid 是从名字按游戏自己那套规则推出来的（见 offlineUuidFor()）。

QString AccountStore::offlineUuidFor(const QString &name) {
    // md5("OfflinePlayer:" + 名字) → 把版本位/变体位摆成 RFC 4122 v3。
    // 摆位这一句不能省：游戏在离线模式下给玩家算 uuid 时做的就是这件事，少了它推出来的
    // 是另一个值 —— 而那个值不会报错，只会让"启动器里的身份"和"游戏里的身份"悄悄错开。
    QByteArray digest = QCryptographicHash::hash(
        QByteArrayLiteral("OfflinePlayer:") + name.toUtf8(), QCryptographicHash::Md5);
    digest[6] = char((digest.at(6) & 0x0F) | 0x30);
    digest[8] = char((digest.at(8) & 0x3F) | 0x80);
    return QString::fromLatin1(digest.toHex());
}

bool AccountStore::addOffline(const QString &name, QString *errorOut) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("离线账号得有个名字。");
        }
        return false;
    }
    // 长度按官方 ID 那条上限（16）。**下限不跟官方走**：官方要求至少 3 个字符，那是登录
    // 名的规矩；离线玩的时候叫 "ab" 完全正常，跟着卡只会拦下本来能用的人。
    if (trimmed.size() > 16) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("离线账号的名字最多 16 个字符。");
        }
        return false;
    }
    // 空白和控制字符真的会有问题：这个名字要进 --username、也要当存档和服务器列表里的
    // 标识，中间带一个空格就会在那些地方被切开或者对不上。
    for (const QChar &ch : trimmed) {
        if (ch.isSpace() || ch.isNull() || ch.category() == QChar::Other_Control) {
            if (errorOut != nullptr) {
                *errorOut = QStringLiteral("离线账号的名字里不能有空格或控制字符。");
            }
            return false;
        }
    }

    const QString uuid = offlineUuidFor(trimmed);
    QJsonObject entry;
    entry.insert(QLatin1String(kUuidField), uuid);
    entry.insert(QLatin1String(kNameField), trimmed);
    entry.insert(QLatin1String(kSavedAtField), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    entry.insert(QLatin1String(kOfflineField), true);

    // 同名 = 同一个 uuid = 同一个人，就地更新，否则追加。
    //
    // NOTE: 名字是**逐字节**比的，不是"改个大小写也算同一个"（2026-09-24 把原文那句错的
    //       括号改掉了）：offlineUuidFor() 是 md5("OfflinePlayer:" + name)，大小写不同就是
    //       不同的字节、不同的 uuid —— `Steve` 和 `steve` 在这份列表里是两条，点它们进的是
    //       两个不同的离线身份。照原来那句注释写测试，会写出一个跟代码相反的期望值。
    // NOTE: 同 captureActive()：列表读不出来就不写，否则这一下会把列表里其它账号抹掉。
    bool readable = false;
    QJsonArray array = readAccounts(&readable);
    if (!readable) {
        if (errorOut != nullptr) {
            *errorOut = unreadableListMessage();
        }
        return false;
    }
    bool replaced = false;
    for (int i = 0; i < array.size(); ++i) {
        if (array.at(i).toObject().value(QLatin1String(kUuidField)).toString() == uuid) {
            array.replace(i, entry);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        array.append(entry);
    }
    // 先列表后"当前"：列表没写成功就什么都不改，免得盘上记着一个列表里根本没有的账号。
    if (!writeAccounts(array, errorOut)) {
        return false;
    }
    makeOfflineCurrent(uuid);
    return true;
}

bool AccountStore::isOffline(const QString &uuid) {
    return entryFor(uuid).value(QLatin1String(kOfflineField)).toBool();
}
