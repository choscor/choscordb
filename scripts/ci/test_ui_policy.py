"""Tests for the desktop design-system source and resource boundary."""

from pathlib import Path
import tempfile
import unittest

import ui_policy


class UiPolicyTest(unittest.TestCase):
    def fixture(self, source):
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        (root / "desktop/widgets").mkdir(parents=True)
        (root / "desktop/design_system").mkdir(parents=True)
        icons = root / "desktop/resources/icons"
        icons.mkdir(parents=True)
        for name in (*ui_policy.REQUIRED_ICONS, "LICENSE-LUCIDE", "SOURCE-LUCIDE.json"):
            (icons / name).write_text("asset", encoding="utf-8")
        (root / "desktop/resources/resources.qrc").write_text(
            " ".join(ui_policy.REQUIRED_ICONS), encoding="utf-8"
        )
        (root / "desktop/widgets/sample.cpp").write_text(source, encoding="utf-8")
        return temporary, root

    def test_rejects_screen_owned_color_and_stylesheet(self):
        temporary, root = self.fixture(
            'auto color = QColor("#abcdef");\nwidget.setStyleSheet("color: red");\n'
            "widget.resize(640, 480);\n"
        )
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(len(found), 3)

    def test_accepts_token_consumers_with_complete_resources(self):
        temporary, root = self.fixture("widget.setPalette(theme.palette());\n")
        with temporary:
            self.assertEqual(ui_policy.violations(root), [])


if __name__ == "__main__":
    unittest.main()
