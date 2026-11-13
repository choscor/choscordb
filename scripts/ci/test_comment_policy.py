from pathlib import Path
import subprocess
import tempfile
import unittest

import comment_policy


def tree(files):
    temporary = tempfile.TemporaryDirectory()
    root = Path(temporary.name)
    subprocess.run(["git", "init", "-q", str(root)], check=True)
    for relative, content in files.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        data = content if isinstance(content, bytes) else content.encode("utf-8")
        path.write_bytes(data)
    return temporary, root


class CommentPolicyTest(unittest.TestCase):
    def found(self, files):
        temporary, root = tree(files)
        with temporary:
            return comment_policy.violations(root)

    def fixed(self, relative, content):
        temporary, root = tree({relative: content})
        with temporary:
            comment_policy.fix(root)
            self.assertEqual(comment_policy.violations(root), [])
            return (root / relative).read_bytes().decode("utf-8")

    def test_reports_rust_line_doc_and_block_comments(self):
        source = (
            "//! crate\n"
            "/// item\n"
            "fn f() {\n"
            "    let x = 1; // trailing\n"
            "    /* block\n"
            "       spans */\n"
            "}\n"
        )
        self.assertEqual(
            self.found({"crates/a/src/lib.rs": source}),
            [
                "crates/a/src/lib.rs:1: comment",
                "crates/a/src/lib.rs:2: comment",
                "crates/a/src/lib.rs:4: comment",
                "crates/a/src/lib.rs:5: comment",
            ],
        )

    def test_ignores_comment_markers_inside_rust_literals_and_lifetimes(self):
        source = (
            "fn f<'a>(s: &'a str) -> [&'a str; 4] {\n"
            '    let raw = r#"// not "a" comment"#;\n'
            "    let slash = '/';\n"
            "    let quote = '\\'';\n"
            '    [s, raw, "http://x/*y*/", b"//"]\n'
            "}\n"
        )
        self.assertEqual(self.found({"crates/a/src/lib.rs": source}), [])

    def test_ignores_comment_markers_inside_cpp_literals(self):
        source = (
            "int n = 1'000;\n"
            'auto raw = R"sql(-- // /* x */)sql";\n'
            "char c = '/';\n"
            'auto url = u8"https://example.com";\n'
            "#include <a/b.h>\n"
        )
        self.assertEqual(self.found({"desktop/a.cpp": source}), [])

    def test_reports_cpp_comments(self):
        self.assertEqual(
            self.found({"desktop/a.h": "int a; // why\n/* what */ int b;\n"}),
            ["desktop/a.h:1: comment", "desktop/a.h:2: comment"],
        )

    def test_accepts_directives_with_reasons_only(self):
        source = (
            "    // SAFETY: the pointer is valid for the call\n"
            "    // because the caller owns the buffer.\n"
            "int a; // ui-ok: no token fits a platform metric\n"
            "       // not a continuation\n"
            "// perf-ok: the list has three entries\n"
            "int b = 10000; // ui-budget: equals the page limit\n"
            "// clang-format off: the header order matters\n"
            "// clang-format on\n"
            "// SAFETY:\n"
            "// ui-ok:\n"
            "int c; // NOLINT(readability-magic-numbers)\n"
            "// TODO: later\n"
        )
        self.assertEqual(
            self.found({"desktop/a.cpp": source}),
            [
                "desktop/a.cpp:4: comment",
                "desktop/a.cpp:9: comment",
                "desktop/a.cpp:10: comment",
                "desktop/a.cpp:11: comment",
                "desktop/a.cpp:12: comment",
            ],
        )

    def test_reports_python_comments_and_docstrings_but_not_strings(self):
        source = (
            "#!/usr/bin/env python3\n"
            '"""Module doc."""\n'
            "import os  # noqa: F401\n"
            "\n"
            "\n"
            "def f():\n"
            '    """Function doc."""\n'
            '    return "# not a comment"  # why\n'
        )
        self.assertEqual(
            self.found({"scripts/a.py": source}),
            [
                "scripts/a.py:2: comment",
                "scripts/a.py:3: comment",
                "scripts/a.py:7: comment",
                "scripts/a.py:8: comment",
            ],
        )

    def test_reports_hash_comments_in_build_and_ci_files(self):
        files = {
            ".github/workflows/ci.yml": (
                "# header\n"
                "on: push\n"
                'name: "a # b"\n'
                "env:\n"
                "  url: a#b\n"
                "  run: echo ${#x} can't  # why\n"
            ),
            "CMakeLists.txt": 'set(A "#x") # why\n',
            "cmake/a.cmake": "#[[ block\n]]\nset(B 1)\n",
            "deny.toml": "# why\nkey = 'a#b'\n",
        }
        self.assertEqual(
            self.found(files),
            [
                ".github/workflows/ci.yml:1: comment",
                ".github/workflows/ci.yml:6: comment",
                "CMakeLists.txt:1: comment",
                "cmake/a.cmake:1: comment",
                "deny.toml:1: comment",
            ],
        )

    def test_reports_qss_sql_and_installer_comments(self):
        files = {
            "desktop/a.qss": '/* why */\nQLabel {\n    font-family: "a//b";\n}\n',
            "examples/a.sql": "-- why\nSELECT '--' /* x */;\n",
            "scripts/a.nsi": '; why\n# why\nName "a;b"\n',
            "scripts/a.ps1": "<# why #>\n$a = '#' # why\n",
        }
        self.assertEqual(
            self.found(files),
            [
                "desktop/a.qss:1: comment",
                "examples/a.sql:1: comment",
                "examples/a.sql:2: comment",
                "scripts/a.nsi:1: comment",
                "scripts/a.nsi:2: comment",
                "scripts/a.ps1:1: comment",
                "scripts/a.ps1:2: comment",
            ],
        )

    def test_skips_vendored_build_and_unknown_files(self):
        files = {
            ".gitignore": "/build/\ntarget/\n/local-build/\n",
            "local-build/a.cpp": "// ignored by git\n",
            "crates/driver-postgres/third_party/x/src/lib.rs": "// upstream\n",
            "build/dev/a.cpp": "// generated\n",
            "target/a.rs": "// generated\n",
            "docs/a.md": "# heading\n",
            "a.json": '{"a": "//"}\n',
        }
        self.assertEqual(self.found(files), [])

    def test_fix_removes_rust_comments_and_keeps_code(self):
        source = (
            "/// item\n"
            "fn f() {\n"
            "    // lead\n"
            "    let x = 1; // trailing\n"
            "\n"
            "    // gap\n"
            "\n"
            '    let y = "//";\n'
            "}\n"
        )
        self.assertEqual(
            self.fixed("crates/a/src/lib.rs", source),
            'fn f() {\n    let x = 1;\n\n    let y = "//";\n}\n',
        )

    def test_fix_keeps_directives(self):
        source = "// SAFETY: valid\nunsafe { f() }; // why\n"
        self.assertEqual(
            self.fixed("crates/a/src/lib.rs", source),
            "// SAFETY: valid\nunsafe { f() };\n",
        )

    def test_fix_replaces_sole_python_docstring_with_pass(self):
        source = (
            "#!/usr/bin/env python3\n"
            '"""Module doc."""\n'
            "\n"
            "\n"
            "class Error(RuntimeError):\n"
            '    """Only doc."""\n'
            "\n"
            "\n"
            "def f():\n"
            '    """Doc\n'
            '    more."""\n'
            "    return 1  # why\n"
        )
        self.assertEqual(
            self.fixed("scripts/a.py", source),
            "#!/usr/bin/env python3\n"
            "\n"
            "\n"
            "class Error(RuntimeError):\n"
            "    pass\n"
            "\n"
            "\n"
            "def f():\n"
            "    return 1\n",
        )

    def test_fails_loudly_on_unterminated_literals_and_line_splices(self):
        for relative, source in {
            "crates/a/src/lib.rs": 'let s = "abc;\n// c\n',
            "desktop/a.cpp": "int a; // splice \\\nint b;\n",
        }.items():
            with self.subTest(relative=relative):
                temporary, root = tree({relative: source})
                with temporary, self.assertRaises(ValueError):
                    comment_policy.violations(root)

    def test_single_line_strings_end_at_newline_or_end_of_file(self):
        files = {
            "desktop/a.cpp": "#error don't\nint a; // why\n#error don't",
            "scripts/a.sh": "echo 'open\n# why\n",
        }
        self.assertEqual(
            self.found(files), ["desktop/a.cpp:2: comment", "scripts/a.sh:2: comment"]
        )

    def test_rejects_a_byte_order_mark(self):
        temporary, root = tree({"scripts/a.sh": "\ufeff# hidden\n"})
        with temporary, self.assertRaises(ValueError):
            comment_policy.violations(root)

    def test_reads_cmake_bracket_arguments_and_comments(self):
        source = "set(X [=[ # no ]=])\n#[=[ yes\n]=]\nset(Y a#b)\n"
        self.assertEqual(
            self.found({"cmake/a.cmake": source}), ["cmake/a.cmake:2: comment"]
        )

    def test_accepts_dependabot_version_labels_on_pinned_actions_only(self):
        sha = "3d3c42e5aac5ba805825da76410c181273ba90b1"
        source = (
            f"      - uses: actions/checkout@{sha} # v7.0.1\n"
            "      - uses: actions/setup-go@v6 # v6\n"
            "        with: a # v7\n"
        )
        self.assertEqual(
            self.found({".github/workflows/ci.yml": source}),
            [
                ".github/workflows/ci.yml:2: comment",
                ".github/workflows/ci.yml:3: comment",
            ],
        )

    def test_scans_docker_git_and_extensionless_python_files(self):
        files = {
            "examples/ssh/Dockerfile": "# why\nFROM alpine\n",
            ".gitattributes": "# why\n*.svg binary\n",
            "scripts/dev/tool": '#!/usr/bin/env python3\nX = """\n# text\n"""\n',
        }
        self.assertEqual(
            self.found(files),
            [".gitattributes:1: comment", "examples/ssh/Dockerfile:1: comment"],
        )

    def test_fix_keeps_separate_words_line_endings_and_odd_line_breaks(self):
        self.assertEqual(
            self.fixed("desktop/a.cpp", "int f() {\r\n    return/**/x; // c\r\n}\r\n"),
            "int f() {\r\n    return x;\r\n}\r\n",
        )
        self.assertEqual(
            self.fixed("scripts/a.py", 'x = "a\x85b\x0cc"\ny = 2  # c\n'),
            'x = "a\x85b\x0cc"\ny = 2\n',
        )

    def test_fix_keeps_directive_continuations(self):
        source = (
            "    // SAFETY: the buffer\n    // outlives the call.\n    g();\n\n"
            "    // stray\n    h();\n"
        )
        self.assertEqual(
            self.fixed("crates/a/src/lib.rs", "fn f() {\n" + source + "}\n"),
            "fn f() {\n    // SAFETY: the buffer\n    // outlives the call.\n"
            "    g();\n\n    h();\n}\n",
        )


if __name__ == "__main__":
    unittest.main()
