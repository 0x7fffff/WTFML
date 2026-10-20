# 测试与验证

## 统一入口（Windows / PowerShell 7）

在仓库根目录运行，先确保 CMake、所用 MinGW 和 Qt 的 bin 目录在 PATH 中：

```powershell
./tools/test.ps1                         # 构建后运行快速专项
./tools/test.ps1 -Suite java             # Java 管理
./tools/test.ps1 -Suite mods             # 模组操作栏、迁移与依赖检查
./tools/test.ps1 -Suite upgrade          # 原地升级、完整快照和故障回滚
./tools/test.ps1 -Suite network          # 本机可控服务器下载测试，不访问外网
./tools/test.ps1 -Suite core             # 核心回归
./tools/test.ps1 -Suite window           # 主窗口集成回归
./tools/test.ps1 -Suite full             # 全部非 visual 回归（提交前）
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
| `-Suite ci-core` | verify | 核心、测试入口自检、分片完整性检查 |
| `-Suite ci-ui` | ui_check | 控件 UI 专项，不含历史 visual |
| `-Suite ci-integration` | mw_check | 主窗口及全部集成专项 |

每份 CMake 测试目录自动附加所属分片标签，新测试无需复制维护一份名称白名单。
`wtfml.ci-partitions` 对比四组的并集与 full：缺项、重复、空组都会失败，不能靠少跑测试提速。
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

GitHub 的 main 提交、版本标签和 PR 跑与 full 等价的四路分片；功能分支 push 不再与其 PR 重复构建。
每周和手动勾选 visual 时另跑完整 UI，不自动跳过已知问题。
同事件、同分支的新提交取消旧运行。日志与 JUnit 附件无论成功失败均尝试上传，保留 14 天。
本地和 CI 对齐 Qt 6.10.1 / MinGW 13.1；本地并不自动下载工具链。

## 新增测试约定

- 小专项实现放独立 `.cpp`，`.hpp` 只声明入口；不要再把整段实现 include 到巨型 main.cpp。
  JavaManageChecks / ModActionPanelChecks / RangeChecks / SegmentInstallChecks /
  ToolFeatureChecks / PresetInstallChecks 六份专项已采用这一结构，巨型入口中的历史分段待分批迁移。
- 每个专项登记到 CTest，设置标签、独立超时和运行环境，避免只在开发者手动命令中存在。
- 每项使用独立临时目录、临时端口；夹具失败明确报错，不能让依赖该夹具的断言“假绿”。
- 功能断言和性能基线分离；不能用 offscreen、调试器或未优化构建的速度代表产品性能。
- 已知崩溃保留首次证据、独立复现，不以“重跑直到绿”为验收规则。

## 测试入口自身的回归

`./tools/test-runner-check.ps1`（CTest 名 `wtfml.test-runner`）会用独立的带空格/中文路径
构造成功、断言失败、超时和空测试集四种情况，验证退出码、环境恢复、独立报告和 JUnit 生成。
这些故意失败的内部样例不混入普通 JUnit 报告；原始输出保存在
`build/test-runner-selfchecks/<随机号>/`，只有入口自身判断失败才使本项变红。

## 后续结构优化

尚未完成：旧三个 main.cpp 的全面拆分、共享业务/UI 库以避免重复编译、逐测试路径隔离、
隔离后的安全并行、主窗口后台请求隔离、多 DPI 截图基线和内存诊断工具链。当前没有宣称已修复历史堆损坏。
