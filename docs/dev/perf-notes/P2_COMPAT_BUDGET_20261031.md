# P2 平台兼容查询接入请求预算（2026-10-31）

## 本轮范围

预设兼容检查的那两条平台查询（`Presets::PlatformCompatibilityProvider`：Modrinth
`/project/{id}/version`、CurseForge `/mods/{id}/files`）从「自己的 QNAM + 自己搭一个 abort
定时器」改成 `RequestBudget::ControlGet` 作业，与 `HttpClient`、加载器版本查询、
模板/清单这些短请求共用四个 Control 名额。大文件仍走 Bulk；仍然沿用用户设置的代理与
原有来源，不把请求名额解释成 TCP/Java 连接上限。

- 排队和传输计入同一尝试的截止（`m_transferTimeoutMs`，出厂 12 秒）；没有名额时只排队，
  不阻塞界面。
- 取消 = 删掉那个作业：还排在队里的作业一个字节都不会发；已经发出去的那一发在析构里
  abort，且不回调（结论由 `CompatibilityChecker` 的超时那一路收）。
- 提供方析构、或者它那个 QNAM 先没：不向已经开始析构的业务对象回调。
- 截止到点：回复上打 `wtfmlControlDeadlineExpired`，结论仍是「查询超时，暂时无法确认」，
  既不算缺失，也不在这一层再挂一轮重试。
- 正文上限：`ControlGet` 出厂 2 MiB 仍适用于一般元数据。平台版本表每一发都带着
  changelog 全文，几百上千个版本的项会超过这条线，所以这条路显式抬到 8 MiB —— 仍然是个
  硬顶（异常答复不许无界缓冲），只是不会把一份正常答复截成「这一发失败」。上限是
  `ControlGet::start()` 的逐作业参数，默认值不变。
- 429 窗口、瞬态失败的退避重试、`Unknown` 不进缓存这些既有口径一条没动。

## 验证

- `tests/core_modules/PresetCompatibilityTests.cpp` 新增 PH6（6 条，本机真实 HTTP 夹具）：
  额度占满时这一发只排队（一个字节没发、也不回调）；删掉排队中的作业后腾出名额也不发；
  名额回来照常发出、空版本表照旧判「缺失」；截止到点报「查询超时」且不算缺失；
  超时那一发把共用名额还回去。
- `tests/mw_check/RequestBudgetChecks.cpp` 新增一条：正文上限是逐作业参数 —— 超限答复被
  掐断、默认上限的答复照常完成、收尾不残留租约。
- 结果：`verify_presets` 762/762（含 PH6 全绿）；`mw_check --download-retry-check`
  57 PASS / 0 FAIL（exit 0）；全量 `verify` 见基线记录。

## 仍未覆盖

皮肤那三发（档案、贴图、随机皮肤列表）已单独迁到同一档（见
[皮肤请求预算](P2_SKIN_BUDGET_20261031.md)）；资源商店（`ResourceStore`：
搜索、版本表、项目、文件反查、精确前置、检查更新、图标）也已迁完，`ControlGet` 顺带加上了
带正文的 POST，见 [资源商店请求预算](P2_RESOURCE_BUDGET_20261031.md)；本轮没有增加
每来源 FIFO 或速率限制，也不把并发名额当作 429 的完整解法。分段协调器的持续低速比较
已接入（见 [高速下载](../../features/HIGH_SPEED_DOWNLOAD.md)）；签名 URL 过期/表示变化的额外实网样本、
多网络对照仍在主计划。
