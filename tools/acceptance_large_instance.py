#!/usr/bin/env python3
"""Manual large-instance / failure-retention acceptance probe.

Not registered in CTest: it writes a real multi-hundred-MB instance and exercises
the production copy/upgrade paths through verify_instances' acceptance probes.
Point --work-dir at a slow disk or a drive with antivirus real-time scanning to
use it as the real-machine evidence harness.

Checks:
  1. large copy: publish lands in versions/<name>, no .wtfml-staging remains;
  2. file held without sharing (antivirus/file-lock shape): copy fails, the
     destination never appears, and the locked source file is untouched;
  3. upgrade cancellation during copy/snapshot: original instance and user
     payload stay in place (the staging copy is discarded, not published);
  4. abnormal exit during upgrade + startup recovery: original instance is
     still readable afterwards and no half replacement is left behind.

The script does not claim a real slow disk was used; it records the measured
elapsed time and lets the operator choose the work directory.
"""

from __future__ import annotations

import argparse
import ctypes
import os
import shutil
import subprocess
import sys
import tempfile
import time
from ctypes import wintypes
from pathlib import Path

GENERIC_READ = 0x80000000
OPEN_EXISTING = 3
FILE_ATTRIBUTE_NORMAL = 0x80
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value


def hold_without_sharing(path: Path):
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateFileW.argtypes = [
        wintypes.LPCWSTR,
        wintypes.DWORD,
        wintypes.DWORD,
        ctypes.c_void_p,
        wintypes.DWORD,
        wintypes.DWORD,
        ctypes.c_void_p,
    ]
    kernel32.CreateFileW.restype = wintypes.HANDLE
    handle = kernel32.CreateFileW(
        str(path), GENERIC_READ, 0, None, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, None
    )
    if handle == INVALID_HANDLE_VALUE:
        raise OSError(ctypes.get_last_error(), "CreateFileW failed", str(path))
    return kernel32, handle


def release(kernel32, handle) -> None:
    kernel32.CloseHandle(handle)


def write_bytes(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def write_blob(path: Path, size_mb: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    chunk = b"x" * (1024 * 1024)
    with path.open("wb") as stream:
        for _ in range(size_mb):
            stream.write(chunk)


def run_probe(exe: Path, args: list[str], env: dict, timeout: float):
    started = time.monotonic()
    completed = subprocess.run(
        [str(exe), *args],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return completed, time.monotonic() - started


def main() -> int:
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass
    parser = argparse.ArgumentParser(description="large instance acceptance probe")
    repo = Path(__file__).resolve().parent.parent
    parser.add_argument(
        "--verify-exe",
        default=str(repo / "build/verify/tests/verify_instances.exe"),
    )
    parser.add_argument("--qt-bin", help="directory with Qt6Core.dll (prepended to PATH)")
    parser.add_argument("--work-dir", help="directory for the large fixture (default: temp)")
    parser.add_argument("--payload-mb", type=int, default=256, help="one large file inside the instance")
    parser.add_argument("--files", type=int, default=1500, help="many small files beside it")
    parser.add_argument("--cancel-ms", type=int, default=200, help="upgrade cancellation point")
    parser.add_argument("--kill-after", type=float, default=0.8, help="abnormal-exit kill delay (s)")
    parser.add_argument("--timeout", type=float, default=600.0)
    parser.add_argument("--keep", action="store_true", help="keep the fixture")
    args = parser.parse_args()

    verify_exe = Path(args.verify_exe).resolve()
    if not verify_exe.is_file():
        raise SystemExit(f"verify executable not found: {verify_exe}")
    env = os.environ.copy()
    if args.qt_bin:
        env["PATH"] = str(Path(args.qt_bin).resolve()) + os.pathsep + env.get("PATH", "")

    work = (
        Path(args.work_dir).resolve()
        if args.work_dir
        else Path(tempfile.mkdtemp(prefix="wtfml-accept-large-"))
    )
    work.mkdir(parents=True, exist_ok=True)
    game = work / "game"
    dest_root = work / "dest-game"
    source = game / "versions" / "Acceptance"
    target = game / "versions" / "Target"
    dest_root.mkdir(parents=True, exist_ok=True)

    write_bytes(source / "Acceptance.jar", b"old-core")
    write_bytes(source / "Acceptance.json", b'{"id":"Acceptance"}')
    write_bytes(source / "wtfml.meta", b'{"instanceId":"acceptance-id","name":"Acceptance"}')
    write_bytes(source / "config" / "user-marker.txt", b"USER")
    write_bytes(source / "mods" / "old-mod.jar", b"old-mod")
    write_bytes(source / "saves" / "World" / "level.dat", b"level")
    write_blob(source / "saves" / "World" / "region" / "large.bin", args.payload_mb)
    for index in range(args.files):
        write_bytes(source / "saves" / "World" / "files" / f"{index:05d}.bin", b"payload")
    write_bytes(target / "Target.jar", b"new-core")
    write_bytes(target / "Target.json", b'{"id":"Target"}')
    core_jar = target / "Target.jar"

    failures: list[str] = []

    def check(ok: bool, message: str, detail: str = "") -> None:
        print(("PASS  " if ok else "FAIL  ") + message + (f"   [{detail}]" if detail else ""))
        if not ok:
            failures.append(message)

    staging_parent = dest_root / "versions" / ".wtfml-staging"
    try:
        completed, elapsed = run_probe(
            verify_exe,
            ["--large-copy", str(source), str(dest_root), "大副本"],
            env,
            args.timeout,
        )
        print(completed.stdout.strip())
        check(completed.returncode == 0, "large copy succeeds")
        check((dest_root / "versions" / "大副本" / "大副本.jar").is_file(),
              "large copy publishes the renamed client jar")
        check(not staging_parent.exists(), "large copy leaves no staging directory")
        copied_payload = dest_root / "versions" / "大副本" / "saves/World/region/large.bin"
        check(copied_payload.is_file()
              and copied_payload.stat().st_size == args.payload_mb * 1024 * 1024,
              "large payload copied byte-for-byte")
        print(f"      large copy elapsed: {elapsed:.2f}s")

        # Antivirus/file-lock shape: hold one source file without sharing while the
        # production copy runs. The operation must fail cleanly and must not publish.
        kernel32, handle = hold_without_sharing(source / "saves" / "World" / "region" / "large.bin")
        try:
            completed, elapsed = run_probe(
                verify_exe,
                ["--large-copy", str(source), str(dest_root), "被占用副本"],
                env,
                args.timeout,
            )
        finally:
            release(kernel32, handle)
        print(completed.stdout.strip())
        check(completed.returncode != 0, "file held without sharing makes the copy fail")
        check(not (dest_root / "versions" / "被占用副本").exists(),
              "failed copy publishes nothing under versions/")
        check(not staging_parent.exists(), "failed copy cleans its own staging")
        source_payload = source / "saves" / "World" / "region" / "large.bin"
        check(source_payload.is_file()
              and source_payload.stat().st_size == args.payload_mb * 1024 * 1024,
              "source payload is untouched after the failure")
        print(f"      locked-copy elapsed: {elapsed:.2f}s")

        completed, elapsed = run_probe(
            verify_exe,
            [
                "--large-upgrade-cancel",
                str(game),
                "Acceptance",
                str(core_jar),
                str(args.cancel_ms),
                "cancel-txn",
            ],
            env,
            args.timeout,
        )
        print(completed.stdout.strip())
        check(completed.returncode == 0 and "cancelled=1" in completed.stdout,
              "large upgrade stops cooperatively at the requested point")
        marker = source / "config" / "user-marker.txt"
        check(marker.is_file() and marker.read_bytes() == b"USER",
              "cancelled upgrade leaves the original user files in place")
        check((source / "saves" / "World" / "region" / "large.bin").is_file()
              and (source / "saves" / "World" / "region" / "large.bin").stat().st_size
              == args.payload_mb * 1024 * 1024,
              "cancelled upgrade keeps the large user payload retryable")
        print(f"      cancelled-upgrade elapsed: {elapsed:.2f}s")

        # Abnormal exit: start an upgrade and kill the process, then run recovery.
        process = subprocess.Popen(
            [
                str(verify_exe),
                "--large-upgrade-cancel",
                str(game),
                "Acceptance",
                str(core_jar),
                "600000",
                "abandon-txn",
            ],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(args.kill_after)
        process.kill()
        process.wait(timeout=10)
        check(process.returncode != 0, "abnormal-exit probe was actually killed")
        completed, elapsed = run_probe(
            verify_exe, ["--upgrade-recover", str(game)], env, args.timeout
        )
        print(completed.stdout.strip())
        check(completed.returncode == 0, "startup recovery completes after an abnormal exit")
        marker = source / "config" / "user-marker.txt"
        check(marker.is_file() and marker.read_bytes() == b"USER",
              "recovery keeps the original instance and user files")
        source_payload = source / "saves" / "World" / "region" / "large.bin"
        check(source_payload.is_file()
              and source_payload.stat().st_size == args.payload_mb * 1024 * 1024,
              "recovery keeps the large payload readable")
        check((source / "Acceptance.jar").is_file()
              and (source / "Acceptance.jar").read_bytes() == b"old-core",
              "recovery leaves the original client jar intact")
        print(f"      recovery elapsed: {elapsed:.2f}s")
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print(f"kept: {work}")

    print(f"\nlarge-instance acceptance: {'PASS' if not failures else 'FAIL'} ({len(failures)} failure(s))")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
