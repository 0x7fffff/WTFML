#!/usr/bin/env python3
"""Bounded real-process restore/import acceptance. Only writes a new fixture root.

This is not a power-loss, full-volume, antivirus or physical I/O failure test.
Failed fixtures and per-process logs are always retained; no arbitrary cleanup.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--qt-bin", required=True)
    parser.add_argument("--work-dir", required=True)
    parser.add_argument("--timeout", type=float, default=240)
    args = parser.parse_args()
    if not 0 < args.timeout <= 900:
        parser.error("timeout must be in (0, 900]")
    root = Path(args.work_dir).resolve()
    if root.exists() and any(root.iterdir()):
        parser.error("work directory must be new or empty")
    root.mkdir(parents=True, exist_ok=True)
    exe = str(Path(args.exe).resolve())
    env = dict(os.environ)
    env["PATH"] = str(Path(args.qt_bin).resolve()) + os.pathsep + env.get("PATH", "")
    deadline = time.monotonic() + args.timeout
    checks = []
    counter = 0

    def check(ok, message):
        checks.append({"ok": bool(ok), "message": message})
        print(("PASS " if ok else "FAIL ") + message, flush=True)
        if not ok:
            raise RuntimeError(message)

    def run(flag, case, *extra, hold=None, expected=0):
        nonlocal counter
        counter += 1
        log = root / f"{counter:02d}-{flag.removeprefix('--p0-')}.log"
        with log.open("wb") as stream:
            process = subprocess.Popen([exe, flag, str(case), *extra], env=env,
                                       stdout=stream, stderr=subprocess.STDOUT)
            try:
                if hold:
                    while time.monotonic() < deadline:
                        if process.poll() is not None:
                            raise RuntimeError(f"exited before hold: {log}")
                        if f"P0_HOLD {hold}" in log.read_text(encoding="utf-8", errors="replace"):
                            process.kill()
                            process.wait(timeout=10)
                            check(process.returncode != 0, f"{hold}: externally terminated while parked")
                            return
                        time.sleep(0.05)
                    raise TimeoutError(f"hold deadline: {log}")
                code = process.wait(timeout=max(0.01, deadline - time.monotonic()))
                check(code == expected, f"{flag}: exit {code}, expected {expected}")
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=10)

    def prepare(name):
        case = root / name
        run("--p0-prepare", case)
        return case, case / "versions/RestoreTest"

    error = None
    try:
        for phase in ("retained", "published"):
            case, target = prepare("restore-" + phase)
            run("--p0-restore-hold", case, phase, hold="restore-" + phase)
            originals = list((case / "versions").glob(".wtfml-restore-previous-*/level.dat"))
            check(len(originals) == 1 and originals[0].read_bytes() == b"CURRENT",
                  f"{phase}: original retained after external termination")
            run("--p0-recover", case)
            messages = json.loads((case / "recovery-result.json").read_text(encoding="utf-8"))["messages"]
            check(bool(messages), f"{phase}: recovery reported actual transaction")
            if phase == "retained":
                check((target / "level.dat").read_bytes() == b"CURRENT", "retained: original restored")
                run("--p0-recover", case)
                check(not json.loads((case / "recovery-result.json").read_text(encoding="utf-8"))["messages"],
                      "retained: second recovery is idempotent")
            else:
                check((target / "level.dat").read_bytes() == b"BACKUP" and originals[0].read_bytes() == b"CURRENT",
                      "published: recovery never overwrites either complete directory")
            run("--p0-restore", case)
            check((target / "level.dat").read_bytes() == b"BACKUP" and
                  (target / "config/user.txt").read_bytes() == b"USER", f"{phase}: restore can run again")

        case, target = prepare("restore-collision")
        run("--p0-restore-collision", case, expected=4)
        result = json.loads((case / "restore-result.json").read_text(encoding="utf-8"))
        originals = list((case / "versions").glob(".wtfml-restore-previous-*/level.dat"))
        check(not result["ok"] and result["recoveryFailed"] and len(originals) == 1 and
              originals[0].read_bytes() == b"CURRENT" and "wtfml-restore-previous" in result["problem"],
              "publish/rollback failure reports retained original location")
        run("--p0-recover", case)
        check((target / "external.txt").read_bytes() == b"EXTERNAL" and originals[0].read_bytes() == b"CURRENT",
              "recovery refuses to overwrite external target")
        # This is our exact collision fixture, not a guessed directory or glob.
        (target / "external.txt").unlink()
        target.rmdir()
        run("--p0-recover", case)
        check((target / "level.dat").read_bytes() == b"CURRENT", "collision removed: original recoverable")
        run("--p0-restore", case)

        case, target = prepare("restore-journal-lock")
        run("--p0-restore-journal-lock", case)
        result = json.loads((case / 'restore-result.json').read_text(encoding='utf-8'))
        check(result['journalLocked'], 'commit journal was exclusively held during the actual publication update')
        retained = Path(result['retainedPath'])
        check(result['ok'] and retained.is_relative_to(case/'versions') and
              (retained/'level.dat').read_bytes() == b'CURRENT' and
              (target/'level.dat').read_bytes() == b'BACKUP',
              'journal commit failure retains both published backup and original, with explicit retained path')
        journals = list((case/'cache/restore-transactions').glob('*.json'))
        check(len(journals) == 1 and json.loads(journals[0].read_text(encoding='utf-8'))['state'] != 'published',
              'failed publication journal remains in pre-commit state, never falsely records a durable commit')
        run('--p0-recover', case)
        check((retained/'level.dat').read_bytes() == b'CURRENT' and
              (target/'level.dat').read_bytes() == b'BACKUP',
              'fresh recovery after journal failure never overwrites either complete directory')
        run('--p0-restore', case)

        case, _ = prepare("completion-report")
        instance = case / "versions/CompletionTest"
        run("--p0-completion-hold", case, hold="completion-report")
        report = json.loads((instance / "wtfml.import-report.json").read_text(encoding="utf-8"))
        check(report["complete"] and (instance / "wtfml.importing").is_file(),
              "completed report alone does not remove unfinished gate")
        check((instance / "config/user.txt").read_bytes() == b"USER", "completion interruption preserves files")
        run("--p0-completion", case)
        result = json.loads((case / "completion-result.json").read_text(encoding="utf-8"))
        meta = json.loads((instance / "wtfml.meta").read_text(encoding="utf-8"))
        check(result["ok"] and not result["unfinished"] and not (instance / "wtfml.importing").exists(),
              "fresh process completes import without original archive")
        check(meta["jvmArgs"] == ["-Dkeep=user"], "completion retry preserves user JVM options")
    except Exception as exc:
        error = repr(exc)
        print("ERROR " + error, flush=True)
    finally:
        summary = {"passed": error is None and all(c["ok"] for c in checks),
                   "error": error, "checks": checks, "workDir": str(root)}
        (root / "result.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"Evidence retained: {root}", flush=True)
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
