/**
 * @file GameRuleNames.cpp
 * @brief 实现 GameRuleNames：官方中文名那张表（界面上盖在英文名上面的那一层）。
 * @note 这一层**只给界面看**（`ui/` 里的东西，一个字节的盘都不碰，也不参与写盘时的任何
 * 判断）—— 规则叫什么、值是什么，出处仍然是 `WorldFiles` 和盘上那份 `level.dat`。
 * 表本身是从哪儿抄的、照什么手抄，见下面那两段加长说明。
 */

#include "GameRuleNames.hpp"

#include <QHash>
#include <QList>
#include <QPair>

namespace {

/**
 * @brief **官方那张表**：从游戏自己的语言文件里抄来的规则中文名。
 *
 * 【怎么再抄一遍】`assets/indexes/<最新的那个>.json` 里找 `"minecraft/lang/zh_cn.json"`
 * 的 `hash`，文件就在 `assets/objects/<hash 前两位>/<hash>`；抄 `gamerule.*` 里**没有**
 * `.description` 后缀、也不是 `category.*` 的那些（`.description` 是游戏里那一行说明，
 * 这一页不摆它）。
 *
 * 【为什么抄官方那张、不是跟着我们自己的规则表走】
 *
 * 这一页列的是"**这个存档里有什么**"：盘上那些规则（`WorldFiles::allGameRules()` 会原样
 * 带出来，模组写的也算）不一定落在 `knownGameRules()` 那 44 条里 —— 一个 1.21.10 的存档
 * 里就有 `locatorBar` / `minecartMaxSpeed` / `spawnChunkRadius` 这些比那张表新的。中文名
 * 这一层要盖的是**屏幕上真出现的那些行**，所以收的是游戏自己认得的全部规则。
 */
const QList<QPair<QString, QString>> &officialTable() {
    static const QList<QPair<QString, QString>> table = {
        {QStringLiteral("allowEnteringNetherUsingPortals"), QStringLiteral("允许进入下界")},
        {QStringLiteral("allowFireTicksAwayFromPlayer"), QStringLiteral("允许火在远离玩家处蔓延")},
        {QStringLiteral("announceAdvancements"), QStringLiteral("进度通知")},
        {QStringLiteral("blockExplosionDropDecay"), QStringLiteral("在方块交互爆炸中，一些方块不会掉落战利品")},
        {QStringLiteral("commandBlockOutput"), QStringLiteral("广播命令方块输出")},
        {QStringLiteral("commandBlocksEnabled"), QStringLiteral("启用命令方块")},
        {QStringLiteral("commandModificationBlockLimit"), QStringLiteral("命令修改方块数量限制")},
        {QStringLiteral("disableElytraMovementCheck"), QStringLiteral("禁用鞘翅移动检测")},
        {QStringLiteral("disablePlayerMovementCheck"), QStringLiteral("禁用玩家移动检测")},
        {QStringLiteral("disableRaids"), QStringLiteral("禁用袭击")},
        {QStringLiteral("doDaylightCycle"), QStringLiteral("游戏内时间流逝")},
        {QStringLiteral("doEntityDrops"), QStringLiteral("非生物实体掉落")},
        {QStringLiteral("doFireTick"), QStringLiteral("火焰蔓延")},
        {QStringLiteral("doImmediateRespawn"), QStringLiteral("立即重生")},
        {QStringLiteral("doInsomnia"), QStringLiteral("生成幻翼")},
        {QStringLiteral("doLimitedCrafting"), QStringLiteral("合成需要配方")},
        {QStringLiteral("doMobLoot"), QStringLiteral("生物战利品掉落")},
        {QStringLiteral("doMobSpawning"), QStringLiteral("生成生物")},
        {QStringLiteral("doPatrolSpawning"), QStringLiteral("生成灾厄巡逻队")},
        {QStringLiteral("doTileDrops"), QStringLiteral("方块掉落")},
        {QStringLiteral("doTraderSpawning"), QStringLiteral("生成流浪商人")},
        {QStringLiteral("doVinesSpread"), QStringLiteral("藤蔓蔓延")},
        {QStringLiteral("doWardenSpawning"), QStringLiteral("生成监守者")},
        {QStringLiteral("doWeatherCycle"), QStringLiteral("天气更替")},
        {QStringLiteral("drowningDamage"), QStringLiteral("溺水伤害")},
        {QStringLiteral("enableCommandBlocks"), QStringLiteral("启用命令方块")},
        {QStringLiteral("enderPearlsVanishOnDeath"), QStringLiteral("掷出的末影珍珠在死亡时消失")},
        {QStringLiteral("entitiesWithPassengersCanUsePortals"), QStringLiteral("被骑乘的实体能否使用传送门")},
        {QStringLiteral("fallDamage"), QStringLiteral("摔落伤害")},
        {QStringLiteral("fireDamage"), QStringLiteral("火焰伤害")},
        {QStringLiteral("forgiveDeadPlayers"), QStringLiteral("宽恕死亡玩家")},
        {QStringLiteral("freezeDamage"), QStringLiteral("冰冻伤害")},
        {QStringLiteral("globalSoundEvents"), QStringLiteral("全局声音事件")},
        {QStringLiteral("keepInventory"), QStringLiteral("死亡后保留物品栏")},
        {QStringLiteral("lavaSourceConversion"), QStringLiteral("允许流动熔岩转化为熔岩源")},
        {QStringLiteral("locatorBar"), QStringLiteral("启用玩家定位栏")},
        {QStringLiteral("logAdminCommands"), QStringLiteral("通告管理员命令")},
        {QStringLiteral("maxCommandChainLength"), QStringLiteral("命令连锁执行数量限制")},
        {QStringLiteral("maxCommandForkCount"), QStringLiteral("命令上下文数量限制")},
        {QStringLiteral("maxEntityCramming"), QStringLiteral("实体挤压上限")},
        {QStringLiteral("minecartMaxSpeed"), QStringLiteral("矿车最大速度")},
        {QStringLiteral("mobExplosionDropDecay"), QStringLiteral("在生物爆炸中，一些方块不会掉落战利品")},
        {QStringLiteral("mobGriefing"), QStringLiteral("允许破坏性生物行为")},
        {QStringLiteral("naturalRegeneration"), QStringLiteral("生命值自然恢复")},
        {QStringLiteral("playersNetherPortalCreativeDelay"), QStringLiteral("创造模式下玩家在下界传送门中等待的时间")},
        {QStringLiteral("playersNetherPortalDefaultDelay"), QStringLiteral("非创造模式下玩家在下界传送门中等待的时间")},
        {QStringLiteral("playersSleepingPercentage"), QStringLiteral("入睡占比")},
        {QStringLiteral("projectilesCanBreakBlocks"), QStringLiteral("弹射物能否破坏方块")},
        {QStringLiteral("pvp"), QStringLiteral("启用PvP")},
        {QStringLiteral("randomTickSpeed"), QStringLiteral("随机刻速率")},
        {QStringLiteral("reducedDebugInfo"), QStringLiteral("简化调试信息")},
        {QStringLiteral("sendCommandFeedback"), QStringLiteral("发送命令反馈")},
        {QStringLiteral("showDeathMessages"), QStringLiteral("显示死亡消息")},
        {QStringLiteral("snowAccumulationHeight"), QStringLiteral("积雪厚度")},
        {QStringLiteral("spawnChunkRadius"), QStringLiteral("出生区块半径")},
        {QStringLiteral("spawnMonsters"), QStringLiteral("生成怪物")},
        {QStringLiteral("spawnRadius"), QStringLiteral("重生点半径")},
        {QStringLiteral("spawnerBlocksEnabled"), QStringLiteral("启用刷怪笼方块")},
        {QStringLiteral("spectatorsGenerateChunks"), QStringLiteral("允许旁观者生成地形")},
        {QStringLiteral("tntExplodes"), QStringLiteral("允许TNT被点燃并爆炸")},
        {QStringLiteral("tntExplosionDropDecay"), QStringLiteral("在TNT爆炸中，一些方块不会掉落战利品")},
        {QStringLiteral("universalAnger"), QStringLiteral("无差别愤怒")},
        {QStringLiteral("waterSourceConversion"), QStringLiteral("允许流动水转化为水源")},
    };
    return table;
}

/**
 * @brief 别名表：**同一个规矩的另一种写法**。
 *
 * 为什么需要它：官方 lang 里的键名和盘上那个规则名**不总是一样** ——
 *
 * * 规则改过名：`spawnRadius`（1.8~1.21.1）在 1.21.2 改叫 `respawnRadius`，而 lang 里
 * 留着的还是老键；
 * * 1.21.2 起有的规矩从"禁用某某"翻成了"启用某某"（`disableRaids` → `raids`、
 * `disableElytraMovementCheck` → `elytraMovementCheck`），lang 里这两条**各有一条**
 * （老的 `gamerule.disableXxx` 上面那张表里有，新的 `gamerule.minecraft.xxx_yyy` 是
 * 另一条）；
 * * 新记法把规则名收进了命名空间（`minecraft:elytra_movement_check`），盘上、游戏里的
 * `/gamerule` 里都可能是这一种写法。
 *
 * 所以这一张表认另一头那几种写法。**多出来的条目不会凭空多出规则** —— 这一层只翻译
 * 名字，列表里有什么是 `WorldFiles::allGameRules()` 说了算。
 */
const QList<QPair<QString, QString>> &aliasTable() {
    static const QList<QPair<QString, QString>> table = {
        // 「禁用袭击」/「启用袭击」：1.21.2 起那一条规矩正着说了（老名字上面那张表里还认）。
        {QStringLiteral("raids"), QStringLiteral("启用袭击")},
        // 同上。★ 我们自己的规则表里那一条叫 `elytraMovementCheck`（是"启用"那一头）。
        {QStringLiteral("elytraMovementCheck"), QStringLiteral("启用鞘翅移动检测")},
        {QStringLiteral("playerMovementCheck"), QStringLiteral("启用玩家移动检测")},
        {QStringLiteral("fireSpreadRadiusAroundPlayer"), QStringLiteral("火焰蔓延半径")},
        // 带命名空间那种写法（`gamerule.minecraft.xxx_yyy` 那条键对应的是这个规则名）。
        {QStringLiteral("minecraft:raids"), QStringLiteral("启用袭击")},
        {QStringLiteral("minecraft:elytra_movement_check"), QStringLiteral("启用鞘翅移动检测")},
        {QStringLiteral("minecraft:player_movement_check"), QStringLiteral("启用玩家移动检测")},
        {QStringLiteral("minecraft:fire_spread_radius_around_player"), QStringLiteral("火焰蔓延半径")},
        // `spawnRadius` 在 1.21.2 改叫这个名字（lang 里只有老键）。
        {QStringLiteral("respawnRadius"), QStringLiteral("重生点半径")},
    };
    return table;
}

/// 两张表并成一张（头一次用到的时候建一次）。
const QHash<QString, QString> &lookupTable() {
    static const QHash<QString, QString> map = [] {
        QHash<QString, QString> out;
        const auto put = [&out](const QList<QPair<QString, QString>> &table) {
            for (const QPair<QString, QString> &pair : table) {
                out.insert(pair.first, pair.second);
            }
        };
        put(officialTable());
        put(aliasTable());
        return out;
    }();
    return map;
}

}  // namespace

QString GameRuleNames::displayName(const QString &ruleName) {
    // 认不出来就原样回它自己（见头文件里那条 ★）：屏幕上最坏也只是回到改造之前的样子。
    return lookupTable().value(ruleName, ruleName);
}

int GameRuleNames::count() {
    return lookupTable().size();
}
