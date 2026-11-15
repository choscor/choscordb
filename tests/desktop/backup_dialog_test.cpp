#include "app/appearance_controller.h"
#include "app/main_window.h"
#include "app/query_workspace.h"
#include "design_system/status_line/status_line.h"
#include "widgets/backup_dialog/backup_dialog.h"
#include <QAction>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class BackupDialogTest : public QObject {
    Q_OBJECT
    static choscordb::QueryWorkspace* connectSqlite(choscordb::MainWindow& window,
                                                    const QString& path) {
        window.show();
        if (!QTest::qWaitFor([&] {
                return window.findChild<choscordb::AppearanceController*>()->isReady() &&
                       window.findChild<QAction*>("newQuery")->isEnabled();
            }))
            return nullptr;
        window.findChild<QAction*>("newQuery")->trigger();
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        workspace->connectSqlite(path);
        if (!QTest::qWaitFor([&] {
                return connected.count() == 1 && workspace->selectedConnection().has_value();
            }))
            return nullptr;
        return workspace;
    }
    static choscordb::BackupDialog* openDialog(choscordb::MainWindow& window, const char* action) {
        window.findChild<QAction*>(action)->trigger();
        choscordb::BackupDialog* dialog = nullptr;
        const bool shown = QTest::qWaitFor([&] {
            dialog = window.findChild<choscordb::BackupDialog*>();
            return dialog && dialog->isVisible();
        });
        return shown ? dialog : nullptr;
    }
    static QString status(QWidget* dialog) {
        return dialog->findChild<choscordb::design::StatusLine*>("backupStatusLine")
            ->findChild<QLabel*>()
            ->text();
    }

  private slots:
    void backupCopiesASqliteDatabaseAndShowsTheCommand() {
        QTemporaryDir folder;
        QFile file(folder.filePath("main.sqlite"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        choscordb::MainWindow window(nullptr, folder.filePath("settings.store"));
        QVERIFY(connectSqlite(window, folder.filePath("main.sqlite")));
        auto* dialog = openDialog(window, "backupDatabase");
        QVERIFY(dialog);
        QVERIFY(!dialog->findChild<QLineEdit*>("backupToolFolder")->isVisibleTo(dialog));
        QVERIFY(dialog->findChildren<QCheckBox*>().isEmpty());
        const auto target = folder.filePath("copy.sqlite");
        dialog->findChild<QLineEdit*>("backupPath")->setText(target);
        auto* output = dialog->findChild<QPlainTextEdit*>("backupOutput");
        QString shown;
        connect(output, &QPlainTextEdit::textChanged, output,
                [&shown, output] { shown = output->toPlainText(); });
        QPointer<choscordb::BackupDialog> alive(dialog);
        dialog->findChild<QPushButton*>("backupStart")->click();
        QTRY_VERIFY(alive.isNull());
        QVERIFY2(shown.startsWith("VACUUM INTO '"), qPrintable(shown));
        QVERIFY(QFileInfo(target).isFile());
    }
    void sqliteRestoreShowsTheReason() {
        QTemporaryDir folder;
        QFile file(folder.filePath("main.sqlite"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        choscordb::MainWindow window(nullptr, folder.filePath("settings.store"));
        QVERIFY(connectSqlite(window, folder.filePath("main.sqlite")));
        auto* dialog = openDialog(window, "restoreDatabase");
        QVERIFY(dialog);
        QCOMPARE(status(dialog), QString("ChoscorDB cannot restore over an open SQLite database. "
                                         "Open the backup file as a new connection."));
        QVERIFY(!dialog->findChild<QPushButton*>("backupStart")->isEnabled());
        dialog->findChild<QPushButton*>("backupCancel")->click();
    }
    void serverDialogsShowToolOptionsAndRequestErrors() {
        choscordb::EngineAdapter adapter;
        choscordb::BackupDialog dialog(&adapter, {1, "postgres", {}, "shop", false});
        dialog.show();
        QVERIFY(dialog.findChild<QLineEdit*>("backupToolFolder")->isVisibleTo(&dialog));
        auto* custom = dialog.findChild<QCheckBox*>("backupOption_custom_format");
        QVERIFY(custom && custom->isChecked());
        QVERIFY(!dialog.findChild<QCheckBox*>("backupOption_schema_only")->isChecked());
        dialog.findChild<QLineEdit*>("backupPath")->setText("shop.dump");
        dialog.findChild<QPushButton*>("backupStart")->click();
        QVERIFY(status(&dialog).contains("Enter the full path of the file."));
        QCOMPARE(dialog.findChild<QPushButton*>("backupStart")->text(), QString("Retry"));

        choscordb::BackupDialog restore(&adapter, {1, "mysql", {}, "shop", true});
        QCOMPARE(restore.findChildren<QCheckBox*>().size(), 1);
        QCOMPARE(restore.findChild<QPushButton*>("backupStart")->text(), QString("Restore"));
    }
};

QTEST_MAIN(BackupDialogTest)
#include "backup_dialog_test.moc"
