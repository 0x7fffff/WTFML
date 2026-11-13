"""r71 第 0 步：实测 hitokoto / ZenQuotes / PoetryDB，写 samples.md。"""
import json, os, re, subprocess, time
from collections import Counter

OUT_DIR = r"D:\AI\WTFML-develop\.tongxi\shots\r71"
os.makedirs(OUT_DIR, exist_ok=True)
OUT = os.path.join(OUT_DIR, "samples.md")
UA = "WTFML-Launcher"

lines_out = []
def w(s=""):
    lines_out.append(s)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines_out) + "\n")

def curl(url, timeout=12):
    raw = ""
    for attempt in range(2):
        p = subprocess.run(["curl", "-sS", "-m", str(timeout), "-A", UA,
                            "-w", "\n%{http_code}", url], capture_output=True)
        raw = p.stdout.decode("utf-8", "replace")
        if p.returncode == 0 and "\n" in raw:
            body, _, code = raw.rpartition("\n")
            if code.strip().isdigit():
                return int(code), body
        time.sleep(2)
    return 0, raw

HIRA = re.compile(r"[\u3040-\u309f]")
KATA = re.compile(r"[\u30a0-\u30ff]")
HAN = re.compile(r"[\u4e00-\u9fff]")
def lang_of(s):
    if HIRA.search(s) or KATA.search(s):
        return "ja"
    if HAN.search(s):
        return "zh"
    return "en"

def bucket(n):
    for hi in (10, 15, 20, 24, 30, 40, 60, 80, 128):
        if n <= hi:
            return "<=%d" % hi
    return ">128"

w("# 第 0 步：三个接口实测原始记录")
w()
w("采集时间：%s（本机 Windows，curl 直连，UA=%s）" % (time.strftime("%Y-%m-%d %H:%M:%S"), UA))
w()
w("## 1. 一言 hitokoto（60 次，间隔 1.1 s）")
w()
raws, langs, lens, zh_items, ja_items = [], [], [], [], []
fails = 0
for i in range(60):
    code, body = curl("https://v1.hitokoto.cn/?encode=json&max_length=30")
    if code == 200 and body.strip():
        try:
            j = json.loads(body)
        except Exception:
            fails += 1
            time.sleep(1.1)
            continue
        t = j.get("hitokoto") or ""
        raws.append((body.strip(), j))
        langs.append(lang_of(t))
        lens.append(len(t))
        if lang_of(t) == "zh":
            zh_items.append(j)
        elif lang_of(t) == "ja":
            ja_items.append(j)
    else:
        fails += 1
        if fails <= 3:
            w("  失败样本 #%d：http=%d body=%r" % (i, code, body[:120]))
    time.sleep(1.1)

w("- 成功 %d / 60，失败 %d。" % (len(raws), 60 - len(raws)))
w("- 语种分布：%s" % dict(Counter(langs)))
w("- 长度（字符）分布：%s" % dict(sorted(Counter(bucket(n) for n in lens).items())))
if lens:
    w("- 长度 min/median/max：%d / %d / %d" % (min(lens), sorted(lens)[len(lens)//2], max(lens)))
w()
w("3 条原始返回（原样）：")
w()
for body, j in raws[:3]:
    w("```json")
    w(body)
    w("```")
w()

w("## 2. ZenQuotes")
w()
code, body = curl("https://zenquotes.io/api/today")
w("`/api/today`：http=%d，原始返回：" % code)
w()
w("```json")
w(body.strip())
w("```")
w()
time.sleep(6)
code, body = curl("https://zenquotes.io/api/random")
w("`/api/random`：http=%d，原始返回：" % code)
w()
w("```json")
w(body.strip())
w("```")
w()
time.sleep(6)
en_items, en_lens, en_fails = [], [], 0
for i in range(20):
    code, body = curl("https://zenquotes.io/api/random")
    if code == 200 and body.strip().startswith("["):
        try:
            j = json.loads(body)
            q = j[0].get("q", "")
            en_items.append(j[0])
            en_lens.append(len(q))
        except Exception:
            en_fails += 1
    else:
        en_fails += 1
        if en_fails <= 3:
            w("  random 失败样本 #%d：http=%d body=%r" % (i, code, body[:120]))
    time.sleep(6.3)
ok = [n for n in en_lens if n <= 80]
w("- random 采样 %d 条成功，失败 %d；≤80 字符的 %d/%d = %.0f%%。" % (
    len(en_lens), en_fails, len(ok), len(en_lens), 100.0 * len(ok) / max(1, len(en_lens))))
w("- 长度 min/max：%d / %d。" % (min(en_lens or [0]), max(en_lens or [0])))
w("- 20 条原文：")
w()
for it in en_items:
    w("- (%d) %s — %s" % (len(it.get("q", "")), it.get("q", ""), it.get("a", "")))
w()

w("## 3. PoetryDB")
w()
w("取行规则：以 `.` `!` `?` 结尾、长度 20-80、首字母大写；一首没有就换一首，最多 3 首。")
w()

def pick_line(lines):
    for ln in lines:
        t = ln.strip()
        if not t:
            continue
        if not (t.endswith(".") or t.endswith("!") or t.endswith("?")):
            continue
        if not (20 <= len(t) <= 80):
            continue
        if not t[0].isupper():
            continue
        return t
    return None

poems = 0
picked = []
tries = 0
seen_titles = set()
while len(picked) < 50 and tries < 120:
    tries += 1
    code, body = curl("https://poetrydb.org/random/1")
    if code != 200:
        w("  请求失败：http=%d" % code)
        time.sleep(2)
        continue
    try:
        arr = json.loads(body)
    except Exception:
        continue
    if not arr:
        continue
    p = arr[0]
    poems += 1
    title = p.get("title", "")
    if title in seen_titles:
        continue
    seen_titles.add(title)
    ln = pick_line(p.get("lines") or [])
    if ln:
        picked.append((ln, p.get("author", ""), title))
    if poems % 20 == 0:
        w("  ... 已取 %d 首，挑出 %d 行" % (poems, len(picked)))

w("- 实际取 %d 首随机诗，挑出 %d 行（未筛选、未美化）：" % (poems, len(picked)))
w()
for ln, au, ti in picked:
    w("- (%d) %s — %s, %s" % (len(ln), ln, au, ti))
w()

w("## 4. 兜底候选（脚本按长度规则挑，无口吻过滤）")
w()
w("### 中文（hitokoto，含汉字无假名，6-24 字）")
w()
zh_ok = [j for j in zh_items if 6 <= len(j.get("hitokoto", "")) <= 24]
for j in zh_ok[:30]:
    w("- %s ｜%s %s" % (j["hitokoto"], j.get("from") or "", j.get("from_who") or ""))
w()
w("### 日文（hitokoto，含假名，<=30 字）")
w()
ja_ok = [j for j in ja_items if len(j.get("hitokoto", "")) <= 30]
for j in ja_ok[:20]:
    w("- %s ｜%s %s" % (j["hitokoto"], j.get("from") or "", j.get("from_who") or ""))
w()
w("### 英文（ZenQuotes / PoetryDB，20-80 字符，去重）")
w()
seen = set()
n = 0
for it in en_items + [{"q": q, "a": a} for q, a, t in picked]:
    q = it["q"].strip()
    if q in seen or not (20 <= len(q) <= 80):
        continue
    seen.add(q)
    w("- %s" % q)
    n += 1
    if n >= 40:
        break
w()
w("完成：%s" % time.strftime("%H:%M:%S"))
print("DONE", OUT, len(raws), len(picked))
