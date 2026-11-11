# assets/fonts —— 标题字体

只给标题角色（`Theme::TextRole::Heading` / `Display`）用，正文、表单、列表仍走系统 UI 字体。`ui/Theme.cpp` 的 `loadTitleFonts()` 在启动时注册。

| 文件 | 字体 | 来源 | 许可 | 处理 |
|---|---|---|---|---|
| `LXGWWenKai-Medium.ttf` | 霞鹜文楷 Medium v1.530 | https://github.com/lxgw/LxgwWenKai | SIL OFL 1.1（`OFL-LXGWWenKai.txt`） | pyftsubset 到 GB2312 一级字 + 界面源码里出现过的全部汉字 + ASCII，约 3900 字 |
| `CormorantGaramond-SemiBold.ttf`、`CormorantGaramond-SemiBoldItalic.ttf` | Cormorant Garamond | https://github.com/google/fonts/tree/main/ofl/cormorantgaramond | SIL OFL 1.1（`OFL-CormorantGaramond.txt`） | 可变字体按 wght=600 实例化，再子集到拉丁基本区 + Latin-1 + Latin Extended-A |

界面加了新汉字标题却显示成系统字体时，说明子集里没有那个字：重新跑一遍子集化（字符表从 `ui/`、`src/` 的源码里收集）。