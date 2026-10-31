# 嵌套事件循环盘点（2026-10-31，快照 `a51159f`）

「导航/实例元数据渐进收口」那一项要动 `execPage()` 的嵌套事件循环和逐根页路由栈 ——
动之前先把**现在到底有哪些地方会重入事件循环**数清楚。这份记录只做盘点，不改任何代码。

## 两类，别混着看

- **A. 有意等用户**：`OverlayHost::execPage()`。调用方要等用户把这一页收掉才往下走
  （原来的 `QDialog::exec()`，现在收进内容区）。它们**必须**阻塞 —— "等用户"就是语义。
- **B. 等后台**：等工作线程、等网络回复、等子进程退出。这一类里凡是在 UI 线程上等的
  都是"界面在这段时间里不动"的候选，分钟级的几处已经单列在
  [测试笔记](../../WTFML-测试笔记.md) 的待办里。

## A：`execPage()` 的调用点

`execEmbedded()` 里那次 `loop.exec()`（`ui/OverlayHost.cpp:710`）没有超时、**处理用户输入**
（和 `QDialog::exec()` 同义）。退出条件是四个：对话框 `finished`（accept/reject）、
对话框被销毁、宿主被销毁。`backPressed` 只有"当成页面打开"那一路才接，收到就对 dialog
`reject()`（`ui/OverlayHost.cpp:677-679`）。

重入守卫只有一条：`openPage()` 在 `m_mode != None` 时返回 `nullptr` —— 一层只放一页，
收在别的选项卡上的页也算占着（`ui/OverlayHost.cpp:384-390`）。

| 调用点 | 打开的是什么 |
| --- | --- |
| `MainWindow.cpp:389` | 通用入口（各 controller 经 `Host::execPage` 转进来） |
| `MainWindow.cpp:4962` / `:5905` | 账号外观那一页 / 分组设置对话框 |
| `InstanceDialogController.cpp:2434` | 实例设置（整页，十几个闭环的接线台） |
| `IntegrityController.cpp:197` | 体检报告页 |
| `JavaSettingsController.cpp:116` | 「Java 管理…」 |
| `PresetControllerEditor.cpp:73` | 预设编辑器 |
| `ResourcePageController.cpp:232` | 资源详情页 |
| `ServerConsoleController.cpp:73` / `ServerSettingsPageController.cpp:107` | 服务端控制台 / 服务端设置 |
| `TaskDetailController.cpp:91` | 任务详情 |
| `ToolboxController.cpp:425` / `UpdateCenterController.cpp:63` | 工具盒 / 更新中心 |

**收页的两条路（这一条是路由栈重构的前提，别记混）**：

- `dismiss()`（`ui/OverlayHost.cpp:562`）：提示层发 `promptDismissed`、页面层发 `backPressed`，
  dialog 被 `reject()` ⇒ `finished` ⇒ 那个 `loop.quit()` 醒过来。**在页面自己的按钮处理里
  收页必须走它**。
- `closeOverlay()`（`ui/OverlayHost.cpp:441`）：只拆外壳，一个信号都不发 ⇒ **结束不了**
  正卡在 `loop.exec()` 里的调用方。`execEmbedded()` 是在循环返回**之后**才调它收自己那一层
  （`ui/OverlayHost.cpp:721-723`）。

所以现在有一批"局部对象 + 栈上 lambda"的写法（对话框是 `openInstanceSettingsNow()` 里那个
栈上的 `InstanceSettingsDialog dialog(...)`）：只要调用方还在嵌套循环里，它就还活着；一旦改成
非阻塞（回调/路由栈），这些捕获全得重接 —— 这就是那一项没有顺手做掉的原因。

## B：等后台的那些（UI 线程上）

| 位置 | 等什么 | 用户输入 | 超时 | 守卫 |
| --- | --- | --- | --- | --- |
| `ImportController.cpp:73` `runOffUiThread()` | 工作线程 finished（解析整合包文件） | 会 | 无 | `m_importFlow` |
| `ImportController.cpp:677` `repairFromReport()` | `QFutureWatcher` | **不会**（`ExcludeUserInputEvents`） | 循环无超时；工作侧 900 s | `m_importFlow` |
| `LaunchController.cpp:69` `runOffUiThread()` | 工作线程 finished（natives / Java） | 会 | 无 | `m_startInProgress` + `m_abortRequested` |
| `PresetControllerEditor.cpp:158` `leaveEditor()` | `saveEditor()` 的完成回调 | 会 | 无 | `m_editorSaving` / `m_editorPrompting` |
| `MainWindow.cpp:1240` `enterSyncWait()` | 不是等谁：进去之前排一次重绘 | **不会** | 无 | 由调用方挡（启动/导入的忙标志） |
| `MainWindow.cpp:5094` `refreshAccountSkin()` | 排一次重绘，随后同步取令牌 / 披风 | **不会**（这一次）；后面的同步调用压根不跑循环 | 网络那两次未读到 | 只有 `isVisible()` 判据 |
| `GameWindowSettings.cpp:223` `maximizedContentSize()` | 离屏探测窗排版 | **不会** | 无 | 静态缓存 |
| `HttpClient.cpp:37` | Control 名额（25 ms 轮询） | 会 | 15 s 默认 | 名额租约 |
| `HttpClient.cpp:80` | 网络回复 finished | 会 | 15 s 默认（减去已用） | 无 |
| `GlobalJavaSettings.cpp:94` `runProcess()` | 子进程退出（`waitForFinished`） | **不会** | 5 s | 无 |
| `GlobalJavaSettings.cpp:461` | 下载回复 | 会 | 30 s | 写目录锁 |
| `GlobalJavaSettings.cpp:976` | 下载回调 | 会 | 30 s | Control 名额 |
| `ServerPing.cpp:381` `lookupSrv()` | DNS finished | 会 | ≤ 2 s | 无 |
| `VersionManager.cpp:146`（析构） | `QFuture` finished | **不会** | **无超时** | 析构前 `cancel()` |
| `VersionManager.cpp:153` / `VersionManagerLoaderInstall.cpp:1481` | 安装器进程（kill 后） | **不会** | 5 s | 后者先把 phase 置 Idle |
| `ZipUtil.cpp` `runPowershell` / `readEntry` / `extractEntryToFile` / `listEntries` | PowerShell 子进程（`waitForFinished`） | **不会** | 60 s / 180 s / 300 s / 900 s / 1800 s 各档，强杀后再等 5 s | 只有可选的 `cancelled()` 回调 |
| `ServerProcess.cpp:43`（析构）/ `:170` `stopAndWait()` | 服务端子进程 | **不会** | 3 s；优雅 `gracefulMs + 5 s`，强杀再 5 s | `m_status == Stopping` |

## 从这张表能直接读出来的几件事

1. **"会送用户输入回来"的那些**（A 全部，加上 ImportController:73、LaunchController:69、
   PresetControllerEditor:158、HttpClient 两处、GlobalJavaSettings 两处、ServerPing）意味着
   重入是**可能发生的**，挡它的只有各自那一个忙标志。这几处的忙标志就是它们的正确性边界：
   少一个，用户点两下就是同一段路跑两遍。
2. **"不会送用户输入"的那些**（`waitForFinished` 与 `ExcludeUserInputEvents`）不重入，但
   **界面在这段时间里完全不动** —— 分钟级的全在这一列（ZipUtil 那几档、`refreshAccountSkin()`
   后面两次同步网络调用、`VersionManager` 析构里那个**没有超时**的 `waitForFinished`）。
3. `VersionManager::~VersionManager()` 里那个 `waitForFinished()`（`VersionManager.cpp:146`）
   是唯一**没有超时参数**的一处：它等的是 `m_modpackOverridesWork`，前面先置取消标志、
   再 `cancel()` —— 那个标志是给外部解压进程看的，所以它肯退出，这里就等得完；它不退出，
   这里就等下去（析构场景，也没有别的路可走）。
4. 路由栈那一项真正要改的是 A 类：把"调用方等在循环里"换成"页面收掉时回调"。B 类里凡是
   UI 线程同步等的，得先挪到后台（其中几处已在测试笔记的待办里），否则改成非阻塞之后，
   卡顿只是换了个位置。

行号以 `a51159f` 为准；之后动过这些文件的，按名字找、别按行号找。
