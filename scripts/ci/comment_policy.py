from dataclasses import dataclass
import argparse
import ast
import io
from pathlib import Path
import re
import subprocess
import sys
import tokenize

ROOT = Path(__file__).resolve().parents[2]
VENDORED_DIRECTORY = "third_party"
REASONED_DIRECTIVES = ("SAFETY", "ui-ok", "perf-ok", "ui-budget")
REASONED_NAMES = "|".join(REASONED_DIRECTIVES)
KEYWORD = re.compile(rf"//\s*(?:{REASONED_NAMES}|clang-format)\b")
REASONED_DIRECTIVE = re.compile(rf"//\s*(?:{REASONED_NAMES}):\s*\S")
DIRECTIVE = re.compile(rf"{REASONED_DIRECTIVE.pattern}|//\s*clang-format (?:off|on)\b")
PINNED_ACTION = re.compile(r"uses:\s*[^\s#]+@[0-9a-f]{40}\s+$")
VERSION_LABEL = re.compile(r"#\s*v\d[\w.-]*\s*$")
CMAKE_BRACKET = re.compile(r"(#?)\[(=*)\[")
WORD = re.compile(r"\w")
RUST_RAW_STRING = re.compile(r'(?<![A-Za-z0-9_])[bc]?r(#*)"')
RUST_CHAR = re.compile(r"'(?:\\(?:x[0-9a-fA-F]{2}|u\{[0-9a-fA-F_]{1,6}\}|.)|[^\\'\n])'")
CPP_RAW_STRING = re.compile(r'(?<![A-Za-z0-9_])(?:u8|[uUL])?R"([^()\\\s]{0,16})\(')
CPP_NUMBER_TAIL = re.compile(r"[0-9A-Za-z_.']*$")


@dataclass(frozen=True)
class Syntax:
    line: tuple[str, ...] = ()
    block: tuple[tuple[str, str], ...] = ()
    quotes: str = '"'
    multiline_strings: bool = False
    word_start: bool = False
    dialect: str = ""


@dataclass(frozen=True)
class Comment:
    start: int
    end: int
    text: str
    replacement: str = ""


RUST = Syntax(("//",), (("/*", "*/"),), "\"'", True, dialect="rust")
CPP = Syntax(("//",), (("/*", "*/"),), "\"'", True, dialect="cpp")
QSS = Syntax((), (("/*", "*/"),), "\"'")
SQL = Syntax(("--",), (("/*", "*/"),), "'\"", True)
HASH = Syntax(("#",), (), "\"'", word_start=True)
CMAKE = Syntax(("#",), (), '"', True, word_start=True, dialect="cmake")
POWERSHELL = Syntax(("#",), (("<#", "#>"),), "\"'", word_start=True)
NSIS = Syntax((";", "#"), (("/*", "*/"),), "\"'`", word_start=True)
PYTHON = Syntax(dialect="python")
NAMES = {
    "CMakeLists.txt": CMAKE,
    "requirements.txt": HASH,
    ".clang-format": HASH,
    ".clang-tidy": HASH,
    ".gitattributes": HASH,
    ".gitignore": HASH,
    "Dockerfile": HASH,
}
SUFFIXES = {
    ".rs": RUST,
    **dict.fromkeys(
        (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".mm"), CPP
    ),
    ".qss": QSS,
    ".sql": SQL,
    ".py": PYTHON,
    ".cmake": CMAKE,
    ".sh": HASH,
    ".toml": HASH,
    ".yml": HASH,
    ".yaml": HASH,
    ".ps1": POWERSHELL,
    ".nsi": NSIS,
}


def syntax_for(path):
    if path.name in NAMES:
        return NAMES[path.name]
    if path.suffix in SUFFIXES:
        return SUFFIXES[path.suffix]
    if not path.suffix:
        with path.open("rb") as handle:
            shebang = handle.readline()
        if shebang.startswith(b"#!"):
            return PYTHON if b"python" in shebang else HASH
    return None


def source_files(root):
    listed = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=root,
        check=True,
        capture_output=True,
    ).stdout.decode("utf-8")
    for name in sorted(filter(None, listed.split("\0"))):
        path = root / name
        if VENDORED_DIRECTORY in Path(name).parts or not path.is_file():
            continue
        syntax = syntax_for(path)
        if syntax is not None:
            yield path, syntax


def read_source(path):
    text = path.read_bytes().decode("utf-8")
    if text.startswith("\ufeff"):
        raise ValueError("byte order mark at offset 0")
    return text


def at_word_start(text, index):
    return index == 0 or text[index - 1] in " \t\r\n;("


def string_end(text, index, quote, multiline):
    position = index + 1
    while position < len(text):
        character = text[position]
        if character == "\\":
            position += 2
            continue
        if character == quote:
            return position + 1
        if character == "\n" and not multiline:
            return position
        position += 1
    if not multiline:
        return len(text)
    raise ValueError(f"unterminated {quote} string at offset {index}")


def block_end(text, index, opener, closer, nested):
    depth = 0
    position = index
    while position < len(text):
        if nested and text.startswith(opener, position):
            depth += 1
            position += len(opener)
        elif text.startswith(closer, position):
            depth -= 1
            position += len(closer)
            if depth == 0 or not nested:
                return position
        else:
            position += 1
    raise ValueError(f"unterminated {opener} comment at offset {index}")


def raw_string_end(text, index, dialect):
    pattern = RUST_RAW_STRING if dialect == "rust" else CPP_RAW_STRING
    match = pattern.match(text, index)
    if match is None:
        return None
    if dialect == "rust":
        closer = '"' + match.group(1)
    else:
        closer = ")" + match.group(1) + '"'
    end = text.find(closer, match.end())
    if end < 0:
        raise ValueError(f"unterminated raw string at offset {index}")
    return end + len(closer)


def quote_end(text, index, syntax):
    if syntax.dialect == "rust" and text[index] == "'":
        match = RUST_CHAR.match(text, index)
        return match.end() if match else index + 1
    if syntax.dialect == "cpp" and text[index] == "'":
        tail = CPP_NUMBER_TAIL.search(text, 0, index).group(0)
        if tail[:1].isdigit():
            return index + 1
        return string_end(text, index, "'", False)
    return string_end(text, index, text[index], syntax.multiline_strings)


def content_end(text, index):
    end = text.find("\n", index)
    end = len(text) if end < 0 else end
    return end - 1 if end > index and text[end - 1] == "\r" else end


def cmake_bracket(text, index, word_start):
    match = CMAKE_BRACKET.match(text, index)
    if match is None or (match.group(1) and not word_start):
        return None
    closer = "]" + match.group(2) + "]"
    end = text.find(closer, match.end())
    if end < 0:
        raise ValueError(f"unterminated bracket at offset {index}")
    return bool(match.group(1)), end + len(closer)


def scanned_comments(text, syntax):
    comments = []
    index = 0
    while index < len(text):
        if syntax.dialect in {"rust", "cpp"} and text[index] in "bcruULR":
            end = raw_string_end(text, index, syntax.dialect)
            if end is not None:
                index = end
                continue
        word_start = not syntax.word_start or at_word_start(text, index)
        bracket = (
            cmake_bracket(text, index, word_start)
            if syntax.dialect == "cmake" and text[index] in "#["
            else None
        )
        block = next(
            (pair for pair in syntax.block if text.startswith(pair[0], index)), None
        )
        if bracket is not None:
            is_comment, end = bracket
            if is_comment:
                comments.append(Comment(index, end, text[index:end]))
            index = end
        elif block is not None and word_start:
            end = block_end(text, index, *block, nested=syntax.dialect == "rust")
            comments.append(Comment(index, end, text[index:end]))
            index = end
        elif word_start and any(text.startswith(line, index) for line in syntax.line):
            end = content_end(text, index)
            if syntax.dialect == "cpp" and text[index:end].endswith("\\"):
                raise ValueError(f"line splice in a comment at offset {index}")
            comments.append(Comment(index, end, text[index:end]))
            index = end
        elif text[index] in syntax.quotes and word_start:
            index = quote_end(text, index, syntax)
        else:
            index += 1
    return comments


def python_comments(text):
    starts = [0]
    for line in io.StringIO(text).readlines():
        starts.append(starts[-1] + len(line))

    def offset(row, column, encoded=False):
        line = text[starts[row - 1] : starts[row]] if row < len(starts) else ""
        if encoded:
            column = len(line.encode("utf-8")[:column].decode("utf-8"))
        return starts[row - 1] + column

    comments = [
        Comment(offset(*token.start), offset(*token.end), token.string)
        for token in tokenize.generate_tokens(io.StringIO(text).readline)
        if token.type == tokenize.COMMENT
    ]
    for node in ast.walk(ast.parse(text)):
        owners = (ast.Module, ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)
        if not isinstance(node, owners) or not node.body:
            continue
        first = node.body[0]
        if not (
            isinstance(first, ast.Expr)
            and isinstance(first.value, ast.Constant)
            and isinstance(first.value.value, str)
        ):
            continue
        sole = len(node.body) == 1 and not isinstance(node, ast.Module)
        start = offset(first.lineno, first.col_offset, encoded=True)
        end = offset(first.end_lineno, first.end_col_offset, encoded=True)
        comments.append(
            Comment(start, end, text[start:end], replacement="pass" if sole else "")
        )
    return sorted(comments, key=lambda comment: comment.start)


def is_directive(text, comment):
    line_before = text[text.rfind("\n", 0, comment.start) + 1 : comment.start]
    return (
        (comment.start == 0 and comment.text.startswith("#!"))
        or bool(DIRECTIVE.match(comment.text))
        or bool(PINNED_ACTION.search(line_before) and VERSION_LABEL.match(comment.text))
    )


def continues(text, previous, comment):
    gap = text[previous.end : comment.start]
    column = previous.start - text.rfind("\n", 0, previous.start) - 1
    return (
        comment.text.startswith("//")
        and not KEYWORD.match(comment.text)
        and gap.count("\n") == 1
        and not gap.strip()
        and len(gap) - 1 == column
        and not text[text.rfind("\n", 0, previous.start) + 1 : previous.start].strip()
    )


def forbidden_comments(text, syntax):
    comments = (
        python_comments(text)
        if syntax.dialect == "python"
        else scanned_comments(text, syntax)
    )
    forbidden = []
    directive = None
    for comment in comments:
        if directive is not None and continues(text, directive, comment):
            directive = comment
        elif is_directive(text, comment):
            directive = comment if REASONED_DIRECTIVE.match(comment.text) else None
        else:
            directive = None
            forbidden.append(comment)
    return forbidden


def file_comments(root):
    for path, syntax in source_files(root):
        text = read_source(path)
        try:
            comments = forbidden_comments(text, syntax)
        except (ValueError, SyntaxError) as error:
            raise ValueError(f"{path.relative_to(root).as_posix()}: {error}") from error
        yield path, text, comments


def violations(root=ROOT):
    found = set()
    for path, text, comments in file_comments(root):
        relative = path.relative_to(root).as_posix()
        found.update(
            (relative, text.count("\n", 0, comment.start) + 1) for comment in comments
        )
    return [f"{relative}:{line}: comment" for relative, line in sorted(found)]


def without_comments(text, comments):
    for comment in reversed(comments):
        line_start = text.rfind("\n", 0, comment.start) + 1
        line_end = content_end(text, comment.end)
        next_line = text.find("\n", comment.end)
        next_line = len(text) if next_line < 0 else next_line + 1
        before = text[line_start : comment.start]
        after = text[comment.end : line_end]
        if comment.replacement:
            line = before + comment.replacement + after
        elif not before.strip() and not after.strip():
            text = text[:line_start] + text[next_line:]
            previous = text[: max(line_start - 1, 0)].rsplit("\n", 1)[-1].rstrip()
            following_end = text.find("\n", line_start)
            following = text[
                line_start : len(text) if following_end < 0 else following_end
            ]
            opens = (
                line_start == 0
                or not previous
                or previous.endswith(("{", ":", "(", "["))
            )
            if opens and following_end >= 0 and not following.strip():
                text = text[:line_start] + text[following_end + 1 :]
            continue
        elif not before.strip():
            line = before + after.lstrip()
        else:
            head = before.rstrip()
            joins_words = WORD.match(head[-1:]) and WORD.match(after[:1])
            line = head + (" " if joins_words else "") + after
        text = text[:line_start] + line.rstrip() + text[line_end:]
    return text


def fix(root=ROOT):
    for path, text, comments in list(file_comments(root)):
        if comments:
            path.write_bytes(without_comments(text, comments).encode("utf-8"))


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Reject comments in first-party code. Names and types are the documentation."
    )
    parser.add_argument(
        "--fix", action="store_true", help="delete the comments that the policy rejects"
    )
    args = parser.parse_args(argv)
    if args.fix:
        fix()
    problems = violations()
    if problems:
        print("\n".join(problems), file=sys.stderr)
        print(
            f"comment_policy: {len(problems)} comment lines. Put the meaning in names, "
            "types and tests. Only SAFETY, ui-ok, perf-ok, ui-budget, "
            "clang-format, shebang and pinned-action version labels are allowed.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
