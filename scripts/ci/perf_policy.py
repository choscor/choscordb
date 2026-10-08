#!/usr/bin/env python3
"""Reject desktop patterns that scale with result, navigator, or selection size.

Each rule targets a regression found in large result pages, wide selections,
deep navigator trees, or long pin lists. A line may opt out with a trailing or
preceding ``// perf-ok: <reason>`` comment; the reason must be non-empty so the
exception is reviewable.
"""

from pathlib import Path
import argparse
import json
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
CPP_SUFFIXES = {".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".mm"}
SCANNED = ("desktop/app", "desktop/models", "desktop/widgets", "desktop/design_system")
SCREEN = ("desktop/app", "desktop/models", "desktop/widgets")
MARKER = re.compile(r"//\s*perf-ok:\s*(\S.*)?$")

# Functions that run per paint, per size hint, or per model role lookup.
HOT_FUNCTIONS = re.compile(
    r"^(?:paint|paintEvent|paintSection|drawRow|drawBranches|sizeHint|"
    r"sectionSizeFromContents|initStyleOption|data|headerData|flags|"
    r"highlightBlock|filterAcceptsRow|parent|indexFor)$"
)
FUNCTION = re.compile(
    r"(?:^|[\s:*&>])(?P<name>~?[A-Za-z_]\w*)\s*\((?P<args>[^;{}]*)\)\s*"
    r"(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:final\s*)?"
    r"(?:->\s*[\w:<>,\s*&]+)?\{"
)
CONTROL = {"if", "for", "while", "switch", "catch", "return", "sizeof", "connect"}

LINE_RULES = (
    (
        "content-sized columns or rows",
        re.compile(
            r"\bresize(?:Columns|Rows)ToContents\s*\(|"
            r"\bresize(?:Column|Row)ToContents\s*\(|QHeaderView::ResizeToContents"
        ),
        SCREEN,
        "measure a bounded sample or use fixed sections; content sizing visits "
        "every row of large result pages",
    ),
    (
        "materialized selection",
        re.compile(r"\bselectedIndexes\s*\("),
        SCREEN,
        "walk QItemSelection ranges or use hasSelection()/currentIndex(); "
        "selectedIndexes() allocates one index per selected cell",
    ),
    (
        "undebounced view filter",
        re.compile(
            r"textChanged[^;]*setFilter(?:FixedString|RegularExpression|Wildcard)"
        ),
        SCANNED,
        "apply proxy filters from a debounced member QTimer",
    ),
    (
        "non-uniform tree rows",
        re.compile(r"\bsetUniformRowHeights\s*\(\s*false\s*\)"),
        SCANNED,
        "navigation rows have a fixed height; keep uniform row heights",
    ),
    (
        "whole-tree expansion",
        re.compile(r"\bexpandAll\s*\(\s*\)"),
        SCREEN,
        "expand known ancestors only; expandAll() lays out every loaded node",
    ),
    (
        "full-viewport hover repaint",
        re.compile(r"\bviewport\s*\(\s*\)\s*->\s*update\s*\(\s*\)"),
        (
            "desktop/design_system/tree",
            "desktop/design_system/table",
            "desktop/design_system/item_view",
            "desktop/app",
            "desktop/widgets",
        ),
        "update the changed visualRect() instead of the whole viewport",
    ),
)

# Statement rules: evaluated on the balanced text of each connect(...) call.
CONNECT_SIGNALS = re.compile(
    r"&\w+::(?:rowsInserted|rowsRemoved|dataChanged|layoutChanged|modelReset|"
    r"completionChanged|textChanged|textEdited|selectionChanged)\b"
)
ZERO_SHOT = re.compile(r"\bQTimer::singleShot\s*\(\s*0\s*,")

# Expressions that must stay out of HOT_FUNCTIONS.
HOT_RULES = (
    (re.compile(r"\bQSvgRenderer\b"), "SVG parsing on a paint/data path"),
    (re.compile(r"\bQTextDocument\b"), "text document layout on a paint/data path"),
    (
        re.compile(r"\bQRegularExpression\s*[({]"),
        "regex compilation on a paint/data path",
    ),
    (re.compile(r"\bEngineAdapter::\w+\s*\("), "bridge call on a paint/data path"),
    (re.compile(r"\bbeginResetModel\s*\("), "model reset on a paint/data path"),
    (
        re.compile(
            r"\b(?:QIcon|QPixmap|QImage)\s*(?:\w+\s*)?[({]\s*(?:QStringLiteral\s*\()?\""
        ),
        "image file load on a paint/data path (use cached design::themedIcon)",
    ),
    (
        re.compile(r"\bstd::find_if\b|\.indexOf\s*\("),
        "linear search on a paint/data path",
    ),
)


def strip_comments_and_strings(text):
    """Blank comments and string literals while keeping offsets and newlines."""
    out = []
    index, length = 0, len(text)
    while index < length:
        char = text[index]
        pair = text[index : index + 2]
        if pair == "//":
            end = text.find("\n", index)
            end = length if end == -1 else end
            out.append(" " * (end - index))
            index = end
        elif pair == "/*":
            end = text.find("*/", index + 2)
            end = length if end == -1 else end + 2
            out.append(re.sub(r"[^\n]", " ", text[index:end]))
            index = end
        elif char == '"' or (char == "'" and not text[index - 1 : index].isalnum()):
            quote, end = char, index + 1
            while end < length and text[end] != quote and text[end] != "\n":
                end += 2 if text[end] == "\\" else 1
            end = min(end + 1, length)
            out.append(quote + " " * max(0, end - index - 2) + quote)
            index = end
        else:
            out.append(char)
            index += 1
    return "".join(out)


def balanced(text, start, open_char="(", close_char=")"):
    depth = 0
    for index in range(start, len(text)):
        if text[index] == open_char:
            depth += 1
        elif text[index] == close_char:
            depth -= 1
            if depth == 0:
                return index
    return len(text) - 1


def functions(code):
    """Yield (name, body_start, body_end) for function definitions."""
    for match in FUNCTION.finditer(code):
        name = match.group("name").split("::")[-1]
        if name in CONTROL:
            continue
        brace = match.end() - 1
        yield name, brace, balanced(code, brace, "{", "}")


def exempt(lines, line_number):
    for candidate in (line_number, line_number - 1):
        if 1 <= candidate <= len(lines):
            marker = MARKER.search(lines[candidate - 1])
            if marker and marker.group(1):
                return True
    return False


def in_scope(relative, scopes):
    return any(
        relative == scope or relative.startswith(scope + "/") for scope in scopes
    )


def file_findings(root, path):
    relative = path.relative_to(root).as_posix()
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    code = strip_comments_and_strings(text)
    code_lines = code.splitlines()
    found = []

    def report(offset_or_line, rule, advice, *, is_line=False):
        line = offset_or_line if is_line else code.count("\n", 0, offset_or_line) + 1
        if not exempt(lines, line):
            found.append(
                {"path": relative, "line": line, "rule": rule, "advice": advice}
            )

    for line_number, line in enumerate(code_lines, 1):
        for rule, pattern, scopes, advice in LINE_RULES:
            if in_scope(relative, scopes) and pattern.search(line):
                report(line_number, rule, advice, is_line=True)

    for match in re.finditer(r"\bconnect\s*\(", code):
        end = balanced(code, match.end() - 1)
        statement = code[match.start() : end + 1]
        if CONNECT_SIGNALS.search(statement) and ZERO_SHOT.search(statement):
            report(
                match.start() + ZERO_SHOT.search(statement).start(),
                "uncoalesced model-signal work",
                "coalesce bursts with a single-shot member QTimer instead of one "
                "zero-delay timer per signal",
            )

    for name, start, end in functions(code):
        if not HOT_FUNCTIONS.match(name):
            continue
        body = code[start:end]
        for pattern, rule in HOT_RULES:
            if rule.startswith("linear search") and name not in {"parent", "indexFor"}:
                continue
            for hit in pattern.finditer(body):
                report(
                    start + hit.start(),
                    rule,
                    f"{name}() runs per paint, size hint, or index lookup; "
                    "precompute or cache outside it",
                )
    return found


def violations(root=ROOT):
    found = []
    for scope in SCANNED:
        base = root / scope
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix in CPP_SUFFIXES and path.is_file():
                found.extend(file_findings(root, path))
    return found


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="print findings as JSON")
    args = parser.parse_args(argv)
    found = violations()
    if args.json:
        print(json.dumps({"findings": found}, indent=2))
    for item in found:
        print(
            f"{item['path']}:{item['line']}: {item['rule']} ({item['advice']})",
            file=sys.stderr,
        )
    if found:
        print(
            "Large-data performance policy: FAIL. Fix the pattern or add a reviewed "
            "`// perf-ok: <reason>` marker on the line or the line above.",
            file=sys.stderr,
        )
        return 1
    print("Large-data performance policy: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
