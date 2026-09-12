import tomllib
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class RustPolicyTests(unittest.TestCase):
    def test_workspace_denies_first_party_unsafe_and_debug_placeholders(self):
        workspace = tomllib.loads((ROOT / "Cargo.toml").read_text())

        self.assertNotIn("rust-version", workspace["workspace"]["package"])
        self.assertEqual(workspace["workspace"]["lints"]["rust"]["unsafe_code"], "deny")
        self.assertEqual(workspace["workspace"]["lints"]["rust"]["warnings"], "deny")
        for lint in ("dbg_macro", "todo", "unimplemented"):
            self.assertEqual(workspace["workspace"]["lints"]["clippy"][lint], "deny")

    def test_every_first_party_crate_inherits_workspace_lints(self):
        manifests = sorted((ROOT / "crates").glob("*/Cargo.toml"))
        self.assertTrue(manifests)
        for manifest in manifests:
            with self.subTest(manifest=manifest.relative_to(ROOT)):
                crate = tomllib.loads(manifest.read_text())
                self.assertEqual(crate.get("lints"), {"workspace": True})


if __name__ == "__main__":
    unittest.main()
