# WTFML — Wonderful Tremendous Fantastic Minecraft Launcher

C++23 + Qt 6 Widgets 写的 Minecraft 启动器，只支持 Windows。

> 这份文档只说到"装上、跑起来"为止。**里面长什么样、为什么这么长** —— 界面骨架、每个
> 功能的规矩、产品硬约束、四个验证工程 —— 在 **[FEATURES.md](FEATURES.md)**。更细的
> "为什么这么写"一律在代码边上（这个仓库的注释密度很高，那才是唯一出处）。

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

**数据目录 = 当前选中的游戏文件夹**，不是 exe 所在目录。所以：

| 什么 | 在哪 |
| --- | --- |
| 日志 | `<数据目录>/logs/wtfml.log`，**每次启动截断重写** |
| 启动器自己的状态、账户表 | `HKCU\Software\WTFML`（令牌是 DPAPI 加密后落盘的） |
| 图标 / 主题图 / 备份 等缓存 | `<数据目录>/cache/` |

GUI 子系统下面**没人看得见 stdout / stderr**，那份日志文件就是"当时到底发生了什么"的
第一现场；想让日志同时进终端就设 `QT_FORCE_STDERR_LOGGING=1`。

## 项目结构

| 目录 | 内容 |
| --- | --- |
| `src/launcher/` | 启动：命令行拼接、继承链合并、natives、进程优先级 |
| `src/auth/` | 微软登录全链、账户表、皮肤与披风、令牌落盘（DPAPI） |
| `src/manage/` | 文件夹与实例、实例分组、整合包导入/导出、服务器状态缓存 |
| `src/download/` | 版本清单/JSON/库/资产下载、加载器元数据、资源（模组那五类）、服务端那八档的安装判据 |
| `src/settings/` | `GlobalJavaSettings`（Java 与内存）、`LauncherSettings`（启动器自己的状态） |
| `src/net/` | `HttpClient` |
| `src/util/` | 与业务无关的小工具（`AppPaths` / `ZipUtil` / `NbtUtil` / `ServerPing` / …） |
| `ui/` | 全部界面。**没有 .ui 文件**，界面一律手写；`ui/` 不许 include `src/` |
| `assets/` | 编进 exe 的资源（`icons.qrc`），来路见 `assets/loader/README.md` |

## 开发与验证

仓库里的本地预设与上面的安装位置一致（Qt 6.10 + `D:/Qt/Qt/Tools/mingw1310_64`），
可以直接运行：

```bash
cmake --preset dev-debug
cmake --build --preset dev-debug

cmake --preset verification
cmake --build --preset verification
ctest --preset verification
```

三套纳入 CTest 的回归程序在 `tests/`：`wtfml.core`（数据层）、`wtfml.ui`
（offscreen 控件与像素）和 `wtfml.main-window`（真 `MainWindow`）。默认验证与 CI 会在
Windows 上编译 Release、打开严格警告并运行两套确定性测试；已知受 Qt offscreen 驱动影响的
像素探针用 `ctest --preset visual` 单独运行。测试夹具也跟源码一起受版本控制。

- **改代码前先读它周围的注释** —— 每一处"为什么这么写"就写在那一行边上。
- 验证工程的细节、当前基线计数和已知的 Qt offscreen 限制，见
  [FEATURES.md](FEATURES.md) 的「验证工程」一节。
- 本地凭据 / 私有文件（`LOCAL_FILES.md`、`dev-login.json`、`credential.json`）**不入库**，
  也不要贴出来或同步到别处。
- 注释约定见 [CONTRIBUTING_COMMENT.md](CONTRIBUTING_COMMENT.md)。

## 许可

见 [LICENSE](LICENSE)。
