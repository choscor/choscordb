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

    def test_scans_objcxx_and_additional_cpp_suffixes(self):
        temporary, root = self.fixture("")
        with temporary:
            (root / "desktop/widgets/native.mm").write_text(
                "widget.resize(640, 480);\n", encoding="utf-8"
            )
            (root / "desktop/widgets/extra.hpp").write_text(
                'widget.setStyleSheet("color: red");\n', encoding="utf-8"
            )
            found = ui_policy.violations(root)
        self.assertEqual(len(found), 2)

    def test_rejects_literal_metrics_in_any_argument_position(self):
        temporary, root = self.fixture(
            "layout->setContentsMargins(0, 8, 0, 0);\n"
            "view->setColumnWidth(1, 500);\n"
            "header->setDefaultSectionSize(62);\n"
            "auto icon = design::themedIcon(Icon::Table, color, 16);\n"
            "auto pixmap = icon.pixmap(30, 30);\n"
            "button->setIconSize(QSize(metrics.icon, 14));\n"
            "layout->setContentsMargins(0, spacing(Spacing::Two), 0, 0);\n"
            "view->setColumnWidth(1, metrics.tableColumn * 3);\n"
        )
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(len(found), 6, found)
        self.assertTrue(all("literal presentation metric" in item for item in found))

    def test_rejects_rgb_colors_menus_standard_icons_and_stock_buttons(self):
        temporary, root = self.fixture(
            "QBrush brush(QColor(255, 0, 0));\n"
            "painter.setPen(Qt::gray);\n"
            "menu->popup(point);\n"
            "icon = style()->standardIcon(QStyle::SP_BrowserReload);\n"
            "#ifdef Q_OS_MACOS\n"
            "quit->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));\n"
            "#endif\n"
            "new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);\n"
            "design::popupContextMenu(*menu, point);\n"
        )
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(len(found), 5, found)

    def test_rejects_paint_geometry_literals(self):
        temporary, root = self.fixture(
            "painter->drawText(bounds.adjusted(14, 8, -14, -33), text);\n"
            "painter->drawRoundedRect(badge, 4, 4);\n"
            "painter->drawRect(bounds.adjusted(1, 1, -1, -1));\n"
            "painter->drawRoundedRect(badge, radius, radius);\n"
        )
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(len(found), 2, found)

    def test_rejects_property_values_the_design_system_does_not_style(self):
        temporary, root = self.fixture(
            'label->setProperty("state", "disconnected");\n'
            'label->setProperty("state", text.isEmpty() ? "" : "warning");\n'
            'label->setProperty("designRole", "description");\n'
        )
        with temporary:
            (root / "desktop/design_system/label.qss").write_text(
                'QLabel[state="warning"] {}\nQLabel[designRole=description] {}\n',
                encoding="utf-8",
            )
            found = ui_policy.violations(root)
        self.assertEqual(len(found), 1, found)
        self.assertIn("state='disconnected'", found[0])

    def test_reviewed_marker_exempts_audit_rules(self):
        temporary, root = self.fixture(
            "// ui-ok: native platform glyph has no design icon\n"
            "icon = style()->standardIcon(QStyle::SP_TitleBarMenuButton);\n"
            "view->setColumnWidth(1, 500); // ui-ok: specimen width\n"
        )
        with temporary:
            self.assertEqual(ui_policy.violations(root), [])


if __name__ == "__main__":
    unittest.main()
