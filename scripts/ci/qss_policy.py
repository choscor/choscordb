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
POSITIONAL = re.compile(r"%\d")
LITERAL_SIZE = re.compile(
    r"^\s*(font-size|(?:border(?:-[a-z]+)*-)?radius)\s*:\s*(?!0(?:px)?\s*;)\d", re.M
)
CASCADE_ENTRY = re.compile(r'u"([a-z0-9_/]+\.qss)"')
CASCADE_SOURCE = "desktop/design_system/style/stylesheet.cpp"
COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
RULE = re.compile(r"([^{}]+)\{([^{}]*)\}")


def declarations(content):
    """Yield (selector, property) pairs, splitting selector lists."""
    for selectors, body in RULE.findall(COMMENT.sub("", content)):
        names = [" ".join(item.split()) for item in selectors.split(",")]
        for declaration in body.split(";"):
            if ":" not in declaration:
                continue
            prop = declaration.split(":", 1)[0].strip()
            for name in names:
                if name:
                    yield name, prop


def duplicate_definitions(root):
    """Report selector and property pairs defined in two shared-cascade files."""
    source = root / CASCADE_SOURCE
    if not source.is_file():
        return []
    design = root / "desktop/design_system"
    owners = {}
    for entry in CASCADE_ENTRY.findall(source.read_text(encoding="utf-8")):
        path = design / entry
        if not path.is_file():
            continue
        for pair in declarations(path.read_text(encoding="utf-8")):
            owners.setdefault(pair, [])
            if entry not in owners[pair]:
                owners[pair].append(entry)
    return [
        f"desktop/design_system/{files[1]}: {selector} {{ {prop} }} already defined in "
        f"desktop/design_system/{files[0]}"
        for (selector, prop), files in owners.items()
        if len(files) > 1
    ]


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
            if POSITIONAL.search(line):
                problems.append(
                    f"{relative}:{number}: use a named @token, not a positional placeholder"
                )
        for match in LITERAL_SIZE.finditer(content):
            number = content.count("\n", 0, match.start()) + 1
            problems.append(
                f"{relative}:{number}: literal {match.group(1)}; use a size or radius @token"
            )
    problems.extend(duplicate_definitions(root))
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
