"""Tests for the desktop large-data performance policy."""

from pathlib import Path
import tempfile
import unittest

import perf_policy


class PerfPolicyTest(unittest.TestCase):
    def findings(self, source, relative="desktop/app/sample.cpp"):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / relative
            path.parent.mkdir(parents=True)
            path.write_text(source, encoding="utf-8")
            return perf_policy.violations(root)

    def rules(self, source, relative="desktop/app/sample.cpp"):
        return [item["rule"] for item in self.findings(source, relative)]

    def test_rejects_materialized_selection_and_content_sizing(self):
        rules = self.rules(
            "void f() {\n"
            "  for (auto i : view->selectionModel()->selectedIndexes()) {}\n"
            "  view->resizeColumnsToContents();\n"
            "  header->setSectionResizeMode(QHeaderView::ResizeToContents);\n"
            "}\n"
        )
        self.assertEqual(
            rules,
            [
                "materialized selection",
                "content-sized columns or rows",
                "content-sized columns or rows",
            ],
        )

    def test_reviewed_marker_exempts_line_or_next_line(self):
        self.assertEqual(
            self.rules(
                "void f() {\n"
                "  view->resizeColumnsToContents(); // perf-ok: bounded metadata\n"
                "  // perf-ok: at most 64 rows\n"
                "  view->resizeRowsToContents();\n"
                "}\n"
            ),
            [],
        )

    def test_marker_requires_reason(self):
        self.assertEqual(
            self.rules("void f() { view->resizeColumnsToContents(); // perf-ok:\n}\n"),
            ["content-sized columns or rows"],
        )

    def test_ignores_comments_and_strings(self):
        self.assertEqual(
            self.rules(
                "// selectedIndexes() is slow\n"
                'const char* text = "resizeColumnsToContents()";\n'
            ),
            [],
        )

    def test_rejects_undebounced_filter_and_tree_patterns(self):
        rules = self.rules(
            "void f() {\n"
            "  connect(edit, &QLineEdit::textChanged, proxy,"
            " &QSortFilterProxyModel::setFilterFixedString);\n"
            "  tree->setUniformRowHeights(false);\n"
            "  tree->expandAll();\n"
            "}\n"
        )
        self.assertEqual(
            rules,
            [
                "undebounced view filter",
                "non-uniform tree rows",
                "whole-tree expansion",
            ],
        )

    def test_rejects_zero_timer_per_model_signal(self):
        source = (
            "void f() {\n"
            "  connect(model, &QAbstractItemModel::rowsInserted, this, [this] {\n"
            "    QTimer::singleShot(0, this, [this] { relayout(); });\n"
            "  });\n"
            "  connect(button, &QPushButton::clicked, this, [this] {\n"
            "    QTimer::singleShot(0, this, [this] { relayout(); });\n"
            "  });\n"
            "}\n"
        )
        self.assertEqual(self.rules(source), ["uncoalesced model-signal work"])

    def test_rejects_expensive_work_in_hot_functions(self):
        source = (
            "QVariant Model::data(const QModelIndex& index, int role) const {\n"
            "  return EngineAdapter::foreignKeyValueFilterable(cell(index));\n"
            "}\n"
            "void Delegate::paint(QPainter* p, const QStyleOptionViewItem& o,"
            " const QModelIndex& i) const {\n"
            "  QSvgRenderer renderer(path);\n"
            '  QIcon icon(":/icons/link.svg");\n'
            "}\n"
            "void Delegate::refresh() {\n"
            '  QIcon icon(":/icons/link.svg");\n'
            "}\n"
        )
        self.assertEqual(
            self.rules(source),
            [
                "bridge call on a paint/data path",
                "SVG parsing on a paint/data path",
                "image file load on a paint/data path (use cached design::themedIcon)",
            ],
        )

    def test_cached_themed_icons_are_allowed_in_paint(self):
        source = (
            "void Delegate::paint(QPainter* p) const {\n"
            "  auto icon = themedIcon(Icon::Link, color, 16);\n"
            "}\n"
        )
        self.assertEqual(
            self.rules(source, "desktop/design_system/table/table_style.cpp"), []
        )

    def test_rejects_linear_sibling_search_in_parent(self):
        source = (
            "QModelIndex Model::indexFor(const Node* node) const {\n"
            "  auto it = std::find_if(siblings.begin(), siblings.end(), match);\n"
            "  return createIndex(int(it - siblings.begin()), 0, node);\n"
            "}\n"
            "void Model::rebuild() {\n"
            "  auto it = std::find_if(nodes.begin(), nodes.end(), match);\n"
            "}\n"
        )
        self.assertEqual(
            self.rules(source, "desktop/models/sample.cpp"),
            ["linear search on a paint/data path"],
        )

    def test_full_viewport_hover_repaint_in_views(self):
        self.assertEqual(
            self.rules(
                "void View::hover() { viewport()->update(); }\n"
                "void View::other() { viewport()->update(visualRect(index)); }\n",
                "desktop/design_system/tree/view.cpp",
            ),
            ["full-viewport hover repaint"],
        )


if __name__ == "__main__":
    unittest.main()
