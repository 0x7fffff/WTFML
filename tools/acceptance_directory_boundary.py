#!/usr/bin/env python3
"""Real two-process acceptance probe for the launcher's data-directory lock.

This is intentionally NOT part of CTest: it needs an interactive Windows desktop,
starts two real product executables, and checks the second one is refused. The
product keeps one launcher window per data root, so the second process must stop
at the refusal dialog and exit without creating a main window.

What it proves:
  * two installations (different exe directories) that share the global
    HKCU\\Software\\WTFML settings point at the same data root;
  * the second real product process is refused before it opens the main window
    or truncates logs/wtfml.log;
  * the first window stays alive and the second exits with code 1 after the
    dialog is dismissed;
  * the test restores the shared CurrentFolder setting it borrowed.

It is not a substitute for a real slow-disk/antivirus run and it does not
automate settings-page edits. Cross-install settings merging is covered by the
deterministic core tests (two stale LauncherSettings/GlobalJavaSettings
snapshots).
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
import winreg
from ctypes import wintypes
from pathlib import Path

MAIN_TITLE = "WTF Minecraft Launcher"
REFUSAL_TITLE = "\u542f\u52a8\u5668\u5df2\u5728\u8fd0\u884c\u6216\u76ee\u5f55\u4e0d\u53ef\u7528"
SETTINGS_KEY = r"Software\WTFML\WTFML\Launcher"
WINDOW_TITLE_LIMIT = 512


class Win32:
    def __init__(self) -> None:
        self.user32 = ctypes.WinDLL("user32", use_last_error=True)
        self.enum_proc = ctypes.WINFUNCTYPE(
            wintypes.BOOL, wintypes.HWND, wintypes.LPARAM
        )
        self.user32.EnumWindows.argtypes = [self.enum_proc, wintypes.LPARAM]
        self.user32.EnumWindows.restype = wintypes.BOOL
        self.user32.GetWindowTextW.argtypes = [
            wintypes.HWND,
            wintypes.LPWSTR,
            ctypes.c_int,
        ]
        self.user32.GetWindowTextW.restype = ctypes.c_int
        self.user32.GetWindowThreadProcessId.argtypes = [
            wintypes.HWND,
            ctypes.POINTER(wintypes.DWORD),
        ]
        self.user32.GetWindowThreadProcessId.restype = wintypes.DWORD
        self.user32.IsWindowVisible.argtypes = [wintypes.HWND]
        self.user32.IsWindowVisible.restype = wintypes.BOOL
        self.user32.PostMessageW.argtypes = [
            wintypes.HWND,
            wintypes.UINT,
            wintypes.WPARAM,
            wintypes.LPARAM,
        ]
        self.user32.PostMessageW.restype = wintypes.BOOL

    def windows(self) -> list[tuple[int, int, str, bool]]:
        result: list[tuple[int, int, str, bool]] = []

        @self.enum_proc
        def callback(hwnd, _lparam):
            text = ctypes.create_unicode_buffer(WINDOW_TITLE_LIMIT)
            if self.user32.GetWindowTextW(hwnd, text, WINDOW_TITLE_LIMIT) <= 0:
                return True
            pid = wintypes.DWORD()
            self.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            result.append(
                (
                    int(hwnd),
                    int(pid.value),
                    text.value,
                    bool(self.user32.IsWindowVisible(hwnd)),
                )
            )
            return True

        self.user32.EnumWindows(callback, 0)
        return result

    def window_titled(self, pid: int, title: str, timeout: float) -> int:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for hwnd, window_pid, text, visible in self.windows():
                if window_pid == pid and visible and text == title:
                    return hwnd
            time.sleep(0.2)
        return 0

    def post_close(self, hwnd: int) -> None:
        self.user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE


def snapshot_value(name: str):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, SETTINGS_KEY) as key:
            return winreg.QueryValueEx(key, name)
    except FileNotFoundError:
        return None


def restore_value(name: str, saved) -> None:
    with winreg.CreateKeyEx(
        winreg.HKEY_CURRENT_USER, SETTINGS_KEY, 0, winreg.KEY_SET_VALUE
    ) as key:
        if saved is None:
            try:
                winreg.DeleteValue(key, name)
            except FileNotFoundError:
                pass
        else:
            value, kind = saved
            winreg.SetValueEx(key, name, 0, kind, value)


def read_bytes(path: Path) -> bytes:
    try:
        return path.read_bytes()
    except OSError:
        return b""


def find_exe(repo: Path, explicit: str | None) -> Path:
    if explicit:
        candidate = Path(explicit).resolve()
        if candidate.is_file():
            return candidate
        raise SystemExit(f"not a file: {candidate}")
    for relative in (
        "build/verification-mingw13/WTFMinecraftLauncher.exe",
        "build/release-mingw13/WTFMinecraftLauncher.exe",
        "build/WTFMinecraftLauncher.exe",
    ):
        candidate = repo / relative
        if candidate.is_file():
            return candidate
    raise SystemExit("product exe not found; pass --exe")


def stage_install(source: Path, destination: Path) -> Path:
    destination.mkdir(parents=True, exist_ok=True)
    target = destination / source.name
    shutil.copy2(source, target)
    return target


def terminate(process: subprocess.Popen, timeout: float) -> int | None:
    try:
        return process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.terminate()
        try:
            return process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
            return None


def main() -> int:
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass
    parser = argparse.ArgumentParser(description="real dual-process lock acceptance")
    repo = Path(__file__).resolve().parent.parent
    parser.add_argument("--exe", help="product exe (default: find under build/)")
    parser.add_argument("--qt-bin", help="directory with Qt6Core.dll (prepended to PATH)")
    parser.add_argument("--keep", action="store_true", help="keep temp install/data dirs")
    parser.add_argument("--timeout", type=float, default=45.0, help="startup timeout (seconds)")
    args = parser.parse_args()

    exe = find_exe(repo, args.exe)
    environment = os.environ.copy()
    if args.qt_bin:
        environment["PATH"] = (
            str(Path(args.qt_bin).resolve()) + os.pathsep + environment.get("PATH", "")
        )
    environment["QT_FORCE_STDERR_LOGGING"] = "1"

    running = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq WTFMinecraftLauncher.exe", "/NH"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if "WTFMinecraftLauncher.exe" in running.stdout:
        raise SystemExit("a launcher is already running; close it before this acceptance probe")

    saved_current = snapshot_value("CurrentFolder")
    saved_title = snapshot_value("TitleText")
    temp = Path(tempfile.mkdtemp(prefix="wtfml-accept-boundary-"))
    install_a = temp / "install-a"
    install_b = temp / "install-b"
    shared_root = temp / "shared" / ".minecraft"
    shared_root.mkdir(parents=True, exist_ok=True)
    exe_a = stage_install(exe, install_a)
    exe_b = stage_install(exe, install_b)

    win32 = Win32()
    log_file = shared_root / "logs" / "wtfml.log"
    a: subprocess.Popen | None = None
    b: subprocess.Popen | None = None
    a_err = None
    b_err = None
    failures: list[str] = []

    def check(ok: bool, message: str, detail: str = "") -> None:
        print(("PASS  " if ok else "FAIL  ") + message + (f"   [{detail}]" if detail else ""))
        if not ok:
            failures.append(message)

    try:
        # Both installations share the global CurrentFolder setting. This is the
        # product-level form of "different install directories, one data root".
        with winreg.CreateKeyEx(
            winreg.HKEY_CURRENT_USER, SETTINGS_KEY, 0, winreg.KEY_SET_VALUE
        ) as key:
            winreg.SetValueEx(key, "CurrentFolder", 0, winreg.REG_SZ, str(shared_root))
            # The main window title is user-configurable. Pin it for the probe so both
            # windows can be identified by the product default, then restore it.
            winreg.SetValueEx(key, "TitleText", 0, winreg.REG_SZ, MAIN_TITLE)

        a_err = open(temp / "a.stderr.log", "wb")
        a = subprocess.Popen(
            [str(exe_a)],
            cwd=str(install_a),
            env=environment,
            stdout=subprocess.DEVNULL,
            stderr=a_err,
        )
        a_hwnd = win32.window_titled(a.pid, MAIN_TITLE, args.timeout)
        check(a_hwnd != 0, "first real product process shows its main window")
        if not a_hwnd:
            return 1

        # Let the first window settle, then remember what the log looked like.
        time.sleep(1.0)
        log_before = read_bytes(log_file)
        log_mtime_before = log_file.stat().st_mtime_ns if log_file.exists() else None

        b_err = open(temp / "b.stderr.log", "wb")
        b = subprocess.Popen(
            [str(exe_b)],
            cwd=str(install_b),
            env=environment,
            stdout=subprocess.DEVNULL,
            stderr=b_err,
        )
        b_hwnd = win32.window_titled(b.pid, REFUSAL_TITLE, min(args.timeout, 20.0))
        b_windows = [
            (hwnd, text)
            for hwnd, pid, text, visible in win32.windows()
            if pid == b.pid and visible
        ]
        check(b_hwnd != 0, "second real product process shows the refusal dialog", str(b_windows))
        check(
            not any(text == MAIN_TITLE for _hwnd, text in b_windows),
            "second process did not create a launcher main window",
            str(b_windows),
        )
        check(b.poll() is None, "second process is blocked at the dialog (no silent acceptance)")

        log_after = read_bytes(log_file)
        log_mtime_after = log_file.stat().st_mtime_ns if log_file.exists() else None
        check(
            log_after.startswith(log_before)
            and (log_after != log_before or log_mtime_after == log_mtime_before),
            "refused process did not truncate/rewrite logs/wtfml.log",
            f"before={len(log_before)} after={len(log_after)}",
        )

        # A survived the second attempt.
        check(a.poll() is None, "first process stays alive after the refusal")

        if b_hwnd:
            win32.post_close(b_hwnd)
        b_code = terminate(b, 15.0) if b is not None else None
        check(b_code == 1, "second process exits with code 1 after the dialog is dismissed", str(b_code))

        # The first window is still there and can be closed normally.
        check(
            win32.window_titled(a.pid, MAIN_TITLE, 2.0) == a_hwnd,
            "first window is still present after the second process exits",
        )
        win32.post_close(a_hwnd)
        a_code = terminate(a, 20.0) if a is not None else None
        check(a_code == 0, "first process exits cleanly when its window closes", str(a_code))
        check(log_file.exists(), "shared data root received its own launcher log")
    finally:
        for process in (b, a):
            if process is not None and process.poll() is None:
                process.kill()
                process.wait(timeout=5)
        for stream in (b_err, a_err):
            if stream is not None:
                stream.close()
        restore_value("CurrentFolder", saved_current)
        restore_value("TitleText", saved_title)
        if not args.keep:
            shutil.rmtree(temp, ignore_errors=True)
        else:
            print(f"kept: {temp}")

    print(f"\nboundary acceptance: {'PASS' if not failures else 'FAIL'} ({len(failures)} failure(s))")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
