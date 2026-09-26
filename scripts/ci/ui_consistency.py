#!/usr/bin/env python3
"""Audit desktop UI construction sites and local visual ownership."""

import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]

# Stock Qt controls styled by the shared control style or application palette.
# Their component families are documented in desktop/design_system/README.md.
STOCK_CONTROLS = {
    "QCheckBox",
    "QComboBox",
    "QDoubleSpinBox",
    "QFontComboBox",
    "QKeySequenceEdit",
    "QLabel",
    "QLineEdit",
    "QListWidget",
    "QMenu",
    "QPlainTextEdit",
    "QSpinBox",
    "QTableView",
    "QTextEdit",
    "QToolButton",
    "QTreeView",
    "QTreeWidget",
}
DESIGN_CONTROLS = {"Button", "Text", "NavigationTreeView", "Tooltip"}
DESIGN_COMPOSITES = {
    "ButtonGroup",
    "DialogSections",
    "FieldValidation",
    "NavigationProfileDelegate",
}
DESIGN_NON_VISUAL = {
    "PlatformAccessibilityMonitor",
    "PreviewWindow",
    "ThemeManager",
}
FEATURE_COMPOSITES = {"QuerySettingsDialog"}
STRUCTURAL_CONTROLS = {
    "QDialogButtonBox",
    "QDockWidget",
    "QFrame",
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
NEW_CONTROL = re.compile(r"\bnew\s+(?:(design)::)?([A-Z][A-Za-z0-9_]*)\s*(?=[({;])")
FONT_FAMILY = re.compile(
    r'\bQFont\s*(?:\(\s*|[A-Za-z_]\w*\s*\(\s*)(?:(?:QStringLiteral|QLatin1String)\s*\(\s*)?"'
)
FONT_SIZE = re.compile(
    r"\bset(?:Pixel|Point)Size(?:F)?\s*\(\s*(?:[0-9]+(?:\.[0-9]+)?\b|std::(?:max|min)\s*\(\s*[0-9]+(?:\.[0-9]+)?\b)"
)
FONT_FAMILY_SETTER = re.compile(
    r'\bsetFamily\s*\(\s*(?:(?:QStringLiteral|QLatin1String)\s*\(\s*)?"'
)
COMPONENT_FONT_SIZE = re.compile(r"\bset(?:Pixel|Point)Size(?:F)?\s*\(")
HEX_COLOR = re.compile(r"#[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?\b")
COLOR_VALUE = re.compile(r"\bQColor\s*\(\s*[0-9]+\s*,")
QT_COLOR = re.compile(
    r"\bQt::(?:black|white|red|darkRed|green|darkGreen|blue|darkBlue|cyan|darkCyan|magenta|darkMagenta|yellow|darkYellow|gray|darkGray|lightGray)\b"
)
QSS_COLOR = re.compile(
    r":\s*(?:rgb|rgba|hsl|hsla)\s*\(|"
    r":\s*(?:red|blue|green|yellow|cyan|magenta|black|white|gray|grey|orange|purple)\s*;"
)
MESSAGE_BOX = re.compile(r"\b(?:new\s+)?QMessageBox\s+[A-Za-z_]\w*\s*\(")


def screen_sources(desktop):
    app = desktop / "app"
    app_groups = {}
    if app.is_dir():
        for source in sorted((*app.glob("*.cpp"), *app.glob("*.h"))):
            name = source.stem
            if name.startswith("main_window"):
                name = "main_window"
            elif name.startswith("query_workspace"):
                name = "query_workspace"
            app_groups.setdefault(f"app/{name}", []).append(source)
    for name, sources in sorted(app_groups.items()):
        yield name, sources
    widgets = desktop / "widgets"
    if widgets.is_dir():
        for directory in sorted(widgets.iterdir()):
            if directory.is_dir():
                sources = sorted(directory.glob("*.cpp")) + sorted(
                    directory.glob("*.h")
                )
                if sources:
                    yield f"widgets/{directory.name}", sources


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
    for screen, sources in screen_sources(desktop):
        counts = {"explicit": 0, "stock": 0, "unclassified": 0, "composite": 0}
        for path in sources:
            for number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), 1
            ):
                location = f"{path.relative_to(root)}:{number}"
                for match in NEW_CONTROL.finditer(line):
                    name = match.group(2)
                    if match.group(1):
                        if name in DESIGN_CONTROLS:
                            counts["explicit"] += 1
                        elif name in DESIGN_COMPOSITES:
                            counts["composite"] += 1
                        elif name not in DESIGN_NON_VISUAL:
                            counts["unclassified"] += 1
                            problems.append(
                                f"{location}: unclassified design control {name}"
                            )
                    elif name in STOCK_CONTROLS:
                        counts["stock"] += 1
                    elif name in FEATURE_COMPOSITES:
                        counts["composite"] += 1
                    elif name not in STRUCTURAL_CONTROLS and is_visual_qt(name):
                        counts["unclassified"] += 1
                        problems.append(
                            f"{location}: unclassified visual control {name}"
                        )
        screens[screen] = counts

    # Check every production application / widget file, including controllers that
    # are not themselves visual composition roots.
    for area in (desktop / "app", desktop / "widgets"):
        if not area.is_dir():
            continue
        for path in sorted(area.rglob("*")):
            if path.suffix not in {".cpp", ".h"}:
                continue
            for number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), 1
            ):
                location = f"{path.relative_to(root)}:{number}"
                for pattern, label in (
                    (FONT_FAMILY, "local font family"),
                    (FONT_FAMILY_SETTER, "local font family"),
                    (FONT_SIZE, "local font size"),
                    (COLOR_VALUE, "local color value"),
                    (QT_COLOR, "local color value"),
                    (MESSAGE_BOX, "use shared confirmation dialog"),
                ):
                    if pattern.search(line):
                        problems.append(f"{location}: {label}")

    # Hex tokens belong to the colors foundation, including those embedded in QSS.
    for path in sorted(desktop.rglob("*")):
        if path.suffix not in {".cpp", ".h", ".qss"}:
            continue
        if (
            desktop / "design_system/colors" in path.parents
            or desktop / "design_system/metrics" in path.parents
            or desktop / "tools/preview" in path.parents
            or path == desktop / "design_system/icons.cpp"
        ):
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if (
                desktop / "design_system" in path.parents
                and desktop / "design_system/fonts" not in path.parents
                and COMPONENT_FONT_SIZE.search(line)
            ):
                problems.append(f"{path.relative_to(root)}:{number}: local font size")
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
