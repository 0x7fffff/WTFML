# 请求名额与截止：元数据请求怎么排队

给要加或改联网调用的人看：短请求（查版本、查商店、取皮肤）统一走 `RequestBudget::ControlGet`，共用 4 个并发名额，排队和传输共用一个截止。这是**并发名额**，不是耗时阈值，也不是 TCP 或 Java 连接数。

整套迁移完成于 2026-10-31，数字和调用点已按当前代码核对。

## 机制

`net/ControlGet.hpp` 提供非阻塞的 GET / POST 作业，走 `RequestBudget::Lane::Control`。`RequestBudget`（`net/RequestBudget.hpp`）里 Control 通道固定 4 个名额，与 `HttpClient` 的短请求共用；大文件走 Bulk 通道（默认上限 24，`setBulkLimit()` 可调）。

- **排队不阻塞界面**：没有名额就排队；排队和传输计入同一次尝试的截止。
- **取消 = 删作业**：还在排队的作业一个字节都不会发、也不回调；已经发出的在析构里 abort，同样不回调。切换版本/加载器、销毁页面、对象析构都走这条。
- **没有回复**（排队到点、被掐、QNAM 先销毁）：调用方照旧收尾一次，原因文案由 `unansweredText()` / `replyReason()` 给出，与以前的超时收场一致。
- **截止到点**：在飞的回复上打 `wtfmlControlDeadlineExpired`，调用方据此区分"超时"和"用户取消"，镜像回退等逻辑靠它判断。排队耗时记在 `wtfmlControlQueuedMs`。
- **代次**：每次查询带代次，旧响应不能覆盖新选择。
- **额度归还**：完成、失败、取消、回复销毁都归还名额；完成回调之前先还，避免回调里再发请求时拿不到。
- **正文上限**：默认 2 MiB（`kDefaultBodyLimit`），超了当场 abort，这一发按失败收场，不无界缓冲。上限是 `start()` 的逐作业参数，平台版本表这一路抬到 8 MiB。
- **POST**：`ControlGet::startPost()`，正文按值存进作业，与 GET 走同一套排队/截止/取消；`Content-Type` 仍由调用方设在 `QNetworkRequest` 上。
- QNAM 不换：各调用方继续用自己原来的 QNetworkAccessManager（连接池，不是名额），代理和来源沿用用户设置。

## 渠道 × 名额 × 截止

| 渠道 | 调用点 | 每次尝试截止 | 正文上限 | 没回复时 |
| --- | --- | --- | --- | --- |
| 加载器版本 | `VersionManager` 五类加载器版本查询 | 30 秒；NeoForge 镜像回退是新一次尝试 | 2 MiB | 同一句"拿不到……检查网络后重试" |
| 加载器安装 | `VersionManagerLoaderInstall`：Fabric/Quilt 版本表与 profile、Forge/NeoForge 版本表（镜像↔官方双向兜底）、OptiFine 版本表与广告页 token | 30 秒 | 2 MiB | `replyReason()`；换源判据 `transientJobFailure()`，镜像回退只退一次 |
| 服务端安装 | `ServerInstallController`：Paper 构建列表、版本清单、版本 json（含镜像失败退官方） | 15 秒（`HttpClient::kDefaultTimeoutMs`） | 2 MiB | `unansweredText()`，界面文案与超时相同 |
| Java 管理 | `GlobalJavaSettings::downloadJavaFromMojang` 的 Adoptium 列表 | 30 秒 | 2 MiB | 同步返回 `bool`（嵌套事件循环里等作业收尾） |
| 预设兼容 | `Presets::PlatformCompatibilityProvider`：Modrinth `/project/{id}/version`、CurseForge `/mods/{id}/files` | 12 秒（`m_transferTimeoutMs`） | **8 MiB**（`kVersionBodyLimit`，版本表每发带 changelog 全文） | 结论「查询超时，暂时无法确认」，不算缺失，不再挂重试 |
| 皮肤 | `PlayerSkinStore::refreshForUuid()`（档案）、`fetchTexture()`（贴图） | 10 秒（`kTransferTimeoutMs`） | 2 MiB | 发 `failed()`，`PlayerSkinController` 靠它复位「正在获取」 |
| 资源商店 | `ResourceStore`：搜索、版本表、批量项目（CurseForge `POST /mods`）、`POST /version_files`、精确前置版本/文件、检查更新 `POST /version_files/update` | 15 秒 | 2 MiB | 带 `unansweredText(what, jobError)` 的收尾；检查更新记入 `LatestRun::problem` |
| 资源商店图标 | `ResourceStore::startIconRequest()` | 8 秒（`kIconTimeoutMs`） | 2 MiB | 记一次失败并发回占位图（`iconReady` 契约：每一发都回一次） |

仍自己管超时、不走 ControlGet 的联网：普通下载（Bulk）、Java 运行时压缩包（`downloadFileToLocal()`，几十到两百兆）、`SkinApi` 的 POST/PUT/DELETE（带令牌的写操作）。`resourceRequest()` / `iconRequest()` 里的 `setTransferTimeout()` 保留，出发时被作业按剩余时间覆盖。

## 各调用方保留的约定

- **皮肤**：档案那条路在 429 退避窗口内一个请求都不发（在进入 ControlGet 之前）；贴图按内容寻址缓存，命中即同步发信号、不发请求；`textureRequestUrl()` 做 http→https 与 host 白名单。
- **资源商店**：`m_replies` 是 `QHash<QString, ControlGet*>`，同一把钥匙上换一发 = `delete` 旧作业；回调里不再比较"这一发是不是手上这个"（被换掉的作业不会回调）；钥匙在回调**开头**摘掉，因为收尾信号发出后接收方可能立刻在同一把钥匙上再开一发。429 话术（`throttledText`）、CurseForge key 提示、图标的 304 / 缓存 / 主次地址回退 / 连续三次不通就放弃，不在这一层。
- **预设兼容**：429 窗口、瞬态失败退避重试、`Unknown` 不进缓存，都在 ControlGet 之外。
- **服务端安装**：`m_metadataReply` 是作业指针，`finishWith()` 里直接 `delete`。
- **Java 管理**：UI 线程上的同步调用，排队长时界面等待也变长；这本来就是阻塞等待，没有新增冻结点。
- 这几条路的截止原来是"纯传输超时"，现在是"排队 + 传输"合计。

## 验证（2026-10-31）

- `tests/mw_check/RequestBudgetChecks.cpp`：跨两个 QNAM 的 8 次本机 HTTP 请求、Control 额度耗尽、排队超时、释放后继续、删除排队作业/页面、活动请求取消、传输绝对截止、`VersionManager` 换版本取消、正文上限是逐作业参数（超限掐断、默认上限照常完成、无残留租约）。网络四组回归 4/4（80.06 秒），记录在 `build/test-runs/20261031-005941-ee57bd98/`；扩展后的专项 1/1（9.30 秒）。
- `tests/core_modules/PresetCompatibilityTests.cpp` PH6（6 条）：额度占满时只排队、删掉排队作业后不发、名额回来照常发出、截止到点报超时且不算缺失、超时那发归还名额。`verify_presets` 762/762。
- `tests/core_modules/PlayerSkinTests.cpp` T8：档案、贴图各自在额度占满时只排队，名额回来后到达；排队作业随对象删除后不发、不残留租约。`verify_accounts` 376/376。
- `tests/core_modules/ResourceStoreBudgetTests.cpp` ZI 节（13 条）：搜索、带正文 POST、图标额度占满时只排队；出发的确实是带正文的 POST；同一把钥匙换一发只发一次、只回一次；排队作业随 `ResourceStore` 删除后不发。`verify_resources` 913/913。
- 安装与 Java 三条路没有本机 HTTP 夹具（`LoaderInstallLive` 需要真沙箱），语义由上面的 ControlGet 专项保证。迁移完成时全量 `verify` 6102/6102，`mw_check` 2722/2722，`mw_check_download_retry` 56 PASS / 0 FAIL。

## 意外发现

- 网络管理器销毁场景首次测试时，回复先于 QNAM 的 `destroyed` 通知销毁，触发过早的取消回调。修正：回复销毁时立即还额度，取消回调下一轮确认，管理器销毁时撤销回调。原失败日志在 `build/p2-metadata-final-test.log`。
- `net/ControlGet.hpp` 的截止定时器原先只看 `m_reply && !m_reply->isFinished()`，修法见 [REFACTORING.md](../REFACTORING.md) 中对应一节。
- `mw_check_download_retry` 的"正文上限"判据偶发红（约 1/6）是判据自己的写法问题，已修，见 [PRESET_LIST_IO.md](PRESET_LIST_IO.md) 的"偶发红"一节。

## 未覆盖

| 项 | 状态 |
| --- | --- |
| 每来源 FIFO、速率限制、按来源分别计名额 | 未做；并发名额加既有退避不是 429 的完整解法 |
| `LatestRun`（检查更新）拆成多发的最大并发 | 未做 |
| 签名 URL 过期 / 表示变化的实网样本、多网络对照 | 留在 [ROADMAP](../../ROADMAP.md)；分段协调器的持续低速比较已接入，见 [高速下载](../../features/HIGH_SPEED_DOWNLOAD.md) |
| 其它代理环境、真实上游 429 | 只有本机 HTTP 夹具 |
