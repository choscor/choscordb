"""Tests for the desktop design-system source and resource boundary."""

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import ui_policy

LUCIDE = (
    '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" '
    'viewBox="0 0 24 24" stroke="currentColor" stroke-width="2"><path d="M5 12h14"/></svg>'
)
BRAND = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128"><path d="M0 0h1"/></svg>'


def sha256(text):
    return hashlib.sha256(text.encode()).hexdigest()


class UiPolicyTest(unittest.TestCase):
    def fixture(self, source):
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        (root / "desktop/widgets").mkdir(parents=True)
        (root / "desktop/design_system").mkdir(parents=True)
        icons = root / "desktop/resources/icons"
        icons.mkdir(parents=True)
        brands = {"app-mark.svg", "mysql.svg"}
        lucide = {}
        for name in ui_policy.REQUIRED_ICONS:
            text = BRAND if name in brands else LUCIDE.replace("M5", "M" + name[:1])
            (icons / name).write_text(text, encoding="utf-8")
            if name not in brands:
                lucide[name] = sha256(text)
        (icons / "LICENSE-LUCIDE").write_text("license", encoding="utf-8")
        (icons / "SOURCE-LUCIDE.json").write_text(
            json.dumps(
                {
                    "icons": lucide,
                    "adaptations": {"app-mark.svg": {"sha256": sha256(BRAND)}},
                }
            ),
            encoding="utf-8",
        )
        (icons / "SOURCE-DATABASE-LOGOS.json").write_text(
            json.dumps({"icons": {"mysql.svg": {"sha256": sha256(BRAND)}}}),
            encoding="utf-8",
        )
        self.write_qrc(root, sorted(ui_policy.REQUIRED_ICONS))
        (root / "desktop/widgets/sample.cpp").write_text(source, encoding="utf-8")
        return temporary, root

    def write_qrc(self, root, names):
        (root / "desktop/resources/resources.qrc").write_text(
            '<RCC><qresource prefix="/icons">'
            + "".join(f'<file alias="{name}">icons/{name}</file>' for name in names)
            + "</qresource></RCC>",
            encoding="utf-8",
        )

    def test_rejects_changed_lucide_bytes(self):
        temporary, root = self.fixture("")
        with temporary:
            play = root / "desktop/resources/icons/play.svg"
            play.write_bytes(play.read_bytes() + b" ")
            found = ui_policy.violations(root)
        self.assertEqual(len(found), 1, found)
        self.assertIn("play.svg: differs from its SOURCE-LUCIDE.json record", found[0])

    def test_rejects_unrecorded_icon_and_record_without_file(self):
        temporary, root = self.fixture("")
        with temporary:
            icons = root / "desktop/resources/icons"
            (icons / "drawn.svg").write_text(LUCIDE, encoding="utf-8")
            (icons / "plus.svg").unlink()
            self.write_qrc(root, [*sorted(ui_policy.REQUIRED_ICONS), "drawn.svg"])
            found = ui_policy.violations(root)
        self.assertIn(
            "desktop/resources/icons/drawn.svg: not recorded in SOURCE-LUCIDE.json",
            found,
        )
        self.assertIn(
            "desktop/resources/icons/plus.svg: provenance record has no file", found
        )
        self.assertIn("desktop/resources/icons/plus.svg: missing required icon", found)

    def test_reports_provenance_without_required_fields(self):
        temporary, root = self.fixture("")
        with temporary:
            (root / "desktop/resources/icons/SOURCE-DATABASE-LOGOS.json").write_text(
                '{"icons": {"mysql.svg": {}}}', encoding="utf-8"
            )
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                "desktop/resources/icons/SOURCE-*.json: missing or invalid icon provenance"
            ],
        )

    def test_rejects_qrc_icon_without_provenance(self):
        temporary, root = self.fixture("")
        with temporary:
            self.write_qrc(root, [*sorted(ui_policy.REQUIRED_ICONS), "ghost.svg"])
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            ["desktop/resources/icons/ghost.svg: qrc icon has no provenance record"],
        )

    def test_rejects_raster_icons(self):
        temporary, root = self.fixture("")
        with temporary:
            (root / "desktop/resources/icons/mysql.png").write_bytes(b"raster")
            found = ui_policy.violations(root)
        self.assertEqual(
            found, ["desktop/resources/icons/mysql.png: icon assets must be SVG"]
        )

    def test_rejects_non_square_or_sized_brand_logos(self):
        temporary, root = self.fixture("")
        with temporary:
            icons = root / "desktop/resources/icons"
            logos = json.loads((icons / "SOURCE-DATABASE-LOGOS.json").read_text())
            for name, text in [
                ("wide.svg", BRAND.replace("0 0 128 128", "0 0 432 445")),
                ("sized.svg", BRAND.replace("<svg ", '<svg width="128" height="128" ')),
            ]:
                (icons / name).write_text(text, encoding="utf-8")
                logos["icons"][name] = {"sha256": sha256(text)}
            (icons / "SOURCE-DATABASE-LOGOS.json").write_text(json.dumps(logos))
            self.write_qrc(
                root, [*sorted(ui_policy.REQUIRED_ICONS), "wide.svg", "sized.svg"]
            )
            found = ui_policy.violations(root)
        self.assertEqual(
            sorted(found),
            [
                "desktop/resources/icons/sized.svg: brand logo must not set width or height",
                "desktop/resources/icons/wide.svg: brand logo needs a square viewBox",
            ],
        )

    def test_rejects_literal_icon_sizes_in_the_design_system(self):
        temporary, root = self.fixture("")
        with temporary:
            design = root / "desktop/design_system"
            (design / "dock.cpp").write_text(
                "button->setIconSize(QSize(14, 14));\n"
                "auto icon = themedIcon(role, color, 24);\n"
                "label->setPixmap(icon.pixmap(QSize(24, 24), scale));\n"
                "label->setPixmap(icon->pixmap(16));\n"
                "button->setIconSize(QSize(size, size));\n"
                "item->setIcon(themedIcon(driverIcon(driver),\n"
                "                         colors.fgMuted, 16));\n"
                "auto token = themedIcon(role, color, dimension(Dimension::Icon));\n",
                encoding="utf-8",
            )
            (design / "metrics").mkdir()
            (design / "metrics/metrics.cpp").write_text(
                "auto icon = themedIcon(role, color, 16);\n", encoding="utf-8"
            )
            found = ui_policy.violations(root)
        self.assertEqual(len(found), 5, found)
        self.assertIn(
            "design_system/dock.cpp:6: literal icon size in themedIcon()", found[4]
        )
        self.assertTrue(all("literal icon size" in item for item in found))
        self.assertTrue(all("design_system/dock.cpp" in item for item in found))

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
        self.assertEqual(sum("literal icon size" in item for item in found), 3, found)
        self.assertEqual(
            sum("literal presentation metric" in item for item in found), 3, found
        )

    def test_rejects_rgb_colors_menus_standard_icons_everywhere_and_stock_buttons(self):
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
        (root / "desktop/widgets/native.mm").write_text(
            "check->setIcon(window.style()->standardIcon(QStyle::SP_BrowserReload));\n",
            encoding="utf-8",
        )
        with temporary:
            found = ui_policy.violations(root)
        # The macOS block and the Objective-C++ file get no standard-icon exemption.
        self.assertEqual(len(found), 7, found)
        self.assertEqual(sum("Qt standard icon" in item for item in found), 3, found)

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
