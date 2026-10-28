#!/usr/bin/env python3
"""Read-only output audit. Never includes matched credential values in its report.
A clean scan is scoped evidence, not proof that a real OAuth flow was exercised.
"""
import argparse
import json
import re
import time
from urllib.parse import unquote
from pathlib import Path

VALUE = r"(?P<value>\"[^\"\r\n]+\"|'[^'\r\n]+'|[^\s,;}\]]+)"
RULES = [
    ("token-field", re.compile(r"(?i)[\"']?(?:access_token|refresh_token|accessToken|client_secret)[\"']?\s*[:=]\s*" + VALUE)),
    ("launch-argument", re.compile(r"(?i)--accessToken(?:[\"']?\s*,\s*|=|\s+)" + VALUE)),
    ("bearer-header", re.compile(r"(?i)\bBearer\s+" + VALUE)),
    ("jwt-shape", re.compile(r"(?P<value>\beyJ[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,})")),
]
SENTINELS = ("__MC_SECURITY_TEST_ABC123456789__",)

def findings(text):
    hits = []
    for kind, rule in RULES:
        for match in rule.finditer(text):
            value = unquote(match.group("value").strip("\"'"))
            placeholder = re.fullmatch(r"(?:\$\{[^}]+\}|\$[A-Za-z_][A-Za-z0-9_]*|%[A-Za-z_][A-Za-z0-9_]*%)", value)
            if len(value) < 12 or placeholder or any(c in value for c in ("<", ">", "*")):
                continue
            hits.append({"line": text.count("\n", 0, match.start()) + 1, "kind": kind, "valueLength": len(value)})
    for sentinel in SENTINELS:
        start = 0
        while (pos := text.find(sentinel, start)) >= 0:
            hits.append({"line": text.count("\n", 0, pos) + 1, "kind": "known-test-sentinel", "valueLength": len(sentinel)})
            start = pos + len(sentinel)
    return hits

def self_test():
    for text in ('--accessToken "0123456789abcdefgh"', '"refresh_token":"0123456789abcdefgh"', 'Authorization: Bearer 0123456789abcdefgh', '("--accessToken", "0123456789abcdefgh")', 'access_token=0123456789%2Eabcdefgh', SENTINELS[0]):
        assert findings(text), "positive audit fixture was missed"
    for text in ('--accessToken 0', '--accessToken <redacted>', '--accessToken %WTFML_MC_ACCESS_TOKEN%', '"access_token":"${token}"'):
        assert not findings(text), "redacted/placeholder fixture was incorrectly flagged"
    encoded = json.dumps(findings('--accessToken 0123456789abcdefgh'))
    assert '0123456789abcdefgh' not in encoded, "report must not copy credentials"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', action='append', default=[])
    parser.add_argument('--report')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
    paths = set()
    errors = []
    for value in args.root:
        root = Path(value).resolve()
        if not root.exists():
            errors.append({"path": str(root), "reason": "missing root"})
            continue
        if root.is_file():
            paths.add(root)
        else:
            for path in root.glob('*'):
                if len(paths) >= 4096:
                    errors.append({"path": str(root), "reason": "4096-file cap reached"})
                    break
                if path.is_file() and not path.is_symlink() and path.suffix.lower() in ('.log', '.txt', '.bat', '.cmd', '.ps1'):
                    paths.add(path)
    results = []
    scanned = 0
    total_bytes = 0
    deadline = time.monotonic() + 60
    for path in sorted(paths):
        if time.monotonic() >= deadline:
            errors.append({"reason": "60-second audit deadline reached"})
            break
        try:
            size = path.stat().st_size
            if size > 32 * 1024 * 1024 or total_bytes + size > 128 * 1024 * 1024:
                errors.append({"path": str(path), "reason": "size budget exceeded; not scanned"})
                continue
            data = path.read_bytes()
            total_bytes += len(data)
            text = data.decode('utf-8', errors='replace')
            scanned += 1
            for hit in findings(text):
                results.append({"file": str(path), **hit})
        except OSError as error:
            errors.append({"path": str(path), "reason": type(error).__name__})
    report = {"format": 1, "selfTest": args.self_test, "filesScanned": scanned,
              "bytesScanned": total_bytes, "suspects": results, "errors": errors,
              "scope": "supplied files and top-level output samples only; no real OAuth verification or encoded/binary credential audit"}
    if args.report:
        Path(args.report).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({"filesScanned": scanned, "suspects": len(results), "errors": len(errors), "selfTest": args.self_test}))
    return 1 if results or errors else 0

if __name__ == '__main__':
    raise SystemExit(main())
