# assets/fonts —— 手写体

优雅、低调、清新：品牌、大标题、导航、小节标题、按钮短标签、空状态用手写体；长文本、列表正文、表单输入、日志、数字密集处仍走系统 UI 字体。`ui/Theme.cpp` 的 `Theme::handFamily()` 第一次用到时注册。

| 文件 | 字体 | 用在 | 来源 | 许可 |
|---|---|---|---|---|
| `LXGWWenKai-Regular.ttf` | 霞鹜文楷 Regular v1.530 | 导航、小节标题、按钮 | https://github.com/lxgw/LxgwWenKai | SIL OFL 1.1（`OFL-LXGWWenKai.txt`） |
| `LXGWWenKai-Light.ttf` | 霞鹜文楷 Light v1.530 | 大标题（Display） | 同上 | 同上 |
| `DancingScript-Regular.ttf` | Dancing Script（wght=400 实例） | 纯西文的品牌、题词 | https://github.com/google/fonts/tree/main/ofl/dancingscript | SIL OFL 1.1（`OFL-DancingScript.txt`） |

文楷两支子集到 GB2312 一级字 + `ui/`、`src/` 源码里出现过的全部汉字 + ASCII（约 3900 字），Dancing Script 整套保留；三支合计约 3.9 MB。

界面加了新汉字标题却显示成系统字体时，说明子集里没有那个字：把原始字体放进一个目录，跑 `python tools/subset-title-fonts.py <目录>` 重新生成。
