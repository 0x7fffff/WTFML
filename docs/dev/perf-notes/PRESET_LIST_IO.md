# P2：预设列表的冷读改成并行（2026-11-01）

[上一轮](P2_PRESET_LIST_CACHE_20261101.md)把**重复**那一半解决了（同一份目录第二次问从
2.3 s 掉到 11 ms），冷的第一趟还是 **2.3 s**：1000 份 manifest 一份一份地开、读、解析。
这一轮解决冷的那一半 —— 先把缓存没命中的那些攒起来，再**一起读**。

数字归 [性能记录](../PERFORMANCE.md)，这份文档记形状、数据、边界和踩到的坑。

## 改了什么

`Presets::list()`（`src/presets/PresetStore.cpp`）从"一份一份地边读边列"改成两趟：

1. **第一趟只 stat**：按"下标次序 + 盘上多出来（排序后追加）"定下这一趟的次序，对每份
   manifest 先 `QFileInfo`（一份十来微秒），问摘要缓存命中没有。命中的直接占好格子。
2. **第二趟只读没命中的那些**：`loadManifests(root, missIds)` 一次读完，结果按次序落回
   各自的格子，再统一进缓存、统一列出来。

`loadManifests()` 只在**份数 >= 8 且全局线程池不止一个线程**时才
`QtConcurrent::blockingMapped`；少的时候串行（起线程比读几份文件还贵），池只剩一个线程时
也串行（调用方自己就在池线程上，等池线程会互等）。

结果次序不依赖"谁先读完"：`blockingMapped` 保序，回来按 `missIds` 的下标落格子；
坏了/不在的那几份按同一次序进 `broken`。

## 实测（同一份探针 exe，`-Instances 200`，1000 份预设，3 跑）

| 步骤 | 上一轮（只有缓存） | 这一轮（缓存 + 并行冷读） |
| --- | --- | --- |
| `presets:read-store-1`（冷） | 2273–2321 ms | **394.9 / 398.8 / 407.1 ms** |
| `presets:read-store-2/3`（热） | 11.4–15.4 ms | 11.5–12.4 ms |
| `presets:start-background-store`（真 `CatalogLoader`，先丢缓存） | 2314–2357 ms | **402 / 417 / 424 ms** |
| `presets:prepare-store-1000`（写 1000 份） | 1744–1802 ms | 1680.7 / 1713 / 1716.7 ms（没动） |

冷读 **2.3 s → 0.40 s**（约 5.7×，同一个进程、同一批文件）。热读没变 —— 缓存那一层照旧。

主线程那边：上一轮 `stalls.maxStallMs` 有两跑落在 `presets:read-store-1` 上（2331 / 2271 ms），
这一轮它不再是最大的一停（剩下的最大一停是写 1000 份那步的 1.7 s，以及那条还没查清的
`nav:settings` 首次 1.7–3.4 s）。后台目录加载期间的界面最大间隔仍是 37–47 ms（上一轮 38–41 ms），
只是加载窗口从 2.3 s 缩到 0.4 s，所以 `backgroundPresetStore.guiTicks` 从 74–76 掉到 12–14 ——
那是窗口变短，不是界面变卡。

## 为什么值得做

预设页、安装弹窗、`availablePresets()` 每一次**冷**开都吃这 2.3 s。缓存只对"同一个进程里问
第二次"有效；用户开机后第一次点下载 → 安装弹窗，或者第一次点预设页，永远是冷的。

## 踩到的坑

1. **`slots` 是 Qt 的空宏**（`private slots:` 那个）。变量名起了 `slots`，
   `QList<Slot> slots;` 被预处理器抹成 `QList<Slot> ;`，报的是
   `declaration does not declare anything`；接着 `slots[i]` 又被当成 lambda 的捕获表，
   一路 CS0188 式的怪错。**凡是叫 slot/slots/signals/emit 的标识符都先想一下。**
   另一个相邻的坑：`QList<Slot> slots(ordered.size());` 是**函数声明**（most vexing parse），
   于是写成"先声明再 `resize()`"。
2. **缓存把"多任务 A/B"的样品缩水了**。上一轮修的坑是"热缓存让后台加载空转"（先
   `forgetSummaryCache`）；这一轮加载真变快之后，`concurrent:busy-*` 那 8 发里只有 **2 发**
   落在加载窗口里（上一轮 8/8）—— 剩下 6 发是空载的数，混进平均数会把 `busy` 拉回 `calm`，
   看着"多任务不挡事"。探针改成**只把 `loadActive` 的那几发算进 `busy`**，样本数从 8 掉到 2，
   数字是诚实的小样本：空载 3.2–3.5 ms、加载中 9.1–10.7 ms（每跑 2 发）。

## 边界

- **并行只覆盖"读 manifest"这一段**：第一趟 stat 仍是串行的（1000 份约 2–3 ms），
  拼摘要、进缓存、拼列表也都是串行的。
- **份数少于 8 一律串行**：小目录（常态：个位数预设）行为与以前完全一样，不付线程池的价。
- **占的是全局线程池**：别的 `QtConcurrent` 作业同时跑时，两边分线程。预设列表的读盘
  只是打开 + 读 + 解析小文件，抢不到也不至于把谁饿死；这条没专门压测过。
- **跨进程那份摘要**（2026-11-02 增补，见下）只省"下一次启动的第一趟"：同一进程里热读
  照旧是内存那张；盘上那份删掉只会让下一趟多读 manifest，没有任何数据后果。

## 验收

- `tests/core_modules/PresetStoreTests.cpp` 的 **PA13**（5 条）：12 份一次列全、
  **次序就是下标的次序**（不是读完的先后）、改过的那一份照样看得见、坏一份只掉它自己、
  别的十一份照列。PA12 那 12 条老判据（缓存失效五条路）继续全绿。
- **PA15**（11 条，2026-11-02）：盘上摘要能灌回来（manifest 坏着也照列）、字节数对不上
  就重读、本进程写过的同大小同毫秒也拿旧名字顶不掉、格式标记不认识就当没有缓存、
  `forgetSummaryCache()` 两档删不删盘上那份。
- `verify_presets` **838/838**（上一轮 776 条 + PA13 的 17 条 + PA15 的 11 条等）。
- 整轮 `ctest`：`wtfml.download-retry` 里那条 "a per-job body limit …" 会偶发红
  （约 1/6，**HEAD 的二进制同样会红**，见下面"这不是这一轮引入的"），其余全绿。

### 那条偶发红（不是这一轮引入的，同一天修了）

整轮 `ctest` 里 `wtfml.download-retry` 的 `RequestBudgetChecks.cpp` 那条
"a per-job body limit …" 会偶发红，约 1/6。最早以为是 `ControlGet` 里
`readyRead` 的 `abort()` 和 `finished` 抢先后 —— **查下来是判据自己写错了**：

`normal` 一开始就是 `true`（意思是"还没看见失败"），而 `wait()` 等的是
`oversized && normal`。如果 `control` 那一发比 `fast` 那一发回来得晚，
`wait()` 在它回来之前就满足了，紧接着的 `snapshot().control==0` 读到 1 —— 红。
用 `git stash` 掉本轮改动、重编同一份 HEAD 二进制对照过（6 跑红 1 跑），
确认不是本轮引入的；诊断打印也证实失败那一跑 `control` 的回调根本没跑过。

修法是把 `normal` 从 `false` 起、并把"`control` 回调来过"也算进 `wait()` 的条件里。
修完连跑 20 次、整轮 `ctest` 39/39。

## 复现

```powershell
# 1) 编探针
cmake -S . -B build/tmp-wtfml-probe -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=D:/Qt/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_BUILD_TYPE=Release -DWTFML_PERF_PROBE=ON -DWTFML_BUILD_TESTS=OFF
cmake --build build/tmp-wtfml-probe --target WTFMinecraftLauncher -j 8

# 2) 跑（看 read-store-1 / start-background-store 两行）
pwsh -File tools/perf-startup.ps1 -Probe `
  -Exe build/tmp-wtfml-probe/WTFMinecraftLauncher.exe `
  -Runs 3 -Warmup 0 -Instances 200 -InstanceMeta -StallSeconds 2 `
  -ReportDir build/tmp-wtfml-parallel-list3

# 3) 验收
ctest --test-dir build/verification-mingw13 -R core-presets --output-on-failure
```

证据：`build/tmp-wtfml-parallel-list3/`（三跑，上表就是它），临时文件，不入库。

## 增补（2026-11-02）：摘要缓存落盘

并行之后冷读 0.40 s，但**那是每个进程付一次**：用户开机后第一次点预设页 / 安装弹窗，
仍然要等它。这一轮把上一次读出来的摘要写在盘上（`<root>/.preset-summaries.json`，
路径由 `Presets::summaryCachePath()` 给出），下一次启动只要 stat 一遍每份 manifest，
对得上的直接拿摘要。

形状（`PresetStore.cpp` 里那段注释是唯一出处）：

```json
{"format":"wtfml.preset-summaries","version":1,
 "entries":[{"id":"<uuid>","size":1234,"mtime":1700000000000,"listed":true,
             "name":"…","description":"…","updatedAt":"…","sections":3,
             "category":"mods","components":["mods"],"icon":"…"}]}
```

- 判据和进程里那张缓存**一模一样**：字节数 + 毫秒都相等才用；
- 任何一格不对（格式标记不认识、版本比我们新、某一项缺字段、类别认不出）⇒ 整项/整份当
  没有缓存，照旧读 manifest。缓存坏了**只许变慢，不许变错**；
- 本进程写过的 manifest 记在一张表里，灌盘上缓存时跳过（同字节数、同一毫秒时 stat 认不出
  新旧，只能靠写侧作废）；
- `list()` 只在**真有新读回来的**时候重写这份文件；全是缓存命中时一个字节都不写；
- `forgetSummaryCache(root)` 连盘上那份一起删（"冷读"测量还要的是真冷）；
  `forgetSummaryCache(root, true)` 只清进程里那张，量"下一次启动"走这一档。

同一份探针 exe、1000 份预设（`build/perf-preset-cache-20261102/`，一跑）：

| 步骤 | 动作 ms |
| --- | ---: |
| `presets:read-store-1`（冷，读 1000 份 manifest） | 441.8 |
| `presets:read-store-2/3`（热，进程缓存） | 12.1 / 11.3 |
| `presets:read-store-from-disk-cache`（下一次启动：盘上摘要 + 1000 次 stat） | **18.4** |

冷读 0.44 s → 18 ms（约 24×）。这一档的代价主要是 1000 次 stat 和读一份摘要文件，
不再碰 manifest 正文。

## 没做

- **写那一半还在 1.7 s**（见上一轮文档的"没做"）：那是 `QSaveFile::commit()` 每次
  `FlushFileBuffers`。产品里没有一次写 1000 份的路（这个量只出现在探针造数），
  而把多个文件的 flush 合并/跳过就是拿掉原子替换的耐久性 —— 不为了探针数字动它。
- **摘要文件本身还能再省**：18 ms 里 stat 占大头；再往下要么靠文件系统目录快照，
  要么接受更粗的失效判据，都不值当。
