import json, re, subprocess, time
HIRA = re.compile(r"[\u3040-\u309f\u30a0-\u30ff]")
seen = []
for i in range(90):
    p = subprocess.run(["curl","-sS","-m","10","-A","WTFML-Launcher",
                        "https://v1.hitokoto.cn/?encode=json&max_length=30"], capture_output=True)
    try:
        j = json.loads(p.stdout.decode("utf-8","replace"))
    except Exception:
        time.sleep(1.1); continue
    t = j.get("hitokoto") or ""
    if HIRA.search(t) and len(t) <= 30:
        seen.append((t, j.get("from") or "", j.get("from_who") or ""))
        print(len(seen), t, "|", j.get("from"), j.get("from_who"), flush=True)
        if len(seen) >= 8: break
    time.sleep(1.1)
print("TOTAL", len(seen))
