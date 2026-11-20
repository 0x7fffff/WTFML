/**
 * @file DefaultSkin.hpp
 * @brief 从离线账号的名字算出**游戏会给他分配哪张官方默认皮肤**，再从本地客户端 JAR 里把那张图取出来（只落缓存）。
 * @note 用户问的是「能不能根据假用户名推测会随机分配什么官方皮肤，到指定地址现下（只存进缓存，
 * 不缝进资源）」。能 —— 而且比他想的还强一层：**那个"随机"其实是确定的**，客户端里没有
 * 掷骰子，只有一条从 uuid 出发的映射。所以这里做的不是"猜一张好看的"，是"算出游戏本来
 * 就会给他的那一张"。于是它同时比随机那条路多两样东西：图是官方的，模型（窄/宽）也是对的。
 *
 * 客户端里那个类叫 DefaultPlayerSkin，入口只有"给一个 uuid"这一种。`javap -p -c` 的结果：
 *
 * - **新规矩（1.20.1，混淆名 fvm）**：一个 18 元数组，
 * `arr[Math.floorMod(uuid.hashCode(), 18)]`。18 条的顺序**就是常量池里那 18 个字符串的
 * 顺序**（不是照社区说法排的，是逐个从字节码里抄下来的）：前 9 条
 * `textures/entity/player/slim/{alex,ari,efe,kai,makena,noor,steve,sunny,zuri}.png`，
 * 后 9 条同一个名字表、换成 `wide/`。**模型由条目自己带**：同一份字节码里 `fvm$a` 这个
 * 枚举是 `SLIM → "slim"`、`WIDE → "default"`（宽版那个模型串就叫 default），
 * `b(uuid)` 取的就是它。所以模型不用另猜，算下标的时候一起出来了。
 * - **旧版布局（1.18.2，fbf）**：只有两张图。私有谓词 `c(UUID)` 返回真走 alex，
 * 假走 steve；`b(UUID)` 是模型串，和 `c()` 同一个分支（同一段字节码里 ldc 出 `"slim"`
 * 和 `"default"`）—— **alex = slim、steve = default（宽）**，这一条是照字节码对上的，
 * 不是照老经验猜的。
 *
 * 【两套规矩**不按版本号判**】
 *
 * 分界线（社区说法是 1.19.4）不适合写成版本号判据：中间那几个小版本的行为没有可靠依据，
 * 而"1.19.4 之前是两张、之后是十八张"这句话不能当先验。
 * 所以判据是**客户端 JAR 自己**：先按新布局那条路径去取，取不到再按旧版布局两条里的一条去取，
 * 两条都没有才算算不出来。好处是它对快照版本、对被人改过名字的版本目录、对未来新版本
 * 一样成立 —— 只要 JAR 里有，就能算。
 *
 * 【只落缓存，不缝进资源】
 *
 * 取出来的图写在 `<数据目录>/skins/default-<账号键>-<版本键>-<条目别名>.png`，仓库和 Qt 资源
 * 里一个像素都不进（用户明确要求）。缓存文件名里那三段都有用：
 * - 账号键 = PlayerSkinStore::uuidKey(uuid)（同一套写法，见那边的 NOTE）；
 * - **版本键** = md5(版本 id) 前 12 位十六进制。**它不能省**：换到低版本之后算出来的是
 * 另一条路径的另一张图，判据必须能分开"这个缓存算的是哪个版本"。用哈希而不是把版本 id
 * 直接拼进去，是因为版本 id 是**外面来的名字**（目录名，可能有空格、全角括号、中文），
 * 拿它拼路径就是在拼一个可能建不出来的文件名 —— 这和 PlayerSkinStore::uuidKey() 不肯
 * 拼野路径是同一条规矩。
 * - 条目别名 = 条目路径去掉公共前缀、`/` 换成 `-`（`player-wide-kai` / 旧版布局的 `alex`）。
 *
 * 于是"算过没有"就是两次 QFile::exists（新老两条各一次，顺序：先新），**命中时一个进程都不起**；
 * 换版本、再换回来也各自命中自己那一份。想重算就删掉 `<数据目录>/skins/default-*`。
 *
 * 【算不出来的时候】
 *
 * 没选中任何已安装版本、版本目录里没有客户端 JAR、或者那个 JAR 里两条都没有（不是原版客户端
 * —— 第三方安装器做的 Forge 那种薄壳 JAR 就是这种情况）—— 都算不出来。**调用方**（MainWindow）
 * 显示内置 Steve，本模块不联网、不弹窗：外观是装饰，
 * 理由同 PlayerSkinStore.hpp 文件头。
 */

#pragma once

#include <QString>

namespace DefaultSkin {

/// 这个版本的客户端 JAR 用的是哪套规矩。**由 JAR 说话**，不是版本号（理由见文件头）。
enum class Rule {
    Unknown,   ///< 还没定下来 / 两条都没取到
    Eighteen,  ///< 十八张：9 个名字 × {slim, wide}，按 floorMod(hash, 18) 取
    Two,       ///< 两张：只有 alex / steve，按 hash 的最低位取
};

/// 算出来的一条结果：哪一张、模型是不是窄的。
struct Prediction {
    /// 算出来了没有。false 时其余字段都不要看。
    bool valid = false;
    Rule rule = Rule::Unknown;
    /// 官方那张皮肤的名字（alex / ari / efe / kai / makena / noor / steve / sunny / zuri）。
    QString name;
    /// 是不是窄模型（游戏里的 `slim`；宽模型那个串叫 `default`）。
    bool slim = false;
    /// 客户端 JAR 里的完整条目名，`/` 分隔，例如
    /// `assets/minecraft/textures/entity/player/wide/kai.png`。
    QString entryPath;
    /// 十八张那套里它在数组中的下标；两张那套没有下标，固定 -1。
    int index = -1;
};

/// 一次"算 + 取"的结果。
enum class Result {
    Resolved,     ///< 图已经在盘上了，pathOut / slimOut 有效
    Unavailable,  ///< 算不出来（没选版本 / 没有 JAR / JAR 里没有那两条）—— **常态，调用方别报错**
    Failed,       ///< 出错了（读 JAR 失败、写不出去）—— 调用方记一行日志，表现同样是退回随机那张
};

/// 九张官方默认皮肤的名字，**按游戏里那个数组的顺序**（新规矩：前 9 个是窄版、后 9 个是宽版，
/// 名字表一模一样）。给验证工程断言用，也让"名字表只有一份"这件事有个出处。
QStringList skinNames();

/// 一次要点几张（= 名字数 × 2）。**唯一的那个 18 就是从这儿来的**。
int eighteenCount();

/**
 * @brief Java 的 `UUID.hashCode()`。
 * @param uuid uuid 字符串，带不带横线都行（大小写也随便，先过 PlayerSkinStore::uuidKey()）。
 * @param hashOut 成功时写哈希值。
 * @return 认不出这个 uuid（不是 32 位十六进制）时返回 false，*hashOut 不动。
 *
 * NOTE: **算法必须和 Java 逐位一致**，不然算出来的皮肤和游戏里那张对不上 —— 而"对不上"
 * 不会报错，只会静默显示另一张。真值（本机 JVM 出的）钉在 verify 里，包括中文名字。
 */
bool uuidHashCode(const QString &uuid, int *hashOut);

/**
 * @brief 按 @p rule 那套规矩算出这张脸。
 * @param uuid uuid 字符串（带不带横线都行）。
 * @param rule 哪套规矩；Unknown 时返回 valid = false。
 * @return 预测结果。uuid 认不出来时 valid = false。
 */
Prediction predict(const QString &uuid, Rule rule);

/**
 * @brief 一个 (账号, 版本, 条目) 对应的缓存图路径。
 * @param uuid 账号 uuid。
 * @param versionId 算它时用的是哪个实例（版本目录名）。
 * @param entryPath 客户端 JAR 里的条目名。
 * @return `<数据目录>/skins/default-<账号键>-<版本键>-<条目别名>.png`；
 * uuid 认不出来时返回空串（**不是**拼一个野路径）。
 *
 * 三段名字的来历见文件头。**这是这条路径的唯一出处**：读和写都走它。
 */
QString cachePathFor(const QString &uuid, const QString &versionId, const QString &entryPath);

/**
 * @brief 算 + 取：把游戏会给这个名字的那张默认皮肤放到缓存里。
 * @param playerName 离线账号的名字 —— **必须是进 `--username` 的那一个**（游戏就是拿它算 uuid 的，
 * 差一个字符就是另一张脸）。
 * @param versionId 拿哪个实例的客户端 JAR 来判断规矩、取图；空串 = 没选中版本。
 * @param pathOut 成功时写缓存图的绝对路径。
 * @param slimOut 成功时写那张图是不是窄模型。
 * @param reasonOut 非 Resolved 时写一句给日志看的原因（Unavailable 也有，只是调用方不用报）。
 *
 * 三条路：① 缓存命中（两次 QFile::exists）→ 一个进程都不起；② 没命中 → 起一次 ZipUtil::readEntry
 * 问新布局那条，取不到再起一次问旧版布局那条；③ 都取不到 → Unavailable。
 *
 * NOTE: 这条路**只碰缓存目录**（`<数据目录>/skins`），仓库里一个字节都不写。
 */
Result resolve(const QString &playerName, const QString &versionId, QString *pathOut,
               bool *slimOut, QString *reasonOut = nullptr);

}  // namespace DefaultSkin
