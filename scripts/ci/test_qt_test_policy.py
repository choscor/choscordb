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


if __name__ == "__main__":
    unittest.main()
