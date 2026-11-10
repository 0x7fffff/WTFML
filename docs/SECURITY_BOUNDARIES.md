# 外部输入 → 盘上路径 / 解码：边界盘点（2026-10-31）

用户要求「不要留会被恶意利用的漏洞」。这一轮把**会变成盘上路径的名字**和**会按声明尺寸
分配内存的图片**从头盘了一遍，给缺口补上了判据，并把判据本身用断言钉住。

只做"外部输入当数据看"这一件事；不改任何产品行为（合法输入走的路、界面上的每一句话
都没有变），也不宣称做了完整的渗透测试或安全认证。

## 1. 已经变成路径的那几个名字

| 输入（来源） | 拼成什么 | 判据 | 钉住它的断言 |
| --- | --- | --- | --- |
| `version.json` 的 `assetIndex.id` | `<assets>/indexes/<id>.json` | `AssetNames::isSafeIndexId()` | `verify_downloads` ZAO2 |
| 资源索引里每个对象的 `hash` | `<assets>/objects/<前两位>/<整个哈希>` | `AssetNames::isSha1Hex()` | `verify_downloads` ZAO1 |
| Modrinth / CurseForge 版本的 `filename` | `<实例>/mods/<filename>` 等 | `InstanceFiles::validateFileName()` | `verify_instances` N0 |
| 升级审计表 `wtfml.preset-mod-install.json` 的 `filename` | `<暂存>/mods/<filename>` | 同上（这份表是**从盘上读回来的**） | N0 + 代码里同一处判据 |

要点：

- **判据只有一份**。索引号 / 文件名都是"一段路径"：`InstanceFiles::validatePathSegment()`
  收 `..`、分隔符、`:`（NTFS 备用数据流）、`< > " | ? *` 和控制字符、结尾的点和空格、
  `CON` 这类保留设备名。文件名在它上面只多一条长度上限（`kMaxFileNameLength = 200`）。
  哈希另起一条（40 位十六进制），顺带保证 `objects/<前两位>/` 那一段永远是两位十六进制。
- 拒掉的地方都有**终态**：`downloadAssets()` 把不安全的索引号记成一条失败任务
  （进分母、发 `downloadFinished(false)`），整批不会被当成成功；资源文件名那一路向用户
  说一句"这一版的文件名没法用（…）：<原因>\n没有装/没有下载"，并在下载开始**之前**
  就拦住（`ResourceInstallController::installNow()`），而不是下完一批才逐个失败。
- `AssetNames` 是纯头（`src/download/AssetNames.hpp`），不碰盘，测试直接钉真值表。

## 2. 会按声明尺寸分配内存的图片

PNG / BMP / WebP 的头部可以声明几万乘几万，而压缩后的字节只有几十个 —— 解码器照那个
数字分配，一次就是几个 GB。这一轮把「先问尺寸、超了不解」做成 `ImageDecode::decode()`
的默认行为（`src/util/ImageDecode.hpp` 的 `Limits`）：

- 默认一档 `4096 / 16 MP`，给**网上来的**图标 / 服务器图标用；
- `ThemeImage` 那一档放宽到 `8192 / 64 MP`，因为那是用户自己挑的本机背景图；
- 尺寸检查发生在两个位置：`QImageReader::size()`（只读头，读像素之前）和
  WIC 的 `GetSize()`（`QImage` 分配之前）；解完之后再核一次，兜住"头里没写尺寸"的格式。

调用点和各自的上限：

| 位置 | 图片从哪儿来 | 上限 |
| --- | --- | --- |
| `ResourceStore`（资源图标） | Modrinth / CurseForge | 默认 4096 / 16 MP |
| `ServerPingController` / `ToolboxController`（服务器图标） | 服务器答复里的 data URL、盘上缓存 | 默认 |
| `ThemeImage`（背景图） | 用户挑的本机文件 | 8192 / 64 MP（源文件 32 MB 封顶） |
| `MainWindow::onSkinUploadRequested` / `onOfflineSkinPickRequested` | 用户挑的本机文件（"皮肤包"） | 256×256，源文件 16 MB 封顶 |
| `VersionIcons::fileMark`（实例图标） | 实例目录的 `icon.png` | 8192 / 64 MP |
| `InstanceFilesPage::directoryThumbnail`（存档格） | 存档目录的 `icon.png` | 1024 / 1 MP |
| `InstanceSettingsLifecycle`（截图缩略图） | `screenshots/` 里的文件 | 16384 / 64 MP |

皮肤图真正的尺寸判据仍然是 `SkinApi::isAcceptableSkinSize()`（64×64 / 64×32）——
上面那些只是"别按一个荒唐的声明去分配"，不是替代它。

## 3. 原本就在、这一轮没有改的边界

- 解压：`ZipUtil` 逐条挡 `..`、开头的 `/`、盘符冒号；整合包清单另过
  `ModpackImport::isSafeRelativePath()`（逐段走同一套规矩）。
- 库文件：`LibraryRules::libraryRelativePathIsSafe()` 同时管 artifact 和 natives 的路径。
- 子进程：PowerShell 一律 `-EncodedCommand` + `psQuote()`；递给它的脚本正文有长度上限
  （条目表落临时文件，见仓库外《WTFML-测试笔记》里那一条）。
- 界面：富文本一律 `toHtmlEscaped()`，外部链接 `setOpenExternalLinks(false)`；
  导出的 `.bat` 不许带已知凭据（`SecretRedactor::remember()` 那一层）。

## 4. 刻意没有动的

- **本机用户自己的配置文件**（`options.txt`、`server.properties` 等）按"用户自己的东西"
  处理：读坏了如实报错，不额外加路径判据。
- **列表虚拟化**不在这一轮：它会动选择按身份还回、悬停露详情和滚动锚点，和"大列表性能"
  那一项一起评估。
- 导出的 `.bat` 里参数自带 `"` 的转义：cmd 没有"引号里再次包引号"的写法，`quote()` 的
  注释已说明"不假装能转义"；这是本地操作，不是外部输入。

## 5. 这一轮的验证

`build/verification-mingw13`（MinGW 13.1 / Qt 6.10.1，`-Werror`）全绿：

| 套件 | 结果 |
| --- | --- |
| `verify` | 6173/6173（原 6148，新增 ZC3 的 10 条、N0 的 15 条） |
| `verify_launch` | 329/329 |
| `verify_resources` | 913/913 |
| `ui_check` | 3133/3133 |
| `mw_check` | 2722/2722 |
| `verify_downloads` | 244/244（含 ZAO 新增的 34 条） |
| `verify_instances` | 1622/1622 |

`tools/check-test-modules.ps1` 与 `tools/check-ci-partitions.ps1` 均通过。

## 6. 2026-10-31 补：`powershell.exe` 按系统路径解析

以前五处起 PowerShell 都拿裸名 `powershell`。Qt 在 Windows 上走 `CreateProcess`，
`applicationName` 是空 —— 只有名字的程序按「应用目录 → **当前目录** → 系统目录 → PATH」
找。便携版从下载目录 / 游戏文件夹里跑起来时，同目录扔一个 `powershell.exe` 就会被我们
这一发执行，拿到的是用户自己的权限。

现在统一走 `util/PowerShell.hpp` 的 `PowerShell::executable()`：
`%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe`（32 位进程由 WOW64
重定向到 SysWOW64 那份），解析一次、缓存；连 SystemRoot 都读不到才回退裸名。
Java 下载那条 `Expand-Archive` 顺手从 `-Path` 换成 `-LiteralPath`：`-Path` 认通配符，
数据目录带 `[` / `*` 时会被当成模式去展开。

验证：`verify` 6183/6183（`LongPathTests` 新增一条，钉住解析出来的是系统那份、不是
裸名）；`verify_presets` 762/762（走同一套 `ZipUtil` 解压）。

## 7. 智能体 Stage 1A（2026-11-10）

- 网络只有模型 API 一处；只接受 `https://`，loopback `http://` 需显式勾选并持续提示；
  拒绝 userinfo/片段/查询串；不跟随重定向；不忽略 TLS 错误；key 只进认证头。
- API Key 只落 Windows 凭据管理器（主）或 DPAPI 后备；两条路失败拒绝保存并保留旧值，
  绝不降级为明文/QSettings 明文。界面默认星号遮挡，不回显、不复制、不写日志/会话/错误。
- 会话默认明文 JSON，但不含密钥；可选整会话 DPAPI 加密（`.enc`），失败保留明文并报错，
  解密失败不回退明文。加密只保护落盘，不改变发送给模型的内容。
- 发送前过 `AgentRedaction`：Bearer/JWT/私钥块/云 key/凭据字段值在本机遮挡；用户消息命中
  时提示“已遮挡后再发送”。
- 响应上限 16 MB、SSE 单行 1 MB；流式分块边界、坏 JSON、超限都中止并可见。
- Stage 1A 不读文件/日志/实例元数据、不调用工具、不启动 MCP；模型不能修改系统提示、
  API 配置、工具白名单或审批策略。
- 关闭窗口/切换会话/切换选项卡会取消在飞请求；流中断保留已收到内容并标“已中断”。

## 8. 智能体 Stage 1B：文件工具与审批（2026-11-10）

- 路径：字符串快速拒绝（绝对路径、`..`、盘符、UNC、ADS、保留名、尾点/空格、控制字符）
  之后，已存在路径再用 `CreateFileW` + `GetFinalPathNameByHandleW` + `FileIdInfo` +
  `FileAttributeTagInfo` 核最终路径、卷序列/文件 ID、reparse。**不宣称彻底解决 TOCTOU**。
- 工具根是会话锁定实例；模型不能传实例 id；换游戏文件夹/实例删除返回
  `InstanceUnavailable`，不自动换同名实例。
- `fs.list` 只发目录元数据；完整文件名清单/配置/其他文本逐次确认，**没有会话级目录放行**。
- 凭据文件/凭据存储/二进制硬阻断；普通文本里的凭据字段只发送脱敏片段。
- `fs.write` 只允许 `config/**`、`defaultconfigs/**`、`options.txt`，每次审批；
  必须带旧内容哈希与身份 token；写前 DPAPI 备份、原子替换、写后回读；对象/内容变化拒绝。
- `versions/libraries/assets/java/Servers` 原始写拒绝；这些目录的高层操作留给现有事务。
- 模型可读 artifact 只存脱敏内容并 DPAPI 加密；本地原始存档 id 不在模型可见空间；
  `artifact.read` 每次重新脱敏，先前审批不豁免。
- 审批请求持久化但不存“已批准”布尔；重启后作废、只能重新请求。
- 维护备份 `owner=agent`，元数据保留 `sourceSessionId`；删除会话不删备份；
  `owner=launcher/user/shared` 不允许 Agent 清理。
- 自动读取也要告知“可能发送给当前模型服务”；严格模式关闭所有自动读取。

## 9. G1A 人工审查补充（2026-11-10）

- G1A 除网络错误外全部通过；API Key 未出现在日志/会话/界面，Windows 凭据管理器主路径可用。
- 网络错误（401/403、429、断网、流中断、重定向、证书、代理、400 错误体）延后到
  [网络错误验收](NETWORK_ERROR_ACCEPTANCE.md)，未完成前不写成已通过。
- 会话加密是可选功能：开关现在单独一行显示“加密本会话（DPAPI）”，并标明当前会话是
  明文 JSON 还是已加密；加密只保护落盘，不改变发送给模型的内容。


## 10. G1B 首轮人工发现的边界缺口（未修复前不通过）

- `AgentRedaction` 目前只覆盖精确常见字段名和固定令牌形态；`hypixel_api_key`、
  `DB_PASSWORD`、`REDIS_PASSWORD`、`API_SECRET`、`JWT_SECRET` 等变体未命中。
  **不能把它当成通用凭据兜底**。
- 凭据文件识别只覆盖精确 `.env`、`credentials*`、`token*`、`secret*`、`auth.*` 和
  少数证书后缀；`env`、`test.env` 等变体会进入普通文本审批。
- 大文本 `truncated=true` 时仍会生成模型可读 artifact；artifact 只存本次读取窗口
  （默认 256 KB、最大 1 MB），但脱敏未命中时仍可被 `artifact.read` 分段取回。
  “截断”不等于安全；当前“完整内容见 artifact”文案不准确。
- 日志自动读取仍可能包含 Windows 用户名、启动参数回显、绝对路径、显卡/驱动/音频设备等
  硬件指纹；这些要在 Stage 2 的日志/诊断字段投影里处理。
- 详细记录：[G1B_MANUAL_RESULTS_20261110.md](G1B_MANUAL_RESULTS_20261110.md)。
