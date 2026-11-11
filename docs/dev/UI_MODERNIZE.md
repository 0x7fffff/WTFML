# UI 批量调优工具

`tools/ui_modernize.py` 用来统一调整 WTFML 的公共 UI 令牌。它不会批量重写页面业务代码，也不会自动改动画：这些高风险项只进入审计报告，等截图确认后再处理。

## 当前页面结构约定（2026-10-31）

后续页面沿用当前共用页面框架 `MainWindowPageLayout`：列表、详情与编辑采用一致的页头、左导航、内容区和操作区。预设编辑与实例设置一样进入应用内独立页面，保留统一返回与未保存检查，不另开窗口或嵌套整张管理页。预设继续跟随当前游戏文件夹。改进动画或控件时保留现有分组、导航和布局，不恢复旧版页面结构。

## 快速开始

在仓库根目录执行：

```powershell
python tools/ui_modernize.py audit
python tools/ui_modernize.py apply
python tools/ui_modernize.py apply --write
python tools/ui_modernize.py verify --build
```

第一条会生成 `build/ui-modernize-report.md`。第二条只显示 diff，不写文件；第三条才会把默认的 `modern_soft` 配置写入源码；第四条检查 diff、配置定位、源码是否已应用所选 profile，并构建工程。

如果还要跑测试：

```powershell
python tools/ui_modernize.py verify --build --tests
```

## 调整参数

编辑 `tools/ui-modernize.json` 中的 `modern_soft`。常用参数：

- `accent`：主题色，格式为 `#RRGGBB`。
- `*_radius`：控件、按钮、卡片和分区圆角。
- `action_height` / `primary_action_height`：普通和主要按钮高度。
- `surface_padding` / `page_margin`：卡片内边距和页面外边距。
- `task_*`：任务中心的间距、进度条高度和阶段间距。
- `bottom_bar_height`：主页底部任务摘要栏高度。

每次改配置后先运行不带 `--write` 的 `apply` 看 diff。脚本要求每个令牌在预期文件中恰好匹配一次；源码结构改变时会停止，不会猜测替换。

`verify --profile NAME` 还会确认当前源码确实符合该 profile。如果只改了 JSON 却忘记执行
`apply --write`，验证会失败并列出尚未更新的文件。

## 看截图后的迭代方法

1. 启动程序，建议截主页、实例管理、下载页、设置页和任务中心各一张。
2. 优先比较信息密度、卡片层级、按钮高度、圆角和任务栏是否抢视线。
3. 只改 JSON 中对应令牌，再执行 `apply --write` 和 `verify --build`。
4. 结构、动画或单页例外不要塞进批量脚本；确认截图后单独改组件。

`baseline` 保存了引入脚本时的令牌值。如需仅恢复这些公共令牌，可执行：

```powershell
python tools/ui_modernize.py apply --profile baseline
python tools/ui_modernize.py apply --profile baseline --write
```

它只恢复脚本管理的令牌，不会覆盖其他未提交工作。

## 审计报告怎么看

报告里的项目是线索，不等于错误：

- `hard-coded color`：应判断是否应改用 `Theme` 颜色。
- `literal geometry` / `literal spacing`：重复出现时适合提取为令牌。
- `local style sheet`：可能绕开全局主题，需人工确认。
- `native progress bar` / `progress text enabled`：适合逐步替换为统一的任务进度组件。

脚本只自动处理 `TOKEN_RULES` 白名单中的共享令牌，因此可以在已有未提交改动的工作区中使用，但仍建议先阅读预览 diff。写入时会先在原文件旁完整暂存所有结果；后续替换失败时会尝试恢复已经替换的文件。
