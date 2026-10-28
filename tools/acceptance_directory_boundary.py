#!/usr/bin/env python3
"""Real product-entry acceptance with marked private INI/data profiles.
No registry edits, user credentials, external UI automation or existing game data.
The explicit acceptance mode captures/closes only its own Qt windows on timers.
All evidence is retained, including failed runs. A clean result is not real OAuth.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True)
    parser.add_argument('--root', required=True)
    parser.add_argument('--qt-bin')
    parser.add_argument('--in-place', action='store_true', help='run directly from an existing standalone bundle')
    parser.add_argument('--timeout', type=float, default=45)
    parser.add_argument('--keep', action='store_true', help='compatibility flag; evidence is always retained')
    args = parser.parse_args()
    if os.name != 'nt' or not 1 <= args.timeout <= 60:
        raise SystemExit('Windows required; timeout must be 1..60 seconds')
    exe = Path(args.exe).resolve()
    root = Path(args.root)
    if not root.is_absolute() or root.exists() or not exe.is_file():
        raise SystemExit('Use an existing product exe and a NEW absolute evidence root')
    root.mkdir(parents=True)
    env = os.environ.copy()
    if env.get('QT_QPA_PLATFORM') not in (None, '', 'windows'):
        raise SystemExit('Remove the non-native Qt platform override')
    if args.qt_bin:
        env['PATH'] = str(Path(args.qt_bin).resolve()) + os.pathsep + env.get('PATH', '')
    env['QT_FORCE_STDERR_LOGGING'] = '1'
    env.pop('QT_MESSAGE_PATTERN', None)
    env['QT_LOGGING_RULES'] = '*.info=true'
    deadline = time.monotonic() + 150
    processes, streams, checks = [], [], []
    report = {'format': 2, 'root': str(root), 'checks': checks, 'registryModified': False, 'inPlaceBundle': args.in_place}

    def check(ok, name):
        checks.append({'name': name, 'ok': bool(ok)})
        print(('PASS ' if ok else 'FAIL ') + name, flush=True)

    def profile(name, lifetime, stress):
        path = root / name
        path.mkdir()
        sentinel = 'WTFML_AUDIT_' + uuid.uuid4().hex
        (path / 'acceptance-profile.json').write_text(json.dumps({
            'format': 'wtfml-startup-acceptance/1', 'closeAfterMs': lifetime,
            'stressRows': stress, 'sentinel': sentinel}), encoding='utf-8')
        return path, sentinel

    def spawn(name, arguments):
        install = root / name
        if args.in_place:
            install = exe.parent
            binary = exe
        else:
            install.mkdir()
            binary = install / exe.name
            shutil.copy2(exe, binary)
        stderr = (root / (name + '.stderr.log')).open('wb')
        streams.append(stderr)
        process = subprocess.Popen([str(binary), *arguments], cwd=install, env=env,
                                   stdout=subprocess.DEVNULL, stderr=stderr)
        processes.append(process)
        return process

    def state(path, process):
        try:
            return json.loads((path / f'startup-{process.pid}.json').read_text(encoding='utf-8'))
        except (OSError, ValueError):
            return {}

    def wait_for(path, process, field):
        limit = min(deadline, time.monotonic() + args.timeout)
        while time.monotonic() < limit:
            result = state(path, process)
            if result.get(field):
                return result
            if process.poll() is not None:
                return result
            time.sleep(.1)
        return state(path, process)

    def exit_code(process):
        return process.wait(timeout=max(.1, min(50, deadline - time.monotonic())))

    try:
        shared, sentinel = profile('shared-profile', 30000, 200)
        independent, _ = profile('independent-profile', 15000, 0)
        first = spawn('install-a', ['--acceptance-profile', str(shared)])
        first_state = wait_for(shared, first, 'mainWindowShown')
        first_state = wait_for(shared, first, 'stressDone')
        check(first_state.get('mainWindowShown') and first_state.get('platform') == 'windows',
              'first real product entry displays its own native main window')
        check(first_state.get('stressDone') and first_state.get('stressRows') == 800,
              'four worker threads finish the controlled log load')
        check(Path(first_state.get('settingsFile', '')).is_relative_to(shared / 'settings'),
              'settings resolve inside the private INI profile')
        log = shared / 'game/logs/wtfml.log'
        before = log.read_bytes() if log.exists() else b''
        lines = [line for line in before.decode('utf-8', 'replace').splitlines() if '[acceptance-stress]' in line]
        check(len(lines) == 800 and all(line.count('[acceptance-stress]') == 1 and '<redacted>' in line for line in lines),
              'concurrent log rows remain complete and redacted')
        check(sentinel.encode() not in before, 'known synthetic credential never appears in the file log')
        other = spawn('install-c', ['--acceptance-profile', str(independent)])
        other_state = wait_for(independent, other, 'mainWindowShown')
        check(other_state.get('mainWindowShown') and other.poll() is None,
              'another data/profile directory can run concurrently')
        second = spawn('install-b', ['--acceptance-profile', str(shared)])
        second_state = wait_for(shared, second, 'refused')
        check(second_state.get('dialogShown') and second_state.get('screenshot'),
              'same data directory shows and captures the real refusal dialog')
        check(not second_state.get('mainWindowShown'), 'refused process never creates the main window')
        check(exit_code(second) == 1, 'refused process exits with code 1 after its own test timer dismisses the dialog')
        after = log.read_bytes() if log.exists() else b''
        check(bool(before) and after.startswith(before), 'refused process does not truncate the first process log')
        check(first.poll() is None and other.poll() is None, 'both accepted windows survive the refused attempt')
        check(exit_code(other) == 0 and exit_code(first) == 0, 'accepted windows close normally on their own test timers')
        stderr = (root / 'install-a.stderr.log').read_bytes()
        check(sentinel.encode() not in stderr and stderr.count(b'[acceptance-stress]') == 800,
              'stderr is also complete and contains no raw synthetic credential')
        for label, arguments in [('missing-marker', ['--acceptance-profile', str(root / 'unmarked')]),
                                 ('relative-profile', ['--acceptance-profile', 'relative']),
                                 ('missing-value', ['--acceptance-profile']),
                                 ('duplicate-profile', ['--acceptance-profile', str(shared), '--acceptance-profile', str(shared)])]:
            invalid = spawn(label, arguments)
            check(exit_code(invalid) == 2, label + ' is rejected before normal startup')
    except Exception as error:
        report['error'] = type(error).__name__
        check(False, 'acceptance could not complete; see retained per-process logs')
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
        for stream in streams:
            stream.close()
        report['ok'] = bool(checks) and all(item['ok'] for item in checks)
        (root / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('Evidence: ' + str(root / 'report.json'))
    return 0 if report['ok'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
