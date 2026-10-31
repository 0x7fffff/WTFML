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
