#include "app/main_window.h"
#include "bridge/engine_adapter.h"
#include "widgets/sql_editor.h"
#include <QAction>
#include <QDialog>
#include <QKeySequenceEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <Qsci/qscilexer.h>
#include <QtTest>
class PreferencesWorkspaceTest : public QObject {
    Q_OBJECT
  private slots:
    void savedPreferencesSurviveDialogClosureAndRestart() {
        QTemporaryDir directory;
        const auto path = directory.filePath("preferences.sqlite");
        {
            choscordb::MainWindow window(nullptr, path);
            window.show();
            auto* tabs = window.findChild<QTabWidget*>("editorTabs");
            QTRY_VERIFY(tabs->isEnabled());
            window.findChild<QAction*>("preferences")->trigger();
            auto* dialog = window.findChild<QDialog*>("preferencesDialog");
            auto* apply = dialog->findChild<QPushButton*>("preferencesApply");
            QTRY_VERIFY(apply->isEnabled());
            dialog->findChild<QSpinBox*>("preferencesFontSize")->setValue(19);
            dialog->findChild<QKeySequenceEdit*>("shortcut_find")
                ->setKeySequence(QKeySequence("Ctrl+J"));
            apply->click();
            delete dialog;
            auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
            QTRY_COMPARE(editor->lexer()->defaultFont().pointSize(), 19);
            auto* adapter = window.findChild<choscordb::EngineAdapter*>();
            choscordb::EditorPreferences stale;
            stale.fontSize = 35;
            emit adapter->editorPreferencesReady(123456, stale);
            emit adapter->editorPreferencesReady(quint64(1) << 56, stale);
            QCOMPARE(editor->lexer()->defaultFont().pointSize(), 19);
            window.close();
            QTRY_VERIFY(!window.isVisible());
        }
        choscordb::MainWindow restarted(nullptr, path);
        restarted.show();
        auto* tabs = restarted.findChild<QTabWidget*>("editorTabs");
        QTRY_VERIFY(tabs->isEnabled());
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        QTRY_COMPARE(editor->lexer()->defaultFont().pointSize(), 19);
        QCOMPARE(restarted.findChild<QAction*>("command_find")->shortcut(), QKeySequence("Ctrl+J"));
        restarted.findChild<QAction*>("preferences")->trigger();
        auto* dialog = restarted.findChild<QDialog*>("preferencesDialog");
        QTRY_VERIFY(dialog->findChild<QPushButton*>("preferencesApply")->isEnabled());
        QVERIFY(dialog->grab().save("native-preferences.png"));
        dialog->close();
        restarted.close();
        QTRY_VERIFY(!restarted.isVisible());
    }
    void shutdownRejectsLateProfileMutations() {
        choscordb::EngineAdapter adapter;
        QSignalSpy failed(&adapter, &choscordb::EngineAdapter::profileFailed);
        adapter.beginShutdown();
        choscordb::SavedProfile profile;
        profile.id = "closing-profile";
        profile.name = "Closing profile";
        profile.path = ":memory:";
        adapter.saveProfile(profile, 501);
        adapter.duplicateProfile(profile.id, "copy", "Copy", 502);
        adapter.deleteProfile(profile.id, 503);
        QCOMPARE(failed.count(), 3);
        for (const auto& event : failed)
            QVERIFY(event.at(1).toString().contains("closing"));
    }
    void shutdownRejectsSettingsAfterItsFlushBarrier() {
        choscordb::EngineAdapter adapter;
        QSignalSpy saved(&adapter, &choscordb::EngineAdapter::editorPreferencesReady);
        QSignalSpy closed(&adapter, &choscordb::EngineAdapter::shutdownReady);
        choscordb::EditorPreferences preferences;
        preferences.fontSize = 21;
        QVERIFY(adapter.setEditorPreferences(preferences, 42));
        adapter.beginShutdown();
        QVERIFY(!adapter.setEditorPreferences(preferences, 43));
        QTRY_COMPARE(closed.count(), 1);
        QCOMPARE(saved.count(), 1);
        QCOMPARE(saved.first().at(0).toULongLong(), quint64(42));
    }
    void customizedAndDisabledUndoAffectActualKeyboardInput_data() {
        QTest::addColumn<QString>("binding");
        QTest::newRow("native-collision") << QString("Ctrl+U");
        QTest::newRow("multi-chord") << QString("Ctrl+K, Ctrl+U");
    }
    void customizedAndDisabledUndoAffectActualKeyboardInput() {
        QFETCH(QString, binding);
        choscordb::MainWindow window;
        window.show();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        window.findChild<QAction*>("preferences")->trigger();
        auto* dialog = window.findChild<QDialog*>("preferencesDialog");
        auto* apply = dialog->findChild<QPushButton*>("preferencesApply");
        QTRY_VERIFY(apply->isEnabled());
        dialog->findChild<QKeySequenceEdit*>("shortcut_undo")
            ->setKeySequence(QKeySequence(binding));
        apply->click();
        QTRY_VERIFY(apply->isEnabled());
        dialog->hide();
        window.activateWindow();
        editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());
        editor->setText("SELECT 1");
        editor->setCursorPosition(0, 8);
        QTest::keyClicks(editor, ";");
        QCOMPARE(editor->text(), QString("SELECT 1;"));
        QTest::keySequence(editor, QKeySequence::Undo);
        QCOMPARE(editor->text(), QString("SELECT 1;"));
        QTest::keySequence(editor, QKeySequence(binding));
        QCOMPARE(editor->text(), QString("SELECT 1"));
        dialog->show();
        dialog->findChild<QKeySequenceEdit*>("shortcut_undo")->setKeySequence({});
        apply->click();
        QTRY_VERIFY(apply->isEnabled());
        dialog->hide();
        window.activateWindow();
        editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());
        editor->setCursorPosition(0, 8);
        QTest::keyClicks(editor, ";");
        QTest::keySequence(editor, QKeySequence::Undo);
        QCOMPARE(editor->text(), QString("SELECT 1;"));
    }
    void confirmedPreferencesReachExistingAndNewEditors() {
        choscordb::MainWindow window;
        window.show();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        auto* preferences = window.findChild<QAction*>("preferences");
        QVERIFY(preferences);
        preferences->trigger();
        auto* apply = window.findChild<QPushButton*>("preferencesApply");
        QVERIFY(apply);
        QTRY_VERIFY(apply->isEnabled());
        editor->setText("SELECT 1");
        window.findChild<QSpinBox*>("preferencesFontSize")->setValue(23);
        window.findChild<QKeySequenceEdit*>("shortcut_find")
            ->setKeySequence(QKeySequence("Ctrl+J"));
        apply->click();
        QTRY_COMPARE(editor->lexer()->defaultFont().pointSize(), 23);
        QCOMPARE(editor->text(), QString("SELECT 1"));
        QCOMPARE(window.findChild<QAction*>("command_find")->shortcut(), QKeySequence("Ctrl+J"));
        window.findChild<QAction*>("command_new_query")->trigger();
        auto* next = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        QVERIFY(next != editor);
        QCOMPARE(next->lexer()->defaultFont().pointSize(), 23);
    }
};
QTEST_MAIN(PreferencesWorkspaceTest)
#include "preferences_workspace_test.moc"
