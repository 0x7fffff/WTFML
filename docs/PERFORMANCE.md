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
