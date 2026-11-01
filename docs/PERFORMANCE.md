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

### 2026-10-26 专用测试运行器热重链

`tools/perf-test-links.ps1 -Jobs 6 -TimeoutSeconds 300` 对同一构建目录分别预热，
然后仅触碰共享静态库时间戳；两种模式均通过同一个汇总目标遍历一次依赖图。
记录实际 C++ 编译与链接次数，不用“少跑测试”制造加速。

| 模式 | C++ 重编 | 专用 exe 重链 | 秒 |
| --- | ---: | ---: | ---: |
| Standalone | 0 | 18 | 50.390 |
| Compact | 0 | 2 | 31.150 |

证据：`build/perf-test-links-20261026-181122/summary.json` 及同目录原始日志。
这是本机单次热重链对照，约减少38.2%，不是冷构建、测试执行或 Actions 的加速保证。
核心分片、历史完整回归和产品不在该表内；不与上面的8路全工程基线直接横比。
共享模式保留18个独立 CTest 进程，合并的是链接输出；局部选择 Auto 仍独立构建，
避免为了几项变动而冷编译该层全部专用用例。首次共享构建的预热成本不隐去。

175347 的旧测量预热超时，180124 的逐目标遍历测量被汇总入口方式替代并停止；
180715 检查抓到同秒时间戳导致只重链1/2个输出，拒绝作为有效对照。
最终脚本在触碰前留出一个 Make 时间刻度，181122 两种模式的零编译/链接数校验均通过。

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

- 真实下载执行期间的任务中心刷新（合成视图负载与行复用基线已补，见第七节；尚不覆盖下载线程/磁盘/网络争用）
- 更大预设体量、复杂段/模组内容、增删改后的目录重扫与单份大清单预览；1000条视图和临时元数据扫描见第八节。
- 主线程空档的精确调用栈归因（已按定时器跨度记录 `fromStep/atStep`，不是CPU采样或调用栈证据）
- 大量实例下的滚动、分组展开（现在只量了"点实例页"这一下）

在这之前，任何"变快了"的说法都以这份文档里同口径的数字为准。

## 六、P2 优化：设置页 Java 扫描移出 GUI（2026-10-25）

基准 `f702eeb`，修改为本轮 Java 设置控制器后台扫描工作区。沿用同一个
`build/perf-probe`、Release、Qt 6.10.1、`WTFML_PERF_PROBE=ON`；不改探针步骤。
各预热一次，再测三次，0 实例。测量期间另有并行 2 的验证编译，因此不视为空闲机器基准；
不拿可见时间或总看门狗数值宣称整体启动变快。

| 首次 `nav:settings` | 三次 actionMs（不含预热） | 中位数 | repaint 中位数 |
| --- | --- | ---: | ---: |
| 修改前 | 110.9 / 113.2 / 109.6 | 110.9 ms | 5.7 ms |
| 修改后 | 17.0 / 18.5 / 19.6 | 18.5 ms | 4.8 ms |

同步翻页开销降低约 83%；扫描工作没有消失，只是不再阻塞 GUI。
探针仍故意先同步执行 `scan:java` 来测扫描本身，因此总看门狗峰值不代表优化后的真实翻页阻塞。
主题切换、列表/任务中心和大量预设不由这条结果推断。

证据：`build/perf-java-before-async-20261025/`、`build/perf-java-after-async-20261025/`，
两份 `startup.json` 均确认 registry.restoredVerbatim=true。
修改前 exe SHA-256：`22167E2011FD1A5B2340EFB754F82F22468179C8382E7178BEC994B06EDAA5A6`；
修改后：`77AD451B37C60893E908BA693BE38CB7537D57372FF8A77A54BFA0D29517C535`。

实现边界：首次扫描和自定义 Java 探测在后台执行，输入捕获 Java 根目录/路径；重复刷新合并，
目录或路径改变后的旧结果作废，控制器销毁后不回调旧界面。显式检测/下载结果优先于旧自动扫描。
扫描结果只更新管理展示，不自动改写用户 Java 选择，不提前下载运行时。

功能验证：`20261025-195745-67ed6c08`，主窗口 2696/2696、Java 管理专项通过（合计 99.04 秒）。
新增九条受控后台任务检查验证即时返回、界面事件、去重、作废及析构回调；
它们只证明功能/生命周期，不作为耗时基线。
`20261025-200518-3e90d6a3` 普通启动器构建成功，网络诊断自检通过（18.98 秒）；
产物为 `build/verification-mingw13/WTFMinecraftLauncher.exe`，不是性能探针构建。

## 七、P2 优化：任务列表局部复用（2026-10-25）

测量构建增加固定视图负载：200 条任务（20 进行中、180 已完成），每条两步，
按 150 ms 间隔更新 20 次，每次只变一条进行中任务的进度/说明。
使用真实 `TaskCenterPanel` 控件、内嵌三列布局和真实绘制；不创建下载器或游戏进程，
因此只说明视图刷新成本，不说明网络吞吐、下载线程争用或完整账本接线的性能。
窗口 900×620、40 个合成实例；同机同 Release 探针，前后各预热一次、计三次，测量时不并行编译。

| 指标 | 修改前（每趟中位） | 最终修改后（每趟中位） |
| --- | --- | --- |
| 单条更新 actionMs，20 次/趟 | 61.1 / 60.1 / 61.0 ms | 1.8 / 1.8 / 1.8 ms |
| 所有更新 actionMs 范围 | 58.9–64.4 ms | 1.7–2.6 ms |
| 首次展开 actionMs | 79.8 / 81.4 / 79.5 ms | 77.9 / 78.0 / 81.1 ms |
| 整趟最差主线程阻塞（含其他探针步骤，已减20ms节拍） | 107 / 94 / 99 ms | 104 / 92 / 111 ms |

刷新中位约降低 97%，首次展开没有观察到增加；整趟最大阻塞没有改善，不能拿局部数字宣称整体流畅度已收口。
修改只复用未变化行，改变的行仍按原规则创建；不是完整虚拟列表，首次创建、左栏和详情重建仍有成本。
行号、回调版本、调色板/字体和所有行内展示字段参与失效；详情独立读取最新快照，滚动位置在更新后保留。
曾出现首次展开变成约110ms的中间版本，原因是无滚动时也强制同步布局；最终仅在恢复非零滚动位置时强制布局。

证据：修改前 `build/perf-task-before-20261025/`，最终 `build/perf-task-final-20261025/`；
两者 `registry.restoredVerbatim=true`。修改前 exe MD5 `0654703fb1ce04409ad973b49550f5ed`，
最终 exe SHA-256 `A0DE51F080E4DB421C29BDF14E4610B928642A9B2C95D7B0DC125943489DD3AB`。
复现：沿用第四节探针构建，`tools/perf-startup.ps1 -Exe build/perf-probe/WTFMinecraftLauncher.exe -Probe -Runs 3 -Warmup 1 -Instances 40`。
JSON 的 `taskWorkload` 标明合成来源与行数；`tasks:open-200`、`tasks:update-NN`、`tasks:close` 为对应步骤。

失败也保留：`build/perf-task-after-20261025/` 的预热成功，首次计量提前结束且无报告；当时另有验证编译，
但没有证据证明编译就是退出原因。旧脚本把“已退出”误记为 `cleanExit=true`，未保存退出码，不能认定正常退出。
已修脚本为记录实际退出码、非零时报错，缺报告仍失败。之后两套空闲复测各三次完整通过，
并不等于该次提前结束的根因已定位；中间结果不用于前后性能结论。

功能验证 `20261025-205703-6211941e`：ui-refresh、install-layout、lifecycle、theme 四模块通过。
新增检查覆盖持续进度、未变行保留、滚动位置、详情新鲜度、回调替换、调色板变化和移除释放；
既有检查继续覆盖单选/取消选择、需要处理计数和失败行可见性。这些是功能保证，不作为耗时基线。
`20261025-205805-3251de49` 的 workspace、activity-selection 两个真实主窗口模块通过（0.82s）。
`20261025-210149-6ce322e2` 普通启动器构建成功，network-diag 应用检查通过（18.97s）。

## 八、P2 优化：大量预设筛选与后台目录读取（2026-10-25）

拆开三种成本测量，不能把筛选快当作读盘快：

1. **视图**：1000条合成预设，半数“建筑”、半数“生存”，包含名称/说明、默认标记、类别。
   按同样七个字符串依次搜索（全匹配、半匹配、清空、数字、说明、无匹配、再清空），只调用真实输入框。
2. **磁盘基准**：在启动沙盒内新建随机临时目录，写1000份有效UUID预设及顺序索引，使用生产 `Presets::list()` 连读三遍。
   数据仅含元数据，不是复杂配置/模组文件；准备阶段与同步读取是刻意的测量步骤，不代表优化后的页面路径。
3. **后台读取**：同一临时目录交给生产 `CatalogLoader`，记录提交、完成总耗时及读取期间GUI定时器。
   页面接入由受控模块测试验证；该性能步骤未包含主窗口导航、实例下拉框和完整控制器完成回调的全部成本。

同机 Release，40个合成实例、900×620窗口；前后均预热一次、计三次，测量期间没有并行构建/测试。

| 指标 | 原实现（逐趟） | 新实现（逐趟） |
| --- | --- | --- |
| 创建1000行视图 actionMs | 14.3 / 14.0 / 14.3 | 8.9 / 9.3 / 9.1 |
| 七次搜索 actionMs 中位 | 6.2 / 5.8 / 5.9 | 0.5 / 0.6 / 0.6 |
| 七次搜索范围 | 0.5–6.7 ms | 0.4–1.0 ms |
| 同步读取1000份元数据 | 未测 | 首读2491.1 / 2477.8 / 2545.1 ms |
| 后台读取完成耗时 | 未测 | 2595 / 2529 / 2589 ms |
| 后台提交 actionMs | 未测 | 0.0 / 0.1 / 0.0（四舍五入到0.1ms，并非零成本） |
| 后台读取期间GUI心跳数 | 未测 | 83 / 81 / 82 |
| 后台读取期间最大GUI间隔 | 未测 | 44 / 41 / 45 ms（含20ms节拍） |

搜索中位约降低90%；文件读取本身仍约2.5秒。扫描移出GUI避免整段同步阻塞，不能据此宣称IO加速或原生输入延迟只有这些数。
普通预设页首次打开通过后台加载，重复打开同一在途目录合并；同步刷新会作废在途结果。
工作线程只捕获根路径/读取函数，完成回GUI；控制器或页面销毁后不操作旧控件。
搜索/分类用代理模型保留源条目；列表刷新后按实际可见选中项显示详情，避免隐藏项继续成为操作上下文。
加载与错误提示放在滚动编辑区外，加载期间禁用修改；未解决的坏预设仍明确展示。

证据：原视图 `build/perf-preset-before-20261025/`（exe MD5 `a69ad3eef94e58ab82c9668a9cd7aecc`）；
新视图/读取 `build/perf-preset-async-20261025/`（exe SHA-256 `3B4F4CD1FE9E879618E75AA3EE9B0B557CACA0A38AE2FC70D9C05D7496BB4565`）。
两份报告均 `registry.restoredVerbatim=true`。后续只将状态提示移出滚动区，没有修改筛选/扫描热路径。
中间隔离证据 `build/perf-preset-final-20261025/` 记录同步扫描原始成本，`presetStoreWorkload.ok=true`；它不作为后台响应基线。
测量构建曾因新增Host字段缺少默认初始化而在严格警告下失败；已补默认初始化，未关闭警告；之后严格构建通过。

卡顿归属：`stalls.maxGapFromStep/maxGapAtStep` 及 `slowSpans` 保存50ms以上跨度。
同步测试的最大间隔约2.5秒，标签为 `presets:read-store-*`；造数也会阻塞。
这是同一探针故意同步读取的结果，不可拿总峰值判定后台页面仍卡2.5秒；后台响应单看 `backgroundPresetStore`。
复现沿用探针命令，后台等待最多30秒，整个探针进程仍受启动脚本120秒限制；样本准备/行数校验失败返回非零。

剩余：增删改后的同步重扫、单份大清单与复杂预览、超过1000条的内存/滚动、多盘/慢盘与外部改动一致性。
作废只丢弃结果，不中断进行中的文件读取；线程退出及协作式读取取消仍需单独评估，不把销毁保护当作IO取消完成。

最终功能验证：`20261025-213510-15e5e224` preset-install通过（1.15s），覆盖1000条源模型稳定性、
搜索/类别组合、默认切换、坏项不可操作、隐藏请求项的回退，以及后台加载去重、根目录代际、
显式作废、GUI交付、加载中响应、提示位置、控制器/页面生命周期。
`20261025-213614-450707b8` 的 ui-refresh/install-layout/lifecycle/theme 四模块通过（2.50s）。
`20261025-213737-03997e20` 普通启动器严格构建成功，network-diag应用检查通过（18.96s）。

## 九、并发下载（本机 HTTP 基准，2026-10-25）

产品 exe 没有假网络注入点，验证工程又是按设计 `-O0` 编的 —— 所以单开一个**测量专用**的
Release 工具：`tools/perf_download/`（`-DWTFML_PERF_DOWNLOAD=ON` 才编，默认 OFF、不进 ctest、
不进发布物）。它在本机 `127.0.0.1` 起一个 HTTP/1.1 文件服务器（走**另一条线程**，免得服务器的
socket 写入和被测对象抢主线程），用**产品同一份** `DownloadManager` 下一批文件，量：

- 整批完成时间与吞吐（`transferBytes` 合计 ÷ 实际耗时）；
- 失败数、SHA-1 是否逐文件核对通过；
- 主线程 20 ms 看门狗的**最长空档**（`max_gap - 20 ms`，和第四节同一口径）；
- 每个文件的进度/字节信号次数（信号量本身对主线程的成本）。

编法与跑法：

```powershell
cmake -S . -B build/perf-download -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER=D:/Qt/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_PREFIX_PATH=D:/Qt/Qt/6.10.1/mingw_64 `
  -DWTFML_BUILD_TESTS=OFF -DWTFML_PERF_DOWNLOAD=ON
cmake --build build/perf-download --target WTFMLPerfDownload -j 8
pwsh -File tools/perf-download.ps1            # 全表；-Only '*seg*' 只跑一部分
```

脚本每例先热身一轮（只留覆盖结果），再测三轮取中位；结果落在
`build/perf-download-<时间戳>/`（每例一份 JSON + `summary.json`）。

### 2026-10-25（Release、Qt 6.10.1、n=3 中位）

| 用例 | 文件 | 并发 | 分段 | 限速 | 完成 | 吞吐 | 主线程最长空档 |
| --- | ---: | ---: | :---: | ---: | ---: | ---: | ---: |
| small-100MiB-c6 | 400 × 256K | 6 | – | 不限 | 681 ms | 146.8 MiB/s | 9 ms |
| small-100MiB-c12 | 400 × 256K | 12 | – | 不限 | 690 ms | 144.9 MiB/s | 24 ms |
| small-100MiB-c24 | 400 × 256K | 24 | – | 不限 | 685 ms | 146.0 MiB/s | 41 ms |
| small-100MiB-c48 | 400 × 256K | 48 | – | 不限 | 946 ms | 105.7 MiB/s | 169 ms |
| many-1000x64K-c24 | 1000 × 64K | 24 | – | 不限 | 1313 ms | 47.6 MiB/s | 34 ms |
| large-200MiB-plain-c24 | 40 × 5M | 24 | – | 不限 | 424 ms | 471.7 MiB/s | 40 ms |
| large-200MiB-seg-c24 | 40 × 5M | 24 | 是 | 不限 | 793 ms | 252.2 MiB/s | 98 ms |
| limit-4MiBps-24MiB-c24 | 24 × 1M | 24 | – | 4 MiB/s | 6122 ms | 3.9 MiB/s | 3 ms |
| limit-4MiBps-1000x32K | 1000 × 32K | 24 | – | 4 MiB/s | 7775 ms | 4.0 MiB/s | 20 ms |
| limit-8MiBps-2x5M-c24 | 2 × 5M | 24 | – | 8 MiB/s | 1321 ms | 7.6 MiB/s | 2 ms |
| limit-8MiBps-2x5M-seg | 2 × 5M | 24 | 是 | 8 MiB/s | 1252 ms | 8.0 MiB/s | 17 ms |

怎么读这张表：

1. **并发档位在这条链路上不产生收益**（c6 / c12 / c24 都在 144–147 MiB/s，c48 反而更慢）。
   loopback 没有往返延迟可摊薄，瓶颈是两边 CPU、单线程服务器与磁盘写；产品默认 24 的依据
   来自 `DownloadConcurrency.hpp` 里那条广域网实测（6 路 138.76 s → 24 路 72.63 s），
   不能拿这张表反推"并发数无所谓"。
2. **分段在无时延链路上是净开销**：同一批 200 MiB，plain 472 MiB/s、seg 252 MiB/s。
   分段的收益在"单连接被限速 / 长肥管道"那一侧，不在本机。
3. 小文件的成本主要是每文件连接与调度：400 × 256K 是 146 MiB/s，1000 × 64K 掉到 48 MiB/s。
4. 主线程空档随并发数上升（6 路 9 ms → 48 路 169 ms），但吞吐没有跟着涨 —— 48 档位在这条
   链路上是纯负担。

### 顺手修掉的一个真问题：小文件时「速度上限」被放大数倍

基准第一轮就跑出了反例：**1000 × 32 KiB、限 4 MiB/s，1.25 s 下完（25 MiB/s，6.25 倍上限）**。
原因是 QNAM 会把正文提前读进自己的缓冲，而 `finished` 那一刻缓冲区里剩下的字节是由
`stream->drain(reply)` **一次写完**的 —— 这一段天然绕过令牌桶。文件比缓冲（256 KiB）小的
时候整份都在这一段里。

改了三处（`DownloadManager`，唯一出处都写了注释）：

1. **限速时缓冲按"一拍分到多少"给**（`throttledReadBufferBytes()`，一拍 = 100 ms 的份额 × 2，
   不低于 8 KiB），让 TCP 在源头上就被按住；不限速时仍是 256 KiB。
2. **收尾那一口记账**：`finished` 时把缓冲区里还剩的字节从令牌余额里扣掉（允许余额为负）；
   `onSpeedPumpTick` 只抹**正**的余额（负数是已经写出去的字节，不是攒下的额度）。
3. **欠账没还清不发新作业**：`pumpDownloadQueue()` 在余额 < 1 字节时直接返回，
   由限速泵下一拍补上再发车 —— 否则小文件可以一个接一个地"整份从收尾那一口过"。

改前 / 改后（同一工具、同一台机器，均 n=3 中位）：

| 用例 | 改前 | 改后 |
| --- | ---: | ---: |
| limit-4MiBps-24MiB-c24 | 4622 ms / 5.2 MiB/s | 6122 ms / 3.9 MiB/s |
| limit-4MiBps-1000x32K | 1250 ms / 25.0 MiB/s | 7775 ms / 4.0 MiB/s |

24 MiB 那一跑略高于 4 MiB/s 是令牌桶的 burst 容量（1/4 秒 = 1 MiB）允许的，不是漏记。

### 同一处修出来又暴露的第二层：只开一两条作业时反而低于上限

第一版把上面的缓冲**按并发档位（24）**去分。文件多的时候没问题，只开一两条作业时每条只剩
几 KB —— 而限速泵每 100 ms 才读一次、一次只读得到缓冲区里那点字节，于是速率跟设定值脱钩：

| 用例（2 × 5 MiB、限 8 MiB/s） | 按 24 路分、上限 256 KiB | 按在飞条数分、上限 256 KiB | 最终 |
| --- | ---: | ---: | ---: |
| plain | 8202 ms / 1.2 MiB/s | 2203 ms / 4.5 MiB/s | 1321 ms / 7.6 MiB/s |
| segmented（对照） | 1271 ms / 7.9 MiB/s | 1354 ms / 7.4 MiB/s | 1252 ms / 8.0 MiB/s |

第二列那 256 KiB 的封顶是新的瓶颈：两条回复一拍最多读 512 KiB ⇒ 上限 5.1 MiB/s。
最终版把分母改成 `activeDownloads` 的实际条数（这条回复自己此刻还没进表，+1），
高水位 = 一拍份额 × 2，**只**用一个总内存闸（在飞条数 × 高水位 ≤ 32 MiB）兜住极值 ——
它只在限速超过 ~160 MiB/s（手改值，预设里没有）时才会真的钳住。表格第一列那两行是
当时的临时跑法，没有留 JSON；第三列起才是 `tools/perf-download.ps1` 的两个固定用例
（`limit-8MiBps-2x5M-*`），改完直接复跑即可。
**不限速路径未改**：同一批小文件 c24 改前 141.8 MiB/s、改后 146.0 MiB/s（噪声内），
`large-*` 两跑也与改前同量级。

局限（写清楚，别越读越远）：loopback 量的是调度 / 写盘 / 主线程影响，**不是广域网**；
服务器与客户端在同一台机器上争 CPU 与磁盘；每个文件内容相同（只压连接与调度，
不模拟内容差异）；没有覆盖"真实下载期间任务中心刷新"（那一条仍在第五节没测清单里）。

证据：`build/perf-download-report-20261025/`（改前全表）、
`build/perf-download-report-before-limit-fix/`（改前限速用例）、
`build/perf-download-report-20261025-final/`（上表这一次：11 个用例、每例逐文件 SHA-1 核对通过）。
功能回归：`build/verification-mingw13` 严格构建通过、`ctest` 38/38 通过（354.20 s）。

## 十、共享头扇出（静态，2026-10-28）

口径：`build/verification-mingw13` 里每一份 `.d` 依赖文件算一个 TU，数"哪些 TU 依赖这份头"。
这是**静态扇出**，不是耗时 —— 它说的是"动这份头要重编多少个 TU"，编译每个 TU 要多久另算。
TU 总数 1412（产品 218 / 验证 1194），仓库内头 297 份。

| TU | 产品 | 验证 | 头 |
| ---: | ---: | ---: | --- |
| 478 | 71 | 407 | `src/util/AppPaths.hpp` |
| 436 | 85 | 351 | `ui/Theme.hpp` |
| 433 | 52 | 381 | `src/download/DownloadSpeedLimit.hpp` |
| 433 | 52 | 381 | `src/download/DownloadConcurrency.hpp` |
| 420 | 62 | 358 | `src/manage/InstanceFiles.hpp` |
| 397 | 45 | 352 | `src/download/LoaderMeta.hpp` |
| 394 | 40 | 354 | `src/download/ResourceCatalog.hpp` |
| 393 | 44 | 349 | `src/manage/VersionDisplay.hpp` |
| 371 | 70 | 301 | `ui/TaskCenterPanel.hpp` |
| 370 | 35 | 335 | `src/settings/GameWindowSettings.hpp` |
| 355 | 50 | 305 | `src/download/NetworkManagerFactory.hpp` |
| 345 | 62 | 283 | `ui/TaskProgressBar.hpp` |
| 345 | 62 | 283 | `ui/SkinViewer.hpp` |
| 345 | 48 | 297 | `src/download/LocalFileReuse.hpp` |
| 345 | 30 | 315 | `src/server/ServerRestart.hpp` |
| 344 | 29 | 315 | `src/download/ServerInstall.hpp` |
| 341 | 61 | 280 | `ui/GroupDialog.hpp` |
| 341 | 46 | 295 | `src/util/ZipUtil.hpp` |
| 341 | 29 | 312 | `src/server/ServerModel.hpp` |
| 340 | 62 | 278 | `ui/MemorySlider.hpp` |

读法（别越读越远）：

- 排在前面的十几份彼此只差几十个 TU —— 它们底下挂着**同一个传递底座**（工具 / 路径 / 版本显示那几份），
  所以"把某一根从 436 降到 400"这种数字没有意义；要看的是**产品那一栏**（改它要重编多少产品 TU）。
- 验证那一栏远大于产品那一栏，理由和第二节 AppPaths 那一条相同：验证工程有一份公共夹具，
  夹具引一份头就等于全体验证 TU 都引。
- 本轮没有据此改任何 include —— 这张表是记账，不是优化凭证。

原始数据：`build/tmp-wtfml-fanout.json`（临时文件，不入库）。

## 2026-10-29 实例文件批量选择

真实 Qt 列表的 1000 条目对照：逐行选择参考路径 1279.164ms、1000 次通知；
生产反选按钮的批量路径 1.430ms、1 次通知，最终选择集合相同。
仅统计同步调用，未含首绘、后续绘制、冷盘和真实模组解析；不外推整页加速比例。
详细改动、复测命令及专项/UI 验证见 [选择性能](P2_SELECTION_PERFORMANCE_20261029.md)。

## 2026-10-31 实例列表：`wtfml.meta` 读侧缓存

```powershell
pwsh -File tools/perf-startup.ps1 -Runs 3 -Instances 200 -InstanceMeta -StallSeconds 2 -ReportDir <目录>
```

怎么量的：产品 exe（`build/verification-mingw13`，Release），外部测量口径与第二节相同
（visible / ready / `WM_NULL` 最长阻塞）。**同一次会话、同一台机器、同一份沙盒夹具**连测
两轮；两轮之间只把 `src/manage/VersionManager.hpp/.cpp` stash 掉重编一次，两份 exe 的差异
就是那一格缓存（md5 记在各自的 `startup.json` 里）。

夹具：`-InstanceMeta` 是这次新加的开关 —— 每个实例多写一份 `wtfml.meta`
（`schemaVersion` 1 / 显示名 / 稳定 `instanceId`）。默认夹具**没有**它（实例刚下载完时
本来也没有，改过名 / 归过组 / 换过图标的实例才有），所以**两种夹具的数字不能横向比**；
带 meta 的这一份才量得到"每个实例被问三四遍"那条路。

| 200 个实例（各带 wtfml.meta） | visible 中位 | 范围 | ready 中位 | maxBlock 中位 | 范围 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 无缓存（`exe-before`，md5 `5be401d6`） | 922.8 ms | 904.1–931.5 | 939.2 ms | 203.9 ms | 202.2–205.2 |
| 有缓存（`exe-after`，md5 `d1036ec0`） | 840.2 ms | 816.9–865.1 | 845.4 ms | 150.0 ms | 144.0–188.7 |

- 窗口早出来 82.6 ms，主线程最长阻塞降 53.9 ms；两组范围不重叠。
- 剩下的 150 ms 大致就是"没有 meta 的那一轮"的数：同一天、同一台机器、无 meta 夹具的
  200 实例是 788–829 ms / 144–165 ms（`build/perf-instances-200-metacache`，exe md5
  `9d709661`）。早先同一版二进制的分段探针（同样是**无 meta** 夹具）里，一轮刷新的两大块
  是每行图标（`marks` ~91 ms）与树填充（`fill` ~37 ms）—— 接下来要压的是它们，不是元数据。
- 无 meta 的夹具本来就没有可缓存的东西：缓存把"打不开"换成"stat 一下"，数字基本不动。
  所以这一条**只在"有 meta"的夹具上成立**，别拿它去解释无 meta 的数字。

正确性：缓存按"在不在 + 修改时刻 + 字节数"失效，我们自己的写侧（重命名 / 分组 / 图标）
写完当场丢掉那一格。`verify` 的 O12 一段钉住四种失效：外面的改写（改大 / 改小）、
写侧改完当场可见、文件被删掉之后回到"没有"。判据的缝只有"同一毫秒里、同样字节数"的
外部改写 —— 那种改写光靠这三个值分不出来，代码注释里写明了为什么接受它。

读法：单机、n=3、外部测量，只差这一段代码的 A/B。别把它当成"启动快一成"的通用结论。

## 2026-10-31 换主题：只给露着的滚动条重挂样式

```powershell
pwsh -File tools/perf-startup.ps1 -Probe -Runs 3 -Instances 0 -Exe build/perf-probe/WTFMinecraftLauncher.exe -ReportDir <目录>
```

起因：第四节里「换主题」一直是 40–55 ms 一档（`theme:ocean` 最贵），只是一下配色变化，
这个价能感觉到一顿。这次先把它拆开量：在 `Theme::apply()` 里临时加 `THEME-PROBE` 计时
（`setPalette()` / `styleScrollBars()` / `repolishWidgets()` 三段各一行 `qInfo`），量完
整段删掉。拆分前后的探针构建 md5 分别是 `ad5ef306`、`21a6933b`，最终构建 `76b1a0d5`。

拆分结果（空目录夹具，拆分前后各两轮，只列真换了主题的那四次，单位 ms）：

| 段 | 改之前 | 改之后 |
| --- | ---: | ---: |
| `setPalette()` | 0.36–1.08 | 0.36–0.82 |
| `styleScrollBars()` | 14.7–25.9（ocean ≈25，其余 ≈14.7–16.0） | 0.05–0.36 |
| `repolishWidgets()` | 22.5–26.7 | 22.5–25.7 |

原注释写着「很便宜」的 `styleScrollBars()` 其实是第二贵的一笔：它对
`QApplication::allWidgets()` 里每一根 `QScrollBar` 单独 `setStyleSheet()`，一次切主题
要给全应用几百根一起重挂。现在只算 `isVisible()` 的那些 —— 一屏上真露着的滚动条通常
就一两根。藏着的那些身上记的代数标记还是旧的，`ScrollBarStyler`（应用级事件过滤器）收
`QEvent::Show` 时看标记不对才补一发，所以它们第一次露出来时颜色已经是新的。

最终数字（同一台机器、同一份探针配置、空目录夹具，n=3 中位）：

| 步骤 | 改之前（md5 `ea78d72f`） | 改之后（md5 `76b1a0d5`） |
| --- | ---: | ---: |
| `theme:ocean` | 54.9 | 28.9 |
| `theme:dark` | 40.6 | 26.5 |
| `theme:system` | 40.1 | 26.6 |
| `theme:restore` | 41.1 | 24.9 |

同一份报告里其余步骤没动：`nav:instances` 25.6 → 25.1、`nav:settings` 9.9 → 9.2、
`nav:settings-again` 3.5 → 3.9。窗口可见时间 595.0 → 610.3 ms，两次范围（579–607 /
600–613）互相叠着，这条路径本来就不经过主题，既不当收益也不当回归。

剩下的大头是 `repolishWidgets()`（22.5–25.7 ms）。**没有**顺手把它也改成懒的：藏着的
控件不重 polish，就得在每次 `Show` 时自己记"我这一身是哪一代主题"，漏一处就是老配色
停在屏上，需要单独的钩子加真机视觉验收，不能拿自动测试代替。记在这儿，留给以后。

正确性：`ui_check` 第 ⑭ 组三条钉住「藏着的没有控件级样式 / 换主题不给它白跑一趟 /
一露头就把新主题的样式补上」，外加原有的滚动条配色检查。全量复跑：`ui_check`
3136/3136、`mw_check` 2722/2722、`verify` 6182/6182。

读法：单机、n=3、只量了探针构建（Release，`-DWTFML_PERF_PROBE=ON`，只编产品），说的是
"换一次主题同步那一截"，不含整窗重画那几毫秒。

## 2026-10-31 安装确认弹窗：预设数据只扫一趟盘

路径：下载页 → 选一个版本 → 装 → 确认弹窗里那四类预设（游戏设置 / 模组 / 资源包 /
数据包）。

原来的写法：弹窗按四类各问一次 `presetsInCategory()`，而每一类里都调
`availablePresets()` —— 也就是 `Presets::list()`，把 store 里每份 manifest 读一遍；
最后为了「复合预设」那张表又扫一遍、再按份读 manifest。四类加大表 = **五趟全量扫盘**，
复合预设的 manifest 还在一趟里被读两遍。

现在：`PresetController::installPickerData()` 扫一次，复合预设每份只读一次 manifest，
四类的选项由 `InstallPickerData::optionsFor()` 从这份快照里派生；
`InstallPageController` 拿同一份数据去拼四类和复合表，这一整段不再碰盘。

价：第八节实测过，1000 份预设单趟同步读是 2.2–2.5 s（本机、Release）。按它折算，
旧写法在 1000 份时是五趟 ≈ 11 s 的冻结，新写法是一趟；20 份时约 220 ms → 44 ms。
**这是拿单趟成本折算的，不是这次在弹窗上重新量的**（这一段没有探针）；要真数字得给
弹窗加一步测量，目前没做。

正确性：`mw_check_preset_install` 新增十项 —— 四类各自只出现自己的、复合预设的子项
按父项次序出现在对应类里、缺的那一类不凭空造、未知类别回空、复合项带着父项名字和
读回来的 `components`（不再第二次读 manifest）。复跑：`verify` 6182/6182、
`verify_presets` 762/762、`ui_check` 3136/3136、`mw_check` 2722/2722，
`mw_check_preset_install` 全过。

## 2026-10-31 实例列表：加载器 / 版本类型一次问清楚

```powershell
pwsh -File tools/perf-startup.ps1 -Runs 3 -Instances 200 -InstanceMeta -Exe build/perf-probe/WTFMinecraftLauncher.exe -ReportDir <目录>
```

（改之前那一份跑的是 `-Runs 2`，计入 2 次；改之后是 3 次。其余参数相同。）

起因：上一节把"同一个实例被问三四遍 `wtfml.meta`"压掉之后，分段探针显示一轮刷新剩下的
两块是每行的 `marks`（35–63 ms）和树填充 `fill`（38–50 ms）。看代码才明白 `marks` 不是
画图标：每行都要问「是哪个加载器」（实例行前面的标志、默认分组**再问一遍**、频道判定
还要问一次版本类型），而这三个问题每次都各自把 `wtfml.meta` 读一遍、再各自把合并过的
版本 json 认一遍（扫 `libraries`、展开 `arguments.game`）。同一个实例、同一份数据，
一轮里认三遍。

现在：`VersionManager::instanceTraits()` 一次返回三格 —— 加载器名、加载器版本、版本类型。
meta 那半读一次就够；json 那半只在缺答案时才去认，认出来的结果记在
`mergedVersionJsons` 那一格里（`MergedJsonEntry`）。`instanceModLoader()` 变成它的一层
包装，界面上那三处改成一人调一次。

价（同一台机器、同一份 200 个各带 `wtfml.meta` 的夹具，外部测量口径同第二节）：

| 200 个实例（各带 wtfml.meta） | visible 中位 | 范围 | ready 中位 | maxBlock 中位 | 范围 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 改之前（`build/perf-probe-refresh-before`，md5 `ad7d5f51`） | 815.6 ms | 809.3–821.9 | 826.2 ms | 148.0 ms | 132.9–163.1 |
| 改之后（`build/perf-probe-traits-after`，md5 `549da091`） | 654.6 ms | 640.8–662.8 | 657.3 ms | 19.8 ms | 19.1–20.9 |

同两份报告里的分段探针（`TEMP REFRESH-PROBE`，一列 `qInfo` 打在 `refreshInstalledVersions()`
里，量完整段删掉；单位 ms，每个进程两轮刷新）：

| 段 | 改之前（6 轮） | 改之后（8 轮） |
| --- | ---: | ---: |
| `scan`（`installedVersions()` 扫盘） | 17–25 | 18–24 |
| `parts`（`displayParts()` 那圈） | 1–9 | 1–11 |
| `marks`（每行的标志 / 分组 / 频道） | 35, 39, 43, 61, 63, 121 | 41, 42, 42, 102（进程里第一轮）；1, 1, 2, 2（第二轮） |
| `fill`（`rebuildInstanceTree()`，含分组那一趟） | 38, 40, 42, 43, 45, 50 | 3–6（每一轮） |

- `fill` 一起掉下来的原因：分组那一趟原来对每个实例各自问一次加载器和类型，现在走
  同一份 `instanceTraits()` 结果。
- **诚实的一条**：进程里第一轮刷新仍然要 41–102 ms（那 102 是冷盘那次）。它付的是
  "200 份版本 json 各解析一遍 + 认一遍"的一次性成本，现在缓存住了；第二轮起 1–2 ms。
  改之前是**每一轮**都付这 35–63 ms，这才是省下来的东西。
- `scan` 没动：每实例几次 stat 还是那样。下一步要再压就是它。
- 夹具带 meta。无 meta 的实例走的是 json 那半，收益同样是"一轮只认一遍"，但没有这一轮
  的 A/B 数字。

正确性：`verify_instances` 新增 QQ 一段（26 项）钉住三条契约 —— meta 写了加载器就以
meta 为准、meta 没写才去 json 的 `libraries` 里认（含薄 json 从父版本继承）；
`clearVersionJsonCache()` 之后必须重新认（那一格事实就存在 `mergedVersionJsons` 里，
所以六处清缓存的地方天然一起失效）；外部直接改 `wtfml.meta`，同一个会话里的下一次问
就看得见（那半每轮都 stat）。全量复跑：`verify_instances` 1657/1657、`verify` 全过、
`ui_check`、`mw_check`、`verify_presets`、`verify_downloads` 全过。整轮 `ctest` 跑完
只有 `wtfml.core-resources` 闪过一次 SEGFAULT，单独重跑三次全过，按已知的
`build/verification-mingw13` 时序 flake 处理，没当回归追。

读法：单机、改前 n=2 / 改后 n=3、只量探针构建（Release `-O3`、只编产品）。别把
815 → 655 外推成"所有机器都快 20%"，它说的是这一份夹具、这一条路径。

## 2026-10-31 实例目录扫描：两趟 readdir 合成一趟

```powershell
pwsh -File tools/perf-startup.ps1 -Runs 3 -Instances 200 -InstanceMeta -Exe build/perf-probe/WTFMinecraftLauncher.exe -ReportDir <目录>
```

上一节之后一轮刷新里最大的一块变成 `scan`（`installedVersions()` 扫盘，18–23 ms）。拆开看
每实例一次：`resolveInstance()` 里 `DirListing` 先 `entryList("*.jar")`、再
`entryList("*.json")` —— **两趟完整的目录读取**，看的是同一批顶层条目。

现在 `InstanceFiles::scanDir()` 一趟 `entryList(QDir::Files)` 取全，再按后缀分进两个表。
顺序逐字不变（两种写法都按文件名排），后缀比较**不区分大小写**（`QDir` 的名字过滤器默认
就是这个规矩，`A.JAR` 在改之前就是候选 jar）；目录不存在时 `entryList()` 照样给空表，
所以原来那句 `dir.exists()` 早退一起去掉了。

| 200 个实例（各带 wtfml.meta） | visible 中位 | 范围 | 每轮 `scan`（探针） |
| --- | ---: | ---: | ---: |
| 改之前（`build/perf-probe-scan-before`，md5 `6959ce8e`） | 656.4 ms | 653.0–681.4 | 18–23 ms |
| 改之后（`build/perf-probe-scan-after`，md5 `7ff4cdd0`） | 623.5 ms | 615.3–625.5 | 7–10 ms |

- 两轮背靠背、同一台机器、同一份夹具、各 n=3；visible 的两次范围不重叠。
- `maxBlock` 两次范围是叠着的（12.8–24.3 / 19.1–30.3），这一条**不当收益**。
- 分段探针里 `marks` 也跟着变了（第一次 94 → 62、之后 44 → 25），那是进程第一轮解析
  200 份 json 那一次的一次性成本，受磁盘/AV 状态影响大，**不当成这次改动的收益**。
- 正确性：`verify_instances` 的 J 段（认不出来的目录进不进列表、告警条数）与
  `InstanceFileTests` 的真值表覆盖这条判据；整轮 `ctest` 39/39 全过。

读法：单机、只量探针构建、n=3；说的是这一份夹具下"每轮刷新的扫盘那一段"。
## 2026-10-31 主页那颗箭头：一个字形把启动拖长了 270 ms

```powershell
pwsh -File tools/perf-startup.ps1 -Runs 3 -Instances 200 -InstanceMeta -Exe <exe> -ReportDir <目录>
```

上一节之后启动是 615–625 ms。这一段是从"哪一块最贵"一路拆下来的：`setupUi()`
≈ 335–358 ms，其中四页 ≈ 296–316 ms，再往下几乎全在**主页**（271–275 ms），
主页里又几乎全在右栏那张账号卡片的一段几何计算上：

```
STARTUP-PROBE home left 1        ← 左栏全部搭完，1 ms
STARTUP-PROBE home card 1        ← 卡片外壳，1 ms
STARTUP-PROBE home account 10    ← 账号按钮 + 三个标签，9 ms
STARTUP-PROBE home viewer 10     ← SkinViewer 构造
STARTUP-PROBE home skin 10       ← 兜底皮肤
STARTUP-PROBE home minheight 279 ← profileCard->heightForWidth(...) 这一段 269 ms
```

`heightForWidth()` 里逐个问子控件的 `sizeHint()`（那个卡片的做法见
`HomeProfileCard`）。逐个计时：

```
STARTUP-PROBE carditem QPushButton 264   ← 就是这一下
STARTUP-PROBE carditem SkinViewer 0
```

换成一颗裸的 `QPushButton` 量 `sizeHint()` 只要 7 ms；带上这套属性（kind /
homeAction / accountHeader）、带子布局也都还是 0 ms。差别只剩那颗箭头：
`QLabel("⌄")`，而 **U+2304 不在默认界面字体的覆盖表里**。Qt 第一次量这段文字
就要去字体库里做一遍**回退查找**，本机实测：

```
STARTUP-PROBE probebtn   QSize(54, 34) 7     ← 裸按钮
STARTUP-PROBE glyph      QSize(10, 19) 268   ← 就一个 "⌄"
STARTUP-PROBE plainlabel QSize(24, 18) 0     ← 中文/ASCII 不付这笔
```

不是 QSS、不是布局、不是皮肤，是**一个字形没在默认字体里**。改法：那颗箭头改用
`VersionIcons::chevron()` 画的位图（和底栏、版本树里那些箭头是同一份笔画），
控件、尺寸、换主题重画都由 `Theme::follow()` 管；U+2304 从启动路径上拿掉。

| 200 个实例（各带 wtfml.meta） | visible 中位 | 范围 | 最长阻塞 |
| --- | ---: | ---: | ---: |
| 改之前（`build/perf-probe-arrow-before.exe`，md5 `0bfb054c`） | 670.0 ms | 642–722 | 13.1–33.1 ms |
| 改之后（`build/perf-probe-arrow-after.exe`，md5 `ad323ad6`） | 383.4 ms | 374–391 | 12.9–15.5 ms |

- 两份都是**不带探针**的探针构建（`WTFML_PERF_PROBE=ON`、Release），背靠背各 n=3，
  同一台机器、同一份夹具；`git stash` 前后各编一次，所以是真正的同版 A/B。
- 拿掉的这一下是**一次性**的（进程里第一次量那段文字），不是每帧都付；它落在
  `show()` 之前，所以直接表现为"窗口晚 270 ms 出来"。
- `maxBlock` 两边范围叠着，不当收益；这一节的收益只有一个：visible 少了 ≈287 ms。

**诚实的一条**：这 270 ms 是字体回退那套东西的**一次性**初始化，不是每颗字形的价。
同一个进程里接着量别的回退字形（`✓` U+2713、`⚠` U+26A0）都是 0 ms，再量一遍 `⌄` 也是
0 ms。也就是说这次改的是"**这笔钱不再压在启动上**"，不是"这笔钱不存在了"：界面上还有
别的回退字形（实例文件页的 `⚠`、任务中心的 `✓` / `✗` 之类），第一次真的画出那一页时
仍然会付一次。启动第一屏（主页）现在一个回退字形都不用，这是实测到的收益；要把这笔钱
彻底去掉，得把那些也换成画的图标，那是一次单独的清扫，这次不做。

```
GLYPH-PROBE ascii   QSize(21, 16) 7
GLYPH-PROBE cjk     QSize(24, 16) 0
GLYPH-PROBE chevron QSize(9, 16) 271   ← U+2304，第一次回退查找
GLYPH-PROBE check   QSize(12, 16) 0    ← U+2713，后面的都免费
GLYPH-PROBE warn    QSize(17, 16) 0    ← U+26A0
GLYPH-PROBE chevron-again QSize(9, 16) 0
```

正确性：`accountArrow` 只是装饰，没有任何断言认那个字形；`ui_check` / `mw_check`
全套、整轮 `ctest` 全过（`wtfml.core-resources`、`wtfml.download-retry` 各闪一次，
单独重跑都过，按已知的 `build/verification-mingw13` 并行时序 flake 处理）。

读法：单机、n=3、只量探针构建。270 ms 这个数取决于**这台机器默认字体里有没有那一格**
——换一台默认字体更全的机器，改之前那一版可能本来就不付这笔；收益的绝对值不可外推，
但"启动第一屏别用字体里没有的字形"这条规矩与机器无关。


## 2026-10-31 界面正文里的回退字形：一次清扫

上一节把启动第一屏那一颗 `⌄` 换成画的了，但别的页还在正文里塞 `✓` / `✗` / `⚠` / `☑`
——那些也都是默认界面字体里没有的字，谁先画出来谁付那 270 ms。这一轮把那几格清掉。

**怎么量的**：临时探针 `build/tmp-wtfml-fontprobe/main.cpp`，**一个码点一个独立进程**，
量它第一次 `QFontMetrics::horizontalAdvance()` 的耗时（回退机器是进程内一次性的，
一个进程里连着量一串会让后面全是 0，看不出谁付的钱）：

```
U+0041 A     metrics=6ms      U+2304 ⌄     metrics=281ms
U+2190 ←     metrics=4ms      U+2303 ⌃     metrics=275ms
U+2192 →     metrics=4ms      U+26A0 ⚠     metrics=277ms
U+2022 •     metrics=4ms      U+2713 ✓     metrics=279ms
U+00D7 ×     metrics=4ms      U+2717 ✗     metrics=272ms
U+2605 ★     metrics=4~5ms    U+25B6 ▶     metrics=287ms
U+25A0 ■     metrics=4ms      U+2610 ☐     metrics=294ms
U+25A1 □     metrics=4ms      U+2611 ☑     metrics=276ms
U+25CF ●     metrics=4ms      U+24D8 ⓘ     metrics=285ms
U+203B ※     metrics=4ms      U+23F3 ⏳    metrics=277ms
U+221A √     metrics=4ms      U+2194 ↔     metrics=277ms
U+FF01 ！    metrics=4ms      0x1F3xx 那一片（emoji） 未测，按回退字形对待
```

判据只有一条：**主字体里有没有**。`QFontMetrics::inFont()` 不行 —— 它问的是整条回退链，
本机上上面每一个字符它都回 1。

**改了什么**（`ui/TextMarks.hpp` 是唯一出处，五格 + 三个「记号 + 空格 + 正文」的重载）：

| 原来 | 现在 | 首次排版 |
| --- | --- | --- |
| `✓`（任务中心、日志、升级向导步骤） | `√` U+221A | 279 → 4 ms |
| `✗`（同上） | `×` U+00D7 | 272 → 4 ms |
| `⚠`（预设页、键位冲突、原理图兼容提示） | `※` U+203B | 277 → 4 ms |
| `☑` / `☐`（迁移 / 升级计划表） | `■` / `□` U+25A0 / U+25A1 | 294 / 276 → 4 ms |
| `▶ 启动实例`（实例概览的主动作） | `启动实例` | 287 → 0 ms |

`○ ● → • ★` 这几格本来就在主字体里，没动。`ui/TextMarks.hpp` 的头注释里列了
**不许再用**的那一串（`✔ ✘ ⚡ ▶ ◀ ↻ ⟳ ⓘ ℹ ⌛ ⏳ ✎ ✏ ⌄ ⌃ ↔`），它们是同一套回退字形；
要图标就走 `VersionIcons` 画的那一套。

**钉住它的断言**：`tests/ui_check/modules/UiTaskCenterChecks.cpp` 的 I1，直接对
`TextMarks::done()` 这几格的**字符本身**下断言——有人把 `✓` 写回去，那条会红，
红的那句话里写清楚了为什么（不是美学，是 277 ms）。

**诚实的两条**：

- 这不是"把那笔钱彻底删掉了"。用户在 Minecraft 日志、模组名、服务器 MOTD 里带进来的
  回退字形照样会触发一次（那是外部数据，控制不了）；这里保证的是**界面自己写的话**
  一个都不碰回退路径。
- 数字都来自本机（Windows 11 中文版，默认界面字体 Microsoft YaHei UI 9pt）。换一台
  默认字体更全的机器，改之前可能本来就不付这笔；"界面自己别用回退字形"这条规矩与机器
  无关，绝对值不可外推。

正确性：整轮 `ctest`（`build/verification-mingw13`，39 项）过，
`wtfml.core-resources` / `wtfml.download-retry` 各闪一次、单独重跑都过，按已知的并行
时序 flake 处理。断言里跟记号有关的那几条已改成对 `TextMarks` 下断言（不是再抄一份
字面量）：任务中心 I1/I2、任务详情 I6、原理图 SG1、迁移计划、实例补全那一句日志。

## 2026-11-01 发布副本去符号（静态单文件）

静态 Qt 那份 `-O3` 单文件 73,531,415 字节，`strip --strip-unneeded` 之后 43,288,540
（省 41.1%），导入 DLL 清单逐条相同。启动三跑：原件 visible 188/199/189 ms，
去符号副本 171/185/190 ms —— 没有变慢。`-Os`（MinSizeRel）再去 4.9 MB，但启动中位
189 → 238 ms，不采用；`-flto=auto` 在这条工具链上链接不过（16 个自家符号被判未定义），
不引入。工具是 `tools/prepare-release-copy.ps1`（只动副本、留 `.debug`、写
`release-copy.json`），完整口径、证据目录和"没验到的"见
[发布体积实测](P2_RELEASE_SIZE_20261101.md)。

## 2026-11-01 共享头扇出的时间成本与预编译头

第十节量的是**静态扇出**——动一份头要重编多少个 TU，那一节自己写着"编译每个 TU 要多久
另算"。这一节把那笔另算补上，并据此加了一项构建选项。

### 动一份头要等多久（验证工程，`--parallel 12`）

做法：`touch` 那一份头 → `cmake --build build/verification-mingw13 --parallel 12` →
记墙钟和 `Building CXX object` 的行数。每份都是上一次构建完全就绪之后再动。

| 动的头 | 重编 TU | 墙钟 |
| --- | ---: | ---: |
| `src/util/AppPaths.hpp` | 411 | 297 s |
| `ui/Theme.hpp` | 328 | 238 s |
| `ui/MainWindowUI.hpp` | 271 | 218 s |

### 这 218~297 秒花在哪儿：单个 TU 的解析成本

`g++ -fsyntax-only`、与产品同一条命令（`-O3 -std=c++23` + 那份 `includes_CXX.rsp`），
每份跑 3 次取最低值：

| TU 里只有 | 解析 |
| --- | ---: |
| 空 TU | 66 ms |
| `#include "AppPaths.hpp"` | 66 ms |
| `#include <QString>` + `<QWidget>` | 1427 ms |
| `#include "Theme.hpp"` | 1566 ms |
| `#include "InstanceFilesPage.hpp"` | 1481 ms |
| `#include "MainWindowUI.hpp"` | 1853 ms |

读法：**1.4 秒的底座是 Qt 控件头**，项目自己的头在它上面只加 0.1–0.4 秒（`AppPaths.hpp`
压根不引 Qt，所以在它上面几乎是 0）。所以"再砍几个项目头"换不来什么；能吃回来的是
那 1.4 秒本身 —— 每个 TU 都在重复解析同一批 Qt 声明。

### 预编译头：哪一张能要，哪一张不能要

`QWidget` 那张 `.gch`：建一次 4.4 s，177,643,858 字节。同一个 TU 读回来：
1427 → **337 ms**（`MainWindowUI.hpp` 1853 → 825，`InstanceFilesPage.hpp` 1481 → 402），
空 TU 66 → 335 ms —— 那 335 ms 就是固定读回成本，**只有本来要解析 Qt 的 TU 才赚**。

`<QtWidgets>` 整个模块那张：11.0 s、425,264,619 字节，同一个 TU 读回来 **3445 ms** ——
比不用 PCH 还慢。这张不要。

### 落地：`WTFML_USE_PCH`（默认 ON，只给主程序）

```cmake
if(WTFML_USE_PCH)
    target_precompile_headers(WTFMinecraftLauncher PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:<QtWidgets/QWidget>>")
endif()
```

全量产品构建（`WTFML_BUILD_TESTS=OFF`、Release、严格警告、全新目录、`--parallel 12`）：

| | TU | 墙钟 |
| --- | ---: | ---: |
| `WTFML_USE_PCH=OFF` | 237 | 95 s |
| `WTFML_USE_PCH=ON` | 238（多的是 `.gch` 本身） | **75 s（−21%）** |

带测试的全量构建（`WTFML_USE_PCH=ON`、严格警告 + `-Werror`、801 个 TU）：**378 s**。
**这一项没有 A/B**：OFF 那一份跑到一半被"机器内存吃紧"停掉了，不是构建报错 ——
所以"带测试的构建快多少"这一格不填，只记 ON 这一份的实测值。按单 TU 省下的解析折算，
它最多只能吃回那 238 个产品 TU 的份额，测试 TU 一点不受益。

为什么只给主程序挂：验证工程是几十个各自独立的 target，每个都要自己那份 `.gch`
（几十份 × 177 MB），换不回来。

验证：打开 PCH 之后 `build/verification-mingw13` 整棵重编通过（226 TU，`--parallel 8`），
`ctest --preset verification` **38/38 通过**（403.85 s）。PCH 只改"这份头解析几次"，
不改编出来的语义；这一跑是确认没有哪条 TU 原本靠"自己 include"编过、现在被兜住了。

### 踩过的坑（写下来省下一次）

- **PCH 与使用端的编译选项必须逐字一致**，否则 GCC 静默不用它 —— 看起来"没变快"，
  其实根本没读。`-Winvalid-pch` 才看得见。这次撞到的是建 `.gch` 时带了 `-O3`、
  编译 TU 时漏了，警告原文是 `created and used with differing settings of
  '-munroll-only-small-loops'`。
- PCH 会把"漏写的 include"兜住：主程序少写一句 `#include <QWidget>` 也能编过。
  这份 PCH 只有一条头，影响面就到这儿为止。

### 没验到的

- **CI（sccache）上的效果**：没在 runner 上量过，也没验证 sccache 对 GCC PCH 的缓存行为。
- 低内存机器：每个 g++ 进程都会映射那 177 MB；本机 31.4 GB 没量到边界。
- 磁盘：每个构建目录多一份 177 MB（`build/tmp-wtfml-pch-on` 261 MB、带测试的
  `build/tmp-wtfml-pch-tests` 1.9 GB 是整棵树的体积，不是 `.gch` 单独的体积）。

静态发布那份（`build/static-release`，GCC 15.2 / `C:/mingw64`，另一份 Qt）也重编通过：
231 TU、95 s，`.gch` 190.8 MB —— 这条工具链上 PCH 一样可用，发布路径不受影响。

证据：`build/tmp-wtfml-pch-{off,on}-build.log`、`build/tmp-wtfml-pch-tests-build.log`、
`build/tmp-wtfml-verify-pch-build.log`、`build/tmp-wtfml-pch-static-build.log`
（都是临时文件，不入库）。

## 2026-11-01 主线程卡住时的调用栈

`actionMs` 只回答"这一步用了多久"，不回答"这段时间主线程在做什么"。这一轮加了采样器
（`src/perf/StallSampler.cpp`，只在 `-DWTFML_PERF_PROBE=ON` 的构建里编）：主线程每个
探针步骤打一次心跳，另一个线程看心跳，停了 50 ms 以上就 `SuspendThread` →
`GetThreadContext` → `StackWalk64` → `ResumeThread`，放行之后才 `SymFromAddr`；
本程序的帧只记 `exe+0xRVA`，事后交给 `tools/symbolize-stalls.ps1`（addr2line）解析。
方法、踩过的坑（`SYMBOL_INFO::Name` 数组重载、返回地址要 `-1`、dbghelp 只有导出表）
和边界见 [主线程卡住时的调用栈](P2_STALL_STACKS_20261101.md)。

三跑同一份 exe（`-Instances 200`，200 个实例），栈顶与步骤耗时对得上：

| 步骤 | `actionMs`（三跑） | 卡住时主线程在哪 |
| --- | --- | --- |
| `presets:read-store-1/2/3` | 2292–2367 ms | `Presets::list` → `loadManifest` → `QFile::read`（`PresetStore.cpp:410`） |
| `presets:prepare-store-1000` | 1670–1694 ms | `saveManifest` → `writeBytesAtomic` → `QSaveFile::commit` → `FlushFileBuffers`（`PresetStore.cpp:64`） |
| `nav:instances` | 273–278 ms | DirectWrite 字形回退（`fallbacksForFamily` / `stringToCMap`） |
| `scan:java` | 81–108 ms | `QProcess::waitForFinished`（`GlobalJavaSettings.cpp:95`） |
| `tasks:open-200` | 77–82 ms | 造 200 行任务控件：`ensurePolished` → `totalSizeHint` |

`gapMs` 比同一步骤的 `actionMs` 大 1.9–86.6 ms（22 份栈，中位 25.6 ms），采样器没有
把测量本身拖慢一个量级。另有一次 `nav:settings` 卡 2.9 s、栈停在 `QThread::start` →
`CreateThread`，同一份 exe 后两跑只有 12.3 / 10.9 ms，没有复现，只记现象不记结论。

证据：`build/tmp-wtfml-stall-run3/`、`build/tmp-wtfml-stall-run4/` 的报告与
`build/tmp-wtfml-probe/` 的探针构建（都是临时文件，不入库）。

## 2026-11-01 后台目录加载与界面交互（多任务负载）

探针原来是串行的：后台目录加载那一步会把后面的步骤全挡住，所以"用户在预设页转圈时
点翻页"从没量过。这一轮改成同一份 exe 里做 A/B：`concurrent:calm-01..08`（空载）→
起真正的 `Presets::CatalogLoader`（1000 份 manifest）→ `concurrent:busy-01..08`
（加载中），每步都记 `loadActive`。

10 跑（`-Instances 200`，200 个实例，后台那一趟都是 8/8 步真的压在读盘窗口上）：

| 指标 | 空载 | 加载中 |
| --- | --- | --- |
| 翻页一次（`actionMs`，8 发均值） | 2.9–3.3 ms | 3.6–3.9 ms |
| 整窗重画（8 发均值） | 3.3–3.5 ms | 3.9–4.8 ms |
| 后台那段时间主线程心跳最大间隔 | — | 37–44 ms |

两个方向稳（10 跑 10 次加载中更慢），绝对值都在 4 ms 上下，用户看不见。后台那一趟
没有一份超过 50 ms 的卡顿栈被采到。对照是同一个 store **同步**读一趟 2.3 s
（见上一节），后台化这一步是对的。

顺手排除：`WTFML_PERF_NO_STALL_SAMPLER=1` 关掉采栈再跑 4 次，`nav:settings` 10.1–11.7 ms，
与开着的 11.1–12.5 ms 同量级，没说采栈拖慢了被测量的步骤。

没解释的现象：第一次进设置页时 `QThreadPool::start` 偶尔卡在建线程里 2.8–2.9 s
（9 次带栈的测量里 2 次，栈停在 `QThread::start` → `CreateThread`），同 exe 后来 8 跑
没复现，未定位。方法与边界见
[后台干活时界面还点得动吗](P2_CONCURRENT_LOAD_20261101.md)。

## 2026-11-01 预设列表的摘要缓存

上一节那条 2.3 s 的栈（`Presets::list()` 一份一份读 manifest）解决的是**重复**那一半：
`src/presets/PresetStore.cpp` 给 `list()` 加了一层进程内摘要缓存，键是 manifest 的
完整路径、新鲜度按 `(字节数, 修改时刻)` 判定；只缓存列表要的那几格，不缓存正文；
本进程写盘（`writeBytesAtomic`）提交成功后当场作废那一格；读不回来的那份不进缓存；
上限 4096 份，带 `QMutex`（后台 `CatalogLoader` 与界面线程都会调）。

同一份探针 exe、`-Instances 200`、1000 份预设，3 跑：

| 步骤 | 改之前 | 改之后 |
| --- | --- | --- |
| `presets:read-store-1`（冷） | 2351–2367 ms | 2273–2321 ms |
| `presets:read-store-2`（热） | 2306–2335 ms | 11.4–11.9 ms |
| `presets:read-store-3`（热） | 2292–2308 ms | 11.4–15.4 ms |

**第一次仍然要 2.3 s**，重复的那几趟每趟少 2.29 s（约 200×）。写那 1.7 s 没动。

改完发现热缓存会让探针的"后台目录加载"那一步 11 ms 就回来、多任务 A/B 的加载中那半组
空转（`busyDuringLoad` 8/8 → 0/8）；探针改了先调 `Presets::forgetSummaryCache()` 再起
后台加载，三跑回到 2.31–2.36 s。形状、五条失效路径的验收（PA12）与边界见
[预设列表的摘要缓存](P2_PRESET_LIST_CACHE_20261101.md)。
