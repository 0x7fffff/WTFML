# P2 资源商店请求接入请求预算（2026-10-31）

## 本轮范围

下载页那条路上的九处网络调用从「自己的 QNAM + 自己 `setTransferTimeout()`」改成
`RequestBudget::ControlGet` 作业，与 `HttpClient`、加载器版本查询、模板/清单、平台兼容
查询、皮肤共用四个 Control 名额：

- `ResourceStore::search()` —— 搜索（CurseForge 镜像/官方、Modrinth）；
- `ResourceStore::fetchVersions()` —— 版本表；
- `ResourceStore::fetchProjects()` —— 批量问项目（CurseForge `POST /mods`、Modrinth GET）；
- `ResourceStore::fetchModrinthFiles()` —— 按 SHA-1 反查文件（`POST /version_files`）；
- `ResourceStore::fetchModrinthVersion()` —— 精确前置版本；
- `ResourceStore::fetchCurseForgeVersion()` —— 精确前置文件（`POST /mods/files`）；
- `ResourceStore::fetchLatestVersions()` —— 检查更新那一批（`POST /version_files/update`）；
- `ResourceStore::startIconRequest()` —— 图标那一发。

QNAM 还是原来那一个（连接池，不是名额）。排队与截止由 `ControlGet` 统一管。

- 排队和传输计入同一尝试的截止：接口那几发 15 秒、图标 8 秒（`kIconTimeoutMs`）。
  这两个数原来是「纯传输超时」，现在是「排队 + 传输」的合计 —— 元数据请求都是只读的，
  早放弃比拖着强，这一点没变。
- **新增带正文的作业**：`ControlGet::startPost()`。上面那四处 POST 要带一小段 JSON
  （`{"modIds":[…]}`、`{"fileIds":[…]}`、`{"algorithm":"sha1","hashes":[…]}`），而
  `ControlGet` 原来只做 GET。正文按值存进作业、和 GET 走同一套排队/截止/取消；
  `Content-Type` 仍由调用方设在 `QNetworkRequest` 上（`resourceRequest()` 那第三个参数，
  唯一出处没动）。
- 取消 = 删掉那个作业：还排在队里的作业一个字节都不会发、也不回调；已经发出去的那一发
  在析构里 abort。所以 `m_replies` 从 `QHash<QString, QNetworkReply*>` 换成了
  `QHash<QString, ControlGet*>`，「换掉上一发」从 `abort()` 变成 `delete`，
  `abortAll()` 从逐个 `abort()` 变成 `qDeleteAll()`。
- 回调里那句「这一发还是不是手上这个」的比较**删掉了**：被换掉/被掐掉的作业根本不会
  回调，留着那句话只会让人以为还有第二条竞态。钥匙仍然在回调**开头**摘掉 —— 收尾信号
  发出去之后接的人可能立刻在同一把钥匙上再开一发。
- 没有回复（排队到点 / 被掐 / QNAM 先没）**照旧发一次收尾**：
  - 搜索、版本表、项目信息、文件反查、精确版本 → 带一句
    `unansweredText(what, jobError)`（「…失败：等待网络请求超时。」）；
  - 检查更新那一批 → 记进 `LatestRun::problem`，其余几发照样等；
  - 图标 → 记一次失败、把占位发回去（`iconReady` 的契约是「每一发都会回一次」）。
  以前超时走的是「网络错误 → `failureText()`」那条路，收场一样，只是这句原因更准。
- 正文上限仍用出厂 2 MiB：搜索一页 20 条、版本表、几百个号的项目表都在这条线以内。
  没有为这一路单独抬高。
- 429 的话术（`throttledText`）、CurseForge key 提示（`failureText`）、图标的 304 /
  内容寻址缓存 / 地址主次回退 / 「连着三次不通就放弃」那道闸、`LatestRun` 的代次与合并，
  一条都没动。

## 验证

- `tests/core_modules/ResourceStoreBudgetTests.cpp` 新增 ZI 节（13 条，本机真实 HTTP
  夹具，一个字节都不出网）：额度占满时搜索 / 带正文的 POST / 图标各自只排队（一个字节
  没发、不回调）；名额回来照常发出并收尾；**出发的确实是带正文的 POST**（号表在正文里，
  不是空 GET）；同一把钥匙上换一发 ⇒ 排队的旧作业被删掉、只发一次、只回一次；★ 排队中的
  作业跟着 `ResourceStore` 一起删掉时，名额腾出来也不发、租约不残留。
- 结果：`verify_resources` 913/913；全量 `verify` 6102/6102；`mw_check` 2722/2722
  （含 ZH 那几节真打本机假接口的搜索/版本表）；`mw_check_download_retry` 56 PASS / 0 FAIL。
  首次跑 `mw_check_download_retry` 时「正文上限」那条红的（已知的时序 flake，重跑即过），
  与本次改动无关。

## 仍未覆盖

九处调用点已全部接入，`ResourceStore` 的截止只在作业那一层：`resourceRequest()` /
`iconRequest()` 里那两句 `setTransferTimeout()` 留着，只是让单拎一份请求看也自洽，
出发时会被作业按「整项剩余时间」覆盖掉。仓库里别的独立 QNAM（服务端安装、Java 管理、
随机版本挑选等）仍各自管超时，不在本轮范围。这一轮
只改了「谁排队、谁截止」，没有做更细的东西：每来源 FIFO 或速率限制、按来源分别计的名额、
429 的完整解法（当前只是共用并发名额 + 既有退避），以及 `LatestRun` 那一批拆成多发的
最大并发；分段协调器持续低速择源、签名 URL 过期/表示变化的额外实网样本、多网络对照仍在
主计划。
