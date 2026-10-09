#!/usr/bin/env python3
"""Census native sources and enforce concrete Rust/C++ ownership boundaries."""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".c++",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".h++",
    ".m",
    ".mm",
    ".ipp",
    ".inl",
    ".tpp",
    ".ixx",
    ".cppm",
}
# Tests are counted and scanned, but their fixture capabilities do not block.
# Unknown first-party locations are scanned AND rejected until classified here.
ROLES = {
    "desktop/app/": "presentation",
    "desktop/widgets/": "presentation",
    "desktop/models/": "display-model",
    "desktop/design_system/": "design-system",
    "desktop/bridge/": "bridge",
    "desktop/tools/": "development-tool",
    "tests/": "test",
    "vendor/": "third-party",
}
RULES = {
    "storage": (
        "error",
        r"\b(?:QSettings|QFile|QSaveFile|QTemporaryFile|QTemporaryDir|QDirIterator|QLockFile|QStorageInfo|QFileSystemWatcher|fstream|ifstream|ofstream|basic_fstream|basic_ifstream|basic_ofstream)\b|\bstd\s*::\s*filesystem\b",
    ),
    "filesystem-query": (
        "review",
        r"\bQFileInfo\s*::\s*exists\s*\(|\bQDir\s*::\s*(?:homePath|tempPath|currentPath|setCurrent|drives)\b|\b(?:entryList|entryInfoList|mkpath|mkdir|rmdir|removeRecursively)\s*\(",
    ),
    "network": (
        "error",
        r"\b(?:QNetwork\w*|QTcp\w*|QUdp\w*|QAbstractSocket|QSsl\w*|QWebSocket\w*|QHostInfo|QDnsLookup|curl_\w*|CURLOPT_\w*|WinHttp\w*|InternetOpen\w*|NSURLSession|NSURLConnection)\b",
    ),
    "database": (
        "error",
        r"\b(?:QSql\w*|sqlite3\w*|mysql_\w*|PQ(?:connect\w*|exec\w*|send\w*|getResult|finish)|SQL(?:Connect|DriverConnect|ExecDirect|Execute|AllocHandle)\w*)\b",
    ),
    "process": (
        "error",
        r"\b(?:QProcess|QProcessEnvironment)\b|\b(?:CreateProcess\w*|ShellExecute\w*|posix_spawn\w*|execve|execl|execvp|popen|system)\s*\(",
    ),
    "native-io": (
        "error",
        r"\b(?:fopen|freopen|fwrite|fread|fputs|fprintf|openat|creat|unlink|socket|CreateFile\w*|ReadFile|WriteFile|RegOpenKey\w*|RegSetValue\w*)\s*\(",
    ),
    "serialization": (
        "review",
        r"\b(?:QJsonDocument|QDataStream|QXmlStreamReader|QXmlStreamWriter|QDomDocument|QCryptographicHash|QPasswordDigestor)\b",
    ),
    "domain-parsing": ("review", r"\b(?:QRegularExpression|QVersionNumber)\b"),
}
COMPILED = {
    key: (level, re.compile(pattern)) for key, (level, pattern) in RULES.items()
}
SQL = re.compile(
    r"\b(?:SELECT\s+.+?\s+FROM|INSERT\s+INTO|DELETE\s+FROM|UPDATE\s+\S+\s+SET|CREATE\s+(?:TABLE|INDEX)|DROP\s+(?:TABLE|DATABASE)|ALTER\s+TABLE)\b",
    re.I | re.S,
)
BACKEND_INCLUDE = re.compile(
    r"(?:^|/)(?:QtSql|QtNetwork|sqlite3\.h|libpq-fe\.h|mysql\.h|sql\.h|sqlext\.h|curl\.h|fstream|filesystem|sys/socket\.h|sys/stat\.h)(?:/|$)"
)
BUILD_BACKEND = re.compile(
    r"\b(?:Qt[56]::(?:Sql|Network|NetworkAuth|WebSockets)|SQLite::\w+|PostgreSQL::\w+|CURL::\w+|libpq|mysqlclient|sqlite3)\b",
    re.I,
)


RUST_LIMIT = re.compile(
    r"^\s*pub(?:\([^)]*\))?\s+const\s+((?:MAX|MIN|DEFAULT)_\w+)\s*:\s*(?:u8|u16|u32|u64|u128|usize|i32|i64)\s*=\s*([\d_\s*()]+);",
    re.M,
)
# Smaller values (ports, counts, milliseconds) collide with ordinary UI numbers.
MIN_REPORTED_LIMIT = 4096
TRANSLATED_CONTROL_FLOW = re.compile(
    r"(?:\.(?:startsWith|endsWith|contains|compare|indexOf)\s*\(|[=!]=)\s*(?:(?:QObject|QCoreApplication|QApplication)\s*::\s*)?tr\s*\("
)
LIMIT_FIELD = (
    r"(?:\w+\s*::\s*)?\w*[Ll]imits\w*(?:\s*\(\s*\))?\s*(?:\.|->)\s*(?:min|max)\w*"
)
LIMIT_REVALIDATION = re.compile(
    rf"(?<![<>-])[<>]=?\s*{LIMIT_FIELD}|{LIMIT_FIELD}\s*(?:[<>]=?)(?![<>])"
)
# Behavior keyed by text, object kinds, or driver names belongs to Rust policy
# (TextFilter, ObjectKindTraits, driver workflow/form DTOs). Design-system and
# tool code may map them to presentation; screens, models and the bridge may not.
SEMANTIC_ROLES = {"presentation", "display-model", "bridge"}
TEXT_MATCHING = re.compile(
    r"\b(?:contains|indexOf|startsWith|endsWith|compare)\s*\((?:[^;()]|\([^;()]*\))*?"
    r"\bQt::CaseInsensitive\b"
    r"|\b(?:toLower|toCaseFolded)\s*\(\s*\)\s*\.\s*(?:contains|indexOf|startsWith|endsWith)\s*\("
    r"|\bsetFilter(?:FixedString|RegularExpression|Wildcard|CaseSensitivity)\s*\("
)
OBJECT_KINDS = (
    "table|view|materialized_view|foreign_table|schema|database|catalog|column|index|"
    "sequence|function|procedure|trigger|connection|group|primarykey|foreignkey|uniquekey"
)
DRIVERS = "sqlite|postgres|postgresql|mysql|mariadb"
STRING_WRAPPER = (
    r"(?:QStringLiteral|QLatin1String(?:View)?|QStringView|QAnyStringView|QString|"
    r"QByteArrayLiteral|QByteArray)"
)


def fragments(names):
    """Names plus their prefixes and suffixes of three or more characters."""
    parts = set()
    for name in names.split("|"):
        for size in range(3, len(name) + 1):
            parts.update({name[:size], name[-size:]})
    return "|".join(sorted(map(re.escape, parts), key=len, reverse=True))


def literal_comparison(names, flags=0):
    """Branches on a listed name: equality, list membership, prefix or comparison."""

    def literal(words):
        return (
            rf'(?:{STRING_WRAPPER}\s*\(\s*)?(?:u8|u|U|L)?"(?:{words})"'
            r"(?:_s|_L1|_ba|_sv)?\s*\)?"
        )

    exact, partial = literal(names), literal(fragments(names))
    call = r"(?:\.|->|::)\s*"
    return re.compile(
        rf"(?:[=!]=)\s*{exact}|{exact}\s*(?:[=!]=)"
        rf'|\{{[^{{}};]*?"(?:{names})"[^{{}};]*\}}\s*{call}(?:contains|indexOf|count)\s*\('
        rf"|{call}(?:startsWith|endsWith|contains|indexOf)\s*\(\s*{partial}"
        rf"|{call}compare\s*\(\s*(?:[^;,()]+,\s*)?{exact}",
        flags,
    )


OBJECT_KIND_LITERAL = literal_comparison(OBJECT_KINDS)
# The bridge event "schema" shares its word with an object kind; event dispatch
# compares it through a variable named eventKind. No other kind word is exempt.
EVENT_KIND_SCHEMA = re.compile(r'\beventKind\s*[=!]=\s*"schema"')
# Driver names arrive in any case ("MySQL"), so they match case-insensitively.
DRIVER_LITERAL = literal_comparison(DRIVERS, re.IGNORECASE)
# A UI-only budget (event-loop slicing, display paging) that happens to equal a
# Rust limit, reviewed with its reason on the same line or the line above.
UI_BUDGET_MARKER = re.compile(r"//\s*ui-budget:\s*\S")
NUMBER = re.compile(r"(?<![\w.'])\d[\d']*(?:\s*\*\s*\d[\d']*)*(?![\w.'])")


def product(text):
    value = 1
    for factor in re.split(r"\s*\*\s*", text.replace("'", "").replace("_", "")):
        value *= int(factor)
    return value


def rust_limits(root):
    """Large numeric Rust MAX_/MIN_/DEFAULT_ constants that C++ must not copy."""
    limits = {}
    for path in sorted((root / "crates").glob("*/src/**/*.rs")):
        for match in RUST_LIMIT.finditer(path.read_text(encoding="utf-8")):
            expression = match[2].replace("(", "").replace(")", "").strip()
            if not re.fullmatch(r"[\d_]+(?:\s*\*\s*[\d_]+)*", expression):
                continue
            value = product(expression)
            if value >= MIN_REPORTED_LIMIT:
                limits.setdefault(value, match[1])
    return limits


def inventory(root):
    """Git defines the source checkout; include untracked, non-ignored additions."""
    result = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=root,
        check=True,
        stdout=subprocess.PIPE,
    )
    paths = set(result.stdout.decode("utf-8").split("\0")) - {""}
    # An ignore pattern must not silently exempt source inside owned roots.
    for directory in (root / "desktop", root / "tests"):
        for path in directory.rglob("*"):
            if path.suffix.lower() in SUFFIXES:
                paths.add(path.relative_to(root).as_posix())
    # Unstaged deletions are absent from the working tree, just as for compilation.
    return [p for p in sorted(paths) if (root / p).exists() or (root / p).is_symlink()]


def lex(source):
    """Mask comments/literals, preserving physical line numbers; return literals.

    This is lexical analysis, not a preprocessor or an AST. Scan inactive #if
    branches too, so the host platform cannot hide another platform's code.
    """
    # Translation phase 2: splice lines before recognizing tokens/comments.
    spliced = re.sub(r"\\\r?\n", "", source)
    # Map positions back to original lines even across line continuations.
    lines = []
    line = 1
    i = 0
    while i < len(source):
        match = re.match(r"\\\r?\n", source[i : i + 3])
        if match:
            line += 1
            i += len(match[0])
            continue
        lines.append(line)
        line += source[i] == "\n"
        i += 1
    token = re.compile(
        r'//[^\n]*|/\*[\s\S]*?\*/|(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=delimiter)"|(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*"|(?<![\w])(?:u8|u|U|L)?\'(?:\\[\s\S]|[^\'\\])*\''
    )
    masked = list(spliced)
    comments_removed = list(spliced)
    literals = []
    for match in token.finditer(spliced):
        value = match[0]
        is_comment = value.startswith(("//", "/*"))
        if not is_comment and '"' in value:
            literals.append((match.start(), value))
        for j in range(match.start(), match.end()):
            if spliced[j] != "\n":
                masked[j] = " "
                if is_comment:
                    comments_removed[j] = " "
    return "".join(masked), "".join(comments_removed), literals, lines


def role_for(path):
    return next(
        (role for prefix, role in ROLES.items() if path.startswith(prefix)),
        "unclassified",
    )


def scan(path, source, limits=None):
    code, uncommented, literals, lines = lex(source)
    findings = []

    def add(rule, severity, pos, evidence):
        findings.append(
            {
                "path": path,
                "line": lines[pos] if lines else 1,
                "rule": rule,
                "severity": severity,
                "evidence": evidence.strip(),
            }
        )

    for rule, (severity, pattern) in COMPILED.items():
        for match in pattern.finditer(code):
            add(rule, severity, match.start(), match[0])
    for match in TRANSLATED_CONTROL_FLOW.finditer(code):
        add("translated-control-flow", "error", match.start(), match[0])
    for match in LIMIT_REVALIDATION.finditer(code):
        add("limit-revalidation", "error", match.start(), match[0])
    if role_for(path) in SEMANTIC_ROLES:
        for rule, pattern in (
            ("text-matching", TEXT_MATCHING),
            ("object-kind-literal", OBJECT_KIND_LITERAL),
            ("driver-literal", DRIVER_LITERAL),
        ):
            for match in pattern.finditer(uncommented):
                around = uncommented[max(0, match.start() - 64) : match.end()]
                if rule == "object-kind-literal" and EVENT_KIND_SCHEMA.search(around):
                    continue
                add(rule, "error", match.start(), match[0])
    source_lines = source.splitlines()

    def reviewed_budget(pos):
        line = lines[pos] if lines else 1
        nearby = source_lines[max(0, line - 2) : line]
        return any(UI_BUDGET_MARKER.search(text) for text in nearby)

    for match in NUMBER.finditer(code):
        name = (limits or {}).get(product(match[0]))
        if name and not reviewed_budget(match.start()):
            add("rust-limit-literal", "error", match.start(), f"{match[0]} ({name})")
    for match in re.finditer(
        r'^\s*#\s*(?:include|import)\s*[<"]([^>"\n]+)[>"]', uncommented, re.M
    ):
        header = match[1]
        if BACKEND_INCLUDE.search(header):
            add("backend-include", "error", match.start(), header)
        if not path.startswith("tests/") and (
            "tests/" in header or "vendor/" in header
        ):
            add("excluded-dependency", "error", match.start(), header)
    # Adjacent literals are joined, including literals separated by comments.
    groups = []
    for pos, literal in literals:
        if groups and not code[groups[-1][2] : pos].strip():
            groups[-1][1] += literal.strip('"')
            groups[-1][2] = pos + len(literal)
        else:
            groups.append([pos, literal.strip('"'), pos + len(literal)])
    for pos, value, _ in groups:
        if SQL.search(value):
            add("sql-text", "review", pos, value[:160])
    # One finding per rule and physical line, regardless of repeated tokens.
    return list({(f["rule"], f["line"]): f for f in findings}.values())


def audit(root, paths, exceptions):
    files, findings, errors = [], [], []
    limits = rust_limits(root)
    for path in sorted(set(paths)):
        native = Path(path).suffix.lower() in SUFFIXES
        cmake = Path(path).name == "CMakeLists.txt" or path.endswith(".cmake")
        if not (native or cmake):
            continue
        role = role_for(path)
        if cmake:
            # Test build files keep the test role: fixtures may link capabilities.
            role = (
                "third-party"
                if path.startswith("vendor/")
                else "test"
                if path.startswith("tests/")
                else "build"
            )
        full = root / path
        if full.is_symlink():
            errors.append(
                f"{path}: source symlinks are not auditable; use a regular file"
            )
            continue
        data = full.read_bytes()
        source = data.decode("utf-8").replace("\r\n", "\n")
        # Git may check text out with CRLF on Windows. Content approval should
        # survive that conversion, but no other whitespace/content change.
        digest = hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()
        row = {
            "path": path,
            "role": role,
            "lines": len(source.splitlines()),
            "sha256": digest,
            "scanned": role != "third-party",
        }
        files.append(row)
        if role == "third-party":
            continue
        if role == "unclassified":
            errors.append(f"{path}: classify new native source location in ROLES")
        if cmake:
            # CMake comments are removed; scan all platform branches.
            for number, line in enumerate(source.splitlines(), 1):
                for match in BUILD_BACKEND.finditer(line.split("#", 1)[0]):
                    findings.append(
                        {
                            "path": path,
                            "line": number,
                            "rule": "backend-link",
                            "severity": "error",
                            "evidence": match[0],
                        }
                    )
        else:
            # Design-system numbers are geometry and timing tokens, not Rust policy.
            findings.extend(
                scan(path, source, None if role == "design-system" else limits)
            )
    by_path = {f["path"]: f for f in files}
    seen = set()
    for entry in exceptions:
        if set(entry) != {"path", "rule", "sha256", "reason"}:
            errors.append("exception requires exactly path, rule, sha256, reason")
            continue
        path, rule = entry["path"], entry["rule"]
        if not all(isinstance(v, str) and v.strip() for v in entry.values()):
            errors.append("exception fields must be nonempty strings")
            continue
        key = (path, rule)
        matches = [f for f in findings if (f["path"], f["rule"]) == key]
        if (
            key in seen
            or path not in by_path
            or not matches
            or by_path[path]["sha256"] != entry["sha256"]
        ):
            errors.append(
                f"{path}: stale, duplicate, or unmatched exception for {rule}; review the current file"
            )
            continue
        seen.add(key)
        for finding in matches:
            finding["exception"] = entry["reason"]
    for finding in findings:
        finding["blocking"] = (
            by_path[finding["path"]]["role"] != "test" and "exception" not in finding
        )
    findings.sort(key=lambda f: (f["path"], f["line"], f["rule"]))
    native_files = [
        f
        for f in files
        if f["role"] != "third-party" and Path(f["path"]).suffix.lower() in SUFFIXES
    ]
    summary = {
        "native_files_scanned": len(native_files),
        "native_lines_scanned": sum(f["lines"] for f in native_files),
        "files_by_role": dict(sorted(Counter(f["role"] for f in files).items())),
        "blocking_findings": sum(f["blocking"] for f in findings),
        "blocking_errors": sum(
            f["blocking"] and f["severity"] == "error" for f in findings
        ),
        "blocking_reviews": sum(
            f["blocking"] and f["severity"] == "review" for f in findings
        ),
        "excepted_findings": sum("exception" in f for f in findings),
        "inventory_errors": len(errors),
    }
    return {
        "schema_version": 1,
        "status": "fail" if errors or summary["blocking_findings"] else "pass",
        "summary": summary,
        "files": files,
        "findings": findings,
        "errors": sorted(errors),
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        config = json.loads(
            (args.root / "scripts/ci/cpp_ownership_exceptions.json").read_text(
                encoding="utf-8"
            )
        )
        if not isinstance(config, list) or not all(isinstance(e, dict) for e in config):
            raise ValueError("exceptions must be a JSON array of objects")
        paths = inventory(args.root)
        if not any(
            p.startswith("desktop/") and Path(p).suffix.lower() in SUFFIXES
            for p in paths
        ):
            raise ValueError("no desktop native sources discovered")
        report = audit(args.root, paths, config)
    except (OSError, UnicodeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"cpp-ownership: {error}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(f"C++ ownership: {report['status'].upper()}")
        print(json.dumps(report["summary"], sort_keys=True))
        for error in report["errors"]:
            print(error)
        for f in report["findings"]:
            if f["blocking"]:
                print(
                    f"{f['path']}:{f['line']}: {f['severity']} [{f['rule']}] {f['evidence']}"
                )
        print(
            "Scan coverage is not semantic compliance. Review findings require ownership review; see docs/architecture/cpp-ownership.md."
        )
    return int(report["status"] != "pass")


if __name__ == "__main__":
    sys.exit(main())
