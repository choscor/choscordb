"""Tests for the QSS source and resource policy."""

from pathlib import Path
import tempfile
import unittest

import qss_policy


class QssPolicyTest(unittest.TestCase):
    def test_rejects_inline_and_unregistered_one_line_rules(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            component = root / "desktop/design_system/button"
            component.mkdir(parents=True)
            resources = root / "desktop/resources"
            resources.mkdir(parents=True)
            (resources / "styles.qrc").write_text("<RCC/>", encoding="utf-8")
            (component / "button.cpp").write_text(
                'return QStringLiteral("QPushButton { min-height: 10px; }");\n',
                encoding="utf-8",
            )
            (component / "ButtonStyle.qss").write_text(
                "QPushButton { color: red; }\n", encoding="utf-8"
            )
            found = qss_policy.violations(root)
            self.assertTrue(any("inline QSS" in item for item in found))
            self.assertTrue(any("incorrect styles.qrc alias" in item for item in found))
            self.assertTrue(any("separate lines" in item for item in found))
            self.assertTrue(any("snake_case" in item for item in found))

    def test_accepts_colocated_formatted_resource(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            component = root / "desktop/design_system/button"
            component.mkdir(parents=True)
            resources = root / "desktop/resources"
            resources.mkdir(parents=True)
            (resources / "styles.qrc").write_text(
                '<RCC><qresource prefix="/styles"><file alias="button/button_style.qss">'
                "../design_system/button/button_style.qss</file></qresource></RCC>",
                encoding="utf-8",
            )
            (component / "button_style.qss").write_text(
                "QPushButton {\n    color: red;\n}\n", encoding="utf-8"
            )
            self.assertEqual(qss_policy.violations(root), [])

    def test_rejects_wrong_alias_and_multiple_declarations(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            component = root / "desktop/design_system/button"
            component.mkdir(parents=True)
            resources = root / "desktop/resources"
            resources.mkdir(parents=True)
            (resources / "styles.qrc").write_text(
                '<RCC><qresource prefix="/styles"><file alias="button/wrong.qss">'
                "../design_system/button/button_style.qss</file></qresource></RCC>",
                encoding="utf-8",
            )
            (component / "button.cpp").write_text(
                'return loadStyleSheet(QStringLiteral("button/button_style.qss"));\n',
                encoding="utf-8",
            )
            (component / "button_style.qss").write_text(
                "QPushButton {\n  color: red; padding: 2px;\n}\n", encoding="utf-8"
            )
            found = qss_policy.violations(root)
            self.assertTrue(any("missing QSS alias" in item for item in found))
            self.assertTrue(any("incorrect styles.qrc alias" in item for item in found))
            self.assertTrue(any("separate lines" in item for item in found))

    def test_rejects_adjacent_inline_strings_and_wrong_resource_prefix(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            component = root / "desktop/design_system/button"
            component.mkdir(parents=True)
            resources = root / "desktop/resources"
            resources.mkdir(parents=True)
            (resources / "styles.qrc").write_text(
                '<RCC><qresource prefix="/styles"/>'
                '<qresource prefix="/other"><file alias="button/button_style.qss">'
                "../design_system/button/button_style.qss</file></qresource></RCC>",
                encoding="utf-8",
            )
            (component / "button.cpp").write_text(
                'return "QPushButton {" " min-height: 10px;" "}";\n',
                encoding="utf-8",
            )
            (component / "button_style.qss").write_text(
                "QPushButton {\n  color: red;\n}\n", encoding="utf-8"
            )
            found = qss_policy.violations(root)
            self.assertTrue(any("inline QSS" in item for item in found))
            self.assertTrue(any("incorrect styles.qrc alias" in item for item in found))

    def _component(self, root, files):
        design = root / "desktop/design_system"
        resources = root / "desktop/resources"
        resources.mkdir(parents=True)
        entries = "".join(
            f'<file alias="{name}">../design_system/{name}</file>' for name in files
        )
        (resources / "styles.qrc").write_text(
            f'<RCC><qresource prefix="/styles">{entries}</qresource></RCC>',
            encoding="utf-8",
        )
        for name, content in files.items():
            path = design / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")

    def test_rejects_positional_placeholders_and_literal_sizes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._component(
                root,
                {
                    "button/button_style_sheet.qss": "QPushButton {\n"
                    "  color: %1;\n"
                    "  font-size: 12px;\n"
                    "  border-top-left-radius: 5px;\n"
                    "  border-radius: 0;\n"
                    "}\n"
                },
            )
            found = qss_policy.violations(root)
            self.assertTrue(any("positional placeholder" in item for item in found))
            self.assertTrue(any("literal font-size" in item for item in found))
            self.assertTrue(
                any("literal border-top-left-radius" in item for item in found)
            )
            self.assertFalse(any("literal border-radius" in item for item in found))

    def test_accepts_named_tokens(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._component(
                root,
                {
                    "button/button_style_sheet.qss": "QPushButton {\n"
                    "  color: @fg;\n"
                    "  font-size: @font-dense;\n"
                    "  border-radius: @radius-md;\n"
                    "}\n"
                },
            )
            self.assertEqual(qss_policy.violations(root), [])

    def test_rejects_selector_property_defined_in_two_cascade_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._component(
                root,
                {
                    "field/field_style_sheet.qss": "QLineEdit, QComboBox {\n"
                    "  min-height: @control;\n"
                    "}\n",
                    "select/select_style_sheet.qss": "QComboBox {\n"
                    "  min-height: @control;\n"
                    "  padding-right: @space-1;\n"
                    "}\n",
                    "select/popup_style_sheet.qss": "QComboBox {\n"
                    "  min-height: @control;\n"
                    "}\n",
                },
            )
            style = root / "desktop/design_system/style"
            style.mkdir(parents=True)
            (style / "stylesheet.cpp").write_text(
                "constexpr std::array sharedCascade = {\n"
                '    u"field/field_style_sheet.qss",\n'
                '    u"select/select_style_sheet.qss",\n'
                "};\n",
                encoding="utf-8",
            )
            found = qss_policy.violations(root)
            self.assertEqual(len(found), 1, found)
            self.assertIn("QComboBox { min-height }", found[0])
            self.assertIn("select/select_style_sheet.qss", found[0])


if __name__ == "__main__":
    unittest.main()
