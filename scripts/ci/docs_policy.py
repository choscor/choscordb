#!/usr/bin/env python3
"""Keep human documentation linked, short-sentenced, and current with the code.

Default mode checks every page in scope:

- links: relative Markdown links, `#anchor` targets, and `docs/...` or
  `user-guide/...` paths resolve; every glob in the `docs/README.md` map matches
  a file.
- sentence length (ASD-STE100): a sentence in a list item has at most 20 words;
  any other sentence has at most 25. Fenced code, inline code, tables, headings
  and link targets are not counted.

`--changed --base <ref>` instead fails when a file under a mapped glob changed
in `<ref>...HEAD` but none of its owning pages changed, unless `PR_BODY` or a
commit message in the range has a `Docs: none needed, <reason>` line.

A line may opt out with `<!-- docs-ok: <reason> -->` on it or on the line
above; the reason must be non-empty so the exception is reviewable.
"""

from pathlib import Path
import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
ROOT_PAGES = ("README.md", "CONTRIBUTING.md", "CLAUDE.md")
PAGE_TREES = ("docs", "user-guide")
MAP_PAGE = "docs/README.md"
STEP_LIMIT = 20
SENTENCE_LIMIT = 25

MARKER = re.compile(r"<!--\s*docs-ok:(.*?)-->")
REVIEWED = re.compile(r"<!--\s*docs-ok:\s*\S.*?-->")
FENCE = re.compile(r"^\s*(```|~~~)")
MAP_FENCE = re.compile(r"^\s*```docs-map\s*$")
LIST_ITEM = re.compile(r"^\s*(?:[-*+]|\d+[.)])\s+")
LINK = re.compile(r"!?\[([^\]]*)\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
INLINE_CODE = re.compile(r"`[^`]*`")
BARE_PATH = re.compile(r"(?<![\w./-])((?:docs|user-guide)/[\w./-]*[\w/])")
HTML_COMMENT = re.compile(r"<!--.*?-->")
SENTENCE_END = re.compile(r"[.!?](?=\s|$)")
ESCAPE = re.compile(r"^Docs: none needed, *(\S.*)$", re.MULTILINE)


def pages(root):
    found = [root / name for name in ROOT_PAGES if (root / name).is_file()]
    for tree in PAGE_TREES:
        if (root / tree).is_dir():
            found.extend(sorted((root / tree).rglob("*.md")))
    return found


def slug(heading):
    text = INLINE_CODE.sub(lambda match: match.group(0).strip("`"), heading)
    text = re.sub(r"[^\w\s-]", "", text.strip().lower())
    return re.sub(r"\s", "-", text)


def anchors(path):
    found, fenced = set(), False
    for line in path.read_text(encoding="utf-8").splitlines():
        if FENCE.match(line):
            fenced = not fenced
        elif not fenced and line.startswith("#"):
            found.add(slug(line.lstrip("#")))
    return found


def doc_map(root):
    """Return (line, glob, owning pages) rows from the docs/README.md map block."""
    path = root / MAP_PAGE
    if not path.is_file():
        return []
    rows, inside = [], False
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if MAP_FENCE.match(line):
            inside = True
        elif inside and FENCE.match(line):
            inside = False
        elif inside and "->" in line:
            glob, _, targets = line.partition("->")
            owners = [item.strip() for item in targets.split(",") if item.strip()]
            rows.append((number, glob.strip(), owners))
    return rows


def matches(glob, name):
    # fnmatch's `*` also matches `/`, so `dir/**` and `dir/prefix_*` cover nested files.
    return fnmatch.fnmatchcase(name, glob)


def reviewed(lines, index):
    return any(
        0 <= candidate < len(lines) and REVIEWED.search(lines[candidate])
        for candidate in (index, index - 1)
    )


def finding(relative, line, rule, message):
    return {"path": relative, "line": line, "rule": rule, "message": message}


def link_findings(root, path, relative, lines):
    found, fenced = [], False
    for index, line in enumerate(lines):
        if FENCE.match(line):
            fenced = not fenced
            continue
        targets = []
        if not fenced:
            prose = INLINE_CODE.sub("", line)
            for match in LINK.finditer(prose):
                target = match.group(2)
                if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.IGNORECASE):
                    continue
                file_part, _, anchor = target.partition("#")
                resolved = (path.parent / file_part) if file_part else path
                targets.append((target, resolved, anchor))
            for match in BARE_PATH.finditer(LINK.sub(" ", line)):
                if not any(char in match.group(1) for char in "*?["):
                    targets.append((match.group(1), root / match.group(1), ""))
        for target, resolved, anchor in targets:
            if not resolved.exists():
                message = f"{target} does not exist"
            elif anchor and resolved.is_file() and anchor not in anchors(resolved):
                message = f"{target} has no heading for #{anchor}"
            else:
                continue
            if not reviewed(lines, index):
                found.append(finding(relative, index + 1, "broken link", message))
    return found


def prose_blocks(lines):
    """Yield (first line index, is list item, [(line index, text)]) blocks."""
    block, is_item, fenced = [], False, False
    for index, line in enumerate(lines):
        if FENCE.match(line):
            fenced = not fenced
            if block:
                yield block[0][0], is_item, block
            block = []
            continue
        stripped = line.strip()
        if (
            fenced
            or not stripped
            or stripped.startswith(("#", "|", "<!--"))
            or LIST_ITEM.match(line)
        ):
            if block:
                yield block[0][0], is_item, block
            block, is_item = [], False
            if fenced or not stripped or stripped.startswith(("#", "|")):
                continue
            if stripped.startswith("<!--") and stripped.endswith("-->"):
                continue
            is_item = bool(LIST_ITEM.match(line))
            line = LIST_ITEM.sub("", line, count=1)
        block.append((index, line))
    if block:
        yield block[0][0], is_item, block


def sentence_findings(relative, lines):
    found = []
    for _, is_item, block in prose_blocks(lines):
        text, starts = "", []
        for index, line in block:
            prose = HTML_COMMENT.sub(" ", line)
            prose = LINK.sub(r"\1", INLINE_CODE.sub("code", prose))
            starts.append((len(text), index))
            text += prose.strip() + " "
        limit = STEP_LIMIT if is_item else SENTENCE_LIMIT
        start = 0
        for sentence_end in [*SENTENCE_END.finditer(text), None]:
            end = sentence_end.end() if sentence_end else len(text)
            words = [word for word in text[start:end].split() if re.search(r"\w", word)]
            if len(words) > limit:
                offset = start + len(text[start:end]) - len(text[start:end].lstrip())
                index = max(line for position, line in starts if position <= offset)
                if not reviewed(lines, index):
                    kind = "long step" if is_item else "long sentence"
                    found.append(
                        finding(
                            relative,
                            index + 1,
                            kind,
                            f"{len(words)} words; split it to {limit} or fewer",
                        )
                    )
            start = end
    return found


def marker_findings(relative, lines):
    return [
        finding(
            relative, index + 1, "marker without reason", "add a reason after docs-ok:"
        )
        for index, line in enumerate(lines)
        for marker in MARKER.finditer(line)
        if not marker.group(1).strip()
    ]


def tracked(root):
    """Files in the Git index; a new file matches map globs only after `git add`."""
    result = subprocess.run(
        ["git", "ls-files"], cwd=root, capture_output=True, text=True, check=False
    )
    if result.returncode == 0 and result.stdout:
        return result.stdout.splitlines()
    return [
        path.relative_to(root).as_posix()
        for path in root.rglob("*")
        if path.is_file() and ".git" not in path.parts
    ]


def map_findings(root):
    rows = doc_map(root)
    if not rows:
        return []
    files = tracked(root)
    found = [
        finding(MAP_PAGE, line, "missing map owner", f"{owner} does not exist")
        for line, _, owners in rows
        for owner in owners
        if not (root / owner).is_file()
    ]
    return found + [
        finding(MAP_PAGE, line, "unmatched map glob", f"{glob} matches no file")
        for line, glob, _ in rows
        if not any(matches(glob, name) for name in files)
    ]


def violations(root=ROOT):
    found = []
    for path in pages(root):
        relative = path.relative_to(root).as_posix()
        lines = path.read_text(encoding="utf-8").splitlines()
        found += marker_findings(relative, lines)
        found += link_findings(root, path, relative, lines)
        found += sentence_findings(relative, lines)
    found += map_findings(root)
    return sorted(found, key=lambda item: (item["path"], item["line"], item["rule"]))


def git(root, *args):
    return subprocess.run(
        ["git", *args], cwd=root, capture_output=True, text=True, check=True
    ).stdout


def changed_violations(root, base, pr_body=""):
    changed = set(git(root, "diff", "--name-only", f"{base}...HEAD").splitlines())
    messages = git(root, "log", "--format=%B", f"{base}..HEAD")
    escapes = HTML_COMMENT.sub("", (pr_body or "") + "\n" + messages)
    if ESCAPE.search(escapes):
        return []
    found = []
    for line, glob, owners in doc_map(root):
        code = sorted(name for name in changed if matches(glob, name))
        if code and not changed.intersection(owners):
            found.append(
                finding(
                    MAP_PAGE,
                    line,
                    "doc not updated",
                    f"{code[0]} changed under {glob}; update {' or '.join(owners)} "
                    "or add 'Docs: none needed, <reason>' to the PR body or a commit",
                )
            )
    return found


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="print findings as JSON")
    parser.add_argument(
        "--changed", action="store_true", help="check doc updates for changed code"
    )
    parser.add_argument("--base", default="origin/main", help="base ref for --changed")
    args = parser.parse_args(argv)
    if args.changed:
        found = changed_violations(ROOT, args.base, os.environ.get("PR_BODY", ""))
    else:
        found = violations()
    if args.json:
        print(json.dumps({"findings": found}, indent=2))
    for item in found:
        print(
            f"{item['path']}:{item['line']}: {item['rule']} ({item['message']})",
            file=sys.stderr,
        )
    if found:
        print(
            "Documentation policy: FAIL. Fix the page, or add a reviewed "
            "`<!-- docs-ok: <reason> -->` marker on the line or the line above.",
            file=sys.stderr,
        )
        return 1
    print("Documentation policy: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
