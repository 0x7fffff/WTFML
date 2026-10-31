# P2 皮肤请求接入请求预算（2026-10-31）

## 本轮范围

外观那条路上的三发 GET 从「自己的 QNAM + 自己 `setTransferTimeout()`」改成
`RequestBudget::ControlGet` 作业，与 `HttpClient`、加载器版本查询、模板/清单、平台兼容
查询共用四个 Control 名额：

- `PlayerSkinStore::refreshForUuid()` —— sessionserver 的档案；
- `PlayerSkinStore::fetchTexture()` —— `textures.minecraft.net` 的贴图；
- `RandomSkinStore::fetchRandomSkin()` —— MineSkin 的随机皮肤列表。

QNAM 还是原来那两个（连接池，不是名额）：不从 `DownloadManager` 借，是因为那边 30 秒的
超时是给几十兆的 jar 的。排队与截止由 `ControlGet` 统一管。

- 排队和传输计入同一尝试的截止（`kTransferTimeoutMs`，10 秒）。这 10 秒原来是「纯传输
  超时」，现在是「排队 + 传输」—— 外观是可选请求，早放弃比拖着强，这一点没变。
- 没有名额时只排队，不阻塞界面。档案那条路还有 429 退避：退避窗口里**一个请求都不发**，
  这一条在进 `ControlGet` 之前，本轮没动。
- 取消 = 删掉那个作业（或者对象析构）：还排在队里的作业一个字节都不会发、也不回调；
  已经发出去的那一发在析构里 abort。删作业这条语义本来就有（调用方要「不要的回调」），
  `ControlGet` 只是把它保持住。
- 没有回复（排队到点 / 被掐 / 管理器先没）仍然发 `failed()`：`PlayerSkinController::
  onStoreFailed()` 靠它把「正在获取」那一格落回 false。以前超时走的是「网络错误 →
  `Failed`」那条路，收场一样。
- `RandomSkinStore` 的 `m_inFlight` 闸在收尾处（不论成败、包括没有回复那一档）先放开再
  发信号 —— 不放的话一次排队超时会把「挑一张」永久卡死。
- 正文上限用出厂 2 MiB：档案几 KB、一张 64×64 皮肤 PNG 也就几 KB、一页 128 条的列表
  JSON 十几 KB。没有为这条路单独抬高。
- 429/204 的静默降级、贴图内容寻址缓存（命中即同步发信号、一个请求都不发）、
  `textureRequestUrl()` 的 http→https 与 host 白名单，一条都没动。

`SkinApi`（POST/PUT/DELETE 管账号皮肤，带令牌）**不在本轮范围**：那是写操作，不是元数据
GET，也不该和「顺手取个列表」抢名额。

## 验证

- `tests/core_modules/PlayerSkinTests.cpp` 新增 T8（本机真实 HTTP 夹具，9 条）：占满
  Control 名额时档案那一发只排队（一个字节没发、不回调）；名额回来照常发出、档案到达；
  贴图那一发同样只排队、名额回来后过 PNG 判据并落盘；`RandomSkinStore::fetchRandomSkin()`
  同样只排队、名额回来后挑出一张指向官方 CDN 形状的地址；★ 排队中的作业跟着对象一起
  删掉时，名额腾出来也不发、收尾不残留租约。
- 为让列表那一发能在本地闭环，`RandomSkinStore` 加了 `setEndpointOverride()`（空串 =
  官方地址，**只有验证工程会调**），和 `PlayerSkinStore::setEndpointOverrides()` 同一套。
- 结果：`verify_accounts` 376/376（连跑三次）；`verify_presets` 762/762；
  `mw_check download-retry-check` 57 PASS / 0 FAIL（exit 0）；全量 `verify`
  6102/6102 通过（含 T8 那 9 条）。

## 仍未覆盖

资源商店（`ResourceStore`：搜索、版本表、项目、文件反查、精确前置、检查更新、图标那九处
带代次与中止语义的网络调用）也已迁到同一档，`ControlGet` 为此加上了带正文的 POST，
见 [资源商店请求预算](P2_RESOURCE_BUDGET_20261031.md)。本轮没有增加每来源 FIFO 或速率限制，
也不把并发名额当作 429 的完整解法。分段协调器的持续低速比较已接入
（见 [高速下载](HIGH_SPEED_DOWNLOAD.md)）；签名 URL 过期/表示变化的额外实网样本、
多网络对照仍在主计划。
