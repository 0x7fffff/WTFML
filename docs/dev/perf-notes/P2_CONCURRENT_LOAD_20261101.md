# P2：后台干活的时候界面还点得动吗（2026-11-01）

计划里 §4「运行/构建剩余测量」的最后一格：**实际多任务负载**。以前的探针是串行的，
每一步单独跑、互不重叠；更糟的是后台目录加载那一步会把后面的步骤**全挡住**
（`runNext()` 里"等异步目录回来"）。所以"用户在预设页转圈的时候点翻页"这种事
一次都没量过。

数字归 [性能记录](../PERFORMANCE.md)，这份文档记方法、数据和边界。

## 结论

- 后台目录加载（1000 份 manifest，2.3 s，QtConcurrent worker）**跑着的时候**点翻页：
  每次 3.6–3.9 ms，空载同一串动作 2.9–3.3 ms。**多 0.5–0.8 ms**，10 跑 10 次都是
  "加载中更慢"，方向稳定；绝对值在 4 ms 以内，肉眼看不出来。
- 整窗重画同样稳定地多 0.5–0.9 ms（3.3–3.5 → 3.9–4.8 ms）。
- 加载那段时间主线程心跳最大间隔 37–44 ms（采栈阈值 50 ms），**一份卡顿栈都没采到**：
  这一趟没把界面顶住。
- 对照很刺眼：同一个 store **同步**读一趟是 2.3 s（见
  [卡住时的调用栈](P2_STALL_STACKS_20261101.md)）。差三个数量级——后台化这条是对的。
- 顺手排除了一个嫌疑：把采样器关掉再跑 8 次，`nav:settings` 的耗时两边都在 10–12.5 ms，
  **没有证据**说采栈把某一步拖慢了。

## 怎么改的探针

`src/perf/PerfProbe.cpp` 三处：

1. **不再让后台目录挡路**。`runNext()` 原来在 `m_asyncCatalogPending` 时原地等；
   现在只有"步骤全走完、后台还没回来"时才等（有上限），中途照常发下一步。
2. **同一串翻页动作跑两趟**：`concurrent:calm-01..08`（空载，对照）→
   `presets:start-background-store`（起真正的 `Presets::CatalogLoader`）→
   `concurrent:busy-01..08`（加载中）。两趟都是 home → instances → download → settings
   转两圈，同一份 exe、同一份报告，不跨机器比。
3. **每步记 `loadActive`**：这一步点下去时后台到底还在不在读。报告里
   `concurrentWorkload.busyDuringLoad` 说明"加载中"那趟有几发是真的在加载中。
   两趟谁都没"假装"——10 跑全是 8/8。

报告新增：

```json
"concurrentWorkload": {
  "source": "nav-cycle",
  "calm": {"count": 8, "avgMs": 3.0, "maxMs": 4.4},
  "busy": {"count": 8, "avgMs": 3.6, "maxMs": 4.2},
  "busyDuringLoad": 8,
  "catalogElapsedMs": 2349
}
```

另加一个开关：`WTFML_PERF_NO_STALL_SAMPLER=1` 只停采栈、保留看门狗（心跳 / 最大间隔），
用来分清"某一步慢"是产品的还是测量工具的。

## 数据（10 跑，同一份 exe，`-Instances 200`，200 个实例）

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

"空载重画"那一列有一格 4.9 ms（比它自己的加载中那趟还高），是这十跑里唯一违反
"加载中更慢"趋势的值——单发噪声，所以趋势只说均值。

后台那一趟的 `catalogElapsedMs` 在 2.3–2.4 s 之间，`rows` 都是 1000，
和"加载中那一趟真的压在整个读盘窗口上"（`busyDuringLoad = 8/8`）一致。

## 采样器开关那 8 跑（顺手排掉一个嫌疑）

`nav:settings` 是同一份报告里最可疑的一步（下面那节），这里只列它的 `actionMs`：

| 采样器 | 四跑 `nav:settings` |
| --- | --- |
| 开 | 12.5 / 11.4 / 11.1 / 11.5 ms |
| 关 | 10.5 / 11.7 / 11.5 / 10.1 ms |

差不到 2 ms，而且开关两边都在同一个量级。结论只有一句：**没有证据**说明采栈拖慢了
被测量的步骤；这不等于"采样器零成本"（它的开销在
[卡住时的调用栈](P2_STALL_STACKS_20261101.md)里单独量过）。

## 当时没解释的：第一次进设置页偶发 2.9 s（同一天查过了）

第一次点设置页有时 10 ms、有时 2.9 s，栈停在 `QThreadPool::start` → `QThread::start`
→ `CreateThread`。这一轮把它量成"建线程那一下偶尔要 3 s"：不止我们自己的 Java 发现，
Qt 自己在绘制里起线程也撞上过；普通控制台程序连建 240 个线程一次没慢过；提前预建线程
只是把卡顿挪到 `nav:instances`，不是修好。**产品代码没动**，证据与下一步见
[起一个线程偶尔要 3 秒](P2_THREAD_CREATE_STALL_20261101.md)。

## 没覆盖到的

- 后台作业只有**一种**：预设目录读盘（1000 份 manifest）。真实的多任务还包括下载、
  实例扫描、启动游戏——下载要网络，这次没做；多路并发（几个作业同时跑）也没测。
- 每趟 8 发、每发 3–4 ms，样本量和绝对值都小；只报均值/最大，不做显著性。
- 只量了"点下去这一下"和"整窗重画"，没量动画在加载中的掉帧。

## 复现

```powershell
# 构建（探针 + -g）
cmake -S . -B build/tmp-wtfml-probe -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=D:/Qt/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_BUILD_TYPE=Release -DWTFML_PERF_PROBE=ON -DWTFML_BUILD_TESTS=OFF
cmake --build build/tmp-wtfml-probe --target WTFMinecraftLauncher -j 8

# 跑（-Runs 4 就是四跑，报告按 run-NN-probe.json 落盘）
pwsh -File tools/perf-startup.ps1 -Probe `
  -Exe build/tmp-wtfml-probe/WTFMinecraftLauncher.exe `
  -Runs 4 -Warmup 0 -Instances 200 -InstanceMeta -StallSeconds 2 `
  -ReportDir build/tmp-wtfml-concurrent-run

# 只关采栈（看门狗照旧），用来做"是不是测量工具拖慢"的对照
$env:WTFML_PERF_NO_STALL_SAMPLER = '1'
```

证据：`build/tmp-wtfml-concurrent-run/`、`build/tmp-wtfml-sampler-{on,off}/`
（都是临时文件，不入库）。
