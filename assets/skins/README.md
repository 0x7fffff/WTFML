# assets/skins —— 内置默认皮肤

离线账号没有自选皮肤、本地也算不出游戏默认皮肤（没装客户端 JAR）时，主页立绘和头像用这张图；贴图还在路上或取不到时也先显示它。有真实皮肤时照旧显示真实皮肤。

| 文件 | 说明 | 来源 | 许可 |
|---|---|---|---|
| `default.png` | 64×64 经典（宽臂）模型，棕发、蓝衣、米色围巾 | 项目自绘：UI 设计稿 `skinFigure()` 的像素图案，由 [tools/gen-default-skin.py](../../tools/gen-default-skin.py) 生成 | 随本仓库 |

不打包 Mojang 的 Steve / Alex 贴图：那是游戏素材，不随启动器分发。装了客户端的实例仍会按游戏规则从本地 JAR 里取官方默认皮肤（见 `src/manage/DefaultSkin.*`）。
