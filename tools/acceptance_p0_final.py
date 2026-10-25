"""Bounded P0 software-safety closure batch. Never fills physical disks or changes AV.

ENOSPC evidence is audited from retained dedicated-VHD runs, not rerun over evidence.
Every stage has an independent log; failures do not get retried to manufacture green.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--timeout', type=float, default=840)
    args = parser.parse_args()
    if not 0 < args.timeout <= 900:
        parser.error('timeout must be in (0,900]')
    repo = Path(__file__).resolve().parent.parent
    stamp = time.strftime('%Y%m%d-%H%M%S')
    report = repo/'build'/('p0-closure-'+stamp)
    report.mkdir(exist_ok=False)
    deadline = time.monotonic()+args.timeout
    env = dict(os.environ)
    env['PATH'] = ';'.join(('D:/Qt/Qt/Tools/CMake_64/bin', 'D:/Qt/Qt/Tools/mingw1310_64/bin',
                           'D:/Qt/Qt/6.10.1/mingw_64/bin', env.get('PATH','')))
    exe = 'build/verification-mingw13/tests/verify_instances.exe'
    qt = 'D:/Qt/Qt/6.10.1/mingw_64/bin'
    python = os.sys.executable
    stages, evidence = [], []
    def stage(name, command, limit):
        remaining = min(limit, deadline-time.monotonic())
        if remaining <= 0:
            stages.append({'name':name, 'passed':False, 'error':'batch budget exhausted'})
            return
        log = report/(name+'.log')
        print('START '+name, flush=True)
        start = time.monotonic()
        with log.open('wb') as stream:
            process = subprocess.Popen(command, cwd=repo, env=env, stdout=stream, stderr=subprocess.STDOUT)
            timed_out = False
            try:
                notice = start+25
                while process.poll() is None:
                    now = time.monotonic()
                    if now-start >= remaining:
                        timed_out = True
                        subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],
                                       stdout=stream,stderr=subprocess.STDOUT,timeout=10)
                        process.wait(timeout=10)
                        break
                    if now >= notice:
                        print(f'RUNNING {name}: {now-start:.0f}s; {log}', flush=True)
                        notice = now+25
                    time.sleep(.2)
            finally:
                if process.poll() is None:
                    subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],
                                   stdout=stream,stderr=subprocess.STDOUT,timeout=10)
                    process.wait(timeout=10)
        result = {'name':name, 'passed':process.returncode==0 and not timed_out,
                  'exitCode':process.returncode,'timedOut':timed_out,
                  'seconds':round(time.monotonic()-start,3), 'log':str(log)}
        stages.append(result)
        print(('PASS ' if result['passed'] else 'FAIL ')+name, flush=True)
    def test(name, suite, modules, limit):
        stage(name, ['pwsh','-NoProfile','-File','tools/test.ps1','-Suite',suite,'-Modules',modules,
                     '-BuildTimeoutSeconds','300','-TestTimeoutSeconds','180','-BuildJobs','2'], limit)
    try:
        test('core-safety','ci-core','core-instances,core-presets,core-install,core-resources,core-servers,core-launch',360)
        # A shared-header rebuild can consume ~270 s before the ~100 s integration run.
        # Keep the independent build/test bounds and the overall batch deadline.
        test('window-lifecycle-upgrade','ci-integration','window,upgrade',450)
        test('widget-lifecycle','ci-ui','ui',120)
        stage('transactions',[python,'tools/acceptance_p0_transactions.py','--exe',exe,'--qt-bin',qt,
                              '--work-dir',str(report/'transactions'),'--timeout','120'],130)
        stage('permissions',[python,'tools/acceptance_p0_permissions.py','--exe',exe,'--qt-bin',qt,
                             '--work-dir',str(report/'permissions'),'--timeout','120'],130)
        stage('hdd-publish',[python,'tools/acceptance_large_instance.py','--verify-exe',exe,'--qt-bin',qt,
                            '--work-dir','H:/WTFML-Acceptance-20261025/p0-closure-'+stamp,
                            '--payload-mb','128','--files','1000','--kill-stage','publish','--keep',
                            '--timeout','240','--total-timeout','260','--json-out',str(report/'hdd.json')],270)
        for relative in ('build/p0-full-volume-20261025-151856/result.json',
                         'build/p0-full-volume-20261025-152818/result.json',
                         'build/p0-full-volume-20261025-152858/result.json',
                         'build/p0-full-volume-20261025-153555/result.json',
                         'build/p0-final-73ce051-hdd-verify.json'):
            path = repo/relative
            try:
                raw = path.read_bytes()
                data = json.loads(raw)
                item = {'path':relative,'passed':data.get('passed') is True,
                        'sha256':hashlib.sha256(raw).hexdigest(), 'reusedEvidence':True}
            except Exception as exc:
                item = {'path':relative,'passed':False,'error':repr(exc)}
            evidence.append(item)
            print(('PASS ' if item['passed'] else 'FAIL ')+'retained evidence '+relative, flush=True)
    finally:
        result = {'passed':len(stages)==6 and len(evidence)==5 and all(s['passed'] for s in stages+evidence),
                  'stages':stages,'evidence':evidence,
                  'scope':'software safety; no physical power-loss/device/AV modification tests'}
        (report/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        print('REPORT '+str(report/'result.json'),flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
