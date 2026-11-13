# 验收流水（2026-10-25 ～ 2026-11-10）

给要查"某项真机/实盘验收跑过没有、数字是多少、遇到过什么坑"的人看。按日期分节，每条写 日期｜跑了什么｜结论｜首轮失败/意外发现；
证据路径都在本地 `build/` 下（已被 git 忽略，不随仓库分发）。需要重跑时见文末「复现命令」。
由 10 篇原验收记录（10-25 深度验收与 P0 深化、P1 收尾、下载恢复、服务端、11-09 真机、G1A/G1B 人工验收与首轮结果、网络错误延后项）合并压缩而成，原文在 git 历史里。

## 2026-10-25　大实例、USB HDD、服务端、窗口与全量回归

环境：Qt 6.10.1 / MinGW 13；只用新建夹具和离线文件，不读真实账号和存档。

| 项 | 结果 | 首轮失败 / 意外发现 |
| --- | --- | --- |
| USB HDD（H:）512 MiB＋3000 文件复制 | 通过，22.62 秒；占用失败不发布、暂存清理、源保留；取消约 0.23 秒 | 初版"定时异常结束"判据太弱（`recover count=0`，没走到日志建立），已补强脚本 |
| HDD 提交边界无析构退出（`_Exit` 探针） | 通过，准备 201.13 秒；退出 86，新进程恢复 1 条事务；原 512 MiB SHA-256、核心和用户标记一致 | 原 180 秒预算超时，改用 600 秒的新事务 `publish-boundary-long`，旧现场保留 |
| 本机 D:（NVMe，Defender 实时保护开）1 GiB＋6000 文件，`--kill-stage publish` 外部终止 | 大复制 3.70 秒；独占失败不发布 3.88 秒；取消 0.23 秒；publish 推进 50.74 秒后外部终止；恢复 `count=1`、0.04 秒，原 jar/用户标记/1 GiB 载荷完整；恢复后重试 52.61 秒完成 | 本机大复制首次发布失败一次、未定位；ASCII/中文路径复测成功。保留，不据重试抹掉 |
| 真实入口同目录拒绝 | 第二进程显示"启动器已在运行或目录不可用"，退出 1；第一进程存活，日志 SHA-256 未变 | — |
| Windows 真实窗口 DPR 1.0 / 1.25 / 1.5（原生 150%，其余用 Qt 缩放） | 主页、列表、账号、实例设置、任务中心、下载页等通过；隔离 Fabric 客户端启动到 1.21.11 主菜单，退出 0/Normal | — |
| 官方 1.21.11 服务端（127.0.0.1:25567，用户授权接受 EULA） | EULA 首启、Java21 正常运行/控制台/stop、Java17 不匹配（UnsupportedClassVersionError，退出 1）、强杀（wasForceKilled=true）通过 | 端口冲突时进程退出 0 被显示成 Stopped，已修：未就绪退出 0 判 Crashed，保留原始退出码；四场景修复后复测全过 |
| 全量自动化 | 核心 16/16 入口（137.47 秒）；主窗口/升级/预设 3/3；其余集成 10/10；非 visual UI 8/8；visual 3119/3119；网络诊断 1/1；合计 38 个不同入口 0 失败 | — |
| 主题逐项保存（从主分支移入） | 主窗口 2684/2684（91.61 秒）；覆盖外部删除、不同键互不覆盖、未知键保留、清除覆盖冲突 | 初轮 2682/2682 不含后补代码，以 2684 为准 |
| PR #8 合并前复核 | 实例回归 1483/1483；publish/verify 各用 16 MiB＋100 文件通过 | 两套探针均失败 1 项：独占源文件后暂存目录未清净（源完整、无半成品）。修复：Windows 删空目录失败后最多 4 次 25ms 重试，出现新内容/链接即停；复测两套 passed=true，实例回归 1487/1487，合并后 **1499/1499** |

### P0 深化（同日，含独立 256 MiB VHD）

- **权限矩阵**：世界保存/导入、实例复制、快照、还原、迁移、预设文件部署、删除，用当前 SID 的真实 NTFS deny/release，共 35 条通过；失败有原因、原件保留、不发布半成品，解除后重试成功。
- **进程中断矩阵**：还原（原目录保留后/新目录发布后/发布与归位同时失败）、整合包完成报告写入时结束进程，共 33 条通过（后加日志占用探针后事务组 41 条）。
- **USB HDD**：128 MiB＋1000 文件，publish 与 verify 两条路径复制/占用失败清理/取消/外部终止/恢复 `count=1`/重试/SHA-256 全过；最长阶段等待 76.029 秒（首份 JSON 记 48.44 秒，是统计口径漏算推进时间，已修，原记录不覆盖），复查最长 20.363 秒。
- **VHD 满盘**：操作开始前卷已满时，世界保存/导入、实例复制、快照、预设文件部署、模组迁移、还原七类均报告失败，原件 SHA-256 不变、无半成品，释放占位后重试成功（73 条检查）。写入中途耗尽：备份暂存副本（错误码 112，12 条检查）、ZIP 压缩（`archive.pending` 写到 4866520 字节时占满，外部压缩器报 "not enough space"，12 条）通过；还原日志提交占用（独占日志阻断 Published 状态的原子替换）保留新旧目录并报告 `retainedPath`，新进程恢复不覆盖任一份。
  - 首轮失败：几字节的小夹具遇 ENOSPC 仍保存成功（NTFS 驻留数据，不算结论），改用 2 MiB 随机内容；中途满盘探针先后因"freeBytes 必须为 0"（实际剩 4096）和 Qt `ResourceError` 判据（Qt 实际归 `WriteError`）失败，改成按原始错误码＋剩余空间小于下一块判定，没有放宽产品断言。
- **范围收口**：用户确认以启动器软件责任为发布 P0 范围（失败不误报成功、原件/半成品保护、取消与中断后的保守恢复）；断电、设备物理损坏、杀毒组合不再作发布 P0，仍未验收。
- **终批 `p0-closure-20261025-154237`（passed=false，原件保留）**：资源模块首次 823/824（取消夹具继承上一段 `complete=true`，改为先重置，补跑 825/825）；窗口阶段重编译接近 300 秒被外层预算截断（工具失败，上限改 450 秒，NoBuild 补验通过，93.96 秒）；**HDD 恢复后重试升级报"用户文件副本未通过完整校验"一次**（恢复后快照 1007 个文件大小与 SHA-1 全匹配，无损坏；首因不明）。
- **诊断补强**：`copyTreeWithoutFollowingLinks()` 的 `problemOut` 接到全部 5 个复制入口（原地升级、复制实例、世界导入、预设备份、数据包替换），失败话术点名文件、阶段和系统错误；升级区分"复制失败"与"副本校验失败"。回归 `verify` 5911/5911、`verify_instances` 1509/1509。
- **掉盘提示**：新增 `StorageAvailability`（卷认得出且就绪才算在；落点未建时往上找最近祖先），接入启动按钮/启动前检查（说"游戏文件夹所在的磁盘现在不可用"）、下载入口（开批次前探卷，盘不在发终态、不写字节）、批次失败话术；判据 ZK1，`verify_launch` 290/290。设备卡死、系统调用不返回时仍无应用层超时。
- **HDD 间歇失败复现**：同参数新夹具连续 10 轮 10/10 通过（重试升级 15.7–26.2 秒）；2026-10-27 再跑一次整轮约 46 秒全过。未复现，按用户决定从 P0 降为 P3，根因未确认，也不据此宣称已修。

## 2026-10-27 ～ 28　服务端与下载恢复

| 项 | 结果 | 首轮失败 / 意外发现 |
| --- | --- | --- |
| 真实 1.20.1 服务端：原版 / Fabric 0.16.14 / Forge 47.2.0（Microsoft Java 17，127.0.0.1） | 三种均安装成功；EULA 拒绝、就绪后 stop 保存退出、再次启动强杀、端口占用、Java 8 不匹配全部验证，`report.json` 均 ok=true | 端口占用时原版/Fabric 可能退出 0，生产状态须为 Crashed；Fabric 遇 `UnsupportedClassVersionError` 可能留下错误界面/线程不退出（120 秒超时判失败）→ 产品现在启动中识别该致命 Java 错误，结束自己启动的失败进程，按异常启动报告，不当用户强杀 |
| 安装向导真实主窗口链 + 控制台日志导出（脱敏） | 三种现场 `ui-report.json` ok=true；属性编辑用读取时 SHA-256 基线，外部改动后拒绝保存 | Fabric 向导关闭后 getter 用 `isVisible` 丢加载器参数 → 改按栏位自身 `isHidden` 判断 |
| 并发重装的 Forge 现场 | 原现场 `DOWNLOAD_MOJMAPS` 静默到 300 秒工具截止，未装成功；补有界静默重试后，同现场 run-only 重试 installed/ok=true | 不据后续通过宣称网络根因消失；高速下载默认 3 分钟静默 / 10 分钟总截止 |
| 下载恢复模块（网络/预设）| 5/5（74.08 秒；最终 72.48 秒）；USB HDD 文件批次 8.08 秒、预设 12.50 秒；最终复核 2/2（8.44 / 11.63 秒） | 4 次首轮失败：缺头文件、字段写成调用、受限环境 Permission denied（5 组失败＋预设夹具段错，根因未定）、安装器夹具漏生成 `user_jvm_args.txt`＋持久暂存使旧"空目录删除"夹具失效（顺带补了生产代码不得重建已消失目标的漏洞） |
| 主窗口回归 | 2716/2716（93.18 秒）；正式构建退出 0 | 新暂停断言先暴露：启动版本列表未返回时错误禁用暂停（独立版本列表请求被一并阻挡），已修；版本 JSON/资源索引阶段仍不能暂停 |
| 真实 Windows 窗口：服务端恢复（单一任务、暂停、重开主窗口、补齐后等待、第二次明确继续） | `wizard-install.json` 全过，不启动服务端 | — |
| 客户端真实 1.20.1 / Forge 47.2.0 / Java 17 恢复链 | 冷安装执行实际安装器，暂停退出、新 OS 进程补齐、再明确继续并应用 FOV 预设，"安装完成"提示由电脑控制确认（不含主菜单/世界） | 冷下载超出工具 300 秒观察窗口，产品已补齐等待继续而工具空等；发现并修复实发布缺陷：命名实例薄 JSON 与老的未完成导入形状相同，被列表/启动守卫误拦 → 改为父版本文件完整后才原子保存实例身份 |

要点：预设暂存改为实例内 `.wtfml-preset-downloads/<sha1>.jar`，旧文件每次完整校验；服务端文件批次绑定任务并保存原服务端/Java/版本/加载器/下载清单，Paper 等动态构建不静默换版本；批次回执不序列化 Java 执行栈，安装器运行和文件发布期间不能暂停，跨重启须重新明确继续；大原件/模组目录的同步指纹成本归 P2。

## 2026-10-28　P1 收尾

| 项 | 结果 | 首轮失败 / 意外发现 |
| --- | --- | --- |
| 正版登录配置 | 移除源码中的历史 App ID 兜底；构建时 `WTFML_CLIENT_ID` 注入，`WTFML_REDIRECT_URI` 默认 `http://localhost:38226/`；未配置时登录/刷新在发请求前失败并给中文说明；账号模块 367/367 | 项目尚无自己的 Azure App ID，真实授权/刷新/启动/退出全链暂缓 |
| 系统 DPI 实机采集（`wtfml_desktop_acceptance`） | 100% / 125% / 150% 各 24 张（三主题×四页面×两档尺寸），共 72 张；无基础窄屏控件遮挡 | 发现主题缺陷：设置说明文字（`addDynamicHint()`）固化创建时的 PlaceholderText，翡翠切炭灰后仍暗绿 → 改共享 secondary 文字角色，150% 重采 24 张失配 0；100%/125% 采于修复前，未重测。900×600 请求实际最小为 900×620。受限环境 DNS 失败，下载列表只有空状态 |
| 预设测试空列表保护 | PA4/PA7/PA9 补非空判断；受限运行 713/717（3 项目录链接被拒、1 项本地 HTTP 重试取消失败），放开后 736/736 | — |
| 保守并行 | 账号/账本/工具箱 3 个模块并行，其余 35 项独占；串行/并行均 3/3 | 更激进并行属性能工作，未开放 |
| 本机回归 | 非 visual **38/38**（395.53 秒）；visual **3121/3121**（9.91 秒）；主窗口 2716/2716 | 首次构建发现网络诊断工具缺恢复/凭据存储依赖，补目标依赖后通过；一次完整回归因会话中断无报告，不算通过 |
| 凭据 | 认证响应与凭据存储已知字段（含嵌套账号）统一脱敏，覆盖 Microsoft access/refresh、Xbox、Minecraft token、授权码、PKCE verifier；DPAPI 保护失败改为拒绝写入、不再降级保存（旧文件字节不变且可解密）；输出扫描 16–21 个文本样本疑似令牌 0 | 仅文本抽样 |
| 原生交互（当前主屏 150%） | 扩展界面 60 张（三主题×两尺寸×十场景）、24 组受控 Tab；双原生日志窗口各 25000 行 10 项通过（上限 20000，心跳最大间隔 86ms）；电脑控制点击启动隔离 Fabric 1.21.11 到主菜单再退出（0/Normal） | 真实 Tab 首次未分配焦点（三次探针失败保留）→ 修窗口空焦点时从可见后代进入焦点链，复测 focusFailures=0 |
| Release 与实网 | Release 入口：同目录拒绝、不同目录并发、四线程 800 行日志脱敏通过；去掉 Qt 搜索路径的部署包冒烟通过；Modrinth Fabric API 2426039 字节、CurseForge Oritech 9006088 字节均校验大小/SHA-1（21.27 秒）；版本列表 921 项，镜像失速后官方回退成功 | 部署器提示可选 dxcompiler/dxil 缺失；包内诊断自检首次误指定 offscreen（部署包只含 Windows 插件），45 秒截止，改用 windows 插件后通过；版本唯一出处仍为根 CMake 0.4.2 |
| 产物审计 | 824 个源码/配置与 24 个部署文件摘要失配 0；验证程序残留 0 | — |

暂缓（不算通过，已登记在 [ROADMAP](../ROADMAP.md)）：正版真实全链、100%/125% 复杂场景补测、Windows"减少动态效果"真实设置、历史探针首因、Actions 线上测量。

## 2026-11-09 ～ 10　真机验收

| 项 | 结果 | 首轮失败 / 意外发现 |
| --- | --- | --- |
| 双进程目录边界（`acceptance_directory_boundary.py`，static-release 产物） | **16/16 PASS**；第二进程显示并截图拒绝对话框、不建主窗口、退出 1；私有 INI、并发日志、凭据脱敏、marker 校验通过 | — |
| 窄窗口 1000×750 + 150% 缩放（acceptance profile 新增 `windowWidth`/`windowHeight`） | PASS；截图 1500×1125，导航/hero/账号卡/底栏无裁切 | — |
| 大实例升级 1 GiB＋6000 文件，kill 在 publish（`--kill-stage publish`） | **PASS 0 failure**：large-copy 4.62 s、locked-copy 5.01 s、cancelled-upgrade 1.56 s；恢复 0.05 s（原实例/用户文件/1 GiB 载荷/原 jar 完整）；retry-upgrade 57.44 s（checking 10.5 → verify-snapshot 34.9 → retain-original 54.2 → publish 54.2 → done 57.4） | 首次 64 MB 验收失败：`ZipUtil` 的压缩炸弹闸（压缩比 1000）误杀全零 region（真实世界大量空区块合法），升级快照自校验失败 → 两处阈值 1000→10000，条目 2 GiB / 整包 16 GiB 硬上限不变，`BackupTests` 加 8 MB 全零回归 |
| 完整核心 `verify.exe`（真实 Windows，非 offscreen） | **6508/6508**（11-09 与 11-10 窗口化/统计缓存/预设快速写之后各一次） | — |
| 设置页 + 任务中心上层覆层（`mw_check overlay-live`） | PASS：设置页开着点任务按钮叠成提示层、再点回到原页；打开设置 9 ms | — |
| 大列表分帧建行与窗口化（1000 个 jar） | 首帧只建 80 行，1000 行 362 ms 建完；窗口化后首行 141 ms、滚到底 961 ms、末行控件 1、顶行回收 1 | 滚动条 `valueChanged` 连接曾被并进注释行（实际没接上）；分帧完成后补一次 `ensureRowWidgetWindow()`，否则滚到底后新建的顶部行盖过回收结果 |
| 目录页窗口化（500 个存档） | PASS：首行 96 ms、滚到底 912 ms；已算目录大小/时间进 `m_statCache`，滚回不重复递归 | — |

**待复核（环境限制）**：
- `assoc-live`：当前会话写 `HKCU\Software\Classes\WTFMLSelfTest` 被拒（错误 5），需正常桌面会话复跑；
- `sevenzip-live`：本机无 `7z.exe`，未跑；
- `java-runtime-live`：Java 17 运行时下载探针约 20 MB 后超 25 分钟无进展，已终止，不算通过；
- 真机 GUI 的拖拽白块、任务中心关闭动画等需人工目视。

## 2026-11-10　Agent 功能人工验收（Stage 1A / 1B）

设计与实现见 [AGENT.md](../features/AGENT.md)、[AGENT_STAGE1A.md](../features/AGENT_STAGE1A.md)、[AGENT_STAGE1B.md](../features/AGENT_STAGE1B.md)。测试 provider 为 DeepSeek `deepseek-flash`，用专用可撤销 key；分支 `agent-development`。

**G1A（无工具聊天）：除网络错误外通过。**
- 范围：启动与导航（五颗顶部导航、900×620）、API/URL 校验（拒绝远程明文 HTTP、userinfo/查询串/片段，本地 HTTP 需显式勾选）、凭据遮挡与 Windows 凭据管理器存储（`WTFML/Agent/<profileId>`）、聊天生命周期（停止保留部分内容标"已中断"、切会话/切页取消在飞请求、关窗有界退出、删除会话先取消）、会话存储与 DPAPI 加密（`%LOCALAPPDATA%\WTFML\agent\sessions\`，加密失败保留明文、解密失败绝不回退明文）、无文件工具/无 MCP。日志、会话、错误卡片、导出里没有明文 key。
- 提交区分：严格 G1A 只能在 Stage 1A 提交 `27070e5` 单独构建上打勾；当前 HEAD 含 1B 文件工具，"无工具/无 MCP"一组须在 `27070e5` 构建上验或并入 G1B。
- 审查期间的修复：加密开关从右上角挤占位置移到聊天头部下方单独一行，显示"加密本会话（DPAPI）"与"当前会话：明文 JSON / 已加密（DPAPI）"；工具 function name 不允许点号（线上名改 `fs_read` 等）；流式请求非 2xx 时错误 JSON 不再被丢弃。
- 判定口径：P0（明文 key 落盘/进日志、凭据降级明文、URL 校验被绕过、加密失败回退明文、崩溃/数据丢失）与 P1（停止/取消无效、会话恢复失败、错误不可见、无配置仍可发送、切页串会话）为 FAIL；仅 P2/P3 可 CONDITIONAL。

**G1B（文件工具）：首轮人工未通过，P0 未清。**

| 组 | 结果 |
| --- | --- |
| L1–L3、L5–L9（实例锁定、`fs.list` 不含文件名、`fs.listNames` 审批且无会话放行、严格模式） | PASS；L4（实例芯片切换确认）用户复核通过 |
| R1–R3 配置读取 | 流程 PASS，**脱敏 FAIL** |
| R4 凭据文件 | **FAIL/需复测** |
| R5 jar | PASS（`egress_blocked`） |
| R6 大文本 | PASS（1 GiB 文件 `truncated=true`，返回 modelArtifact id，artifact 只存本次读取的 256 KB～1 MB） |
| R7 变体字段名脱敏 | **FAIL** |
| R8/R9、W1–W3、P1–P3、E1–E5 | PASS |
| W4/W5、A1、P4、P5 | 未测/延后（见下） |

- **B1（P0）脱敏只认精确键名**：`hypixel_api_key` `redactionHits=0`；`DB_PASSWORD`/`REDIS_PASSWORD`/`API_SECRET`/`JWT_SECRET` 未命中；精确 `password`、`API_KEY` 命中。修复方向：键名 token 化后含 password/secret/token/api-key/credential 即脱敏，保留格式检测，补变体测试，不能只把实测名字加白名单。
- **B2（P0）凭据文件识别过窄**：硬阻断仅限文件名恰为 `.env`、`credentials*`、`token*`、`secret*`、`auth.*`、`*.pem/key/p12/pfx/jks`；本次 R4 用的文件名可能是 `env`/`test.env`，进了普通审批。需把文件名改为精确 `.env` 复测，并把 `*.env`、`*.env.*`、`*credential*`、`*secret*`、`*token*`、`*apikey*` 一并按凭据文件处理。
- **B3（P1）"截断"不等于安全**：脱敏未命中时 artifact 仍保存本次读到的内容并可被 `artifact.read` 分段取回；工具文案"完整内容见 artifact"应改为"本次读取内容见 artifact"并说明同样经过 D11/脱敏。
- **B4（P2，Stage 2 前处理）**：自动读取的日志含 Windows 用户名、启动参数回显、本机绝对路径、硬件指纹（显卡＋驱动、音频设备名、SDL 版本）；Stage 2 日志/诊断工具须按字段白名单投影，不整段送模型。
- 用户复核勾选（边界、流程、UI 行为，不含脱敏覆盖）：写入 diff/备份/原子性/哈希与拒绝后字节不变、artifact 只含脱敏内容且本地原始存档不可读、D11 边界与严格模式、活动会话删除、真实 tool_calls 流程、无 MCP、会话锁定后工具不接受模型传入实例 id、grep key/敏感头为空。
- 敏感值处理：测试 `config.toml` 含真实 `hypixel_api_key`，不要分享/上报；若曾离开本机，去 Hypixel 重置。

**G1B 修复后须复测**：R1–R4、R6、R7（脱敏/凭据边界）。未测项怎么补：
- W4/W5：`sha256` 不在 `.toml` 里，看写/读工具卡展开 JSON 的 `sha256`、`fileIdentityToken`、`bytesBefore/bytesAfter`，以及 `%LOCALAPPDATA%\WTFML\agent\backups\maintenance\<backupId>\meta.json`（`owner=agent`、`source=agent-session`、`sourceSessionId`、`sha256`、`originalPaths`），与 `Get-FileHash` 对比。
- A1：先读 `big.txt` 拿 `modelArtifact` id，再让模型 `artifact.read` 带 `offset/length`，确认仍是脱敏内容。
- P4：重启后打开有未完成审批的会话，旧审批不自动执行，提示"已作废，需重新请求"。
- P5：提交阶段指"重新核对旧身份/哈希 → 写前备份 → 原子替换 → 写后回读"；小文件瞬间完成，主要靠自动化测试，人工可标 N/A。
- 实例芯片：智能体页右上、模型下拉左边，文字"跟随主页：A / 已锁定：A / 已锁定：A（主页当前：B）"。

**待网络环境复核（延后，未执行，不算通过）**：用本机 loopback 兼容服务（设置页勾"允许本地明文 HTTP"）返回 400/429/500/重定向/超时/断流，本地自签名 HTTPS 验证证书错误，真实 provider 只测 401/403 和断网（不为 429 刷额度），抓包确认 URL 不含 key、key 只在认证头（抓包文件不提交）。检查项：
- [ ] 401/403：不自动重试，提示检查 key/权限，能跳设置；
- [ ] 429：尊重 `Retry-After`（上限 60 秒），最多 2 次，显示等待；
- [ ] 首字节前断网：重试 2 次后显示网络错误；
- [ ] 流开始后断网：保留已收内容，标"已中断"，不自动重发；
- [ ] 3xx 不跟随，key 不发到新 host；
- [ ] TLS 证书错误直接失败，无"继续"；
- [ ] 模型 API 遵循 Qt/系统代理；
- [ ] 400 等显示供应商 JSON 的安全摘要，不只"HTTP 400"；
- [ ] 错误卡片/日志/抓包无 API Key、Authorization 头、请求 body 原文。

现有实现：错误分类 `Unauthorized`/`RateLimited`/`Quota`/`Network`/`Protocol`/`Cancelled`；401/403/配额不重试，首字节前网络/429/5xx 最多 2 次。

## 2026-11-11　UI 7a 外壳与主页（设计稿对照）

`tools/acceptance_desktop.ps1 -DesignShots` 按设计稿尺寸截主页三主题、900×620、1600×1000、折叠轨与组件总表（1:1 像素，隔离夹具），与设计稿 PNG 并排目视。ui_check 3202/3202、ui-refresh 0 失败；ctest theme/ui/ui-agent/ui-bootstrap/ui-refresh/main-window/main-window-agent/workspace/install-layout 串行通过。首轮发现：首张截图被启动后的主页刷新清空（夹具抓图前重灌）；轨底任务卡被压成 22 高（按钮最小尺寸改跟随内部布局）；换主题后任务标题字色停在旧主题。

## 2026-11-11　UI 7a 合入前小修（设计稿对照）

内置默认皮肤替换黑色问号剪影，落地影改用令牌；轨底运行状态平时收起；输入框 / 下拉框 / 数字框高 36；可复用列表行委托（悬停快捷操作、选中淡底 + 3px 指示条）；下载页最小宽 826，轨折起后整窗最小 894；主页实例封面按名字配色方块。重截主页三主题、900×620 与组件总表并排对照。ui_check 3207/3207、mw_check 2784/2784，ctest 同上九项串行。

## 2026-11-11　UI 7c 设置、任务详情、账号、游戏日志与对话框（设计稿对照）

`tools/acceptance_desktop.ps1 -PageShots` 截设置、活动中心、任务详情（运行中 / 失败 / 崩溃）、账号、游戏日志（运行中 / 筛空 / 已退出）、五种对话框与管理 / 下载 / 智能体整窗衔接图，深色全看、翡翠与海洋抽查，与设计稿 PNG 并排目视。首轮发现：弹窗背景发黑（整页快照带了不透明窗口底色，遮罩改为令牌色 × 0.6 并补遮罩外沿）；整页覆盖层下导航轨和顶栏仍是清晰风景（主页场景被盖住时整窗改回磨砂）；活动中心嵌在覆盖层里时自己铺了不透明底。ui_check 3207/3207、mw_check 2784/2784 串行通过。

## 2026-11-11　UI 7e 活动中心浮动面板、设置页索引与搜索、菜单与提示卡（设计稿对照）

`-PageShots` 重截活动中心浮动面板、设置、账号、游戏日志与对话框，深色 / 翡翠 / 海洋并排对照设计稿。首轮发现：面板开着时重建，新行要等下一拍才 show，高度塌成只剩标题行（改为当场 show，ui_check 加回归）；截图脚本拍完浮动面板没收，后面各张都叠着它；账号页「刷新外观」被拉成整条、图标停在上一套主题的字色；设置小节索引挂在整列分类末尾（改为当前分类行让出一截摆在下面）。换主题后 chip 样式表不跟（U32.4）的根因是菜单样式用了令牌色，破坏「应用样式表与主题无关」，改为 palette() 引用并加检查。合入 main 后 ui_check 3187/3187、mw_check 2784/2784 串行通过；ui-refresh 主页启动卡 7 项在 main 上同样失败，非本轮引入。

## 2026-11-11　UI 精修第一轮（减字、扁平、行名定宽）

按「用户 11-11 补充原则」逐页过：下载页刷新改无框图标钮、下载源钮只留短标签；智能体会话分「固定 / 今天 / 更早」、发送 / 停止改方形图标钮、模式改 chip；实例启动设置改左右分行（行名定宽 96）；工具箱纹饰搭配补统一表头、五页说明收短；设置·启动内存说明压一行；账号分区标题收短；三处列表搜索框加放大镜。`-DesignShots` / `-PageShots` 三主题目视。首轮发现：数字框右侧上下钮在翡翠 / 海洋下是一条不透明白柱（Fusion 用 `setHsv` 丢了按钮色的透明度，代理样式里先混成不透明色）；内存说明换成截尾标签后整句的宽度把左边行名挤没（封顶 420）；账号列加最小宽度会把整个对话框最小宽度顶过 860 的窄屏阈值（改用伸缩比）。ui_check 3187/3187、ui-refresh / ui-agent 等 7 个界面模块、mw_check 2784/2784 串行通过；install-layout、lifecycle 在 main 35d3c69 上同样失败，非本轮引入。

## 2026-11-12　窗口基础（拖动、高速下载窗二次打开、API 设置）

真机（Win11 26200、125% 缩放）起一个实例，鼠标事件以消息级为主，拖动 / 双击各用一次真实输入。

| 项 | 结果 | 首轮失败 / 意外发现 |
| --- | --- | --- |
| 窗口哪都拖不动 | 用户实例上最顶层窗口下盖着一块覆盖整个客户区的原生子窗口（`WindowFromPoint` 点中的是子 HWND，不是主窗口），HTCAPTION 到不了顶层窗。来路：`ShellOpenWith::openWithDialog` 是代码里唯一对子控件调 `winId()` 的地方，Qt 会把它和所有祖先都变成原生窗口（独立小程序实测）。改成取顶层窗句柄 | 新起的实例拖得动；用户实例里那块子窗口是否来自「打开方式」没有直接证据（那个实例已在用） |
| 空白处拖动、双击 | 顶部条、导航轨、主页内容区空白各拖一次到位；双击空白最大化、再双击还原 | — |
| 高速下载窗第二次打开 | 第二次打开前窗口高度比第一次多 38 px（系统标题栏回来），改后两次都是 768×580 | 关窗再开时 Show 事件先于新平台窗口，`winId()` 还是旧 HWND（`GetWindowLong` 全 0）；`applyFrame()` 贴 customMargins 落在已销毁的句柄上 |
| 高速下载窗背景 | 亚克力档下窗口里只写着色层，真屏截图透出桌面，和主窗口同一档 | 之前恒铺那张磨砂风景 |
| 智能体页「API 设置」 | 覆盖层被别的选项卡上收着的页占着时 `showPage()` 返回 false，按钮无事发生；改成静默收掉再开 | 新实例里点它是好的 |
| `ui_check_window_chrome`（windows 平台插件，窗口摆屏外不激活） | 68 项通过：主窗口五页与 7 扇独立窗口的空白判定、顶部条 WM_NCHITTEST、原生子窗口、反复关闭再打开的标题栏 | 测试里复现不了第二次打开带标题栏（和产品的显示时序不同），该项靠真机脚本把关 |

## 未覆盖

| 项 | 状态 |
| --- | --- |
| 真实微软账号/Azure 审核、正版全链 | 暂缓，需用户配合 |
| 断电、设备物理损坏/断连、杀毒组合、所有 I/O 位置的故障笛卡尔积 | 不作发布 P0，未验 |
| 物理慢盘上的大实例脚本（含阶段计时和外部终止） | 仅 128 MiB 规模在 USB HDD 跑过；1 GiB 在 NVMe |
| 多显示器、读屏、减少动态效果、100%/125% 复杂场景、干净 Windows/GPU 矩阵 | 未验 |
| 公网玩家加入服务端、所有整合包/加载器/模组组合 | 未验 |
| `assoc-live`、`sevenzip-live`、`java-runtime-live`、网络错误矩阵 | 见上，待环境 |

## 复现命令

```powershell
# 大实例升级（外部终止/恢复/重试；--payload-mb 1024 --files 6000 为 1 GiB 规模）
python tools/acceptance_large_instance.py --verify-exe build/<dir>/tests/verify_instances.exe --qt-bin D:/Qt/Qt/6.10.1/mingw_64/bin `
  --work-dir build/<新目录> --payload-mb 1024 --files 6000 --kill-stage publish --json-out build/<新目录>/large.json

# P0：事务与权限（测试目录须全新；权限脚本只对自己新建目录加/移当前 SID 的 deny，不重置整张 ACL）
python tools/acceptance_p0_transactions.py --exe <verify_instances.exe> --qt-bin <Qt bin> --work-dir build/<新目录>
python tools/acceptance_p0_permissions.py  --exe <verify_instances.exe> --qt-bin <Qt bin> --work-dir build/<新目录>
python tools/acceptance_p0_final.py        # 一次跑完整批，总预算默认 840 秒、上限 900 秒

# 满盘：专用 256 MiB VHD 由用户手动在管理员 PowerShell 创建/卸载；脚本只在该卷（卷标 WTFML_P0_ONLY）制造满盘
pwsh -NoProfile -File tools/prepare-p0-vhd.ps1 -Action Create   # 或 -Action Detach
python tools/acceptance_p0_full_volume.py --exe <verify_instances.exe> --qt-bin <Qt bin> --timeout 240 [--mid-write-only | --zip-write-only]

# 下载恢复、桌面 DPI、目录边界
pwsh tools/acceptance_download_recovery.ps1 -Root <全新绝对目录>
pwsh -NoProfile -File tools/acceptance_desktop.ps1 -Root build/<新目录>
python tools/acceptance_directory_boundary.py --exe <WTFMinecraftLauncher.exe> --root build/<新目录> --qt-bin <Qt bin>
```

注意：不要对真实游戏目录执行权限重置；失败现场保留，不自动清理。VHD 卸载后只能用 `Mount-DiskImage -ImagePath <既有镜像>` 重新挂载，不能再次 Create。服务端验收工具与安全边界见 [tests/acceptance/server_boundary/README.md](../../tests/acceptance/server_boundary/README.md)。

## 2026-11-12　控件层第一批（数字框 / 焦点 F1 / 去小箭头）

| 项 | 结果 | 首轮失败 / 意外发现 |
| --- | --- | --- |
| 数字框统一（全仓 25 处构造点）+ 无上下箭头 + 单位在框内 | 应用级 QSS 一处接管：`QLineEdit, QComboBox, QAbstractSpinBox` 同一盒模型（3px 透明边 + 1px padding），`::up-button/::down-button` 宽度 0；滚轮 / ↑↓ / PgUp / PgDn 照旧 | 数字框比输入框高 3px：`QStyleSheetStyle` 给 `CT_SpinBox` 在盒模型之外多算一圈框，内容盒再减 3px 才落成 36 |
| 焦点态 F1 + 去小箭头 | 底部 2px 强调线整条删掉；键盘 Tab / Backtab 到达由 `ControlFocusVisibility` 挂 `wtfmlKeyboardFocus`，QSS 加 3px 淡环（`palette(highlight)`）；下拉 `::drop-down` 与 `QToolButton::menu-indicator` 宽度 0，`ComboChevronPainter` 删除；折叠行改整行按钮 + 右侧「展开 / 收起」 | `QComboBox::drop-down` 宽度归零后，自绘 ∨ 的过滤器若留着会继续画 —— 类与安装点一起删 |
| ui_check（offscreen） | 3229 条通过 / 0 失败 | W13 三组旧断言（∨ 形状、底部边线、箭头禁用色）按新规格重写；下拉框那格用空条目量，否则「跟随全局」四个字伸进右带造成假红 |
| 数字框越界键入钳到边界 | 0–5 的框里打 99、焦点一走就落到 5（Qt 出厂只会退回旧值） | **焦点转移里直接 `setValue()` 会把页面在别人的 FocusOut 事件里拆掉**：mw_check 当场段错误（服务端设置页那几颗表内编辑框）。改成只读文本算值、`QTimer::singleShot` 带控件当上下文再落值，复跑 2784/2784 全过 |
| mw_check（offscreen） | 2784 条通过 / 0 失败 | ui_check 首跑有一条 `I2b Esc 收起浮动面板` 偶发失败（offscreen 时序），同一二进制复跑 3229/3229 全过 |

## 2026-11-12　控件层第二批（tooltip / 下拉弹层 / 浮层磨砂）

| 项 | 结果 | 首轮失败 / 意外发现 |
| --- | --- | --- |
| tooltip 换成自绘那一条 | 停 500ms 出、1 秒内换控件立即出、120ms 淡入上移 2px；文字按钮按 `text()` 判、不出提示；条目仍用 `ToolTipRole` | 系统那条 QToolTip 一律被 `QEvent::ToolTip` 吃掉，不再出现黄底方框 |
| 下拉弹层（全仓 67 处 QComboBox） | 应用级一个过滤器接管：行高 34、圆角 8、选中强调淡底、8 项限高、S2 3px 细条 600ms 淡出、超过 15 项顶上搜索框 | ① `QComboBox::setMaxVisibleItems()` 在 Fusion + 样式表这一支上**不管用**（45 项照样整屏，正是用户报的），限高得自己按 8 项把弹层窗口收回；② Qt 自带上下翻页箭头（`QComboBoxPrivateScroller`）每次弹出会重新露出来，得在 show 的下一拍再藏一次 |
| 浮层磨砂统一 | 下拉 / 菜单 / tooltip / 工具箱与账号面板 / 实例设置两个浮层都走 `Popups::paintSurface()` + `glassify()`（Win11 瞬态亚克力） | `Glass.cpp` / `WindowChrome.cpp` 一行没动（DSH 正在改失焦磨砂），浮层外壳在新文件 `ui/Popups.cpp` |
| ui_check_popups（新增模块，offscreen） | 37 条通过 / 0 失败 | 验收图 `grab()` 拍不到 DWM 合成那层，图里看到的是自绘着色玻璃 |
| ui_check / mw_check（offscreen，合入主页后） | ui_check 3238 条、mw_check 2783 条，全过 | ccache 的 `CCACHE_BASEDIR` 会把 `__FILE__` 改写成相对路径 —— 三个「扫源码」的断言靠 `__FILE__` 推仓库根，结果根算成了构建目录（`TL1 扫到了…0`）。改成 CMake 传绝对路径 `WTFML_REPO_ROOT` |
| 浮层外壳改成复用主页的 `GlassPopover` | 面（圆角 + 玻璃 + 顶上 1px 高光）从 `GlassPopover::paintSurface()` 抽出，窗口里卡片、下拉弹层、菜单、tooltip 共用；圆角 14 / 淡入 150ms 也从它取 | 下拉弹层与菜单是 Qt 自己的顶层弹出窗口，装不进窗口里 —— 共用的是**画法**，不是那个 QFrame；顶层那份仍自己挂 DWM 瞬态亚克力 |
| 设置页重排（顶部 4 页签、单栏 720、分组淡底、行 < 600 叠放、失焦即存） | ui_check 3246 条通过 / 0 失败（含 900×620 与 1280×800 每个页签无横向溢出、叠放、失焦即存、「已保存」淡出）；mw_check 2783/2783；`ui_check_window_chrome` 23、`ui_check_ui_refresh` 129、`ui_check_theme` 94；`wtfml.core*` 等 18 项与其余 ctest 套件全过 | `mw_check` 首跑 `T20` 服务器收藏失败后 `tree->setCurrentItem` 崩：测试取了 overlay 里最早那扇 `InstanceSettingsDialog`（上一扇还在等 deleteLater），改取最后一扇；另修两处拆窗时的悬空指针（标题栏 hero 联动 lambda 捕获 `this`、服务端版本树 `invalidate` 读已析构的 `QStackedLayout`） |
| 工具箱改成目录页 → 工具页（第六选项卡，去掉弹出框，高速下载并入） | ui_check 3246 条、ui_check_toolbox_layout 460 条（十个工具页在 900×620 / 1280×800 / 内容宽 560·700·836·1216 下无横向滚动范围、无被裁控件；附魔四框同高 36、内容宽 < 640 折两行）、toolbox-polish 55、armor-trim 45、ui-refresh 129、ui-agent 41、window-chrome 62（去掉 6 条高速下载窗断言）、mw_check 2783、tool-features 77、high-speed-download 340，全过 | 面包屑负边距在 Qt 里不是「向左偏」而是「用样式默认值」，页头改成两侧各多 8px 对齐内容；GutterScrollArea 右侧常留一条滚动条位，页头也要让出来，否则宽窗口下居中差 5px |
| 去选中竖条、去右侧小箭头、工具箱字阶收敛、设置页分段页签去外框、换页不再垫不透明底 | ui_check 3243/3243（新增：整窗磨砂上换页幕布空白处 alpha 保持 80、无不透明像素；设置页分段页签鼠标后无环、键盘后有环），theme、ui-refresh、install-layout、toolbox-polish、toolbox-layout、window-chrome、main-window（mw_check）、main-window-agent 全过；离屏验收图 18 张（6 页 + 下载树展开 + 实例设置 × 暗 / 青瓷） | 换页闪实心的根因：幕布先铺 `QPalette::Base` 再贴一张 `grab()` 的 QPixmap，而 `Glass::Backdrop` 往 QPixmap 里画时按不透明风景底画（DWM 层拍不进位图）；改成照进 QImage、底板用 Source 连 alpha 写回。分段页签那圈框是鼠标点页签后 Qt 把焦点从藏起来的那页挪过来（理由也是 TabFocusReason），自绘环只判 hasFocus；离屏下 DWM 不存在，换页关键帧前后对比拍不出差别，只能靠像素断言钉住 |
| 账号入口回主页右上（圆头像 + 名字 + 小状态胶囊，淡底无描边）、删导航轨账号格、去零实例英文副题 | ui_check 3260/3260（新增：胶囊在主页右栏、轨上无账号格、高 44、未登录写「登录」、1280×800 / 900×620 下整颗在窗里且不压拖动条不挤标题、四套预设下名字在花背景上 ≥ 4.5:1、弹层从胶囊下 8 弹出右对齐）；mw_check 2787/2787（新增：离线 Alex 胶囊写「离线」、切到 Carol 写「微软账号」、全删账号后写「登录」且弹层「未登录」）；ui-refresh 128、theme 109、window-chrome 62、main-window-agent、toolbox-polish、toolbox-layout、install-layout 全过；离屏样张 24 张（已登录 / 弹层开 / 未登录 / 未登录弹层开 × 暗 · 青瓷 · 海蓝 × 两档）+ 零实例 4 张 | 设计验收程序的 `--root` 必须是**不存在**的目录（存在就直接退 2）；海蓝 / 青瓷在亮天空上那层浅色淡底几乎看不出（深字压浅底，对比度本来就够），暗色主题上底才明显；退出登录后弹层仍列着留在列表里的账号（只有一个账号都没有时才是「未登录」+「登录」），这是现有弹层行为，没动 |
| 工具箱信标 / 彩色文本 / 旗帜三页重排为左右两栏（最小窗口不出纵向滚动条） | toolbox-layout 466 条（新增三页 900×620 无纵向滚动范围；窄宽检查附最小宽数字）、quick 套件 16/16、wtfml.ui、main-window 全过；离屏样张 12 张（三页 × 暗 · 青瓷 × 两档）| 彩色文本并排的最小宽约 690，翻成上下叠放的阈值设为 720；旗帜结果的步骤框在 6 层时仍会在框内滚动（框内限高，页面不滚）；光柱逐层文字改由清单色点表达，说明里只留汇总，复制仍拿整份 |
| 主页风景 ↔ 其他页磨砂：风景层约 250ms 淡出 / 淡入（导航轨不动），换页幕布在淡变期间每拍重拍底板 | window-chrome 92/0（新增：主页→设置中途 0<α<1 且单调变小、终值恰为 0；设置→主页单调变大、终值恰为 1；淡完撤掉「正在淡」窗口标记；动效关着换页当下直接 0 / 1）、ui_check 3260/3260、main-window 2787/2787、main-window-agent 36/0、ui-refresh 128、theme 109、install-layout 80、toolbox-polish 55、toolbox-layout 466；离屏样张 40 张（暗 / 翡翠 / 海蓝 / 跟随系统 × 900×620、1280×800 × 主页→出主页中途→设置→回主页中途→主页），中途帧平均色都落在两端之间 | 首轮 window-chrome 失败：该用例前面把主窗口隐藏再显示过，摆在屏外的窗口重显后 Windows 不送 WM_PAINT，Backdrop 按「还没上屏」直接跳终值（这是给启动 / 从托盘回来留的）；用例里先画一帧再量。亚克力 / 云母真机上的中途帧（风景半透盖在 DWM 材质上）离屏拍不到 |
2026-11-13｜打磨 B/C/D：实例设置页眉去掉重复小标题、一级页标题统一落点（设置/工具箱窄栏改贴左）、主按钮左右内边距 20 且最小宽 88｜相关套件 ui/ui-refresh/theme/toolbox-layout/install-layout/toolbox-polish/main-window 全过，src 零告警｜智能体页无页标题，未纳入对齐
2026-11-13｜整页开合（账号与外观、实例设置等）并入换页过渡：覆盖层铺底带上正在淡的主页风景、开页 / 返回都走 pageAppear；导航轨从整页切走不再硬切；整页上弹提示时风景不回来、不透出主页，半路的幕布先收到终点｜真机 1280×800 录帧：修前开页一帧内硬切成磨砂、弹「登录失败」时主页整块透上来，修后开页 / 返回约 250ms 渐变 + 内容淡入上抬，与导航轨一致；window-chrome 100/0（新增 6b 整页过渡段）、ui、ui-refresh、main-window、core-accounts 全过，src 零告警｜首轮 6b 用 255 比提示底的不透明度失败：用例里背板是亚克力 / 云母档，底本来就半透，改为和同一页没弹提示时比
