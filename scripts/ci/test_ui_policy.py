import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import ui_policy

PHOSPHOR = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" '
    'fill="currentColor"><path d="M5 12h14"/></svg>'
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
        phosphor = {}
        for name in ui_policy.REQUIRED_ICONS:
            text = BRAND if name in brands else PHOSPHOR.replace("M5", "M" + name[:1])
            (icons / name).write_text(text, encoding="utf-8")
            if name not in brands:
                phosphor[name] = sha256(text)
        (icons / "LICENSE-PHOSPHOR").write_text("license", encoding="utf-8")
        (icons / "SOURCE-PHOSPHOR.json").write_text(
            json.dumps(
                {
                    "icons": phosphor,
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

    def test_rejects_ad_hoc_panel_state_labels(self):
        source = (
            'empty->setText(tr("No saved queries yet."));\n'
            'auto* failed = new QLabel(tr("Could not load pins"), this);\n'
            'status->setProperty("state", "error");\n'
        )
        temporary, root = self.fixture(source)
        with temporary:
            found = ui_policy.violations(root)
        rule = "ad-hoc panel state label (use design::createPanelStateText and showPanelState)"
        self.assertEqual(
            found,
            [f"desktop/widgets/sample.cpp:{line}: {rule}" for line in (1, 2, 3)],
        )

    def test_accepts_panel_state_component(self):
        source = (
            "auto* empty = design::createPanelStateText({}, this);\n"
            "showPanelState(empty, shown);\n"
            'timeout->setSpecialValueText(tr("No timeout"));\n'
        )
        temporary, root = self.fixture(source)
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(found, [])

    def test_rejects_changed_phosphor_bytes(self):
        temporary, root = self.fixture("")
        with temporary:
            play = root / "desktop/resources/icons/play.svg"
            play.write_bytes(play.read_bytes() + b" ")
            found = ui_policy.violations(root)
        self.assertEqual(len(found), 1, found)
        self.assertIn(
            "play.svg: differs from its SOURCE-PHOSPHOR.json record", found[0]
        )

    def test_rejects_retired_lucide_provenance(self):
        temporary, root = self.fixture("")
        with temporary:
            icons = root / "desktop/resources/icons"
            (icons / "LICENSE-LUCIDE").write_text("license", encoding="utf-8")
            (icons / "SOURCE-LUCIDE.json").write_text("{}", encoding="utf-8")
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                "desktop/resources/icons/LICENSE-LUCIDE: unknown icon license or provenance",
                "desktop/resources/icons/SOURCE-LUCIDE.json: unknown icon license or provenance",
            ],
        )

    def test_requires_the_phosphor_license(self):
        temporary, root = self.fixture("")
        with temporary:
            (root / "desktop/resources/icons/LICENSE-PHOSPHOR").unlink()
            found = ui_policy.violations(root)
        self.assertEqual(
            found, ["desktop/resources/icons/LICENSE-PHOSPHOR: missing attribution"]
        )

    def test_rejects_unrecorded_icon_and_record_without_file(self):
        temporary, root = self.fixture("")
        with temporary:
            icons = root / "desktop/resources/icons"
            (icons / "drawn.svg").write_text(PHOSPHOR, encoding="utf-8")
            (icons / "plus.svg").unlink()
            self.write_qrc(root, [*sorted(ui_policy.REQUIRED_ICONS), "drawn.svg"])
            found = ui_policy.violations(root)
        self.assertIn(
            "desktop/resources/icons/drawn.svg: not recorded in SOURCE-PHOSPHOR.json",
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

    def test_rejects_literal_animation_durations_everywhere(self):
        temporary, root = self.fixture("timer->setDuration(250);\n")
        with temporary:
            (root / "desktop/design_system/toast.cpp").write_text(
                "fade_->setDuration(180);\n"
                "fade_->setDuration(motionSpec(Motion::Popup, reduced).durationMs);\n",
                encoding="utf-8",
            )
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                "desktop/design_system/toast.cpp:1: literal animation duration "
                "(use motionSpec)",
                "desktop/widgets/sample.cpp:1: literal animation duration "
                "(use motionSpec)",
            ],
        )

    def test_rejects_instruction_placeholders(self):
        temporary, root = self.fixture(
            'path_->setPlaceholderText(tr("Choose the known-hosts file to update"));\n'
            'host->setPlaceholderText(tr("Enter the host name"));\n'
            'host->setPlaceholderText(tr("bastion.example.com"));\n'
            "preview_->setPlaceholderText(\n"
            '    saved ? tr("Saved private key")\n'
            '          : tr("Paste a complete private key."));\n'
            'filter->setPlaceholderText(tr("Filter objects…"));\n'
        )
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                "desktop/widgets/sample.cpp:1: instruction placeholder "
                "(a placeholder shows only an example value)",
                "desktop/widgets/sample.cpp:2: instruction placeholder "
                "(a placeholder shows only an example value)",
                "desktop/widgets/sample.cpp:4: instruction placeholder "
                "(a placeholder shows only an example value)",
            ],
        )

    def test_rejects_instruction_placeholders_in_the_gallery(self):
        temporary, root = self.fixture("")
        with temporary:
            preview = root / "desktop/tools/preview"
            preview.mkdir(parents=True)
            (preview / "gallery.cpp").write_text(
                'field->setPlaceholderText("Enter a value…");\n'
                'field->setPlaceholderText("warehouse_01");\n',
                encoding="utf-8",
            )
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                "desktop/tools/preview/gallery.cpp:1: instruction placeholder "
                "(a placeholder shows only an example value)",
            ],
        )

    def test_rejects_design_values_without_a_caller(self):
        temporary, root = self.fixture("auto gap = spacing(design::Spacing::One);\n")
        with temporary:
            metrics = root / "desktop/design_system/metrics"
            metrics.mkdir(parents=True)
            (metrics / "metrics.h").write_text(
                "enum class Spacing { Half, One, Two };\n"
                "enum class DialogSize { Short, Ddl };\n",
                encoding="utf-8",
            )
            (metrics / "metrics.cpp").write_text(
                "case Spacing::Half: case Spacing::Two: case DialogSize::Ddl:\n",
                encoding="utf-8",
            )
            (root / "desktop/design_system/style.cpp").write_text(
                "tokens.insert(space2, spacing(Spacing::Two));\n"
                "auto size = dialogSize(DialogSize::Short);\n",
                encoding="utf-8",
            )
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                "desktop/design_system/metrics/metrics.h: DialogSize::Ddl has no caller",
                "desktop/design_system/metrics/metrics.h: Spacing::Half has no caller",
            ],
        )

    def test_rejects_search_placeholders_without_an_ellipsis(self):
        temporary, root = self.fixture(
            'search->setPlaceholderText(tr("Filter this page"));\n'
            'find->setPlaceholderText(tr("Find text…"));\n'
            'replace->setPlaceholderText(tr("Replace with"));\n'
        )
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                "desktop/widgets/sample.cpp:1: search placeholder without a final ellipsis",
                "desktop/widgets/sample.cpp:3: search placeholder without a final ellipsis",
            ],
        )

    def test_rejects_dialog_decisions_outside_the_dialog_contract(self):
        temporary, root = self.fixture(
            "auto answer = ConfirmationDialog::question(this, a, b, c, d);\n"
            "if (answer == QMessageBox::Yes) {}\n"
            'box.addButton(tr("Drop"), QMessageBox::DestructiveRole);\n'
            "buttons |= QMessageBox::Discard;\n"
            "sections->footerLayout()->addWidget(save);\n"
            "ConfirmationDialog::confirm(this, a, b, c, Consequence::Destructive);\n"
        )
        with temporary:
            found = ui_policy.violations(root)
        reason = "dialog decision outside the dialog contract (use ConfirmationDialog::confirm, addActionButton or DialogSections::addAction)"
        self.assertEqual(
            found,
            [f"desktop/widgets/sample.cpp:{line}: {reason}" for line in range(1, 6)],
        )

    def test_rejects_title_case_labels(self):
        temporary, root = self.fixture(
            'menu->addAction(tr("Close Others"));\n'
            'menu->addAction(tr("Check for Updates…"));\n'
            'menu->addAction(tr("Close to the right"));\n'
            'format->addItem(tr("JSON Lines"));\n'
            'label->setText(tr("Open Data to read an object."));\n'
            'menu->addAction(tr("About ChoscorDB"));\n'
        )
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                "desktop/widgets/sample.cpp:1: title case label (use sentence case)",
                "desktop/widgets/sample.cpp:2: title case label (use sentence case)",
            ],
        )

    def test_rejects_mnemonics_outside_menu_bar_titles(self):
        temporary, root = self.fixture(
            'start_ = sections->addAction(DialogActionRole::Primary, tr("&Export"));\n'
            'start_->setText(tr("&Retry"));\n'
            'auto* fileMenu = menuBar()->addMenu(tr("&File"));\n'
            'label->setText(tr("Schema & objects"));\n'
        )
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                "desktop/widgets/sample.cpp:1: mnemonic outside a menu bar title "
                "(dialog and toolbar labels have no mnemonic)",
                "desktop/widgets/sample.cpp:2: mnemonic outside a menu bar title "
                "(dialog and toolbar labels have no mnemonic)",
            ],
        )

    def test_rejects_em_dash_separators_in_ui_text(self):
        temporary, root = self.fixture(
            'return tr("Disconnected — %1").arg(name);\n'
            'list_->addItem(type + " — " + hash);\n'
            'return tr("Disconnected · %1").arg(name);\n'
            'auto* dash = new QLabel(QStringLiteral("\\u2014"), this);\n'
        )
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                'desktop/widgets/sample.cpp:1: em dash separator (use " · " or ": ")',
                'desktop/widgets/sample.cpp:2: em dash separator (use " · " or ": ")',
                'desktop/widgets/sample.cpp:4: em dash separator (use " · " or ": ")',
            ],
        )

    def test_rejects_generic_success_toast_titles(self):
        temporary, root = self.fixture(
            'toast_->showToast(tr("Success"), message, ToastVariant::Success);\n'
            'toast_->showToast(tr("SQL generated"), message, ToastVariant::Success);\n'
            'toast->showToast(QCoreApplication::translate("ScreenState", "Success"), body,\n'
            'region->showReplacingToast(title, tr("Success"));\n'
        )
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                "desktop/widgets/sample.cpp:1: generic toast title (name the result)",
                "desktop/widgets/sample.cpp:3: generic toast title (name the result)",
                "desktop/widgets/sample.cpp:4: generic toast title (name the result)",
            ],
        )

    def test_rejects_tooltip_sentences_without_a_final_period(self):
        temporary, root = self.fixture(
            'edit->setToolTip(tr("Type a condition. Omit WHERE"));\n'
            'edit->setToolTip(tr("Type a condition. Do not type WHERE."));\n'
            'button->setToolTip(tr("Refresh"));\n'
        )
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            ["desktop/widgets/sample.cpp:1: tooltip sentence without a final period"],
        )

    def test_rejects_member_access_on_a_temporary_variant_value(self):
        temporary, root = self.fixture(
            "const auto sql = item->data(Qt::UserRole).value<SavedHistoryEntry>().sql;\n"
            "const auto entryData = item->data(Qt::UserRole);\n"
            "const auto sql = entryData.value<SavedHistoryEntry>().sql;\n"
            "const auto profile = item->data(Qt::UserRole).value<SavedProfile>();\n"
            "const auto id = combo->currentData().value<SavedProfile>().id;\n"
            "const auto name = model->headerData(0, Qt::Horizontal).value<Column>().name;\n"
            'const auto kind = widget->property("entry").value<SavedProfile>().id;\n'
        )
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                "desktop/widgets/sample.cpp:1: member of a value<T>() from a temporary "
                "QVariant (bind the QVariant first)",
                "desktop/widgets/sample.cpp:5: member of a value<T>() from a temporary "
                "QVariant (bind the QVariant first)",
                "desktop/widgets/sample.cpp:6: member of a value<T>() from a temporary "
                "QVariant (bind the QVariant first)",
                "desktop/widgets/sample.cpp:7: member of a value<T>() from a temporary "
                "QVariant (bind the QVariant first)",
            ],
        )

    def test_rejects_failed_and_unable_error_sentences(self):
        temporary, root = self.fixture(
            'message(tr("History clear failed: %1").arg(error));\n'
            'message(tr("Export failed. %1").arg(error));\n'
            'message(tr("Unable to open the file."));\n'
            'message(tr("Could not clear the history: %1").arg(error));\n'
            'toast_->showToast(tr("Export failed"), body, ToastVariant::Danger);\n'
            'message(tr("Failed to load result page: %1").arg(error));\n'
        )
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                "desktop/widgets/sample.cpp:1: error sentence form (use Could not …)",
                "desktop/widgets/sample.cpp:2: error sentence form (use Could not …)",
                "desktop/widgets/sample.cpp:3: error sentence form (use Could not …)",
                "desktop/widgets/sample.cpp:6: error sentence form (use Could not …)",
            ],
        )

    def test_rejects_terms_that_the_term_list_does_not_permit(self):
        temporary, root = self.fixture(
            'menu->addAction(tr("Disconnect selected session"));\n'
            'status->setText(tr("New settings apply to new queries."));\n'
            'menu->addAction(tr("Query preferences…"));\n'
            'button->setText(tr("Save profile"));\n'
            'label->setText(tr("Edit the connection profile"));\n'
        )
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                "desktop/widgets/sample.cpp:1: term not in the term list "
                "(use connection or preferences)",
                "desktop/widgets/sample.cpp:2: term not in the term list "
                "(use connection or preferences)",
                "desktop/widgets/sample.cpp:4: term not in the term list "
                "(use connection or preferences)",
            ],
        )

    def test_rejects_engine_text_with_forbidden_terms_and_error_forms(self):
        temporary, root = self.fixture("")
        with temporary:
            source = root / "crates/core/src"
            source.mkdir(parents=True)
            (source / "lib.rs").write_text(
                'Presentation::busy("Saving profile…");\n'
                'finished("Connection saved", detail);\n'
                'error("Invalid SSH tunnel settings");\n'
                'error("PostgreSQL connection failed; check the server");\n'
                'error("Could not connect to PostgreSQL.");\n'
                'db.execute("INSERT INTO settings(key) VALUES (?1)");\n'
                'error("Saved connection profile is missing");\n',
                encoding="utf-8",
            )
            found = ui_policy.violations(root)
        term = "term not in the term list (use connection or preferences)"
        self.assertEqual(
            found,
            [
                f"crates/core/src/lib.rs:1: {term}",
                f"crates/core/src/lib.rs:3: {term}",
                "crates/core/src/lib.rs:4: error sentence form (use Could not …)",
            ],
        )

    def test_rejects_literal_timer_intervals_in_screens(self):
        temporary, root = self.fixture(
            "debounce_.setInterval(350);\n"
            "QTimer::singleShot(20, this, retry);\n"
            "QTimer::singleShot(0, this, relayout);\n"
            "debounce_.setInterval(EngineAdapter::layoutSaveDelayMs());\n"
            "startupPoll.start(10);\n"
            "std::this_thread::sleep_for(250ms);\n"
            "std::this_thread::sleep_for(std::chrono::milliseconds(5));\n"
            "startupPoll.start(EngineAdapter::startupPollMs());\n"
            "debounce_.start();\n"
            "QTimer::singleShot(400, this, capture); // ui-ok: screenshot waits for paint\n"
        )
        with temporary:
            found = ui_policy.violations(root)
        interval = "literal timer interval (read the interval from EngineAdapter)"
        self.assertEqual(
            found,
            [
                f"desktop/widgets/sample.cpp:1: {interval}",
                f"desktop/widgets/sample.cpp:2: {interval}",
                f"desktop/widgets/sample.cpp:5: {interval}",
                f"desktop/widgets/sample.cpp:6: {interval}",
                f"desktop/widgets/sample.cpp:7: {interval}",
            ],
        )

    def test_rejects_release_headers_inside_the_preview_block(self):
        temporary, root = self.fixture(
            '#include "widgets/search_panel/search_panel.h"\n'
            "#ifdef CHOSCORDB_DEVELOPMENT_PREVIEW\n"
            '#include "tools/preview/preview_window.h"\n'
            '#include "widgets/filter_field/filter_field.h"\n'
            "#endif\n"
            '#include "widgets/sql_editor/sql_editor.h"\n'
        )
        with temporary:
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                "desktop/widgets/sample.cpp:4: release header inside the preview block "
                "(release builds leave out CHOSCORDB_DEVELOPMENT_PREVIEW)"
            ],
        )

    def test_preview_block_forms_and_release_branches(self):
        temporary, root = self.fixture(
            "#if defined (CHOSCORDB_DEVELOPMENT_PREVIEW)\n"
            '#include "widgets/a.h"\n'
            "#else\n"
            '#include "widgets/b.h"\n'
            "#endif\n"
            "#if CHOSCORDB_DEVELOPMENT_PREVIEW\n"
            '#include "widgets/c.h"\n'
            "#endif\n"
            "#ifdef Q_OS_MACOS\n"
            '#include "widgets/d.h"\n'
            "#elif defined(CHOSCORDB_DEVELOPMENT_PREVIEW)\n"
            '#include "widgets/e.h"\n'
            "#endif\n"
            "#ifndef CHOSCORDB_DEVELOPMENT_PREVIEW\n"
            '#include "widgets/f.h"\n'
            "#endif\n"
            "#if !defined(CHOSCORDB_DEVELOPMENT_PREVIEW)\n"
            '#include "widgets/g.h"\n'
            "#endif\n"
        )
        with temporary:
            found = ui_policy.violations(root)
        rule = (
            "release header inside the preview block "
            "(release builds leave out CHOSCORDB_DEVELOPMENT_PREVIEW)"
        )
        self.assertEqual(
            found,
            [
                f"desktop/widgets/sample.cpp:2: {rule}",
                f"desktop/widgets/sample.cpp:7: {rule}",
                f"desktop/widgets/sample.cpp:12: {rule}",
            ],
        )

    def test_gallery_uses_design_buttons_and_token_sizes(self):
        temporary, root = self.fixture("")
        with temporary:
            preview = root / "desktop/tools/preview"
            preview.mkdir(parents=True)
            (preview / "gallery.cpp").write_text(
                'auto* copy = new QPushButton(tr("Copy"), host);\n'
                "tree->setFixedSize(220, 275);\n"
                "list->setFixedHeight(8 * dimension(Dimension::Row));\n"
                "header->resizeSection(0, 240);\n"
                "header->resizeSection(0, dimension(Dimension::TableColumn));\n"
                "resize(1280, 900); // ui-ok: capture baseline\n",
                encoding="utf-8",
            )
            found = ui_policy.violations(root)
        self.assertEqual(
            found,
            [
                "desktop/tools/preview/gallery.cpp:1: stock button in the gallery "
                "(use design::Button)",
                "desktop/tools/preview/gallery.cpp:2: literal presentation metric in "
                "setFixedSize()",
                "desktop/tools/preview/gallery.cpp:4: literal presentation metric in "
                "resizeSection()",
            ],
        )

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
                "widget.resize(dimension(Dimension::ModalWidth), height);\n",
                encoding="utf-8",
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
