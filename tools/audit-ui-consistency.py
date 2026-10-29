"""Source inventory, not a substitute for runtime layout/accessibility review."""
import argparse
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
lex = re.compile(r'(?P<comment>//[^\n]*|/\*[\s\S]*?\*/)|(?P<char>\'(?:\\.|[^\'\\])*\')|(?P<string>"(?:\\.|[^"\\])*")')
report = {"scope": "src + ui source files; no game directories", "middle_dot_literals": [],
          "font_overrides": [], "long_ui_literals": [], "largest_sources": []}
for folder in ("ui", "src"):
    for path in sorted((root / folder).rglob("*")):
        if path.suffix not in (".cpp", ".hpp"):
            continue
        source = path.read_text(encoding="utf-8")
        name = path.relative_to(root).as_posix()
        report["largest_sources"].append({"file": name, "lines": len(source.splitlines())})
        for match in lex.finditer(source):
            literal = match.group("string")
            if not literal:
                continue
            item = {"file": name, "line": source.count("\n", 0, match.start()) + 1}
            if "·" in literal:
                report["middle_dot_literals"].append({**item, "text": literal})
            if folder == "ui" and len(literal) > 100 and re.search(r"[\u4e00-\u9fff]", literal):
                report["long_ui_literals"].append({**item, "text": literal[:180]})
        if folder == "ui":
            for i, line in enumerate(source.splitlines(), 1):
                if re.search(r"\b\w+\.set(?:Point|Pixel)Size", line) and not line.lstrip().startswith("//"):
                    report["font_overrides"].append({"file": name, "line": i, "source": line.strip()})
report["largest_sources"] = sorted(report["largest_sources"], key=lambda x: x["lines"], reverse=True)[:12]
report["limitations"] = ["String scanning is heuristic; review findings before changing text.",
    "Icon rasterization and user-controlled log zoom are intentional font exceptions.",
    "This does not prove alignment, contrast, native DPI, reduced motion, or all runtime text."]
encoded = json.dumps(report, ensure_ascii=False, indent=2)
if args.output:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(encoded + "\n", encoding="utf-8")
print(json.dumps({key: len(report[key]) for key in
    ("middle_dot_literals", "font_overrides", "long_ui_literals")}, ensure_ascii=False))
if not args.output:
    print(encoded)
