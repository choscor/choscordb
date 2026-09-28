#!/usr/bin/env python3
"""Audit desktop UI construction sites and local visual ownership."""

import argparse
from bisect import bisect_right
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
CPP_SUFFIXES = {".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".mm"}

# Stock Qt controls styled by the shared control style or application palette.
# Their component families are documented in desktop/design_system/README.md.
STOCK_CONTROLS = {
    "QCheckBox",
    "QComboBox",
    "QDoubleSpinBox",
    "QFontComboBox",
    "QGraphicsView",
    "QGroupBox",
    "QKeySequenceEdit",
    "QLabel",
    "QLineEdit",
    "QListWidget",
    "QMenu",
    "QPlainTextEdit",
    "QProgressBar",
    "QPushButton",
    "QRadioButton",
    "QSpinBox",
    "QTableView",
    "QTextEdit",
    "QToolButton",
    "QTreeView",
    "QTreeWidget",
}
DESIGN_CONTROLS = {"Button", "Text", "NavigationTreeView", "RightSheet", "Tooltip"}
DESIGN_COMPOSITES = {
    "ButtonGroup",
    "DialogSections",
    "FieldValidation",
    "ModalDialog",
    "NavigationProfileDelegate",
    "RecentHistoryRowDelegate",
    "TabAddCorner",
}
SHARED_DIALOGS = {"ConfirmationDialog", "DialogShell"}
DESIGN_NON_VISUAL = {
    "PlatformAccessibilityMonitor",
    "PreviewWindow",
    "ThemeManager",
}
STRUCTURAL_CONTROLS = {
    "QDialogButtonBox",
    "QDockWidget",
    "QFrame",
    "QMessageBox",
    "QScrollArea",
    "QSplitter",
    "QStackedWidget",
    "QTabBar",
    "QTabWidget",
    "QToolBar",
    "QWidget",
    "QWidgetAction",
    "QButtonGroup",
}
TYPE = r"(?:(?:[A-Za-z_]\w*)::)*[A-Z][A-Za-z0-9_]*"
NEW_CONTROL = re.compile(r"\bnew\s+(" + TYPE + r")\s*(?=[({;])")
SMART_CONTROL = re.compile(rf"\b(?:std::)?make_(?:unique|shared)\s*<\s*({TYPE})\s*>")
STACK_CONTROL = re.compile(
    r"(?<![\w:])(?:const\s+)?(" + TYPE + r")\s+[A-Za-z_]\w*\s*(?=[({])"
)
FEATURE_CLASS = re.compile(
    r"\b(?:class|struct)\s+([A-Za-z_]\w*)(?:\s+final)?\s*:\s*public\s+(" + TYPE + r")\b"
)
FONT_FAMILY = re.compile(
    r'\bQFont\s*(?:[({]\s*|[A-Za-z_]\w*\s*[({]\s*)(?:(?:QStringLiteral|QLatin1String)\s*\(\s*)?"'
)
FONT_SIZE = re.compile(
    r"\bset(?:Pixel|Point)Size(?:F)?\s*\(\s*(?:[0-9]+(?:\.[0-9]+)?\b|std::(?:max|min)\s*\(\s*[0-9]+(?:\.[0-9]+)?\b)"
)
FONT_FAMILY_SETTER = re.compile(
    r'\bsetFamily\s*\(\s*(?:(?:QStringLiteral|QLatin1String)\s*\(\s*)?"'
)
COMPONENT_FONT_SIZE = re.compile(r"\bset(?:Pixel|Point)Size(?:F)?\s*\(")
HEX_COLOR = re.compile(
    r"(?<![\w#])#(?:[0-9a-fA-F]{3,4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})\b"
)
COLOR_VALUE = re.compile(
    r"\bQColor\s*(?:[({]|[A-Za-z_]\w*\s*[({])\s*[0-9]+\s*,|"
    r"\bQColor::from(?:Rgb|Hsv|Cmyk)F?\s*\(\s*[0-9]+(?:\.[0-9]+)?\s*,|"
    r'\bQColor\s*(?:[({]|[A-Za-z_]\w*\s*[({])\s*"(?:red|blue|green|yellow|cyan|magenta|black|white|gray|grey|orange|purple)"'
)
QT_COLOR = re.compile(
    r"\bQt::(?:black|white|red|darkRed|green|darkGreen|blue|darkBlue|cyan|darkCyan|magenta|darkMagenta|yellow|darkYellow|gray|darkGray|lightGray)\b"
)
QSS_COLOR = re.compile(
    r":\s*(?:rgb|rgba|hsl|hsla)\s*\(|"
    r":\s*(?:red|blue|green|yellow|cyan|magenta|black|white|gray|grey|orange|purple)\s*;"
)
MESSAGE_BOX = re.compile(
    r"\bnew\s+QMessageBox\s*[({]|\bQMessageBox\s+[A-Za-z_]\w*\s*[({]|"
    r"\bQMessageBox::(?:information|warning|critical|question|about|aboutQt)\s*\("
)


def screen_sources(desktop):
    """Group every production C++ source, including loose and nested widgets."""
    groups = {}
    for area in ("app", "widgets"):
        directory = desktop / area
        if not directory.is_dir():
            continue
        for source in sorted(directory.rglob("*")):
            if not source.is_file() or source.suffix not in CPP_SUFFIXES:
                continue
            relative = source.relative_to(directory)
            name = relative.parts[0] if len(relative.parts) > 1 else source.stem
            if area == "app" and name.startswith("main_window"):
                name = "main_window"
            elif area == "app" and name.startswith("query_workspace"):
                name = "query_workspace"
            groups.setdefault(f"{area}/{name}", []).append(source)
    for name, sources in sorted(groups.items()):
        yield name, sources


def cpp_without_comments(source, *, mask_literals=False):
    """Preserve offsets and newlines while hiding comments (and optionally literals)."""
    output = list(source)
    index = 0
    while index < len(source):
        if source.startswith('R"', index):
            opening = source.find("(", index + 2)
            delimiter = source[index + 2 : opening] if opening >= 0 else ""
            closing = source.find(")" + delimiter + '"', opening + 1)
            end = len(source) if closing < 0 else closing + len(delimiter) + 2
            if not mask_literals:
                index = end
                continue
        elif source.startswith("//", index):
            end = source.find("\n", index)
            end = len(source) if end < 0 else end
        elif source.startswith("/*", index):
            closing = source.find("*/", index + 2)
            end = len(source) if closing < 0 else closing + 2
        elif source[index] in {'"', "'"}:
            quote = source[index]
            end = index + 1
            while end < len(source):
                if source[end] == "\\":
                    end += 2
                elif source[end] == quote:
                    end += 1
                    break
                else:
                    end += 1
            if not mask_literals:
                index = end
                continue
        else:
            index += 1
            continue
        for offset in range(index, min(end, len(source))):
            if output[offset] != "\n":
                output[offset] = " "
        index = end
    return "".join(output)


def feature_composites(sources):
    """Find feature UI classes by their QWidget inheritance, including local bases."""
    bases = {}
    for path in sources:
        code = cpp_without_comments(
            path.read_text(encoding="utf-8"), mask_literals=True
        )
        bases.update(
            (match.group(1), match.group(2).split("::")[-1])
            for match in FEATURE_CLASS.finditer(code)
        )
    composites = set()
    while True:
        found = {
            name
            for name, base in bases.items()
            if base in composites
            or base in {"DialogShell", "QsciScintilla"}
            or is_visual_qt(base)
        }
        if found == composites:
            return composites
        composites = found


def control_kind(qualified_name, feature_classes):
    parts = qualified_name.split("::")
    name = parts[-1]
    if "design" in parts[:-1]:
        if name in DESIGN_CONTROLS:
            return "explicit", None
        if name in DESIGN_COMPOSITES:
            return "composite", None
        if name in DESIGN_NON_VISUAL:
            return None, None
        return "unclassified", f"unclassified design control {name}"
    if name in STOCK_CONTROLS:
        return "stock", None
    if name in SHARED_DIALOGS:
        return "composite", None
    if name in feature_classes:
        return "composite", None
    if name not in STRUCTURAL_CONTROLS and is_visual_qt(name):
        return "unclassified", f"unclassified visual control {name}"
    return None, None


def is_visual_qt(name):
    return (
        name.startswith("Q")
        and not name.startswith("Qsci")
        and name.endswith(
            (
                "Widget",
                "View",
                "Box",
                "Bar",
                "Edit",
                "Label",
                "Button",
                "Area",
                "Menu",
                "Frame",
                "Splitter",
                "Slider",
                "Dial",
                "ProgressBar",
                "RadioButton",
                "GroupBox",
                "Dialog",
                "TextBrowser",
            )
        )
    )


def audit(root=ROOT):
    desktop = root / "desktop"
    screens = {}
    problems = []
    groups = dict(screen_sources(desktop))
    feature_sources = sorted({path for sources in groups.values() for path in sources})
    feature_classes = feature_composites(feature_sources)
    for screen, sources in groups.items():
        counts = {"explicit": 0, "stock": 0, "unclassified": 0, "composite": 0}
        for path in sources:
            source = cpp_without_comments(
                path.read_text(encoding="utf-8"), mask_literals=True
            )
            starts = [0] + [match.end() for match in re.finditer("\n", source)]
            for pattern in (NEW_CONTROL, SMART_CONTROL, STACK_CONTROL):
                for match in pattern.finditer(source):
                    if pattern is STACK_CONTROL:
                        arguments = source[match.end() :].lstrip()
                        if re.match(
                            r"\(\s*\)|\(\s*(?:const\s+)?" + TYPE + r"\s*[*&]", arguments
                        ):
                            continue
                    kind, problem = control_kind(match.group(1), feature_classes)
                    if kind is None:
                        continue
                    counts[kind] += 1
                    if problem:
                        number = bisect_right(starts, match.start())
                        problems.append(f"{path.relative_to(root)}:{number}: {problem}")
        screens[screen] = counts

    # Check every production application / widget file, including controllers that
    # are not themselves visual composition roots.
    for path in feature_sources:
        source = cpp_without_comments(path.read_text(encoding="utf-8"))
        code = cpp_without_comments(source, mask_literals=True)
        starts = [0] + [match.end() for match in re.finditer("\n", source)]
        for pattern, label in (
            (FONT_FAMILY, "local font family"),
            (FONT_FAMILY_SETTER, "local font family"),
            (FONT_SIZE, "local font size"),
            (COLOR_VALUE, "local color value"),
            (QT_COLOR, "local color value"),
            (MESSAGE_BOX, "use shared confirmation dialog"),
        ):
            for match in pattern.finditer(source):
                if code[match.start()].isspace():
                    continue
                number = bisect_right(starts, match.start())
                problems.append(f"{path.relative_to(root)}:{number}: {label}")

    # Hex tokens belong to the colors foundation, including those embedded in QSS.
    for path in sorted(desktop.rglob("*")):
        if not path.is_file() or path.suffix not in CPP_SUFFIXES | {".qss"}:
            continue
        if (
            desktop / "design_system/colors" in path.parents
            or desktop / "design_system/metrics" in path.parents
            or desktop / "tools/preview" in path.parents
            or path == desktop / "design_system/icons.cpp"
        ):
            continue
        content = path.read_text(encoding="utf-8")
        if path.suffix in CPP_SUFFIXES:
            content = cpp_without_comments(content)
            if desktop / "design_system" in path.parents:
                code = cpp_without_comments(content, mask_literals=True)
                starts = [0] + [match.end() for match in re.finditer("\n", content)]
                checks = [
                    (COLOR_VALUE, "local color value"),
                    (QT_COLOR, "local color value"),
                ]
                if desktop / "design_system/fonts" not in path.parents:
                    checks.extend(
                        (
                            (FONT_FAMILY, "local font family"),
                            (FONT_FAMILY_SETTER, "local font family"),
                            (COMPONENT_FONT_SIZE, "local font size"),
                        )
                    )
                for pattern, label in checks:
                    for match in pattern.finditer(content):
                        if not code[match.start()].isspace():
                            number = bisect_right(starts, match.start())
                            problems.append(
                                f"{path.relative_to(root)}:{number}: {label}"
                            )
        for number, line in enumerate(content.splitlines(), 1):
            if HEX_COLOR.search(line):
                problems.append(f"{path.relative_to(root)}:{number}: local hex color")
            if path.suffix == ".qss" and QSS_COLOR.search(line):
                problems.append(f"{path.relative_to(root)}:{number}: local QSS color")
    for area in (desktop / "app", desktop / "widgets"):
        if area.is_dir():
            for path in sorted(area.rglob("*.qss")):
                problems.append(f"{path.relative_to(root)}: feature-owned QSS")

    totals = {
        key: sum(row[key] for row in screens.values())
        for key in ("explicit", "stock", "unclassified")
    }
    denominator = sum(totals.values())
    zero_site_groups = [
        name
        for name, counts in screens.items()
        if not any(counts[key] for key in ("explicit", "stock", "unclassified"))
    ]
    return {
        "screen_count": len(screens) - len(zero_site_groups),
        "source_group_count": len(screens),
        "zero_site_groups": zero_site_groups,
        "construction_sites": denominator,
        "component_coverage_percent": round(
            100 * (totals["explicit"] + totals["stock"]) / denominator, 2
        )
        if denominator
        else 0.0,
        "totals": totals,
        "screens": screens,
        "violations": sorted(problems),
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--json", action="store_true", help="write the full report as JSON"
    )
    args = parser.parse_args(argv)
    report = audit()
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(
            f"UI construction-site coverage: {report['component_coverage_percent']}% "
            f"({report['totals']['explicit']} design + {report['totals']['stock']} centrally styled stock "
            f"/ {report['construction_sites']} leaf sites; {report['screen_count']} measured groups, "
            f"{report['source_group_count']} source groups)"
        )
        for screen, counts in report["screens"].items():
            total = counts["explicit"] + counts["stock"] + counts["unclassified"]
            coverage = (
                f"{100 * (counts['explicit'] + counts['stock']) / total:.1f}%"
                if total
                else "N/A"
            )
            print(
                f"  {screen}: {coverage} ({counts['explicit']} design, "
                f"{counts['stock']} styled stock, {counts['unclassified']} unclassified; "
                f"{counts['composite']} composites)"
            )
        for problem in report["violations"]:
            print(problem, file=sys.stderr)
    return 1 if report["violations"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
