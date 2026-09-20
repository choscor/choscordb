from pathlib import Path
import tempfile
import unittest

from artifact_privacy import verify_payload_privacy


class PayloadPrivacyTest(unittest.TestCase):
    def test_rejects_builder_paths_without_echoing_them(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / "ChoscorDB.app"
            app.mkdir()
            binary = app / "choscordb"
            for encoding in ("utf-8", "utf-16-le", "utf-16-be"):
                with self.subTest(encoding=encoding):
                    binary.write_bytes(
                        b"binary\0"
                        + "/Users/PrivateBuilder/project/main.cpp".encode(encoding)
                    )
                    with self.assertRaisesRegex(
                        ValueError, "local build path"
                    ) as error:
                        verify_payload_privacy(app, ["/Users/PrivateBuilder"])
                    self.assertNotIn("PrivateBuilder", str(error.exception))

    def test_portable_diagnostics_and_unrelated_paths_are_allowed(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory)
            (app / "resource").write_bytes(
                b"/choscordb/src/main.cpp\0/System/Library/Frameworks"
            )
            verify_payload_privacy(
                app, ["/Users/PrivateBuilder", "/private/build-source"]
            )

    def test_checks_resources_as_well_as_executables(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory)
            (app / "Resources").mkdir()
            (app / "Resources/metadata.json").write_text(
                '{"source":"/private/build-source/file"}'
            )
            with self.assertRaisesRegex(ValueError, "local build path"):
                verify_payload_privacy(app, ["/private/build-source"])


if __name__ == "__main__":
    unittest.main()
