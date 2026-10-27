#include "app/appearance_controller.h"
#include "app/main_window.h"
#include "app/pin_store.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/status_line/status_line.h"
#include "design_system/toast_region/toast_region.h"
#include "models/navigator_model.h"
#include "navigator_sql_workspace_test.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAbstractButton>
#include <QAction>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QtTest>

void NavigatorSqlWorkspaceTest::recoveryActionsRemainInMenuWithoutToolButtons() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("workspace"));
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    QVERIFY(recovery);
    auto* menu = window.findChild<QMenu*>("workspaceRecoveryMenu");
    QVERIFY(menu);
    for (const char* name : {"retryWorkspaceRecovery", "startNewWorkspace", "closeWithoutRecovery",
                             "cancelRecoveryClose"}) {
        QVERIFY(!window.findChild<QPushButton*>(name));
        auto* action = window.findChild<QAction*>(name);
        QVERIFY(action);
        QVERIFY(menu->actions().contains(action));
        QVERIFY(!action->isEnabled());
    }
    QVERIFY(QMetaObject::invokeMethod(recovery, "errorOccurred", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("Restore failed")),
                                      Q_ARG(bool, false)));
    QVERIFY(window.findChild<QAction*>("retryWorkspaceRecovery")->isEnabled());
    QVERIFY(window.findChild<QAction*>("startNewWorkspace")->isEnabled());
    QVERIFY(!window.findChild<QAction*>("closeWithoutRecovery")->isEnabled());
    QVERIFY(!window.findChild<QAction*>("cancelRecoveryClose")->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(recovery, "errorOccurred", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("Save failed")),
                                      Q_ARG(bool, true)));
    QVERIFY(!window.findChild<QAction*>("startNewWorkspace")->isEnabled());
    QVERIFY(window.findChild<QAction*>("closeWithoutRecovery")->isEnabled());
    QVERIFY(window.findChild<QAction*>("cancelRecoveryClose")->isEnabled());
    window.findChild<QAction*>("cancelRecoveryClose")->trigger();
    for (auto* action : menu->actions())
        QVERIFY(!action->isEnabled());
}

void NavigatorSqlWorkspaceTest::recoveryFailuresUsePersistentWindowToast() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("workspace"));
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    QVERIFY(recovery);
    QVERIFY(!recovery->isReady());
    window.show();
    auto* toast =
        window.findChild<choscordb::ToastRegion*>("toastRegion", Qt::FindDirectChildrenOnly);
    QVERIFY(toast);
    auto* header = window.findChild<QWidget*>("workspaceToolbar");
    QVERIFY(header);
    const QString restoreDetail = QStringLiteral("invalid command input");
    QVERIFY(QMetaObject::invokeMethod(recovery, "errorOccurred", Qt::DirectConnection,
                                      Q_ARG(QString, restoreDetail), Q_ARG(bool, false)));
    QVERIFY(toast->isVisible());
    QCOMPARE(toast->property("variant").toString(), QStringLiteral("danger"));
    QVERIFY(
        toast->accessibleDescription().startsWith(QStringLiteral("Could not restore workspace.")));
    QVERIFY(toast->accessibleDescription().contains(restoreDetail));
    QVERIFY(toast->accessibleDescription().contains(QStringLiteral("File → Workspace recovery")));
    QVERIFY(!header->findChild<QWidget*>("workspaceRecoveryActions"));
    for (auto* label : header->findChildren<QLabel*>())
        QVERIFY(!label->text().contains(restoreDetail));
    QCOMPARE(toast->parentWidget(), &window);
    window.resize(1100, 760);
    QCoreApplication::processEvents();
    QVERIFY(toast->geometry().right() <= window.width());
    QVERIFY(toast->geometry().bottom() <= window.height());
    QVERIFY(toast->geometry().right() > window.width() - 40);
    QVERIFY(toast->geometry().bottom() > window.height() - 40);
    QTRY_VERIFY(recovery->isReady());
    QVERIFY(QMetaObject::invokeMethod(recovery, "errorOccurred", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("save error 42")),
                                      Q_ARG(bool, true)));
    QVERIFY(toast->accessibleDescription().startsWith(
        QStringLiteral("Could not save workspace before closing.")));
    QVERIFY(toast->accessibleDescription().contains(QStringLiteral("save error 42")));
    QTest::qWait(5200);
    QVERIFY(toast->isVisible());
    QVERIFY(toast->accessibleDescription().contains(QStringLiteral("save error 42")));
}

void NavigatorSqlWorkspaceTest::recoveryToastDismissalKeepsActionsAndLaterFailure() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("workspace"));
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    auto* toast =
        window.findChild<choscordb::ToastRegion*>("toastRegion", Qt::FindDirectChildrenOnly);
    QVERIFY(recovery && toast);
    QTRY_VERIFY(recovery->isReady());
    QVERIFY(QMetaObject::invokeMethod(recovery, "errorOccurred", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("autosave error 17")),
                                      Q_ARG(bool, false)));
    QVERIFY(toast->accessibleDescription().startsWith(
        QStringLiteral("Could not save workspace for recovery.")));
    QVERIFY(toast->accessibleDescription().contains(QStringLiteral("autosave error 17")));
    window.showToast(QStringLiteral("Another warning"), choscordb::ToastVariant::Warning);
    QVERIFY(toast->accessibleDescription().contains(QStringLiteral("autosave error 17")));
    QVERIFY(QMetaObject::invokeMethod(recovery, "persistenceSucceeded", Qt::DirectConnection));
    QVERIFY(toast->accessibleDescription().contains(QStringLiteral("Another warning")));
    QVERIFY(QMetaObject::invokeMethod(recovery, "errorOccurred", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("dismissed save error")),
                                      Q_ARG(bool, false)));
    auto* dismiss = toast->findChild<QAbstractButton*>("toastDismiss");
    QVERIFY(dismiss);
    dismiss->click();
    QTRY_VERIFY(!toast->accessibleDescription().contains(QStringLiteral("dismissed save error")));
    QVERIFY(window.findChild<QAction*>("retryWorkspaceRecovery")->isEnabled());
    QVERIFY(!window.findChild<QAction*>("startNewWorkspace")->isEnabled());
    QVERIFY(!window.findChild<QToolBar*>()->isEnabled());
    dismiss->click();
    QTRY_VERIFY(toast->isHidden());
    window.showToast(QStringLiteral("After dismissal"), choscordb::ToastVariant::Warning);
    QVERIFY(QMetaObject::invokeMethod(recovery, "persistenceSucceeded", Qt::DirectConnection));
    QVERIFY(toast->accessibleDescription().contains(QStringLiteral("After dismissal")));
    QVERIFY(QMetaObject::invokeMethod(recovery, "errorOccurred", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("second save error")),
                                      Q_ARG(bool, false)));
    QVERIFY(toast->accessibleDescription().contains(QStringLiteral("second save error")));
}

void NavigatorSqlWorkspaceTest::startNewWhileRestorePendingKeepsRecoveryBlocked() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("workspace"));
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    auto* toast =
        window.findChild<choscordb::ToastRegion*>("toastRegion", Qt::FindDirectChildrenOnly);
    auto* toolbar = window.findChild<QToolBar*>("queryToolbar");
    auto* startNew = window.findChild<QAction*>("startNewWorkspace");
    auto* retry = window.findChild<QAction*>("retryWorkspaceRecovery");
    QVERIFY(recovery && toast && toolbar && startNew && retry);
    QVERIFY(!recovery->isReady());
    window.show();
    QVERIFY(QMetaObject::invokeMethod(recovery, "errorOccurred", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("restore pending")),
                                      Q_ARG(bool, false)));
    QVERIFY(startNew->isEnabled());
    QVERIFY(!toolbar->isEnabled());
    startNew->trigger();
    QVERIFY(!recovery->isReady());
    QVERIFY(!toolbar->isEnabled());
    QVERIFY(retry->isEnabled());
    QVERIFY(toast->isVisible());
    QVERIFY(toast->accessibleDescription().contains(QStringLiteral("restore pending")));
}

void NavigatorSqlWorkspaceTest::startNewAfterRestoreFailureResolvesNotice() {
    QTemporaryDir storage;
    const auto path = storage.filePath("invalid.sqlite");
    QFile invalid(path);
    QVERIFY(invalid.open(QIODevice::WriteOnly));
    QVERIFY(invalid.write("not a SQLite database") > 0);
    invalid.close();
    choscordb::MainWindow window(nullptr, path);
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    auto* toast =
        window.findChild<choscordb::ToastRegion*>("toastRegion", Qt::FindDirectChildrenOnly);
    auto* startNew = window.findChild<QAction*>("startNewWorkspace");
    auto* retry = window.findChild<QAction*>("retryWorkspaceRecovery");
    auto* toolbar = window.findChild<QToolBar*>("queryToolbar");
    QVERIFY(recovery && toast && startNew && retry && toolbar);
    QTRY_VERIFY(startNew->isEnabled());
    QVERIFY(!recovery->isReady());
    QVERIFY(toast->isVisible());
    window.showToast(QStringLiteral("Independent notice"), choscordb::ToastVariant::Warning);
    QVERIFY(
        toast->accessibleDescription().startsWith(QStringLiteral("Could not restore workspace.")));
    startNew->trigger();
    QVERIFY(recovery->isReady());
    QVERIFY(!retry->isEnabled());
    QVERIFY(toolbar->isEnabled());
    QTRY_VERIFY(
        !toast->accessibleDescription().startsWith(QStringLiteral("Could not restore workspace.")));
    QVERIFY(toast->accessibleDescription().contains(QStringLiteral("Independent notice")));
}

void NavigatorSqlWorkspaceTest::rejectedRecoverySubmissionShowsBackendDetail() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("workspace"));
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    auto* appearance = window.findChild<choscordb::AppearanceController*>();
    auto* adapter = window.findChild<choscordb::EngineAdapter*>();
    auto* toast =
        window.findChild<choscordb::ToastRegion*>("toastRegion", Qt::FindDirectChildrenOnly);
    QVERIFY(recovery && appearance && adapter && toast);
    QTRY_VERIFY(recovery->isReady() && appearance->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QVERIFY(tabs && tabs->count() > 0);
    tabs->setTabText(0, QString());
    QSignalSpy failures(adapter, &choscordb::EngineAdapter::recoveryFailed);
    recovery->changed();
    recovery->flush();
    QTRY_VERIFY(!failures.isEmpty());
    QCOMPARE(failures.last().at(1).toString(), QStringLiteral("invalid command input"));
    QTRY_VERIFY(toast->isVisible());
    QCOMPARE(toast->property("variant").toString(), QStringLiteral("danger"));
    QVERIFY(toast->accessibleDescription().startsWith(
        QStringLiteral("Could not save workspace for recovery.")));
    QVERIFY(toast->accessibleDescription().contains(QStringLiteral("invalid command input")));
    auto* retry = window.findChild<QAction*>("retryWorkspaceRecovery");
    QVERIFY(retry->isEnabled());
    tabs->setTabText(0, QStringLiteral("Recovered draft"));
    retry->trigger();
    QTRY_VERIFY(!retry->isEnabled());
    QTRY_VERIFY(!toast->isVisible());
    QVERIFY(window.findChild<QToolBar*>("queryToolbar")->isEnabled());
}

void NavigatorSqlWorkspaceTest::startNavigationPreservesDocumentsAndIndependentFeedback() {
    QTemporaryDir storage;
    choscordb::MainWindow window(nullptr, storage.filePath("workspace"));
    window.show();
    auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>();
    QTRY_VERIFY(recovery->isReady());
    window.findChild<QAction*>("newQuery")->trigger();
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QVERIFY(tabs && tabs->count() == 1);
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(editor);
    editor->setText("SELECT 123;");
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Start));
    QCOMPARE(tabs->count(), 1);
    QCOMPARE(editor->text(), QString("SELECT 123;"));
    QVERIFY(window.showScreen(choscordb::MainWindow::Screen::Sql));
    auto* toast = window.findChild<choscordb::ToastRegion*>("toastRegion");
    QVERIFY(toast);
    window.reportNotice("Wait for database work");
    QVERIFY(toast->text().contains("Wait for database work"));
    // A resolved appearance warning does not remove a different notice.
    auto* appearance = window.findChild<choscordb::AppearanceController*>();
    QVERIFY(appearance);
    QVERIFY(QMetaObject::invokeMethod(appearance, "warningChanged", Qt::DirectConnection,
                                      Q_ARG(QString, QString{})));
    QVERIFY(toast->text().contains("Wait for database work"));
    window.clearNotice("Wait for database work");
    QTRY_VERIFY(!toast->text().contains("Wait for database work"));
    window.reportError("Saved file could not open");
    auto* dialog = window.findChild<choscordb::ConfirmationDialog*>("errorFeedbackDialog");
    QTRY_VERIFY(dialog && dialog->isVisible());
    QCOMPARE(dialog->text(), QString("1 error:\n\n• Saved file could not open"));
    dialog->reject();
    // An error after the list closes starts a new list.
    window.reportError("Second file could not open");
    QTRY_VERIFY(
        (dialog = window.findChild<choscordb::ConfirmationDialog*>("errorFeedbackDialog")) &&
        dialog->isVisible());
    QCOMPARE(dialog->text(), QString("1 error:\n\n• Second file could not open"));
    dialog->reject();
    QTRY_VERIFY(!window.findChild<choscordb::ConfirmationDialog*>("errorFeedbackDialog"));
    // An error waits until the open modal closes, and then shows.
    auto* blocker = new choscordb::ConfirmationDialog(QMessageBox::Information, "Busy", "Busy",
                                                      QMessageBox::Close, &window);
    blocker->setAttribute(Qt::WA_DeleteOnClose);
    blocker->open();
    QTRY_VERIFY(blocker->isVisible());
    window.reportError("Third file could not open");
    QCoreApplication::processEvents();
    QVERIFY(!window.findChild<choscordb::ConfirmationDialog*>("errorFeedbackDialog"));
    // A modal that only hides also releases the list.
    blocker->hide();
    QTRY_VERIFY(
        (dialog = window.findChild<choscordb::ConfirmationDialog*>("errorFeedbackDialog")) &&
        dialog->isVisible());
    QCOMPARE(dialog->text(), QString("1 error:\n\n• Third file could not open"));
    dialog->reject();
    delete blocker;
}

void NavigatorSqlWorkspaceTest::userActionErrorsShareOneModalWithACount() {
    using namespace choscordb;
    QTemporaryDir storage;
    const auto path = storage.filePath("workspace");
    auto pin = [](const QString& profileId, const QString& name) {
        PinRecord record;
        record.profileId = profileId;
        record.profileName = profileId;
        record.objectId = QStringLiteral("[\"main\",\"%1\"]").arg(name);
        record.name = name;
        record.qualifiedName = QStringLiteral("\"main\".\"%1\"").arg(name);
        record.kind = QStringLiteral("table");
        record.parentObjectId = QStringLiteral("[\"main\"]");
        record.ancestryIds = {record.parentObjectId};
        return record;
    };
    QString error;
    // Pins of saved connections that no longer exist fail when they expand.
    QVERIFY2(
        PinStore(path).save({pin("missing-a", "orders"), pin("missing-b", "invoices")}, &error),
        qPrintable(error));
    MainWindow window(nullptr, path);
    window.show();
    auto* pins = window.findChild<QTreeView*>("pinnedList");
    QVERIFY(pins);
    QTRY_COMPARE(pins->model()->rowCount(), 2);
    const auto dialogs = [&window] {
        QList<ConfirmationDialog*> shown;
        for (auto* dialog : window.findChildren<ConfirmationDialog*>())
            if (dialog->isVisible())
                shown.append(dialog);
        return shown;
    };
    pins->expand(pins->model()->index(0, 0));
    QTRY_COMPARE(dialogs().size(), 1);
    const auto first = dialogs().first()->text();
    // The missing profile reports once, from the reconnect.
    QVERIFY2(first.contains("saved connection is unavailable") && !first.contains("errors"),
             qPrintable(first));
    pins->expand(pins->model()->index(1, 0));
    QTRY_VERIFY(dialogs().first()->text() != first);
    QCOMPARE(dialogs().size(), 1);
    QVERIFY(dialogs().first()->text().contains("2 errors"));
    // Each failed pin stays red with its own message.
    for (int row = 0; row < 2; ++row)
        QCOMPARE(pins->model()->index(row, 0).data(NavigatorModel::StatusRole).toInt(),
                 int(RowStatusKind::Failed));
    // The sidebar has no status text line for pins.
    QVERIFY(!window.findChild<QWidget*>("pinsStatusLine"));
    dialogs().first()->reject();
    QTRY_VERIFY(dialogs().isEmpty());
}
