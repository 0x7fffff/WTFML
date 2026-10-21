# WTFML — Wonderful Tremendous Fantastic Minecraft Launcher

C++23 + Qt 6 Widgets 写的 Minecraft 启动器，只支持 Windows。

> **文档分工**
>
> - 安装、构建、运行、目录结构：本文件。
> - 当前功能行为、产品硬约束、数据约定、验证边界：**[FEATURES.md](FEATURES.md)**。
> - 还没完成的工作：**[CODING_PLAN_FULL.md](CODING_PLAN_FULL.md)**。
> - 注释规范：**[CONTRIBUTING_COMMENT.md](CONTRIBUTING_COMMENT.md)**。
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
Qt 的 dll 在 `PATH` 上。要**真·单文件**（一个自包含的 exe，不带任何 dll）就换静态 Qt，
CMakeLists 一个字都不用改：

```bash
cmake -S . -B build/static-release -G "MinGW Makefiles" \
      -DCMAKE_PREFIX_PATH=D:/Qt/static-6.10.1/install \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build/static-release -j
```

版本号**只有一个出处**：`CMakeLists.txt` 的 `project(... VERSION x.y.z)`，编成
`WTFML_VERSION` 宏交给界面显示。

## 跑起来之后

**数据目录 = 当前选中的游戏文件夹**，不是 exe 所在目录。

| 什么 | 在哪 |
| --- | --- |
| 日志 | `<数据目录>/logs/wtfml.log`，**每次启动截断重写** |
| 启动器状态、账户表 | `HKCU\Software\WTFML`（令牌是 DPAPI 加密后落盘的） |
| 图标 / 主题图 / 备份等缓存 | `<数据目录>/cache/` |

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

- `verify` / `wtfml.core`：数据层纯函数与文件格式；
- `ui_check`：offscreen 控件、布局与像素探针；
- `mw_check`：真 `MainWindow` 与跨控制器集成。

默认验证与 CI 跑非 visual 的确定性集合；依赖真机桌面或已知受 Qt offscreen 影响的像素探针
用 `ctest --preset visual` 单独运行。测试基线、已知离屏限制和验证原则见
[FEATURES.md](FEATURES.md) 的「验证工程」，命令细节见 [docs/TESTING.md](docs/TESTING.md)。

- **改代码前先读它周围的注释** —— 例外、坑点和“为什么不能换一种写法”写在代码边上。
- 新功能/行为变化同步更新 `FEATURES.md`；未完成事项只进 `CODING_PLAN_FULL.md`。
- 本地凭据 / 私有文件（`LOCAL_FILES.md`、`dev-login.json`、`credential.json`）**不入库**，
  也不要贴出来或同步到别处。

## 许可

见 [LICENSE](LICENSE)。
