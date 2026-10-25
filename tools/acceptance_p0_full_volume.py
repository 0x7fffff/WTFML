"""Real ENOSPC acceptance, ONLY on the dedicated 256 MiB P0 VHD.

Evidence stays on the repository volume. Never recursively deletes fixtures.
Only this invocation's explicitly-created filler is removed in finally.
This is a full-volume test, NOT a power-loss or device-disconnection test.
"""
import argparse
import ctypes
import errno
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True)
    parser.add_argument('--qt-bin', required=True)
    parser.add_argument('--timeout', type=float, default=240)
    parser.add_argument('--mid-write-only', action='store_true', help='exhaust after the first real snapshot chunk')
    parser.add_argument('--zip-write-only', action='store_true', help='exhaust only after observing partial ZIP bytes')
    args = parser.parse_args()
    if os.name != 'nt' or not 0 < args.timeout <= 900:
        parser.error('Windows and timeout in (0,900] required')
    repo = Path(__file__).resolve().parent.parent
    record = json.loads((repo / 'build/p0-full-volume/mounted.json').read_text(encoding='utf-8-sig'))
    identity = 'WTFML-P0-ISOLATED-VHD-1'
    expected_image = repo / 'build/p0-full-volume/WTFML-P0-256MB.vhd'
    if record.get('identity') != identity or Path(record.get('image', '')).resolve() != expected_image.resolve():
        raise RuntimeError('Invalid VHD ownership record')
    volume = Path(record['volumeRoot'])
    if volume != Path(volume.anchor) or len(volume.drive) != 2:
        raise RuntimeError('Expected a local drive root')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)

    def verify_volume():
        label = ctypes.create_unicode_buffer(261)
        serial = ctypes.c_ulong()
        flags = ctypes.c_ulong()
        component = ctypes.c_ulong()
        fs = ctypes.create_unicode_buffer(261)
        if not kernel.GetVolumeInformationW(str(volume), label, len(label), ctypes.byref(serial),
                                            ctypes.byref(component), ctypes.byref(flags), fs, len(fs)):
            raise ctypes.WinError(ctypes.get_last_error())
        free, total, unused = ctypes.c_ulonglong(), ctypes.c_ulonglong(), ctypes.c_ulonglong()
        if not kernel.GetDiskFreeSpaceExW(str(volume), ctypes.byref(free), ctypes.byref(total), ctypes.byref(unused)):
            raise ctypes.WinError(ctypes.get_last_error())
        if label.value != 'WTFML_P0_ONLY' or fs.value != 'NTFS' or not 240*1024**2 <= total.value <= 256*1024**2:
            raise RuntimeError('Volume identity/size guard failed; refusing fill')
        if (volume / '.wtfml-p0-volume').read_text(encoding='ascii').strip() != identity:
            raise RuntimeError('Missing dedicated VHD sentinel')
        return {'label': label.value, 'serial': serial.value, 'total': total.value, 'free': free.value}

    initial = verify_volume()
    stamp = time.strftime('%Y%m%d-%H%M%S')
    evidence = repo / 'build' / ('p0-full-volume-' + stamp)
    evidence.mkdir(exist_ok=False)
    fixture = volume / ('acceptance-' + stamp)
    fixture.mkdir(exist_ok=False)
    deadline = time.monotonic() + args.timeout
    env = dict(os.environ)
    env['PATH'] = str(Path(args.qt_bin).resolve()) + os.pathsep + env.get('PATH', '')
    exe = str(Path(args.exe).resolve())
    checks, results = [], []
    filler = None

    def check(ok, message):
        checks.append({'ok': bool(ok), 'message': message})
        print(('PASS ' if ok else 'FAIL ') + message, flush=True)
        if not ok:
            raise RuntimeError(message)

    def run(case, flag, operation='', expected=0):
        command = [exe, flag, str(case)] + ([operation] if operation else [])
        output = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=max(.01, deadline-time.monotonic()))
        (evidence / f'{len(list(evidence.glob("*.log"))):02d}-{case.name}-{flag[5:]}.log').write_bytes(output.stdout)
        check(output.returncode == expected, f'{case.name}/{flag}: exit {output.returncode}, expected {expected}')
        records = [line[10:] for line in output.stdout.decode('utf-8', errors='replace').splitlines()
                   if line.startswith('P0_RESULT ')]
        result = json.loads(records[-1]) if records else None
        mid = [line[11:] for line in output.stdout.decode('utf-8', errors='replace').splitlines()
               if line.startswith('P0_MIDFULL ')]
        if result is not None and mid:
            result['midWrite'] = json.loads(mid[-1])
        return result

    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    error = None
    try:
        operations = ('snapshot',) if (args.mid_write_only or args.zip_write_only) else ('world', 'import', 'copy', 'snapshot', 'preset', 'migration', 'restore')
        for operation in operations:
            case = fixture / operation
            run(case, '--p0-prepare')
            run(case, '--p0-prepare-writes')
            # Tiny files can remain resident in NTFS MFT even when clusters are full.
            # Force real allocation/compression work instead of interpreting that success as a bug.
            payload = os.urandom((8 if args.zip_write_only else 2)*1024**2)
            paths = ('versions/RestoreTest/config/payload.bin',) if args.zip_write_only else ('versions/CompletionTest/config/payload.bin',
                             'versions/RestoreTest/config/payload.bin',
                             'preset-source/config/test.txt', 'migration-source/mods/test.jar')
            for relative in paths:
                (case/relative).write_bytes(payload)
            name = b'P0Payload'
            nbt = b'\x0a\x00\x00\x07' + struct.pack('>H', len(name)) + name + struct.pack('>i', len(payload)) + payload + b'\x00'
            if not args.zip_write_only:
                (case/'world/level.dat').write_bytes(nbt)
                (case/'world-source/level.dat').write_bytes(nbt)
                run(case, '--p0-write-attempt', 'snapshot')
                (case/'snapshot-target/snapshot.zip').replace(case/'restore.zip')
            originals = {
                'world': case/'world/level.dat', 'import': case/'world-source/level.dat',
                'copy': case/'versions/CompletionTest/CompletionTest.jar',
                'snapshot': case/'versions/RestoreTest/level.dat',
                'preset': case/'preset-target/config/test.txt',
                'migration': case/'migration-source/mods/test.jar',
                'restore': case/'versions/RestoreTest/level.dat',
            }
            original = originals[operation]
            before = digest(original)
            check(verify_volume()['serial'] == initial['serial'], 'same dedicated test volume')
            if args.mid_write_only or args.zip_write_only:
                filler = case/'mid-write-filler.bin'
                source = case/'versions/RestoreTest/config/payload.bin'
                source_before = digest(source)
                mode = 'snapshot-zipfull' if args.zip_write_only else 'snapshot-midfull'
                result = run(case, '--p0-write-attempt', mode, expected=4)
                event = (result or {}).get('midWrite', {})
                results.append({'operation': mode, 'failure': result})
                check(event.get('phase') == ('zip' if args.zip_write_only else 'staging'), 'exact write phase observed')
                check(0 < event.get('alreadyWritten', 0) < event.get('sourceSize', 0),
                      'snapshot: actual partial payload existed after space precheck and before exhaustion')
                check(event.get('nativeError') in (39, 112) and 0 <= event.get('freeBytes', -1) < 1024*1024 and
                      event.get('fillerBytes', 0) > 0,
                      'snapshot: real disk-full resource error during write, remaining space smaller than next chunk')
                check(not result['ok'] and bool(result['problem']), 'snapshot: resumed real write fails with a reason')
                check(digest(original) == before and digest(source) == source_before,
                      'snapshot: originals unchanged after mid-write ENOSPC')
                check(not (case/'snapshot-target/snapshot.zip').exists(), 'snapshot: incomplete archive never published')
                filler.unlink()
                filler = None
                run(case, '--p0-write-attempt', 'snapshot')
                check(True, 'snapshot: retry succeeds after mid-write disk-full failure')
                continue
            filler = fixture / (operation + '-filler.bin')
            allocated = 0
            exhausted = False
            with filler.open('xb', buffering=0) as stream:
                while allocated <= 256*1024**2:
                    if time.monotonic() >= deadline:
                        raise TimeoutError('overall test deadline')
                    try:
                        written = stream.write(b'X' * (64*1024))
                        allocated += written
                        if written <= 0:
                            raise RuntimeError('filler made no progress')
                    except OSError as exc:
                        if exc.errno != errno.ENOSPC and getattr(exc, 'winerror', None) not in (39, 112):
                            raise
                        exhausted = True
                        break
            check(exhausted, f'{operation}: real filesystem ENOSPC reached within 256 MiB cap')
            # Consume the last sub-chunk so small atomic writes cannot fit in the remainder.
            with filler.open('ab', buffering=0) as stream:
                for _ in range(16):
                    try:
                        stream.write(b'X'*4096)
                    except OSError as exc:
                        if exc.errno != errno.ENOSPC and getattr(exc, 'winerror', None) not in (39,112):
                            raise
                        break
            full = verify_volume()
            results.append({'operation': operation, 'fullVolume': full})
            flag = '--p0-restore' if operation == 'restore' else '--p0-write-attempt'
            result = run(case, flag, '' if operation == 'restore' else operation, expected=4)
            check(result is not None and not result['ok'] and bool(result['problem']),
                  f'{operation}: failure has a readable reason even when on-volume report cannot save')
            check(original.exists() and digest(original) == before, f'{operation}: original preserved')
            published = {'import': case/'versions/ImportTarget/saves/Imported',
                         'copy': case/'copy-target/versions/Copied',
                         'snapshot': case/'snapshot-target/snapshot.zip'}.get(operation)
            if published:
                check(not published.exists(), f'{operation}: no incomplete output published')
            results[-1]['failure'] = result
            filler.unlink()
            filler = None
            run(case, flag, '' if operation == 'restore' else operation)
            check(True, f'{operation}: retry succeeds after releasing only the test filler')
    except Exception as exc:
        error = repr(exc)
        print('ERROR ' + error, flush=True)
    finally:
        if filler is not None and filler.exists():
            # Explicit file created above; never delete arbitrary files or fixture trees.
            try:
                filler.unlink()
            except OSError as exc:
                error = (error or '') + '; filler cleanup failed: ' + repr(exc)
        (evidence/'result.json').write_text(json.dumps({
            'initialVolume': initial, 'fixture': str(fixture), 'checks': checks, 'results': results,
            'error': error, 'passed': error is None and all(c['ok'] for c in checks),
            'scope': ('snapshot ZIP mid-write ENOSPC; not all operations or power loss' if args.zip_write_only else
                      'snapshot staging mid-write ENOSPC; not all operations or power loss' if args.mid_write_only else
                      'already-full-volume; does not prove failure after partial business write or power loss'),
        }, ensure_ascii=False, indent=2), encoding='utf-8')
        print('Evidence retained: ' + str(evidence), flush=True)
    return 0 if error is None else 1


if __name__ == '__main__':
    raise SystemExit(main())
