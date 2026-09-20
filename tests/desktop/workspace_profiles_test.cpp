#include "app/appearance_controller.h"
#include "app/application_data.h"
#include "app/main_window.h"
#include "app/query_workspace.h"
#include "app/updater.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/field/field.h"
#include "models/history_model.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/history_dock/history_dock.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/search_panel/search_panel.h"
#include "widgets/sql_editor/sql_editor.h"
#include "workspace_test.h"
#include "workspace_test_fixture.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

void WorkspaceTest::savedProfilesCreateDuplicateTestDeleteAndConnect() {
    WorkspaceFixture f;
    QTRY_COMPARE(f.connections.count(), 1);
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
    QVERIFY(dialog);
    auto* save = dialog->findChild<QPushButton*>("profileSave");
    auto* list = dialog->findChild<QListWidget*>("profileList");
    auto* status = dialog->findChild<QLabel*>("profileStatus");
    QVERIFY(save && list && status);
    QTRY_VERIFY(save->isEnabled());
    choscordb::SavedProfile profile;
    profile.id = "native-local";
    profile.name = "Saved SQLite";
    profile.path = ":memory:";
    dialog->saveDraft(profile);
    QTRY_COMPARE(list->count(), 1);
    QTRY_VERIFY(save->isEnabled());
    dialog->testDraft(profile);
    QTRY_VERIFY(status->text().contains("succeeded"));
    QCOMPARE(f.connections.count(), 1);
    dialog->findChild<QPushButton*>("profileDuplicate")->click();
    QTRY_COMPARE(list->count(), 2);
    QTRY_VERIFY(save->isEnabled());
    QTimer::singleShot(0, dialog, [] {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* box = qobject_cast<QMessageBox*>(widget))
                box->done(QMessageBox::Yes);
    });
    dialog->findChild<QPushButton*>("profileDelete")->click();
    QTRY_COMPARE(list->count(), 1);
    dialog->selectProfile(profile.id);
    dialog->findChild<QPushButton*>("profileConnect")->click();
    QTRY_COMPARE(f.connections.count(), 2);
    // Connecting another session leaves this draft's existing target intact.
    QCOMPARE(f.connections.currentIndex(), 0);
    f.connections.setCurrentIndex(1);
    QCOMPARE(f.connections.currentText(), QString("Saved SQLite"));
    QVERIFY(dialog->grab().save("native-profiles.png"));
    f.execute("SELECT 7");
    QTRY_VERIFY(f.run.isEnabled());
    QCOMPARE(f.editor.property("profileId").toString(), profile.id);
    f.connections.setCurrentIndex(0);
    f.messages.clear();
    f.execute("SELECT 8");
    QTRY_VERIFY(f.run.isEnabled());
    QVERIFY(f.editor.property("profileId").toString().isEmpty());
    QTRY_VERIFY(f.messages.toPlainText().contains("Completed"));
    QSignalSpy history(f.workspace.adapter(), &choscordb::EngineAdapter::historyListed);
    QVERIFY(f.workspace.adapter()->listHistory(100, 0, 777));
    QTRY_COMPARE(history.count(), 1);
    const auto entries = qvariant_cast<QList<choscordb::SavedHistoryEntry>>(history.at(0).at(1));
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries[0].sql, QString("SELECT 8"));
    QVERIFY(entries[0].profileId.isEmpty());
    QCOMPARE(entries[1].sql, QString("SELECT 7"));
    QCOMPARE(entries[1].profileId, profile.id);
}

void WorkspaceTest::profileAdapterPersistsAcrossRestart() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto storagePath = directory.filePath("metadata.sqlite");
    {
        choscordb::EngineAdapter adapter(nullptr, storagePath);
        bool saved = false;
        connect(&adapter, &choscordb::EngineAdapter::profileSaved, &adapter,
                [&saved](quint64 token, const choscordb::SavedProfile&) { saved = token == 77; });
        choscordb::SavedProfile profile;
        profile.id = "persisted";
        profile.name = "Persistent SQLite";
        profile.path = ":memory:";
        adapter.saveProfile(profile, 77);
        QTRY_VERIFY(saved);
    }
    choscordb::EngineAdapter adapter(nullptr, storagePath);
    QList<choscordb::SavedProfile> profiles;
    bool listed = false;
    connect(&adapter, &choscordb::EngineAdapter::profilesReady, &adapter,
            [&](quint64 token, const QList<choscordb::SavedProfile>& result) {
                if (token == 78) {
                    profiles = result;
                    listed = true;
                }
            });
    adapter.listProfiles(78);
    QTRY_VERIFY(listed);
    QCOMPARE(profiles.size(), 1);
    QCOMPARE(profiles[0].name, QString("Persistent SQLite"));
}

void WorkspaceTest::profileFailuresKeepDraftAndShowConnectionCodes() {
    WorkspaceFixture f;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QTRY_COMPARE(f.connections.count(), 1);
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
    QVERIFY(dialog);
    auto* save = dialog->findChild<QPushButton*>("profileSave");
    auto* status = dialog->findChild<QLabel*>("profileStatus");
    auto* name = dialog->findChild<QLineEdit*>("profileName");
    QVERIFY(save && status && name);
    QTRY_VERIFY(save->isEnabled());
    choscordb::SavedProfile profile;
    profile.id = "invalid-path";
    profile.name = "Unsaved draft";
    dialog->saveDraft(profile);
    QTRY_VERIFY(save->isEnabled());
    QCOMPARE(name->text(), QString("Unsaved draft"));
    QVERIFY(status->text().contains("Invalid", Qt::CaseInsensitive));
    profile.path = directory.filePath("missing/data.sqlite");
    dialog->testDraft(profile);
    QTRY_VERIFY(status->text().contains("[Code: 14]"));
    QCOMPARE(f.connections.count(), 1);
    dialog->saveDraft(profile);
    QTRY_VERIFY(save->isEnabled());
    auto* open = dialog->findChild<QPushButton*>("profileConnect");
    QVERIFY(open);
    open->click();
    QVERIFY(!open->isEnabled());
    QTRY_VERIFY(open->isEnabled());
    QVERIFY(status->text().contains("[Code: 14]"));
    QCOMPARE(status->textFormat(), Qt::PlainText);
    QCOMPARE(f.connections.count(), 1);
}

void WorkspaceTest::passwordDraftSurvivesTestConnectAndFailedRemember() {
    WorkspaceFixture f;
    QTRY_COMPARE(f.connections.count(), 1);
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
    QVERIFY(dialog);
    auto* save = dialog->findChild<QPushButton*>("profileSave");
    auto* password = dialog->findChild<QLineEdit*>("profilePassword");
    auto* remember = dialog->findChild<QCheckBox*>("profileRememberPassword");
    auto* status = dialog->findChild<QLabel*>("profileStatus");
    QVERIFY(save && password && remember && status);
    QTRY_VERIFY(save->isEnabled());
    choscordb::SavedProfile profile;
    profile.id = "password-draft";
    profile.name = "PostgreSQL draft";
    profile.driver = "postgres";
    profile.host = "localhost";
    profile.database = "app";
    profile.user = "user";
    profile.port = 1;
    dialog->saveDraft(profile);
    QTRY_VERIFY(save->isEnabled());
    QCOMPARE(password->echoMode(), QLineEdit::Password);
    password->setText("draft-secret");
    password->setModified(true);
    dialog->testDraft(profile);
    QTRY_VERIFY(save->isEnabled());
    QCOMPARE(password->text(), QString("draft-secret"));
    QVERIFY(password->isModified());
    dialog->findChild<QPushButton*>("profileConnect")->click();
    QTRY_VERIFY(save->isEnabled());
    QCOMPARE(password->text(), QString("draft-secret"));
    dialog->saveDraft(profile);
    QTRY_VERIFY(save->isEnabled());
    QCOMPARE(password->text(), QString("draft-secret"));
    remember->setChecked(true);
    dialog->saveDraft(profile);
    QTRY_VERIFY(save->isEnabled());
    QVERIFY(status->text().contains("unavailable", Qt::CaseInsensitive));
    QCOMPARE(password->text(), QString("draft-secret"));
    QVERIFY(!status->text().contains("draft-secret"));
    QVERIFY(dialog->grab().save("native-profile-password.png"));
}

void WorkspaceTest::mysqlSelectedScriptNavigatesDifferentResultSchemas() {
    if (qEnvironmentVariable("CHOSCORDB_TEST_MYSQL").isEmpty())
        QSKIP("Requires the isolated MySQL fixture environment");
    WorkspaceFixture f;
    f.connectMysqlLive();
    QTRY_COMPARE(f.connections.count(), 2);
    f.editor.setText("SELECT 11 AS first_answer; SELECT 'second' AS second_answer;");
    f.editor.selectAll();
    f.runConfirmedSql();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QCOMPARE(f.grid.model()->headerData(0, Qt::Horizontal).toString(),
             QString("first_answer · LONGLONG"));
    QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("11"));
    QTRY_VERIFY(f.next.isEnabled());
    QCOMPARE(f.next.toolTip(), QString("Next result"));
    // Refreshing the session mode must preserve these results if the user
    // cancels the new statement's confirmation.
    f.editor.setText("DELETE FROM mysql_confirmation_must_not_execute");
    bool rejected = false;
    QTimer reject;
    connect(&reject, &QTimer::timeout, &f.parent, [&] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (box && box->windowTitle() == "Confirm SQL execution") {
            rejected = true;
            box->button(QMessageBox::Cancel)->click();
        }
    });
    reject.start(10);
    f.run.trigger();
    QTRY_VERIFY(rejected);
    reject.stop();
    QTRY_VERIFY(f.next.isEnabled());
    f.next.click();
    QTRY_COMPARE(f.grid.model()->headerData(0, Qt::Horizontal).toString(),
                 QString("second_answer · VAR_STRING"));
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("second"));
    QTRY_VERIFY(f.run.isEnabled());
    QVERIFY(!f.next.isEnabled());
}

void WorkspaceTest::mysqlGridEditsReviewBoundValuesAndPersistChanges() {
    if (qEnvironmentVariable("CHOSCORDB_TEST_MYSQL").isEmpty())
        QSKIP("Requires the isolated MySQL fixture environment");
    WorkspaceFixture f(true);
    f.connectMysqlLive();
    QTRY_COMPARE(f.connections.count(), 2);
    for (const QString& sql :
         {QString("DROP TABLE IF EXISTS mysql_workspace_edits"),
          QString("CREATE TABLE mysql_workspace_edits (id INT PRIMARY KEY, label VARCHAR(100))"),
          QString("INSERT INTO mysql_workspace_edits VALUES (1, 'before')")}) {
        QTRY_VERIFY(f.run.isEnabled());
        f.editor.setText(sql);
        f.editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        f.runConfirmedSql();
        QTRY_VERIFY(f.run.isEnabled());
        QTRY_VERIFY2(f.messages.toPlainText().contains("Completed"),
                     qPrintable(f.messages.toPlainText()));
    }
    f.execute("SELECT id, label FROM mysql_workspace_edits");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    const auto label = f.grid.model()->index(0, 1);
    QTRY_VERIFY(f.grid.model()->flags(label).testFlag(Qt::ItemIsEditable));
    const QString changed = "after 'quoted' \\ value";
    QVERIFY(f.grid.model()->setData(label, changed, Qt::EditRole));
    QTRY_VERIFY(f.applyEdits.isEnabled());
    QString review;
    QTimer accept;
    QObject::connect(&accept, &QTimer::timeout, &f.parent, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->windowTitle() != "Review grid changes")
            return;
        auto* preview = dialog->findChild<QPlainTextEdit*>("gridEditReview");
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (preview && buttons) {
            review = preview->toPlainText();
            buttons->button(QDialogButtonBox::Ok)->click();
        }
    });
    accept.start(10);
    f.applyEdits.click();
    accept.stop();
    QVERIFY(review.contains("UPDATE `choscordb_test`.`mysql_workspace_edits` SET `label` = ?"));
    QVERIFY(review.contains("`id` <=> ?"));
    QVERIFY(review.contains("`label` <=> ?"));
    QVERIFY(review.contains("Parameter 1: text \"after 'quoted' \\\\ value\""));
    QTRY_VERIFY(!f.workspace.hasPendingEdits());
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("SELECT label AS persisted FROM mysql_workspace_edits WHERE id=1");
    QTRY_COMPARE(f.grid.model()->headerData(0, Qt::Horizontal).toString(),
                 QString("persisted · VAR_STRING"));
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), changed);
    QTRY_VERIFY(f.run.isEnabled());
    f.editor.setText("DROP TABLE mysql_workspace_edits");
    f.editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
    f.runConfirmedSql();
    QTRY_VERIFY(f.run.isEnabled());
    QTRY_VERIFY(f.messages.toPlainText().contains("Completed"));
}

void WorkspaceTest::mysqlModeRefreshDoesNotExecuteChangedEditorInput() {
    if (qEnvironmentVariable("CHOSCORDB_TEST_MYSQL").isEmpty())
        QSKIP("Requires the isolated MySQL fixture environment");
    WorkspaceFixture f;
    f.connectMysqlLive();
    QSignalSpy states(&f.workspace, &choscordb::QueryWorkspace::executionStateChanged);
    int replies = 0;
    connect(f.workspace.adapter(), &choscordb::EngineAdapter::eventReady, &f.parent,
            [&](const choscordb::BridgeEvent& e) {
                if (e.kind == "session_sql_mode" && e.request_token != 0)
                    ++replies;
            });
    f.editor.setText("SELECT 41");
    f.run.trigger();
    // The asynchronous reply has not entered the GUI event loop yet.
    f.editor.setText("SELECT 42");
    QTRY_COMPARE(replies, 1);
    QCOMPARE(states.count(), 0);
    QCOMPARE(f.grid.model()->rowCount(), 0);
    f.runConfirmedSql();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("42"));
}

void WorkspaceTest::mysqlSessionModeChangesSubsequentCursorExecution() {
    if (qEnvironmentVariable("CHOSCORDB_TEST_MYSQL").isEmpty())
        QSKIP("Requires the isolated MySQL fixture environment");
    WorkspaceFixture f;
    f.connectMysqlLive();
    QTRY_COMPARE(f.connections.count(), 2);
    f.editor.setText("SET SESSION sql_mode='NO_BACKSLASH_ESCAPES'");
    f.editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
    f.runConfirmedSql();
    QTRY_VERIFY(f.run.isEnabled());
    QTRY_VERIFY2(f.messages.toPlainText().contains("Completed"),
                 qPrintable(f.messages.toPlainText()));
    // In the default mode this backslash escapes the closing quote, so
    // the second SELECT would incorrectly belong to the first statement.
    const QString sql = "SELECT 'backslash\\'; SELECT 29 AS mode_result;";
    f.editor.setText(sql);
    f.editor.SendScintilla(QsciScintilla::SCI_GOTOPOS,
                           static_cast<int>(sql.indexOf("SELECT 29") + 7));
    f.runConfirmedSql();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QCOMPARE(f.grid.model()->headerData(0, Qt::Horizontal).toString(),
             QString("mode_result · LONGLONG"));
    QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("29"));
    QTRY_VERIFY(f.run.isEnabled());
    QVERIFY(!f.next.isEnabled());
}

void WorkspaceTest::postgresProfileExecutesPagesAndCancels() {
    if (qEnvironmentVariable("CHOSCORDB_TEST_POSTGRES_PORT").isEmpty())
        QSKIP("Requires the isolated PostgreSQL fixture environment");
    WorkspaceFixture f;
    QTRY_COMPARE(f.connections.count(), 1);
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
    QVERIFY(dialog);
    auto* save = dialog->findChild<QPushButton*>("profileSave");
    auto* password = dialog->findChild<QLineEdit*>("profilePassword");
    auto* status = dialog->findChild<QLabel*>("profileStatus");
    QVERIFY(save && password && status);
    QTRY_VERIFY(save->isEnabled());
    choscordb::SavedProfile profile;
    profile.id = "postgres-live";
    profile.name = "PostgreSQL integration";
    profile.driver = "postgres";
    profile.host = qEnvironmentVariable("CHOSCORDB_TEST_POSTGRES_HOST", "localhost");
    profile.port = qEnvironmentVariable("CHOSCORDB_TEST_POSTGRES_PORT").toUShort();
    profile.database = qEnvironmentVariable("CHOSCORDB_TEST_POSTGRES_DATABASE", "postgres");
    profile.user = qEnvironmentVariable("CHOSCORDB_TEST_POSTGRES_USER", "choscordb");
    profile.rootCertificate = qEnvironmentVariable("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE");
    profile.tls = "verify_full";
    dialog->saveDraft(profile);
    QTRY_VERIFY(save->isEnabled());
    password->setText(qEnvironmentVariable("CHOSCORDB_TEST_POSTGRES_PASSWORD"));
    password->setModified(true);
    dialog->testDraft(profile);
    QTRY_VERIFY2(status->text().contains("succeeded"), qPrintable(status->text()));
    QCOMPARE(f.connections.count(), 1);
    dialog->findChild<QPushButton*>("profileConnect")->click();
    QTRY_COMPARE(f.connections.count(), 2);
    f.connections.setCurrentIndex(1);
    f.execute("SELECT i::numeric(30,8) AS value FROM generate_series(1,1001) AS i");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
    QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("1.00000000"));
    f.next.click();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(),
             QString("1001.00000000"));
    f.previous.click();
    QTRY_COMPARE(f.grid.model()->rowCount(), 1000);
    f.execute("SELECT pg_sleep(30)");
    QTRY_VERIFY(f.cancel.isEnabled());
    f.cancel.trigger();
    QTRY_VERIFY(f.run.isEnabled());
    QTRY_VERIFY2(f.messages.toPlainText().contains("cancel", Qt::CaseInsensitive),
                 qPrintable(f.messages.toPlainText()));
    f.messages.clear();
    f.execute("SELECT 7::bigint AS recovered");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("7"));
    QTRY_VERIFY(f.messages.toPlainText().contains("Completed"));
    QSignalSpy history(f.workspace.adapter(), &choscordb::EngineAdapter::historyListed);
    QVERIFY(f.workspace.adapter()->listHistory(100, 0, 919));
    QTRY_COMPARE(history.count(), 1);
    const auto entries = qvariant_cast<QList<choscordb::SavedHistoryEntry>>(history.at(0).at(1));
    QCOMPARE(entries.size(), 3);
    for (const auto& entry : entries)
        QCOMPARE(entry.profileId, profile.id);
    QCOMPARE(entries[0].sql, QString("SELECT 7::bigint AS recovered"));
    QCOMPARE(entries[0].rowCount, quint64(1));
    QCOMPARE(entries[1].status, QString("cancelled"));
    QVERIFY(!entries[1].hasRowCount);
    QCOMPARE(entries[2].rowCount, quint64(1001));
    QVERIFY(entries[2].hasRowCount);
}

void WorkspaceTest::writesCommitAndRollbackComplete() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("CREATE TABLE t(x INTEGER)");
    QTRY_VERIFY(f.messages.toPlainText().contains("rows affected"));
    QTRY_VERIFY(f.run.isEnabled());
    f.mode.setCurrentIndex(1);
    f.messages.clear();
    f.execute("INSERT INTO t VALUES(1)");
    QTRY_VERIFY(f.messages.toPlainText().contains("1 rows affected"));
    QTRY_VERIFY(f.rollback.isEnabled());
    f.mode.setCurrentIndex(0);
    QCOMPARE(f.mode.currentIndex(), 1);
    QVERIFY(f.messages.toPlainText().contains("Commit or roll back"));
    f.rollback.trigger();
    QTRY_VERIFY(f.messages.toPlainText().contains("rolled back"));
    f.messages.clear();
    f.execute("INSERT INTO t VALUES(2)");
    QTRY_VERIFY(f.messages.toPlainText().contains("1 rows affected"));
    f.commit.trigger();
    QTRY_VERIFY(f.messages.toPlainText().contains("committed"));
    f.execute("SELECT x FROM t");
    QTRY_COMPARE(f.grid.model()->rowCount(), 1);
    QCOMPARE(f.grid.model()->data(f.grid.model()->index(0, 0)).toString(), QString("2"));
}

void WorkspaceTest::cancelsRunningQuery() {
    WorkspaceFixture f;
    QTRY_VERIFY(f.run.isEnabled());
    f.execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<100000000) "
              "SELECT sum(x) FROM n");
    QTRY_VERIFY(f.cancel.isEnabled());
    f.cancel.trigger();
    QTRY_VERIFY(f.run.isEnabled());
    QVERIFY(f.grid.model()->rowCount() == 0);
}
