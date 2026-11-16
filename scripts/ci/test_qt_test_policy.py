from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("qt_test_policy.py")


class QtTestPolicyTest(unittest.TestCase):
    def check(self, source):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "tests/desktop/sample_test.cpp"
            path.parent.mkdir(parents=True)
            path.write_text(source, encoding="utf-8")
            (root / "desktop").mkdir()
            (root / "desktop/peer.cpp").write_text("process.waitForReadyRead();\n")
            return subprocess.run(
                [sys.executable, str(SCRIPT), "--root", str(root)],
                capture_output=True,
                text=True,
                check=False,
            )

    def test_rejects_a_line_read_after_a_partial_ready_read(self):
        result = self.check(
            "QVERIFY(peer.waitForStarted());\n"
            "QVERIFY(peer.waitForReadyRead());\n"
            "QTRY_VERIFY(peer.canReadLine());\n"
        )
        self.assertEqual(result.returncode, 1)
        self.assertEqual(
            result.stderr.splitlines(),
            [
                "tests/desktop/sample_test.cpp:2: waitForReadyRead can return before a "
                "full line (use QTRY_VERIFY(process.canReadLine()))"
            ],
        )

    def test_accepts_a_wait_for_a_full_line(self):
        result = self.check("QTRY_VERIFY(peer.canReadLine());\n")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_a_spinner_assertion_without_a_motion_policy(self):
        result = self.check(
            "MainWindow window;\nwindow.show();\nQVERIFY(busy->isTurning());\n"
        )
        self.assertEqual(result.returncode, 1)
        self.assertEqual(
            result.stderr.splitlines(),
            [
                "tests/desktop/sample_test.cpp:3: isTurning depends on the host "
                "reduced-motion setting (set ThemeManager::setReducedMotion or the theme "
                "reducedMotion field)"
            ],
        )

    def test_accepts_a_spinner_assertion_with_an_explicit_motion_policy(self):
        result = self.check(
            "window.findChild<ThemeManager*>()->setReducedMotion(false);\n"
            "QVERIFY(busy->isTurning());\n"
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_a_file_time_set_before_the_buffered_write_reaches_the_file(self):
        result = self.check(
            "QFile file(path);\n"
            "QVERIFY(file.open(QIODevice::WriteOnly));\n"
            "file.write(sql);\n"
            "QVERIFY(file.setFileTime(old, QFileDevice::FileModificationTime));\n"
        )
        self.assertEqual(result.returncode, 1)
        self.assertEqual(
            result.stderr.splitlines(),
            [
                "tests/desktop/sample_test.cpp:4: setFileTime after an unflushed write "
                "is lost when QFile flushes its buffer (call file.flush() first)"
            ],
        )

    def test_accepts_a_file_time_set_after_a_flush(self):
        result = self.check(
            "file.write(sql);\n"
            "QVERIFY(file.flush());\n"
            "QVERIFY(file.setFileTime(old, QFileDevice::FileModificationTime));\n"
            "other.write(sql);\n"
            "QVERIFY(file.setFileTime(old, QFileDevice::FileModificationTime));\n"
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_a_completion_request_before_the_editor_has_focus(self):
        result = self.check(
            "editor->setFocus();\n"
            "auto* popup = completion->popup();\n"
            "completion->requestCompletion();\n"
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "tests/desktop/sample_test.cpp:3: requestCompletion needs the editor focus "
            "(wait with QTRY_VERIFY(editor->hasFocus()) after setFocus)",
            result.stderr,
        )

    def test_accepts_a_completion_request_after_a_focus_wait(self):
        result = self.check(
            "editor->setFocus();\n"
            "QTRY_VERIFY(editor->hasFocus());\n"
            "completion->requestCompletion();\n"
            "completion->requestCompletion();\n"
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
