# P2 安装与 Java 管理的元数据接入请求预算（2026-10-31）

## 本轮范围

装东西那三条路上"问一句就完事"的 GET，从「自己的 QNAM + 自己 `setTransferTimeout()`」
改成 `RequestBudget::ControlGet` 作业，与 `HttpClient`、加载器版本查询、模板/清单、
平台兼容查询、皮肤、资源商店共用四个 Control 名额：

- 服务端安装（`ServerInstallController`）：Paper 那份构建列表、版本清单、某个版本的
  json（含"镜像失败退官方"那一发），都按 15 秒；
- 加载器安装（`VersionManagerLoaderInstall`）：Fabric/Quilt 的版本表与 profile、
  Forge/NeoForge 的版本表（含镜像 ↔ 官方两向兜底）、OptiFine 的版本表与广告页
  token，都按 30 秒；
- Java 管理（`GlobalJavaSettings::downloadJavaFromMojang`）：Adoptium 的
  `/v3/assets/latest/{n}/hotspot` 列表，30 秒。

QNAM 没有换：服务端安装、加载器那几条继续用各自原来的那一份（连接池，不是名额）。

- 排队和传输计入同一尝试的截止（15 秒 / 30 秒）。这两个数原来是"纯传输超时"，现在是
  "排队 + 传输"的合计 —— 这些请求都是只读元数据，早放弃比拖着强。
- 取消 = 删掉那个作业。服务端安装的 `m_metadataReply` 从回复指针换成作业指针，
  `finishWith()` 里直接 `delete`；还在排队的一个字节都不会发、也不回调。
- 没有回复（排队到点 / 被掐 / QNAM 先没）照旧收尾一次：
  - 服务端安装走 `unansweredText()`，界面那一句和以前超时一个收场；
  - 加载器安装走 `replyReason()`，同一句「拿不到 …… 检查网络后重试」；
  - 加载器"换源再试"的判据抽成 `transientJobFailure()`：没回复算瞬态、传输出错按
    `DownloadRetryPolicy::transientReply()`、在飞时被截止掐掉认
    `kDeadlineExpiredProperty` —— 和版本表那条路（`onLoaderVersionsReply`）同一个口径，
    镜像回退只会退一次。
- Java 那一路是 UI 线程上的**同步**调用（嵌套事件循环里等作业收尾），收场仍是同步返回
  `bool`；排队长的时候界面等待也跟着变长，但本来就是阻塞等待，没有新增冻结点。
- 不算进来的：Java 运行时压缩包本身（`downloadFileToLocal()`，几十到两百兆）。那是 Bulk
  量级的正文，不是元数据，继续用自己的 QNAM + 不活动超时。

## 验证

- 全量 `verify` 6102/6102；`verify_resources` 913/913；`mw_check` 2722/2722；
  `mw_check_download_retry` 56 PASS / 0 FAIL。
- 这几条路没有本机 HTTP 夹具（`LoaderInstallLive` 要真沙箱），所以本轮**没有**新增断言；
  排队 / 截止 / 删除即取消的语义由已有的 ControlGet 专项保证（ZI、PH6、T8、
  `RequestBudgetChecks` 的正文上限那一条）。

## 仍未覆盖

元数据这一层到这轮为止已经全部接入：仓库里还自己 `setTransferTimeout()` 的联网只剩
普通下载、Java 运行时压缩包，以及 `resourceRequest()` / `iconRequest()` 那两句会被作业
覆盖的自洽写法。仍未做的：每来源 FIFO / 速率限制、按来源分别计的名额、429 的完整解法；
签名 URL 过期/表示变化、多网络对照仍在主计划（分段协调器的持续低速比较已接入，
见 [高速下载](../../features/HIGH_SPEED_DOWNLOAD.md)）。
