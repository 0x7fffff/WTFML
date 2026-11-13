import json, urllib.request, time, sys
UA={'User-Agent':'WTFML-Launcher','Accept':'application/json'}
seen={}
tries=0
while len(seen)<26 and tries<60:
    tries+=1
    try:
        req=urllib.request.Request('https://zenquotes.io/api/random',headers=UA)
        with urllib.request.urlopen(req,timeout=6) as r:
            d=json.loads(r.read().decode('utf-8'))
        q=d[0]['q'].replace('\u2019',"'").strip()
        a=d[0]['a'].strip()
        if 12<=len(q)<=80 and q not in seen:
            seen[q]=a
            print(f'({len(q)}) {q} — {a}', flush=True)
    except Exception as e:
        print('ERR',e, file=sys.stderr, flush=True)
    time.sleep(1.2)
print('total', len(seen), 'tries', tries, file=sys.stderr)
