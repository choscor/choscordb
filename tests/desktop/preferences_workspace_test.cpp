#include "app/appearance_controller.h"
#include "app/main_window.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/theme_manager.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <Qsci/qscilexer.h>
#include <QtTest>
class PreferencesWorkspaceTest : public QObject {
    Q_OBJECT
  private slots:
    void appearanceResetRequiresSaveAndSurvivesRestart_data() {
        QTest::addColumn<bool>("corrupt");
        QTest::addColumn<bool>("save");
        QTest::newRow("valid-close") << false << false;
        QTest::newRow("valid-save") << false << true;
        QTest::newRow("corrupt-close") << true << false;
        QTest::newRow("corrupt-save") << true << true;
    }
    void appearanceResetRequiresSaveAndSurvivesRestart() {
        using namespace choscordb;
        QFETCH(bool, corrupt);
        QFETCH(bool, save);
        QTemporaryDir directory;
        const auto path = directory.filePath("reset.sqlite");
        {
            EngineAdapter writer(nullptr, path);
            if (corrupt) {
                bool connected = false, completed = false;
                connect(&writer, &EngineAdapter::eventReady, &writer,
                        [&](const BridgeEvent& event) {
                            connected = connected || event.kind == "connected";
                            completed = completed || event.kind == "query_finished";
                        });
                const auto connection = writer.connectSqlite(path);
                QVERIFY(connection);
                QTRY_VERIFY(connected);
                const auto query = writer.execute(
                    *connection,
                    "INSERT INTO appearance_layout(singleton,value) VALUES(1,'{bad json')");
                QVERIFY(query);
                writer.fetchPage(*query);
                QTRY_VERIFY(completed);
            } else {
                AppearanceLayout initial;
                initial.theme = "dark";
                initial.width = 1100;
                initial.height = 760;
                QSignalSpy saved(&writer, &EngineAdapter::appearanceLayoutReady);
                QVERIFY(writer.setAppearanceLayout(initial, 902));
                QTRY_COMPARE(saved.count(), 1);
            }
        }
        {
            MainWindow window(nullptr, path);
            window.show();
            auto* appearance = window.findChild<AppearanceController*>();
            QTRY_VERIFY(appearance->isReady());
            window.findChild<QAction*>("preferences")->trigger();
            auto* dialog = window.findChild<QDialog*>("preferencesDialog");
            auto* apply = dialog->findChild<QPushButton*>("preferencesApply");
            auto* reset = dialog->findChild<QPushButton*>("appearanceReset");
            QTRY_VERIFY(reset->isEnabled());
            reset->click();
            QTRY_VERIFY(apply->isEnabled());
            if (save) {
                apply->click();
                QTRY_VERIFY(!window.findChild<QDialog*>("preferencesDialog"));
            } else {
                dialog->findChild<QPushButton*>("preferencesClose")->click();
            }
            window.close();
            QTRY_VERIFY(!window.isVisible());
        }
        MainWindow restarted(nullptr, path);
        auto* appearance = restarted.findChild<AppearanceController*>();
        QTRY_VERIFY(appearance->isReady());
        if (corrupt && !save) {
            QVERIFY(!appearance->currentWarning().isEmpty());
            QVERIFY(!appearance->canSave());
        } else {
            QVERIFY(appearance->currentWarning().isEmpty());
            QCOMPARE(appearance->persisted().theme, save ? QString("system") : QString("dark"));
            QCOMPARE(appearance->persisted().width, save ? quint32(1280) : quint32(1100));
        }
    }

    void brokenAppearanceRequiresExplicitRepair_data() {
        QTest::addColumn<QString>("payload");
        QTest::newRow("corrupt") << QString("{bad json");
        QTest::newRow("unsupported") << QString("{\"version\":2}");
    }
    void brokenAppearanceRequiresExplicitRepair() {
        using namespace choscordb;
        QFETCH(QString, payload);
        QTemporaryDir directory;
        const auto path = directory.filePath("broken-appearance.sqlite");
        {
            // Seed through the real SQL adapter, without a second SQLite dependency.
            EngineAdapter writer(nullptr, path);
            bool connected = false, completed = false;
            connect(&writer, &EngineAdapter::eventReady, &writer, [&](const BridgeEvent& event) {
                const auto kind =
                    QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
                const auto state =
                    QString::fromUtf8(event.state.data(), qsizetype(event.state.size()));
                connected = connected || kind == "connected";
                completed = completed || (kind == "query_state" && state == "completed");
            });
            const auto connection = writer.connectSqlite(path);
            QVERIFY(connection.has_value());
            QTRY_VERIFY(connected);
            const auto query = writer.execute(
                *connection,
                QStringLiteral("INSERT INTO appearance_layout(singleton,value) VALUES(1,'%1')")
                    .arg(payload));
            QVERIFY(query.has_value());
            writer.fetchPage(*query);
            QTRY_VERIFY(completed);
        }
        MainWindow window(nullptr, path);
        window.show();
        auto* appearance = window.findChild<AppearanceController*>();
        QTRY_VERIFY(appearance->isReady());
        QVERIFY(!appearance->currentWarning().isEmpty());
        window.findChild<QAction*>("preferences")->trigger();
        auto* dialog = window.findChild<QDialog*>("preferencesDialog");
        auto* apply = dialog->findChild<QPushButton*>("preferencesApply");
        QTRY_VERIFY(dialog->findChild<QSpinBox*>("preferencesFontSize")->isEnabled());
        QVERIFY(!apply->isEnabled());
        auto* warning = dialog->findChild<QLabel*>("appearanceStatus");
        QVERIFY(!warning->text().isEmpty());
        QSignalSpy saved(appearance, &AppearanceController::saveFinished);
        appearance->applyPreview();
        QTRY_COMPARE(saved.count(), 1);
        QVERIFY(!saved.first().first().toBool());
        QSignalSpy retried(appearance, &AppearanceController::readyChanged);
        dialog->findChild<QPushButton*>("appearanceRetry")->click();
        QTRY_COMPARE(retried.count(), 1);
        QVERIFY(!warning->text().isEmpty());
        QVERIFY(!apply->isEnabled());
        dialog->findChild<QPushButton*>("appearanceReset")->click();
        QTRY_VERIFY(warning->text().isEmpty());
        QTRY_VERIFY(apply->isEnabled());
        dialog->reject();
        window.close();
        QTRY_VERIFY(!window.isVisible());
    }

    void invalidThemeKeepsLastValidPreview() {
        choscordb::MainWindow window;
        auto* appearance = window.findChild<choscordb::AppearanceController*>();
        auto* theme = window.findChild<choscordb::design::ThemeManager*>();
        QTRY_VERIFY(appearance->isReady());
        QVERIFY(appearance->preview("dark"));
        QSignalSpy warning(appearance, &choscordb::AppearanceController::warningChanged);
        QVERIFY(!appearance->preview("sepia"));
        QCOMPARE(theme->mode(), choscordb::design::ThemeMode::Dark);
        QVERIFY(!warning.last().first().toString().isEmpty());
        appearance->cancelPreview();
        QCOMPARE(theme->mode(), choscordb::design::ThemeMode::System);
    }

    void themeOnlyApplyRetainsLegacyAppearanceAndGeometry() {
        using namespace choscordb;
        QTemporaryDir directory;
        const auto path = directory.filePath("legacy.sqlite");
        AppearanceLayout legacy;
        legacy.theme = "dark";
        legacy.density = "comfortable";
        legacy.accentKind = "custom";
        legacy.accent = "#FFFFFF";
        legacy.width = 1100;
        legacy.height = 760;
        legacy.navigatorWidth = 300;
        legacy.editorResultsSplit = 600;
        {
            EngineAdapter adapter(nullptr, path);
            QSignalSpy saved(&adapter, &EngineAdapter::appearanceLayoutReady);
            QVERIFY(adapter.setAppearanceLayout(legacy, 910));
            QTRY_COMPARE(saved.count(), 1);
        }
        {
            MainWindow window(nullptr, path);
            window.show();
            auto* appearance = window.findChild<AppearanceController*>();
            auto* theme = window.findChild<design::ThemeManager*>();
            QTRY_VERIFY(appearance->isReady());
            QCOMPARE(appearance->current().accent, QString("#FFFFFF"));
            QCOMPARE(appearance->current().density, QString("comfortable"));
            QCOMPARE(appearance->persisted().editorResultsSplit, quint16(600));
            QCOMPARE(window.size(), QSize(1100, 760));
            window.findChild<QAction*>("preferences")->trigger();
            auto* dialog = window.findChild<QDialog*>("preferencesDialog");
            auto* apply = dialog->findChild<QPushButton*>("preferencesApply");
            QTRY_VERIFY(apply->isEnabled());
            auto* mode = dialog->findChild<QComboBox*>("appearanceTheme");
            mode->setCurrentIndex(mode->findData("light"));
            QCOMPARE(theme->mode(), design::ThemeMode::Light);
            QSignalSpy saved(appearance, &AppearanceController::saveFinished);
            apply->click();
            QTRY_COMPARE(saved.count(), 1);
            QVERIFY(saved.first().first().toBool());
            QCOMPARE(appearance->persisted().theme, QString("light"));
            QCOMPARE(appearance->persisted().accent, QString("#FFFFFF"));
            QCOMPARE(appearance->persisted().accentKind, QString("custom"));
            QCOMPARE(appearance->persisted().density, QString("comfortable"));
            QTRY_VERIFY(!window.findChild<QDialog*>("preferencesDialog"));
            window.findChild<QAction*>("preferences")->trigger();
            dialog = window.findChild<QDialog*>("preferencesDialog");
            mode = dialog->findChild<QComboBox*>("appearanceTheme");
            QTRY_VERIFY(dialog->findChild<QPushButton*>("preferencesApply")->isEnabled());
            mode->setCurrentIndex(mode->findData("dark"));
            dialog->reject();
            QCOMPARE(theme->mode(), design::ThemeMode::Light);
            window.close();
            QTRY_VERIFY(!window.isVisible());
        }
        EngineAdapter restarted(nullptr, path);
        QSignalSpy loaded(&restarted, &EngineAdapter::appearanceLayoutReady);
        QVERIFY(restarted.getAppearanceLayout(911));
        QTRY_COMPARE(loaded.count(), 1);
        const auto value = qvariant_cast<AppearanceLayout>(loaded.first().at(2));
        QCOMPARE(value.theme, QString("light"));
        QCOMPARE(value.density, QString("comfortable"));
        QCOMPARE(value.accentKind, QString("custom"));
        QCOMPARE(value.accent, QString("#FFFFFF"));
        QCOMPARE(value.width, quint32(1100));
        QCOMPARE(value.height, quint32(760));
    }

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
        window.findChild<QAction*>("newQuery")->trigger();
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        window.findChild<QAction*>("preferences")->trigger();
        auto* dialog = window.findChild<QDialog*>("preferencesDialog");
        auto* apply = dialog->findChild<QPushButton*>("preferencesApply");
        QTRY_VERIFY(apply->isEnabled());
        dialog->findChild<QKeySequenceEdit*>("shortcut_undo")
            ->setKeySequence(QKeySequence(binding));
        apply->click();
        QTRY_VERIFY(!window.findChild<QDialog*>("preferencesDialog"));
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
        window.findChild<QAction*>("preferences")->trigger();
        dialog = window.findChild<QDialog*>("preferencesDialog");
        apply = dialog->findChild<QPushButton*>("preferencesApply");
        QTRY_VERIFY(apply->isEnabled());
        dialog->findChild<QKeySequenceEdit*>("shortcut_undo")->setKeySequence({});
        apply->click();
        QTRY_VERIFY(!window.findChild<QDialog*>("preferencesDialog"));
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
        auto* newQuery = window.findChild<QAction*>("newQuery");
        QVERIFY(newQuery != nullptr);
        newQuery->trigger();
        auto* next = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        QVERIFY(next != nullptr);
        QVERIFY(next != editor);
        QCOMPARE(next->lexer()->defaultFont().pointSize(), 23);
    }
};
QTEST_MAIN(PreferencesWorkspaceTest)
#include "preferences_workspace_test.moc"
