"""重新生成 assets/fonts 里的标题字体子集（界面加了新汉字标题后跑一遍）。

用法：python tools/subset-title-fonts.py <原始字体目录>
原始字体目录里放（都不入库）：
  LXGWWenKai-Light.ttf、LXGWWenKai-Regular.ttf  https://github.com/lxgw/LxgwWenKai/releases
  DancingScript[wght].ttf                       https://github.com/google/fonts/tree/main/ofl/dancingscript
字表 = GB2312 一级字 + ui/、src/ 源码里出现过的全部汉字与全角标点 + ASCII；英文字体整套保留。
需要 fontTools（pip install fonttools）。
"""
import os
import re
import sys

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'assets', 'fonts')


def source_chars():
    found = set()
    pattern = re.compile(r'[　-〿一-鿿＀-￯—…·“”‘’]')
    for top in ('ui', 'src'):
        for base, _, files in os.walk(os.path.join(ROOT, top)):
            for name in files:
                if name.endswith(('.cpp', '.hpp', '.h')):
                    with open(os.path.join(base, name), encoding='utf-8', errors='ignore') as f:
                        found.update(pattern.findall(f.read()))
    return found


def gb2312_level1():
    return {bytes([hi, lo]).decode('gb2312')
            for hi in range(0xB0, 0xD8) for lo in range(0xA1, 0xFF)
            if not (hi == 0xD7 and lo > 0xF9)}


def write_subset(font, text, dst):
    options = subset.Options()
    options.layout_features = ['*']
    options.name_IDs = ['*']
    options.name_languages = ['*']
    options.notdef_outline = True
    options.hinting = False
    subsetter = subset.Subsetter(options)
    subsetter.populate(text=''.join(sorted(text)))
    subsetter.subset(font)
    font.save(dst)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    src = sys.argv[1]
    chars = source_chars() | gb2312_level1() | {chr(c) for c in range(0x20, 0x7f)}
    for weight in ('Light', 'Regular'):
        name = 'LXGWWenKai-%s.ttf' % weight
        write_subset(TTFont(os.path.join(src, name)), chars, os.path.join(OUT, name))
    dancing = instancer.instantiateVariableFont(
        TTFont(os.path.join(src, 'DancingScript[wght].ttf')), {'wght': 400}, updateFontNames=True)
    dancing.save(os.path.join(OUT, 'DancingScript-Regular.ttf'))
    total = sum(os.path.getsize(os.path.join(OUT, n)) for n in os.listdir(OUT) if n.endswith('.ttf'))
    print('汉字表 %d 字，assets/fonts 共 %.2f MB' % (len(chars), total / 1048576))
    assert total < 4.2 * 1048576, '字体资源超过约 4 MB 的预算'


if __name__ == '__main__':
    main()
