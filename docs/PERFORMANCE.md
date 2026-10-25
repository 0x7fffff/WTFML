# 性能基线（构建与运行）

这份文档只放**数字和量法**。产品行为的约定在 `FEATURES.md`，验证工程的约定在
`docs/TESTING.md`。一条规则贯穿全部：

> 功能断言和性能基线分离；不能用 offscreen、调试器或未优化构建的速度代表产品性能。
> —— `docs/TESTING.md`「新增测试约定」

所以这里的每一条都注明构建档位（`Release`）和测量方式；验证工程（`verify*` /
`mw_check` / `ui_check`）按设计是 `-O0`，它们的耗时一律**不进**这份基线。

## 环境

| 项 | 值 |
| --- | --- |
| 机器 | Intel Core Ultra 9 275HX / 31.4 GB / Windows 11 家庭版 中文版 26200 |
| 编译器 | MinGW 13.1.0（`D:/Qt/Qt/Tools/mingw1310_64`） |
| Qt | 6.10.1（`D:/Qt/Qt/6.10.1/mingw_64`） |
| 构建 | MinGW Makefiles / `CMAKE_BUILD_TYPE=Release` / 并行 8 |
| 提交 | `1473e52` + 未提交工作区（`tools/perf-*.ps1`、`src/perf/`、本文档、`CMakeLists.txt` 里那个探针开关） |

不同并行度、不同机器、不同提交的数字不能横向比较。每一跑的报告里都记了这些字段。

## 一、构建耗时

```powershell
# 带 38 个验证目标（CI 和日常验证就是这么编的）
pwsh -File tools/perf-build.ps1 -Jobs 8
# 只编产品，用来分清产品/验证工程各占多少
pwsh -File tools/perf-build.ps1 -Jobs 8 -ProductOnly
```

脚本用**全新构建目录**（复用目录量出来的"冷构建"只是空转，非空目录默认拒绝），
每一步的原始日志、`baseline.json`、`summary.md` 落在 `build/perf-report-<时间戳>/`。

### 2026-10-25（带测试）

证据：`build/perf-report-20261025-185304/`（构建目录
`build/perf-build-20261025-185304`，690 个对象、1.55 GB）

| 步骤 | 秒 | 重编 TU | 链接 exe | 链接静态库 |
| --- | ---: | ---: | ---: | ---: |
| configure | 3.7 | 0 | 0 | 0 |
| cold-full（全新目录） | 449.7 | 690 | 35 | 9 |
| incremental-noop（什么都不改） | 9.7 | 0 | 0 | 0 |
| leaf-cpp（`src/util/SystemMemory.cpp`） | 95.3 | 2 | 34 | 1 |
| fanout-header（`src/util/AppPaths.hpp`） | 409.5 | 357 | 35 | 8 |
| big-tu（`src/MainWindow.cpp`） | 102.3 | 2 | 13 | 1 |
| link-only（删主程序 exe 后只链接） | 3.2 | 0 | 1 | 0 |

### 2026-10-25（只编产品，同一台机器、同一次会话）

证据：`build/perf-report-20261025-191453/`（191 个对象）

| 步骤 | 秒 | 重编 TU | 链接 exe | 链接静态库 |
| --- | ---: | ---: | ---: | ---: |
| configure | 2.2 | 0 | 0 | 0 |
| cold-full（全新目录） | 148.7 | 191 | 2 | 1 |
| incremental-noop | 1.5 | 0 | 0 | 0 |
| leaf-cpp | 5.2 | 1 | 1 | 0 |
| fanout-header | 62.5 | 61 | 2 | 1 |
| big-tu | 33.9 | 2 | 1 | 0 |
| link-only | 3.1 | 0 | 1 | 0 |

两跑一对照，读出来的事：

1. **带测试的增量耗时，大头是"重链 34 个验证 exe"，不是编译**：同一个叶子 .cpp
   2 个 TU，带测试 95.3 秒、只编产品 5.2 秒；`incremental-noop` 也是 9.7 秒对 1.5 秒。
   单次链接约 2–3 秒（link-only 3 秒），乘上目标数就是全部。日常验证要么接受这笔钱
   （它买的是 38 个独立进程的隔离），要么减少"产品库一变就全体重链"的目标数 ——
   后者要看验证入口是不是真的需要那么多独立 exe。
2. **`AppPaths.hpp` 是高扇出头**：带测试 357 个 TU / 409.5 秒，只编产品 61 个 TU /
   62.5 秒。也就是说这份头的绝大多数消费者是验证工程；产品这边仍有 61 个 TU，
   动它之前先知道代价。
3. **`MainWindow.cpp` 带测试时编两遍**（产品一次、验证工程的 mw runtime 一次），
   102.3 秒对只编产品的 33.9 秒。
4. **冷构建 449.7 秒里，产品占 148.7 秒**，剩下的约 300 秒是 38 个验证目标。

## 二、启动与就绪（真产品 exe，外部测量）

```powershell
pwsh -File tools/perf-startup.ps1 -Runs 6 -Instances 0
pwsh -File tools/perf-startup.ps1 -Runs 6 -Instances 40
```

量的是 `build/verification-mingw13/WTFMinecraftLauncher.exe`（Release `-O3`），**不是**
验证工程。方式：

- 数据目录换成沙盒：把整个 `HKCU\Software\WTFML` 导出 → 删掉 → 种一份
  `Launcher/CurrentFolder`/`Folders` 指向沙盒的最小设置 → 跑 → `finally` 里还原，
  再导出一份和备份做 **SHA256 逐字节比对**（`startup.json` 里
  `registry.restoredVerbatim`）。脚本开头检查有没有启动器在跑，发现就拒绝。
- 每次运行：进程创建 → 标题为 `WTF Minecraft Launcher` 的可见顶层窗口出现
  （`visibleMs`）→ 该窗口开始应答 `WM_NULL`（`readyMs`，消息循环能转了）→
  之后 3 秒内每 20 ms 探一次，记窗口应答的最长一次（`maxBlockMs`）和超时次数。

### 2026-10-25

| 场景 | 次数 | visible 中位 | 范围 | ready 中位 | maxBlock 范围 | 常驻内存 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 空数据目录 | 6 | 764 ms | 728–822 | 840 ms | 1.0–14.7 ms | 136–146 MB |
| 40 个实例 | 6 | 851 ms | 841–871 | 866 ms | 30.9–54.0 ms | 141–146 MB |

证据：`build/perf-runtime-default/`、`build/perf-runtime-40instances/`。

- 0.76–0.85 秒窗口就出来了；两种场景都没有超过 500 ms 的无应答。
- 40 个实例比空目录多约 90 ms、界面卡顿从 ~10 ms 抬到 30–54 ms：**实例列表是要看的
  第一处运行期开销**，但还没到"卡一下"的量级。

## 三、CI 的分片与缓存

```powershell
pwsh -File tools/perf-ci.ps1 -Limit 8
```

只调 GitHub REST 的 GET（`/actions/runs`、`/actions/runs/{id}/jobs`），凭据用 git 里
已经存好的那一份，token 不打印。

**2026-10-25 的结论：CI 从来没有真正跑过。** 最近 5 次运行的 `plan` 与 `build-and-test`
都在 2 秒内失败、`runner_name` 为空、没有任何步骤，check-run 上的注解是：

> The job was not started because recent account payments have failed or your spending
> limit needs to be increased. Please check the 'Billing & plans' section in your settings

也就是说分片/缓存耗时现在**没有可测的对象** —— 要等账号计费那一侧处理完。证据：
`build/perf-ci-20261025-185917/`（`ci.json` 里每次运行每个作业的结论都在）。

## 四、界面交互（探针构建）

验证工程是 `-O0`，量不了产品的手感；所以做了一份**只用于测量的产品构建**
`-DWTFML_PERF_PROBE=ON`（默认 OFF，和 `WTFML_DIAG_DOWNLOAD` 一个模式）：

```powershell
$Compiler = 'D:/Qt/Qt/Tools/mingw1310_64/bin/g++.exe'   # tools/perf-build.ps1 的默认值
$QtPrefix = 'D:/Qt/Qt/6.10.1/mingw_64'
cmake -S . -B build/perf-probe -G "MinGW Makefiles" `
    '-DCMAKE_BUILD_TYPE=Release' `
    "-DCMAKE_CXX_COMPILER=$Compiler" `
    "-DCMAKE_PREFIX_PATH=$QtPrefix" `
    '-DWTFML_BUILD_TESTS=OFF' '-DWTFML_PERF_PROBE=ON' `
    '-DWTFML_STRICT_WARNINGS=ON' '-DWTFML_WARNINGS_AS_ERRORS=ON'
cmake --build build/perf-probe --parallel 8
```

探针本身在 `src/perf/PerfProbe.cpp`：只有 argv 里带
`--wtfml-perf-probe=<文件>` 时才动，否则这个 exe 和普通启动器没有区别。它按**用户点的
那几颗按钮**发动作（`TitleBar::navButton()`、设置页那个主题下拉框），不绕业务层：

- 每步量两段：`actionMs`（同步那一截，翻页后的懒加载/列表重建都在里面）+
  `repaintMs`（把整窗立刻重画一遍）。后者是**上限**，真实使用只重画脏的几块。
- 一个 20 ms 的 `Qt::PreciseTimer` 看门狗记主线程空档：`maxGapMs` 含 20 ms 节拍，
  `maxStallMs` 是减掉节拍之后的值，`over50ms` 数超过 50 ms 的节拍。
- 步骤：**先单独跑一遍本地 Java 扫描**（`scan:java`，见下面第 1 条）→ 四个导航页各一次
  → 四个主题预设各一次 → 回主页 → 还原主题 → **再进一次设置页** → 回主页。
  `nav:settings-again` 是特意加的：第一次进设置页那次本地 Java 扫描只花一次，
  两者的差就是那笔钱的价。

跑法还是那套沙盒 + 注册表逐字还原（`tools/perf-startup.ps1 -Probe`）：

```powershell
pwsh -File tools/perf-startup.ps1 -Probe -Runs 3 -Instances 0  -Exe build/perf-probe/WTFMinecraftLauncher.exe -ReportDir build/perf-probe-empty-n3
pwsh -File tools/perf-startup.ps1 -Probe -Runs 3 -Instances 40 -Exe build/perf-probe/WTFMinecraftLauncher.exe -ReportDir build/perf-probe-40-n3
```

### 2026-10-25（`build/perf-probe/`：Release `-O3`、只编产品、探针 ON；n=3 中位）

| 步骤 | 空目录 动作 ms | 空目录 重画 ms | 40 实例 动作 ms | 40 实例 重画 ms |
| --- | ---: | ---: | ---: | ---: |
| `scan:java`（单独跑一遍本地 Java 扫描） | 72.7 | 0 | 76.3 | 0 |
| `nav:home` | 0 | 0.9 | 0 | 0.9 |
| `nav:instances` | 28.0 | 3.8 | 30.1 | 4.5 |
| `nav:download` | 4.9 | 3.2 | 5.1 | 3.5 |
| `nav:settings`（第一次） | 86.0 | 4.7 | 86.1 | 4.6 |
| `theme:emerald`（本来就是它） | 0 | 1.3 | 0 | 1.4 |
| `theme:ocean` | 52.5 | 2.8 | 54.4 | 3.7 |
| `theme:dark` | 41.4 | 3.0 | 40.9 | 2.9 |
| `theme:system` | 40.1 | 3.3 | 41.6 | 3.3 |
| `nav:home-again` | 3.9 | 4.0 | 3.9 | 4.1 |
| `theme:restore` | 39.6 | 5.4 | 39.7 | 5.5 |
| `nav:settings-again`（第二次） | 3.9 | 3.8 | 4.2 | 4.1 |
| `nav:home-final` | 3.2 | 3.7 | 3.4 | 3.8 |

主线程（20 ms 看门狗，每跑约 79 个节拍）：

| 场景 | 最长空档 | 减掉节拍 | 超过 50 ms 的节拍 | 窗口可见 |
| --- | ---: | ---: | ---: | ---: |
| 空目录（n=3） | 96 / 101 / 124 ms | 76 / 81 / 104 ms | 5 / 5 / 7 次 | 551–579 ms（中位 566） |
| 40 个实例（n=3） | 101 / 106 / 116 ms | 81 / 86 / 96 ms | 5 / 7 / 8 次 | 610–615 ms，另一次 1064 ms |

证据：`build/perf-probe-empty-n3/`、`build/perf-probe-40-n3/`（每次跑的
`run-NN-probe.json` + 中位表都在 `summary.md` 里；`run-00` 是热身，不进结论）。

读出来的事：

1. **「第一次进设置页」86–90 ms，第二次同一页 4 ms —— 差的那 82 ms 里，73 ms 就是本地
   Java 扫描**：`scan:java` 单独量出来 72.7 / 76.3 ms（这是同一台机器上有几份 JDK 时的价）。
   扫描只做一次（`JavaSettingsController::refreshList()` 里 `m_scanned`），页面铺开本身
   只有十几毫秒。**可改的一处**：同一个扫描在 `JavaRuntimeController` 里已经走
   `QtConcurrent::run()` 了，而 `refreshList()` 这一条是同步的 —— 挪到后台能让这一下
   从 ~86 ms 掉到十几毫秒（代价是第一次进来那一瞬间 Java 那几行先是空的）。
   注意 `scan:java` 排在最前面，所以表里 `nav:settings` 那次扫描已经是热缓存，
   它量到的 86 ms 是"扫描（热）+ 首次铺开"，冷启动那一跑（没有 `scan:java` 这一步）
   是 89.9 ms。
2. **换主题 40–54 ms**（`ocean` 最贵）：一次点击里这是能感觉到的一顿（两三帧）。
   "换成已经在用的那一档"是 0 ms —— 控制器里那段去重是生效的。
3. **40 个实例几乎不动「点实例页」**（28.0 → 30.1 ms）：实例列表由代理画、不建行控件，
   这一层没有随实例数线性涨。
4. **主线程最长空档 96–124 ms（减掉节拍 76–104 ms），比探针量的任何一个动作都长**。
   空档里有启动期那一段（看门狗在 `app.exec()` 之前就开始了），所以它不等于"点一下卡
   100 ms"；下一步要在看门狗里带上**当时在哪个步骤**的标记，才能把归属钉死。
5. **探针构建的窗口可见时间是 551–615 ms**（40 实例那跑有一次 1064 ms 的离群值，中位不受影响），
   比第二节同一台机器那 728–851 ms 短；两者构建目录、exe 都不同
   （那边是 `build/verification-mingw13`），不能直接相减。

## 五、还没测的

下面这些现有工具还量不了：

- 任务中心刷新（要有**正在跑**的任务才能量到有内容的那一版，探针里还没有造任务的入口）
- 大量实例预设的加载（预设落在 `<exe 目录>/.minecraft/presets` —— 数据目录换成沙盒
  并不会改它，见上一节；要先在那里备一份预设夹具，再在探针里加一步"打开预设"）
- 并发下载（产品 exe 没有假网络注入点；要么在验证工程里做一个 `-O3` 的下载基准，
  要么接受"只测调度、不测网络"）
- 主线程空档的**归属**（现在只记最长那一次；要按步骤打标记）
- 大量实例下的滚动、分组展开（现在只量了"点实例页"这一下）

在这之前，任何"变快了"的说法都以这份文档里同口径的数字为准。
