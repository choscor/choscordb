import contextlib
import io
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

import tla

ROOT = Path(__file__).resolve().parents[2]


def tools_available():
    jar = ROOT / "build/ci/tla/tla2tools.jar"
    return (
        shutil.which("java") is not None
        and jar.is_file()
        and tla.sha256(jar) == tla.JAR_SHA256
    )


class ValueTests(unittest.TestCase):
    def test_values_are_written_as_tla_literals(self):
        self.assertEqual(tla.tla_value(True), "TRUE")
        self.assertEqual(tla.tla_value(3), "3")
        self.assertEqual(tla.tla_value('a "b"'), '"a \\"b\\""')
        self.assertEqual(tla.tla_value(["x", False]), '<<"x", FALSE>>')
        self.assertEqual(
            tla.tla_value({"slot": "sql", "kind": "QueryState"}),
            '[kind |-> "QueryState", slot |-> "sql"]',
        )

    def test_an_unknown_value_type_fails_loudly(self):
        with self.assertRaises(tla.CheckFailed):
            tla.tla_value(1.5)


class TableTests(unittest.TestCase):
    TABLE = {
        "states": ["Idle", "Busy"],
        "events": ["Go"],
        "transitions": [["Idle", "Go", "Busy"]],
        "presentation": {
            "Idle": {
                "tone": "Neutral",
                "busy": False,
                "body": "None",
                "pane": "Unchanged",
                "toast": False,
            }
        },
        "read_sessions": [["Open", "Catalog"]],
    }

    def test_each_table_is_checked_against_its_spec_operator(self):
        module = tla.table_module("Machine", self.TABLE)
        self.assertIn("EXTENDS Machine, TLC", module)
        for name in ("States", "Events", "Transitions", "Presentation", "ReadSessions"):
            with self.subTest(name=name):
                self.assertIn(f"Table{name} == ", module)
                self.assertIn(f"ASSUME Assert(Table{name} = {name},", module)
        self.assertIn(
            '<<"Idle", "Neutral", FALSE, "None", "Unchanged", FALSE>>', module
        )

    def test_a_table_without_transitions_is_rejected(self):
        with self.assertRaisesRegex(tla.CheckFailed, "no transitions"):
            tla.table_module("Machine", {"states": [], "events": []})


class TraceTests(unittest.TestCase):
    def write(self, text):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "run.ndjson"
        path.write_text(text, encoding="utf-8")
        return path

    def test_trace_lines_are_json_objects(self):
        path = self.write('{"kind": "Connected"}\n\n{"kind": "Disconnected"}\n')
        self.assertEqual(
            tla.trace_lines(path), [{"kind": "Connected"}, {"kind": "Disconnected"}]
        )
        module = tla.trace_module(tla.trace_lines(path))
        self.assertIn("EXTENDS ConnectionTrace", module)
        self.assertIn(
            'Recorded == <<[kind |-> "Connected"], [kind |-> "Disconnected"]>>', module
        )

    def test_invalid_and_empty_traces_fail(self):
        for text, message in [
            ("[1]\n", "must be a JSON object"),
            ("{nope\n", "run.ndjson:1"),
            ("\n", "no trace lines"),
        ]:
            with self.subTest(text=text):
                with self.assertRaisesRegex(tla.CheckFailed, message):
                    tla.trace_lines(self.write(text))

    def test_missing_trace_directory_names_the_command_that_writes_it(self):
        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.object(tla, "require_tools", return_value=("java", "jar")):
                with self.assertRaisesRegex(tla.CheckFailed, "cargo test --workspace"):
                    tla.traces(Path(directory), Path(directory))


class ToolTests(unittest.TestCase):
    def test_missing_java_or_jar_exits_with_an_install_hint(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for which, message in [(None, "java"), ("/usr/bin/java", "tla.py install")]:
                with self.subTest(which=which):
                    stderr = io.StringIO()
                    with (
                        mock.patch.object(tla.shutil, "which", return_value=which),
                        contextlib.redirect_stderr(stderr),
                    ):
                        self.assertEqual(
                            tla.main(["check"], root=root), tla.TOOL_MISSING
                        )
                    self.assertIn(message, stderr.getvalue())

    def test_a_jar_with_another_digest_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            jar = root / "build/ci/tla/tla2tools.jar"
            jar.parent.mkdir(parents=True)
            jar.write_bytes(b"not tlc")
            with mock.patch.object(tla.shutil, "which", return_value="/usr/bin/java"):
                with self.assertRaisesRegex(tla.ToolMissing, "does not match"):
                    tla.require_tools(root)


class RepositoryTests(unittest.TestCase):
    def test_each_spec_family_has_a_config_and_a_transition_table(self):
        configs = tla.family_configs(ROOT)
        families = {config.parent.name for config in configs}
        self.assertTrue(
            {
                "query",
                "connection",
                "panel",
                "grid_edit",
                "export",
                "updater",
                "profile_test",
            }
            <= families
        )
        for config in configs:
            with self.subTest(config=config.name):
                table = json.loads((config.parent / "transitions.json").read_text())
                self.assertTrue(table["transitions"])
                self.assertTrue(config.with_suffix(".tla").is_file())

    @unittest.skipUnless(
        tools_available(), "java and the pinned TLC jar are not installed"
    )
    def test_a_transition_table_that_differs_from_the_spec_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shutil.copytree(ROOT / "specs" / "export", root / "specs" / "export")
            table_path = root / "specs/export/transitions.json"
            table = json.loads(table_path.read_text())
            table["transitions"] = table["transitions"][1:]
            table_path.write_text(json.dumps(table))
            jar = root / "build/ci/tla/tla2tools.jar"
            jar.parent.mkdir(parents=True)
            shutil.copy2(ROOT / "build/ci/tla/tla2tools.jar", jar)
            with contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(tla.CheckFailed, "Export.cfg"):
                    tla.check(root)


if __name__ == "__main__":
    unittest.main()
