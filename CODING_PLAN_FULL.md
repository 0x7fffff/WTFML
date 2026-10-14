# WTFML 完整版 Coding Plan

> 更新时间：2026-10-13
>
> 本文是唯一主计划，已合并原“完整版 Coding Plan”和 UI/实例预设计划。
> 完成状态以当前源码为准；极速版以后从完整版裁剪，不在当前阶段维护第二套实现。
> `NEXT_TBD.md` 仅作为评审备忘；其中已确认的建议已经吸收到本文，后续不单独维护第二套进度。

## 1. 当前结论与状态

WTFML 的启动器主路径，以及此前规划的任务中心、修复、备份、主页最近活动和服务端管理主体
已经落地。接下来的重点是稳定现有功能、通过截图收口 UI、实现实例预设，再补全更新和发布能力。

- `[x]` 已在当前源码中实现；
- `[~]` 已有主体，仍需收尾或扩大覆盖；
- `[ ]` 尚未实现。

### 1.1 已实现基线

- [x] 原版及 Fabric / Forge / NeoForge / OptiFine 安装与启动；
- [x] 微软登录、离线账号、皮肤与披风；
- [x] 多游戏目录、实例分组、复制、改名、删除、导入与导出；
- [x] 模组、资源包、数据包、光影、存档、服务器列表、原理图和配置文件管理；
- [x] Java 检测、下载、全局与实例启动参数；
- [x] 世界和服务器 Quick Play；
- [x] 截图、Replay、日志、崩溃报告等常用目录入口；
- [x] `TaskManager`、任务历史、底栏摘要、任务中心和任务详情；
- [x] 实例完整性报告与补全；
- [x] 游戏异常退出记录、日志与错误报告导出；
- [x] 世界、实例及服务端世界备份和恢复；
- [x] 上一次游玩卡片、启动结果记录和继续世界/服务器；
- [x] 独立服务端安装、运行、控制台、设置、检查和备份；
- [~] 取消、重试已有入口和部分实现，仍需逐任务检查真实安全点；
- [~] 白名单、OP、封禁、插件/模组更新等服务端日常管理仍可完善。

### 1.2 UI 工具基线

- [x] `tools/ui_modernize.py`：audit / apply / verify；
- [x] `baseline` 与 `modern_soft` 配置档；
- [x] 白名单替换、幂等校验和参数关系校验；
- [x] 多文件暂存写入和失败恢复；
- [x] 验证源码是否真正应用指定 profile；
- [ ] 固定的多页面、多 DPI 截图基线；
- [ ] 统一自绘任务进度组件替换剩余原生 `QProgressBar`。

### 1.3 2026-10-13 实现快照

以下项目已经进入源码和测试，后续计划不得再把它们当作“从零开始”：

- [x] 已有首个领域 controller：`JavaRuntimeController`；Java 探测与运行时下载不再由
  `MainWindow` 直接执行；
- [x] 顶部导航以 `navigationApplied(source, target, reselected)` 显式传递来路和语义，
  不再依赖两个 Qt 槽的连接先后顺序；
- [x] Fabric API 仅在前置展示列表中隐藏，真实依赖解析和一键安装仍保留它；
- [x] 下载并发预设 6 / 12 / 24 / 48 已接入设置、持久化和下载队列，调大后立即补满可用槽位；
- [x] OAuth Client ID 与回调地址支持 CMake 注入，监听端口由回调地址统一推导；
- [x] UI 现代化脚本、`baseline` / `modern_soft` profile 和使用文档已入库；
- [x] 核心构建通过；真实 Windows 环境主窗口集成测试 `2518/2518` 通过；
- [~] UI 验证当前为 `2877/2884`；剩余 7 条必须结合现代化 profile 的新截图判断是更新
  几何基线还是修正布局，不能直接删除断言，也不能为了旧像素值撤销新风格。

## 2. 架构原则

### 2.1 不继续扩大 MainWindow

`MainWindow` 只负责编排。新功能的模型、文件格式、兼容判据和事务逻辑下沉到对应模块。
UI 不直接 include 业务对象，通过显示 DTO 和信号交互。

### 2.2 一个用户意图对应一个任务

安装实例、修复实例、应用预设和备份世界分别是一项主任务。内部下载、校验和写文件是步骤，
不要给数千个文件各创建一项任务。

### 2.3 不编造进度与兼容性

- 未知总量显示不确定进度和阶段；
- 不能确认兼容的配置标记为未知，不静默强写；
- 只有执行层真正支持取消时才显示取消；
- 所有覆盖和跳过都必须在预览或结果中可解释。

### 2.4 文件操作默认可恢复

- 重要 JSON 使用 `QSaveFile`；
- 覆盖用户文件前创建备份或回滚点；
- 临时文件校验后再替换成品；
- 规范化后的路径必须仍位于授权根目录；
- 游戏运行期间禁止修改该实例的游戏配置。

### 2.5 已确认的架构债务与边界

以下问题已经影响新功能的安全落地，但不做一次性重写：

- `[~]` 顶部导航不再依赖 Qt 信号连接顺序；已改为显式的
  `navigationApplied(source, target, reselected)` 事件；这只完成了去除隐式时序，尚未完成
  路由栈和非阻塞导航；
- `[ ]` 以非阻塞 `NavigationController` 取代 `OverlayHost::execPage()` 的嵌套事件循环；
- `[ ]` 四个根选项卡各自保持路由栈；返回只 `pop`，重点当前项只 `popToRoot`；
- `[ ]` 删除隐藏占位页、`runWhenOverlayFree()`、导航用 `singleShot(0)` 和手工来路变量；
- `[ ]` 后台任务完成只产生通知/活动中心条目，不驱逐当前页或自动跳转；
- `[ ]` 将 `MainWindow` 按实例、安装、资源、启动、账号、服务端、维护和活动中心
  逐个抽出 controller；`JavaRuntimeController` 是已经完成的第一步，不纳入这八个待拆业务域
  重复计算；
- `[ ]` 将 `VersionManager` 里的原版安装、加载器安装、整合包导入和修复拆为独立 Job；
- `[ ]` 将 `wtfml.meta` 收口为带 `schemaVersion` 的 `InstanceMetadataStore`，不再向全项目暴露
  任意 `QJsonObject` 补丁；
- `[ ]` 将测试按模块拆分，保留现有回归断言，不边拆业务边重写行为。

导航栈、`MainWindow` 拆分、安装 Job 和多批次下载调度都属于大型迁移。必须逐步
双轨替换，禁止为了减少文件行数一次性切断已有信号链。

Controller 的下一拆不预先锁死为“实例域”。每次从满足以下条件的域中选择：边界已经能用
输入/输出 DTO 说清、可用现有测试保护、能在一个提交内完成双轨接入。实例域优先级较高，
但如果导航或活动中心先形成更小、更安全的闭环，可以先做；验收看业务决策是否迁出，
不看 `MainWindow` 减少了多少行。

### 2.6 实例身份与命名模型

将一个实例拆成四类数据：

```cpp
struct InstanceDescriptor {
    QString instanceId;     // 稳定 UUID，用于关联、任务、分组和历史
    QString storageId;      // 目录 / json / jar 使用的技术 ID
    QString title;          // 用户输入的显示名，不自动拼加技术信息
    GameDescriptor game;    // Minecraft 版本、加载器和加载器版本
};
```

- `[ ]` 实例列表改为“主标题 + 技术副标题”；
- `[ ]` 普通重命名只改 `title`，不改目录、jar 和 json；
- `[ ]` 磁盘 ID 重命名移入高级操作，保留当前回滚和运行中守卫；
- `[ ]` 跨刷新、跨改名的状态逐步改用 `instanceId`，磁盘边界才使用 `storageId`；
- `[ ]` 用 `Compact / Standard / Detailed` 语义档位代替四个布尔开关的自由组合；
- `[ ]` 旧 `name` / `realVersion` / `loaderName` / `loaderVersion` 原样读取，不猜测剥离
  `-Forge` 等后缀。

### 2.7 活动中心定案

底栏中间区点击后打开完整活动中心，采用三列逻辑区：

```text
可收起的运行中实例 | 按时间和状态排列的横向任务 | 上下文详情/摘要
```

- `[x]` 左列显示运行中实例，支持用按钮收起；复用底栏已有的停止、设置和日志动作，
  不复制实例管理全部功能；
- `[x]` 中列使用 Steam 式有顺序的横行，展示账本已有的阶段、进度、速度与待处理状态；
  ETA 等执行器尚未提供的数据不在 UI 中猜测；
- `[x]` 右列选中任务时显示步骤、日志、错误与操作；未选中时显示“进行中 /
  已完成 / 需要处理”摘要；
- `[~]` 窄窗口已自动收起左右列，并保留现有内嵌任务详情页作为降级路径；后续可改成
  当前面板内的详情抽屉；
- `[ ]` 列宽动画 180–220 ms，尊重减少动态效果，不动画文字缩放和表格列宽；
- `[~]` 任务操作已按 capability 提供取消、重试、打开目标、日志、错误报告和崩溃目录；
  “完成后启动实例”尚待任务模型提供目标实例 capability，不按任务类型在 UI 现猜按钮；
- `[x]` 用户列表不放进右列，账号仍保留在底栏右侧与账号页。

第一阶段仍可以一个下载批次对应一个任务；真正多批次并发与排队等待下载调度器重构。

## 3. 当前最高优先级：稳定与 UI 收口

### 3.1 回归现有功能

- [~] 任务 capability 和重试入口已有主体，继续逐任务核对真实取消安全点；
- [x] 已覆盖关闭启动器时 worker、游戏进程和在途下载的收尾；
- [ ] 对备份恢复增加损坏包、磁盘不足和中断场景；
- [x] 服务端停止优先发送 `stop`，宽限超时后才允许强制结束；
- [x] 主页、服务端、备份和配置编辑已有主窗口/UI 回归覆盖；
- [ ] 每收口一个页面同步检查键盘导航、焦点可见性和减少动态效果，不把
  可访问性集中拖到发布前返工。

### 3.2 UI 截图循环

固定截图：主页、实例管理、下载、设置、任务中心、任务详情、服务端列表/控制台/设置、窄窗口、
浅色与深色主题。至少检查 100%、125%、150%、200% DPI。

重点观察：

- 页面和卡片内边距；
- 按钮高度、圆角和主次关系；特别检查“可操作但看起来像禁用”的灰色按钮；
- 列表密度、空状态和长文本；
- 弹出层、下拉框和动画幕布背景；底栏左右两颗按钮展开的面板必须纳入固定截图；
- 4 px 任务进度条在高 DPI 下是否清晰；
- 动画前、中、后是否闪灰或改变底色。

```powershell
python tools/ui_modernize.py audit
python tools/ui_modernize.py apply
python tools/ui_modernize.py apply --write
python tools/ui_modernize.py verify --build --tests
```

全局节奏只改 `tools/ui-modernize.json`。结构、动画和单页例外单独修改组件。

### 3.3 UI 后续工作

- [ ] 底栏、任务中心和详情页统一为自绘进度组件；
- [~] 统一按钮视觉层级：Primary 使用主题色，Secondary 使用有边界的中性面，Ghost 只用于
  低频辅助动作，Danger 使用语义红；清理仍沿用平台灰色默认外观的可操作按钮；
- [x] 单独设计 Disabled 状态：降低对比度但仍保留轮廓和文字可读性，不能让正常 Secondary
  和 Disabled 看起来是同一档；同时检查 hover / pressed / focus / checked 五种状态；
- [~] 翻新底栏左右两颗按钮展开的面板（运行中实例、账户）：统一圆角、间距、阴影/边界、
  行高、图标、标题与次要文字；去掉“系统菜单/旧式对话框”观感；
- [x] 底栏弹层使用同一套容器与行组件，定位锚点、开合动画、点击外部关闭、
  Esc、键盘焦点和窄窗口避让只实现一次；业务内容仍由各自模型提供；
- [x] 运行中实例弹层优先展示实例名、运行时长和“打开日志/停止”等可用动作；账户弹层保持紧凑，
  单账户不浪费成大列表，多账户时再展开切换列表；
- [~] 收口剩余局部样式和硬编码颜色；
- [ ] 深色主题逐页验收透明页面、日志、弹层和下拉框；
- [~] 完善键盘焦点、Tab 顺序和减少动态效果；
- [ ] 截图确认后更新依赖旧尺寸的 UI 测试。
- [ ] 逐条处置当前 7 条 UI 验证失败：先保存失败画面和期望画面，再决定改布局或改基线；
  禁止用放宽所有几何断言的方式一次性“变绿”。

## 4. 实例预设：产品定义

用户可以保存多个预设，将任意一个设为唯一默认项，并在安装实例时选择和确认应用。

预设可以包含：

- FOV、GUI 缩放、语言、音量、渲染距离等游戏设置；
- 原版和普通模组注册的按键；
- MaLiLib、Inventory Profiles Next 等自定义按键；
- 服务器列表；
- 资源包、光影包、模组及启用顺序；
- `config/`、`defaultconfigs/` 等配置；
- Java 参数、窗口尺寸等启动器已理解的实例设置。

默认排除账号与令牌、世界、截图、Replay、日志、崩溃报告、实例 UUID 和启动器身份文件。

## 5. 实例预设架构

```text
预设管理 UI
    ↓
PresetManager / PresetStore
    ↓
PresetPlanner（预览、兼容性、冲突、风险）
    ↓
PresetApplyTask（备份、执行、结果）
    ↓
Adapter 层
├─ VanillaOptionsAdapter
├─ ServerListAdapter
├─ MalilibAdapter
├─ InventoryProfilesAdapter
├─ ControlifyAdapter
└─ GenericFileAdapter
```

建议目录：

```text
src/presets/
  PresetTypes.hpp
  PresetStore.hpp/.cpp
  PresetManager.hpp/.cpp
  PresetPlanner.hpp/.cpp
  PresetApplyTask.hpp/.cpp
  PresetBackup.hpp/.cpp
  adapters/

ui/
  PresetManagePage.hpp/.cpp
  PresetEditorDialog.hpp/.cpp
  PresetApplyPreview.hpp/.cpp
  PresetConflictDialog.hpp/.cpp
```

存储结构：

```text
<launcher-data>/presets/
  index.json
  <preset-id>/
    manifest.json
    files/
```

`index.json` 保存顺序和唯一的 `defaultPresetId`。每个预设独立保存。

```cpp
enum class ApplyMode { Merge, Replace, Append, CopyIfMissing, Skip };

struct PresetManifest {
    int schemaVersion = 1;
    QString id;
    QString name;
    QString description;
    QDateTime createdAt;
    QDateTime updatedAt;
    QList<PresetSection> sections;
};
```

资源条目支持：固定本地文件与 SHA-256、固定平台版本、按 Minecraft/加载器解析兼容最新版。

## 6. 预设实施阶段

### P1：仓库与管理 UI

- [ ] PresetStore CRUD、排序与唯一默认项；
- [ ] schema 校验、损坏项隔离和原子写入；
- [ ] 新建、复制、重命名、删除和从实例提取；
- [ ] 管理页左侧列表、右侧摘要和内容编辑；
- [ ] ID 使用 UUID，显示名不作为磁盘路径。

验收：损坏单个预设不阻止其他预设加载；非法名称无法逃逸目录。

### P2：原版设置与按键

- [ ] 建立保序的 `options.txt` 模型；
- [ ] 保留未知行和目标版本新增字段；
- [ ] 基础设置与 `key_` 按键分组选择；
- [ ] 默认 Merge，Replace 必须明确确认；
- [ ] 检测标准按键的精确重复；
- [ ] 显示器、全屏分辨率等硬件相关项默认不选。

不要简单用 `QMap` 重写整个文件，也不要把缺失字段当空值写回。

### P3：服务器与通用文件组

- [ ] `servers.dat` 支持 Append / Replace / Skip；
- [ ] 按规范化地址去重并保留顺序；
- [ ] GenericFileAdapter 支持 `config/`、`defaultconfigs/`、资源包和光影包；
- [ ] 每项支持 Replace / CopyIfMissing / Skip；
- [ ] 路径穿越、绝对路径和符号链接逃逸检查；
- [ ] 未知配置明确标记为“只能复制/覆盖”。

### P4：MaLiLib 适配

首批识别 `malilib.json`、`litematica.json`、`tweakeroo.json`、`minihud.json`、
`itemscroller.json`，并通过结构识别 TweakerMore、Masa Gadget、OMMC 等扩展。

- [ ] 默认仅合并热键；
- [ ] 可选合并全部已知设置、完整覆盖、缺失时复制；
- [ ] 热键连同触发、上下文、额外键、顺序、独占和取消行为保存；
- [ ] 身份使用 `modId/groupId/optionName`，不使用翻译名和行号；
- [ ] 目标字段缺失时跳过并报告，类型变化时拒绝转换；
- [ ] 保存来源 Minecraft、加载器、模组和 MaLiLib 版本。

统一展示 DTO：

```cpp
struct KeyGesture {
    QStringList keys;
    QString trigger;
    QString context;
    bool orderSensitive = false;
    bool allowExtraKeys = false;
    bool exclusive = false;
};
```

DTO 只用于展示和冲突分析，写回仍由适配器维护原格式。

### P5：其他自定义系统

- [ ] Inventory Profiles Next / libIPN：先整体文件组，再分离热键、规则、锁定槽位和装备方案；
- [ ] Controlify：区分可迁移动作映射和不可默认迁移的设备 GUID、校准值；
- [ ] Figura、宏脚本和模块化客户端暂按通用文件组处理；
- [ ] UI 区分“结构化配置”和“兼容性未知的配置文件”。

## 7. 安装流程集成

加载器选择区域增加：

```text
实例预设  [使用默认：Fabric 客户端 ▼]  [查看内容]
          ☑ 安装完成后应用
```

用户手动选择后，不因后台默认项变化而偷偷切换。开始安装前展示精确计划：

```text
游戏设置          合并 18 项
原版按键          合并 23 项，冲突 1 项
MaLiLib 热键      合并 12 项，跳过 2 项
服务器列表        追加 3 个
资源包            安装 2 个
配置文件          复制 8 个，覆盖 1 个
```

执行顺序：

```text
创建实例
→ 安装游戏与加载器
→ 安装模组和资源
→ 生成并显示 ApplyPlan
→ 确认游戏未运行
→ 备份覆盖目标
→ 应用预设
→ 写入实例元数据和任务结果
```

预设应用是安装任务的一个步骤。第一版只处理首次启动前配置；需要模组首次运行生成的配置先
创建模板或跳过，首次退出后二阶段应用以后再做。

## 8. 预设安全与测试

### 8.1 JSON 与写入

- [ ] 检查打开错误、`QJsonParseError`、根节点和字段类型；
- [ ] 区分缺失、`null` 与错误类型；
- [ ] 大整数 ID 保存为字符串；
- [ ] 格式错误显示字节位置；
- [ ] 所有写入先完成 ApplyPlan，再备份和原子替换；
- [ ] 应用期间给实例加写锁。

### 8.2 测试矩阵

```text
原版 → 原版，同版本/跨版本
Fabric + MaLiLib → 新 Fabric 实例
Forge/NeoForge 通用配置文件组
目标缺少预设中的模组
目标模组版本更高/更低
游戏进程正在运行
磁盘不足或写入中断
损坏 JSON/NBT
中文路径和长路径
路径穿越与符号链接逃逸
```

UI 覆盖无预设、单预设、多预设、默认项丢失、100+ 冲突、窄窗口和多 DPI。

## 9. 其余产品能力

### 9.1 统一更新中心

- [x] 已有统一结果页、资源检查、逐项更新和下载路径；
- [x] 忽略版本、固定版本、更新前轻量回滚点和撤销；
- [~] 多选批量更新与失败项定向重试仍需补齐；
- [x] 每个资源有可见结论，未识别与已是最新不混为一类；
- [x] 禁用资源不参与检查，更新后仍保持禁用。

### 9.2 下载调度

- [x] 并发数预设 6 / 12 / 24 / 48、设置持久化与运行时即时生效；
- [ ] 下载限速和代理；
- [ ] 速度与 ETA 使用滑动窗口；
- [ ] HTTP Range 与 `.part` 稳定后支持暂停/继续；
- [ ] 再评估多个下载批次真正并发。

### 9.3 启动器自更新

- [ ] 正式发布通道和版本说明；
- [ ] 临时文件、SHA-256 与签名校验；
- [ ] 独立 updater 原子替换和失败回滚；
- [ ] 自动检查/仅提醒/关闭，不静默安装。

### 9.4 主题、可访问性与发布

- [x] 已有统一主题底层、动画开关和减少动态效果接线；
- [x] 浅色/深色/跟随系统入口；
- [x] 主题色、文字不透明度、导入导出和恢复默认；
- [~] 主题预览仍以即时应用为主，需随截图基线补齐逐页深浅验收；
- [ ] 逐页键盘和减少动态效果验收；
- [ ] Azure App ID 审核与真实账号全链验收；
- [x] Client ID 和回调地址通过构建配置注入；回调监听端口不再维护第二份常数；
- [ ] 发布构建移除或替换源码兜底 App ID，并检查产物、日志和仓库中没有敏感凭据。

## 10. 推荐提交顺序

```text
1. ui-test: capture and resolve the 7 remaining modern-profile visual assertions
2. ui: finalize screenshot baseline and shared progress component
3. stability: finish task capability/cancellation audit and destructive-path fixtures
4. architecture: extract one bounded domain controller with dual-path regression coverage
5. presets: add manifest types and atomic preset store
6. presets-ui: add management and extraction workflow
7. presets: add vanilla options and keybinding merge
8. presets: add servers and generic file adapters
9. presets: add MaLiLib adapter and conflict analysis
10. presets: add IPN and Controlify file-level support
11. install: add preset selector and apply preview
12. tasks: integrate preset apply into installation task
13. resources: resolve preset mods and packs by compatibility
14. product: complete update center, downloader and self-update
```

每个提交必须独立构建。默认自动应用必须等预览、备份和错误报告完成后再开启。

## 11. 里程碑

### M0：当前功能稳定化

UI 截图基线、当前 7 条 UI 验证差异、任务取消/重试审计、备份/服务端/主页回归、UI 工具自身测试。
同时完成按钮状态体系和底栏左右弹层现代化；它们属于全局框架视觉，不拖到实例预设之后返工。

完成 M0 的硬条件：核心与主窗口测试保持全绿；现代化 profile 的视觉验证全绿；每条被更新的
几何基线都有对应截图依据；不以“MainWindow 降到多少行”作为验收指标。

### M1：可发布的基础预设

多预设 CRUD、唯一默认项、从实例提取、`options.txt`、`servers.dat`、通用文件组，以及安装时
选择、预览、确认、备份和结果。

专项验收：应用前可见精确计划；覆盖前有回滚点；中途失败可恢复并给出逐项结果；
原本禁用的资源继续禁用；没有证据的版本兼容不做静默转换。

### M2：主流自定义按键

MaLiLib 字段级适配、统一冲突展示、IPN 热键、Controlify 安全文件级迁移。

### M3：资源和长期维护

Modrinth/CurseForge 兼容解析、同步已有实例、导入导出、schema 迁移，再评估继承、云同步和
首次退出后二阶段应用。

### M4：发布完整度

统一更新中心、下载限速/代理/断点续传、自更新、深色主题、可访问性和正版登录发布验收。

发布硬阻塞：用于正式发布的 Azure App ID 必须通过 Mojang 审核，并用获批的应用完成
首次登录、令牌刷新、过期、无游戏资格、无 Xbox 档案和登出的真实全链验收。审核是
外部依赖，代码不能绕过；未通过时不得宣称正版登录可发布。

## 12. Definition of Done

- 入口、执行状态、完成结果和失败恢复齐全；
- 长任务不阻塞 UI；
- 取消与失败不留下被当成成品的半文件；
- 错误既进入日志，也在当前流程中可见；
- 覆盖用户数据前有预览和恢复信息；
- 新状态有核心测试和主窗口测试；
- 视觉变化有 UI/截图验收；
- `FEATURES.md` 更新已实现约定；
- Release 构建、`ctest --preset verification` 和视觉验证通过。

## 13. 明确暂缓

- 插件/脚本市场；
- 云同步、好友/聊天、公共服务器广告；
- 公网穿透、托管和多机器远程控制；
- 启动器内网页浏览器；
- 自动化整合包制作工作室；
- NBT 通用编辑器（世界设置所需的定向 NBT 能力继续维护，但暂不扩成开发者工具）；
- 极速版分支的同步维护。

先完成实例预设、更新闭环和发布可靠性，再评估这些扩大安全边界的功能。
