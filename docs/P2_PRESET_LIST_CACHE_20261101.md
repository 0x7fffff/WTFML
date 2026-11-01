# P2：预设列表的摘要缓存（2026-11-01）

上一轮的采栈把价格量出来了：`Presets::list()` 一份一份读 manifest，1000 份 **2.3 s**
（[主线程卡住时停在哪一行](P2_STALL_STACKS_20261101.md)）。这一轮解决它的**重复**那一半：
同一进程里第二次、第三次问同样一份目录，不再重新读盘。

数字归 [性能记录](PERFORMANCE.md)，这份文档记形状、数据、边界和踩到的坑。

## 改了什么

`Presets::list()`（`src/presets/PresetStore.cpp`）加一层进程内摘要缓存：

- **键 = manifest 的完整路径**，判定新鲜用 **`(字节数, 修改时刻)`** 两格一起比。
- **只缓存摘要**（`PresetSummary` 那几格 + 一个"是不是内部部件"的标志），不缓存
  manifest 正文。正文是"应用到实例"那条路上真正要用的东西，那条路多读一次没关系，
  读到旧的那一份不行。
- **不缓存 `isDefault`**：它由每一次调用的下标决定，而缓存是跨调用活着的。
- **读不回来的那一份不进缓存**：它多半是"还没写完"或者"正被改"，记住一个错误只会让
  下一趟也看不见它；修好之后下一趟自然会读回来。
- **本进程写盘时显式作废那一格**（`writeBytesAtomic` 提交成功后 `forgetSummaryCacheEntry`）：
  不靠时间戳兜底 —— 同一毫秒里先写后读、时间戳粒度粗一点，都可能让它看起来没变。
- 带 `QMutex`：`Presets::CatalogLoader` 在 `QtConcurrent` worker 上调 `list()`，界面线程
  也在调，两边会撞上。
- 上限 4096 份，到顶整张丢掉重来（这是缓存，不是数据）。

另加一个只给测量用的 `Presets::forgetSummaryCache(root)`：让下一趟真的读盘。产品代码里
不该有人调它（见下面"踩到的坑"）。

## 实测（同一份探针 exe，`-Instances 200`，200 个实例，3 跑）

| 步骤 | 改之前（3 跑） | 改之后（3 跑） |
| --- | --- | --- |
| `presets:read-store-1`（冷） | 2351–2367 ms | 2273–2321 ms |
| `presets:read-store-2`（热） | 2306–2335 ms | **11.4–11.9 ms** |
| `presets:read-store-3`（热） | 2292–2308 ms | **11.4–15.4 ms** |
| `presets:prepare-store-1000`（写 1000 份） | 1670–1694 ms | 1744–1802 ms |

- 冷的那趟没变（2.27–2.32 s，在噪声里）：**第一次读仍然要 2.3 s**，缓存解决的是重复。
- 热的那两趟从 2.3 s 掉到 **11–15 ms**，约 **200×**，每趟少 2.29 s。
- 写那 1.7 s 与这一轮无关，属于另一个格（见下面"没做"）。

## 为什么值得做

`Presets::list()` 在产品里有这几种问法（`src/controllers/PresetController.cpp`）：

- `availablePresets()`（`PresetController.cpp:1327`）—— 安装流程要的只读入口；
- `installPickerData()`（`PresetController.cpp:1392`）—— 每次打开安装页那个选择弹窗扫一趟；
- 预设目录的异步目录加载（`readCatalog` 里顺带列 `drafts`，`PresetController.cpp:154`）；
- 预设页每开一次都走一趟。

同一个进程里这些是**同一份目录反复问**。以前每问一次就把 1000 份 manifest 全读全解析；
现在只有第一趟付这个钱。

## 踩到的坑：热缓存会让"多任务 A/B"空转

改完之后，探针里 `presets:start-background-store`（真 `CatalogLoader`，1000 份）从
2.3 s 变成 **11 ms** —— 因为上面那三趟 `read-store` 已经把缓存捂热了。于是
`concurrent:busy-*` 那半组根本没压在读盘窗口上，报告的 `busyDuringLoad` 从 8/8 掉到 **0/8**：
数据看着"一切正常"，实际上什么都没测到。

修法是在探针那一步之前调 `Presets::forgetSummaryCache(m_storeDir->path())`。改完三跑
`catalogElapsedMs` 回到 2314–2357 ms、`busyDuringLoad = 8/8`，空载/加载中的对比
（3.0–3.1 → 3.7 ms）与上一轮一致。

教训写在这里：**加了缓存之后，任何"它很慢"的测量都要问一句"是不是冷的"**。

## 边界

- **不跨进程**：两个启动器实例各读一遍。这是刻意的 —— 磁盘上是唯一的事实，
  进程内缓存只负责"同一个进程别重复读"。
- **改文件的不是本进程时，靠 `(大小, 修改时刻)` 判定**。修改时刻的分辨率取决于文件系统：
  NTFS 细、FAT32 是 2 秒粒度、网络盘还可能有自己的缓存。极端情况下（外部在 2 秒内改两次、
  且字节数一样）会读到旧摘要，直到下一次它的时间戳再变。这条没实测过，只作为边界记下。
- **只缓存列表要的那几格**：改名、改说明、段数、类别、图标、内部部件标志之外的东西
  （比如段载荷）从来不经过这层。
- **没有"同一份只读一遍"的合并**：两趟冷读同时进来（界面线程 + 后台 worker）会各自
  把 1000 份读一遍。锁只圈住哈希的查/插，不圈文件 I/O，所以不会互相等；代价是极端
  情况下多读一趟，不是错。
- 缓存本身不写盘、不改任何文件格式，删掉进程就没了。

## 验收（`tests/core_modules/PresetStoreTests.cpp` 的 PA12，12 条）

五条失效路径各一条，外加"热的那趟真的走了缓存"：

- 外面改一份、**字节数不变** ⇒ 只靠修改时刻也看得见（测试里显式把 mtime 往后拨，
  不靠"写和读恰好不在同一毫秒"这种运气）；
- 本进程写一次之后**把 mtime 拨回去、大小也一样** ⇒ 照样看得见新名字
  （证明靠的是写盘作废，不是时间戳）；
- 坏掉 ⇒ 这一趟掉出列表；修好 ⇒ **下一趟**就回来；
- 换默认项 ⇒ 下一趟就换过来（`isDefault` 没进缓存）；
- 内部部件不进列表（第一趟填缓存、第二趟读缓存都不进），改成普通预设后下一趟就列出来。

`verify_presets` 776/776 通过。

## 复现

```powershell
# 1) 编探针
cmake -S . -B build/tmp-wtfml-probe -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=D:/Qt/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_BUILD_TYPE=Release -DWTFML_PERF_PROBE=ON -DWTFML_BUILD_TESTS=OFF
cmake --build build/tmp-wtfml-probe --target WTFMinecraftLauncher -j 8

# 2) 跑（看 presets:read-store-1/2/3 三行）
pwsh -File tools/perf-startup.ps1 -Probe `
  -Exe build/tmp-wtfml-probe/WTFMinecraftLauncher.exe `
  -Runs 3 -Warmup 0 -Instances 200 -InstanceMeta -StallSeconds 2 `
  -ReportDir build/tmp-wtfml-cache-run2

# 3) 验收
ctest --test-dir build/verification-mingw13 -R core-presets --output-on-failure
```

证据：`build/tmp-wtfml-cache-run/`（加作废钩子之前的两跑）、
`build/tmp-wtfml-cache-run2/`（三跑，上表就是它），都是临时文件，不入库。

## 没做

- **写那一半**：1000 份 `saveManifest()` 仍是 1.7 s，其中每次 `QSaveFile::commit()` 都
  `FlushFileBuffers`。要不要为"批量造/导入预设"改成分批刷盘，得先看那条路真实的份数
  （用户手点不会一次造 1000 份）。
- **冷读那 2.3 s**：预设页/安装弹窗第一次打开仍然要读全部 manifest。要真省掉它，得让
  列表只读一个预先算好的摘要文件（改盘上格式），比这一轮的改动大得多，先不动。
