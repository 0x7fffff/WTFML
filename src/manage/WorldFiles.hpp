/**
 * @file WorldFiles.hpp
 * @brief 世界（存档）目录上那几个**改盘**的动作：认它是不是一个世界、把它拷一份进来、改它的名字。
 * @note 为什么单独有这个文件：实例设置「存档」那一页里，顶上那颗「导入世界」和行尾那支笔
 * （「编辑」）最后都要落到磁盘上，而界面层（ui/）的规矩是**只发信号、不碰盘**。把这些
 * 动作收在 src/manage 里还有第二个好处：验证工程可以绕开"点按钮 → 弹原生选择框"这条
 * 路（原生对话框在测试里驱动不了），直接把函数调起来，把"重名挡住没有""拷了一半失败
 * 会不会留半份"这些事钉死。
 *
 * 和 `VersionManager::copyInstance()` 的分工：那一个多出来的是一份**实例**（版本目录，
 * 除了目录本身还要把 jar/json 改成新名字、刷新 `instanceId`），这里多出来的是一份**世界**
 * —— `saves/` 底下的一个普通目录，除了目录名以外它没有别的身份（世界的真名写在
 * `level.dat` 里面，启动器和游戏都不靠目录名认它）。
 */
#pragma once

#include <QString>
#include <functional>

#include "util/NbtTree.hpp"

namespace WorldFiles {

/**
 * @brief 这个目录像不像一个世界。
 * @param worldDir 待判定的目录。
 * @return 里面有 `level.dat` 就是 true。
 *
 * 「导入世界」用它判用户挑的那一格是不是世界。判据只有 `level.dat` 这一条：
 * * 不判 `region/`、不判 `level.dat_old` —— 一个刚建好还没存过档的世界也有 level.dat，
 * 而 `region/` 是可以为空的；
 * * **故意不去读 `level.dat` 里的世界名**：那是 NBT，得自己解压缩再解析，而它的内容
 * 和目录名不一致时该怎么办（以哪个为准）是个产品问题 —— 现在两处**都不读**它，
 * 一致性由"导入时用目录名"和"游戏自己按目录名找世界"保证。
 */
bool looksLikeWorld(const QString &worldDir);

/**
 * @brief 世界的目录名合不合法。
 * @param rawName 用户输入的名字。
 * @return 合法返回空串；否则返回给用户看的一句原因。
 *
 * NOTE: 走的是**实例目录名**那一套（`InstanceFiles::validateDirName`：不能有分隔符、
 * 不能是 Windows 保留设备名、不能以点结尾、不超过 64 个字符）。世界是普通的
 * 目录，本来只需要"Windows 建得出来"，但两边各写一套判据的结果是"某一类名字在
 * 改名这里过得了、在实例那里过不了"这种没人说得清的区别。**故意复用**。
 */
QString nameProblem(const QString &rawName);

/**
 * @brief 把一个世界目录拷成 `<savesDir>/<name>`。
 * @param worldDir 源世界目录（用户挑进来的那一格）。
 * @param savesDir 目的地：某个实例的 `saves` 目录（**必须先存在**）。
 * @param name 拷过去之后那个目录叫什么。
 * @param problemOut 非空时，失败的原因写进来（成功时清空）。
 *
 * @return 成功 true；失败 false 且 **盘上不会留下半份**（自己收拾掉，见下）。
 *
 * 规矩：
 * * `worldDir` 里没有 `level.dat` → 拒绝（拿一整个 `.minecraft` 当世界拷进来过，
 * 结果是在 saves 底下多出一大坨东西，而且**不报错**）；
 * * 名字先过 `nameProblem()`；目标**已经存在** → 拒绝（**绝不**合并、绝不覆盖 ——
 * `QFile::copy()` 遇到同名文件返回 false，所以"合并"在这里根本做不成，与其半路
 * 失败不如一开始就挡住）；
 * * 用的是 `copyTreeWithoutFollowingLinks()`：隐藏文件一个不落，链接（junction）
 * 直接判失败，**不会**把用户挂到别的盘上的东西抄一份过来。
 *
 * NOTE: 失败时**删掉自己那份半成品** —— 这一点和整合包导入那条路（"失败不删半成品"，
 * 好让用户接着下）**故意相反**：世界是几百 MB 的东西、不是几百个文件分着下的，
 * 没有"接着拷"这回事，留一半在 saves 里只会让游戏列表里多出一个打不开的世界。
 * 删不掉时返回的失败理由里会**附一句**"那一份也没能删掉"，不让用户以后自己撞上
 * 一个来路不明的目录还不知道是谁留的。
 */
bool copyWorldInto(const QString &worldDir, const QString &savesDir, const QString &name,
                   QString *problemOut = nullptr, const std::function<bool()> &cancelled = {});

/**
 * @brief 把 `<某目录>/<老名字>` 改成同目录下的 `<新名字>`。
 * @param worldDir 现在这个世界的目录（含路径）。
 * @param newName 新名字。
 * @param problemOut 非空时，失败的原因写进来（成功时清空）。
 * @return 成功 true。
 *
 * 判据和 `VersionManager::renameInstance()` 那一路一致：名字过 `nameProblem()`、"新名字
 * 和现在一模一样"直接算成功（不报错、也不动盘）、**同目录下已经有同名**（不区分大小写）
 * 就拒绝。
 *
 * NOTE: 改名**只动目录名**，不碰 `level.dat` 里的任何东西。改完之后游戏的世界列表里
 * 也会跟着变 —— 1.13 起游戏列的就是目录名（旧的 `LevelName` 那一位已经不看了），
 * 所以"改个名"这件事在游戏那侧是**真的改名**，不是只改了个文件夹。
 */
bool renameWorld(const QString &worldDir, const QString &newName, QString *problemOut = nullptr);

// ---------------------------------------------------------------------------
// 世界设置：读 / 改 / 写 `level.dat`（打开之前弹出坏档警告并提供备份按钮）
//
// 分成**两截**，和「服务器列表」那条路一个做法：
// * `readWorldInfo()` / `applyWorldInfo()` —— **纯函数**，进一棵树、出一组值，不碰盘。
// "盘上没有的项不发明""改一格别的地方一个字节不变"这些判据全在这一截里，因为纯值
// 进出，验起来不用造一个真存档。
// * `readLevelData()` / `writeLevelData()` —— 真正碰盘的那一截（gzip 那一层走 ZipUtil
// 的 PowerShell，见它的头文件）。
// 混成一截的话，"解析对不对"和"PowerShell 起没起来"会缠在同一个失败返回值里，而这两件事
// 给用户看的话完全不同（一个是"这个存档坏了"，一个是"你机器上没跑起来解压"）。
// ---------------------------------------------------------------------------

/// `<worldDir>/level.dat` 的完整路径（写之前留备份、判"坏没坏"都要它）。
QString levelDataPath(const QString &worldDir);

/// 从 `level.dat` 里读出来、摆在世界设置页上的那一组值。
///
/// NOTE: **每一位都带一个 `hasXxx`**，理由是这一整页的纪律：**盘上没有的项不发明**。
/// 老存档没有 `allowCommands`（1.8 才加的）、有些存档没有 `hardcore` —— 界面上那
/// 一格该显示成"这台存档没有这一项"并且**点不动**，而不是拿一个默认值写进去。
/// 游戏对多出来的字段通常无所谓，但"我用启动器改了一下世界，从此它多出一个我
/// 没要的开关"这件事本身就不该发生。
struct WorldInfo {
    bool gzipped = true;         ///< 盘上那份是 gzip 包着的（少见的不包也能解，见 .cpp）
    bool hasLevelName = false;   ///< `Data/LevelName`
    QString levelName;
    bool hasVersionName = false; ///< `Data/Version/Name`（"1.20.1"）
    QString versionName;
    bool hasLastPlayed = false;  ///< `Data/LastPlayed`（毫秒）
    qint64 lastPlayedMs = 0;
    bool hasDataVersion = false; ///< `Data/DataVersion`（存档格式号）
    qint64 dataVersion = 0;
    bool hasAllowCommands = false;  ///< `Data/allowCommands`
    bool allowCommands = false;
    bool hasHardcore = false;       ///< `Data/hardcore`
    bool hardcore = false;
    bool hasDifficulty = false;     ///< `Data/Difficulty`（0 和平 ~ 3 困难）
    int difficulty = 2;
    bool hasRules = false;          ///< `Data/GameRules` **是一张表**（空表也算有；不是表的算没有）
    QList<NbtTree::GameRule> rules; ///< 顺序就是盘上的顺序
};

/// 游戏规则全集里的一条：名字 + **盘上没有这一条时游戏用的那个值**。
///
/// NOTE: `value` 是字符串，和 `NbtTree::GameRule` 一样（NBT 里 `GameRules` 就是"名字 →
/// `TAG_String`"的表）。形状（开关 / 数字框）由 `NbtTree::ruleKind()` 从这串字推出来
/// —— 所以这一张表里**没有"类型"这一列**，写了也会和值打架。
struct GameRuleDefault {
    QString name;
    QString value;
};

/**
 * @brief 启动器认识的**游戏规则全集**（按名字排序，大小写不敏感）。
 *
 * 【为什么要有一张表】
 *
 * `level.dat` 里那份 `Data/GameRules` **不一定有全部条目**：游戏写档时
 * 写的是它当时认识的那些，而工具做出来的、从没进过的、以及很老的存档**一条都没有**。
 * 那一页不能只写一句「进游戏玩一次就会写进去」—— 那等于让用户先绕一圈。要"列出来
 * 所有的"，缺的那些只能由这一张表兜着。
 *
 * 【这一张表里的是什么】
 *
 * 原版那些常青规则（1.8 就有的一批 + 后来各个版本加的那些），**默认值就是游戏自己的默认
 * 值**。它不是"从盘上读来的"，所以在界面上要能分清（见 `Rule::inFile`）：文件里没有的那
 * 几条，显示的是这里的默认值，**改了才会写进去**。
 *
 * NOTE: 这一张表**不分游戏版本**。存档那个版本不认识的规则，游戏自己会忽略（`GameRules`
 * 是一张字符串表，多出来的键不影响它读），所以最坏的结果是"列表里多几条这个世界用
 * 不上的"。反过来按版本过滤的话，我们得先知道"哪条规则是哪个版本加的" —— 那件事
 * 这一层没有可靠出处（`DataVersion` 是个数字，和版本号的对照表不在这儿），编一个
 * 出来只会让某一档版本的用户看不到本该看到的规则。
 */
const QList<GameRuleDefault> &knownGameRules();

/**
 * @brief 把**盘上那几条**和规则全集并成界面上要摆的那一张表。**纯函数**。
 * @param onDisk `readWorldInfo()` 读出来的那几条（顺序是盘上的顺序）。
 * @return 全集里的每一条（值取盘上那份、盘上没有的取默认值）+ 盘上有而全集里没有的那些
 * （模组写的、我们还不认识的），整体按名字排序（大小写不敏感）。
 *
 * NOTE: 盘上有、全集里没有的那几条**必须留着** —— 这一页说的是"这个世界现在是什么样"，
 * 少列一条就是瞒着用户（他看得见 `mods` 目录里有模组，却看不见模组写进去的规则）。
 * NOTE: 同一个名字在两边都有时**盘上那份说了算**（那是这个世界的真值）。
 */
QList<NbtTree::GameRule> allGameRules(const QList<NbtTree::GameRule> &onDisk);

/**
 * @brief 把**指定**的那几条规则写回树里（可以新增盘上还没有的那一条）。**纯函数**。
 * @param document 整棵树（改的是它，**不落盘** —— 落盘是调用方的事）。
 * @param rules 要写的规则（每一条都是"用户在页面上真改过的那一格"）。
 * @param problemOut 失败原因。
 *
 * 和 `applyWorldInfo()` 的分工：那一个走的是"盘上没有的项不发明"（`hasXxx` 那套，规则
 * 那几条走 `setGameRule()` 的默认档，名字不存在就拒绝）；这一条是 新加的，
 * 走的是 `MissingRule::Create` —— 名字不在表里就加一条，`Data/GameRules` 那一格不在就连
 * 那一格一起建。**调用方保证 @p rules 里的名字是游戏真有的那些**（世界设置那一页拿的是
 * `knownGameRules()` 里的名字，那一页没有"新规则叫什么"这个输入框）。
 *
 * NOTE: 失败时树**可能已经被改了一半**（和 `applyWorldInfo()` 同一条规矩）—— 所以调用方
 * 在失败时**不许落盘**。
 */
bool applyChangedGameRules(NbtTree::Document *document, const QList<NbtTree::GameRule> &rules,
                           QString *problemOut = nullptr);

/// 难度 0~3 的中文名；越界时返回空串（**不返回"普通"** —— 那会把一个坏值说成正常值）。
QString difficultyName(int difficulty);

/// 把一棵解出来的树读成 `WorldInfo`。**纯函数**，读不到的那几位留 `hasXxx = false`。
///
/// NOTE: 这个函数**不失败**（树已经在手上了，"读得出几项"是数量的差别，不是成败）——
/// 会失败的只有解树那一步（`NbtTree::parse()`）。
void readWorldInfo(const NbtTree::Document &document, bool gzipped, WorldInfo *out);

/// 盘上没有这一项的时候怎么办 —— `applyWorldInfo()` 的 `whenMissing`。
enum class MissingField {
    /// 拒绝（**默认**， 那一档）：那一格不在就返回 false，**一个字节都不写**。
    Refuse,
    /// 建一格：按**这一项自己的类型**建（世界名是字符串，允许作弊 / 极限模式
    /// / 难度是 Byte）。`Data` 本身不是复合标签时照样不建。
    Create,
};

/// 把 `WorldInfo` 里那些 `hasXxx = true` 的项写回树里。**纯函数**。
/// @param problemOut 失败原因（哪一项没写进去）。
/// @param whenMissing 那一格**在盘上根本不在**的时候怎么办（见上面那个枚举）。
/// @return 全部成功返回 true；**false 时树可能已经被改了一半**（所以调用方在失败时
/// **不许落盘** —— 用户看到的是"没保存成功"，盘上那份还是完整的）。
///
/// 有 `Create` 这一档，因为界面上那四格**不按"盘上有没有"灰掉**。
/// 老存档没有 `allowCommands`（1.8 才加的）、有些档没有 `hardcore` —— 可动了之后，
/// "盘上没有这一格"就必须有个着落：要么当场说一句"改不了"（用户刚改完的东西
/// **存不下去**，那是最坏的一种界面），要么把它建出来。选后者，但**默认仍然是前者**
/// （`Refuse`）：不主动发明字段，谁要建谁自己传 `Create`。
///
/// NOTE: 建出来的形状是**死的**（世界名 = `String`，其余三项 = `Byte`）—— 这不是"随便挑
/// 一个类型"，是这四项各自在 `level.dat` 里本来就该是什么（见 `readWorldInfo()` 读
/// 它们时用的那几个 `readByteField` / `readStringField`）。
/// NOTE: 判据仍然是**逐项看 `hasXxx`**，所以 `Create` 只会碰到调用方点名要写的那几项 ——
/// 它是"允许建"，不是"见到缺的就补"。
bool applyWorldInfo(NbtTree::Document *document, const WorldInfo &info,
                    QString *problemOut = nullptr,
                    MissingField whenMissing = MissingField::Refuse);

/// 读进内存的一份 `level.dat`。
struct LevelData {
    NbtTree::Document document;  ///< 整棵树（改完之后原样写回去的就是它）
    WorldInfo info;              ///< 从这棵树里读出来的那几项（只读的展示值）
};

/**
 * @brief 把 `<worldDir>/level.dat` 解出来读成一棵树。
 * @param worldDir 世界目录。
 * @param out 成功时写入（**失败时不动它**）。
 * @param problemOut 失败原因（给人看的一句话）。
 * @return 成功 true。
 *
 * 走的路：`level.dat`（gzip）→ 临时文件 → `NbtTree::parse()`。临时文件落在**系统临时
 * 目录**（`QDir::tempPath()`），不落在世界目录里 —— 世界目录里多出来的文件会被游戏扫到。
 *
 * 会失败的那些：文件不在 / gzip 解不动（不是 gzip、被截断）/ 解开之后不是一棵完整的
 * NBT（`NbtTree::parse()` 说的那些）。**任何一条都是"这个存档可能坏了"**，而这一句话
 * 正是用户要在打开之前看到的那个警告（见 MainWindow 里那一段）。
 */
bool readLevelData(const QString &worldDir, LevelData *out, QString *problemOut = nullptr);

/**
 * @brief 把一棵改过的树写回 `<worldDir>/level.dat`。
 * @return 成功 true。
 *
 * 落盘是**先写临时文件、再原子替换**：中途断掉留下的是一个没人认的临时文件，
 * **不是**一个半截的 `level.dat`。这一点比"写得快"重要得多 —— 半截的 `level.dat`
 * 就是用户最怕的那个坏档。
 *
 * NOTE: 中间产物（gzip 之前那份、压好那份）一律落**系统临时目录**，世界目录里我们
 * **一个文件都不摆** —— `QSaveFile` 自己会在 `level.dat` 那个目录里开临时文件，
 * 那是它的事，成功失败都由它收拾。
 * NOTE: 写回去的**形状跟着读进来时那一份**（原来是 gzip 就还是 gzip）。见 .cpp 里那段。
 */
bool writeLevelData(const QString &worldDir, const LevelData &data,
                    QString *problemOut = nullptr);

}  // namespace WorldFiles
