"""Tests for the native dead and duplicated code inventory."""

from pathlib import Path
import tempfile
import unittest
from unittest import mock

import source_inventory


class SourceInventoryTest(unittest.TestCase):
    def run_inventory(self, files, exceptions=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for relative, text in files.items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
            with mock.patch.object(source_inventory, "EXCEPTIONS", exceptions or {}):
                return [
                    (item["path"], item["symbol"], item["message"].split(" (")[0])
                    for item in source_inventory.findings(root, check_lists=False)
                ]

    def base(self, header, source="", extra=None):
        files = {
            "CMakeLists.txt": "add_library(app desktop/app/widget.cpp)\n",
            "desktop/app/widget.h": header,
            "desktop/app/widget.cpp": '#include "widget.h"\n' + source,
        }
        files.update(extra or {})
        return files

    def test_accepts_used_members_overrides_and_connected_signals(self):
        header = (
            "class Widget : public QWidget {\n"
            "    Q_OBJECT\n"
            "public:\n"
            "    explicit Widget(QWidget* parent = nullptr);\n"
            "    int count() const { return count_; }\n"
            "    void refresh();\n"
            "signals:\n"
            "    void changed();\n"
            "protected:\n"
            "    void paintEvent(QPaintEvent* event) override;\n"
            "private:\n"
            "    int count_ = 0;\n"
            "};\n"
        )
        source = (
            "void Widget::refresh() { if (count() > 0) emit changed(); }\n"
            "void use(Widget* w) { QObject::connect(w, &Widget::changed, w, &Widget::refresh); }\n"
        )
        self.assertEqual(self.run_inventory(self.base(header, source)), [])

    def test_rejects_uncalled_members_and_unconnected_signals(self):
        header = (
            "class Widget : public QWidget {\n"
            "public:\n"
            "    bool hasFilters() const { return true; }\n"
            "    void legacy();\n"
            "signals:\n"
            "    void failed();\n"
            "};\n"
        )
        source = "void Widget::legacy() { emit failed(); }\n"
        self.assertEqual(
            self.run_inventory(self.base(header, source)),
            [
                (
                    "desktop/app/widget.h",
                    "hasFilters",
                    "member function is never called",
                ),
                ("desktop/app/widget.h", "legacy", "member function is never called"),
                ("desktop/app/widget.h", "failed", "signal is never connected"),
            ],
        )

    def test_rejects_unlisted_units_and_unincluded_headers(self):
        files = self.base(
            "class Widget {};\n",
            extra={
                "desktop/app/orphan.cpp": "int orphan() { return 1; }\n",
                "desktop/app/orphan.h": "int orphan();\n",
            },
        )
        self.assertEqual(
            self.run_inventory(files),
            [
                (
                    "desktop/app/orphan.cpp",
                    "orphan.cpp",
                    "translation unit is not named by CMake",
                ),
                ("desktop/app/orphan.h", "orphan.h", "header is never included"),
            ],
        )

    def test_rejects_copied_anonymous_helpers(self):
        helper = (
            "namespace {\n"
            "QString text(const rust::String& value) {\n"
            "    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));\n"
            "}\n"
            "} // namespace\n"
        )
        files = self.base(
            "class Widget {};\n",
            helper,
            {
                "CMakeLists.txt": "add_library(app desktop/app/widget.cpp desktop/app/other.cpp)\n",
                "desktop/app/other.cpp": '#include "widget.h"\n' + helper,
            },
        )
        found = self.run_inventory(files)
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0][:2], ("desktop/app/widget.cpp", "text"))

    def test_exceptions_suppress_and_stale_exceptions_fail(self):
        header = "class Widget {\npublic:\n    void seam();\n};\n"
        files = self.base(header, "void Widget::seam() {}\n")
        self.assertEqual(
            self.run_inventory(files, {"desktop/app/widget.h::seam": "test seam"}), []
        )
        self.assertEqual(
            self.run_inventory(
                self.base("class Widget {};\n"), {"desktop/app/widget.h::gone": "old"}
            ),
            [("desktop/app/widget.h", "gone", "stale source-inventory exception")],
        )

    def test_repository_component_lists_resolve(self):
        stale = [
            item
            for item in source_inventory.findings()
            if "ui_consistency list" in item["message"]
        ]
        self.assertEqual(
            [item["symbol"] for item in stale],
            [],
            "remove names from scripts/ci/ui_consistency.py that no longer exist",
        )


if __name__ == "__main__":
    unittest.main()
