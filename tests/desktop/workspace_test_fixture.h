#pragma once

#include "design_system/dialog_presentation/dialog_presentation.h"

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

struct WorkspaceFixture {
    QWidget parent;
    QComboBox connections, mode;
    QAction run, cancel, commit, rollback, newConnection;
    QPushButton next, previous, exportResult;
    QPushButton addRow, deleteRows, setNull, applyEdits, discardEdits;
    QLabel summary;
    QPlainTextEdit messages;
    QTableView grid;
    choscordb::SqlEditor editor;
    choscordb::QueryWorkspace workspace;
    explicit WorkspaceFixture(bool editing = false)
        : workspace({&connections,
                     &mode,
                     &run,
                     &cancel,
                     &commit,
                     &rollback,
                     &newConnection,
                     &next,
                     &summary,
                     &messages,
                     &grid,
                     [this] { return &editor; },
                     &parent,
                     &previous,
                     &exportResult,
                     {},
                     nullptr,
                     false,
                     editing ? &addRow : nullptr,
                     editing ? &deleteRows : nullptr,
                     editing ? &setNull : nullptr,
                     editing ? &applyEdits : nullptr,
                     editing ? &discardEdits : nullptr}) {
        parent.resize(960, 640);
        parent.show();
        mode.addItems({"Auto-commit", "Manual"});
        connections.setEnabled(false);
        mode.setEnabled(false);
        workspace.connectSqlite(":memory:");
    }
    void connectMysqlLive() {
        QTRY_COMPARE(connections.count(), 1);
        newConnection.trigger();
        auto* dialog = parent.findChild<choscordb::ProfileDialog*>("profileDialog");
        QVERIFY(dialog);
        auto* save = dialog->findChild<QPushButton*>("profileSave");
        auto* password = dialog->findChild<QLineEdit*>("profilePassword");
        auto* connect = dialog->findChild<QPushButton*>("profileConnect");
        QVERIFY(save && password && connect);
        QTRY_VERIFY(save->isEnabled());
        choscordb::SavedProfile profile;
        profile.id = "mysql-live-workspace";
        profile.name = "MySQL integration";
        profile.driver = "mysql";
        profile.host = "127.0.0.1";
        profile.port = qEnvironmentVariable("CHOSCORDB_MYSQL_PORT", "33306").toUShort();
        profile.database = "choscordb_test";
        profile.user = "root";
        profile.tls = "disable";
        dialog->saveDraft(profile);
        QTRY_VERIFY(save->isEnabled());
        password->setText("choscordb-test-password");
        password->setModified(true);
        connect->click();
        QTRY_COMPARE(connections.count(), 2);
        connections.setCurrentIndex(1);
        QTRY_VERIFY(run.isEnabled());
        QCOMPARE(workspace.driverForConnection(connections.currentData().toULongLong()),
                 QString("mysql"));
    }
    void runConfirmedSql() {
        // Accept only the SQL confirmation dialog through its public button.
        // A repeating timer also handles prompts reached after nested events.
        QTimer confirm;
        QObject::connect(&confirm, &QTimer::timeout, &parent, [] {
            auto* box =
                qobject_cast<QMessageBox*>(choscordb::design::DialogPresentation::activeDialog());
            if (box && box->windowTitle() == "Confirm SQL execution") {
                if (auto* yes = box->button(QMessageBox::Yes))
                    yes->click();
            }
        });
        QSignalSpy submitted(&workspace, &choscordb::QueryWorkspace::executionStateChanged);
        confirm.start(10);
        run.trigger();
        QTRY_VERIFY(!submitted.isEmpty());
    }
    void execute(const QString& sql) {
        QTRY_VERIFY(run.isEnabled());
        QVERIFY2(!choscordb::EngineAdapter::executionRange(sql, 0, 0, 0).confirmation,
                 "Test SQL must not trigger confirmation");
        editor.setText(sql);
        editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, 0);
        run.trigger();
    }
};
