# 2026-11-09 真机验收记录

本记录只写**这一次真机跑到的事实**；离屏全绿不等于真机验收。

## 1. 双进程目录边界（PASS）

命令：

```powershell
python tools/acceptance_directory_boundary.py `
  --exe build/static-release/WTFMinecraftLauncher.exe `
  --root build/acceptance-boundary-1109 `
  --qt-bin D:/Qt/Qt/6.10.1/mingw_64/bin
```

结果：16/16 PASS。

- 首个真实产品入口显示自己的原生主窗口；
- 同一数据目录的第二个进程显示并截图真实拒绝对话框，不建主窗口、以 1 退出；
- 私有 INI、并发日志、凭据脱敏、marker（缺 marker / 相对路径 / 缺值 / 重复）全部通过；
- 证据：`build/acceptance-boundary-1109/report.json`、各 `*.stderr.log`。

## 2. 窄窗口 + 150% 缩放（PASS）

通过 acceptance profile 新增的 `windowWidth` / `windowHeight` 字段把窗口钉在 1000×750：

```json
{ "format": "wtfml-startup-acceptance/1", "closeAfterMs": 8000,
  "windowWidth": 1000, "windowHeight": 750 }
```

结果：`platform=windows`、`mainWindowShown=true`、`windowWidth=1000`、`windowHeight=750`、
`screenshot=true`，实际截图 1500×1125（150% DPI）。

截图确认：顶部导航、主页 hero、账号卡片、快捷入口/「列表」按钮、底栏三格都在窗口内，无控件被裁。

证据：`build/acceptance-narrow-1109/startup-24120.json`、`window-24120.png`。

## 3. 大实例升级（1 GiB + 6000 文件，kill 在 publish）

```powershell
python tools/acceptance_large_instance.py `
  --verify-exe build-audit/tests/verify_instances.exe `
  --qt-bin D:/Qt/Qt/6.10.1/mingw_64/bin `
  --work-dir build/acceptance-20261109e `
  --payload-mb 1024 --files 6000 --kill-stage publish `
  --json-out build/acceptance-20261109e/large.json
```

结果：PASS (0 failure)。

| 阶段 | 耗时 |
| --- | --- |
| large-copy | 4.62 s |
| locked-copy | 5.01 s |
| cancelled-upgrade | 1.56 s |
| kill-at-publish | 外部终止在 publish 阶段 |
| recovery | 0.05 s（原实例/用户文件/1 GiB 载荷/原 jar 完整） |
| retry-upgrade | 57.44 s（checking 10.5 s → verify-snapshot 34.9 s → retain-original 54.2 s → publish 54.2 s → done 57.4 s） |

证据：`build/acceptance-20261109e/large.json`。

## 4. 完整核心验证（PASS）

`build-audit/tests/verify.exe`（真实 Windows，非 offscreen）：

```
===== 全部通过: 6508/6508 =====
```

## 5. 本轮发现并修复的问题

### ZipUtil 压缩炸弹闸误杀合法内容

第一次 64 MB 验收失败：

```
「Acceptance/saves/World/region/large.bin」的压缩比超过 1000，按压缩炸弹拦下了
```

全零 region（以及真实世界里大量空区块）是合法内容，但被 1000 阈值当成压缩炸弹，
导致升级快照自校验失败。

修复：

- `ZipUtil` 两处解压脚本阈值 **1000 → 10000**；
- 每条目 2 GiB、整包 16 GiB 的硬上限不变；
- `tests/core_modules/BackupTests.cpp` 增加回归：8 MB 全零内容打包后必须能解压。

## 6. 未完成 / 环境限制

- `assoc-live`：当前会话写 `HKCU\Software\Classes\WTFMLSelfTest` 被拒（错误 5），
  属环境限制，需正常桌面会话复跑；
- `sevenzip-live`：本机未安装 7-Zip（无 `7z.exe`），未跑；
- `java-runtime-live`：本轮发起过 17 运行时下载探针，约 20 MB 后超过 25 分钟无进展，已终止，未作为通过项记录；
- 真机 GUI 的拖拽白块、任务中心关闭动画等仍需人工目视确认，acceptance 截图不覆盖交互过程。


## 7. 设置页 + 任务中心的上层覆层（PASS，真实 GUI）

新增探针：mw_check.exe overlay-live <root> <instanceId>。

- 真实 MainWindow（windows 平台、非 offscreen）打开实例设置；
- 点任务按钮：PASS 设置页开着时任务中心叠成提示层（原页还在）；
- 再点一次任务按钮：PASS 再点一次任务按钮回到原设置页。

同一次日志里 overlay show settings = 9 ms、settings first idle = 11 ms（懒建文件页 + 覆层收尾之后的本机结果）。


## 8. 大列表分帧建行（真实 GUI，1000 个 jar）

探针：mw_check.exe overlay-live <root> probe-1000 mod（临时实例，1000 个 32 B 的 mod-*.jar）。

`	ext
settings dialog ctor 12 ms
overlay show settings 9 ms
settings first idle 17 ms
PASS 页面 mod 行=1000，分帧建行完成 362 ms
PASS 设置页开着时任务中心叠成提示层（原页还在）
PASS 再点一次任务按钮回到原设置页
`

即：打开设置 9 ms；1000 行在 362 ms 内分帧全部建完（首帧只建 80 行控件）。


## 9. 可见范围行控件窗口化（真实 GUI，1000 行）

探针：mw_check.exe overlay-live <root> probe-win4 mod（1000 个 jar，真实 windows 平台）。

`	ext
PASS 页面 mod 行=1000 首行出现=141 ms 滚到底=961 ms 末行控件=1 顶行回收=1
PASS 设置页开着时任务中心叠成提示层（原页还在）
`

修复点：滚动条 alueChanged 的连接曾被并进注释行（实际没接上）；分帧全部建完后补一次 nsureRowWidgetWindow()，否则「滚到底后新建的顶部行」会盖过回收结果。


## 10. 目录页窗口化与统计缓存（真实 GUI，500 个存档）

探针：mw_check.exe overlay-live <root> probe-save save。

`	ext
PASS 页面 save 行=500 首行出现=96 ms 滚到底=912 ms 末行控件=1 顶行回收=1
`

save 页 500 个世界目录：滚到底后末行重建、顶行回收；已算过的目录大小/时间进 m_statCache，滚回来直接显示，不重复递归。


## 11. 2026-11-10 全量核心回归（PASS）

窗口化/目录统计缓存/预设快速写之后，真实 Windows 上重跑 uild-audit/tests/verify.exe：

`	ext
===== 全部通过: 6508/6508 =====
`

