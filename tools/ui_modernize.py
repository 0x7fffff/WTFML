#!/usr/bin/env python3
"""Audit and tune WTFML's shared UI tokens without broad source rewrites."""

from __future__ import annotations

import argparse
import difflib
import json
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(__file__).with_name("ui-modernize.json")

# key: (relative file, regex matching only the value, replacement formatter)
TOKEN_RULES = {
    "action_height": ("ui/Theme.hpp", r"(?<=constexpr int actionHeight\(\) \{ return )\d+(?=; \})"),
    "primary_action_height": ("ui/Theme.hpp", r"(?<=constexpr int primaryActionHeight\(\) \{ return )\d+(?=; \})"),
    "control_radius": ("ui/Theme.hpp", r"(?<=constexpr int controlRadius\(\) \{ return )\d+(?=; \})"),
    "button_radius": ("ui/Theme.hpp", r"(?<=constexpr int buttonRadius\(\) \{ return )\d+(?=; \})"),
    "surface_radius": ("ui/Theme.hpp", r"(?<=constexpr int surfaceRadius\(\) \{ return )\d+(?=; \})"),
    "section_radius": ("ui/Theme.hpp", r"(?<=constexpr int sectionRadius\(\) \{ return )\d+(?=; \})"),
    "scrollbar_thickness": ("ui/Theme.hpp", r"(?<=constexpr int scrollBarThickness\(\) \{ return )\d+(?=; \})"),
    "scrollbar_handle_margin": ("ui/Theme.hpp", r"(?<=constexpr int scrollBarHandleMargin\(\) \{ return )\d+(?=; \})"),
    "scrollbar_min_handle_length": ("ui/Theme.hpp", r"(?<=constexpr int scrollBarMinHandleLength\(\) \{ return )\d+(?=; \})"),
    "page_margin": ("ui/MainWindowUI.hpp", r"(?<=static int pageMargin\(\) \{ return )\d+(?=; \})"),
    "task_progress_height": ("ui/MainWindowUI.hpp", r"(?<=static int taskCenterPanelBarHeight\(\) \{ return )\d+(?=; \})"),
    "task_step_spacing": ("ui/MainWindowUI.hpp", r"(?<=static int taskCenterPanelStepSpacing\(\) \{ return )\d+(?=; \})"),
    "bottom_bar_height": ("ui/MainWindowUI.cpp", r"(?<=constexpr int kBottomBarHeight = )\d+(?=;)"),
    "surface_padding": ("ui/MainWindowUI.cpp", r"(?<=constexpr int kSurfacePadding = )\d+(?=;)"),
    "task_panel_padding": ("ui/TaskCenterPanel.cpp", r"(?<=constexpr int kPanelPadding = )\d+(?=;)"),
    "task_row_spacing": ("ui/TaskCenterPanel.cpp", r"(?<=constexpr int kRowSpacing = )\d+(?=;)"),
    "task_section_spacing": ("ui/TaskCenterPanel.cpp", r"(?<=constexpr int kSectionSpacing = )\d+(?=;)"),
    "task_row_inner_spacing": ("ui/TaskCenterPanel.cpp", r"(?<=constexpr int kRowInnerSpacing = )\d+(?=;)"),
}

AUDIT_PATTERNS = {
    "hard-coded color": re.compile(r"#[0-9a-fA-F]{6,8}|QColor\s*\(\s*(?:0x|\d)"),
    "local style sheet": re.compile(r"\.setStyleSheet\s*\(|->setStyleSheet\s*\("),
    "literal geometry": re.compile(r"set(?:Fixed|Minimum|Maximum)(?:Width|Height|Size)\s*\(\s*\d+"),
    "literal spacing": re.compile(r"set(?:Spacing|ContentsMargins)\s*\([^;\n]*\d"),
    "native progress bar": re.compile(r"new\s+QProgressBar"),
    "progress text enabled": re.compile(r"setTextVisible\s*\(\s*true\s*\)"),
}


def load_config(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data.get("profiles"), dict):
        raise ValueError("配置缺少 profiles 对象")
    return data


def selected_profile(args: argparse.Namespace, data: dict) -> tuple[str, dict]:
    name = args.profile or data.get("default_profile")
    if name not in data["profiles"]:
        raise ValueError(f"未知 profile: {name}")
    profile = data["profiles"][name]
    missing = (set(TOKEN_RULES) | {"accent"}) - set(profile)
    if missing:
        raise ValueError("profile 缺少: " + ", ".join(sorted(missing)))
    if not re.fullmatch(r"#[0-9a-fA-F]{6}", str(profile["accent"])):
        raise ValueError("accent 必须是 #RRGGBB")
    for key in TOKEN_RULES:
        value = profile[key]
        if not isinstance(value, int) or not 0 <= value <= 256:
            raise ValueError(f"{key} 必须是 0..256 的整数")
    positive = set(TOKEN_RULES) - {"scrollbar_handle_margin"}
    zero_values = sorted(key for key in positive if profile[key] == 0)
    if zero_values:
        raise ValueError("以下尺寸必须大于 0: " + ", ".join(zero_values))
    if profile["primary_action_height"] < profile["action_height"]:
        raise ValueError("primary_action_height 不能小于 action_height")
    if profile["scrollbar_handle_margin"] * 2 >= profile["scrollbar_thickness"]:
        raise ValueError("scrollbar_handle_margin 必须小于 scrollbar_thickness 的一半")
    if profile["scrollbar_min_handle_length"] < profile["scrollbar_thickness"]:
        raise ValueError("scrollbar_min_handle_length 不能小于 scrollbar_thickness")
    return name, profile


def replace_once(text: str, pattern: str, value: int, label: str) -> str:
    updated, count = re.subn(pattern, str(value), text)
    if count != 1:
        raise RuntimeError(f"{label}: 预期匹配 1 次，实际 {count} 次；源码结构可能已变化")
    return updated


def prepare_changes(profile: dict) -> dict[Path, tuple[str, str]]:
    originals: dict[Path, str] = {}
    updated: dict[Path, str] = {}
    for key, (relative, pattern) in TOKEN_RULES.items():
        path = ROOT / relative
        if path not in originals:
            originals[path] = path.read_text(encoding="utf-8")
            updated[path] = originals[path]
        updated[path] = replace_once(updated[path], pattern, profile[key], key)

    accent_path = ROOT / "ui/Theme.cpp"
    originals[accent_path] = accent_path.read_text(encoding="utf-8")
    accent = profile["accent"].lstrip("#")
    replacement = f"return QColor(0x{accent[0:2]}, 0x{accent[2:4]}, 0x{accent[4:6]});"
    pattern = (
        r"(?s)(QColor\s+defaultAccentColor\(\)\s*\{.*?)"
        r"return QColor\(0x[0-9a-fA-F]{2}, 0x[0-9a-fA-F]{2}, 0x[0-9a-fA-F]{2}\);"
        r"(.*?\n\})"
    )
    updated[accent_path], count = re.subn(
        pattern, lambda match: match.group(1) + replacement + match.group(2),
        originals[accent_path])
    if count != 1:
        raise RuntimeError("accent: 无法唯一定位 defaultAccentColor")
    return {p: (originals[p], updated[p]) for p in originals if originals[p] != updated[p]}


def stage_text(path: Path, text: str) -> Path:
    """Write a complete sibling temporary file without touching the destination."""
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    staged = Path(temporary)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        return staged
    except BaseException:
        staged.unlink(missing_ok=True)
        raise


def write_transaction(changes: dict[Path, tuple[str, str]]) -> None:
    """Stage every file first and restore replaced files if a later replacement fails."""
    staged: dict[Path, Path] = {}
    replaced: list[Path] = []
    try:
        for path, (_, after) in changes.items():
            staged[path] = stage_text(path, after)
        for path in changes:
            os.replace(staged.pop(path), path)
            replaced.append(path)
    except BaseException as error:
        rollback_errors = []
        for path in reversed(replaced):
            try:
                original = changes[path][0]
                os.replace(stage_text(path, original), path)
            except BaseException as rollback_error:
                rollback_errors.append(f"{path}: {rollback_error}")
        detail = ""
        if rollback_errors:
            detail = "；回滚也有失败：" + " | ".join(rollback_errors)
        raise RuntimeError(f"写入未完成，已尝试恢复先前文件：{error}{detail}") from error
    finally:
        for temporary in staged.values():
            temporary.unlink(missing_ok=True)


def command_apply(args: argparse.Namespace) -> int:
    data = load_config(args.config)
    name, profile = selected_profile(args, data)
    changes = prepare_changes(profile)
    print(f"profile: {name}; changed files: {len(changes)}")
    if not changes:
        print("无需修改，当前源码已经符合该 profile。")
        return 0
    for path, (before, after) in changes.items():
        print("".join(difflib.unified_diff(
            before.splitlines(True), after.splitlines(True),
            fromfile=str(path.relative_to(ROOT)), tofile=str(path.relative_to(ROOT)))))
    if not args.write:
        print("以上仅为预览；确认后加 --write 写入。")
        return 0
    write_transaction(changes)
    print(f"已写入 {len(changes)} 个文件。")
    return 0


def audit_rows() -> list[tuple[str, int, str, str]]:
    rows = []
    for base in (ROOT / "ui", ROOT / "src"):
        for path in sorted(base.rglob("*")):
            if path.suffix not in {".cpp", ".hpp", ".h", ".ui"}:
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                for category, pattern in AUDIT_PATTERNS.items():
                    if pattern.search(line):
                        rows.append((path.relative_to(ROOT).as_posix(), number, category, line.strip()))
    return rows


def command_audit(args: argparse.Namespace) -> int:
    rows = audit_rows()
    counts = Counter(row[2] for row in rows)
    output = args.output or ROOT / "build/ui-modernize-report.md"
    output.parent.mkdir(parents=True, exist_ok=True)
    lines = ["# UI modernization audit", "", "Generated findings are review hints, not automatic errors.", ""]
    lines += [f"- {key}: {value}" for key, value in sorted(counts.items())]
    lines += ["", "## Findings", "", "| File | Line | Kind | Source |", "|---|---:|---|---|"]
    for file, number, category, source in rows:
        safe = source.replace("|", "\\|").replace("`", "\\`")
        lines.append(f"| `{file}` | {number} | {category} | `{safe[:180]}` |")
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    try:
        shown_output = output.resolve().relative_to(ROOT)
    except ValueError:
        shown_output = output.resolve()
    print(f"审计完成：{len(rows)} 项，报告 {shown_output}")
    for key, value in sorted(counts.items()):
        print(f"  {key}: {value}")
    return 0


def run_checked(command: list[str]) -> int:
    print("+ " + " ".join(command))
    return subprocess.run(command, cwd=ROOT, check=False).returncode


def command_verify(args: argparse.Namespace) -> int:
    result = run_checked(["git", "diff", "--check"])
    if result:
        return result
    name, profile = selected_profile(args, load_config(args.config))
    pending = prepare_changes(profile)
    if pending:
        files = ", ".join(str(path.relative_to(ROOT)) for path in pending)
        print(f"错误：当前源码尚未应用 profile {name}：{files}", file=sys.stderr)
        print("请先运行 apply 查看差异，确认后使用 apply --write。", file=sys.stderr)
        return 1
    print(f"令牌定位与配置校验通过，当前源码符合 profile {name}。")
    if args.build:
        result = run_checked(["cmake", "--build", "--preset", args.preset, "-j", str(args.jobs)])
        if result:
            return result
    if args.tests:
        result = run_checked(["ctest", "--preset", args.preset, "--output-on-failure"])
    return result


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="WTFML UI 批量审计与令牌调优")
    p.add_argument("--config", type=Path, default=CONFIG)
    sub = p.add_subparsers(dest="command", required=True)
    audit = sub.add_parser("audit", help="扫描需要人工收口的散落样式")
    audit.add_argument("--output", type=Path)
    audit.set_defaults(func=command_audit)
    apply = sub.add_parser("apply", help="预览或应用一个配置档")
    apply.add_argument("--profile")
    apply.add_argument("--write", action="store_true")
    apply.set_defaults(func=command_apply)
    verify = sub.add_parser("verify", help="检查配置、diff，可选构建和测试")
    verify.add_argument("--profile")
    verify.add_argument("--build", action="store_true")
    verify.add_argument("--tests", action="store_true")
    verify.add_argument("--preset", default="verification")
    verify.add_argument("--jobs", type=int, default=4)
    verify.set_defaults(func=command_verify)
    return p


def main() -> int:
    args = parser().parse_args()
    try:
        return args.func(args)
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"错误：{error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
