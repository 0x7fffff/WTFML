# 测试与验证

## 统一入口（Windows / PowerShell 7）

在仓库根目录运行，先确保 CMake、所用 MinGW 和 Qt 的 bin 目录在 PATH 中：

```powershell
./tools/test.ps1                         # 按 origin/main、未提交/未跟踪改动选择模块
./tools/test.ps1 -Suite affected -BaseRef HEAD~1  # 检查最近一次提交的影响
./tools/test.ps1 -Suite quick             # 明确运行固定快速专项
./tools/test.ps1 -Suite java             # Java 管理
./tools/test.ps1 -Suite mods             # 模组操作栏、迁移与依赖检查
./tools/test.ps1 -Suite upgrade          # 原地升级、完整快照和故障回滚
./tools/test.ps1 -Suite network          # 本机可控服务器下载测试，不访问外网
./tools/test.ps1 -Suite core             # 核心回归
./tools/test.ps1 -Suite window           # 主窗口集成回归
./tools/test.ps1 -Suite full             # 全部非 visual 回归（共享改动/发版兜底）
./tools/test.ps1 -Suite visual           # 完整历史 UI：含尚未定位的生命周期问题
./tools/test.ps1 -Suite quick -NoBuild    # 明确使用现有二进制，不保证对应最新源码
./tools/test.ps1 -Suite full -ListOnly    # 仅列测试，不构建、不执行，不代表验证通过
```

默认构建目录是 `build/verification-mingw13`。首次使用会按 `verification` preset 配置；
CI 使用 `-BuildDir build/ci -ConfigurePreset ci-verification`。已有构建目录沿用其配置，
不静默更换编译器。构建并行默认 2，可用 `-BuildJobs 4` 调整；测试仍串行。
CI 的配置/单目标构建预算显式放宽到 900 秒；本地默认仍是 600 秒。

### CI 加速与分片

GitHub 将同一份非 visual 测试集拆成四个并行作业，各自内部仍串行跑测试：

| 入口 | 构建目标 | 验证范围 |
| --- | --- | --- |
| `-Suite ci-app` | 主程序、网络诊断工具 | 主程序编译及诊断工具自测 |
| `-Suite ci-core` | 所选 `verify_<域>` / `verify_baseline` | 核心、测试入口自检、分片完整性检查 |
| `-Suite ci-ui` | 所选 `ui_check_<专项>`；历史模块选 `ui_check` | 控件专项和完整驱动启动检查，不运行历史 visual |
| `-Suite ci-integration` | `mw_check` 及所选 `mw_check_<专项>` | 主窗口及全部集成专项 |

每份 CMake 测试目录自动附加所属分片标签，新测试无需复制维护一份名称白名单。
`wtfml.ci-partitions` 对比四组的并集与 full：缺项、重复、空组都会失败，不能靠漏登记提速。
全量仍直接枚举 CTest 的完整非 visual 集合；增量的具体选择另见下方“按改动选择模块”。
四路有一条失败，原来的 `build-and-test` 汇总检查仍失败；报告附件按分片区分，不互相覆盖。

仅 `ci-verification` 使用 Ninja 和 `-O1 -DNDEBUG`（取代高编译成本的默认 `-O3`）；
验证程序仍保留自己的 `-O0`，严格警告和 `-Werror` 不变。本地 verification/release 不变，
CI 产物不作为正式发布包或性能基准。已有 `build/ci` 若是 Makefiles 生成器，须另选一个
新的构建目录，不能在原目录直接切换生成器。

使用 Mozilla sccache 和 GitHub 仓库内的编译缓存，后续提交只编译缓存未命中的单元；
不缓存整个构建目录，不复用过期二进制跳过测试。缓存服务不可用时允许退回真实编译，
真实编译失败不会吞掉。每个作业仍用 2 个编译进程，避免私有仓库 runner 内存不足。
冷缓存首轮仍需完整编译；实际收益以远端耗时与缓存命中统计为准，不保证固定分钟数。

`ci-app` 在 Ninja 下先刷新已有配置的构建图，再枚举主程序对象，每 32 个编译单元为一个独立阶段，
最后仍构建完整主程序目标（含资源及链接）和诊断工具并执行测试。每批沿用构建超时，
不会重试失败批次；枚举为空直接失败。此举避免所有冷编译挤入同一 900 秒预算，
不减少编译工作量、不保证总耗时缩短；整个作业仍受工作流 45 分钟上限约束。

quick 包含脚本自测、Java 管理、模组操作栏、工作区、任务选择、主题、UI 刷新、生命周期和工具箱外观。
full 不包含完整 visual 套件，也不包含未登记到 CTest 的手动实网探针。
full 通过不等于多 DPI 实机、读屏软件或真实网络全链路已经验收。
full 也尚非严格断网测试：主窗口历史用例会触发后台 Mojang/镜像清单请求，仍需改成显式夹具或拦截。
本机模拟服务器需要允许 `127.0.0.1` 回环连接；受限沙箱里出现 `Permission denied` 时，
应保留失败日志并换用允许回环连接的测试环境，不能据此判断下载实现失效。

## 按改动选择模块

`tools/test-impact.ps1` 是唯一的路径到测试模块映射。本地默认比较 `origin/main...HEAD`，同时合并
未提交和未跟踪文件；分支基准不同请传 `-BaseRef`，基准无法读取会失败，不静默漏测。
可用 `./tools/test-impact.ps1 -ChangedPath src/controllers/UpgradeAssessment.cpp` 查看计划，
或 `./tools/test.ps1 -Suite affected -BaseRef HEAD~1 -ListOnly` 只列出待运行测试。
报告保留 `impact.json`，写明改动文件、模块、测试名和扩大范围的原因。

- 文档改动或无改动：跳过构建和测试。
- 更新控制器：构建应用，运行更新专项和基础 UI 专项，不运行无关的核心/主窗口巨型用例。
- 模组、网络、预设、Java、工具箱等按关联选专项；不能用“只改一个文件”作为漏掉关联测试的理由。
- 共用主窗口、设置/网络/工具基础层、构建配置、未知路径：退回全部非 visual 测试。
- Actions 的 push/PR 使用同一映射；保留 `build-and-test` 必需检查名。计划失败不能假绿。
- 定时、手动工作流和发版标签仍使用全部四个分区，不通过名称白名单限制全量集合；完整历史 UI
  仍按原定时/visual 开关执行。原分片完整性检查保持不变。

CI 的 `-Modules 'upgrade,ui,app'` 是测试名与所属分区的交集，不会把其它分区的测试重复跑一遍。
`-Suite upgrade` 现在构建独立的 `mw_check_instance_upgrade`，不再顺带编译整个正式程序；产品构建由 affected 的 app 模块
或 CI 的 ci-app 分区承担。显式 `-Suite quick/full` 保留原用途。

测试源码已物理拆分，专项不再依赖编译/链接完整的 ui_check 或 mw_check。
CTest 注册同时生成 test-targets.txt；运行器先选测试，再按归属构建独立目标。
脚本测试无需构建 C++；未知/共用改动仍扩大范围，不承诺未经远端测量的加速比例。

## 测试代码的模块边界

- tests/core_modules/：81 个原命名用例函数，各一个 .cpp；按下载、安装、实例、Java、账号、资源、
  启动、服务端、活动、预设、工具箱组织成 11 个独立可执行程序。verify_baseline 保留原 main 中的基础断言，
  12 个断言入口的并集覆盖旧核心集合；verify 兼容手动模式，CI 只检查它能链接和启动，不重复运行整套断言。
- tests/ui_check/modules/：导航、主题、任务、配置编辑、内容页、账号、日志等各自一个编译单元。
  历史顺序测试继续由 ui_check 编排，保留共享窗口和执行顺序；7 个快速 UI 专项独立构建和注册。
  ui-bootstrap 只检查完整驱动能链接、启动和读取图标，保持历史断言的编译门禁；历史 visual 仍按原策略另跑。
- tests/mw_check/modules/：31 个原命名函数、25 个历史界面接线段和 4 个快速专项各自独立 .cpp。
  已有的下载、预设、更新、Java、模组等专项也各有独立入口，不再链接完整主窗口测试驱动。
- Support.hpp/Support.cpp 只容纳共用夹具与声明；跨段变量显式传参，统计计数共享，
  历史测试不因拆文件而改变顺序或丢掉断言。三个原始入口保留编排及手动探针，不再堆放大段专项实现。
- 生产实现依次复用 wtfml_core_runtime、wtfml_ui_runtime、wtfml_mw_runtime，
  测试程序之间相同实现和 Qt 元对象不重复编译；图标资源在 UI 入口显式初始化。
  共享层分别构建，各阶段单独计时；同一专项的断言对象也供完整入口复用。

例如只改 Java 核心测试文件，可运行：

```powershell
./tools/test.ps1 -Suite affected -ChangedPath tests/core_modules/AsciiAliasTests.cpp
./tools/test.ps1 -Suite ci-core -Modules core-java
./tools/test.ps1 -Suite ci-ui -Modules theme
./tools/test.ps1 -Suite upgrade
```

完整入口仍串行；物理拆分不等于历史共享数据已完成隔离，也不代表历史堆损坏已解决。
原有 vm-batch / vm-fetch-fail / 联网安装等手动模式仍显式运行，不因拆分而偷偷加入常规 CI。

### 历史 UI 的验证边界

2026-10-21 本机 Qt 6.10.1 / MinGW 13.1、offscreen、未设置跳过变量时：
`./tools/test.ps1 -Suite visual -NoBuild -BuildDir build/ci-optimization-smoke`
得到 3055/3108（53 条失败）。保留的拆分前二进制
`build/verification-mingw13/tests/ui_check/ui_check.exe`（14:07:54 构建）在同一工作目录、同一 offscreen 环境下
也是 3055/3108，53 个失败标签逐项一致。首次日志分别保留在
`build/test-runs/20261021-190043-0beb72b5/` 和 `build/modular-ui-baseline.stdout.log`。
这说明本次对照没有新增失败项，不表示旧主题/布局判据或历史生命周期问题已经解决；完整 visual 仍未通过。

`wtfml.test-modules` 检查入口不超过 4000 行、职责模块不超过 5000 行，禁止入口 include `.cpp`，
并检查核心用例的归属表。新增用例应放进相应模块，不通过抬高阈值继续堆积入口。

## 超时、日志和失败

- 构建前关闭正在运行的 `WTFMinecraftLauncher`。`-NoBuild` 和 `-ListOnly` 不关闭窗口。
- 配置和每个目标的构建默认各限 600 秒；整个 CTest 阶段默认限 900 秒。可分别用
  `-BuildTimeoutSeconds` / `-TestTimeoutSeconds` 调整，均不得超过 900 秒。
- CTest 内各专项原有超时继续生效。核心保留 900 秒预算，避免把 CI 慢误判为死锁。
- 详细断言输出写入日志，控制台只显示末尾摘要；每 30 秒显示阶段、已耗时间和最新日志。
  超时终止该进程树，阶段记录退出码 124，脚本返回非零。
- 每次报告放到独立的 `build/test-runs/<时间>-<随机号>/`：阶段 stdout/stderr、
  `summary.json`、CTest `results.xml`（CTest 正常收尾时生成；超时以 summary 和日志为准）。
  阶段失败后停止后续阶段，不自动复跑覆盖首次失败；
  CTest 本身仍会完成本组其余用例，便于一次收集多个问题。
- 脚本运行期间 TEMP/TMP 指向本次报告内的 temp，退出后恢复环境；临时材料保留供诊断。
  不把这些临时文件自动上传，避免大文件或敏感材料混进 CI 附件。
- 同一构建目录使用命名互斥锁，第二份脚本直接报错，不与首份争用产物。
  这不能阻止绕过脚本的手动构建；运行脚本时不要另开构建。
- 旧测试仍有固定数据目录，尚未完成逐测试隔离。因此不能擅自提高 CTest 并行度。

GitHub 的 main 提交和 PR 按改动选择分片与测试；标签、定时和手动强制全量保留四路兜底。
每周和手动勾选 visual 时另跑完整 UI，不自动跳过已知问题。
同事件、同分支的新提交取消旧运行。日志与 JUnit 附件无论成功失败均尝试上传，保留 14 天。
本地和 CI 对齐 Qt 6.10.1 / MinGW 13.1；本地并不自动下载工具链。

## 新增测试约定

- 小专项实现放独立 `.cpp`，`.hpp` 只声明入口；不要再把整段实现 include 到巨型 main.cpp。
  用例放在所属模块，入口只负责夹具、调用顺序和退出状态。
- 每个专项登记到 CTest，设置标签、独立超时和运行环境，避免只在开发者手动命令中存在。
  使用 `wtfml_add_test(NAME ... COMMAND <独立目标>)` 同步登记构建归属；漏归属或重复归属明确失败。
- 每项使用独立临时目录、临时端口；夹具失败明确报错，不能让依赖该夹具的断言“假绿”。
- 功能断言和性能基线分离；不能用 offscreen、调试器或未优化构建的速度代表产品性能。
- 已知崩溃保留首次证据、独立复现，不以“重跑直到绿”为验收规则。

## 测试入口自身的回归

`./tools/test-runner-check.ps1`（CTest 名 `wtfml.test-runner`）会用独立的带空格/中文路径
构造成功、断言失败、超时和空测试集四种情况，验证退出码、环境恢复、独立报告和 JUnit 生成。
这些故意失败的内部样例不混入普通 JUnit 报告；原始输出保存在
`build/test-runner-selfchecks/<随机号>/`，只有入口自身判断失败才使本项变红。

## 后续结构优化

### 2026-10-20 CI 失败核对

- 运行 `39304391380`、`39356133554`：主程序冷构建到约 85%/165 个对象时触发单阶段
  900 秒上限（退出 124），不是编译语法错误。现有 `ci-object-batches.ps1` 将 Ninja 对象
  分批构建，保持每阶段 900 秒保护，不通过无限延长超时解决。
- 运行 `39431626583`：其它分片成功，ci-app 构建成功，唯一失败断言为网络诊断的
  `expired node is reprobed once and missing ranges complete safely`；汇总检查失败属于级联。
  安装 Qt 时的 Python EACCES 警告没有使安装步骤失败，不是本次根因。
- 加固该专项的 HTTP 夹具：仅按请求行目标路径路由，不在请求头/查询串里匹配地址；
  完整请求只响应一次。保持严格的两次调度探测断言，次数不符但下载成功时也输出计数、结果与引擎日志。
  旧日志没有实际计数，因此不能据本地通过就宣称已经确定 CI 那次断言的唯一根因。
  夹具修正连续五轮通过（95.31 秒）；新增路由回归后重建，最终再连续三轮通过（57.09 秒）。
- 本机沙箱里另有 H1 协议记录断言失败，允许回环通信后通过；与远端的过期节点断言不是同一问题。

尚未完成：逐测试路径隔离、
隔离后的安全并行、主窗口后台请求隔离、多 DPI 截图基线和内存诊断工具链。当前没有宣称已修复历史堆损坏。
