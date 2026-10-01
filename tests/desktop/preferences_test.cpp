#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/field/field.h"
#include "design_system/theme.h"
#include "design_system/toast_region/toast_region.h"
#include "models/shortcut_catalog.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/preferences_dialog/preferences_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <QtTest>
#include <memory>
class PreferencesTest : public QObject {
    Q_OBJECT
  private slots:
    void appToolsUseSharedDialogSections_data() {
        QTest::addColumn<bool>("exportTool");
        QTest::newRow("preferences") << false;
        QTest::newRow("export") << true;
    }
    void appToolsUseSharedDialogSections() {
        QFETCH(bool, exportTool);
        choscordb::EngineAdapter adapter;
        std::unique_ptr<QDialog> dialog;
        if (exportTool)
            dialog = std::make_unique<choscordb::ExportDialog>(&adapter, nullptr);
        else
            dialog = std::make_unique<choscordb::PreferencesDialog>(
                &adapter, QList<choscordb::ShortcutDescriptor>{});
        dialog->show();
        auto* sections = dialog->findChild<choscordb::design::DialogSections*>();
        QVERIFY(sections);
        auto* header = sections->headerLayout()->parentWidget();
        auto* body = sections->bodyLayout()->parentWidget();
        auto* footer = sections->footerLayout()->parentWidget();
        const auto metrics =
            choscordb::design::resolveMetrics(choscordb::design::Density::Compact, true);
        QCOMPARE(header->height(), metrics.modalHeaderHeight);
        QCOMPARE(sections->headerLayout()->contentsMargins().left(), metrics.modalContentInset);
        QCOMPARE(sections->footerLayout()->contentsMargins().left(), metrics.modalFooterInset);
        QCOMPARE(footer->property("designSurface").toString(), QString("muted"));
        auto* dismiss =
            dialog->findChild<QPushButton*>(exportTool ? "exportDismiss" : "preferencesDismiss");
        auto* close =
            dialog->findChild<QPushButton*>(exportTool ? "exportClose" : "preferencesClose");
        QVERIFY(dismiss && header->isAncestorOf(dismiss));
        QVERIFY(close && footer->isAncestorOf(close));
        auto* content =
            dialog->findChild<QWidget*>(exportTool ? "exportFormat" : "preferencesSections");
        QVERIFY(content && body->isAncestorOf(content));
        dialog->reject();
    }
    void appToolsContainFocusAndRestoreInvoker_data() {
        QTest::addColumn<bool>("exportTool");
        QTest::newRow("preferences") << false;
        QTest::newRow("export") << true;
    }
    void appToolsContainFocusAndRestoreInvoker() {
        QFETCH(bool, exportTool);
        choscordb::EngineAdapter adapter;
        QWidget owner;
        QVBoxLayout layout(&owner);
        QLineEdit invoker;
        QPushButton background("Background action");
        layout.addWidget(&invoker);
        layout.addWidget(&background);
        owner.resize(960, 640);
        owner.show();
        owner.activateWindow();
        invoker.setFocus();
        QTRY_VERIFY(invoker.hasFocus());
        std::unique_ptr<QDialog> dialog;
        if (exportTool)
            dialog = std::make_unique<choscordb::ExportDialog>(&adapter, &owner);
        else
            dialog = std::make_unique<choscordb::PreferencesDialog>(
                &adapter, QList<choscordb::ShortcutDescriptor>{}, &owner);
        QSignalSpy clicked(&background, &QPushButton::clicked);
        dialog->show();
        dialog->activateWindow();
        QTRY_COMPARE(choscordb::design::DialogPresentation::activeDialog(), dialog.get());
        QTRY_VERIFY(owner.isActiveWindow());
        QVERIFY(!dialog->isWindow());
        QCOMPARE(dialog->window(), &owner);
        QTRY_VERIFY(QApplication::focusWidget() &&
                    (QApplication::focusWidget() == dialog.get() ||
                     dialog->isAncestorOf(QApplication::focusWidget())));
        QVERIFY(dialog->windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(owner.findChild<QWidget*>("modalBackdrop")->isVisible());
        QTest::mousePress(owner.windowHandle(), Qt::LeftButton, {},
                          background.mapTo(&owner, background.rect().center()));
        QCOMPARE(clicked.count(), 0);
        for (int i = 0; i < 12; ++i) {
            QTest::keyClick(dialog.get(), Qt::Key_Tab);
            auto* focus = QApplication::focusWidget();
            QVERIFY(focus && (focus == dialog.get() || dialog->isAncestorOf(focus)));
        }
        QTest::keyClick(dialog.get(), Qt::Key_Escape);
        QTRY_VERIFY(!dialog->isVisible());
        QTRY_VERIFY(invoker.hasFocus());
        QTest::mouseClick(owner.windowHandle(), Qt::LeftButton, {},
                          background.mapTo(&owner, background.rect().center()));
        QCOMPARE(clicked.count(), 1);
    }
    void preferencesLoadWhileStartupReadsAwaitDelivery() {
        QTemporaryDir directory;
        choscordb::EngineAdapter adapter(nullptr, directory.filePath("pending-loads.sqlite"));
        QSignalSpy failed(&adapter, &choscordb::EngineAdapter::recoveryFailed);
        const auto timers = adapter.findChildren<QTimer*>(QString{}, Qt::FindDirectChildrenOnly);
        QCOMPARE(timers.size(), 1);
        auto* delivery = timers.first();
        // Keep startup responses awaiting Qt delivery while the dialog opens.
        delivery->stop();
        for (quint64 token = 1000; token < 1007; ++token)
            QVERIFY(adapter.getHistoryPolicy(token));
        choscordb::PreferencesDialog dialog(&adapter, {});
        delivery->start();
        QCOMPARE(failed.count(), 0);
        dialog.show();
        QTRY_VERIFY(dialog.findChild<QPushButton*>("preferencesApply")->isEnabled());
        QCOMPARE(failed.count(), 0);
        dialog.close();
    }

    void preferencesExposeSixSectionsAndCloseOnlyAfterSave() {
        choscordb::EngineAdapter adapter;
        choscordb::PreferencesDialog dialog(&adapter, {});
        dialog.show();
        auto* sections = dialog.findChild<QTabWidget*>("preferencesSections");
        QVERIFY(sections);
        QCOMPARE(sections->count(), 6);
        QCOMPARE(sections->tabText(0), QString("Appearance"));
        QCOMPARE(sections->tabText(1), QString("SQL editor"));
        QCOMPARE(sections->tabText(2).replace("&&", "&"), QString("Results & execution"));
        QCOMPARE(sections->tabText(3), QString("Connections"));
        QCOMPARE(sections->tabText(4).replace("&&", "&"), QString("History & recovery"));
        QCOMPARE(sections->tabText(5), QString("Keyboard shortcuts"));
        auto* save = dialog.findChild<QPushButton*>("preferencesApply");
        QTRY_VERIFY(save->isEnabled());
        QCOMPARE(save->text(), QString("Save preferences"));
        QVERIFY(dialog.findChild<QPushButton*>("preferencesClose"));
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        save->click();
        QCOMPARE(accepted.count(), 0);
        QTRY_COMPARE(accepted.count(), 1);
        QVERIFY(!dialog.isVisible());
    }
    void supportedSettingsPersistTogetherBeforeClosingAndSurviveRestart() {
        using namespace choscordb;
        QTemporaryDir directory;
        const auto path = directory.filePath("settings.sqlite");
        {
            EngineAdapter adapter(nullptr, path);
            QSignalSpy editorSaved(&adapter, &EngineAdapter::editorPreferencesReady);
            QSignalSpy querySaved(&adapter, &EngineAdapter::queryPreferencesReady);
            QSignalSpy historySaved(&adapter, &EngineAdapter::historyPolicyReady);
            PreferencesDialog dialog(&adapter, {});
            dialog.show();
            auto* save = dialog.findChild<QPushButton*>("preferencesApply");
            QTRY_VERIFY(save->isEnabled());
            auto* rows = dialog.findChild<QSpinBox*>("queryPageSize");
            auto* timeout = dialog.findChild<QSpinBox*>("queryTimeoutSeconds");
            auto* record = dialog.findChild<QCheckBox*>("preferencesRecordHistory");
            auto* days = dialog.findChild<QDoubleSpinBox*>("preferencesHistoryDays");
            auto* records = dialog.findChild<QDoubleSpinBox*>("preferencesHistoryRecords");
            QVERIFY(rows && timeout && record && days && records);
            rows->setValue(137);
            timeout->setValue(17);
            record->setChecked(false);
            days->setValue(12);
            records->setValue(345);
            dialog.findChild<QSpinBox*>("preferencesFontSize")->setValue(21);
            QSignalSpy accepted(&dialog, &QDialog::accepted);
            connect(&dialog, &QDialog::accepted, &dialog, [&] {
                QCOMPARE(editorSaved.count(), 2);
                QCOMPARE(querySaved.count(), 2);
                QCOMPARE(historySaved.count(), 2);
            });
            save->click();
            QVERIFY(dialog.isVisible());
            QTRY_COMPARE(accepted.count(), 1);
        }
        EngineAdapter restarted(nullptr, path);
        QSignalSpy query(&restarted, &EngineAdapter::queryPreferencesReady);
        QSignalSpy history(&restarted, &EngineAdapter::historyPolicyReady);
        QSignalSpy editor(&restarted, &EngineAdapter::editorPreferencesReady);
        QVERIFY(restarted.getQueryPreferences(801));
        QVERIFY(restarted.getHistoryPolicy(802));
        QVERIFY(restarted.getEditorPreferences(803));
        QTRY_COMPARE(query.count(), 1);
        QTRY_COMPARE(history.count(), 1);
        QTRY_COMPARE(editor.count(), 1);
        const auto result = qvariant_cast<QueryPreferences>(query.first().at(1));
        QCOMPARE(result.pageSize, quint32(137));
        QCOMPARE(result.timeoutSeconds, quint32(17));
        const auto policy = qvariant_cast<HistoryPolicy>(history.first().at(1));
        QVERIFY(!policy.enabled);
        QCOMPARE(policy.maxAgeDays, quint32(12));
        QCOMPARE(policy.maxRecords, quint32(345));
        QCOMPARE(qvariant_cast<EditorPreferences>(editor.first().at(1)).fontSize, quint16(21));
    }

    void systemSchemasSaveRestoreAndCancelAcrossRestart() {
        using namespace choscordb;
        QTemporaryDir directory;
        const auto path = directory.filePath("settings.sqlite");
        {
            EngineAdapter adapter(nullptr, path);
            PreferencesDialog dialog(&adapter, {});
            dialog.show();
            auto* save = dialog.findChild<QPushButton*>("preferencesApply");
            auto* show = dialog.findChild<QCheckBox*>("preferencesShowSystemSchemas");
            QVERIFY(show);
            QTRY_VERIFY(save->isEnabled());
            QVERIFY(!show->isChecked());
            QSignalSpy saved(&dialog, &PreferencesDialog::systemSchemaVisibilitySaved);
            show->setChecked(true);
            dialog.findChild<QPushButton*>("preferencesClose")->click();
            QCOMPARE(saved.count(), 0);
        }
        {
            EngineAdapter adapter(nullptr, path);
            PreferencesDialog dialog(&adapter, {});
            dialog.show();
            auto* save = dialog.findChild<QPushButton*>("preferencesApply");
            auto* show = dialog.findChild<QCheckBox*>("preferencesShowSystemSchemas");
            QVERIFY(show);
            QTRY_VERIFY(save->isEnabled());
            QVERIFY(!show->isChecked());
            show->setChecked(true);
            dialog.findChild<QPushButton*>("preferencesReset")->click();
            QVERIFY(!show->isChecked());
            show->setChecked(true);
            QSignalSpy saved(&dialog, &PreferencesDialog::systemSchemaVisibilitySaved);
            QSignalSpy accepted(&dialog, &QDialog::accepted);
            save->click();
            QTRY_COMPARE(accepted.count(), 1);
            QCOMPARE(saved.count(), 1);
            QCOMPARE(saved.first().first().toBool(), true);
        }
        EngineAdapter adapter(nullptr, path);
        PreferencesDialog dialog(&adapter, {});
        dialog.show();
        auto* show = dialog.findChild<QCheckBox*>("preferencesShowSystemSchemas");
        QVERIFY(show);
        auto* save = dialog.findChild<QPushButton*>("preferencesApply");
        QTRY_VERIFY(save->isEnabled());
        QVERIFY(show->isChecked());
        dialog.findChild<QPushButton*>("preferencesReset")->click();
        QVERIFY(!show->isChecked());
        QSignalSpy saved(&dialog, &PreferencesDialog::systemSchemaVisibilitySaved);
        save->click();
        QTRY_COMPARE(saved.count(), 1);
        QCOMPARE(saved.first().first().toBool(), false);
        EngineAdapter afterReset(nullptr, path);
        QSignalSpy restored(&afterReset, &EngineAdapter::queryPreferencesReady);
        QVERIFY(afterReset.getQueryPreferences(804));
        QTRY_COMPARE(restored.count(), 1);
        QVERIFY(!qvariant_cast<QueryPreferences>(restored.first().at(1)).showSystemSchemas);
    }

    void existingLargeRetentionValuesRemainUsableAndArePreserved() {
        using namespace choscordb;
        EngineAdapter adapter;
        HistoryPolicy policy;
        policy.maxAgeDays = 3000000000U;
        policy.maxRecords = 4000000000U;
        QSignalSpy persisted(&adapter, &EngineAdapter::historyPolicyReady);
        QVERIFY(adapter.setHistoryPolicy(policy, 901));
        QTRY_COMPARE(persisted.count(), 1);
        PreferencesDialog dialog(&adapter, {});
        dialog.show();
        auto* save = dialog.findChild<QPushButton*>("preferencesApply");
        QTRY_VERIFY(save->isEnabled());
        save->click();
        QTRY_VERIFY(!dialog.isVisible());
        QCOMPARE(qvariant_cast<HistoryPolicy>(persisted.last().at(1)).maxAgeDays,
                 quint32(3000000000U));
        QCOMPARE(qvariant_cast<HistoryPolicy>(persisted.last().at(1)).maxRecords,
                 quint32(4000000000U));
    }

    void escapeDuringAcceptedExportWaitsForCleanup_data() {
        QTest::addColumn<bool>("invalidate");
        QTest::newRow("Escape") << false;
        QTest::newRow("query-invalidated") << true;
    }
    void escapeDuringAcceptedExportWaitsForCleanup() {
        QFETCH(bool, invalidate);
        using namespace choscordb;
        EngineAdapter adapter;
        QTemporaryDir directory;
        bool connected = false, pageReady = false;
        connect(&adapter, &EngineAdapter::eventReady, &adapter, [&](const BridgeEvent& event) {
            const auto kind = QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
            connected = connected || kind == "connected";
            pageReady = pageReady || (kind == "page" && event.row_count > 0);
        });
        const auto connection = adapter.connectSqlite(":memory:");
        QVERIFY(connection);
        QTRY_VERIFY(connected);
        const auto query = adapter.execute(
            *connection,
            "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1000000) "
            "SELECT x FROM n");
        QVERIFY(query);
        adapter.fetchPage(*query);
        QTRY_VERIFY_WITH_TIMEOUT(pageReady, 30000);
        QWidget owner;
        owner.resize(960, 640);
        owner.show();
        ExportDialog dialog(&adapter, &owner);
        dialog.setQuery(*query);
        dialog.show();
        bool cancellationRequested = false;
        connect(&adapter, &EngineAdapter::eventReady, &dialog, [&](const BridgeEvent& event) {
            const auto kind = QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
            if (kind != "export_progress" || !event.exported_rows || cancellationRequested)
                return;
            cancellationRequested = true;
            if (invalidate)
                dialog.clearQuery();
            else
                QTest::keyClick(&dialog, Qt::Key_Escape);
            QVERIFY(dialog.isVisible());
            QVERIFY(dialog.isRunning());
            auto* status = dialog.findChild<QLabel*>("exportStatus");
            QVERIFY(status && status->isVisible());
            QVERIFY(status->text().contains("Cancelling"));
            QVERIFY(!dialog.findChild<choscordb::ToastRegion*>("progressToast"));
        });
        dialog.startExportTo(directory.filePath("cancelled.csv"), "csv");
        QTRY_VERIFY(cancellationRequested);
        QTRY_VERIFY(!dialog.isRunning());
        QVERIFY(!dialog.isVisible());
        auto* finalStatus = dialog.findChild<QLabel*>("exportStatus");
        QVERIFY(finalStatus);
        QCOMPARE(finalStatus->text(), QString("Export cancelled."));
        QVERIFY(!owner.findChild<choscordb::ToastRegion*>("toastRegion"));
        QCOMPARE(QDir(directory.path())
                     .entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)
                     .size(),
                 0);
    }

    void exportFailureKeepsReadableRetryFeedback() {
        choscordb::EngineAdapter adapter;
        QTemporaryDir directory;
        QWidget owner;
        owner.show();
        choscordb::ExportDialog dialog(&adapter, &owner);
        dialog.setQuery(1);
        adapter.shutdown();
        dialog.show();
        dialog.startExportTo(directory.filePath("failed.csv"), "csv");
        QTRY_VERIFY(!dialog.isRunning());
        auto* status = dialog.findChild<QLabel*>("exportStatus");
        QVERIFY(status && status->isVisible());
        QVERIFY(!status->text().isEmpty());
        QCOMPARE(dialog.findChild<QPushButton*>("exportStart")->text(), QString("&Retry"));
        QVERIFY(dialog.isVisible());
        QVERIFY(!owner.findChild<choscordb::ToastRegion*>("toastRegion"));
    }

    void exportCancellationKeepsNeutralLocalFeedback() {
        choscordb::EngineAdapter adapter;
        QTemporaryDir directory;
        QWidget owner;
        owner.show();
        choscordb::ExportDialog dialog(&adapter, &owner);
        dialog.setQuery(1);
        dialog.show();
        dialog.startExportTo(directory.filePath("cancelled.csv"), "csv");
        QVERIFY(dialog.isRunning());
        auto* loading = dialog.findChild<QLabel*>("statusLoadingIcon");
        QVERIFY(loading && loading->isVisible());
        dialog.findChild<QPushButton*>("exportCancel")->click();
        QVERIFY(!dialog.isRunning());
        QVERIFY(!loading->isVisible());
        auto* status = dialog.findChild<QLabel*>("exportStatus");
        QVERIFY(status && status->isVisible());
        QCOMPARE(status->text(), QString("Export cancelled."));
        QVERIFY(!owner.findChild<choscordb::ToastRegion*>("toastRegion"));
    }

    void appearanceOffersOnlySystemLightAndDark() {
        choscordb::EngineAdapter adapter;
        choscordb::PreferencesDialog dialog(&adapter, {});
        auto* theme = dialog.findChild<QComboBox*>("appearanceTheme");
        QVERIFY(theme != nullptr);
        QCOMPARE(theme->count(), 3);
        QCOMPARE(theme->itemData(0).toString(), QString("system"));
        QCOMPARE(theme->itemData(1).toString(), QString("light"));
        QCOMPARE(theme->itemData(2).toString(), QString("dark"));
        QVERIFY(dialog.findChild<QWidget*>("appearanceDensity") == nullptr);
        QVERIFY(dialog.findChild<QWidget*>("appearanceAccent") == nullptr);
        QVERIFY(dialog.findChild<QWidget*>("appearanceCustomAccent") == nullptr);
    }

    void loadFailureRequiresDeliberateResetBeforeApplyingDefaults() {
        choscordb::EngineAdapter adapter;
        adapter.shutdown();
        choscordb::PreferencesDialog dialog(&adapter, {{"find", "Find", "Ctrl+F"}});
        dialog.show();
        auto* reset = dialog.findChild<QPushButton*>("preferencesReset");
        auto* apply = dialog.findChild<QPushButton*>("preferencesApply");
        QTRY_VERIFY(reset->isEnabled());
        QVERIFY(!apply->isEnabled());
        auto* status = dialog.findChild<QLabel*>("preferencesStatus");
        QVERIFY(status);
        QTRY_VERIFY(!status->text().isEmpty());
        QVERIFY(status->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
        QVERIFY(dialog.findChild<choscordb::ToastRegion*>("toastRegion") == nullptr);
        reset->click();
        QVERIFY(apply->isEnabled());
    }
    void shortcutConflictAppearsBelowTheShortcutField() {
        choscordb::EngineAdapter adapter;
        choscordb::PreferencesDialog dialog(
            &adapter, {{"find", "Find", "Ctrl+F"}, {"copy", "Copy", "Ctrl+C"}});
        dialog.show();
        auto* apply = dialog.findChild<QPushButton*>("preferencesApply");
        QTRY_VERIFY(apply->isEnabled());
        auto* find = dialog.findChild<QKeySequenceEdit*>("shortcut_find");
        find->setKeySequence(QKeySequence("Ctrl+C", QKeySequence::PortableText));
        apply->click();
        auto* validation = dynamic_cast<choscordb::design::FieldValidation*>(find->parentWidget());
        QVERIFY(validation);
        QVERIFY(validation->error().contains("conflict"));
        QVERIFY(find->property("invalid").toBool());
        find->setKeySequence(QKeySequence("Ctrl+J", QKeySequence::PortableText));
        QCOMPARE(validation->error(), QString());
    }
    void conflictsIncludeDefaultsReservedCommandsAndPrefixes() {
        QList<choscordb::ShortcutDescriptor> catalog = {{"find", "Find", "Ctrl+F"},
                                                        {"copy", "Copy", "Ctrl+C"},
                                                        {"quit", "Quit", "Ctrl+Q", false}};
        choscordb::EditorPreferences preferences;
        preferences.shortcuts = {{"find", "Ctrl+C"}};
        QVERIFY(!choscordb::shortcutValidationError(preferences, catalog).isEmpty());
        preferences.shortcuts = {{"find", "Ctrl+Q, Ctrl+C"}};
        QVERIFY(!choscordb::shortcutValidationError(preferences, catalog).isEmpty());
        preferences.shortcuts = {{"unknown", "Ctrl+J"}};
        QVERIFY(!choscordb::shortcutValidationError(preferences, catalog).isEmpty());
        preferences.shortcuts = {{"find", "NotARealKey"}};
        QVERIFY(!choscordb::shortcutValidationError(preferences, catalog).isEmpty());
        preferences.shortcuts = {{"quit", "Ctrl+J"}};
        QVERIFY(!choscordb::shortcutValidationError(preferences, catalog).isEmpty());
        preferences.shortcuts = {{"find", "Ctrl+J"}, {"find", "Ctrl+K"}};
        QVERIFY(!choscordb::shortcutValidationError(preferences, catalog).isEmpty());
        preferences.shortcuts = {{"find", "Alt+Ctrl+X"}};
        QVERIFY(choscordb::shortcutValidationError(preferences, catalog).isEmpty());
        preferences.shortcuts = {{"find", "Ctrl+A, "}};
        QVERIFY(!choscordb::shortcutValidationError(preferences, catalog).isEmpty());
        preferences.shortcuts = {{"find", "Ctrl+A, Ctrl+B, Ctrl+D, Ctrl+E, Ctrl+G"}};
        QVERIFY(!choscordb::shortcutValidationError(preferences, catalog).isEmpty());
        preferences.shortcuts = {{"find", ""}};
        QVERIFY(choscordb::shortcutValidationError(preferences, catalog).isEmpty());
    }
    void draftIsSavedOnlyOnApplyAndFailureRetainsIt() {
        choscordb::EngineAdapter adapter;
        QSignalSpy confirmed(&adapter, &choscordb::EngineAdapter::editorPreferencesReady);
        choscordb::PreferencesDialog dialog(
            &adapter, {{"find", "Find", "Ctrl+F"}, {"copy", "Copy", "Ctrl+C"}});
        QSignalSpy visibilitySaved(&dialog,
                                   &choscordb::PreferencesDialog::systemSchemaVisibilitySaved);
        dialog.show();
        auto* apply = dialog.findChild<QPushButton*>("preferencesApply");
        QTRY_VERIFY(apply->isEnabled());
        auto* size = dialog.findChild<QSpinBox*>("preferencesFontSize");
        size->setValue(22);
        QCOMPARE(confirmed.count(), 1);
        dialog.findChild<QKeySequenceEdit*>("shortcut_find")
            ->setKeySequence(QKeySequence("Ctrl+J", QKeySequence::PortableText));
        apply->click();
        QTRY_COMPARE(confirmed.count(), 2);
        QCOMPARE(qvariant_cast<choscordb::EditorPreferences>(confirmed.last().at(1)).fontSize,
                 quint16(22));
        QCOMPARE(
            qvariant_cast<choscordb::EditorPreferences>(confirmed.last().at(1)).shortcuts.size(),
            1);
        QTRY_VERIFY(!dialog.isVisible());
        dialog.show();
        dialog.findChild<QPushButton*>("preferencesReset")->click();
        QCOMPARE(confirmed.count(), 2);
        apply->click();
        QTRY_COMPARE(confirmed.count(), 3);
        QVERIFY(qvariant_cast<choscordb::EditorPreferences>(confirmed.last().at(1))
                    .shortcuts.isEmpty());
        QTRY_VERIFY(!dialog.isVisible());
        dialog.show();
        adapter.shutdown();
        dialog.findChild<QCheckBox*>("preferencesShowSystemSchemas")->setChecked(true);
        size->setValue(24);
        apply->click();
        QTRY_VERIFY(apply->isEnabled());
        QCOMPARE(size->value(), 24);
        emit adapter.editorPreferencesReady(confirmed.first().at(0).toULongLong(),
                                            choscordb::EditorPreferences{});
        QCOMPARE(size->value(), 24);
        auto* status = dialog.findChild<QLabel*>("preferencesStatus");
        QVERIFY(status);
        const auto error = status->text();
        QVERIFY(error.contains("SQL editor / Keyboard shortcuts"));
        QVERIFY(error.contains("Results & execution"));
        QVERIFY(error.contains("History & recovery"));
        QVERIFY(dialog.isVisible());
        QCOMPARE(visibilitySaved.count(), 2);
    }
};
QTEST_MAIN(PreferencesTest)
#include "preferences_test.moc"
