# P2：主线程卡住时停在哪一行（2026-11-01）

计划里那条「运行/构建剩余测量：主线程精确调用栈」。以前只知道"这一步花了 N 毫秒"
（`PerfProbe` 的 `actionMs`），不知道那 N 毫秒里主线程在做什么；这次把**卡住那一刻的
调用栈**采下来，并且把本程序的帧解析到函数名和行号。

数字归 [性能记录](../PERFORMANCE.md)，这份文档只讲方法、怎么复核、量到了什么、以及坑。

## 结论

- 机制可用：探针构建里采栈，事后用 `addr2line` 补符号。八份栈里七份的本程序帧
  全部指到函数 + 行号。
- 卡顿是真的，不是采样器加出来的：每份栈的 `gapMs` 与同一步骤自己的
  `actionMs` 相差 2–87 ms（含 10 ms 轮询粒度），没有出现"采完变慢一截"。
- 量到的三类真卡顿：
  1. **预设 store 同步读**：`Presets::list()` 在主线程一份一份读 manifest，
     1000 份 ≈ 2.3 s（`PresetStore.cpp:410` 的 `QFile::read`）。
  2. **预设 store 同步写**：1000 次 `saveManifest()` ≈ 1.7 s，其中一份正好停在
     `QSaveFile::commit()` → `FlushFileBuffers`（`PresetStore.cpp:64`）。
  3. **第一次把中文画到屏上**：`nav:instances` 停在 DirectWrite 的字形回退查询，
     约 280 ms，这是首帧点字体的成本，和 [界面正文里的回退字形](../PERFORMANCE.md)那节同源。
- 另有一次 `nav:settings` 2.9 s，栈停在 `QThread::start` → `CreateThread` 里；
  后来 9 次带栈的测量里出现过 2 次（同一份 exe 再跑 8 次没有），未定位，只记现象。

## 怎么采的

`src/perf/StallSampler.cpp`（只在 `-DWTFML_PERF_PROBE=ON` 的构建里编进程序）：

1. 主线程每个探针步骤打一次心跳（`PerfProbe` 的看门狗里调 `StallSampler::beat()`）。
2. 另一个线程每 10 ms 看一眼心跳。超过 50 ms 没动 = 卡住了。
3. 卡住了就 `SuspendThread` → `GetThreadContext` → `StackWalk64` → `ResumeThread`。
   暂停期间**只走栈、不查符号**：查符号要分配内存，而主线程卡住的常见原因正是
   卡在分配器里，暂停它再去要内存可能互等。
4. 放行之后才 `SymFromAddr` 补名字；本程序自己的帧补不出来（见下面"坑"），
   只记 `WTFMinecraftLauncher.exe+0xRVA`。
5. 一次卡顿只采一份，一次测量最多 32 份。

一份卡顿有多长，是**放行之后等下一次心跳**算出来的，所以 `gapMs` 可能比卡顿本身
略长（最多一个轮询周期的量级）。

本程序的 RVA 事后用 `tools/symbolize-stalls.ps1` 交给 `addr2line`：

```powershell
# 1) 编一份带探针、带 -g 的程序（CMakeLists 在 WTFML_PERF_PROBE 下自动加 -g）
cmake -S . -B build/tmp-wtfml-probe -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=D:/Qt/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_BUILD_TYPE=Release -DWTFML_PERF_PROBE=ON -DWTFML_BUILD_TESTS=OFF
cmake --build build/tmp-wtfml-probe --target WTFMinecraftLauncher -j 8

# 2) 跑探针（这个脚本会把 HKCU\Software\WTFML 换成沙盒、跑完还原）
pwsh -File tools/perf-startup.ps1 -Probe `
  -Exe build/tmp-wtfml-probe/WTFMinecraftLauncher.exe `
  -Runs 1 -Warmup 0 -Instances 200 -InstanceMeta -StallSeconds 2 `
  -ReportDir build/tmp-wtfml-stall-run

# 3) 把栈翻译成人话（只读文件 + 跑 addr2line，不启动任何程序）
pwsh -File tools/symbolize-stalls.ps1 `
  -ProbeJson build/tmp-wtfml-stall-run/run-01-probe.json `
  -Exe build/tmp-wtfml-probe/WTFMinecraftLauncher.exe
```

## 这次量到的（2026-11-01，同一台机器，`-Instances 200`，200 个实例）

一次测量的步骤耗时（`actionMs`）与采到的卡顿（`gapMs`）：

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

第 1 跑同时是**修完符号之后**的第一跑；第 2、3 跑是同一份 exe 又跑的两趟
（`-Runs 2`），用来分清"每次都这样"和"就那一次"。

## 逐条读栈

### `presets:read-store-1/2/3`：约 2.3 s，主线程一份一份读 manifest

```
ntdll!NtReadFile
KERNELBASE!ReadFile
Qt6Core!QFileDevice::readData
Qt6Core!QIODevice::read
Presets::loadManifest(QString const&, QString const&, bool*, QString*)  PresetStore.cpp:410
Presets::list(QString const&, QList<Presets::BrokenPreset>*)              PresetStore.cpp:502
```

`Presets::list()` 的写法是：先读下标，再按下标里的 id **逐个** `loadManifest()`，
每份都 `QFile::open` + `read` + JSON 解析。1000 份 ≈ 2.3 s，摊下来 ≈ 2.3 ms/份，
和三趟完全一致（唯一有波动的是内存回收那一类栈：`RtlFreeHeap` / `NtFreeVirtualMemory`）。

这不是"用户点一下就冻结 2.3 s"：预设页走的是异步的 `Presets::CatalogLoader`
（同一个步骤 `presets:start-background-store` 在 0 ms 内返回，回调另算）。
但同步入口还在——`PresetController::availablePresets()` /
`installPickerData()` / `PresetController.cpp:1327` 都是直接调 `list()`
（`PresetController.hpp:194` 已经写了这事的代价）。所以这一条栈的用处是：
**卡多少、卡在哪个函数**现在有实测背书，不是靠估。

### `presets:prepare-store-1000`：约 1.7 s，一次卡在 `FlushFileBuffers`

```
ntdll!NtFlushBuffersFile
KERNELBASE!FlushFileBuffers
Qt6Core!QSaveFile::commit
writeBytesAtomic                                                        PresetStore.cpp:64
Presets::saveManifest(QString const&, Presets::PresetManifest const&)    PresetStore.cpp:462
```

另两跑里同一处栈顶换成 `ZwQueryFullAttributesFile` → `QFileInfo::isSymLink`
（`QSaveFile` 提交前问一下目标是不是符号链接）。两者都是"1000 次落盘"里的**某一次**：
`QSaveFile::commit()` 会 `FlushFileBuffers`，也就是每次写都要等一次同步刷盘。
1000 份 ≈ 1.7 s，摊下来 ≈ 1.7 ms/份。

这一条和上面那条合起来说明：store 的形状是"一份预设一个文件 + 每次写都刷盘"，
单份成本不高，**份数一多、又在主线程上，就是秒级**。

### `nav:instances`：约 280 ms，第一次画中文时查字形回退

```
DWrite.dll（十几帧，没有导出符号）
Qt6Gui!QWindowsDirectWriteFontDatabase::populateFamily
Qt6Gui!QPlatformFontDatabase::fallbacksForFamily
Qt6Gui!QFontEngineMulti::ensureFallbackFamiliesQueried
Qt6Gui!QFontEngineMulti::stringToCMap
```

代价在字体引擎那一侧，不在我们的代码里；三趟都在 272–284 ms，很稳定。
和 [主页那颗箭头](../PERFORMANCE.md) / [界面正文里的回退字形](../PERFORMANCE.md)那两节是同一类成本。

### `scan:java`：约 100 ms，主线程等子进程

```
ntdll!ZwCreateUserProcess / ZwWaitForMultipleObjects
KERNELBASE!CreateProcessW / WaitForMultipleObjectsEx
Qt6Core!QProcess::waitForFinished
runProcess                          GlobalJavaSettings.cpp:95
verifyJavaExecutable                GlobalJavaSettings.cpp:132
GlobalJavaSettings::probeJavaMajorVersion                            GlobalJavaSettings.cpp:605
GlobalJavaSettings::scanLocalJavaVersions                            GlobalJavaSettings.cpp:843/865
```

同一步骤三跑分别 108 / 81 / 97 ms，卡进去的那一帧有时是建进程、有时是等进程退出。
也就是说这一步**天然是同步的**：它在主线程上把 java 拉起来问版本再等它退出。

### `tasks:open-200`：约 80 ms，造 200 行任务控件

```
Qt6Widgets!QLabel / QFrame 构造
Qt6Widgets!QWidget::ensurePolished → QLayout::totalSizeHint → QWidgetItem::geometry
ElidedLabel::ElidedLabel(QWidget*)   ui/ElidedLabel.hpp:27
TaskCenterPanel::showEmbedded()      ui/TaskCenterPanel.cpp:649
```

栈里还夹着 `QScrollBar` 的样式动画帧和内联进来的析构——都在"造 200 行控件、
逐层问 sizeHint"这条路上。

### `nav:settings` 那一次 2.9 s（没复现）

```
ntdll!ZwCreateThreadEx → KERNELBASE!CreateRemoteThreadEx → KERNEL32!CreateThread
Qt6Core!QThread::start → QThreadPoolPrivate::startThread → QThreadPool::start
JavaSettingsController::queueDiscovery(...)   src/controllers/JavaSettingsController.cpp:172
JavaSettingsController::refreshList()         src/controllers/JavaSettingsController.cpp:131
MainWindow::onMainTabChanged()                src/MainWindow.cpp:4462
```

`refreshList()` 只是把发现作业丢进线程池，本身不该慢；这一次却卡在"建线程"里
2.9 s。同一份 exe 后来 8 跑 `nav:settings` 都在 10.1–12.5 ms（相差两个数量级），
也没有证据把它归到采样器头上（开关两组同量级），经过见
[多任务负载](P2_CONCURRENT_LOAD_20261101.md)。没复现，也没有证据指向我们
的代码，所以只记现象：**下一次再撞见，先看是不是同一处栈，再考虑是不是扫描软件
在拦线程创建**。栈被 `kMaxFrames = 24` 截断，最底下几帧看不到。

## 采样器本身的代价

- `gapMs` 比 `actionMs` 大 1.9–86.6 ms（三跑 22 份栈，中位 25.6 ms）。差值里含 10 ms 轮询粒度、
  暂停/走栈/放行的开销，以及"心跳是看门狗打"带来的相位差。没有哪一份出现
  数量级的放大。
- 暂停期间不查符号（只 `StackWalk64`），放行之后才 `SymFromAddr`——这是刻意的，
  见文件头的注释。
- 只在探针构建里存在：`WTFML_PERF_PROBE` 打开才会编 `StallSampler.cpp`
  并链 `dbghelp`，产品构建里一行都没有。

## 踩过的坑（写下来省下一次）

1. **`SYMBOL_INFO::Name` 声明成 `CHAR Name[1]`**。直接
   `QString::fromLatin1(symbol->Name)` 会被 Qt 6 挑成"按数组长度取"的那个重载，
   长度恒为 1——量到的是一串首字母（`C`、`S`、`N`）。先退化成
   `const char *` 并带上 `NameLen` 才对。这个坑的样子很迷惑：同一个地址用
   `printf("%s")` 打出来是完整的 `CreateProcessInternalW`，一进 QString 就剩 `C`。
2. **栈里除第一帧外都是返回地址**，指向调用指令的下一条。不 `-1` 就查，会落到下一行
   甚至下一个函数头上，表现为满屏 `~QString()`、`~QList()` 这种噪声。第一次跑出来的
   栈有一半是这种垃圾。
3. **`addr2line` 要 `-i`**：取内联链的**最后**一行才是真正写这段代码的函数
   （前面几行是它内联进去的小函数）。配合 `-a` 才能按地址分组解析一批。
4. **dbghelp 对系统 DLL 只有导出表**：地址落在某个导出函数中间时，它给的是
   "最近的导出 + 位移"。所以报告里的系统帧写成 `符号+0x偏移`，别把偏移大的那些
   当成精确的当前函数（`QScrollbarStyleAnimation::qt_metacall+0x178f1` 显然是别的函数）。
5. **GCC 13.1 不支持 `-gcodeview`**（`unrecognized debug output level 'codeview'`），
   所以拿不到 dbghelp 能直接吃的 PDB，本程序的帧只能事后用 `addr2line` 解析
   （它认 DWARF）。这决定了工具形状：采样器只记 RVA，符号化离线做。
6. **PowerShell 里 `[uint64]` 变量 `--` 会变成 `Decimal`**，然后 `'{0:x}' -f` 报
   "Format specifier was invalid"。改成 `[uint64]($x - 1)`。

## 没验到 / 边界

- 只在**探针步骤**里采过栈，没有在真机上手点界面时采样（探针步骤覆盖的是同一批
  入口，但不是用户鼠标操作的那一条时序）。
- 阈值写死 50 ms、一次卡顿一份、最多 32 份；更细的分布（同一段卡顿里各帧的概率）
  没有统计。
- `kMaxFrames = 24`：`nav:settings` 那次栈底被截断；主线程在很深的事件循环里的栈
  可能看不到 `main`。
- Windows 系统 DLL 的帧没有可靠符号，只有导出名 + 位移；`DWrite.dll` 这类完全没符号。
- 没有做"按栈聚合"的统计报表（同一个栈出现几次是脚本里按帧串去重数出来的）。
