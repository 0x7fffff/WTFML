#!/usr/bin/env python3
"""Bounded real-network smoke using the production diagnostic download engine.
Only public metadata/files; no API key, account or original game directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import urllib.request

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True)
    parser.add_argument('--root', required=True)
    args = parser.parse_args()
    root = Path(args.root)
    exe = Path(args.exe).resolve()
    if not root.is_absolute() or root.exists() or not exe.is_file():
        raise SystemExit('Use a new absolute evidence directory and an existing diagnostic executable')
    root.mkdir(parents=True)
    report = {'scope': 'current host network, system proxy routing; public fixed CurseForge sample, live Modrinth metadata', 'cases': []}
    started = time.monotonic()
    try:
        request = urllib.request.Request('https://api.modrinth.com/v2/project/P7dR8mSH/version?game_versions=%5B%221.21.11%22%5D',
            headers={'User-Agent': 'WTFML/0.4.2 (local acceptance)'})
        with urllib.request.urlopen(request, timeout=20) as response:
            payload = response.read(2*1024*1024+1)
        if len(payload) > 2*1024*1024:
            raise ValueError('metadata budget exceeded')
        versions = json.loads(payload)
        file = next(f for f in versions[0]['files'] if f.get('primary'))
        cases = [('modrinth', file['url'], file['hashes']['sha1'], file['size']),
            ('curseforge', 'https://edge.forgecdn.net/files/6653/356/oritech-neoforge-0.16.2.jar',
             'bfbc87e3af947c3ffe966b7fe2a134016924d344', 9006088)]
        (root/'public-metadata.json').write_text(json.dumps({'modrinthVersion':versions[0]['id'],'cases':cases},indent=2),encoding='utf-8')
        for name, url, sha1, size in cases:
            if not url.startswith('https://') or not 0 < size < 16*1024*1024:
                raise ValueError('sample outside URL/size budget')
            directory = root/name
            scratch = directory/'scratch'
            scratch.mkdir(parents=True)
            config = dict(name=name,url=url,sha1=sha1,size=size,directory=str(directory),scratch=str(scratch),
                route='system',h2=True,segmented=False,concurrency=1,requests=1,timeout_s=60,budget=16*1024*1024)
            env = os.environ.copy()
            env['QT_QPA_PLATFORM']='offscreen'
            flags = subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0
            with (directory/'stdout.jsonl').open('wb') as out, (directory/'stderr.log').open('wb') as err:
                child=subprocess.Popen([str(exe),'--worker'],stdin=subprocess.PIPE,stdout=out,stderr=err,env=env,creationflags=flags)
                try:
                    child.communicate(json.dumps(config).encode(),timeout=min(75,max(1,170-(time.monotonic()-started))))
                except subprocess.TimeoutExpired:
                    child.kill(); child.communicate(timeout=5)
                    raise RuntimeError(name+': parent deadline exceeded; owned worker killed')
            result=json.loads((directory/'result.json').read_text(encoding='utf-8'))
            target=scratch/'payload-0.bin'
            valid=target.is_file() and target.stat().st_size==size and hashlib.sha1(target.read_bytes()).hexdigest()==sha1
            ok=child.returncode==0 and result.get('success') is True and valid
            report['cases'].append(dict(name=name,ok=ok,bytes=size,expectedSha1=sha1,engine=result))
            print(('PASS ' if ok else 'FAIL ')+name,flush=True)
    except Exception as error:
        report['error']=str(error)
    finally:
        report['elapsedSeconds']=round(time.monotonic()-started,2)
        (root/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    return 0 if len(report['cases'])==2 and all(c['ok'] for c in report['cases']) and 'error' not in report else 1

if __name__=='__main__':
    raise SystemExit(main())
