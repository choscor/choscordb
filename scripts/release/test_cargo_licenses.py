import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import cargo_licenses


class CargoLicenseTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.packages = self.root / "packages"
        self.fallback = self.root / "fallback"
        self.packages.mkdir()
        self.fallback.mkdir()
        self.metadata = self.root / "metadata.json"

    def package(self, name, license_text=True):
        root = self.packages / name
        root.mkdir()
        manifest = root / "Cargo.toml"
        manifest.write_text("[package]\n")
        if license_text:
            (root / "LICENSE-MIT").write_text(name + " license")
        return {"id": name, "name": name, "version": "1.0", "license": "MIT",
                "source": "registry+https://example.invalid", "manifest_path": str(manifest)}

    def fixture(self, missing_license=False):
        app = self.package("app")
        dependency = self.package("dependency", not missing_license)
        unused = self.package("unused")
        document = {"workspace_members": ["app"], "packages": [app, dependency, unused],
                    "resolve": {"nodes": [
                        {"id": "app", "deps": [{"pkg": "dependency", "dep_kinds": [{"kind": None}]}]},
                        {"id": "dependency", "deps": []}, {"id": "unused", "deps": []}]}}
        self.metadata.write_text(json.dumps(document))

    def test_collects_only_normal_closure_with_deterministic_index(self):
        self.fixture()
        first = cargo_licenses.collect(self.metadata, self.fallback, self.root / "first", "app")
        second = cargo_licenses.collect(self.metadata, self.fallback, self.root / "second", "app")
        self.assertEqual((first / "index.json").read_bytes(), (second / "index.json").read_bytes())
        index = json.loads((first / "index.json").read_text())
        self.assertEqual([row["name"] for row in index["packages"]], ["dependency"])
        row = index["packages"][0]
        data = (first / "dependency@1.0/LICENSE-MIT").read_bytes()
        self.assertEqual(row["files"][0]["sha256"], hashlib.sha256(data).hexdigest())
        with self.assertRaises(FileExistsError):
            cargo_licenses.collect(self.metadata, self.fallback, first, "app")

    def test_missing_package_license_requires_coordinate_bound_fallback(self):
        self.fixture(missing_license=True)
        with self.assertRaisesRegex(ValueError, "unavailable"):
            cargo_licenses.collect(self.metadata, self.fallback, self.root / "output", "app")
        override = self.fallback / "dependency@1.0"
        override.mkdir()
        license_data = b"verified fallback"
        (override / "LICENSE").write_bytes(license_data)
        (override / "source.json").write_text(
            json.dumps({"coordinate": "dependency@1.0",
                        "url": "https://example.invalid/license",
                        "files": {"LICENSE": hashlib.sha256(license_data).hexdigest()}}))
        output = cargo_licenses.collect(self.metadata, self.fallback, self.root / "output", "app")
        self.assertEqual(json.loads((output / "index.json").read_text())["packages"][0]["origin"],
                         "verified-fallback")
        (override / "LICENSE").write_text("tampered fallback")
        with self.assertRaisesRegex(ValueError, "fallback digest"):
            cargo_licenses.collect(self.metadata, self.fallback, self.root / "tampered", "app")

    def test_empty_and_symlinked_license_sources_fail_closed(self):
        self.fixture()
        license_path = self.packages / "dependency/LICENSE-MIT"
        license_path.write_bytes(b"")
        with self.assertRaisesRegex(ValueError, "Empty"):
            cargo_licenses.collect(self.metadata, self.fallback, self.root / "empty", "app")
        license_path.unlink()
        outside = self.root / "outside"
        outside.write_text("license")
        license_path.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, "unavailable"):
            cargo_licenses.collect(self.metadata, self.fallback, self.root / "link", "app")


if __name__ == "__main__":
    unittest.main()
