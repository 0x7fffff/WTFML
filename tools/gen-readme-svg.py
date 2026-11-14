"""生成 README 用的手写分节标题与下载按钮 SVG（暮色雪山配色，字形转成路径，不依赖访客字体）。

用法：python tools/gen-readme-svg.py   # 输出到 assets/readme/
标题：中文版 h-<名>.svg 为 Dancing Script 英文词加小号霞鹜文楷中文；英文版 h-<名>-en.svg 只有英文词。
"""
from pathlib import Path

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent.parent
FONTS = ROOT / "assets" / "fonts"
OUT = ROOT / "assets" / "readme"

ACCENT, SECONDARY, ON_ACCENT = "#B7B3EE", "#A9ADBF", "#1C1B30"
HEADINGS = {"features": ("Features", "功能"), "home": ("Home", "主页"),
            "contributing": ("Contributing", "参与开发"), "license": ("License", "许可")}
BUTTONS = {"en": "Download", "zh": "下载最新版"}
# 下载箭头图标，按钮里左起 38 px
ICON = "M48 17v15M41.5 26l6.5 6.5 6.5-6.5M38 35.5v2.5a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-2.5"


def num(v):
    s = f"{v:.1f}".rstrip("0").rstrip(".")
    return "0" if s == "-0" else s


class Face:
    def __init__(self, name):
        self.font = TTFont(FONTS / name)
        self.glyphs = self.font.getGlyphSet()
        self.cmap = self.font.getBestCmap()
        self.upm = self.font["head"].unitsPerEm

    def path(self, text, size, x, baseline):
        """返回 (路径数据, 排完后的笔位 x)。"""
        pen = SVGPathPen(self.glyphs, ntos=num)
        k = size / self.upm
        for ch in text:
            name = self.cmap[ord(ch)]
            self.glyphs[name].draw(TransformPen(pen, (k, 0, 0, -k, x, baseline)))
            x += self.font["hmtx"][name][0] * k
        return pen.getCommands(), x


def svg(width, height, body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}">{body}</svg>\n')


def main():
    script, wenkai_light, wenkai = (Face("DancingScript-Regular.ttf"), Face("LXGWWenKai-Light.ttf"),
                                    Face("LXGWWenKai-Regular.ttf"))
    for key, (en, zh) in HEADINGS.items():
        d, x = script.path(en, 54, 2, 54)
        en_body = f'<path fill="{ACCENT}" d="{d}"/>'
        (OUT / f"h-{key}-en.svg").write_text(svg(round(x) + 4, 70, en_body), encoding="utf-8")
        d2, x2 = wenkai_light.path(zh, 24, x + 16, 54)
        body = en_body + f'<path fill="{SECONDARY}" d="{d2}"/>'
        (OUT / f"h-{key}.svg").write_text(svg(round(x2) + 4, 70, body), encoding="utf-8")
    for lang, label in BUTTONS.items():
        d, x = wenkai.path(label, 24, 74, 37)
        width = round(x) + 34
        body = (f'<rect width="{width}" height="56" rx="16" fill="{ACCENT}"/>'
                f'<g fill="none" stroke="{ON_ACCENT}" stroke-width="2.4" stroke-linecap="round" '
                f'stroke-linejoin="round"><path d="{ICON}"/></g><path fill="{ON_ACCENT}" d="{d}"/>')
        (OUT / f"download-{lang}.svg").write_text(svg(width, 56, body), encoding="utf-8")


if __name__ == "__main__":
    main()
