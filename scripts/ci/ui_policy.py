#!/usr/bin/env python3
"""Reject local desktop visual constants/stylesheets and missing icon resources.

Rules added after the design-system consistency audit accept a reviewed
``// ui-ok: <reason>`` marker on the line or the line above.
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
CPP_SUFFIXES = {".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".mm"}
COLOR = re.compile(r"QColor\s*\(\s*(?:QStringLiteral\s*\()?\s*\"#[0-9A-Fa-f]{3,8}")
STYLE = re.compile(r"\bsetStyleSheet\s*\(")
METRIC = re.compile(
    r"\b(?:resize|setIconSize|setContentsMargins|"
    r"set(?:Minimum|Maximum|Fixed)(?:Width|Height|Size)|setSpacing)"
    r"\s*\(\s*[1-9][0-9]*"
)
FEATURE_INCLUDE = re.compile(
    r'^\s*#\s*include\s*[<"](?:\.\./)*(?:app|widgets|bridge|models|tools)/'
)
REQUIRED_ICONS = {"app-mark.svg", "play.svg", "square.svg", "plus.svg"}
MARKER = re.compile(r"//\s*ui-ok:\s*\S")
LITERAL = re.compile(r"^\s*[1-9][0-9]*\s*$")
# Calls whose listed argument positions are presentation metrics.
METRIC_ARGUMENTS = {
    "setContentsMargins": None,  # every argument
    "QMargins": None,
    "QSize": None,
    "addSpacing": (0,),
    "setMargin": (0,),
    "setIndentation": (0,),
    "setDefaultSectionSize": (0,),
    "setMinimumSectionSize": (0,),
    "setColumnWidth": (1,),
    "resizeSection": (1,),
    "themedIcon": (2,),
    "pixmap": (0, 1),
}
CALL = re.compile(r"\b(" + "|".join(METRIC_ARGUMENTS) + r")\s*\(")
RGB_COLOR = re.compile(
    r"\bQColor\s*\(\s*[0-9]|\bQt::(?:red|green|blue|black|white|gray|"
    r"darkGray|lightGray|yellow|cyan|magenta|darkRed|darkGreen|darkBlue)\b"
)
RAW_MENU = re.compile(r"\b\w*[mM]enu\w*\s*(?:\.|->)\s*(?:popup|exec)\s*\(")
STANDARD_ICON = re.compile(r"\bstandardIcon\s*\(")
STOCK_BUTTONS = re.compile(
    r"QDialogButtonBox\s*\(\s*QDialogButtonBox::(?:Ok|Cancel|Close|Save|Apply|"
    r"Yes|No|Discard|Reset)\b"
)
STYLED_PROPERTY = re.compile(
    r"setProperty\s*\(\s*\"(designRole|variant|state|designSurface)\"\s*,"
)
PAINT_GEOMETRY = re.compile(r"(?:\.|->)\s*(adjusted|drawRoundedRect)\s*\(")
LARGE_LITERAL = re.compile(r"^\s*-?\s*(?:[2-9]|[1-9][0-9]+)(?:\.0*)?\s*$")
QSS_SELECTOR = re.compile(r"\[\s*(\w+)\s*=\s*\"?([\w-]+)\"?\s*\]")


def arguments(text, start):
    """Return top-level arguments of the call whose '(' is at start."""
    depth, current, parts = 0, [], []
    for char in text[start:]:
        if char in "([{":
            depth += 1
            if depth == 1:
                continue
        elif char in ")]}":
            depth -= 1
            if depth == 0:
                parts.append("".join(current))
                return parts
        elif char == "," and depth == 1:
            parts.append("".join(current))
            current = []
            continue
        current.append(char)
    return parts


def styled_values(design):
    """Property values the design system styles in QSS or reads in C++."""
    known = {}
    for path in design.rglob("*"):
        if path.suffix == ".qss":
            for name, value in QSS_SELECTOR.findall(path.read_text(encoding="utf-8")):
                known.setdefault(name, set()).add(value)
        elif path.suffix in CPP_SUFFIXES:
            for line in path.read_text(encoding="utf-8").splitlines():
                for name in ("designRole", "variant", "state", "designSurface"):
                    if f'"{name}"' in line:
                        known.setdefault(name, set()).update(
                            re.findall(r'"([\w-]+)"', line)
                        )
    return known


def reviewed(lines, index):
    return any(
        0 <= candidate < len(lines) and MARKER.search(lines[candidate])
        for candidate in (index, index - 1)
    )


def audit_rules(path, root, text, known):
    """Rules from the consistency audit; each honours a ui-ok marker."""
    problems = []
    lines = text.splitlines()
    native_menu_block = 0
    for index, line in enumerate(lines):
        stripped = line.split("//", 1)[0]
        location = f"{path.relative_to(root)}:{index + 1}"
        if re.match(r"\s*#\s*if.*Q_OS_MAC", line):
            native_menu_block += 1
        elif native_menu_block and re.match(r"\s*#\s*endif", line):
            native_menu_block -= 1
        if reviewed(lines, index):
            continue
        for match in CALL.finditer(stripped):
            name = match.group(1)
            if name == "pixmap" and not re.search(
                r"(?:\.|->)\s*pixmap\s*\($", stripped[: match.end()]
            ):
                continue
            args = arguments(stripped, match.end() - 1)
            positions = METRIC_ARGUMENTS[name] or range(len(args))
            if any(i < len(args) and LITERAL.match(args[i]) for i in positions):
                problems.append(f"{location}: literal presentation metric in {name}()")
        if RGB_COLOR.search(stripped):
            problems.append(f"{location}: presentation color")
        if RAW_MENU.search(stripped):
            problems.append(
                f"{location}: raw menu popup (use design::popupContextMenu/execContextMenu)"
            )
        if (
            STANDARD_ICON.search(stripped)
            and not native_menu_block
            and path.suffix != ".mm"
        ):
            problems.append(f"{location}: Qt standard icon (use design::Icon)")
        if STOCK_BUTTONS.search(stripped):
            problems.append(f"{location}: stock dialog buttons (use design::Button)")
        for match in PAINT_GEOMETRY.finditer(stripped):
            if any(
                LARGE_LITERAL.match(arg) for arg in arguments(stripped, match.end() - 1)
            ):
                problems.append(
                    f"{location}: literal paint geometry in {match.group(1)}() "
                    "(use spacing, dimension, or radius tokens)"
                )
        for match in STYLED_PROPERTY.finditer(line):
            name = match.group(1)
            opening = match.start() + match.group(0).index("(")
            values = ",".join(arguments(line, opening)[1:])
            for value in re.findall(r'"([^"]*)"', values):
                if value and value not in known.get(name, set()):
                    problems.append(
                        f"{location}: {name}={value!r} is not styled by the design system"
                    )
    return problems


def violations(root=ROOT):
    problems = []
    desktop = root / "desktop"
    allowed = desktop / "design_system"
    preview = desktop / "tools/preview"
    known = styled_values(allowed) if allowed.is_dir() else {}
    for path in sorted(desktop.rglob("*")):
        if path.suffix not in CPP_SUFFIXES:
            continue
        text = path.read_text(encoding="utf-8")
        if allowed not in path.parents and preview not in path.parents:
            problems.extend(audit_rules(path, root, text, known))
        for line_number, line in enumerate(text.splitlines(), 1):
            if allowed in path.parents:
                if FEATURE_INCLUDE.search(line):
                    problems.append(
                        f"{path.relative_to(root)}:{line_number}: design-system dependency"
                    )
                continue
            # Developer specimens deliberately exercise fixed reference dimensions.
            if preview in path.parents:
                continue
            if COLOR.search(line):
                problems.append(
                    f"{path.relative_to(root)}:{line_number}: presentation color"
                )
            if STYLE.search(line):
                problems.append(
                    f"{path.relative_to(root)}:{line_number}: local stylesheet"
                )
            if METRIC.search(line):
                problems.append(
                    f"{path.relative_to(root)}:{line_number}: presentation metric"
                )
    qrc = root / "desktop/resources/resources.qrc"
    if not qrc.is_file():
        problems.append("desktop/resources/resources.qrc: missing resource manifest")
        return problems
    manifest = qrc.read_text(encoding="utf-8")
    icon_dir = root / "desktop/resources/icons"
    for name in sorted(REQUIRED_ICONS):
        if name not in manifest or not (icon_dir / name).is_file():
            problems.append(f"desktop/resources/icons/{name}: missing required icon")
    if not (icon_dir / "LICENSE-LUCIDE").is_file():
        problems.append("desktop/resources/icons/LICENSE-LUCIDE: missing attribution")
    if not (icon_dir / "SOURCE-LUCIDE.json").is_file():
        problems.append(
            "desktop/resources/icons/SOURCE-LUCIDE.json: missing provenance"
        )
    return problems


def main():
    problems = violations()
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
