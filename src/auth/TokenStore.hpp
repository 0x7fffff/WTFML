/**
 * @file TokenStore.hpp
 * @brief 登录凭据落盘的**唯一**字节格式：QSettings 里那几个键的名字，加上「明文 JSON →
 *        base64 → 保护」这一层。
 * @author 0x7fffff
 * @date 2026-09-22
 * @note 从 AuthManager.cpp 里搬出来的（原来那套 protectToken()/unprotectToken() 是它文件
 *       内的匿名函数）。搬出来的唯一理由是**多账号**：盘上现在不止"当前这一份"凭据，还有
 *       `SavedAccounts` 里那些账号各自的副本，而它们**必须和活动槽是同一套字节格式** ——
 *       换账号就是把这套字节从一处搬到另一处，两边各写一套编解码就等于给自己造了两个
 *       会各自漂移的格式（新格式只加在一边、旧值只读得出另一边，这类 bug 极难查）。
 *
 * 【盘上的键】
 *   - `MicrosoftTokenData` / `MinecraftTokenData`：**活动槽**，也就是"这次启动会用哪张
 *     令牌"。启动游戏、刷新、换皮肤披风读的都是它。
 *   - `SavedAccounts`：多账号列表（`{"accounts":[…]}`，每一项是那个账号的两份凭据副本）。
 *   - `CurrentAccountUuid`：列表里哪一个是当前账号。
 *   后两个是 2026-09-22 加的，只有 AccountStore 会碰。
 *
 * 【格式是自描述的，为了兼容已经躺在用户盘上的旧值】
 *   以 kTokenMagic 开头 → 后面是 DPAPI 密文
 *   其它               → 旧的 XOR 混淆（读得出来；下次写盘自动升级成新格式）
 *
 * NOTE: 这一层的单元测试在 mw_check 的 R / R2 节（R = 旧格式还读得出，R2 = 新格式
 *       往返，写盘之后盘上确实是 kTokenMagic 开头那串）。**别**在别处再写一遍解码器去"验证"它 ——
 *       那种自证只会把同一处字节格式抄成两份。
 */

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

class TokenStore {
public:
    // ---------- 键名 ----------
    // NOTE: 做成函数而不是散在调用点的字面量：`SavedAccounts` 这个名字要在
    //       AccountStore、验证工程、将来别的读取方之间保持一致。
    static QString microsoftKey() { return QStringLiteral("MicrosoftTokenData"); }
    static QString minecraftKey() { return QStringLiteral("MinecraftTokenData"); }
    static QString accountsKey() { return QStringLiteral("SavedAccounts"); }
    static QString currentUuidKey() { return QStringLiteral("CurrentAccountUuid"); }

    // ---------- 保护层 ----------
    /// 落盘前：明文（base64 之后的 JSON）→ 盘上要写的字节。
    ///
    /// NOTE: Windows 上走 DPAPI；**万一**它失败会退回旧的混淆（并在命令行留一行警告）——
    ///       悄悄降级成弱保护比直接失败更糟，所以有那行警告。
    static QByteArray protect(const QByteArray &plain);

    /// 从盘上读回来：自动区分新格式和旧的混淆值。解不开返回空。
    static QByteArray unprotect(const QByteArray &stored);

    // ---------- 读写 ----------
    /// 把一份 JSON 按上面那个格式写进 QSettings 的某个键。写完 sync 一次并检查状态。
    /// @param errorOut 可选，写失败的原因（给日志/状态行看）。
    static bool writeJson(const QString &key, const QJsonObject &json, QString *errorOut = nullptr);

    /// 读回一份 JSON。键不存在、解不开、解出来不是对象 —— 一律返回空对象（不抛）。
    static QJsonObject readJson(const QString &key);

    /// 盘上有没有这个键（值是空字节也算没有）。
    static bool hasValue(const QString &key);

    /// 把某个键从盘上删掉（找不到不算错）。
    /// NOTE: 只有 AccountStore 删当前账号时用得到 —— 那是"退出登录"的意思，所以必须真的
    ///       把字节删掉，而不是写一个空值进去（留着空值会让"盘上有没有凭据"多出一种状态）。
    static void clear(const QString &key);
};
