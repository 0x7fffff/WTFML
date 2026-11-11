# WTFML — Wonderful Tremendous Fantastic Minecraft Launcher

C++23 + Qt 6 Widgets 写的 Minecraft 启动器，只支持 Windows。

> **文档分工**
>
> - 安装、构建、运行、目录结构：本文件。
> - 当前功能行为、产品硬约束、数据约定、验证边界：**[FEATURES.md](docs/FEATURES.md)**。
> - 还没完成的工作：**[ROADMAP.md](docs/ROADMAP.md)**。
> - 注释规范：**[COMMENT_STYLE.md](docs/dev/COMMENT_STYLE.md)**。
>
> `FEATURES.md` 记录“现在仍然成立的约定”，不是开发日志；已完成事项不再按日期追加。

## 构建

需要 **Qt 6.10 + MinGW 13.1**。动态那份（开发用）：

```bash
cmake -S . -B build -G "MinGW Makefiles" \
      -DCMAKE_PREFIX_PATH=D:/Qt/Qt/6.10.1/mingw_64 \
      -DCMAKE_CXX_COMPILER=D:/Qt/Qt/Tools/mingw1310_64/bin/g++.exe \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

产物 `build/WTFMinecraftLauncher.exe`。它是 **GUI 子系统**（跑起来不弹控制台），所以要
Qt 的 dll 在 `PATH` 上。

默认会预编译一份 Qt 控件头（`WTFML_USE_PCH`），主程序的全量构建 95 → 75 秒；代价是构建
目录里多一份 177 MB 的 `.gch`。不想用就 `-DWTFML_USE_PCH=OFF`，量法与取舍见
[docs/dev/PERFORMANCE.md](docs/dev/PERFORMANCE.md) 最后一节。要**真·单文件**（一个自包含的 exe，不带任何 dll）就换静态 Qt，
CMakeLists 一个字都不用改：

```bash
cmake -S . -B build/static-release -G "MinGW Makefiles" \
      -DCMAKE_PREFIX_PATH=D:/Qt/static-6.10.1/install \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=C:/mingw64/bin/gcc.exe \
      -DCMAKE_CXX_COMPILER=C:/mingw64/bin/g++.exe
cmake --build build/static-release -j
```

★ **静态那份必须用编静态 Qt 的那个工具链**。`D:/Qt/static-6.10.1` 是 `C:/mingw64`
（MinGW-Builds，GCC 15.2，mcf 线程模型）编的；拿开发用的 `D:/Qt/Qt/Tools/mingw1310_64`
去链，会在 `_MCF_mutex_lock_slow` / `_MCF_mutex_unlock_slow` / `__intrinsic_setjmpex`
上链接失败 —— 那几个符号是 mcfgthread 的，13.1 那份没有。上面显式指定编译器就是为了
不靠 PATH 的运气。

要拿去发布的那一份再走 `tools/prepare-release-copy.ps1`：只对**副本**去符号，
原构建目录不动，调试信息单独留一份。

版本号**只有一个出处**：`CMakeLists.txt` 的 `project(... VERSION x.y.z)`，编成
`WTFML_VERSION` 宏交给界面显示。

## 跑起来之后

**数据目录 = 当前选中的游戏文件夹**，不是 exe 所在目录。

| 什么 | 在哪 |
| --- | --- |
| 日志 | `<数据目录>/logs/wtfml.log`，**每次启动截断重写** |
| 启动器状态、账户表 | `HKCU\Software\WTFML`（令牌是 DPAPI 加密后落盘的） |
| 图标 / 主题图 / 备份等缓存 | `<数据目录>/cache/` |
| 智能体会话（全局，不跟游戏文件夹） | `%LOCALAPPDATA%\WTFML\agent\sessions`；API Key 在 Windows 凭据管理器/DPAPI |

同一个数据目录同时只允许一个启动器窗口。第二个进程在写日志、开主窗口之前就会被拒绝并退出；
边界细节见 [docs/features/DIRECTORY_SESSION_LOCK.md](docs/features/DIRECTORY_SESSION_LOCK.md)。

GUI 子系统下面**没人看得见 stdout / stderr**，那份日志文件就是“当时到底发生了什么”的
第一现场；想让日志同时进终端就设 `QT_FORCE_STDERR_LOGGING=1`。

## 项目结构

| 目录 | 内容 |
| --- | --- |
| `src/launcher/` | 启动：命令行拼接、继承链合并、natives、进程优先级 |
| `src/auth/` | 微软登录链、账户表、皮肤与披风、令牌落盘（DPAPI） |
| `src/manage/` | 文件夹与实例、分组、整合包导入/导出、备份、世界/服务器文件 |
| `src/download/` | 版本/加载器/资源/服务端下载与安装判据、下载器与限速 |
| `src/presets/` | 实例预设的数据模型与存储 |
| `src/settings/` | `GlobalJavaSettings`（Java/内存）、`LauncherSettings`（启动器状态） |
| `src/net/` | `HttpClient` |
| `src/controllers/` | 连接 UI 与业务层的控制器，避免继续扩大 `MainWindow` |
| `src/server/` | 服务端属性、控制台、进程管理 |
| `src/tools/` | 工具箱的纯函数/后台任务 |
| `src/agent/` | 智能体核心：provider/SSE、会话存储/加密、上下文、Loop、受控文件工具/审批、D11 外发、路径校验、脱敏、凭据存储 |
| `src/diag/` | 启动诊断、游戏退出诊断、错误报告 |
| `src/util/` | 与业务无关的小工具（`AppPaths` / `ZipUtil` / `NbtUtil` / `ServerPing` / …） |
| `ui/` | 全部界面。**没有 .ui 文件**，界面一律手写；`ui/` 不许 include `src/` |
| `assets/` | 编进 exe 的资源（`icons.qrc`），来路见 `assets/loader/README.md` |

## 开发与验证

仓库里的本地预设与上面的安装位置一致，可以直接运行：

```bash
cmake --preset dev-debug
cmake --build --preset dev-debug

cmake --preset verification
cmake --build --preset verification
ctest --preset verification
```

三套纳入 CTest 的回归程序在 `tests/`：

- `verify`：数据层纯函数与文件格式。**已按域拆成多个可执行程序**（`verify_<域>`，另有基线
  那一个），`ctest --preset verification` 一次跑全；
- `ui_check`：offscreen 控件、布局与像素探针；
- `mw_check`：真 `MainWindow` 与跨控制器集成。

日常改完代码用 `tools/test.ps1` 按套件跑（它会挑要建的目标，再调 ctest）：

```powershell
./tools/check-test-modules.ps1                       # 测试入口/模块归属/大小上限
./tools/check-doc-links.ps1                          # 文档相对链接与源码里的 docs 引用
./tools/test.ps1 -Suite quick                        # 十几秒的冒烟
./tools/test.ps1 -Suite full -TestJobs 1             # CI 同款：按标签排除 visual
```

**受限令牌环境**（Windows 沙箱 / 某些企业策略）下子进程只能写工作区，而测试要建临时目录。
这时给 `tools/test.ps1` 指一个工作区里的临时根，否则 `QTemporaryDir` 会拿到"拒绝访问"：

```powershell
$env:WTFML_TEST_TEMP = 'D:\Desktop\the_project\.tmp-test'
```

> 2026-11-05 核对：预设 `dev-debug` / `release` / `verification` / `ci-verification` 与本节命令
> 一致；`verification` 的产物在 `build/verification-mingw13`；`WTFML_USE_PCH` 默认 ON。

默认验证与 CI 跑非 visual 的确定性集合；依赖真机桌面或已知受 Qt offscreen 影响的像素探针
用 `ctest --preset visual` 单独运行。测试基线、已知离屏限制和验证原则见
[FEATURES.md](docs/FEATURES.md) 的「验证工程」，命令细节见 [docs/dev/TESTING.md](docs/dev/TESTING.md)。

- **改代码前先读它周围的注释** —— 例外、坑点和“为什么不能换一种写法”写在代码边上。
- 新功能/行为变化同步更新 `FEATURES.md`；未完成事项只进 `ROADMAP.md`。
- 本地凭据 / 私有文件（`LOCAL_FILES.md`、`dev-login.json`、`credential.json`）**不入库**，
  也不要贴出来或同步到别处。

## 许可

见 [LICENSE](LICENSE)。

