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

    def test_rejects_design_system_dependency_on_feature_layers(self):
        temporary, root = self.fixture("")
        with temporary:
            (root / "desktop/design_system/control.cpp").write_text(
                '#include "widgets/sql_editor/sql_editor.h"\n'
                '#include "app/main_window.h"\n'
                '#include "bridge/engine_adapter.h"\n'
                '#include "models/history_model.h"\n'
                '#include "tools/preview/preview_window.h"\n',
                encoding="utf-8",
            )
            found = ui_policy.violations(root)
        self.assertEqual(len(found), 5)
        self.assertTrue(all("design-system dependency" in item for item in found))

    def test_preview_metrics_do_not_exempt_other_tools(self):
        temporary, root = self.fixture("")
        with temporary:
            preview = root / "desktop/tools/preview"
            preview.mkdir(parents=True)
            (preview / "preview_window.cpp").write_text(
                "widget.resize(640, 480);\n", encoding="utf-8"
            )
            (preview.parent / "other.cpp").write_text(
                "widget.resize(640, 480);\n", encoding="utf-8"
            )
            found = ui_policy.violations(root)
        self.assertEqual(len(found), 1)
        self.assertIn("tools/other.cpp", found[0])


if __name__ == "__main__":
    unittest.main()
