#include "models/shortcut_catalog.h"
#include "widgets/preferences_dialog.h"
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QtTest>
class PreferencesTest : public QObject {
    Q_OBJECT
  private slots:
    void loadFailureRequiresDeliberateResetBeforeApplyingDefaults() {
        choscordb::EngineAdapter adapter;
        adapter.shutdown();
        choscordb::PreferencesDialog dialog(&adapter, {{"find", "Find", "Ctrl+F"}});
        auto* reset = dialog.findChild<QPushButton*>("preferencesReset");
        auto* apply = dialog.findChild<QPushButton*>("preferencesApply");
        QTRY_VERIFY(reset->isEnabled());
        QVERIFY(!apply->isEnabled());
        QVERIFY(!dialog.findChild<QLabel*>("preferencesStatus")->text().isEmpty());
        reset->click();
        QVERIFY(apply->isEnabled());
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
        dialog.findChild<QPushButton*>("preferencesReset")->click();
        QCOMPARE(confirmed.count(), 2);
        apply->click();
        QTRY_COMPARE(confirmed.count(), 3);
        QVERIFY(qvariant_cast<choscordb::EditorPreferences>(confirmed.last().at(1))
                    .shortcuts.isEmpty());
        adapter.shutdown();
        size->setValue(24);
        apply->click();
        QTRY_VERIFY(apply->isEnabled());
        QCOMPARE(size->value(), 24);
        emit adapter.editorPreferencesReady(confirmed.first().at(0).toULongLong(),
                                            choscordb::EditorPreferences{});
        QCOMPARE(size->value(), 24);
        QVERIFY(!dialog.findChild<QLabel*>("preferencesStatus")->text().isEmpty());
    }
};
QTEST_MAIN(PreferencesTest)
#include "preferences_test.moc"
