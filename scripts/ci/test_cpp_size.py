"""Exercise the file-size gate through its command-line interface."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("cpp_size.py")


class CppSizeTest(unittest.TestCase):
    def check_files(self, files):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, contents in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(contents)
            return subprocess.run(
                [sys.executable, str(SCRIPT), "--root", str(root)],
                capture_output=True,
                text=True,
                check=False,
            )

    def test_limit_is_inclusive_and_counts_unterminated_last_line(self):
        result = self.check_files({"desktop/limit.cpp": b"// line\n" * 999 + b"x"})
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.check_files({"desktop/over.cpp": b"// line\n" * 1000 + b"x"})
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("desktop/over.cpp:1001:", result.stderr)
        self.assertIn("1001 lines exceeds 1000", result.stderr)

    def test_headers_tests_and_objcpp_are_checked_in_sorted_order(self):
        result = self.check_files(
            {
                "tests/z.hpp": b"\r\n" * 1001,
                "desktop/a.mm": b"\n" * 1002,
                "desktop/b.h": b"// comment\n" * 1001,
            }
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        lines = result.stderr.splitlines()
        self.assertEqual(len(lines), 3)
        self.assertTrue(lines[0].startswith("desktop/a.mm:1001:"))
        self.assertTrue(lines[1].startswith("desktop/b.h:1001:"))
        self.assertTrue(lines[2].startswith("tests/z.hpp:1001:"))

    def test_generated_build_output_and_non_cpp_assets_are_out_of_scope(self):
        result = self.check_files(
            {
                "build/generated.cpp": b"x\n" * 2000,
                "target/bridge.h": b"x\n" * 2000,
                "desktop/resources/icon.svg": b"x\n" * 2000,
                "desktop/empty.cpp": b"",
            }
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
