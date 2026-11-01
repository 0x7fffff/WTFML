# P2：起一个线程偶尔要 3 秒（2026-11-01）

[上一轮](P2_CONCURRENT_LOAD_20261101.md)留了一条没解释的现象：同一份 exe、同一个
`nav:settings`（第一次点设置页），有时 10 ms、有时 **2.9 s**，栈停在
`QThreadPool::start` → `QThread::start` → `CreateThread`。这一轮把它量清楚了：

- 现象 = **建线程那一下卡住**，不是我们哪一段代码慢；
- **不止一个调用点**：我们自己的 Java 发现，和 Qt 自己在绘制里起的线程，都撞上过；
- 一个普通 Qt 控制台程序连建 240 个线程，一次都没慢过 ⇒ 与"这台机器 + 这个进程"有关；
- 提前把线程池那颗线程建好，**设置页那一下不再卡了（0/20）**，但同样的卡顿又在
  `nav:instances` 冒出来（2/20）—— **转移，不是修好**。

所以这一轮没有改产品代码，只把证据钉下来。数字归 [性能记录](PERFORMANCE.md)。

## 现象长什么样

同一份探针 exe、`-Instances 200`，`nav:settings`（第一次点设置页；`refreshList()` 有
`m_scanned` 闸，第二次以后不再扫）：

| 组 | 跑数 | `nav:settings` | 其中 > 1 s |
| --- | --- | --- | --- |
| 不加任何东西 | 20 | 6 跑 2.91–3.41 s，其余 10–11 ms | **6/20** |
| 第一步先起一颗线程池线程（临时探针步骤） | 20 | 全部 9.0–10.7 ms | **0/20** |
| 同上，但看**任何**一步 | 20 | —— | **2/20**（都在 `nav:instances`，2.4 s / 3.0 s） |

两种形态是**双峰**的：要么 ~10 ms，要么 2.9–3.4 s，中间没有过渡带，值的聚集也在 3 s 附近。

## 栈：两次落在不同的调用者身上

```
ntdll!ZwCreateThreadEx → KERNELBASE!CreateRemoteThreadEx → KERNEL32!CreateThread
Qt6Core!QThread::start → QThreadPoolPrivate::startThread → QThreadPool::start
JavaSettingsController::queueDiscovery   src/controllers/JavaSettingsController.cpp:172
```

```
ntdll!ZwCreateThreadEx → KERNELBASE!CreateRemoteThreadEx → KERNEL32!CreateThread
Qt6Core!QThread::start → QThreadPoolPrivate::startThread → QThreadPool::start
Qt6Gui!…（绘制路径，Qt 自己在 QImage / PaintEngine 一带）
Animations::paintEvent   ui/Animations.cpp:305
```

第二份来自 `build/tmp-wtfml-controlprobe/run-01`：主线程正在画动画换页时，Qt 内部往全局
线程池丢了一个作业，卡了 3.0 s。**这不是"某一段产品代码慢"，是"这一下建线程慢"。**
（`stalls.maxGapAtStep` 记的是心跳间隔结束在哪一步，采样那一份的 `step` 会差一格。）

## 不是"这台机器建线程都慢"

`build/tmp-wtfml-thread-probe/`（临时，不入库）里一个最小 Qt 控制台程序：

- 第一发 `QtConcurrent::run` 0–1 ms、`QThread::start` 1–2 ms（12 跑）；
- 每 200 ms 建一个线程、连建 80 个，跑 3 趟共 **240 次**，没有一次超过 100 ms。

所以"建线程"本身在这台机器上是快的。慢的是**这个 GUI 进程在那一刻**建线程。

## 已知线索（没坐实）

- 机器上 `root/SecurityCenter2` 报了 **Kaspersky**（productState 266240）与 Windows Defender。
  杀毒软件 hook 线程创建（`PsSetCreateThreadNotifyRoutine`）时，`CreateThread` 会在内核里
  等它扫完；值的聚集在 3 s 附近很像"带超时的外部介入"。**没敢关防护去验证**（这条是硬约束）。
- 不是采样器的锅：上一轮已用 `WTFML_PERF_NO_STALL_SAMPLER=1` 对照过，开关两组
  `nav:settings` 都在 10–12.5 ms；这一轮"不开采样器就测不到卡顿"更没法对照。

## 试过、但没用的

第一步先 `QtConcurrent::run([]{})` 把全局池那颗线程建出来（探针临时步骤）。结果：
`nav:settings` 的 6/20 变成 **0/20**，可同样的 2.4–3.0 s 挪到了 `nav:instances`
（Qt 自己在绘制里起线程那一下，2/20）。合并看 2/20 对 6/20 不构成显著改善 ——
**预热的只是"那一次"，不是"这类"**。产品里加这种预热的收益靠不住，所以没加。

## 还没试的（下一步该往哪看）

1. **杀毒白名单**：把构建目录加进 Kaspersky 的排除项再跑 20 次。这要用户点头（改的是机器上
   别人的防护），而且不能靠"关防护"来做结论。
2. **干净虚拟机**：同一份 exe 在没有第三方防护的机器上跑 20 次。这是最干净的判据。
3. **ETW**：`Microsoft-Windows-Kernel-Process` / 第三方 minifilter 的事件，看 `CreateThread`
   那 3 s 里到底谁在占着。需要管理员权限。
4. 如果最后确认是外部介入：产品侧唯一能做的是"少在用户点击路径上建线程"（预建/复用线程池），
   但那只是把代价挪走 —— 本轮已经证明挪走之后还会在别处冒出来。

## 复现

```powershell
# 20 跑，看 nav:settings 那一列是不是双峰
pwsh -File tools/perf-startup.ps1 -Probe `
  -Exe build/tmp-wtfml-probe/WTFMinecraftLauncher.exe `
  -Runs 20 -Warmup 0 -Instances 200 -InstanceMeta -StallSeconds 2 `
  -ReportDir build/tmp-wtfml-controlprobe2

# 只要栈：把某一跑的报告符号化
pwsh -File tools/symbolize-stalls.ps1 `
  -ProbeJson build/tmp-wtfml-controlprobe2/run-01-probe.json `
  -Exe build/tmp-wtfml-probe/WTFMinecraftLauncher.exe
```

证据：`build/tmp-wtfml-{parallel-list3,cache-run2,navsettings-probe,controlprobe,warmprobe,warmprobe2}/`
与 `build/tmp-wtfml-thread-probe/`，都是临时文件，不入库。
