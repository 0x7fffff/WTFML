/**
 * @file NbtTree.hpp
 * @brief NBT 的**整棵树**读写器：把 `level.dat` 读成一棵能改的树，再原样写回去。
 * @author 0x7fffff
 * @date 2026-09-28
 * @note 用户 2026-09-28 原话：「世界根据其存档文件在存档设置（那支笔）里面编辑游戏规则等，
 *       打开之前弹出坏档警告并提供备份按钮」。游戏规则（`GameRules`）就住在存档自己的
 *       `level.dat` 里，而那是 gzip 包着的 NBT —— 要改它，就得**先有一棵能改的树**。
 *
 * 【和 NbtUtil 什么关系（为什么不改那一个）】
 *
 * `util/NbtUtil` 是一个**只读、只跳不建树的**最小读取器，它文件头里那条设计决定写得
 * 很清楚：「**故意不做通用 NBT 读写**：多写一个能改文件的写入器，就等于多一个能把用户的
 * 服务器列表写坏的入口，而启动器对这个文件只需要读」。
 *
 * 那条理由到今天仍然成立，**它说的那个文件也仍然只需要读** —— `NbtUtil` 因此一个字节
 * 都不动（`servers.dat` 的读取照旧走它）。这一份是**另一件事**：它只服务 `level.dat`
 * 这一条路，而那条路上"读"和"写"是同一件事的两半（用户要的就是改规则），所以不能只读。
 * 于是约束换了个形式，从"不写"换成"**写的时候必须保证没碰过的地方一个字节都不变**"：
 *   - 读进来的是**整棵树**（每一种类型都建出来，不是跳过去）；
 *   - 写回去的是**同一棵树**，只有调用方明确改过的那几格不一样；
 *   - 认不出的类型、越界的长度、超深的嵌套一律**当场拒绝**（不是"尽力而为地跳过"）——
 *     一份我们看不懂的文件，宁可一个字都不写。
 *
 * 【这一份里没有盘、没有网、没有 PowerShell】
 *
 * 纯函数：进 `QByteArray`、出 `QByteArray`。gzip 那一层（`level.dat` 通常是压缩的）在
 * `ZipUtil` 里，落盘/备份在 `src/manage/WorldFiles` 里。这样"树的读写对不对"可以完全
 * 离线地一条一条断言（见验证工程 V5 那一带），不用造一个真存档。
 *
 * 【为什么节点是一个"胖结构体"而不是 variant】
 *
 * 一棵 `level.dat` 通常几 KB、最多几十 KB，节点数上千而已 —— 省那点内存换不来什么，而
 * `std::variant` 会让"取一个 Int 的值"变成 `std::get<qint64>(node.value)` 这种每个调用点
 * 都要拼一遍的东西。这里按 `type` 从几个并列的格里取，取错类型的规矩写在 `valueText()`
 * 那一段（**只按自己的 type 解释自己那个格**）。
 *
 * NOTE: **保序**。`Compound` 用的是 `QList<QPair<QString, Node>>` 而不是 `QHash`：NBT 的
 *       复合标签在盘上是有顺序的，虽然 Java 那边读进 `HashMap` 之后顺序本来就会变，但
 *       "我们改一格、别的地方原样写回去"这件事只有在保序时才能逐字节对上 —— 而逐字节
 *       对得上是这一份能被信任的全部理由。
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace NbtTree {

/// 标签类型。**编号就是盘上那个编号**（0~12），不是我们自己排的。
enum class Type : quint8 {
    End = 0,
    Byte = 1,
    Short = 2,
    Int = 3,
    Long = 4,
    Float = 5,
    Double = 6,
    ByteArray = 7,
    String = 8,
    List = 9,
    Compound = 10,
    IntArray = 11,
    LongArray = 12,
};

struct Node;

/// 复合标签的一项。**顺序有意义**（见文件头 NOTE）。
using Field = QPair<QString, Node>;
using FieldList = QList<Field>;
using NodeList = QList<Node>;

/**
 * @brief 一个标签。
 *
 * 值存在哪一格由 `type` 决定：整数族（Byte/Short/Int/Long）都在 `number`（按各自的有符号
 * 位宽解释）、浮点族（Float/Double）在 `real`、字符串在 `text`、`ByteArray` 在 `bytes`、
 * `IntArray`/`LongArray` 在 `ints`/`longs`、`Compound` 在 `fields`、`List` 在 `items`
 * （元素类型在 `itemType`）。**其余的格与这个节点无关**，不要跨类型去读。
 */
struct Node {
    Type type = Type::End;
    /// 整数族与布尔（NBT 没有布尔，0/1 的 Byte 就是它）。
    qint64 number = 0;
    /// 浮点族。
    double real = 0.0;
    /// `String`。
    QString text;
    /// `ByteArray`。
    QByteArray bytes;
    /// `IntArray`。
    QList<qint32> ints;
    /// `LongArray`。
    QList<qint64> longs;
    /// `Compound` 的项（保序）。
    FieldList fields;
    /// `List` 的元素。
    NodeList items;
    /// `List` 的元素类型（`items` 为空时它仍然有意义 —— 盘上就是这个形状）。
    Type itemType = Type::End;
};

/// 一份解出来的 NBT：根那个标签 + 它自己的名字（`level.dat` 的根是**无名**的 Compound）。
struct Document {
    QString rootName;
    Node root;
};

/// 类型的中文名（提示语和断言里用的就是它）。
QString typeName(Type type);
/// 浮点还是整数（`valueText()` 按它决定怎么印）。
bool isFloatingPoint(Type type);
/// 整数族的**有符号位宽**（Byte 8 / Short 16 / Int 32 / Long 64）；不是整数族返回 0。
int integerBits(Type type);

/// 把 `node` 现在的值印成一行字（游戏规则那一格、以及"说不清"的提示语都用它）。
QString valueText(const Node &node);

/// 按**节点自己的类型**收下 `text` 里的那个值。收不下（类型不认识 / 数字写坏了）返回 false，
/// 并且**一个字节都不改**。
bool setValueText(Node *node, const QString &text);

/// 在一个复合标签里找一项（找不到返回 nullptr）。`name` 是**逐字**匹配。
const Node *field(const Node &compound, const QString &name);
Node *field(Node *compound, const QString &name);
/// 从一个**复合**节点出发，按 `Data/GameRules` 这样的路径走下去。中间有一格不存在、
/// 或者中间某一格不是复合标签，返回 nullptr（不做任何"补建"）。
const Node *descend(const Node &root, const QString &path);
Node *descend(Node *root, const QString &path);

/**
 * @brief 解一份 NBT（**已经解开 gzip 之后**的那些字节）。
 * @param raw 正文。
 * @param out 解出来的树（失败时不动它 —— 调用方原来拿着一份就还是那一份）。
 * @param problemOut 给人看的一句话（成功时清空）。
 *
 * 拒绝的东西：长度不够 / 类型不认识 / 长度前缀越界 / 嵌套超过 `kMaxDepth` / 末尾多出
 * 来一截。**任何一条都算失败**，不留"部分解出来"的树。
 */
bool parse(const QByteArray &raw, Document *out, QString *problemOut);

/// 把一棵树写回字节（含根那一格的头）。这一份**不会失败** —— 树本身是合法的（见 parse）。
QByteArray serialize(const Document &document);

/// 嵌套上限（和 `NbtUtil` 用的是同一个数）。
constexpr int kMaxDepth = 64;

// ---------------------------------------------------------------------------
// 游戏规则
// ---------------------------------------------------------------------------

/**
 * @brief 一条游戏规则。
 *
 * 值永远是**字符串**：NBT 里 `GameRules` 就是一个"名字 → TAG_String"的表（`keepInventory`
 * 是 `"true"`、`randomTickSpeed` 是 `"3"`）。所以这里的 `value` 也是字符串 —— 界面上那一格
 * 该画成开关还是数字框，是**看这串字**推出来的（见 `ruleKind()`）。
 */
struct GameRule {
    QString name;
    QString value;
};

/// 一条规则的值是什么形状（决定界面上那一格画成什么）。
enum class ValueKind {
    Bool,   ///< `"true"` / `"false"`（大小写按盘上那份原样认）
    Int,    ///< 能整读成一个十进制整数
    Text,   ///< 别的（有模组会往这里塞任意字符串）
};

/// 这个值是什么形状（判不出整数就是 Text，**不猜**）。
ValueKind ruleKind(const QString &value);
/// 形状的中文名（提示语里写的）。
QString valueKindName(ValueKind kind);
/// 一个"是/否"的字符串认成布尔；认不出返回 false。
bool boolValue(const QString &value, bool *out);

/**
 * @brief 取出 `level.dat` 根里那一张 `Data/GameRules` 表。
 * @return 取出成功（**一条都没有也算成功** —— 老存档、或者全用默认值的世界就是这样）；
 *         没有 `Data/GameRules` 这一格、或者它不是复合标签，返回 false 并给出原因。
 *
 * 表里每一项的**顺序就是盘上的顺序**（不排序）—— 界面要排是界面的事，这里只如实报。
 */
bool gameRulesOf(const Node &root, QList<GameRule> *out, QString *problemOut);

/// 表里没有这个规则名的时候怎么办 —— `setGameRule()` 的 `whenMissing`。
enum class MissingRule {
    /// 拒绝（**默认**，2026-09-28 那一档）：名字不在表里就返回 false，一个字节都不写。
    Refuse,
    /// 加一条（2026-09-29）：名字不在表里就往表尾接一条 `TAG_String`；
    /// `Data/GameRules` 那一格不在（一个刚建好、还没被游戏写过档的世界）就连那一格一起建。
    Create,
};

/**
 * @brief 改一条规则的值（名字不在表里时按 @p whenMissing 决定拒绝还是加一条）。
 *
 * ★ 默认那一档（`Refuse`）是"只改已有的，**不发明新的**"，理由没变：用户在一个已经玩了
 *   几百小时的世界里让我们随手写进去一条**游戏不认识**的规则 —— 那不是"编辑"，那是往
 *   别人的存档里塞东西。默认值就是它，所以老调用点一行都不用改。
 *
 * ★★ 2026-09-29 加了 `Create` 这一档，因为用户点名要它（原话：「下面的世界设置不要要求
 *   进了世界才显示而是把所有的都列出来，并允许修改值」）。上面那条理由还在，只是**收窄
 *   到了调用方**：现在谁传 `Create`，谁就得先保证"写进去的这个名字是游戏真有的那一条" ——
 *   世界设置那一页拿的是 `WorldFiles::knownGameRules()`（启动器认识的规则全集）里的名字，
 *   不是用户手打的一串字（那一页压根没有"新规则叫什么"这个输入框）。
 *
 * NOTE: 值永远是字符串（见 `GameRule`），加进去的也是字符串。
 * NOTE: `Create` 只在 **`Data` 本身是复合标签**时建 `Data/GameRules` —— 连 `Data` 都不是的话
 *       那份 `level.dat` 的形状我们看不懂，一个字节都不动（和 `parse()` 那条规矩一致）。
 *
 * @param problemOut 没写进去时写一句原因（成功时清空）。
 */
bool setGameRule(Node *root, const QString &name, const QString &value, QString *problemOut,
                 MissingRule whenMissing = MissingRule::Refuse);

}  // namespace NbtTree
