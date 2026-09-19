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


if __name__ == "__main__":
    unittest.main()
