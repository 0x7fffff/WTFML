import sys; sys.path.insert(0, '_tmp')
import ed

# docs/README.md 索引
ed.sub('docs/README.md', '''| [DIRECTORY_SESSION_LOCK.md](features/DIRECTORY_SESSION_LOCK.md)''',
'''| [DAILY_QUOTE.md](features/DAILY_QUOTE.md) | 主页「每日一言」：句子从哪三个站来、按日期定语种、限长与回退、字体选择、开关与隐私 |\n| [DIRECTORY_SESSION_LOCK.md](features/DIRECTORY_SESSION_LOCK.md)''')

# FEATURES.md：主页那一段加一句
ed.sub('docs/FEATURES.md', '''- 主页：左上大标题；左下是当前实例的启动区。''',
'''- 主页大标题下面一行「每日一言」：一天一句、单行小字，超宽省略号；悬停显示「— 作者《出处》」。语种按本地日期定（英文 60%，其余为一言的中文或日文），当天不变；当天有缓存就不再请求，连不上先显示句子池 / 内置兜底里那一句。中文用文楷、纯英文用 Dancing Script、含假名用系统日文字体。设置 → 个性化 → 外观可关，关掉不发请求也不占位。细节见 [DAILY_QUOTE.md](features/DAILY_QUOTE.md)。
- 主页：左上大标题；左下是当前实例的启动区。''')

# SECURITY_BOUNDARIES.md：第 9 节，上限表
block = '''
## 9. 主页「每日一言」的外部请求（2026-11-13）

判据在 `src/util/DailyQuote.*`，请求在 `src/controllers/DailyQuoteController.*`。

| 项 | 上限 / 约束 |
| --- | --- |
| 目标站点 | `v1.hitokoto.cn`（备用 `international.v1.hitokoto.cn`）、`zenquotes.io`、`poetrydb.org`，三站之外不发 |
| 方法 / 头 | 只有 GET；`User-Agent: WTFML-Launcher`、`Accept: application/json`；不带账号、Cookie 或任何用户标识 |
| 频率 | 每天至多一次成功取句（当天有缓存就不发）；一次启动最多 5 个请求 |
| 响应体 | 8 KiB（`RequestBudget::ControlGet` 的正文上限，超了当场 abort） |
| 单发超时 | 4 秒（排队与传输共用一个截止） |
| 正文 | ≤128 字符、非空、按语种分档限长（假名 30 / 汉字 24 / 拉丁 80）；不合规当这一发失败 |
| 写入 | 只写 `HKCU\Software\WTFML` 的 `Quote/Cache`、`Quote/Pool`（JSON 字符串）与开关 `Launcher/DailyQuote` |
| 关掉开关 | 不发任何请求、主页不显示也不占位；在飞的请求当场掐掉 |
'''
p='docs/features/SECURITY_BOUNDARIES.md'
s=open(p,'rb').read().decode('utf-8')
assert s.rstrip().endswith('留给 Stage 2 的日志/诊断字段投影处理。')
s = s.rstrip('\r\n') + '\r\n' + block.replace('\n','\r\n')
open(p,'wb').write(s.encode('utf-8'))
print('ok')
