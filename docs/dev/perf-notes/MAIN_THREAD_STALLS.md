# 主线程卡顿：采栈、符号化与已记录的现象

给要查"界面为什么停了一下"的人看：怎么在探针构建里采到卡住那一刻的调用栈、怎么翻译成函数名和行号、已经量到过哪些卡顿，以及一条没查清的"起线程要 3 秒"。耗时基线见 [PERFORMANCE.md](../PERFORMANCE.md)。

数据来自 2026-11-01 同一台机器、同一份探针 exe、`-Instances 200`（200 个实例）。预设 store 的同步读那一条已被 [PRESET_LIST_IO.md](PRESET_LIST_IO.md) 改成并行 + 缓存，下面的栈和耗时是改之前的样子。

## 采栈

`src/perf/StallSampler.cpp`，只在 `-DWTFML_PERF_PROBE=ON` 的构建里编进程序（并链 `dbghelp`），产品构建里没有。

1. 主线程每个探针步骤打一次心跳（`PerfProbe` 的看门狗调 `StallSampler::beat()`）。
2. 另一个线程每 10 ms 看一眼心跳，超过 50 ms 没动就算卡住。
3. 卡住时 `SuspendThread` → `GetThreadContext` → `StackWalk64` → `ResumeThread`。暂停期间只走栈、不查符号：查符号要分配内存，而主线程卡住的常见原因正是卡在分配器里，暂停它再去要内存可能互等。
4. 放行之后才 `SymFromAddr` 补名字；本程序自己的帧补不出来，只记 `WTFMinecraftLauncher.exe+0xRVA`。
5. 一次卡顿采一份，一次测量最多 32 份；栈深上限 `kMaxFrames = 24`，很深的事件循环里看不到 `main`。

`gapMs` 是放行后等下一次心跳算出来的，可能比卡顿本身多一个轮询周期。三跑 22 份栈里 `gapMs` 比同一步骤的 `actionMs` 大 1.9–86.6 ms（中位 25.6 ms），没有数量级的放大。

只停采栈、保留看门狗（心跳和最大间隔）：设 `WTFML_PERF_NO_STALL_SAMPLER=1`，用来分清"某一步慢"是产品的还是测量工具的。开关各 4 跑，`nav:settings` 都在 10–12.5 ms，没有证据说采栈拖慢了被测步骤。

## 符号化

本程序的 RVA 事后用 `tools/symbolize-stalls.ps1` 交给 `addr2line`（`-Exe`、`-ProbeJson`，`-Addr2Line` 默认 `D:/Qt/Qt/Tools/mingw1310_64/bin/addr2line.exe`）：

```powershell
# 1) 编一份带探针、带 -g 的程序（CMakeLists 在 WTFML_PERF_PROBE 下自动加 -g）
cmake -S . -B build/tmp-wtfml-probe -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=D:/Qt/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_BUILD_TYPE=Release -DWTFML_PERF_PROBE=ON -DWTFML_BUILD_TESTS=OFF
cmake --build build/tmp-wtfml-probe --target WTFMinecraftLauncher -j 8

# 2) 跑探针（脚本会把 HKCU\Software\WTFML 换成沙盒，跑完还原）
pwsh -File tools/perf-startup.ps1 -Probe `
  -Exe build/tmp-wtfml-probe/WTFMinecraftLauncher.exe `
  -Runs 1 -Warmup 0 -Instances 200 -InstanceMeta -StallSeconds 2 `
  -ReportDir build/tmp-wtfml-stall-run

# 3) 把栈翻译成人话（只读文件 + 跑 addr2line，不启动任何程序）
pwsh -File tools/symbolize-stalls.ps1 `
  -ProbeJson build/tmp-wtfml-stall-run/run-01-probe.json `
  -Exe build/tmp-wtfml-probe/WTFMinecraftLauncher.exe
```

## 坑

1. `SYMBOL_INFO::Name` 声明成 `CHAR Name[1]`，直接 `QString::fromLatin1(symbol->Name)` 会被 Qt 6 挑成"按数组长度取"的重载，长度恒为 1，量到的是一串首字母。先退化成 `const char *` 并带上 `NameLen`。
2. 栈里除第一帧外都是返回地址，指向调用指令的下一条。不 `-1` 就查会落到下一行甚至下一个函数头，满屏 `~QString()` 噪声。
3. `addr2line` 要 `-i`，取内联链的**最后**一行才是真正写这段代码的函数；配合 `-a` 才能按地址分组解析一批。
4. dbghelp 对系统 DLL 只有导出表，给的是"最近的导出 + 位移"。报告里系统帧写成 `符号+0x偏移`，偏移大的别当成精确的当前函数。
5. GCC 13.1 不支持 `-gcodeview`，拿不到 dbghelp 能吃的 PDB，本程序的帧只能事后用 `addr2line`（认 DWARF）解析，所以采样器只记 RVA，符号化离线做。
6. PowerShell 里 `[uint64]` 变量做 `--` 会变成 `Decimal`，`'{0:x}' -f` 报 "Format specifier was invalid"。改成 `[uint64]($x - 1)`。

## 量到的卡顿（2026-11-01）

三跑的步骤耗时（`actionMs`）与卡顿（`gapMs`）：

| 步骤 | 第 1 跑 | 第 2 跑 | 第 3 跑 | 卡顿 |
| --- | --- | --- | --- | --- |
| `scan:java` | 108.4 ms | 80.7 ms | 97.2 ms | 156–195 ms |
| `nav:instances` | 277.7 ms | 276.7 ms | 272.6 ms | 284–326 ms |
| `tasks:open-200` | 81.5 ms | 76.8 ms | 76.6 ms | 98–122 ms |
| `presets:prepare-store-1000` | 1694.2 ms | 1670.1 ms | 1687.8 ms | 1672–1726 ms |
| `presets:read-store-1` | 2366.5 ms | 2353.3 ms | 2351.2 ms | 2364–2381 ms |
| `presets:read-store-2` | 2318.6 ms | 2334.5 ms | 2306.4 ms | 2321–2353 ms |
| `presets:read-store-3` | 2292.4 ms | 2308.4 ms | 2306.3 ms | 2301–2334 ms |
| `nav:settings` | 2897.2 ms | 12.3 ms | 10.9 ms | 2909 ms（只有第 1 跑） |

八份栈里七份的本程序帧都指到了函数 + 行号。

### 预设 store 同步读：约 2.3 s

```
ntdll!NtReadFile → KERNELBASE!ReadFile → Qt6Core!QFileDevice::readData → QIODevice::read
Presets::loadManifest   PresetStore.cpp
Presets::list
```

当时 `Presets::list()` 先读下标，再按 id 逐个 `loadManifest()`（`QFile::open` + `read` + JSON 解析），1000 份约 2.3 s，每份约 2.3 ms，三趟一致。预设页走异步的 `Presets::CatalogLoader`，同步入口 `PresetController::availablePresets()` / `installPickerData()` 直接调 `list()`。现状见 [PRESET_LIST_IO.md](PRESET_LIST_IO.md)。

### 预设 store 同步写：约 1.7 s

```
ntdll!NtFlushBuffersFile → KERNELBASE!FlushFileBuffers → Qt6Core!QSaveFile::commit
writeBytesAtomic → Presets::saveManifest
```

另两跑栈顶换成 `ZwQueryFullAttributesFile` → `QFileInfo::isSymLink`（`QSaveFile` 提交前问目标是不是符号链接）。1000 份约 1.7 s，每份约 1.7 ms，每次写都等一次同步刷盘。store 的形状是"一份预设一个文件 + 每次写都刷盘"：单份成本不高，份数多又在主线程上就是秒级。

### `nav:instances`：约 280 ms，首次画中文时查字形回退

```
DWrite.dll（十几帧，没有导出符号）
Qt6Gui!QWindowsDirectWriteFontDatabase::populateFamily
Qt6Gui!QPlatformFontDatabase::fallbacksForFamily
Qt6Gui!QFontEngineMulti::ensureFallbackFamiliesQueried → stringToCMap
```

成本在字体引擎一侧，三趟都在 272–284 ms。与 [PERFORMANCE.md](../PERFORMANCE.md) 的"主页那颗箭头""界面正文里的回退字形"同类。

### `scan:java`：约 100 ms，主线程等子进程

栈：`QProcess::waitForFinished` ← `runProcess` ← `verifyJavaExecutable` ← `GlobalJavaSettings::probeJavaMajorVersion` ← `scanLocalJavaVersions`（`GlobalJavaSettings.cpp`）。三跑 108 / 81 / 97 ms，卡进去的一帧有时是建进程、有时是等进程退出。这一步在主线程同步拉起 java 问版本再等它退出。

### `tasks:open-200`：约 80 ms，造 200 行任务控件

栈：`QLabel` / `QFrame` 构造、`QWidget::ensurePolished` → `QLayout::totalSizeHint`、`ElidedLabel::ElidedLabel`、`TaskCenterPanel::showEmbedded()`。都在"造 200 行控件、逐层问 sizeHint"这条路上。

## 起线程偶尔要 3 秒（未解决，只记现象）

`nav:settings`（第一次点设置页；`refreshList()` 有 `m_scanned` 闸，第二次起不再扫）有时 10 ms、有时 2.9 s，栈停在 `QThread::start` → `CreateThread`。

两处不同的调用者撞上过：

```
ntdll!ZwCreateThreadEx → CreateRemoteThreadEx → KERNEL32!CreateThread
Qt6Core!QThread::start → QThreadPoolPrivate::startThread → QThreadPool::start
JavaSettingsController::queueDiscovery       (src/controllers/JavaSettingsController.cpp)
```

```
……同样的 CreateThread → QThread::start → QThreadPool::start
Qt6Gui!…（QImage / PaintEngine 一带，Qt 自己在绘制路径起线程）
Animations::paintEvent                        (ui/Animations.cpp)
```

第二份来自主线程画动画换页时，Qt 内部往全局线程池丢作业，卡 3.0 s。

20 跑 `nav:settings`：

| 组 | `nav:settings` | 其中 > 1 s |
| --- | --- | --- |
| 不加任何东西 | 6 跑 2.91–3.41 s，其余 10–11 ms | **6/20** |
| 第一步先起一颗线程池线程（临时探针步骤） | 全部 9.0–10.7 ms | **0/20** |
| 同上，看任何一步 | —— | **2/20**（都在 `nav:instances`，2.4 s / 3.0 s） |

- 双峰：要么约 10 ms，要么 2.9–3.4 s，没有过渡带。
- 一个最小 Qt 控制台程序，每 200 ms 建一个线程、连建 80 个、跑 3 趟共 240 次，没有一次超过 100 ms；第一发 `QtConcurrent::run` 0–1 ms。所以建线程本身在这台机器上是快的，慢的是这个 GUI 进程在那一刻建线程。
- 预热线程池把卡顿从 `nav:settings` 挪到了 `nav:instances`，没有净改善，所以产品里没加预热。
- 线索：机器上 `root/SecurityCenter2` 报了 Kaspersky（productState 266240）与 Windows Defender。杀毒软件 hook 线程创建时，`CreateThread` 会在内核里等它扫完，值聚集在 3 s 附近像带超时的外部介入。没有关防护验证（硬约束）。

下一步：

1. 把构建目录加进 Kaspersky 排除项再跑 20 次（要用户点头，改的是别人的防护）。
2. 在没有第三方防护的干净虚拟机上跑 20 次。
3. ETW：`Microsoft-Windows-Kernel-Process` / minifilter 事件，看 `CreateThread` 那 3 s 里谁在占着（需管理员权限）。
4. 确认是外部介入后，产品侧只能"少在点击路径上建线程"，但预热实验已显示代价会挪到别处。

复现（20 跑，看 `nav:settings` 是否双峰）：

```powershell
pwsh -File tools/perf-startup.ps1 -Probe `
  -Exe build/tmp-wtfml-probe/WTFMinecraftLauncher.exe `
  -Runs 20 -Warmup 0 -Instances 200 -InstanceMeta -StallSeconds 2 `
  -ReportDir build/tmp-wtfml-controlprobe2
```

## 后台目录加载时界面还点得动吗

以前探针是串行的，后台目录加载那一步会把后面的步骤全挡住（`runNext()` 里等异步目录回来），所以"预设页转圈时点翻页"从没量过。

`src/perf/PerfProbe.cpp` 的改动：

1. `runNext()` 不再在 `m_asyncCatalogPending` 时原地等，只有"步骤全走完、后台还没回来"才等（有上限）。
2. 同一串翻页动作（home → instances → download → settings 转两圈）跑两趟：`concurrent:calm-01..08`（空载）→ `presets:start-background-store`（起真正的 `Presets::CatalogLoader`）→ `concurrent:busy-01..08`（加载中）。
3. 每步记 `loadActive`，报告里 `concurrentWorkload.busyDuringLoad` 说明"加载中"那趟有几发真的在加载中。

报告字段：

```json
"concurrentWorkload": {
  "source": "nav-cycle",
  "calm": {"count": 8, "avgMs": 3.0, "maxMs": 4.4},
  "busy": {"count": 8, "avgMs": 3.6, "maxMs": 4.2},
  "busyDuringLoad": 8,
  "catalogElapsedMs": 2349
}
```

10 跑（`-Instances 200`，当时每次读盘 2.3 s、1000 份 manifest，`busyDuringLoad` 全是 8/8）：

| 组 | 空载 avg / max | 加载中 avg / max | 空载重画 | 加载中重画 | 加载中主线程最大间隔 |
| --- | --- | --- | --- | --- | --- |
| 多任务 | 3.2 / 4.9 ms | 3.7 / 4.3 ms | 3.4 ms | 3.9 ms | 42 ms |
| 多任务 | 3.2 / 5.6 ms | 3.9 / 4.8 ms | 3.3 ms | 4.1 ms | 40 ms |
| 采样开 | 3.0 / 4.4 ms | 3.6 / 4.2 ms | 3.4 ms | 3.9 ms | 40 ms |
| 采样开 | 3.0 / 4.8 ms | 3.6 / 4.5 ms | 3.4 ms | 3.9 ms | 38 ms |
| 采样开 | 3.0 / 4.5 ms | 3.6 / 4.1 ms | 3.4 ms | 4.1 ms | 42 ms |
| 采样开 | 3.0 / 4.2 ms | 3.8 / 4.4 ms | 4.9 ms | 4.0 ms | 44 ms |
| 采样关 | 3.3 / 4.6 ms | 3.7 / 4.5 ms | 3.4 ms | 3.9 ms | 40 ms |
| 采样关 | 3.2 / 4.3 ms | 3.8 / 4.2 ms | 3.4 ms | 4.5 ms | 37 ms |
| 采样关 | 3.1 / 5.1 ms | 3.8 / 4.2 ms | 3.5 ms | 4.8 ms | 41 ms |
| 采样关 | 2.9 / 4.4 ms | 3.6 / 4.0 ms | 3.5 ms | 4.1 ms | 38 ms |

- 加载中翻页每次 3.6–3.9 ms，空载 2.9–3.3 ms，多 0.5–0.8 ms，10 跑方向一致，绝对值 4 ms 以内。
- 整窗重画多 0.5–0.9 ms（3.3–3.5 → 3.9–4.8 ms）；空载重画那格 4.9 ms 是单发噪声，趋势只说均值。
- 加载期间主线程心跳最大间隔 37–44 ms（采栈阈值 50 ms），没采到卡顿栈。
- 同一个 store 同步读一趟是 2.3 s，后台化的收益差三个数量级。

## 未覆盖

| 项 | 状态 |
| --- | --- |
| 真机手点界面时采样 | 只在探针步骤里采过，不是用户鼠标操作的时序 |
| 后台作业的种类 | 只有预设目录读盘一种；下载、实例扫描、启动游戏、多路并发没测 |
| 加载中的动画掉帧 | 没量，只量了"点下去这一下"和整窗重画 |
| 更细的卡顿分布 | 阈值写死 50 ms、一次一份、最多 32 份；没有按栈聚合的统计报表 |
| 系统 DLL 帧 | 只有导出名 + 位移，`DWrite.dll` 完全没符号 |
