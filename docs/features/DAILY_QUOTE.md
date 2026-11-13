# 主页「每日一言」

给改主页、网络层和设置页的人看：这一行显示什么、句子从哪来、什么时候发请求、关掉之后什么都不做。
判据实现在 `src/util/DailyQuote.*`，取句在 `src/controllers/DailyQuoteController.*`，
那一行在 `ui/MainWindowUI.cpp` 的 `setupHomePage()` 与 `setHomeHeroQuote()`。

## 显示

- 位置：主页大标题「下一个世界，等你开启。」正下方那一行（原英文副题那一格，`git show 8fc9f5e` 的反向 diff），
  边距 `kHomeHeroInset + 4, 12, 0, 0`，单行小字，字号 16（窗口窄于 1024 时 14）。
- 一天一句，零实例与有实例的主页都显示；颜色跟随主页英雄区字色（`followHeroInk(quote, true)`）。
- 超宽自己从尾巴收省略号（`ui/ElidedLabel.hpp`），文字长短不会把左栏抻开。
- 悬停提示写署名：「— 作者《出处》」；只有作者写「— 作者」；只有出处写「—《出处》」；都没有就不设提示。
  主页上不写来源站名。
- 没有句子（关掉、或者第一次启动又连不上且句子池为空时理论上不会发生）时这一行**隐藏**，版面与没有它时一致。

## 字体

| 这一句长什么样 | family 列表 |
| --- | --- |
| 含假名（平假名 / 片假名 / 长音符「ー」/「々」） | `Yu Mincho` → `Yu Gothic UI` → `Meiryo UI` → 应用字体 |
| 含汉字、无假名 | 文楷 Light（和主页大标题同一支）→ 应用字体 |
| 纯拉丁 | Dancing Script（品牌手写体）→ 应用字体 |

文楷**没有假名**：`assets/fonts/LXGWWenKai-*.ttf` 的 cmap 一共 3911 个码点，`U+3040–U+30FF`
（平假名 / 片假名 / 长音符）一个都没有，「々」也没有。日文这一档因此不把文楷排在前面 ——
否则一行里汉字用文楷、假名落回系统字，两种字形并排。不为日文另打包字体。

## 句子的来源与频率

- 语种按**本地日期**定，当天不变：`(julianDay * 2654435761u) % 100 < 60` 是英文日（60%），否则是一言日。
  乘法在 `quint32` 上做，溢出回绕是规格的一部分。
- 英文日：英文站点表（`DailyQuoteController.cpp` 的 `kEnglishSources`）按日期伪随机定先后，
  第一个失败就换下一个。删掉 PoetryDB 只改那一行（`enabled = false` 或删掉整行）。
  - ZenQuotes：先 `/api/today`；正文超过 80 字符就再抽 `/api/random`，最多两次。
  - PoetryDB：`/random/1`，从 `lines` 里挑以 `.` `!` `?` 结尾、20–80 字符、首字母大写的行。
- 一言日：`v1.hitokoto.cn`（`encode=json&max_length=30`，不传类别），失败换
  `international.v1.hitokoto.cn`。一言不能指定语种，取到什么显示什么；一言日碰巧拿到英文句也照常显示。
- 频率：**每天至多一次成功取句**；当天有缓存就不再请求。一次启动最多发 5 个请求
  （一言主 + 备 2 个；英文 ZenQuotes 今天 + 两次随机 3 个），每个 4 秒截止。
- 请求全在后台异步（`RequestBudget::ControlGet`，响应体上限 8 KiB），不阻塞启动与首帧。

## 长度

- 显示上限：含假名 ≤30 字、含汉字无假名 ≤24 字、纯拉丁 ≤80 字符。超限换下一发（英文的 ZenQuotes 先补抽随机）。
- 硬校验：响应体 ≤8 KiB、正文 ≤128 字符、非空。超了直接当这一发失败。

## 回退

同语种其他站点 → 最近成功的句子池 → 内置 36 条兜底（英文 22 / 中文 10 / 日文 4，
全部满足分档限长，开发时从 hitokoto / ZenQuotes / PoetryDB 抽样，不做口吻过滤）。

- 句子池：每次成功追加一条，保留最近 30 条（同一句正文只留最新那条），带语种。
- 池里按 `julianDay % 同语种条数` 取，连不上也不会天天同一句。
- 没有等待感：先立刻显示当天缓存、没有就显示池 / 兜底里那一句，网络成功后再替换并写缓存。

## 缓存与开关

- 当天缓存与句子池都在注册表 `HKCU\Software\WTFML` 的 `Quote/Cache`、`Quote/Pool`（JSON 字符串）。
  这两个是缓存，删掉只丢一句。
- 开关：设置 → 个性化 → 外观 →「每日一言」，默认开。关掉 = 不发任何请求、主页不显示那一行、不占位；
  切换即时生效（在飞的请求当场掐掉）。开关值存 `Launcher/DailyQuote`。

## 隐私

- 只发 GET，不带账号、不带任何用户标识；`User-Agent` 固定 `WTFML-Launcher`，`Accept: application/json`。
- 一共三个站：`v1.hitokoto.cn`（+ 备用域名 `international.v1.hitokoto.cn`）、`zenquotes.io`、`poetrydb.org`。
- 关掉开关之后一个请求都不发。上限见 [SECURITY_BOUNDARIES.md](SECURITY_BOUNDARIES.md) 第 9 节。

## 验证

- `verify_resources` 的 DQ 一节（66 条）：语种判别、分档限长、按日期定语种（含溢出回绕与 60 边界）、
  站点先后、三种 JSON 的解析（含 `from_who` 为 null）、当天缓存命中 / 跨日不命中、池的容量与去重与轮取、
  36 条兜底的数量和限长、署名四种情形。全脱网。
- `wtfml.ui-daily-quote`（离屏）：那一行在 / 不在、不占位、中文 / 英文 / 日文的 family 选择、窄窗字号、
  单行与省略号、开关那一格与说明文字。
- 真机只在大陆网络下验过可达性的一半（本机 curl 实测三个站都返回 200，见
  `.tongxi/shots/r71/samples.md`）；海外站在大陆用户机器上的实际成功率由真机验收。
