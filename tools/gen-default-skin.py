"""生成 assets/skins/default.png：内置默认皮肤（64x64 经典模型）。

图案取自 UI 设计稿 shell.js 的 skinFigure()（项目自绘的原创像素图，不含 Mojang 素材）。
用法：python tools/gen-default-skin.py assets/skins/default.png
"""
import sys
from PIL import Image
C = {'h':'#4a3426','H':'#3a281d','S':'#e2ae88','s':'#c99271','w':'#f3f1ee','e':'#33507a','m':'#a86a55',
     'f':'#d6c597','F':'#b9a776','c':'#5f7192','C':'#4b5a78','B':'#7a5a3a','b':'#c9a35a','p':'#3c4152','P':'#30343f','o':'#5c4737','O':'#47372a','g':'#8fa1bf'}
rep=lambda r,n:[r]*n
head_front=['hhhhhhhh','hhhhhhhh','hSSSSSSh','SSSSSSSS','SwesSewS','SSSssSSS','SSsmmsSS','sSSSSSSs']
head_side=['hhhhhhhh','hhhhhhhh','hhhhhHhh','hhSSSSHh','hSSSSSHH','SSSSSShH','SSSSSSSh','sSSSSSSs']
head_back=['hhhhhhhh','hhhhhhhh','hhhhhhhh','hhhhhhhh','hhhHhhhh','hhhhhhhh','HhhhhhhH','HHhhhhHH']
body_front=['ffffffff','fFFFFFFf','ccfFfccc','cccFfccc','cccffccc','ccgffccc','cccffccc','BBBbBBBB','cccffccc','cccffccc','CCCffCCC','CCCCCCCC']
body_side=['ffff','cfFc','cccc','cccc','cccc','cccc','cccc','BBBB','cccc','cccc','CCCC','CCCC']
body_back=['ffffffff','cFFFFFFc','cccccccc','cccccccc','cccccccc','cccccccc','cccccccc','BBBBBBBB','cccccccc','cccccccc','CCCCCCCC','CCCCCCCC']
arm=['cccc']*7+['CCCC','CCCC','SSSS','SSSS','ssss']
leg_front=['pppp']*5+['PPpp','pppp','PPPP','oooo','oooo','oooo','OOOO']
leg_side=['pppp']*7+['PPPP','oooo','oooo','oooo','OOOO']
mirror=lambda pat:[r[::-1] for r in pat]
img=Image.new('RGBA',(64,64),(0,0,0,0))
def paint(x,y,pat):
    for v,row in enumerate(pat):
        for u,k in enumerate(row): img.putpixel((x+u,y+v),tuple(int(C[k][i:i+2],16) for i in (1,3,5))+(255,))
def part(bx,by,w,h,d,front,side,back,top,bottom):
    # 摊开顺序：右 | 前 | 左 | 后（中排），顶 / 底（上排）。右面的右边挨着正面，所以镜像。
    paint(bx,by+d,mirror(side)); paint(bx+d,by+d,front); paint(bx+d+w,by+d,side); paint(bx+d+w+d,by+d,back)
    paint(bx+d,by,top); paint(bx+d+w,by,bottom)
part(0,0,8,8,8,head_front,head_side,head_back,rep('hhhhhhhh',8),rep('SSSSSSSS',8))
part(16,16,8,12,4,body_front,body_side,body_back,rep('ffffffff',4),rep('pppppppp',4))
for bx,by in ((40,16),(32,48)): part(bx,by,4,12,4,arm,arm,arm,rep('cccc',4),rep('ssss',4))
part(0,16,4,12,4,leg_front,leg_side,leg_side,rep('pppp',4),rep('OOOO',4))
part(16,48,4,12,4,mirror(leg_front),leg_side,leg_side,rep('pppp',4),rep('OOOO',4))
img.save(sys.argv[1])
