#!/usr/bin/env python3
"""Reject dead or duplicated native desktop code.

Checks:
- every desktop/test translation unit is named by CMake;
- every desktop header is included somewhere;
- every member function declared in a desktop header is referenced beyond its
  declaration and definition (Qt overrides, operators and special members are
  ignored);
- every declared Qt signal is connected, spied, or forwarded somewhere;
- anonymous-namespace helpers are not copied verbatim between files;
- ui_consistency.py component lists name classes and controls that still exist.

Reviewed exceptions live in ``EXCEPTIONS`` with a reason; stale exceptions fail.
"""

from pathlib import Path
import argparse
import collections
import hashlib
import json
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
CPP_SUFFIXES = {".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".mm"}
UNITS = {".cc", ".cpp", ".cxx", ".mm"}
HEADERS = {".h", ".hh", ".hpp", ".hxx"}
SOURCE_DIRS = ("desktop", "tests")
REFERENCE_DIRS = ("desktop", "tests", "crates/bridge")
SKIP_PARTS = {"build", "target", ".git"}

# "path::symbol" -> reason. Keep entries narrow and re-review when code moves.
EXCEPTIONS = {}

CLASS_OPEN = re.compile(r"^\s*(?:class|struct)\s+(?:\w+\s+)*?(\w+)\b[^;]*\{")
ACCESS = re.compile(r"^\s*(public|protected|private|signals|Q_SIGNALS)\b[^:]*:")
DECLARATION = re.compile(
    r"^\s*(?:(?:virtual|static|inline|explicit|constexpr|Q_INVOKABLE|"
    r"\[\[nodiscard\]\])\s+)*"
    r"(?P<type>[\w:<>,*&\s]+?[\s*&])(?P<name>~?[A-Za-z_]\w*)\s*"
    r"\((?P<args>[^;{}]*)\)\s*(?P<tail>[^;{]*)(?P<end>[;{])"
)
INCLUDE = re.compile(r'^\s*#\s*(?:include|import)\s*[<"]([^>"]+)[>"]', re.M)
ANONYMOUS = re.compile(r"\bnamespace\s*\{")
HELPER = re.compile(
    r"(?:^|\n)[ \t]*(?:static\s+|inline\s+|\[\[nodiscard\]\]\s+)*"
    r"[\w:<>,*&\s]+?[\s*&](?P<name>[A-Za-z_]\w*)\s*\((?P<args>[^;{}]*)\)\s*"
    r"(?:const\s*)?(?:noexcept\s*)?\{"
)
KEYWORDS = {"if", "for", "while", "switch", "catch", "return", "sizeof", "else"}


def source_files(root, directories, suffixes):
    for directory in directories:
        base = root / directory
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            relative = path.relative_to(root)
            if (
                path.suffix in suffixes
                and path.is_file()
                and not (SKIP_PARTS & set(relative.parts))
            ):
                yield path


def strip_comments(text):
    text = re.sub(
        r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S
    )
    return re.sub(r"//[^\n]*", "", text)


def matching_brace(text, start):
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(text) - 1


def cmake_text(root):
    parts = []
    for path in sorted(root.rglob("CMakeLists.txt")) + sorted(root.rglob("*.cmake")):
        if not (SKIP_PARTS & set(path.relative_to(root).parts)):
            parts.append(path.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(parts)


def unlisted_units(root):
    cmake = cmake_text(root)
    for path in source_files(root, SOURCE_DIRS, UNITS):
        if not re.search(rf"(?<![\w.-]){re.escape(path.name)}\b", cmake):
            yield path, path.name, "translation unit is not named by CMake"


def unincluded_headers(root, texts):
    included = collections.Counter()
    for text in texts.values():
        included.update(Path(name).name for name in INCLUDE.findall(text))
    for path in source_files(root, ("desktop",), HEADERS):
        if not included[path.name]:
            yield path, path.name, "header is never included"


def class_members(text):
    """Yield (name, line, is_inline, is_signal) for class-scope declarations."""
    stack = []  # entries: (kind, class_name, section)
    for line_number, line in enumerate(text.splitlines(), 1):
        top = stack[-1] if stack else None
        if top and top[0] == "class":
            access = ACCESS.match(line)
            if access:
                stack[-1] = ("class", top[1], access.group(1))
                top = stack[-1]
            match = DECLARATION.match(line)
            if match and not CLASS_OPEN.match(line):
                name = match.group("name")
                type_ = match.group("type").strip()
                tail = match.group("tail")
                skip = (
                    name.lstrip("~") == top[1]
                    or name.startswith("operator")
                    or type_.endswith("operator")
                    or not type_
                    or type_ in {"return", "else", "new", "delete", "emit"}
                    or re.search(
                        r"\boverride\b|\bfinal\b|=\s*(?:0|default|delete)", tail
                    )
                )
                if not skip:
                    yield (
                        name,
                        line_number,
                        match.group("end") == "{",
                        top[2] in {"signals", "Q_SIGNALS"},
                    )
        class_open = CLASS_OPEN.match(line)
        for index, char in enumerate(line):
            if char == "{":
                if class_open and index >= class_open.end() - 1:
                    name = class_open.group(1)
                    section = "public" if re.match(r"\s*struct\b", line) else "private"
                    stack.append(("class", name, section))
                    class_open = None
                else:
                    stack.append(("block", None, None))
            elif char == "}" and stack:
                stack.pop()


def unused_members(root, texts, words):
    for path in source_files(root, ("desktop",), HEADERS):
        text = strip_comments(texts[path])
        for name, line, inline, signal in class_members(text):
            if signal:
                referenced = re.search(
                    rf"(?:&\s*[\w:]+::|SIGNAL\s*\(\s*){re.escape(name)}\b",
                    words["__all__"],
                )
                if not referenced:
                    yield path, name, f"signal is never connected (line {line})"
            elif words["count"][name] <= (1 if inline else 2):
                yield path, name, f"member function is never called (line {line})"


def anonymous_helpers(text):
    for match in ANONYMOUS.finditer(text):
        end = matching_brace(text, match.end() - 1)
        block = text[match.end() : end]
        for helper in HELPER.finditer(block):
            if helper.group("name") in KEYWORDS:
                continue
            open_brace = helper.end() - 1
            body = block[open_brace : matching_brace(block, open_brace) + 1]
            normalized = re.sub(r"\s+", "", body)
            if len(normalized) >= 48:
                yield helper.group("name"), normalized


def duplicate_helpers(root, texts):
    seen = collections.defaultdict(list)
    for path in source_files(root, ("desktop",), CPP_SUFFIXES):
        for name, body in anonymous_helpers(strip_comments(texts[path])):
            seen[hashlib.sha256(body.encode()).hexdigest()].append((path, name))
    for copies in seen.values():
        paths = sorted({path for path, _ in copies})
        if len(paths) < 2:
            continue
        for path in paths[1:]:
            name = next(name for candidate, name in copies if candidate == path)
            yield (
                path,
                name,
                f"anonymous helper duplicates {paths[0].relative_to(root).as_posix()}; "
                "share one implementation",
            )


def stale_checker_lists(root, corpus):
    """Names in ui_consistency.py lists must still exist in desktop code."""
    import ui_consistency

    checker = root / "scripts/ci/ui_consistency.py"
    design = (
        ui_consistency.DESIGN_CONTROLS
        | ui_consistency.DESIGN_COMPOSITES
        | ui_consistency.SHARED_DIALOGS
        | ui_consistency.DESIGN_NON_VISUAL
    )
    for name in sorted(design):
        if not re.search(rf"\b(?:class|struct)\s+(?:\w+\s+)?{name}\b", corpus):
            yield (
                checker,
                name,
                "ui_consistency list names a class that no longer exists",
            )
    for name in sorted(
        ui_consistency.STOCK_CONTROLS | ui_consistency.STRUCTURAL_CONTROLS
    ):
        if not re.search(rf"\b{name}\b", corpus):
            yield (
                checker,
                name,
                "ui_consistency list names a control nothing constructs",
            )


def findings(root=ROOT, check_lists=True):
    texts = {
        path: path.read_text(encoding="utf-8", errors="replace")
        for path in source_files(root, REFERENCE_DIRS, CPP_SUFFIXES | {".rs"})
    }
    corpus = "\n".join(strip_comments(text) for text in texts.values())
    words = {
        "count": collections.Counter(re.findall(r"\b[A-Za-z_]\w*\b", corpus)),
        "__all__": corpus,
    }
    raw = [
        *unlisted_units(root),
        *unincluded_headers(root, texts),
        *unused_members(root, texts, words),
        *duplicate_helpers(root, texts),
        *(stale_checker_lists(root, corpus) if check_lists else ()),
    ]
    result, used = [], set()
    for path, symbol, message in raw:
        key = f"{path.relative_to(root).as_posix()}::{symbol}"
        if key in EXCEPTIONS:
            used.add(key)
            continue
        result.append(
            {"path": key.split("::")[0], "symbol": symbol, "message": message}
        )
    for key in sorted(set(EXCEPTIONS) - used):
        result.append(
            {
                "path": key.split("::")[0],
                "symbol": key.split("::")[-1],
                "message": "stale source-inventory exception",
            }
        )
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="print findings as JSON")
    args = parser.parse_args(argv)
    found = findings()
    if args.json:
        print(json.dumps({"findings": found}, indent=2))
    for item in found:
        print(f"{item['path']}: {item['symbol']}: {item['message']}", file=sys.stderr)
    if found:
        print(
            "Source inventory: FAIL. Remove dead code or record a reviewed exception.",
            file=sys.stderr,
        )
        return 1
    print("Source inventory: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
