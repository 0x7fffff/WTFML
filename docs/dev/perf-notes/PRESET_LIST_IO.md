# 预设列表的读盘：摘要缓存、并行冷读、盘上摘要

给改 `Presets::list()`（`src/presets/PresetStore.cpp`）或写预设相关测量的人看：列表怎么避免重复读盘、冷读怎么并行、摘要怎么落盘，以及测量时容易踩的"缓存已热"陷阱。耗时基线见 [PERFORMANCE.md](../PERFORMANCE.md)，采栈方法见 [MAIN_THREAD_STALLS.md](MAIN_THREAD_STALLS.md)。

三步都在 2026-11-01 / 11-02 落地，数据来自同一份探针 exe、`-Instances 200`、1000 份预设：

| 日期 | 做了什么 | `presets:read-store-1`（冷） | 热读 | 下次启动首趟 |
| --- | --- | --- | --- | --- |
| 11-01 前 | 一份一份读 manifest | 2351–2367 ms | 2292–2335 ms | — |
| 11-01 | 进程内摘要缓存 | 2273–2321 ms | 11.4–15.4 ms | — |
| 11-01 | 缓存未命中的 manifest 并行读 | 394.9 / 398.8 / 407.1 ms | 11.5–12.4 ms | — |
| 11-02 | 摘要落盘 `.preset-summaries.json` | 441.8 ms（一跑） | 12.1 / 11.3 ms | **18.4 ms** |

## 现行做法

`Presets::list()` 分两趟：

1. **只 stat**：按"下标次序 + 盘上多出来的（排序后追加）"定次序，对每份 manifest 先 `QFileInfo`（一份十来微秒），问摘要缓存命中没有。命中的直接占格子。
2. **只读没命中的**：`loadManifests(root, missIds)` 一次读完，按次序落回格子，再统一进缓存、统一列出来。

`loadManifests()` 在份数 >= 8（`kParallelThreshold`）且全局线程池不止一个线程时才 `QtConcurrent::blockingMapped`。少的时候串行（起线程比读几份文件还贵）；池只剩一个线程时也串行（调用方可能就在池线程上，等池线程会互等）。`blockingMapped` 保序，坏了/不在的按同一次序进 `broken`。

### 进程内摘要缓存

- 键是 manifest 完整路径，判定新鲜用 **`(字节数, 修改时刻)`** 两格一起比。
- 只缓存摘要（`PresetSummary` 那几格 + "是不是内部部件"标志），不缓存 manifest 正文：正文是"应用到实例"那条路要用的，读到旧的不行。
- 不缓存 `isDefault`：它由每次调用的下标决定，而缓存跨调用存活。
- 读不回来的那份不进缓存：多半是没写完或正被改，记住错误只会让下一趟也看不见。
- 本进程写盘时显式作废那一格（`writeBytesAtomic` 提交成功后 `forgetSummaryCacheEntry`），不靠时间戳兜底：同一毫秒里先写后读，时间戳可能看不出变化。
- 带 `QMutex`：`Presets::CatalogLoader` 在 `QtConcurrent` worker 上调 `list()`，界面线程也在调。锁只圈哈希的查/插，不圈文件 I/O，所以两趟冷读同时进来会各读一遍，是多读一趟、不是错。
- 上限 4096 份（`kSummaryCacheEntriesLimit`），到顶整张丢掉重来。
- 不跨进程，删掉进程就没了。

### 盘上摘要（2026-11-02）

路径 `<root>/.preset-summaries.json`（`Presets::summaryCachePath()`）。格式以 `PresetStore.cpp` 里那段注释为准，当前 `version` 为 2，文件上限 8 MiB（`kSummaryCacheBytesLimit`）：

```json
{"format":"wtfml.preset-summaries","version":2,
 "entries":[{"id":"<uuid>","size":1234,"mtime":1700000000000,"listed":true,
             "name":"…","description":"…","updatedAt":"…","sections":3,
             "category":"mods","components":["mods"],"icon":"…"}]}
```

- 判据与进程缓存相同：字节数和毫秒时间都相等才用。
- 格式标记不认识、版本比我们新、某项缺字段、类别认不出：整项/整份当没有缓存，照旧读 manifest。缓存坏了只会变慢，不会变错。
- 本进程写过的 manifest 记在一张表里，灌盘上缓存时跳过（同字节数同一毫秒时 stat 认不出新旧，只能靠写侧作废）。
- `list()` 只在真有新读回来的时候重写这份文件，全是命中时不写。
- `forgetSummaryCache(root)` 连盘上那份一起删（要测真冷读用它）；`forgetSummaryCache(root, true)` 只清进程里那张（测"下一次启动"用它）。产品代码里没人调。
- 下一次启动的成本是 1000 次 stat + 读一份摘要文件，不碰 manifest 正文。

## 限制

- 外部改文件靠 `(大小, 修改时刻)` 判定，时间分辨率取决于文件系统：NTFS 细、FAT32 是 2 秒、网络盘可能有自己的缓存。外部在 2 秒内改两次且字节数相同，会读到旧摘要，直到时间戳再变。没实测过。
- 并行只覆盖"读 manifest"一段；stat、拼摘要、进缓存、拼列表仍串行（1000 份 stat 约 2–3 ms）。
- 并行占全局线程池，别的 `QtConcurrent` 作业同时跑时分线程，没专门压测。
- 写那一半没动：1000 份 `saveManifest()` 约 1.7 s（1680–1802 ms），每次 `QSaveFile::commit()` 都 `FlushFileBuffers`。产品里没有一次写 1000 份的路（只有探针造数），合并/跳过 flush 等于拿掉原子替换的耐久性，所以不动。

## 测量陷阱

1. **热缓存让"多任务 A/B"空转**。加缓存后 `presets:start-background-store`（真 `CatalogLoader`）从 2.3 s 变成 11 ms，因为前面三趟 `read-store` 已经把缓存捂热，`concurrent:busy-*` 没压在读盘窗口上，`busyDuringLoad` 从 8/8 掉到 0/8，数据看着正常实际什么都没测到。探针在该步骤前调 `Presets::forgetSummaryCache(m_storeDir->path())`，三跑 `catalogElapsedMs` 回到 2314–2357 ms、8/8。**加了缓存之后，任何"它很慢"的测量都要先问是不是冷的。**
2. **并行后 busy 样本缩水**。加载只剩 0.4 s，`concurrent:busy-*` 8 发里只有 2 发落在窗口内，其余是空载的数，混进平均会把 busy 拉回 calm。探针改成只把 `loadActive` 的算进 `busy`，样本数 8 → 2，每跑：空载 3.2–3.5 ms、加载中 9.1–10.7 ms。
3. **`slots` 是 Qt 的空宏**（`private slots:`）。变量名叫 `slots` 会被预处理成 `QList<Slot> ;`，报 `declaration does not declare anything`，接着 `slots[i]` 被当成 lambda 捕获表，一路怪错。slot / slots / signals / emit 开头的标识符都要躲。相邻的坑：`QList<Slot> slots(ordered.size());` 是函数声明（most vexing parse），改成先声明再 `resize()`。

## 验收

`tests/core_modules/PresetStoreTests.cpp`：

- **PA12**（12 条）：五条失效路径各一条，外加"热的那趟真走了缓存"。外面改一份、字节数不变，只靠修改时刻也看得见（测试显式把 mtime 往后拨）；本进程写一次后把 mtime 拨回去、大小也一样，照样看得见新名字（证明靠的是写盘作废）；坏掉那一趟掉出列表、修好下一趟回来；换默认项下一趟生效；内部部件不进列表，改成普通预设后下一趟列出。
- **PA13**（5 条）：12 份一次列全、次序就是下标次序（不是读完的先后）、改过的那份照样看得见、坏一份只掉它自己。
- **PA15**（11 条，11-02）：盘上摘要能灌回来（manifest 坏着也照列）、字节数对不上就重读、本进程写过的同大小同毫秒也拿旧名字顶不掉、格式标记不认识就当没有缓存、`forgetSummaryCache()` 两档删不删盘上那份。
- `verify_presets` 838/838（11-02 当时）。

```powershell
ctest --test-dir build/verification-mingw13 -R core-presets --output-on-failure
```

### 偶发红：`wtfml.download-retry` 的"正文上限"判据（同一天修了）

整轮 `ctest` 里 `RequestBudgetChecks.cpp` 的 "a per-job body limit …" 约 1/6 概率红。最早怀疑 `ControlGet` 里 `readyRead` 的 `abort()` 和 `finished` 抢先后，查下来是判据自己写错：`normal` 初值 `true`（"还没看见失败"），`wait()` 等 `oversized && normal`；`control` 那发比 `fast` 回得晚时，`wait()` 在它回来之前就满足，紧接着 `snapshot().control==0` 读到 1。用 `git stash` 掉本轮改动、重编 HEAD 二进制对照过（6 跑红 1 跑）。修法：`normal` 从 `false` 起，并把"`control` 回调来过"也算进 `wait()` 条件。修完连跑 20 次，整轮 `ctest` 39/39。

## 复现

```powershell
# 1) 编探针
cmake -S . -B build/tmp-wtfml-probe -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=D:/Qt/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_BUILD_TYPE=Release -DWTFML_PERF_PROBE=ON -DWTFML_BUILD_TESTS=OFF
cmake --build build/tmp-wtfml-probe --target WTFMinecraftLauncher -j 8

# 2) 跑（看 read-store-1 / read-store-from-disk-cache / start-background-store）
pwsh -File tools/perf-startup.ps1 -Probe `
  -Exe build/tmp-wtfml-probe/WTFMinecraftLauncher.exe `
  -Runs 3 -Warmup 0 -Instances 200 -InstanceMeta -StallSeconds 2 `
  -ReportDir build/tmp-wtfml-parallel-list3
```

## 附录：三步实测

**11-01 摘要缓存**（3 跑）：冷 2273–2321 ms（改前 2351–2367）；热 11.4–11.9 / 11.4–15.4 ms（改前 2306–2335 / 2292–2308），约 200×；写 1000 份 1744–1802 ms（改前 1670–1694，无关）。

**11-01 并行冷读**（3 跑）：冷 2.3 s → 0.40 s（约 5.7×）；`presets:start-background-store`（先丢缓存）402 / 417 / 424 ms（原 2314–2357）；写 1000 份 1680.7 / 1713 / 1716.7 ms（没动）。主线程最大一停不再是 `read-store-1`（原两跑 2331 / 2271 ms），后台加载期间界面最大间隔仍是 37–47 ms，`backgroundPresetStore.guiTicks` 从 74–76 掉到 12–14，是窗口变短，不是界面变卡。

**11-02 盘上摘要**（`build/perf-preset-cache-20261102/`，一跑）：冷读 441.8 ms → 下一次启动 18.4 ms（约 24×）。再往下压要靠文件系统目录快照或更粗的失效判据，不值当。
