"""Check that Qt style rules stay in colocated, readable QSS resources."""

from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
STRING = re.compile(r'R"\((.*?)\)"|"((?:\\.|[^"\\])*)"', re.DOTALL)
CSS_RULE = re.compile(r"\{[^}]*\b[a-z][a-z-]*\s*:", re.DOTALL)
LOAD_PATH = re.compile(r'loadStyleSheet\s*\(\s*QStringLiteral\s*\(\s*"([^"]+)"')
SNAKE_CASE = re.compile(r"[a-z][a-z0-9]*(?:_[a-z0-9]+)*")


def violations(root=ROOT):
    problems = []
    design = root / "desktop/design_system"
    manifest = root / "desktop/resources/styles.qrc"
    if not manifest.is_file():
        return ["desktop/resources/styles.qrc: missing QSS resource manifest"]
    try:
        tree = ET.parse(manifest)
        style_roots = [
            node for node in tree.iter("qresource") if node.get("prefix") == "/styles"
        ]
        if not style_roots:
            problems.append("desktop/resources/styles.qrc: missing /styles prefix")
        resources = {
            node.get("alias"): (manifest.parent / node.text).resolve()
            for style_root in style_roots
            for node in style_root.iter("file")
            if node.text
        }
    except ET.ParseError as error:
        return [f"desktop/resources/styles.qrc: invalid XML: {error}"]

    for path in sorted(design.rglob("*")):
        if path.suffix in {".cpp", ".h"}:
            source = path.read_text(encoding="utf-8")
            adjacent = ""
            previous_end = 0
            for match in STRING.finditer(source):
                current = match.group(1) or match.group(2)
                adjacent = (
                    adjacent + current
                    if not source[previous_end : match.start()].strip()
                    else current
                )
                previous_end = match.end()
                if CSS_RULE.search(adjacent):
                    number = source.count("\n", 0, match.start()) + 1
                    problems.append(
                        f"{path.relative_to(root)}:{number}: inline QSS declaration"
                    )
                    adjacent = ""
            for loaded in LOAD_PATH.findall(source):
                if loaded not in resources:
                    problems.append(
                        f"{path.relative_to(root)}: missing QSS alias {loaded}"
                    )
        if path.suffix != ".qss":
            continue
        relative = path.relative_to(root)
        if not SNAKE_CASE.fullmatch(path.stem):
            problems.append(f"{relative}: filename must use snake_case")
        alias = f"{path.parent.name}/{path.name}"
        if resources.get(alias) != path.resolve():
            problems.append(
                f"{relative}: missing or incorrect styles.qrc alias {alias}"
            )
        content = path.read_text(encoding="utf-8")
        if not content.strip():
            problems.append(f"{relative}: empty stylesheet")
        if content.count("{") != content.count("}"):
            problems.append(f"{relative}: unbalanced rule braces")
        for number, line in enumerate(content.splitlines(), 1):
            if ("{" in line and "}" in line) or line.count(";") > 1:
                problems.append(
                    f"{relative}:{number}: put declarations on separate lines"
                )
    for path in sorted(resources.values()):
        if not path.is_file():
            problems.append(f"{path}: missing QSS resource")
    return problems


def main():
    problems = violations()
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
