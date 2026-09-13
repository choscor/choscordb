#!/usr/bin/env python3
"""Reject local desktop visual constants/stylesheets and missing icon resources."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
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


def violations(root=ROOT):
    problems = []
    desktop = root / "desktop"
    allowed = desktop / "design_system"
    preview = desktop / "tools/preview"
    for path in sorted(desktop.rglob("*")):
        if path.suffix not in {".cpp", ".h"}:
            continue
        text = path.read_text(encoding="utf-8")
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
