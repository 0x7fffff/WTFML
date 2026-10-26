# 大文件职责拆分记录

## 2026-10-26 本轮边界

本轮以行为保持为前提，不改变布局、任务规则或安装策略；不是运行性能结论。

| 原入口 | 拆分前行数 | 拆分后行数 | 本轮职责去向 |
| --- | ---: | ---: | --- |
| src/MainWindow.cpp | 11417 | 10576 | ResourceSearchController、InstanceUpdateCheckController |
| ui/MainWindowUI.cpp | 6256 | 1602 | MainWindowDownloadPage、MainWindowSettingsPage、MainWindowPanels、共享 PageLayout |
| ui/InstanceSettingsDialog.cpp | 4802 | 3812 | InstanceSettingsServers、InstanceSettingsLifecycle |
| tests/core_modules/ModpackImportTests.cpp | 3272 | 926 | 导出格式、平台反查、打包/身份识别流水线、主题图像检查 |

资源搜索控制器持有实际发送的查询词；更新检测控制器持有运行中的请求集合、页面弱引用和工作线程。
主窗口保留原有接线入口及跨页面结果缓存，在销毁 UI 前调用控制器等待线程收尾。
翻译继续使用 MainWindow 的上下文；没有引入第二份查询/运行状态。

UI 的共享布局、表单/提示构造和尺寸定义只有一份实现；页面分文件后仍按原顺序创建。
ui/MainWindowPages.cmake 是产品与两个 UI 验证层共用的源码清单，防止新增页面只加入一侧。
整合包检查仍属于 core-resources，原有354处 check 及执行顺序保留；不减少用例。
测试提示探针实现从 Support.hpp 移入 PromptProbe.cpp，其他断言文件只见声明。

本轮一并修复测试探针：多窗口时不能用第一个打开的页面代替真正的提示；
不能只在0/300毫秒各找一次。现按请求代次等待，20秒上限，命中后立即收尾，过期则退出。
新增另一窗口保留页面、提示晚于300毫秒出现的夹具，并有独立2秒兜底。

## 本轮验证

- `20261026-184714-611067b1`：8个专用UI分片通过。
- `20261026-190617-e39e6dbe`：工作区/活动中心2个分片通过，包含延迟提示与多窗口夹具。
- `20261026-190844-451f23e2`：完整主窗口2707/2707通过，91.65秒；包括搜索、更新检测、控制器与线程退出。
- `20261026-191423-324c6db0`：core-resources 825/825通过，27.11秒。
- `20261026-191541-0cd0bd59`：完整UI 3119/3119通过，11.19秒。
- `20261026-191604-bf1baa87`：正式启动器/网络诊断构建及 network-diag通过。
- `20261026-191742-a0564831`：9个相关专用集成分片8个通过；升级仍是已记录的4条保留目录失败（Windows错误5），未改为通过。

额外核对UI构建器53个、实例设置129个原有成员函数的函数体及签名完全保持；
测试模块大小/归属检查与运行器影响映射自检通过。构建每阶段300秒，测试有独立90～240秒上限。
初次缺少共享文字构造函数/完整类型头导致的编译失败，以及修探针前的主窗口240秒超时均保留日志。
不将这次源文件拆分的行数变化当成编译或运行加速数据。

## 尚未收口

- MainWindow 中安装、服务端、更新中心等剩余协调职责及过大的 API/状态头。
- InstanceSettingsDialog 的概览、启动设置与编辑控件仍在主文件，下一步需连同编辑/脏状态边界一起拆。
- UiContentPagesChecks 等大测试仍共享长寿命夹具，先明确夹具生命周期再分领域，避免机械搬运改变事件时序。
- VersionManager 上一轮已经分为核心、加载器安装、整合包导入、实例识别与导出，本轮没有再按行数切割它。

## 之前的记录（保留原验收口径）

以下为本轮前主计划中的原文，其数字是历史结果，不代表本轮重新验收。

- [ ] **重构｜大文件职责**：按控制器/页面/纯逻辑拆 MainWindow、MainWindowUI、VersionManager、InstanceSettingsDialog 和大测试模块。
  当前约 758/381/342/274 KB，仅为维护热点线索，不是性能结论；保持行为及测试归属，不做纯机械拆分。
  2026-10-25 已在 `MainWindow` 上拆出两片（都是"按职责"、不是按行数切），`MainWindow.cpp`
  13,949 → 11,505 行、38/38 全绿、`mw_check` 2701/2701：
  ① `controllers/MainWindowViewData`：各控件的**视图模型**（实例树 / 分组行 / 底栏面板行 / 版本下拉框），
  纯函数、不碰网络与盘，顺手删掉九条早已没人调的重名助手；
  ② `controllers/ResourcePageController`：「资源详情」那一页的**全部状态**（`ResourcePageState`
  整个搬过去）与开页 / 拨勾选框 / 筛分组 / 收尾那几条；主窗口只留四条薄壳
  （`onResourceDownloadFinished` 被 `ConnectionsController` 按名字接走，另三条别处直接叫）。
  控制器**构造里就建**、页面对象仍懒建 —— 图标与版本列表那两发会比"开过页"早到，
  状态里没有"还没建"这一档。
  ③ `controllers/DownloadFilterController`：下载页每一页顶上那一行筛子的**灌与读**
  （来源 / 版本 / 加载器 / 类型 + 左栏分类那一行 + "这一页空着才搜"）。那一行"内容全在这儿灌、
  读也只从这几个访问器读"的规矩现在有了唯一的门；搜索本身（请求钥匙、结果怎么摆行）仍留主窗口。
  两次搬运后 `MainWindow.cpp` 13,949 → 11,306 行，`mw_check` 2701/2701、38/38 全绿。
  2026-10-26 ④ `ui/MainWindowUI`：`setupUi()` 里那 1,590 行按**一页一摊**拆成
  `setupHomePage()` / `setupInstancePage()` / `setupDownloadPage()` / `setupSettingsTab()`
  四个函数（只共享已经挂好的 `tabWidget`，各自的局部变量谁也不外流）。`setupUi()` 现在
  219 行，只剩"窗口与标题栏、按契约顺序摆四页、导航接线、底栏、覆盖层、外框"。
  实测那份"一直排第一"的热点其实看错了口径：`setupSettingsDownloadPanel` 只 136 行。
  ⑤ 再把 `setupDownloadPage` 那一页里真正不同的三摊拆开：分类列表/页头/堆叠页外壳留在
  `setupDownloadPage`，第 0 页「游戏」进 `setupDownloadGamePage()`（版本树 + 安装页 + 页头
  显隐那两条 connect），其余几页那段循环进 `setupDownloadResourcePages()`。
  三摊之间只共享已经挂好的 `downloadStack` / `gameStack` / `downloadHeader`，没有局部变量要传；
  "第 0 页先加、其余按表顺序往后加"这条下标契约就落在 `setupDownloadPage` 里那两行调用上。
  现在 `ui/MainWindowUI.cpp` 里最长的是 `setupSettingsLookPanel()`（434 行）、其次
  `setupDownloadGamePage()`（370）与 `setupInstancePage()`（364）。
  ⑥ `ui/InstanceSettingsDialog`：构造函数那 1,631 行按"一栏一摊"搬出三摊 ——
  `setupOverviewPage()`（595）、`setupLaunchPage()`（419）、`setupFilePages()`（434），
  构造函数只剩"两栏外壳 + 三行调用 + 接线/灌值"。原来那三摊是**同一个函数里换着用一份
  `grid`、`row` 从 0 数起**（注释里就写着这条），现在每摊自己 `addForm()` 一份；
  只有五样"这一次打开对话框的现场"要传进去（目录名、全局值、追加 JVM 参数、提醒开关、窗口档位），
  不存成成员。
  ⑦ `manage/VersionManager`：146 个成员函数、单函数最长 238 行 ⇒ 是聚合大、不是局部烂，
  于是按"一份状态机一摊"把 6,137 行拆成四份 .cpp + 一份内部头：
  核心（`VersionManager.cpp` 2,197 行：版本清单 / 版本 json / 下载编排 / 批次收尾 / 实例元数据）、
  加载器安装（`VersionManagerLoaderInstall.cpp` 2,101）、整合包导入（`VersionManagerModpack.cpp` 742）、
  实例识别与导出（`VersionManagerExport.cpp` 1,046）。**类没动**（同一个类，定义分散在四个
  翻译单元），所以断言一条都不用改：38/38 全绿、`verify` 5954/5954。
  原来住在本文件顶上匿名命名空间里的 17 个助手（`copyTreeInto()` / `installerOutputTail()` /
  `readVersionJsonAt()` 等）搬进 `VersionManagerInternals.hpp`：匿名命名空间是**内部链接**，
  拆完另外三份根本看不见它们；各抄一份的话"只改一处"就是静默不一致（`copyTreeInto()` 自己
  那段注释写的正是这件事）。两个写法上的坑：头文件里的函数必须 `inline`，那两个
  `const QString` 也得写 `inline` —— 不写不在编译期报错，是链接期一串 duplicate symbol。
  缝合线挑的是**状态机的归属**（`LoaderInstall` 只在安装那一份里走动，`ModpackInstall` /
  `NamedInstance` 只在导入那一份里），不是行数；唯一横跨几摊的是批次收尾那个岔口
  `onAllDownloadsFinished()`，它留在核心这一份里，谁要接下去都在那一处一眼看全。
  这三份 .cpp 要同时进三张源文件表（主目标、`tests/CMakeLists.txt` 的 `wtfml_core_runtime`、
  `tests/mw_check/CMakeLists.txt`）—— 漏一张不在编译期报错，是链接期一串
  `VersionManager::…` 的 undefined reference。
