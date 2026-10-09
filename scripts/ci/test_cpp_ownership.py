"""Mutation-style ownership checks; no Qt/compiler dependencies required."""

import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import cpp_ownership as policy


class OwnershipTests(unittest.TestCase):
    def rules(self, source):
        return {f["rule"] for f in policy.scan("desktop/app/example.cpp", source)}

    def test_forbidden_capabilities_and_aliases(self):
        examples = {
            "storage": [
                "QSettings s;",
                "using File = QFile;",
                "std::ofstream out;",
                "std :: filesystem :: remove(p);",
            ],
            "network": [
                "QNetworkAccessManager manager;",
                "curl_easy_init();",
                "NSURLSession *s;",
            ],
            "database": ["QSqlQuery query;", "sqlite3_open(p, db);", "PQexec(c, sql);"],
            "process": ["QProcess::startDetached(p);", "system(cmd);"],
            "native-io": ["fopen(path, mode);", "CreateFileW(path);"],
            "serialization": ["QJsonDocument::fromJson(bytes);"],
            "domain-parsing": ["QRegularExpression pattern;"],
            "sql-text": [
                'auto s = "SELECT id FROM table";',
                'auto s = R"sql(DELETE FROM x)sql";',
                'auto s = "INSERT " /* join */ "INTO x";',
            ],
        }
        for rule, sources in examples.items():
            for source in sources:
                with self.subTest(rule=rule, source=source):
                    self.assertIn(rule, self.rules(source))

    def test_translated_text_never_drives_control_flow(self):
        for source in [
            'if (message.startsWith(tr("Font size"))) return;',
            'if (status == tr("Searching")) return;',
            'bool busy = text.contains(QObject::tr("Loading"));',
            'if (label != QCoreApplication::tr("x")) {}',
        ]:
            with self.subTest(source=source):
                self.assertIn("translated-control-flow", self.rules(source))
        self.assertNotIn(
            "translated-control-flow",
            self.rules('label->setText(tr("Loading")); setStatus(tr("Ready"));'),
        )

    def test_text_matching_uses_the_rust_filter(self):
        for source in [
            "bool m = name.contains(needle, Qt::CaseInsensitive);",
            "bool m = item->text(0).contains(query.trimmed(), Qt::CaseInsensitive);",
            "proxy->setFilterFixedString(text);",
            "proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);",
            "bool m = a.compare(b, Qt::CaseInsensitive) == 0;",
            "bool m = QString::compare(a, b, Qt::CaseInsensitive) == 0;",
            "bool m = name.toLower().contains(needle);",
            "bool m = title.toCaseFolded().startsWith(prefix);",
            "int at = row.text().toLower().indexOf(query.toLower());",
        ]:
            with self.subTest(source=source):
                self.assertIn("text-matching", self.rules(source))
        self.assertNotIn(
            "text-matching",
            self.rules("if (ids.contains(id)) {} auto lower = label.toLower();"),
        )

    def test_object_kinds_branch_on_rust_traits(self):
        for source in [
            'if (kind == "table") {}',
            'bool root = kind != QStringLiteral("connection");',
            'if (index.data(KindRole).toString() == QLatin1String("column")) {}',
            'if ("view" == kind_) {}',
            'if (kind == u"table") {}',
            'if (kind == "index"_L1) {}',
            'if (kind != "column"_s) {}',
            'if (kind == QLatin1StringView("schema")) {}',
            'if (QStringView(u"view") == kind) {}',
            'if (QStringList{"table", "view"}.contains(kind)) {}',
            'if (QSet<QString>{QStringLiteral("index")}.contains(kind)) {}',
            'if (kind.startsWith("foreign")) {}',
            'if (kind.endsWith(QLatin1String("key"))) {}',
            'if (eventKind == "table") {}',
        ]:
            with self.subTest(source=source):
                self.assertIn("object-kind-literal", self.rules(source))
        self.assertNotIn(
            "object-kind-literal",
            self.rules(
                'node.kind = "table"; if (state == "running") {} '
                'if (eventKind == "schema") {}'
            ),
        )

    def test_driver_names_branch_on_rust_policy(self):
        for source in [
            'if (driver == "mysql") {}',
            'bool tunnel = driver_->currentData() != "sqlite";',
            'if (QStringLiteral("postgres") == driver) {}',
            'if (driver == "MySQL") {}',
            'if (driver == u"PostgreSQL"_s) {}',
            'if (QStringList{"mysql", "mariadb"}.contains(driver)) {}',
            'if (driver.startsWith("postgres")) {}',
            'if (driver.compare("SQLite", Qt::CaseInsensitive) == 0) {}',
        ]:
            with self.subTest(source=source):
                self.assertIn("driver-literal", self.rules(source))
        self.assertNotIn(
            "driver-literal",
            self.rules('profile.driver = "sqlite"; QStringList names{"mysql"};'),
        )

    def test_semantic_rules_skip_design_system_and_tools(self):
        source = 'if (driver == "sqlite" || kind == "table") {} a.contains(b, Qt::CaseInsensitive);'
        for path in ["desktop/design_system/row.cpp", "desktop/tools/preview.cpp"]:
            with self.subTest(path=path):
                rules = {f["rule"] for f in policy.scan(path, source)}
                self.assertFalse(
                    rules & {"text-matching", "object-kind-literal", "driver-literal"}
                )

    def test_rust_limits_are_not_revalidated_in_cpp(self):
        for source in [
            "if (value.pageSize > limits.maxPageSize) return;",
            "if (size < EngineAdapter::queryPreferenceLimits().minPageSize) {}",
            "ok = limits.maxTimeoutSeconds >= value;",
        ]:
            with self.subTest(source=source):
                self.assertIn("limit-revalidation", self.rules(source))
        self.assertNotIn(
            "limit-revalidation",
            self.rules("spin->setRange(limits.minPageSize, limits.maxPageSize);"),
        )

    def test_copies_of_rust_limit_constants_are_reported(self):
        limits = {16 * 1024 * 1024: "MAX_SQL_DOCUMENT_BYTES", 10_000: "MAX_PAGE_SIZE"}

        def rules(source):
            return {
                f["rule"]
                for f in policy.scan("desktop/app/example.cpp", source, limits)
            }

        for source in [
            "if (bytes > 16 * 1024 * 1024) return;",
            "if (rows.size() >= 10000) return;",
            "constexpr int cap = 10'000;",
        ]:
            with self.subTest(source=source):
                self.assertIn("rust-limit-literal", rules(source))
        self.assertNotIn("rust-limit-literal", rules("timer.setInterval(60 * 1000);"))
        self.assertNotIn("rust-limit-literal", rules('auto s = "10000"; // 10000'))
        evidence = [
            f["evidence"]
            for f in policy.scan("desktop/app/x.cpp", "n = 10000;", limits)
            if f["rule"] == "rust-limit-literal"
        ]
        self.assertEqual(evidence, ["10000 (MAX_PAGE_SIZE)"])

    def test_reviewed_ui_budgets_need_a_reason(self):
        limits = {65536: "MAX_SECRET_BYTES"}

        def rules(source):
            return {
                f["rule"]
                for f in policy.scan("desktop/app/example.cpp", source, limits)
            }

        self.assertNotIn(
            "rust-limit-literal",
            rules("int chunk = 65536; // ui-budget: preview paging window\n"),
        )
        self.assertNotIn(
            "rust-limit-literal",
            rules("// ui-budget: preview paging window\nint chunk = 65536;\n"),
        )
        for source in [
            "int chunk = 65536; // ui-budget:\n",
            "// ui-budget: preview\n\nint chunk = 65536;\n",
        ]:
            with self.subTest(source=source):
                self.assertIn("rust-limit-literal", rules(source))

    def test_rust_limit_constants_are_collected_from_crates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "crates/core/src"
            source.mkdir(parents=True)
            (source / "lib.rs").write_text(
                "pub const MAX_BYTES: usize = 16 * 1024 * 1024;\n"
                "pub(crate) const DEFAULT_ROWS: u32 = 10_000;\n"
                "pub const MIN_PORT: u16 = 1;\n"
                'pub const NAME: &str = "x";\n',
                encoding="utf-8",
            )
            self.assertEqual(
                policy.rust_limits(root),
                {16 * 1024 * 1024: "MAX_BYTES", 10_000: "DEFAULT_ROWS"},
            )

    def test_comments_strings_and_ui_operations_do_not_trigger_capabilities(self):
        source = """// QFile f;
/* QSettings settings; */
auto text = "QSqlQuery QNetworkAccessManager";
auto raw = R"tag(QFile f; " // QSettings)tag";
auto c = '\\"';
QObject::connect(button, signal, receiver, slot);
connect(button, signal, receiver, slot);
dialog.open();
QFileDialog::getOpenFileName(parent);
QFileInfo(path).fileName();
QDir(root).filePath(name);
"""
        self.assertEqual(self.rules(source), set())

    def test_multiline_tokens_inactive_platform_and_line_numbers(self):
        source = "// comment\n#if WINDOWS\nQNet\\\nworkAccessManager n;\n#endif\n"
        findings = policy.scan("desktop/app/x.mm", source)
        self.assertEqual([(f["rule"], f["line"]) for f in findings], [("network", 3)])
        self.assertIn("storage", self.rules("#define FILE_TYPE QSettings\n"))
        self.assertIn("storage", self.rules("QFile /* comment */ file;"))

    def test_numeric_separators_do_not_hide_code(self):
        self.assertIn("storage", self.rules("auto n = 1'000; QFile f; auto m = 2'000;"))
        self.assertIn("process", self.rules("QProcess process;"))

    def test_crlf_keeps_exception_valid(self):
        path = "desktop/app/a.cpp"
        source = "QFile f;\n"
        exception = {
            "path": path,
            "rule": "storage",
            "sha256": hashlib.sha256(source.encode()).hexdigest(),
            "reason": "Reviewed fixture.",
        }
        self.assertEqual(
            self.audit({path: source.replace("\n", "\r\n")}, [exception])["status"],
            "pass",
        )

    def test_headers_and_test_dependencies(self):
        for header in [
            "QtSql/QSqlQuery",
            "sqlite3.h",
            "curl/curl.h",
            "fstream",
            "sys/socket.h",
        ]:
            self.assertIn("backend-include", self.rules(f"#include <{header}>"))
        self.assertIn("excluded-dependency", self.rules('#include "tests/helper.h"'))
        self.assertEqual(self.rules("// #include <fstream>"), set())

    def audit(self, contents, exceptions=()):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for path, source in contents.items():
                target = root / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(source, encoding="utf-8")
            return policy.audit(root, contents, exceptions)

    def test_new_extensions_locations_and_build_dependencies(self):
        report = self.audit(
            {
                "desktop/bridge/new.ipp": "QSettings s;",
                "backend/new.cppm": "int x;",
                "cmake/Extra.cmake": "target_link_libraries(app Qt6::Network)",
            }
        )
        self.assertEqual(report["status"], "fail")
        self.assertEqual(report["summary"]["native_files_scanned"], 2)
        self.assertTrue(report["errors"])
        self.assertEqual(
            {f["rule"] for f in report["findings"]}, {"storage", "backend-link"}
        )

    def test_test_build_files_may_link_fixture_capabilities(self):
        report = self.audit(
            {
                "tests/Tests.cmake": "target_link_libraries(fixture Qt6::Network)",
                "CMakeLists.txt": "target_link_libraries(app Qt6::Network)",
            }
        )
        blocking = {f["path"] for f in report["findings"] if f["blocking"]}
        self.assertEqual(blocking, {"CMakeLists.txt"})

    def test_tests_are_visible_and_tools_are_not_exempt(self):
        report = self.audit(
            {
                "tests/fixture.cpp": "QFile f;",
                "vendor/lib.cpp": "QFile f;",
                "desktop/tools/tool.cpp": "QFile f;",
            }
        )
        self.assertEqual(report["summary"]["blocking_findings"], 1)
        self.assertEqual(len(report["findings"]), 2)
        self.assertFalse(
            next(f for f in report["files"] if f["role"] == "third-party")["scanned"]
        )

    def test_exception_is_specific_and_content_pinned(self):
        path, source = "desktop/app/a.cpp", "QFile f;"
        exception = {
            "path": path,
            "rule": "storage",
            "sha256": hashlib.sha256(source.encode()).hexdigest(),
            "reason": "Test reviewed resource reader.",
        }
        report = self.audit({path: source}, [exception])
        self.assertEqual(report["status"], "pass")
        self.assertEqual(report["summary"]["excepted_findings"], 1)
        for contents, entries in [
            ({path: source + " QSettings s;"}, [exception]),
            ({path: "int x;"}, [exception]),
            ({}, [exception]),
            ({path: source}, [exception, exception]),
            ({path: source}, [{**exception, "reason": ""}]),
        ]:
            with self.subTest(contents=contents, entries=entries):
                report = self.audit(contents, entries)
                self.assertEqual(report["status"], "fail")
                self.assertTrue(report["errors"])
        report = self.audit({path: source, "desktop/app/b.cpp": source}, [exception])
        self.assertEqual(report["summary"]["blocking_findings"], 1)

    def test_stable_order_and_no_timestamp_or_absolute_paths(self):
        files = {"desktop/app/z.cpp": "QFile f;", "desktop/app/a.cpp": "QSettings s;"}
        self.assertEqual(
            self.audit(files), self.audit(dict(reversed(list(files.items()))))
        )

    def test_inventory_covers_tracked_and_untracked_but_not_build_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", directory], check=True)
            (root / ".gitignore").write_text("build/\n")
            (root / "tracked.cpp").write_text("int x;")
            subprocess.run(["git", "add", "."], cwd=root, check=True)
            (root / "new.mm").write_text("int y;")
            (root / "build").mkdir()
            (root / "build/generated.cpp").write_text("int z;")
            self.assertEqual(
                policy.inventory(root), [".gitignore", "new.mm", "tracked.cpp"]
            )
            (root / "tracked.cpp").unlink()
            self.assertNotIn("tracked.cpp", policy.inventory(root))

    def test_ignored_owned_sources_are_discovered(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", directory], check=True)
            (root / ".gitignore").write_text("desktop/\n")
            (root / "desktop").mkdir()
            (root / "desktop/hidden.hpp").write_text("QSettings settings;")
            self.assertIn("desktop/hidden.hpp", policy.inventory(root))

    def test_cli_json_exit_codes_and_missing_configuration(self):
        import sys

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", directory], check=True)
            (root / "desktop/app").mkdir(parents=True)
            source = root / "desktop/app/example.cpp"
            source.write_text("int x;")
            config = root / "scripts/ci/cpp_ownership_exceptions.json"
            config.parent.mkdir(parents=True)
            config.write_text("[]")
            command = [
                sys.executable,
                str(Path(policy.__file__)),
                "--root",
                directory,
                "--json",
            ]
            passed = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(passed.returncode, 0, passed.stderr)
            self.assertEqual(json.loads(passed.stdout)["status"], "pass")
            source.write_text("QSettings settings;")
            failed = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(failed.returncode, 1)
            self.assertEqual(json.loads(failed.stdout)["status"], "fail")
            config.unlink()
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 2)

    def test_symlink_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "real").write_text("QFile f;")
            (root / "link.cpp").symlink_to(root / "real")
            report = policy.audit(root, ["link.cpp"], [])
            self.assertEqual(report["status"], "fail")
            self.assertIn("symlink", report["errors"][0])

    def test_migrated_storage_sites_have_no_cpp_storage_capability(self):
        paths = [
            "desktop/app/updater_windows_linux.cpp",
            "desktop/tools/preview/preview_capture.cpp",
            "desktop/tools/preview/preview_window.cpp",
        ]
        findings = []
        for path in paths:
            findings.extend(policy.scan(path, (policy.ROOT / path).read_text()))
        self.assertEqual([f for f in findings if f["rule"] == "storage"], [])

    def test_exception_file_schema_is_json_array(self):
        data = json.loads(
            (policy.ROOT / "scripts/ci/cpp_ownership_exceptions.json").read_text()
        )
        self.assertIsInstance(data, list)
        for entry in data:
            self.assertEqual(set(entry), {"path", "rule", "sha256", "reason"})


if __name__ == "__main__":
    unittest.main()
