#!/usr/bin/env python3
"""Real NTFS deny/release matrix in a new private fixture (no administrator).

Only this script's freshly created directories receive deny entries. Remove the
exact deny entry in finally; never grant global permissions or delete evidence.
"""
import argparse
import csv
import io
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
    if os.name != "nt" or not 0 < args.timeout <= 900:
        parser.error("Windows required; timeout must be in (0, 900]")
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

    def run(command, expected=0):
        nonlocal counter
        counter += 1
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=max(0.01, deadline - time.monotonic()))
        (root / f"{counter:02d}.log").write_bytes(result.stdout)
        if result.returncode != expected:
            raise RuntimeError(f"exit {result.returncode}, expected {expected}: {command[0:2]}; log {counter}")
        return result.stdout

    def probe(flag, *extra, expected=0):
        run([exe, flag, str(root / "game"), *extra], expected)

    def check(ok, message):
        checks.append({"ok": bool(ok), "message": message})
        print(("PASS " if ok else "FAIL ") + message, flush=True)
        if not ok:
            raise RuntimeError(message)

    sid = next(csv.reader(io.StringIO(run(["whoami", "/user", "/fo", "csv", "/nh"]).decode(errors="replace"))))[1]
    if not sid.startswith("S-1-"):
        raise RuntimeError("unable to resolve current SID")
    error = None
    active = None
    try:
        probe("--p0-prepare")
        probe("--p0-prepare-writes")
        game = root / "game"
        world_before = (game / "world/level.dat").read_bytes()
        source_world_before = (game / "world-source/level.dat").read_bytes()
        targets = {
            "world": game / "world",
            "import": game / "versions/ImportTarget/saves",
            "copy": game / "copy-target/versions",
            "snapshot": game / "snapshot-target",
            "preset": game / "preset-target/config",
            "migration": game / "migration-target/mods",
            "restore": game / "versions",
            "delete": game / "delete-target",
        }
        for operation, target in targets.items():
            check(target.is_relative_to(game) and target.is_dir(), f"{operation}: private target verified")
            permissions = "(OI)(CI)(DE,DC)" if operation == "delete" else "(OI)(CI)(WD,AD)"
            active = target
            run(["icacls", str(target), "/deny", f"*{sid}:{permissions}"])
            try:
                if operation == "restore":
                    probe("--p0-restore", expected=4)
                    result = json.loads((game / "restore-result.json").read_text(encoding="utf-8"))
                else:
                    probe("--p0-write-attempt", operation, expected=4)
                    result = json.loads((game / "write-result.json").read_text(encoding="utf-8"))
                check(not result["ok"] and bool(result["problem"]), f"{operation}: denied operation reports failure")
                preserved = {
                    "world": (game / "world/level.dat", world_before),
                    "import": (game / "world-source/level.dat", source_world_before),
                    "copy": (game / "versions/CompletionTest/CompletionTest.jar", b"CLIENT"),
                    "snapshot": (game / "versions/RestoreTest/level.dat", b"CURRENT"),
                    "preset": (game / "preset-target/config/test.txt", b"ORIGINAL"),
                    "migration": (game / "migration-source/mods/test.jar", b"MOD"),
                    "restore": (game / "versions/RestoreTest/level.dat", b"CURRENT"),
                    "delete": (game / "delete-target/keep.txt", b"KEEP"),
                }[operation]
                check(preserved[0].is_file() and
                      (preserved[1] is None or preserved[0].read_bytes() == preserved[1]),
                      f"{operation}: original content retained")
                if operation == "import":
                    check(not (target / "Imported").exists(), "import: denied operation publishes no world")
                if operation == "copy":
                    check(not (target / "Copied").exists(), "copy: denied operation publishes no instance")
                if operation == "snapshot":
                    check(not (target / "snapshot.zip").exists(), "snapshot: no incomplete archive published")
            finally:
                # Only remove this SID's deny entries, never replace the ACL.
                run(["icacls", str(target), "/remove:d", f"*{sid}", "/t", "/c"])
                active = None
            if operation == "restore":
                probe("--p0-restore")
            else:
                probe("--p0-write-attempt", operation)
            check(True, f"{operation}: operation succeeds after deny entry removed")
    except Exception as exc:
        error = repr(exc)
        print("ERROR " + error, flush=True)
    finally:
        if active is not None:
            # Best effort even if the overall test budget is exhausted.
            subprocess.run(["icacls", str(active), "/remove:d", f"*{sid}", "/t", "/c"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        summary = {"passed": error is None and all(c["ok"] for c in checks),
                   "error": error, "checks": checks, "workDir": str(root)}
        (root / "result.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"Evidence retained: {root}", flush=True)
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
