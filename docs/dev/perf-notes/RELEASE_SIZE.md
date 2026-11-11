# 发布体积：去符号、`-Os` 与 LTO 的实测

给打发布包的人看：静态单文件怎么构建、去符号能省多少、哪些优化选项试过不要或编不过。2026-11-01 实测，同一台机器、同一份仓库（`bcacc87` 之上）、同一会话连着编，每次全新构建目录、`--parallel 12`。

## 结论

1. **发布副本去符号划算**：静态单文件 `--strip-unneeded` 后 73,531,415 → 43,288,540 字节（−41.1%），导入 DLL 清单逐条相同，真启动 3 次没有变慢。已落成 `tools/prepare-release-copy.ps1`，只对副本动手，原构建目录一个字节不改。
2. **不用 `-Os`（MinSizeRel）**：再省约 4.9 MB（43.29 → 38.42 MB，−11%），但启动慢约 50 ms（中位 189 → 238 ms，26%）。启动每个用户每次都付，体积只付一次。
3. **LTO 在这条工具链上链接不过**：`-flto=auto` 下报 16 个自家符号未定义，去掉 `-flto` 正常。

## 怎么构建与量

静态 Qt 是 `D:/Qt/static-6.10.1/install`，**编译器必须用 `C:/mingw64`（MinGW-Builds GCC 15.2，mcf 线程模型）**：那份 Qt 就是用它编的，用开发用的 13.1 会在 `_MCF_mutex_lock_slow` / `_MCF_mutex_unlock_slow` / `__intrinsic_setjmpex` 上链接失败。

```powershell
cmake -S . -B build/static-release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH=D:/Qt/static-6.10.1/install `
      -DCMAKE_C_COMPILER=C:/mingw64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/mingw64/bin/g++.exe `
      -DWTFML_BUILD_TESTS=OFF
cmake --build build/static-release --parallel 12
pwsh -File tools/prepare-release-copy.ps1 -BuildDir build/static-release
```

启动量产品 exe 本身（外部测量，口径同 [PERFORMANCE.md](../PERFORMANCE.md) 的"启动与就绪"：visible / ready / `WM_NULL` 最长阻塞），每次 3 跑、0 个预置实例：

```powershell
pwsh -File tools/perf-startup.ps1 -Exe <要量的那一份> -Runs 3 -Instances 0 -StallSeconds 2 `
     -QtBin <一个空目录>
```

注意：`perf-startup.ps1` 默认把动态 Qt 的 `bin` 加进 PATH、把 `QT_PLUGIN_PATH` 指到那份插件目录，静态 exe 不吃这一套，所以 `-QtBin` 要指一个空目录。

## 数字

大小（同一份 `-O3` 静态单文件）：

| 处理 | 字节 | 相对原件 |
| --- | ---: | ---: |
| 原件（`build/static-release`，`-O3`） | 73,531,415 | — |
| `strip --strip-unneeded` 副本 | 43,288,540 | −30,242,875（−41.1%） |
| `objcopy --only-keep-debug` 符号文件 | 30,282,263 | 不进发布包 |

导入清单 49 个，全是系统 DLL / `api-ms-win-*`，去符号前后一致：没有 `Qt6*.dll`、`libstdc++-6`、`libgcc_s_seh-1`、`libwinpthread-1`。动态那份（开发用 `build/release-mingw13`）同样处理只省 24.4%（16,609,159 → 12,559,435），静态链接把 Qt 的符号表也带进了 exe。

启动（3 跑，ms）：

| 构建 | visible 中位 | 范围 | ready 中位 | maxBlock 中位 |
| --- | ---: | --- | ---: | ---: |
| `-O3` 原件 | 189 | 188–199 | 208 | ≤ 4.5 |
| `-O3` 去符号副本 | 185 | 171–190 | 196 | ≤ 1.6 |
| `-Os` 原件 | 238 | 233–247 | 246 | ≤ 9.7 |
| `-O3` 去符号副本 + `QT_OPENGL=software` | 242 / 231（2 跑） | — | 261 / 256 | ≤ 2.8 |

去符号后两组范围重叠，没有启动代价；软件渲染那行说明开着 `QT_OPENGL=software` 窗口能画出来、消息循环正常。

构建时间都是单次测量（`-O3` 1 分 46 秒、`-Os` 2 分 21 秒、LTO 1 分 48 秒到链接），不是稳定基线。

## LTO 链接失败

`-O3 -flto=auto`（链接也带）编到链接失败：

```
ld.exe: .ltrans1.ltrans.o: undefined reference to `DownloadManager::beginBatch()'
        …（共 16 个，全部是本项目自己定义的符号，含 DownloadManager::* 和 DownloadDiag::setSink）
```

编译本身没问题，同一份源码去掉 `-flto` 链接成功。静态 Qt 的 interface 带 `-Wl,--gc-sections`，怀疑是 LTO 与节区回收在 PE 目标上的组合问题，没能单独验证：手动重跑链接命令（`--gc-sections` 后接 `--no-gc-sections`）时 ld 先报 `plugin needed to handle lto object`。要再试 LTO，先解决"链接要能过"这一条。

## `tools/prepare-release-copy.ps1`

读构建目录 → 复制副本 → 只对副本 `--strip-unneeded` → 导入清单逐条比对 → 调试信息单独留一份（`objcopy --only-keep-debug`，再挂 `--add-gnu-debuglink`）→ 写 `release-copy.json`（两边字节数、SHA-256、导入清单、原件路径）。最后再核一次原件的 SHA-256，期间有人构建就报错退出，不留来路说不清的副本。脚本不启动任何程序，不碰原构建目录。`README.md` 的静态构建段已补上必须指定 `C:/mingw64` 编译器和去符号这一步。发布流程是否切到这份副本由用户决定。

证据目录：`build/tmp-wtfml-size/`（`perf-static-base`、`perf-static-slim`、`perf-os-base`、`perf-slim-softwaregl`、各份 `release-copy.json`、`build*.log`）。

## 未覆盖

| 项 | 状态 |
| --- | --- |
| 低端 GPU / 干净机器的软件渲染与驱动依赖 | 只在本机用 `QT_OPENGL=software` 起过窗口 |
| 插件与翻译目录的细核对 | 静态构建里平台插件编进 exe，`windeployqt` 那套不适用，未另行核对 |
| 去符号副本的界面外观 | 启动探针只看窗口出现、能应答，不看画面 |
| LTO 的真实收益 | 链接没过，无从比较 |
