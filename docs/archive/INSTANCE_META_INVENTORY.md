# 实例元数据 `wtfml.meta`：读写者与判据清单（2026-10-28）

这份清单是 P2「导航/实例元数据渐进收口」的第一步：**先列读写者/路由判据，再谈改**。
下面每一条都对着当前代码核过；没有「应该有，但没找到」的推断 —— 找不到就写"不存在"。

路径常量只有一份：`src/util/AppPaths.hpp` 的 `versionMetaFileName()` / `versionMetaPath()`。

## 一、写的人

**中央写入口**：`VersionManager::updateVersionMeta()`（`src/manage/VersionManager.cpp`）。
它**合并写**：先读现有对象作底，再往上盖要改的键；`null` 约定为"删掉这个键"。
写盘走 `QSaveFile`（同目录临时文件，`commit()` 才改名），并且在
`DirectorySessionLock` 之下。

叫它的地方（每一处就是一次写）：

| 谁 | 写什么 |
| --- | --- |
| `VersionManager::ensureInstanceId()` | `instanceId`（UUID） |
| `VersionManager::renameInstance()` | `name`（必要时连带新 `instanceId`）；失败回滚会把这几个键还原/删掉 |
| `VersionManagerModpack.cpp`（整合包导入收尾） | `realVersion` / `loaderName` / `loaderVersion` |
| `VersionManagerLoaderInstall.cpp::writeLoaderMeta()` | 加载器那三格 |
| `InstanceSettingsController`（实例设置保存） | Java 覆盖、窗口覆盖、`skipJavaVersionWarning`、`icon`、`settingsPages` |
| `LaunchController`（「以后用这个 Java」） | `javaPath` |
| `MainWindow`（「不再提醒」） | `skipJavaVersionWarning=true` |

**下层只有一个实现**：`src/manage/InstanceMetaFile.hpp` 的 `mergeWrite(path, fields, error)`
（2026-10-31 收口）。它按**路径**而不是按"当前数据目录下的哪个 id"工作，所以暂存目录、
候选目录、别的游戏文件夹里的那一份都能用。`updateVersionMeta()` 现在只负责
"哪一份 + 目录锁"，读—改—写全在 `InstanceMetaFile` 里：

- `VersionManager::copyInstance()` —— 写**副本暂存目录**里那一份（换 `name` / 新 `instanceId`）；
- `ModpackCompletion`（整合包收尾）—— 补那三格加载器字段；
- `InstanceUpgrade::applyCoreToCandidate()` —— 先只**验证**读得懂（早失败），末尾才补那三格
  + `upgradeTransactionId`。

都是缩进 JSON（`QJsonDocument::Indented`），都走 `QSaveFile`。更新保留字段语义，
但会统一用户原有排版，不能据排版判断写入者。

## 二、读的人

**唯一解析口**：`VersionManager::readVersionMeta()`（文件不在 / 打不开 / 解不动都回空对象，
打不开和解不动会把 `unreadable` 置真）。字段读法全部经由它：`instanceId()` /
`versionIcon()` / `skipJavaVersionWarning()` / `versionJvmArgs()` / `displayParts()`。

直接拿整份对象的：`VersionManager::versionMeta()`，消费方
`MainWindow`（Java 覆盖）、`LaunchController`（窗口 / Java 覆盖）、
`InstanceDialogController`、`MainWindowViewData`（`type`）。

`InstanceUpgrade` 开头那一次"读得懂吗"的检查也走 `InstanceMetaFile::read()`；
除此之外没有第二个人自己解析这份文件。

**只问"在不在"**：`InstanceFiles`（有 meta = 导入完成）、`InstanceTransferController`
（复制完核对）、`ModpackExport`（打包时排除它）。

## 三、现在的判据

- **格式版本：`schemaVersion`，当前是 1（2026-10-31 起）。** 每次写都会补上这个键；
  文件里没有它 = 1（加键之前写出来的一律按 1 认）。**比 1 新 ⇒ 只读不写**：读侧照常
  返回内容（显示名这类字段的意思没变），写侧拒绝并说明版本号 —— 那种文件可能是更新的
  启动器写的，按 1 的规矩合并会把它的新字段解释错再盖回去。版本号不是整数也拒写。
  （对照：`.wtfml-server` 有 `format` + `version`，`wtfml.import-report.json` 有
  `schemaVersion`。）
- **原子写：有。** 所有写都走 `QSaveFile`，断电只会留旧内容，不会有半截 JSON。
- **未知字段：保得住。** 写侧一律"读进来再盖"，不是"照结构体重建"，
  所以用户手写的键不会被抹掉。文件不存在时才从空对象起。
- **大小上限：16 MiB。** 超过当读不懂处理（实例设置不该这么大，别把内存吃光）。
- **损坏：拒绝写、不备份、不隔离。** 解不动时 `updateVersionMeta` 直接拒绝并报错
  （`tests/core_modules/VersionMetaWriteTests.cpp` 的 O3 钉住了这一条），三个下层调用点
  也走同一个拒写；O9–O11 钉住版本号的三种情形。但**没有任何 `.bak` / 隔离 / 上一版恢复**——
  坏掉的那份就一直坏着，直到用户手动修。

## 四、顺手查出来的一个真问题（服务端那一份）

`.wtfml-server` 是**照着结构体重建**的（`ServerModel::toJson`），未知字段活不过一次
读—改—写。更要紧的是右键菜单里「异常退出后自动重启」那一格：
它 `readMeta()` **不看** `problemOut`，读不懂时拿到的是空 `Info`，再 `writeMeta()` 写回去 ——
于是一份**读不懂的记录会被一份最小合法记录顶掉**，里面记的游戏版本 / 内存 / 端口全没了。

判据其实已经有了（`readMeta` 对"文件在、但读不懂"会给出问题文本），只是那一处没问。
修法是"有问题就不改、并且说出来"，不是"猜一份空的写回去"。

**两件都已修（2026-11-04 复核）**：`setRestartOnCrash()` 先看 `readMeta()` 的 `problemOut`，
读不懂就一个字不写（ZX8.5）；`writeMeta()` 改成"先读盘上那份当底、再把已知字段盖上去"，
未知字段（更新的启动器写的、用户手写的）原样保留（ZX8.6）。
读取侧也补了上限：超过 4 MiB 就按“读不懂”处理，不先 readAll 再判（ZX8.7；wtfml.meta 那边是 16 MiB）。

## 五、下一步（按这个顺序）

1. ~~先把上面那个"读不懂就别写"补上（带一条 core 用例：坏文件在、点一下开关，文件字节不变）。~~
   **已落地（2026-10-28）**：判据收进 `ServerRegistry::setRestartOnCrash()`
   （先看 `readMeta()` 的 `problemOut`，读不懂就一个字不写、把那句话回给调用方；
   文件本来就不在不算问题，照写）。右键菜单那一格改成走它，写不进去弹「没记住」。
   用例 `tests/core_modules/ServerRegistryTests.cpp` 的 ZX8.5：坏文件在 ⇒ 回 false、
   问题非空、**字节一个没动**；修好之后照常写，而且同一份记录里别的格还在。
2. ~~`wtfml.meta` 的格式版本~~ **已落地（2026-10-31）**：`InstanceMetaFile::kSchemaVersion = 1`，
   没有键 = 1，比 1 新或不是整数一律拒写、原有字节不动。仍然**不做** `<name>.bak`：
   损坏时原地不动、由用户修，比自动备份后再猜测安全。
3. ~~收口那三个直写者~~ **已落地（2026-10-31）**：新模块 `manage/InstanceMetaFile.{hpp,cpp}`
   按路径参数化（`mergeWrite(path, fields, error)`），`updateVersionMeta()`
   委托到它，三个直写者也改走它。`InstanceUpgrade` 保留开头那次"读得懂吗"的
   提前检查（升级要干一大摊事，别拖到最后才发现写不进去），真正的写仍在末尾。


## 六、同类扫描（2026-11-04）

把"用户可手改的 JSON / 记录文件"过了一遍：

| 文件 | 未知字段 | 读取上限 | 结论 |
| --- | --- | --- | --- |
| `wtfml.meta` | 保得住（读—合并—写） | 16 MiB | 已收口 |
| `.wtfml-server` | 保得住（ZX8.6） | 4 MiB（ZX8.7） | 已收口 |
| `wtfml.folders` | 保得住（整份文档当底） | 8 MiB（Q16） | 本轮补上限 |
| `servers.dat` | 保得住（改的是 NBT 文档本身，不是重建） | 16 MiB | 无需改 |
| `server.properties` | 保得住（逐行原样保留） | 16 MiB | 无需改 |

写侧仍整份重建的只剩启动历史 / 任务历史这类"由我们生成、schema 自带版本"的账本，
它们没有用户手写字段的语义，不为未知字段做合并写。
