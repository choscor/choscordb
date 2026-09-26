"""Behavior checks for the desktop visual ownership audit."""

from pathlib import Path
import tempfile
import unittest

import ui_consistency


class UiConsistencyTest(unittest.TestCase):
    def fixture(self):
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        (root / "desktop/app").mkdir(parents=True)
        (root / "desktop/widgets/preferences_dialog").mkdir(parents=True)
        (root / "desktop/design_system/button").mkdir(parents=True)
        return temporary, root

    def test_reports_each_screen_and_measured_component_coverage(self):
        temporary, root = self.fixture()
        with temporary:
            (root / "desktop/app/main_window_ui.cpp").write_text(
                '#include "design_system/button/button.h"\n'
                'auto* save = new design::Button("Save", parent);\n',
                encoding="utf-8",
            )
            (
                root / "desktop/widgets/preferences_dialog/preferences_dialog.cpp"
            ).write_text("auto* field = new QLineEdit(parent);\n", encoding="utf-8")
            report = ui_consistency.audit(root)
        self.assertEqual(report["screen_count"], 2)
        self.assertEqual(report["component_coverage_percent"], 100.0)
        self.assertEqual(report["construction_sites"], 2)
        self.assertEqual(report["screens"]["app/main_window"]["explicit"], 1)
        self.assertEqual(report["screens"]["widgets/preferences_dialog"]["stock"], 1)

    def test_rejects_feature_owned_visual_constants_and_qss(self):
        temporary, root = self.fixture()
        with temporary:
            (root / "desktop/app/main_window_ui.cpp").write_text(
                'QFont label("Arial");\nlabel.setPixelSize(12);\n'
                "auto fill = QColor(1, 2, 3);\n",
                encoding="utf-8",
            )
            (root / "desktop/widgets/preferences_dialog/local.qss").write_text(
                "QLabel { color: red; }\n", encoding="utf-8"
            )
            problems = ui_consistency.audit(root)["violations"]
        self.assertEqual(len(problems), 5)
        self.assertTrue(any("font family" in item for item in problems))
        self.assertTrue(any("font size" in item for item in problems))
        self.assertTrue(any("color value" in item for item in problems))
        self.assertTrue(any("feature-owned QSS" in item for item in problems))

    def test_allows_user_font_and_shared_semantic_tokens(self):
        temporary, root = self.fixture()
        with temporary:
            (root / "desktop/app/main_window_ui.cpp").write_text(
                "QFont label(userFamily);\n"
                "label.setFont(design::resolveTypography(design::TypographyRole::Ui));\n"
                "label.setPalette(design::applicationPalette(theme));\n",
                encoding="utf-8",
            )
            self.assertEqual(ui_consistency.audit(root)["violations"], [])

    def test_rejects_direct_message_box_but_not_enum_usage(self):
        temporary, root = self.fixture()
        with temporary:
            (root / "desktop/app/main_window_ui.cpp").write_text(
                'QMessageBox box(QMessageBox::Warning, "Failed");\n'
                "auto buttons = QMessageBox::Ok | QMessageBox::Cancel;\n",
                encoding="utf-8",
            )
            problems = ui_consistency.audit(root)["violations"]
        self.assertEqual(len(problems), 1)
        self.assertIn("shared confirmation dialog", problems[0])

    def test_reports_unclassified_visual_control_with_site(self):
        temporary, root = self.fixture()
        with temporary:
            (root / "desktop/app/main_window_ui.cpp").write_text(
                "auto* calendar = new QCalendarWidget(parent);\n", encoding="utf-8"
            )
            report = ui_consistency.audit(root)
        self.assertEqual(report["component_coverage_percent"], 0.0)
        self.assertEqual(report["screens"]["app/main_window"]["unclassified"], 1)
        self.assertIn("QCalendarWidget", report["violations"][0])

    def test_hex_tokens_are_owned_by_foundations(self):
        temporary, root = self.fixture()
        with temporary:
            (root / "desktop/design_system/button/button.cpp").write_text(
                'auto fill = QStringLiteral("#123456");\n', encoding="utf-8"
            )
            colors = root / "desktop/design_system/colors"
            colors.mkdir()
            (colors / "colors.cpp").write_text(
                'auto fill = QColor("#123456");\n', encoding="utf-8"
            )
            problems = ui_consistency.audit(root)["violations"]
        self.assertEqual(len(problems), 1)
        self.assertIn("design_system/button/button.cpp:1: local hex color", problems[0])

    def test_component_font_sizes_are_owned_by_typography_foundation(self):
        temporary, root = self.fixture()
        with temporary:
            (root / "desktop/design_system/button/button.cpp").write_text(
                "font.setPixelSize(10);\n", encoding="utf-8"
            )
            fonts = root / "desktop/design_system/fonts"
            fonts.mkdir()
            (fonts / "fonts.cpp").write_text(
                "font.setPixelSize(10);\n", encoding="utf-8"
            )
            problems = ui_consistency.audit(root)["violations"]
        self.assertEqual(len(problems), 1)
        self.assertIn("design_system/button/button.cpp:1: local font size", problems[0])

    def test_reports_composites_and_zero_leaf_groups_without_inflating_coverage(self):
        temporary, root = self.fixture()
        with temporary:
            (root / "desktop/app/main_window_ui.cpp").write_text(
                "auto* save = new design::Button(parent);\n"
                "auto* unknown = new design::UnreviewedControl(parent);\n",
                encoding="utf-8",
            )
            (
                root / "desktop/widgets/preferences_dialog/preferences_dialog.cpp"
            ).write_text(
                "auto* sections = new design::DialogSections(parent);\n",
                encoding="utf-8",
            )
            editor = root / "desktop/widgets/sql_editor"
            editor.mkdir()
            (editor / "sql_editor.cpp").write_text(
                "class SqlEditor {};\n", encoding="utf-8"
            )
            report = ui_consistency.audit(root)
        self.assertEqual(report["construction_sites"], 2)
        self.assertEqual(report["screen_count"], 1)
        self.assertEqual(report["source_group_count"], 3)
        self.assertEqual(report["component_coverage_percent"], 50.0)
        self.assertEqual(
            report["screens"]["widgets/preferences_dialog"]["composite"], 1
        )
        self.assertIn("widgets/sql_editor", report["zero_site_groups"])
        self.assertTrue(
            any("UnreviewedControl" in item for item in report["violations"])
        )

    def test_rejects_other_literal_fonts_and_colors(self):
        temporary, root = self.fixture()
        with temporary:
            (root / "desktop/app/main_window_ui.cpp").write_text(
                'font.setFamily("Arial");\n'
                "auto color = QColor(Qt::red);\n"
                "palette.setColor(QPalette::Window, Qt::blue);\n",
                encoding="utf-8",
            )
            (root / "desktop/design_system/button/button_style.qss").write_text(
                "QPushButton {\ncolor: red;\nbackground: rgb(1,2,3);\n}\n",
                encoding="utf-8",
            )
            problems = ui_consistency.audit(root)["violations"]
        self.assertEqual(len(problems), 5)

    def test_rejects_unclassified_slider_and_dial(self):
        temporary, root = self.fixture()
        with temporary:
            (root / "desktop/app/main_window_ui.cpp").write_text(
                "auto* slider = new QSlider(parent);\n"
                "auto* dial = new QDial(parent);\n",
                encoding="utf-8",
            )
            report = ui_consistency.audit(root)
        self.assertEqual(report["screens"]["app/main_window"]["unclassified"], 2)
        self.assertEqual(report["component_coverage_percent"], 0.0)

    def test_checks_app_controller_and_excludes_structural_containers(self):
        temporary, root = self.fixture()
        with temporary:
            (root / "desktop/app/main_window_ui.cpp").write_text(
                "auto* host = new QWidget(parent);\n"
                "auto* label = new QLabel(parent);\n",
                encoding="utf-8",
            )
            (root / "desktop/app/editor_preferences.cpp").write_text(
                'auto text = QColor("#123456");\n', encoding="utf-8"
            )
            report = ui_consistency.audit(root)
        self.assertEqual(report["construction_sites"], 1)
        self.assertEqual(report["component_coverage_percent"], 100.0)
        self.assertTrue(
            any("editor_preferences.cpp:1" in item for item in report["violations"])
        )


if __name__ == "__main__":
    unittest.main()
