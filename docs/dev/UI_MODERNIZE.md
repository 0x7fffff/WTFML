# UI 批量调优工具指南

本文档介绍 `tools/ui_modernize.py` 的用法与参数配置，供负责界面视觉与样式令牌调整的开发者使用。该工具用于集中管理与批量调整 WTFML 的共享 UI 令牌，不修改页面业务逻辑。

---

## 1. 快速开始

在仓库根目录下运行：

```powershell
# 1. 扫描散落的硬编码颜色、几何与非标控件（输出到 build/ui-modernize-report.md）
python tools/ui_modernize.py audit

# 2. 预览当前默认 profile（modern_soft）带来的代码差异
python tools/ui_modernize.py apply

# 3. 将所选 profile 写入源码文件
python tools/ui_modernize.py apply --write

# 4. 校验配置匹配、源码格式并执行构建
python tools/ui_modernize.py verify --build

# 5. 可选：构建完成后运行完整测试
python tools/ui_modernize.py verify --build --tests
```

如需切换配置档或恢复基线：

```powershell
# 应用并写入 baseline 基线配置
python tools/ui_modernize.py apply --profile baseline --write
```

---

## 2. 配置文件说明

令牌配置文件位于 `tools/ui-modernize.json`，默认使用 `modern_soft` 配置档。主要参数包括：

- **色彩**：
  - `accent`：全局默认主题色，格式为 `#RRGGBB`（对应 `ui/Theme.cpp` 的 `defaultAccentColor`）。
- **控件与卡片圆角**：
  - `control_radius`：基础输入框与选项控件圆角。
  - `button_radius`：普通按钮与动作按钮圆角。
  - `surface_radius`：主要卡片与表面圆角。
  - `section_radius`：独立内容区块圆角。
- **操作按钮尺寸**：
  - `action_height`：普通操作按钮高度。
  - `primary_action_height`：主要强调按钮高度（需 ≥ `action_height`）。
- **间距与边距**：
  - `page_margin`：页面全局外边距。
  - `surface_padding`：主卡片内边距。
  - `bottom_bar_height`：主界面底部任务摘要栏高度。
- **滚动条**：
  - `scrollbar_thickness`：滚动条宽度。
  - `scrollbar_handle_margin`：滑块外边距（需 < `scrollbar_thickness` / 2）。
  - `scrollbar_min_handle_length`：滑块最小长度。
- **任务中心面板**：
  - `task_panel_padding`：面板内边距。
  - `task_row_spacing`：任务项外间距。
  - `task_row_inner_spacing`：任务项内部元素间距。
  - `task_section_spacing`：面板分区垂直间距。
  - `task_progress_height`：任务进度条高度。
  - `task_step_spacing`：任务步骤文本间距。

---

## 3. 页面布局规范

- 新增与重构页面统一采用 `MainWindowPageLayout` 框架：列表、详情与编辑具有一致的页头、左导航、内容区和底部操作栏。
- 实例设置与预设管理采用应用内覆盖层展示，提供统一的返回导航与未保存变更检测。
- 页面间距与尺寸调整优先通过修改 `ui-modernize.json` 中的共享令牌完成。

---

## 4. 视觉迭代流程

1. 启动程序并捕获关键页面截图（主页、实例管理、下载页、设置页、任务中心）。
2. 对比视觉信息密度、卡片层级、按键比例与底栏视觉焦点。
3. 在 `tools/ui-modernize.json` 中调整对应令牌数值。
4. 运行 `python tools/ui_modernize.py apply` 查看 diff，确认无误后追加 `--write` 写入。
5. 执行 `python tools/ui_modernize.py verify --build` 确认编译通过且代码与配置一致。
6. 特殊动效或单页自定义控件在单独组件文件中针对性调整。

---

## 5. 审计报告排查项

运行 `python tools/ui_modernize.py audit` 会在 `build/ui-modernize-report.md` 生成静态审计清单：

- `hard-coded color`：直接书写的颜色字面量，应评估是否接入 `Theme` 调色板。
- `local style sheet`：局部覆盖的 QSS 样式表，需确认是否影响全局主题适配。
- `literal geometry` / `literal spacing`：重复出现的硬编码尺寸，适合提取为共享令牌。
- `native progress bar` / `progress text enabled`：未统一的原生进度条，建议替换为规范任务进度组件。
