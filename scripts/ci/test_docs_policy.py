"""Tests for the documentation policy: links, STE sentence length, and doc updates."""

from pathlib import Path
import subprocess
import tempfile
import unittest

import docs_policy

MAP = (
    "# Documentation\n\n"
    "```docs-map\n"
    "crates/driver-*/** -> user-guide/connections.md\n"
    "scripts/release/** -> docs/release-macos.md, docs/release-windows-linux.md\n"
    "```\n"
)


class DocsPolicyTest(unittest.TestCase):
    def tree(self, files):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        files = {
            "docs/README.md": MAP,
            "docs/release-macos.md": "# macOS\n",
            "docs/release-windows-linux.md": "# Windows and Linux\n",
            "user-guide/connections.md": "# Connections\n\n## Add a profile\n",
            "crates/driver-sqlite/src/lib.rs": "",
            "scripts/release/stage.py": "",
            **files,
        }
        for name, text in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        return root

    def rules(self, files):
        return [item["rule"] for item in docs_policy.violations(self.tree(files))]

    def test_rejects_missing_link_targets_paths_and_anchors(self):
        found = docs_policy.violations(
            self.tree(
                {
                    "README.md": "See [the guide](user-guide/missing.md).\n"
                    "Read `docs/gone.md` first.\n"
                    "See [profiles](user-guide/connections.md#remove-a-profile).\n"
                }
            )
        )
        self.assertEqual(
            [(item["path"], item["line"], item["rule"]) for item in found],
            [
                ("README.md", 1, "broken link"),
                ("README.md", 2, "broken link"),
                ("README.md", 3, "broken link"),
            ],
        )

    def test_accepts_existing_links_anchors_and_external_urls(self):
        self.assertEqual(
            self.rules(
                {
                    "README.md": "See [profiles](user-guide/connections.md#add-a-profile).\n"
                    "Read `docs/release-macos.md` and [Qt](https://www.qt.io).\n",
                    "docs/build.md": "# Build\n\nSee [the index](README.md).\n",
                }
            ),
            [],
        )

    def test_rejects_long_descriptive_sentence_and_long_step(self):
        long = " ".join(["word"] * 26) + "."
        step = " ".join(["word"] * 21) + "."
        found = docs_policy.violations(
            self.tree(
                {
                    "docs/build.md": f"# Build\n\n{long}\n\n1. {step}\n- {step}\n",
                }
            )
        )
        self.assertEqual(
            [(item["line"], item["rule"]) for item in found],
            [
                (3, "long sentence"),
                (5, "long step"),
                (6, "long step"),
            ],
        )

    def test_accepts_sentences_at_the_limits_and_skips_code_tables_and_headings(self):
        descriptive = " ".join(["word"] * 25) + "."
        step = " ".join(["word"] * 20) + "."
        many = " ".join(["word"] * 40)
        self.assertEqual(
            self.rules(
                {
                    "docs/build.md": f"# {many}\n\n{descriptive} {descriptive}\n\n"
                    f"1. {step}\n\n```sh\n{many}\n```\n\n| {many} |\n|---|\n\n"
                    f"Run `{many}` now.\n"
                    f"Read [{' '.join(['word'] * 3)}](https://example.org/{'a/' * 30}).\n",
                }
            ),
            [],
        )

    def test_sentence_spans_lines_and_reports_its_first_line(self):
        found = docs_policy.violations(
            self.tree(
                {
                    "docs/build.md": "# Build\n\n"
                    + " ".join(["word"] * 13)
                    + "\n"
                    + " ".join(["word"] * 13)
                    + ".\n"
                }
            )
        )
        self.assertEqual(
            [(item["line"], item["rule"]) for item in found], [(3, "long sentence")]
        )

    def test_marker_with_reason_exempts_line_and_marker_without_reason_fails(self):
        long = " ".join(["word"] * 26) + "."
        self.assertEqual(
            self.rules(
                {
                    "docs/build.md": f"# Build\n\n<!-- docs-ok: quoted license text -->\n{long}\n\n"
                    f"{long} <!-- docs-ok: legal name of a standard -->\n",
                }
            ),
            [],
        )
        self.assertEqual(
            self.rules({"docs/build.md": f"# Build\n\n<!-- docs-ok: -->\n{long}\n"}),
            ["marker without reason", "long sentence"],
        )

    def test_scans_root_guides_and_ignores_other_markdown(self):
        long = " ".join(["word"] * 26) + "."
        self.assertEqual(
            self.rules(
                {
                    "CLAUDE.md": f"{long}\n",
                    "CONTRIBUTING.md": f"{long}\n",
                    "crates/driver-sqlite/README.md": f"{long}\n",
                }
            ),
            ["long sentence", "long sentence"],
        )

    def test_map_globs_must_match_files_and_name_existing_docs(self):
        found = docs_policy.violations(
            self.tree(
                {
                    "docs/README.md": MAP.replace(
                        "```\n", "desktop/missing/** -> user-guide/gone.md\n```\n", 1
                    )
                }
            )
        )
        self.assertEqual(
            [item["rule"] for item in found],
            ["missing map owner", "unmatched map glob"],
        )

    def test_map_owner_outside_docs_must_exist(self):
        found = docs_policy.violations(
            self.tree(
                {
                    "docs/README.md": MAP.replace(
                        "```\n", "CMakeLists.txt -> BUILDING.md\n```\n", 1
                    ),
                    "CMakeLists.txt": "",
                }
            )
        )
        self.assertEqual([item["rule"] for item in found], ["missing map owner"])

    def test_fenced_examples_may_name_new_doc_paths(self):
        self.assertEqual(
            self.rules({"docs/build.md": "# Build\n\n```sh\nmkdir docs/new\n```\n"}),
            [],
        )


class ChangedDocsTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.write("docs/README.md", MAP)
        for name in [
            "docs/release-macos.md",
            "docs/release-windows-linux.md",
            "user-guide/connections.md",
            "crates/driver-sqlite/src/lib.rs",
            "scripts/release/stage.py",
            "desktop/app/main.cpp",
        ]:
            self.write(name, "start\n")
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.email", "ci@example.invalid")
        self.git("config", "user.name", "CI")
        self.commit("base")
        self.git("checkout", "-q", "-b", "change")

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def git(self, *args):
        subprocess.run(["git", *args], cwd=self.root, check=True, capture_output=True)

    def commit(self, message):
        self.git("add", "-A")
        self.git("commit", "-q", "-m", message)

    def changed(self, pr_body=""):
        return docs_policy.changed_violations(self.root, "main", pr_body)

    def test_mapped_code_change_without_doc_change_fails(self):
        self.write("crates/driver-sqlite/src/lib.rs", "changed\n")
        self.commit("feat(sqlite): change behavior")
        found = self.changed()
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0]["rule"], "doc not updated")
        self.assertIn("user-guide/connections.md", found[0]["message"])

    def test_owning_doc_change_or_unmapped_change_passes(self):
        self.write("crates/driver-sqlite/src/lib.rs", "changed\n")
        self.write("user-guide/connections.md", "updated\n")
        self.write("scripts/release/stage.py", "changed\n")
        self.write("docs/release-windows-linux.md", "updated\n")
        self.write("desktop/app/main.cpp", "changed\n")
        self.commit("feat: change with docs")
        self.assertEqual(self.changed(), [])

    def test_pr_body_escape_needs_a_reason(self):
        self.write("crates/driver-sqlite/src/lib.rs", "changed\n")
        self.commit("refactor(sqlite): rename a local")
        self.assertEqual(
            self.changed("Summary\n\nDocs: none needed, internal rename\n"), []
        )
        self.assertEqual(len(self.changed("Docs: none needed, \n")), 1)
        self.assertEqual(len(self.changed("Docs: none needed\n")), 1)
        self.assertEqual(
            len(self.changed("Docs: none needed, <!-- Name the updated page. -->\n")), 1
        )

    def test_commit_trailer_escape(self):
        self.write("crates/driver-sqlite/src/lib.rs", "changed\n")
        self.commit(
            "refactor(sqlite): rename\n\nBody.\n\nDocs: none needed, no behavior change"
        )
        self.assertEqual(self.changed(), [])


if __name__ == "__main__":
    unittest.main()
