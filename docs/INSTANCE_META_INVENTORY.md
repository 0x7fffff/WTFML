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

**不走中央入口的三个直写者**（各自读—改—写，也是 `QSaveFile`）：

- `VersionManager::copyInstance()` —— 写**副本暂存目录**里那一份（换 `name` / 新 `instanceId`）；
- `ModpackCompletion`（整合包收尾）—— 补那三格加载器字段；
- `InstanceUpgrade::applyCoreToCandidate()` —— 补那三格 + `upgradeTransactionId`。

四处均采用缩进 JSON：`QJsonDocument::toJson()` 默认就是 `Indented`，
中央入口及 `copyInstance` 显式指定同一格式。不要把默认参数误记成紧凑输出。
更新保留字段语义，但会统一用户原有排版，不能据排版判断写入者。

## 二、读的人

**唯一解析口**：`VersionManager::readVersionMeta()`（文件不在 / 打不开 / 解不动都回空对象，
打不开和解不动会把 `unreadable` 置真）。字段读法全部经由它：`instanceId()` /
`versionIcon()` / `skipJavaVersionWarning()` / `versionJvmArgs()` / `displayParts()`。

直接拿整份对象的：`VersionManager::versionMeta()`，消费方
`MainWindow`（Java 覆盖）、`LaunchController`（窗口 / Java 覆盖）、
`InstanceDialogController`、`MainWindowViewData`（`type`）。

**自己解析、不走 `readVersionMeta` 的三处**：`copyInstance()` 的暂存那份、
`ModpackCompletion`、`InstanceUpgrade` —— 就是上面那三个直写者。

**只问"在不在"**：`InstanceFiles`（有 meta = 导入完成）、`InstanceTransferController`
（复制完核对）、`ModpackExport`（打包时排除它）。

## 三、现在的判据

- **格式版本：不存在。** 文件里没有任何 `version` / `schema` / `format` / `metaVersion` 键，
  读侧也不检查。任何 JSON 对象都算合法。要加版本号，得先回答"旧文件怎么认"，这一步今天没有。
  （对照：`.wtfml-server` 有 `format` + `version`，`wtfml.import-report.json` 有
  `schemaVersion`，两者读侧都检查。）
- **原子写：有。** 四处写全是 `QSaveFile`，断电只会留旧内容，不会有半截 JSON。
- **未知字段：保得住。** 四个写者都是"读进来再盖"，不是"照结构体重建"，
  所以用户手写的键不会被抹掉。文件不存在时才从空对象起。
- **损坏：拒绝写、不备份、不隔离。** 解不动时 `unreadable` 置真，`updateVersionMeta`
  直接拒绝并报错（`tests/core_modules/VersionMetaWriteTests.cpp` 的 O3 钉住了这一条）；
  三个直写者也各自中止。但**没有任何 `.bak` / 隔离 / 上一版恢复**——
  坏掉的那份就一直坏着，直到用户手动修。

## 四、顺手查出来的一个真问题（服务端那一份）

`.wtfml-server` 是**照着结构体重建**的（`ServerModel::toJson`），未知字段活不过一次
读—改—写。更要紧的是右键菜单里「异常退出后自动重启」那一格：
它 `readMeta()` **不看** `problemOut`，读不懂时拿到的是空 `Info`，再 `writeMeta()` 写回去 ——
于是一份**读不懂的记录会被一份最小合法记录顶掉**，里面记的游戏版本 / 内存 / 端口全没了。

判据其实已经有了（`readMeta` 对"文件在、但读不懂"会给出问题文本），只是那一处没问。
修法是"有问题就不改、并且说出来"，不是"猜一份空的写回去"。

## 五、下一步（按这个顺序）

1. ~~先把上面那个"读不懂就别写"补上（带一条 core 用例：坏文件在、点一下开关，文件字节不变）。~~
   **已落地（2026-10-28）**：判据收进 `ServerRegistry::setRestartOnCrash()`
   （先看 `readMeta()` 的 `problemOut`，读不懂就一个字不写、把那句话回给调用方；
   文件本来就不在不算问题，照写）。右键菜单那一格改成走它，写不进去弹「没记住」。
   用例 `tests/core_modules/ServerRegistryTests.cpp` 的 ZX8.5：坏文件在 ⇒ 回 false、
   问题非空、**字节一个没动**；修好之后照常写，而且同一份记录里别的格还在。
2. 再谈 `wtfml.meta` 的格式版本：加不加、旧文件怎么认、要不要在损坏时留一份
   `<name>.wtfml.meta.bak` 而不是原地不动。
3. 收口那三个直写者：它们各自重复了"读—改—写 + 拒绝坏文件"这一套，
   应该回到 `updateVersionMeta()`（它在暂存目录 / 候选目录上也能用，只是路径要参数化）。
   **2026-10-29 复核**：三处**都已经**各自挡住了坏文件（不是漏判），所以这一步是
   **去重**而不是修 bug：
   · `VersionManager::copyInstance()`（`VersionManager.cpp` 第 4 步，暂存目录上一个
     自己拼的 `metaPath` + `QSaveFile`）；
   · `manage/ModpackCompletion.cpp`「现有元数据无法读取，未覆盖」那一句；
   · 升级那一趟。三份的规矩与 `updateVersionMeta()` 逐条相同（合并、null = 删键、
     读不懂就不写、`QSaveFile` 缩进输出）。要收的话**先参数化路径**
   （`updateVersionMetaAt(dir, fields, error)`，现有那个委托到它），否则"目标在别的
   游戏文件夹里"那一条又会把人逼回自己拼路径。
