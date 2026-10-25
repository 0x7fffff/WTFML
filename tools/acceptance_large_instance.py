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
  4. external termination at a named transaction stage + startup recovery:
     the process is killed while it is parked in that stage (not after a fixed
     sleep), recovery restores the retained original, and a fresh transaction
     can complete afterwards;
  5. stage timings and the longest measured wait are recorded for the report.

The script does not claim a real slow disk was used; it records the measured
elapsed time, the volume type/filesystem, and lets the operator choose the work
directory. Keep the "slow disk" claim for an actual slow/removable volume.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
from ctypes import wintypes
from datetime import datetime, timezone
from pathlib import Path

GENERIC_READ = 0x80000000
OPEN_EXISTING = 3
FILE_ATTRIBUTE_NORMAL = 0x80
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

DRIVE_TYPES = {
    0: "unknown",
    1: "no-root",
    2: "removable",
    3: "fixed",
    4: "remote",
    5: "cdrom",
    6: "ramdisk",
}

STAGE_RE = re.compile(r"UPGRADE_STAGE name=([a-z-]+) elapsedMs=(\d+)")
HOLD_RE = re.compile(r"UPGRADE_HOLD name=([a-z-]+)")
RECOVER_RE = re.compile(r"UPGRADE_RECOVER count=(\d+)")


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


def volume_info(path: Path) -> dict:
    """Record what kind of volume the fixture is on; the report must not guess."""
    root = path.drive + "\\" if path.drive else ""
    info = {
        "root": root,
        "driveType": "unknown",
        "filesystem": "",
        "label": "",
        "totalBytes": None,
        "freeBytes": None,
    }
    if not root:
        return info
    try:
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.GetDriveTypeW.argtypes = [wintypes.LPCWSTR]
        kernel32.GetDriveTypeW.restype = wintypes.UINT
        info["driveType"] = DRIVE_TYPES.get(kernel32.GetDriveTypeW(root), "unknown")

        kernel32.GetVolumeInformationW.argtypes = [
            wintypes.LPCWSTR,
            wintypes.LPWSTR,
            wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD),
            ctypes.POINTER(wintypes.DWORD),
            ctypes.POINTER(wintypes.DWORD),
            wintypes.LPWSTR,
            wintypes.DWORD,
        ]
        kernel32.GetVolumeInformationW.restype = wintypes.BOOL
        label = ctypes.create_unicode_buffer(261)
        fs_name = ctypes.create_unicode_buffer(261)
        serial = wintypes.DWORD()
        max_component = wintypes.DWORD()
        flags = wintypes.DWORD()
        if kernel32.GetVolumeInformationW(
            root,
            label,
            261,
            ctypes.byref(serial),
            ctypes.byref(max_component),
            ctypes.byref(flags),
            fs_name,
            261,
        ):
            info["label"] = label.value
            info["filesystem"] = fs_name.value
    except OSError:
        pass
    try:
        usage = shutil.disk_usage(root)
        info["totalBytes"] = usage.total
        info["freeBytes"] = usage.free
    except OSError:
        pass
    return info


def defender_realtime() -> str:
    """Best-effort record; antivirus intervention is only evidence if this says True."""
    if os.name != "nt":
        return "not-windows"
    try:
        completed = subprocess.run(
            [
                "powershell",
                "-NoProfile",
                "-Command",
                "(Get-MpComputerStatus).RealTimeProtectionEnabled",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=30,
        )
        return completed.stdout.strip() or "unknown"
    except (OSError, subprocess.SubprocessError):
        return "unknown"


def parse_stages(output: str) -> list[dict]:
    return [
        {"name": match.group(1), "elapsedMs": int(match.group(2))}
        for match in STAGE_RE.finditer(output)
    ]


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


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


def kill_while_holding(
    exe: Path,
    args: list[str],
    env: dict,
    stage: str,
    timeout: float,
    log_path: Path,
):
    """Kill the probe only after it reports it is parked in `stage`.

    A fixed delay is not evidence: on a fast disk the transaction may already be
    finished (recovery then finds no journal), and on a slow disk it may still be
    in the previous stage. The hold line is printed from the production stage
    callback, so the kill lands in the named stage on both kinds of media.
    """
    with log_path.open("w", encoding="utf-8", errors="replace") as log:
        process = subprocess.Popen(
            [str(exe), *args],
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            text=True,
        )
        deadline = time.monotonic() + timeout
        held = False
        while time.monotonic() < deadline:
            if process.poll() is not None:
                break
            try:
                text = log_path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                text = ""
            if f"UPGRADE_HOLD name={stage}" in text:
                held = True
                break
            time.sleep(0.05)
        started_kill = time.monotonic()
        if process.poll() is None:
            process.kill()
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=15)
        kill_seconds = time.monotonic() - started_kill
    output = log_path.read_text(encoding="utf-8", errors="replace")
    return process.returncode, output, held, kill_seconds


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
    parser.add_argument(
        "--kill-stage",
        default="publish",
        choices=["publish", "verify"],
        help="transaction stage to park in before the external kill",
    )
    parser.add_argument(
        "--json-out",
        help="write scale, stage timings and the longest wait to this JSON file",
    )
    parser.add_argument("--timeout", type=float, default=600.0)
    parser.add_argument("--keep", action="store_true", help="keep the fixture")
    args = parser.parse_args()
    if args.payload_mb < 0 or args.files < 0 or not 0 < args.timeout <= 900:
        parser.error("payload/files must be nonnegative and timeout must be in (0, 900]")

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
    if any(work.iterdir()):
        raise SystemExit(f"work directory must be empty; existing files are never reused or deleted: {work}")
    game = work / "game"
    dest_root = work / "dest-game"
    source = game / "versions" / "Acceptance"
    target = game / "versions" / "Target"
    dest_root.mkdir(parents=True, exist_ok=True)
    started_at = datetime.now(timezone.utc).isoformat()

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
    original_hash = digest(source / "saves/World/region/large.bin")

    volume = volume_info(work)
    defender = defender_realtime()
    print(
        "work dir: {}  [{} / {} / {}]".format(
            work, volume["driveType"], volume["filesystem"] or "?", volume["label"] or "-"
        )
    )
    print(f"defender real-time protection: {defender}")

    failures: list[str] = []
    steps: list[dict] = []

    def check(ok: bool, message: str, detail: str = "") -> None:
        print(("PASS  " if ok else "FAIL  ") + message + (f"   [{detail}]" if detail else ""))
        if not ok:
            failures.append(message)

    def record(name: str, elapsed: float, ok: bool, stdout: str = "", note: str = "") -> None:
        stages = parse_stages(stdout)
        steps.append(
            {
                "name": name,
                "elapsedS": round(elapsed, 3),
                "ok": ok,
                "stages": stages,
                "note": note,
            }
        )

    staging_parent = dest_root / "versions" / ".wtfml-staging"
    try:
        completed, elapsed = run_probe(
            verify_exe,
            ["--large-copy", str(source), str(dest_root), "大副本"],
            env,
            args.timeout,
        )
        print(completed.stdout.strip())
        copy_ok = (
            completed.returncode == 0
            and (dest_root / "versions" / "大副本" / "大副本.jar").is_file()
            and not staging_parent.exists()
        )
        check(copy_ok, "large copy succeeds")
        check(
            (dest_root / "versions" / "大副本" / "大副本.jar").is_file(),
            "large copy publishes the renamed client jar",
        )
        check(not staging_parent.exists(), "large copy leaves no staging directory")
        copied_payload = dest_root / "versions" / "大副本" / "saves/World/region/large.bin"
        check(
            copied_payload.is_file()
            and digest(copied_payload) == original_hash,
            "large payload copied byte-for-byte",
        )
        print(f"      large copy elapsed: {elapsed:.2f}s")
        record("large-copy", elapsed, copy_ok, completed.stdout)

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
        locked_ok = (
            completed.returncode != 0
            and not (dest_root / "versions" / "被占用副本").exists()
            and not staging_parent.exists()
        )
        check(completed.returncode != 0, "file held without sharing makes the copy fail")
        check(
            not (dest_root / "versions" / "被占用副本").exists(),
            "failed copy publishes nothing under versions/",
        )
        check(not staging_parent.exists(), "failed copy cleans its own staging")
        source_payload = source / "saves" / "World" / "region" / "large.bin"
        check(
            source_payload.is_file()
            and digest(source_payload) == original_hash,
            "source payload is untouched after the failure",
        )
        print(f"      locked-copy elapsed: {elapsed:.2f}s")
        record("locked-copy", elapsed, locked_ok, completed.stdout)

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
        cancel_ok = completed.returncode == 0 and "cancelled=1" in completed.stdout
        check(cancel_ok, "large upgrade stops cooperatively at the requested point")
        marker = source / "config" / "user-marker.txt"
        check(
            marker.is_file() and marker.read_bytes() == b"USER",
            "cancelled upgrade leaves the original user files in place",
        )
        check(
            (source / "saves" / "World" / "region" / "large.bin").is_file()
            and (source / "saves" / "World" / "region" / "large.bin").stat().st_size
            == args.payload_mb * 1024 * 1024,
            "cancelled upgrade keeps the large user payload retryable",
        )
        print(f"      cancelled-upgrade elapsed: {elapsed:.2f}s")
        record("cancelled-upgrade", elapsed, cancel_ok, completed.stdout)

        # External termination while the production transaction is parked in the
        # named stage. The probe holds there until this process kills it, so the
        # evidence does not depend on how fast or slow the volume is.
        transaction_id = f"abandon-{args.kill_stage}"
        kill_log = work / f"kill-{args.kill_stage}.log"
        returncode, kill_output, held, kill_seconds = kill_while_holding(
            verify_exe,
            [
                "--large-upgrade-hold",
                str(game),
                "Acceptance",
                str(core_jar),
                args.kill_stage,
                transaction_id,
            ],
            env,
            args.kill_stage,
            args.timeout,
            kill_log,
        )
        print(kill_output.strip())
        killed_ok = held and returncode != 0 and "UPGRADE_HOLD_TIMEOUT" not in kill_output
        check(held, f"the probe reached and parked in the '{args.kill_stage}' stage")
        check(killed_ok, "external kill actually terminated the parked transaction process")
        check(
            returncode != 0 and "UPGRADE_HOLD_TIMEOUT" not in kill_output,
            "the probe did not exit by itself (a timeout is not process-interruption evidence)",
        )
        kill_stages = parse_stages(kill_output)
        check(
            bool(kill_stages) and kill_stages[-1]["name"] == args.kill_stage,
            "the last reported stage is the stage that was held",
            str(kill_stages[-1] if kill_stages else None),
        )
        # At the publish hold point the original has been renamed into the
        # transaction directory, so versions/Acceptance may legitimately be gone.
        # Anything present must be a complete old or new jar, never a partial file.
        pre_kill_jar = source / "Acceptance.jar"
        pre_kill_state = "missing"
        if pre_kill_jar.is_file():
            pre_kill_state = {
                b"old-core": "old",
                b"new-core": "new",
            }.get(pre_kill_jar.read_bytes(), "unexpected")
        check(
            pre_kill_state in {"missing", "old", "new"},
            "before recovery the live instance is either absent (retained) or a complete jar",
            pre_kill_state,
        )
        print(f"      kill wait: {kill_seconds:.3f}s; live jar state: {pre_kill_state}")
        record(
            f"kill-at-{args.kill_stage}",
            kill_seconds,
            killed_ok,
            kill_output,
            note=f"pre-recovery live jar state: {pre_kill_state}",
        )

        completed, elapsed = run_probe(
            verify_exe, ["--upgrade-recover", str(game)], env, args.timeout
        )
        print(completed.stdout.strip())
        recovered = RECOVER_RE.search(completed.stdout)
        recover_count = int(recovered.group(1)) if recovered else -1
        recover_ok = completed.returncode == 0 and recover_count > 0
        check(completed.returncode == 0, "startup recovery completes after an external kill")
        check(
            recover_count > 0,
            "interruption reached a recoverable journal (zero is not recovery evidence)",
            f"count={recover_count}",
        )
        marker = source / "config" / "user-marker.txt"
        marker_ok = marker.is_file() and marker.read_bytes() == b"USER"
        check(marker_ok, "recovery keeps the original instance and user files")
        payload_after = source / "saves" / "World" / "region" / "large.bin"
        payload_ok = (
            payload_after.is_file()
            and digest(payload_after) == original_hash
        )
        check(payload_ok, "recovery keeps the large payload readable")
        jar_ok = (
            (source / "Acceptance.jar").is_file()
            and (source / "Acceptance.jar").read_bytes() == b"old-core"
        )
        check(jar_ok, "recovery restores the retained original client jar")
        # The withheld new core must stay inspectable: at the publish hold it is
        # still in prepared/versions/<id>/, at the verify hold the published copy
        # was moved to replaced/ (that directory *is* the displaced instance).
        transaction = game / "cache" / "instance-upgrades" / transaction_id
        preserved = [
            transaction / "prepared" / "versions" / "Acceptance" / "Acceptance.jar",
            transaction / "replaced" / "Acceptance.jar",
        ]
        preserved_ok = any(
            path.is_file() and path.read_bytes() == b"new-core" for path in preserved
        )
        check(preserved_ok, "the unpublished new core is preserved for inspection")
        print(f"      recovery elapsed: {elapsed:.2f}s")
        record(
            "recovery",
            elapsed,
            recover_ok and marker_ok and payload_ok and jar_ok and preserved_ok,
            completed.stdout,
        )

        # The same user intent must still be executable after kill + recovery.
        completed, elapsed = run_probe(
            verify_exe,
            [
                "--large-upgrade-complete",
                str(game),
                "Acceptance",
                str(core_jar),
                f"retry-{args.kill_stage}",
            ],
            env,
            args.timeout,
        )
        print(completed.stdout.strip())
        retry_ok = (
            completed.returncode == 0
            and (source / "Acceptance.jar").read_bytes() == b"new-core"
            and marker.is_file()
            and marker.read_bytes() == b"USER"
            and payload_after.is_file()
            and digest(payload_after) == original_hash
        )
        check(retry_ok, "a fresh upgrade completes after kill and recovery")
        check(
            (source / "Acceptance.jar").is_file()
            and (source / "Acceptance.jar").read_bytes() == b"new-core",
            "the retried upgrade publishes the new client jar",
        )
        check(
            marker.is_file() and marker.read_bytes() == b"USER",
            "the retried upgrade keeps the original user files",
        )
        print(f"      retry-upgrade elapsed: {elapsed:.2f}s")
        record("retry-upgrade", elapsed, retry_ok, completed.stdout)
    except Exception as exc:  # noqa: BLE001 - the harness must still emit its report
        failures.append(f"unhandled error: {exc!r}")
        print(f"ERROR {exc!r}")
    except BaseException:
        args.keep = True
        raise
    finally:
        if not args.keep and not failures:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print(f"kept: {work}")

    # The artificial hold time is not a wait the product imposed; leave it out of
    # the longest-wait figure so a parked probe cannot look like slow I/O.
    measured = [step["elapsedS"] for step in steps if not step["name"].startswith("kill-at-")]
    longest_wait = max(measured) if measured else 0.0
    summary = {
        "schema": 1,
        "startedAt": started_at,
        "finishedAt": datetime.now(timezone.utc).isoformat(),
        "host": platform.node(),
        "platform": platform.platform(),
        "verifyExe": str(verify_exe),
        "workDir": str(work),
        "volume": volume,
        "defenderRealTimeProtection": defender,
        "scale": {
            "payloadMb": args.payload_mb,
            "files": args.files,
            "fixtureBytes": args.payload_mb * 1024 * 1024 + args.files * len(b"payload") + 64,
        },
        "killStage": args.kill_stage,
        "steps": steps,
        "longestWaitS": round(longest_wait, 3),
        "failures": failures,
        "passed": not failures,
    }
    if args.json_out:
        json_path = Path(args.json_out)
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(
            json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        print(f"json: {json_path}")

    print("\nstage timings:")
    for step in steps:
        stages = ", ".join(f"{item['name']}={item['elapsedMs']}ms" for item in step["stages"])
        print(f"  {step['name']:<24} {step['elapsedS']:>8.2f}s  {stages}")
    print(f"longest measured wait: {longest_wait:.2f}s")
    print(
        f"\nlarge-instance acceptance: {'PASS' if not failures else 'FAIL'} "
        f"({len(failures)} failure(s))"
    )
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
