# 外部输入路径与数据安全边界

本文档汇总 WTFML 对外部输入名称、文件路径、图片解码及脚本执行的安全防护边界与校验规范，供核心与功能模块开发者查阅。

---

## 1. 外部输入名称到盘上路径的转换

对于所有从外部清单、网络接口或归档中读取并拼装为本地路径的字符串，统一实施前置安全校验：

| 输入来源 | 目标本地路径 | 校验判据 | 关键断言 |
| --- | --- | --- | --- |
| `version.json` 的 `assetIndex.id` | `<assets>/indexes/<id>.json` | `AssetNames::isSafeIndexId()` | `verify_downloads` ZAO2 |
| 资源索引内对象的 `hash` | `<assets>/objects/<前两位>/<完整哈希>` | `AssetNames::isSha1Hex()` | `verify_downloads` ZAO1 |
| 平台模组版本的 `filename` | `<实例>/mods/<filename>` 等 | `InstanceFiles::validateFileName()` | `verify_instances` N0 |
| 升级审计表 `wtfml.preset-mod-install.json` 的 `filename` | `<暂存>/mods/<filename>` | `InstanceFiles::validateFileName()` | `verify_instances` N0 |

### 核心约束

- **统一判据**：路径段校验统一由 `InstanceFiles::validatePathSegment()` 处理，拦截 `..`、目录分隔符、NTFS 备用数据流（`:`）、保留字符（`< > " | ? *`）、控制字符、尾部点/空格及 Windows 保留设备名称（如 `CON`、`PRN`、`AUX`、`NUL`、`COM1` 等）。文件名在此基础上增加长度上限（`kMaxFileNameLength = 200`）。
- **对象哈希严格匹配**：对象哈希限定为恰好 40 位十六进制字符，保证 `<前两位>/` 目录结构恒为有效双字符十六进制。
- **前置拦截与终态保证**：不合规的索引号直接记为失败任务，下载停止；非法文件名在发起下载前（`ResourceInstallController::installNow()`）即时阻断并提示错误原因，不执行无效文件写入。

---

## 2. 外部图片解码尺寸保护

PNG、BMP、WebP 等格式的头部信息可能声明超大分辨率，引发解码内存暴增。系统通过 `ImageDecode::decode()`（`src/util/ImageDecode.hpp` 的 `Limits`）实施先测后解的尺寸保护：

- 尺寸检查分别在 `QImageReader::size()`（读取像素前）与 Windows Imaging Component (WIC) 的 `GetSize()` 阶段执行，解码后再行复核。
- 各场景尺寸限制：

| 场景位置 | 图片来源 | 防护上限 |
| --- | --- | --- |
| `ResourceStore`（资源图标） | Modrinth / CurseForge | 4096 / 16 MP |
| `ServerPingController` / `ToolboxController`（服务器图标） | 服务器回复的 data URL、本地缓存 | 4096 / 16 MP |
| `ThemeImage`（用户背景图） | 用户指定的本地文件 | 8192 / 64 MP（源文件 ≤ 32 MB） |
| 皮肤导入入口 | 用户指定的皮肤文件 | 256×256（源文件 ≤ 16 MB；最终规格仍按 `SkinApi::isAcceptableSkinSize` 判定 64×64 / 64×32） |
| `VersionIcons::fileMark`（实例图标） | 实例目录内的 `icon.png` | 8192 / 64 MP |
| `InstanceFilesPage::directoryThumbnail`（存档卡片） | 存档目录内的 `icon.png` | 1024 / 1 MP |
| `InstanceSettingsLifecycle`（截图缩略图） | `screenshots/` 目录内的截图 | 16384 / 64 MP |

---

## 3. 基础执行与归档安全

- **归档解压防护**：`ZipUtil` 逐项拦截 `..`、前导斜杠及盘符冒号；整合包清单由 `ModpackImport::isSafeRelativePath()` 逐段校验。
- **动态库与 Native 路径**：由 `LibraryRules::libraryRelativePathIsSafe()` 统一管控 artifact 与 natives 的相对路径。
- **PowerShell 执行安全**：
  - 启动参数一律采用 `-EncodedCommand` 与 `psQuote()` 拼接；
  - 程序路径强制使用绝对系统路径 `%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe`（由 `PowerShell::executable()` 提供解析并缓存），防止当前工作目录或 PATH 劫持；
  - 解压命令统一使用 `-LiteralPath`，防止方括号等特殊字符被识别为通配符展开。
- **界面文本渲染**：外部展示的富文本一律执行 `toHtmlEscaped()` 转义，外部超链接统一禁用 `openExternalLinks`。

---

## 4. 智能体（Agent）安全边界

- **网络边界**：仅允许访问配置的模型 API 端点；仅接受 `https://`（环回 `http://` 需显式确认）；不跟随 HTTP 重定向，严格校验 TLS 证书。
- **凭据隔离**：API Key 仅存储于 Windows 凭据管理器或 DPAPI，不写入日志、会话文件或错误报告；界面默认遮挡并不提供回显与直接复制。
- **数据脱敏**：出站文本统一过 `AgentRedaction`，对已知令牌、密钥块与凭据字段进行本地遮盖。
- **受控文件工具（Stage 1B）**：
  - 工具操作仅限锁定实例根目录；
  - 路径校验结合字符串拒绝与 Windows 句柄级最终路径核对（`GetFinalPathNameByHandleW`、`FileIdInfo`、`FileAttributeTagInfo`），防范 junction 绕过；
  - 写入范围限定在 `config/**`、`defaultconfigs/**` 及 `options.txt`，写前强制执行 DPAPI 自动备份与用户逐次审批。

---

## 5. 验证基线

核心数据层与安全边界断言的现行自动化验证基线如下：

| 验证工程 | 现行断言数 | 重点安全覆盖断言 |
| --- | --- | --- |
| `verify`（核心全套） | 6508/6508 | 含 `LongPathTests`（PowerShell 系统绝对路径解析） |
| `verify_downloads` | 244/244 | 含 ZAO1（40 位 SHA-1 严格校验）与 ZAO2（资源索引号安全段校验）共 34 条 |
| `verify_instances` | 1622/1622 | 含 N0（文件名安全校验 15 条）与 ZC3（路径穿越防御 10 条） |
| `verify_agent` | 306/306 | 智能体出站脱敏、路径校验、会话加密与受控工具权限 |
| `mw_check` | 2766/2766 | 真实主窗口、跨控制器集成与网络异常隔离 |
| `ui_check` | 3177/3177 | 界面控件与样式边界 |

自动化架构检查脚本 `tools/check-test-modules.ps1` 与 `tools/check-ci-partitions.ps1` 保持全部通过。
