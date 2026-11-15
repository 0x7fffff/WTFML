# 大列表：首绘、重列、滚动与选择

给改 `ui/InstanceFilesPage*` 或任何"一行一个真控件"的列表的人看：实例文件页 1000 行级目录的实测、现行画行做法、反选的批量选择，以及怎么复测。

## 现行做法

实例文件页（`ui/InstanceFilesPage`）是条目最多的列表（模组目录几百到几千个文件是常态）。2026-11-14（步骤 139）起**每一行由 `InstanceFilesPage::RowDelegate`（`ui/InstanceFilesPageRows.cpp`）画**，不再 `setItemWidget()` 挂真控件：

1. **数据全在条目上**：路径 `pathRole()`、禁用 `disabledRole()`、文件夹 `isDirRole()`、大小·时间 `metaRole()`、名字在 `Qt::AccessibleTextRole`（读屏读它；DisplayRole 故意空着，免得列表按首字母跳行）、图标在 `DecorationRole`、开关是 `ItemIsUserCheckable` + `CheckStateRole`。`rebuildRows()` 只 `clear()` + 逐条 `addItem()`，视图和视口不重建。
2. **一行的格子位置只算一处**：`layoutRow()` 给图标 / 名字 / 小字 / `[详情][开关][笔][垃圾桶]` 的矩形，`paint()` 照它画，视口事件过滤器照它命中（按下与松开落在同一格才算点击），测试也照它取（`rowGeometry()` / `rowActionRect()` / `clickRowAction()`）。
3. **行尾按钮借一颗藏着的 `QToolButton` 画**（`drawComplexControl(CC_ToolButton)`，自动浮起、只在悬停时铺底），开关走 `ToggleSwitch::paintSwitch()` —— 和原来那几颗真控件同一套样式。
4. **新冒出来的行淡入**：只记"这一趟新出现的身份"，代理在 `rowAppearMs()` 内按时间降不透明度，只在这一行自己的矩形里画。

下面 2026-10-31 的实测和三档对照是"一行一套控件"时代的，留作为什么改成代理画的依据；那时第 2–4 步（头 80 行 + 分帧 + 窗口回收）的数字没有重测，现在这几步已随代理绘制一起删掉。

## 实测（2026-10-31，offscreen，MinGW 13 构建目录）

测量用例在 `tests/mw_check/ModActionPanelChecks.cpp`，由环境变量开关（默认不跑、不进断言）：

```powershell
$env:WTFML_FILES_PAINT_PROBE_ROWS = '1000'
./build/verification-mingw13/tests/mw_check/mw_check_mod_action_panel.exe
Remove-Item Env:WTFML_FILES_PAINT_PROBE_ROWS
```

输出四个数字（微秒），只打印、不设阈值：

- `build_us`：`setDirectory()` 换目录 + 一次 `processEvents()`（扫盘 + 建行 + 视图排版）；
- `refresh_us`：同一目录再 `refresh()`（重扫 + 整表重建）；
- `scroll_us`：滚到底 + 一次事件循环；
- `filter_us`：搜索框敲一个字（筛到 111 条 + 重建）。

修前：

| 行数 | build_us | refresh_us | scroll_us | filter_us |
| ---: | ---: | ---: | ---: | ---: |
| 200 | 109,939 | 88,670 | 1,194 | 41,329 |
| 500 | 360,707 | 363,993 | 1,619 | 44,561 |
| 1000 | 1,536,874 | 1,323,290 | 2,972 | 52,667 |

1000 行那两格多趟在 1.28–1.54 s 之间，取其中一趟。读法：

1. 滚动和重绘不是问题：1000 行滚到底约 3 ms，跟行数几乎无关。
2. 钱花在"把行造出来并塞进列表"：200 → 500 → 1000 行是 0.11 → 0.36 → 1.3 s，行数翻倍耗时翻 4 倍多。筛到 111 行稳定 40–60 ms，耗时跟建了多少行走，不跟屏幕上看见几行走。
3. 外面套 `setUpdatesEnabled(false)` 没有可测差别（1.35 s → 1.34 s）。

### 三档对照：超线性在 `setItemWidget()` 与 `addItem()` 的交错里

把重建循环临时做成三档（环境变量开关，量完已删），1000 行：

| 档 | 建了什么 | item_us | make_us | set_us | build_us |
| --- | --- | ---: | ---: | ---: | ---: |
| 0 | 只 `addItem`，不建行控件 | 86–147 | 0 | 0 | 7,496 |
| 1 | `makeRow()` 建出来、随即删掉、不挂 | 1,121–1,597 | 46,590–50,147 | 8,307–8,992 | 67,808 |
| 2 | 正常（逐条 `setItemWidget` 夹在 `addItem` 中间） | 1,016,021–1,046,945 | 107,526–110,769 | 163,922–174,955 | 1,340,670 |

`item_us` = `new QListWidgetItem` + `addItem`；`make_us` = `makeRow()`；`set_us` = `setItemWidget()`。第 2 档那 1.0 s 落在 item 一段：第 k 条 item 的插入要给前面 k-1 个行控件各算一遍几何。`makeRow()` 本身约 110 ms。

### 先排后挂的 A/B 与修后表

同一版二进制 A/B（1000 行，`WTFML_FILES_ATTACH=9` 那一档是旧写法，已随临时开关删除）：

| 写法 | item_us | make_us | set_us | build_us |
| --- | ---: | ---: | ---: | ---: |
| 夹着挂（旧） | 1,049,113–1,056,008 | 111,921–115,017 | ~2 | 1,392,085 |
| 先排后挂 | 1,279–1,584 | 79,609–84,539 | 148,935–169,266 | 285,695 |

| 行数 | build_us | refresh_us | scroll_us | filter_us |
| ---: | ---: | ---: | ---: | ---: |
| 200 | 46,319 | 45,888 | 1,070 | 27,207 |
| 500 | 126,035 | 128,210 | 1,571 | 30,007 |
| 1000 | 285,695 | 311,942 | 4,173 | 39,915 |

1000 行 1.39 s → 0.29 s（约 4.9×）。

### 先排后挂之后还剩的

- `makeRow()` 约 80 ms / 1000 行：每行约 10 个控件，要再降只能靠委托画或只给可见范围建（现行的分帧 + 窗口回收是朝后一个方向）。
- `setItemWidget()` 约 150 ms 仍是平方级（每次挂都遍历全部已挂控件重算几何）。试过 `setUpdatesEnabled(false)` 无效、`m_list->hide()` 降到 111 ms、先 `doItemsLayout()` 无效。
- 当时没有改成模型委托 / 虚拟列表：会动行构建、悬停露"详情"、开关、选择按身份还回去、统计异步补格、滚动锚点这些判据。2026-11-14（步骤 139）已改成代理画，这些判据都改为对着条目数据和 `rowGeometry()` 量（见上面「现行做法」）。其余列表没测。

## 同一条规矩：游戏规则表（`WorldSettingsPage`）

`rebuildRuleRows()` 原来也是 `addItem()` + `setItemWidget()` 夹在同一个循环里，每换一个世界、每改一次搜索框（200 ms 防抖后）整表重摆，模组世界几百条规则时最明显。现在同样分两步：循环里建 item 与行控件、配对，出了循环再逐条 `setItemWidget()`。这一页没有单独测量，行为由 `ui_check` 的 Z16 一段和 `mw_check` 的存档页用例覆盖。

## 回归门槛（2026-11-08）

`tests/ui_check/modules/UiBatchManagementChecks.cpp` 末尾：临时目录建 1000 个 `.jar`，构造实例设置页并等列表建满；首绘门槛 10 s、`scrollToBottom()` 门槛 3 s。这是防平方级退化的护栏，不是虚拟化。

## 选择与操作栏（2026-10-29）

- `ui/InstanceFilesSelection.cpp` 集中操作栏创建/布局、选择名单、按钮可用状态、选择恢复登记和批量操作接线；扫描、过滤、行构建留在原文件。状态仍由 `InstanceFilesPage` 持有，没有第二份选择账本；切目录、过滤、重列、改名沿用原有身份规则。
- 一次更新同时算启用/禁用计数，供按钮和状态摘要复用，不再四次创建目标文件名单；实际操作时重新取当前选中项并核对状态。
- 反选用 `QItemSelectionModel` 一次 Toggle 当前可见范围，替代逐行 `setSelected`；列表信号仍走现有的选择记账、详情和"加入世界"按钮接线。
- 启用、禁用是两颗直接按钮：混合选中时两者可用，目标状态相同的文件跳过，无目标项置灰；侧栏 240px、两列操作。

规模测量（真实 Qt 列表、1000 个临时文件、动画关闭，同一页面内对照，只比较批量通知的收益）：

| 路径 | 调用耗时 | 选择变化通知 |
| --- | ---: | ---: |
| 逐行选择参考路径 | 1279.164 ms | 1000 |
| 生产反选按钮，一次批量选择 | 1.430 ms | 1 |

最终选中都是 1000 条。只统计同步调用，不含准备文件、首绘和之后的事件循环绘制。

模组操作专项通过（`build/test-runs/20261029-194850-04a25114/`，1.05 秒）；覆盖直接按钮、混合状态、关闭保留选择、重开、重列/改名、过滤、短窗可达、依赖检查、缓存边界，以及反选的单次通知和最终选择集合。完整 UI 回归 3123/3123（9.88 秒，`20261029-195405-322c7b26`）。

复测（测试进程截止 120 秒，新增文件数上限 2000）：

```powershell
$env:WTFML_MOD_SELECTION_PROBE_ROWS = '1000'
./tools/test.ps1 -Suite mods -BuildTimeoutSeconds 300 -TestTimeoutSeconds 120
Remove-Item Env:WTFML_MOD_SELECTION_PROBE_ROWS
```

意外发现：受限环境创建测试 junction 失败（记录 `20261029-195234-6d784def`），换到允许的环境重跑，判据没有跳过。

## 未覆盖

| 项 | 状态 |
| --- | --- |
| 模型委托 / 完整虚拟化 | 未做，等其它列表也出现 1000 行级目录 |
| 其它列表（含复杂预设详情）的首绘/滚动 | 未测，见 [ROADMAP](../../ROADMAP.md) |
| 分帧建行 + 窗口回收后的首绘/滚动数字 | 未重测 |
| 反选之外的批量操作、USB HDD / 冷缓存、真实模组解析、整页滚动 | 未测 |
