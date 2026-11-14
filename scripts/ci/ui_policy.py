#!/usr/bin/env python3

import hashlib
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ElementTree

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
PREVIEW_BRANCH = re.compile(
    r"^\s*#\s*(?:ifdef\s+|(?:el)?if\s+(?:defined\s*\(?\s*)?)CHOSCORDB_DEVELOPMENT_PREVIEW\b"
)
CONDITIONAL_OPEN = re.compile(r"^\s*#\s*if")
CONDITIONAL_BRANCH = re.compile(r"^\s*#\s*(?:else|elif)\b")
CONDITIONAL_CLOSE = re.compile(r"^\s*#\s*endif\b")
PREVIEW_HEADER = re.compile(r'^\s*#\s*include\s*"tools/')
INCLUDE = re.compile(r"^\s*#\s*include\b")
REQUIRED_ICONS = {"app-mark.svg", "mysql.svg", "play.svg", "stop.svg", "plus.svg"}
ICON_RECORDS = {
    "LICENSE-PHOSPHOR",
    "LICENSE-DEVICON",
    "SOURCE-PHOSPHOR.json",
    "SOURCE-DATABASE-LOGOS.json",
}
MARKER = re.compile(r"//\s*ui-ok:\s*\S")
LITERAL = re.compile(r"^\s*[1-9][0-9]*\s*$")
METRIC_ARGUMENTS = {
    "setContentsMargins": None,
    "QMargins": None,
    "QSize": None,
    "addSpacing": (0,),
    "setMargin": (0,),
    "setIndentation": (0,),
    "setDefaultSectionSize": (0,),
    "setMinimumSectionSize": (0,),
    "setColumnWidth": (1,),
    "resizeSection": (1,),
}
CALL = re.compile(r"\b(" + "|".join(METRIC_ARGUMENTS) + r")\s*\(")
ICON_SIZE_ARGUMENTS = {"themedIcon": (2,), "setIconSize": (0,), "pixmap": (0, 1)}
ICON_SIZE_CALL = re.compile(r"\b(themedIcon|setIconSize|pixmap)\s*\(")
QSIZE = re.compile(r"^\s*QSize\s*\(")
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
AD_HOC_PANEL_STATE = re.compile(
    r"(?:\b(?:setText|QLabel|Text)\s*\(\s*tr\(\s*\"(?:No |Nothing |Could not load)"
    r"|setProperty\s*\(\s*\"state\"\s*,\s*\"(?:error|empty|noMatch|failed)\")"
)
PAINT_GEOMETRY = re.compile(r"(?:\.|->)\s*(adjusted|drawRoundedRect)\s*\(")
LARGE_LITERAL = re.compile(r"^\s*-?\s*(?:[2-9]|[1-9][0-9]+)(?:\.0*)?\s*$")
LITERAL_DURATION = re.compile(r"\bsetDuration\s*\(\s*\d+\s*\)")
PLACEHOLDER_CALL = re.compile(r"\bsetPlaceholderText\s*\(")
PLACEHOLDER_TEXT = re.compile(r'"((?:[^"\\]|\\.)*)"')
INSTRUCTION_VERB = re.compile(
    r"(?:Choose|Enter|Type|Select|Pick|Click|Provide|Specify|Paste|Drop|Use|Leave)\b"
)
SEARCH_VERB = re.compile(r"(?:Filter|Find|Search|Replace)\b")
TRANSLATED_LABEL = re.compile(r'\btr\(\s*"((?:[^"\\]|\\.)*)"')
PROPER_LABEL_PHRASES = ("JSON Lines",)
MNEMONIC_LABEL = re.compile(r'\btr\(\s*"[^"]*&[A-Za-z]')
EM_DASH_SEPARATOR = re.compile(r'"[^"]* — [^"]*"|" — "|"\\u2014"')
GENERIC_SUCCESS_TITLE = re.compile(
    r'Toast\w*\(.*\b(?:tr|translate)\(\s*(?:"[^"]*"\s*,\s*)?"Success"\s*\)'
)
TOOLTIP_TEXT = re.compile(r'setToolTip\(\s*tr\(\s*"((?:[^"\\]|\\.)*)"\s*\)\s*\)')
FAILED_SENTENCE = re.compile(
    r'\btr\(\s*"[^"]*(?:\b[Ff]ailed[.:]\s|\bUnable to\b|(?<=")Failed to\b)'
)
TEMPORARY_VARIANT_MEMBER = re.compile(
    r"\b(?:\w*[Dd]ata|property)\([^;]*?\)\.value<\w+>\(\)\.\w"
)
FORBIDDEN_TERM = re.compile(
    r'\btr\(\s*"[^"]*(?:\b(?:[Ss]essions?|[Ss]ettings)\b|(?<![Cc]onnection )\b[Pp]rofiles?\b)'
)
RUST_UI_TEXT = re.compile(r'"([A-Z][a-z](?:[^"\\]|\\.)*)"')
RUST_ERROR_FORM = re.compile(r"\b[Ff]ailed[.:;]\s|\bUnable to\b|^Failed to\b")
RUST_FORBIDDEN_TERM = re.compile(
    r"\b(?:[Ss]essions?|[Ss]ettings)\b|(?<![Cc]onnection )\b[Pp]rofiles?\b"
)
LITERAL_TIMER = re.compile(
    r"\b(?:setInterval|start)\s*\(\s*[1-9][0-9]*\s*\)"
    r"|\bsingleShot\s*\(\s*[1-9][0-9]*\s*,"
    r"|\bsleep_for\s*\(\s*[1-9][0-9]*(?:ms|s)?\s*\)"
    r"|\bsleep_for\s*\(\s*(?:std::chrono::)?(?:milli)?seconds\s*\(\s*[1-9]"
)
SCREEN_DIRECTORIES = ("app", "widgets", "models")
MENU_BAR_TITLE = re.compile(r"\bmenuBar\(\)\s*->\s*addMenu\s*\(")


def title_case_label(text):
    if re.search(r"[.!?:·%/]|\\n", text):
        return False
    for phrase in PROPER_LABEL_PHRASES:
        text = text.replace(phrase, "")
    words = [word.strip("“”\"'(),…&") for word in text.split()]
    return any(re.fullmatch(r"[A-Z][a-z]+", word) for word in words[1:])


LOOSE_DIALOG_DECISION = re.compile(
    r"ConfirmationDialog::question\s*\(|QMessageBox::(?:Yes|No|Discard|DestructiveRole)\b"
    r"|\bfooterLayout\s*\("
)
QSS_SELECTOR = re.compile(r"\[\s*(\w+)\s*=\s*\"?([\w-]+)\"?\s*\]")


def arguments(text, start):
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


def placeholder_rules(path, root, text):
    problems = []
    for match in PLACEHOLDER_CALL.finditer(text):
        line_number = text.count("\n", 0, match.start()) + 1
        for placeholder in PLACEHOLDER_TEXT.findall(
            "".join(arguments(text, match.end() - 1))
        ):
            if INSTRUCTION_VERB.match(placeholder):
                problems.append(
                    f"{path.relative_to(root)}:{line_number}: instruction placeholder "
                    "(a placeholder shows only an example value)"
                )
            elif SEARCH_VERB.match(placeholder) and not placeholder.endswith("…"):
                problems.append(
                    f"{path.relative_to(root)}:{line_number}: search placeholder without a "
                    "final ellipsis"
                )
    return problems


DESIGN_VALUE_HEADERS = ("metrics/metrics.h", "fonts/fonts.h")
DESIGN_VALUE_DEFINITIONS = {
    "metrics.h",
    "metrics.cpp",
    "fonts.h",
    "fonts.cpp",
    "tokens.cpp",
}
ENUM_DEFINITION = re.compile(r"enum class (\w+)\s*\{([^}]*)\}")


def unused_design_values(root):
    design = root / "desktop/design_system"
    sources = [
        path.read_text(encoding="utf-8")
        for path in sorted((root / "desktop").rglob("*"))
        if path.suffix in CPP_SUFFIXES and path.name not in DESIGN_VALUE_DEFINITIONS
    ]
    problems = []
    for header in DESIGN_VALUE_HEADERS:
        path = design / header
        if not path.is_file():
            continue
        unused = []
        for name, body in ENUM_DEFINITION.findall(path.read_text(encoding="utf-8")):
            for value in (part.split("=")[0].strip() for part in body.split(",")):
                use = re.compile(rf"\b{name}::{value}\b")
                if value and not any(use.search(source) for source in sources):
                    unused.append(f"{name}::{value}")
        problems.extend(
            f"{path.relative_to(root)}: {value} has no caller"
            for value in sorted(unused)
        )
    return problems


def styled_values(design):
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


GALLERY_SIZE_CALL = re.compile(
    r"\b(set(?:Fixed|Minimum|Maximum)(?:Size|Width|Height)|resize|resizeSection|setColumnWidth)"
    r"\s*\("
)
GALLERY_STOCK_BUTTON = re.compile(r"\bnew\s+QPushButton\s*\(")


def gallery_rules(path, root, text):
    problems = []
    lines = text.splitlines()
    for index, line in enumerate(lines):
        stripped = line.split("//", 1)[0]
        location = f"{path.relative_to(root)}:{index + 1}"
        if reviewed(lines, index):
            continue
        if GALLERY_STOCK_BUTTON.search(stripped):
            problems.append(
                f"{location}: stock button in the gallery (use design::Button)"
            )
        for match in GALLERY_SIZE_CALL.finditer(stripped):
            args = arguments(stripped, match.end() - 1)
            if any(LITERAL.match(arg) for arg in args):
                problems.append(
                    f"{location}: literal presentation metric in {match.group(1)}()"
                )
    return problems


def audit_rules(path, root, text, known):
    problems = []
    lines = text.splitlines()
    for index, line in enumerate(lines):
        stripped = line.split("//", 1)[0]
        location = f"{path.relative_to(root)}:{index + 1}"
        if reviewed(lines, index):
            continue
        for match in CALL.finditer(stripped):
            name = match.group(1)
            if name == "QSize" and re.search(
                r"setIconSize\s*\(\s*$", stripped[: match.start()]
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
        if STANDARD_ICON.search(stripped):
            problems.append(f"{location}: Qt standard icon (use design::Icon)")
        if STOCK_BUTTONS.search(stripped):
            problems.append(f"{location}: stock dialog buttons (use design::Button)")
        if AD_HOC_PANEL_STATE.search(stripped):
            problems.append(
                f"{location}: ad-hoc panel state label "
                "(use design::createPanelStateText and showPanelState)"
            )
            continue
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


def icon_size_rules(path, root, text):
    problems = []
    code = "\n".join(line.split("//", 1)[0] for line in text.splitlines())
    for match in ICON_SIZE_CALL.finditer(code):
        name = match.group(1)
        if name == "pixmap" and not re.search(
            r"(?:\.|->)\s*pixmap\s*\($", code[: match.end()]
        ):
            continue
        args = arguments(code, match.end() - 1)
        sizes = []
        for position in ICON_SIZE_ARGUMENTS[name]:
            if position < len(args):
                argument = args[position]
                if QSIZE.match(argument):
                    sizes += arguments(argument, argument.index("("))
                else:
                    sizes.append(argument)
        if any(LITERAL.match(size) for size in sizes):
            line = code.count("\n", 0, match.start()) + 1
            problems.append(
                f"{path.relative_to(root)}:{line}: literal icon size in "
                f"{name}() (use Dimension::IconSmall or Dimension::Icon)"
            )
    return problems


def svg_root(path):
    try:
        return ElementTree.parse(path).getroot()
    except ElementTree.ParseError:
        return None


def icon_asset_rules(root, manifest):
    problems = []
    icon_dir = root / "desktop/resources/icons"
    relative = "desktop/resources/icons/"
    try:
        phosphor = json.loads(
            (icon_dir / "SOURCE-PHOSPHOR.json").read_text(encoding="utf-8")
        )
        logos = json.loads(
            (icon_dir / "SOURCE-DATABASE-LOGOS.json").read_text(encoding="utf-8")
        )
        brands = {
            name: record["sha256"]
            for name, record in {
                **phosphor.get("adaptations", {}),
                **logos["icons"],
            }.items()
        }
        records = {**phosphor["icons"], **brands}
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return [f"{relative}SOURCE-*.json: missing or invalid icon provenance"]
    for path in sorted(icon_dir.iterdir()):
        name = path.name
        if name.startswith("LICENSE-") or name.startswith("SOURCE-"):
            if name not in ICON_RECORDS:
                problems.append(f"{relative}{name}: unknown icon license or provenance")
            continue
        if path.suffix != ".svg":
            problems.append(f"{relative}{name}: icon assets must be SVG")
            continue
        if name not in records:
            problems.append(f"{relative}{name}: not recorded in SOURCE-PHOSPHOR.json")
            continue
        if hashlib.sha256(path.read_bytes()).hexdigest() != records[name]:
            source = (
                "SOURCE-PHOSPHOR.json" if name in phosphor["icons"] else "provenance"
            )
            problems.append(f"{relative}{name}: differs from its {source} record")
        if name in brands:
            svg = svg_root(path)
            box = (svg.get("viewBox", "") if svg is not None else "").split()
            if len(box) != 4 or box[2] != box[3]:
                problems.append(f"{relative}{name}: brand logo needs a square viewBox")
            if svg is not None and ("width" in svg.attrib or "height" in svg.attrib):
                problems.append(
                    f"{relative}{name}: brand logo must not set width or height"
                )
    listed = set(re.findall(r">\s*icons/([^<\s]+)\s*<", manifest))
    for name in sorted(listed - set(records)):
        problems.append(f"{relative}{name}: qrc icon has no provenance record")
    for name in sorted(records):
        if not (icon_dir / name).is_file():
            problems.append(f"{relative}{name}: provenance record has no file")
    return problems


def rust_text_rules(root):
    problems = []
    for path in sorted((root / "crates").glob("*/src/**/*.rs")):
        lines = path.read_text(encoding="utf-8").splitlines()
        for line_number, line in enumerate(lines, 1):
            for text in RUST_UI_TEXT.findall(line):
                if RUST_ERROR_FORM.search(text):
                    problems.append(
                        f"{path.relative_to(root)}:{line_number}: error sentence form "
                        "(use Could not …)"
                    )
                if RUST_FORBIDDEN_TERM.search(text):
                    problems.append(
                        f"{path.relative_to(root)}:{line_number}: term not in the term list "
                        "(use connection or preferences)"
                    )
    return problems


def preview_block_rules(path, root, lines):
    problems = []
    depth = 0
    preview_depth = None
    for line_number, line in enumerate(lines, 1):
        if CONDITIONAL_OPEN.search(line) and not CONDITIONAL_BRANCH.search(line):
            depth += 1
            if preview_depth is None and PREVIEW_BRANCH.search(line):
                preview_depth = depth
        elif CONDITIONAL_BRANCH.search(line):
            if preview_depth == depth:
                preview_depth = None
            elif preview_depth is None and PREVIEW_BRANCH.search(line):
                preview_depth = depth
        elif CONDITIONAL_CLOSE.search(line):
            if preview_depth == depth:
                preview_depth = None
            depth -= 1
        elif (
            preview_depth is not None
            and INCLUDE.search(line)
            and not PREVIEW_HEADER.search(line)
        ):
            problems.append(
                f"{path.relative_to(root)}:{line_number}: release header inside the preview "
                "block (release builds leave out CHOSCORDB_DEVELOPMENT_PREVIEW)"
            )
    return problems


def violations(root=ROOT):
    problems = []
    desktop = root / "desktop"
    allowed = desktop / "design_system"
    preview = desktop / "tools/preview"
    metrics = allowed / "metrics"
    known = styled_values(allowed) if allowed.is_dir() else {}
    for path in sorted(desktop.rglob("*")):
        if path.suffix not in CPP_SUFFIXES:
            continue
        text = path.read_text(encoding="utf-8")
        if metrics not in path.parents:
            problems.extend(icon_size_rules(path, root, text))
        if allowed not in path.parents and preview not in path.parents:
            problems.extend(audit_rules(path, root, text, known))
        if preview in path.parents:
            problems.extend(gallery_rules(path, root, text))
        problems.extend(placeholder_rules(path, root, text))
        lines = text.splitlines()
        problems.extend(preview_block_rules(path, root, lines))
        screen = any(desktop / name in path.parents for name in SCREEN_DIRECTORIES)
        for line_number, line in enumerate(lines, 1):
            if (
                screen
                and LITERAL_TIMER.search(line)
                and not reviewed(lines, line_number - 1)
            ):
                problems.append(
                    f"{path.relative_to(root)}:{line_number}: literal timer interval "
                    "(read the interval from EngineAdapter)"
                )
            if LITERAL_DURATION.search(line):
                problems.append(
                    f"{path.relative_to(root)}:{line_number}: literal animation duration "
                    "(use motionSpec)"
                )
            if any(title_case_label(label) for label in TRANSLATED_LABEL.findall(line)):
                problems.append(
                    f"{path.relative_to(root)}:{line_number}: title case label (use sentence case)"
                )
            if MNEMONIC_LABEL.search(line) and not MENU_BAR_TITLE.search(line):
                problems.append(
                    f"{path.relative_to(root)}:{line_number}: mnemonic outside a menu bar title "
                    "(dialog and toolbar labels have no mnemonic)"
                )
            if FORBIDDEN_TERM.search(line):
                problems.append(
                    f"{path.relative_to(root)}:{line_number}: term not in the term list "
                    "(use connection or preferences)"
                )
            if any(
                ". " in tip and not tip.endswith(".")
                for tip in TOOLTIP_TEXT.findall(line)
            ):
                problems.append(
                    f"{path.relative_to(root)}:{line_number}: tooltip sentence without a final "
                    "period"
                )
            if TEMPORARY_VARIANT_MEMBER.search(line):
                problems.append(
                    f"{path.relative_to(root)}:{line_number}: member of a value<T>() from a "
                    "temporary QVariant (bind the QVariant first)"
                )
            if FAILED_SENTENCE.search(line):
                problems.append(
                    f"{path.relative_to(root)}:{line_number}: error sentence form "
                    "(use Could not …)"
                )
            if GENERIC_SUCCESS_TITLE.search(line):
                problems.append(
                    f"{path.relative_to(root)}:{line_number}: generic toast title (name the result)"
                )
            if EM_DASH_SEPARATOR.search(line) and preview not in path.parents:
                problems.append(
                    f'{path.relative_to(root)}:{line_number}: em dash separator (use " · " or ": ")'
                )
            if LOOSE_DIALOG_DECISION.search(line):
                problems.append(
                    f"{path.relative_to(root)}:{line_number}: dialog decision outside the dialog "
                    "contract (use ConfirmationDialog::confirm, addActionButton or "
                    "DialogSections::addAction)"
                )
            if allowed in path.parents:
                if FEATURE_INCLUDE.search(line):
                    problems.append(
                        f"{path.relative_to(root)}:{line_number}: design-system dependency"
                    )
                continue
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
    problems.extend(unused_design_values(root))
    problems.extend(rust_text_rules(root))
    qrc = root / "desktop/resources/resources.qrc"
    if not qrc.is_file():
        problems.append("desktop/resources/resources.qrc: missing resource manifest")
        return problems
    manifest = qrc.read_text(encoding="utf-8")
    icon_dir = root / "desktop/resources/icons"
    for name in sorted(REQUIRED_ICONS):
        if name not in manifest or not (icon_dir / name).is_file():
            problems.append(f"desktop/resources/icons/{name}: missing required icon")
    if not (icon_dir / "LICENSE-PHOSPHOR").is_file():
        problems.append("desktop/resources/icons/LICENSE-PHOSPHOR: missing attribution")
    if not (icon_dir / "SOURCE-PHOSPHOR.json").is_file():
        problems.append(
            "desktop/resources/icons/SOURCE-PHOSPHOR.json: missing provenance"
        )
        return problems
    problems.extend(icon_asset_rules(root, manifest))
    return problems


def main():
    problems = violations()
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
