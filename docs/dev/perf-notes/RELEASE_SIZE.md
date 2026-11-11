# P2「发布体积与依赖」：去符号、体积优化与 LTO 的实测（2026-11-01）

这一轮的结论先说：

1. **发布副本去符号是划算的**：静态单文件 `--strip-unneeded` 之后 73,531,415 → 43,288,540 字节
   （省 41.1%），导入 DLL 清单逐条相同，真启动 3 次没有变慢。这件事已经落成一个工具
   （`tools/prepare-release-copy.ps1`），**只对副本动手，原构建目录一个字节不改**。
2. **`-Os`（MinSizeRel）不要**：再去掉约 4.9 MB，但启动慢约 50 ms（中位 189 → 238 ms），
   不划算。
3. **LTO 在这条工具链上编不过**：`-flto=auto` 下链接报 16 个自家符号未定义
   （`DownloadManager::*`、`DownloadDiag::setSink`）。同一条链接命令去掉 `-flto` 正常。
   这一轮不采用，原因见下文，不写成"LTO 没用"。

## 怎么量的

同一台机器、同一份仓库（`bcacc87` 之上）、同一次会话里连着编；每次都是全新构建目录、
`--parallel 12`。静态 Qt 是 `D:/Qt/static-6.10.1/install`，**编译器必须用 `C:/mingw64`
（MinGW-Builds GCC 15.2，mcf 线程模型）** —— 那份 Qt 就是用它编的，用开发用的 13.1 会在
`_MCF_mutex_lock_slow` / `_MCF_mutex_unlock_slow` / `__intrinsic_setjmpex` 上链接失败。

```powershell
cmake -S . -B build/static-release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH=D:/Qt/static-6.10.1/install `
      -DCMAKE_C_COMPILER=C:/mingw64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/mingw64/bin/g++.exe `
      -DWTFML_BUILD_TESTS=OFF
cmake --build build/static-release --parallel 12
pwsh -File tools/prepare-release-copy.ps1 -BuildDir build/static-release
```

启动量的是产品 exe 本身（外部测量，口径同 `docs/dev/PERFORMANCE.md` 第二节：visible / ready /
`WM_NULL` 最长阻塞），每次 3 跑、0 个预置实例、`StallSeconds 2`：

```powershell
pwsh -File tools/perf-startup.ps1 -Exe <要量的那一份> -Runs 3 -Instances 0 -StallSeconds 2 `
     -QtBin <一个空目录>
```

★ `perf-startup.ps1` 默认把**动态** Qt 的 `bin` 加进 PATH、把 `QT_PLUGIN_PATH` 指到那份
插件目录 —— 静态 exe 不吃这一套，所以 `-QtBin` 指一个空目录，让环境干净。
证据目录：`build/tmp-wtfml-size/`（`perf-static-base`、`perf-static-slim`、`perf-os-base`、
`perf-slim-softwaregl`、各份 `release-copy.json`、`build*.log`）。

## 数字

### 大小（同一份 -O3 静态单文件）

| 处理 | 字节 | 相对原件 |
| --- | ---: | ---: |
| 原件（`build/static-release`，`-O3`） | 73,531,415 | — |
| `strip --strip-unneeded` 副本 | 43,288,540 | **−30,242,875（−41.1%）** |
| `objcopy --only-keep-debug` 出来的符号文件 | 30,282,263 | （不进发布包） |

导入清单（49 个，全是系统 DLL / `api-ms-win-*`）去符号前后完全一致：没有 Qt6\*.dll、
没有 `libstdc++-6` / `libgcc_s_seh-1` / `libwinpthread-1`，静态单文件这件事仍然成立。

**为什么静态那份省得这么多**：动态那份（开发用的 `build/release-mingw13`）同样的处理只省
24.4%（16,609,159 → 12,559,435）—— 静态链接把 Qt 的符号表也一起带进 exe 了。

### 启动（3 跑，单位 ms）

| 构建 | visible（中位） | 范围 | ready（中位） | maxBlock 中位 |
| --- | ---: | --- | ---: | ---: |
| `-O3` 原件 | 189 | 188–199 | 208 | ≤ 4.5 |
| `-O3` 去符号副本 | **185** | 171–190 | 196 | ≤ 1.6 |
| `-Os`（MinSizeRel）原件 | 238 | 233–247 | 246 | ≤ 9.7 |
| `-O3` 去符号副本 + `QT_OPENGL=software` | 242 / 231（2 跑） | — | 261 / 256 | ≤ 2.8 |

读法：

- 去符号**没有**让启动变慢（两组范围重叠，中位数还低 4 ms —— 这 4 ms 不当结论，
  只说明"没有代价"）。
- `-Os` 比 `-O3` 慢约 50 ms（26%），换 4.9 MB（43.29 → 38.42 MB，再省 11%）——
  启动是每个用户每次都要付的，体积只付一次，**不要这一换**。
- 软件渲染那一行只证明"开着 `QT_OPENGL=software` 也能把窗口画出来、消息循环照常转"，
  **不是**低端 GPU / 干净机器上的验收（那需要真机，见「没验到的」）。

### LTO

`-O3 -flto=auto`（链接也带 `-flto=auto`）编到链接这一步失败：

```
ld.exe: .ltrans1.ltrans.o: undefined reference to `DownloadManager::beginBatch()'
        …（共 16 个，全部是本项目自己定义的符号）
```

- 编译本身没问题（1 分 48 秒到链接），同一份源码去掉 `-flto` 就链接成功。
- 静态 Qt 的 interface 里带了 `-Wl,--gc-sections`。怀疑是 LTO + 节区回收在 PE 目标上的组合
  问题，但**没能单独验证**：手动重跑那条链接命令（`--gc-sections` 后面接
  `--no-gc-sections`）时，ld 先报 `plugin needed to handle lto object`，没走到能判断的地方。
  所以这里只记现象，不把"是 `--gc-sections` 的锅"写成结论。
- 因此这一轮不引入 LTO，也不改默认构建。要再试的话，先解决"LTO 链接要能过"这一条。

## 落地的东西

- `tools/prepare-release-copy.ps1`：读构建目录 → 复制副本 → 只对副本 `--strip-unneeded` →
  导入清单逐条比对 → 调试信息单独留一份（`objcopy --only-keep-debug`，再挂
  `--add-gnu-debuglink`）→ 写 `release-copy.json`（两边字节数、SHA-256、导入清单、原件路径）。
  最后再核一次原件的 SHA-256：期间有人构建就报错退出，不留"来路说不清"的副本。
  它**不启动任何程序**，也不碰原构建目录。
- `README.md`：静态构建那段补上必须显式指定 `C:/mingw64` 的编译器，以及去符号副本这一步。

发布流程里要不要切到这份精简副本，是用户的决定；工具只负责造出来并留下证据。

## 没验到的（别当成已经过了）

- **低端 GPU / 干净机器**上的软件渲染与显卡驱动依赖：只在本机用 `QT_OPENGL=software`
  起过窗口，没有在另一台机器上跑。
- 更细的插件与翻译目录核对（`windeployqt` 那一套在静态构建里不适用：平台插件是编进 exe 的）。
- 去符号副本的界面外观：启动探针只看"窗口出现、能应答"，不看画面。
- LTO 的真实收益（没连上，谈不上比较）。
- 构建时间都是**单次**测量（`-O3` 1 分 46 秒、`-Os` 2 分 21 秒、LTO 1 分 48 秒到链接），
  没有取中位数，别当稳定基线。
