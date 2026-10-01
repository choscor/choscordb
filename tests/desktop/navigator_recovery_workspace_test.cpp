#include "app/appearance_controller.h"
#include "app/main_window.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "design_system/status_line/status_line.h"
#include "design_system/toast_region/toast_region.h"
#include "navigator_sql_workspace_test.h"
#include "widgets/sql_editor/sql_editor.h"
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
    auto* dismiss = toast->findChild<QToolButton*>("toastDismiss");
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
    window.showStatus("Settings storage failed", choscordb::ToastVariant::Danger, "preferences");
    window.showStatus("Wait for database work", choscordb::ToastVariant::Warning);
    window.showStatus("Saved file could not open", choscordb::ToastVariant::Danger, "saved");
    auto* preferences = window.findChild<choscordb::design::StatusLine*>("preferencesStatusLine");
    auto* saved = window.findChild<choscordb::design::StatusLine*>("savedStatusLine");
    QVERIFY(preferences && saved);
    QCOMPARE(preferences->accessibleDescription(), QString("Settings storage failed"));
    QCOMPARE(saved->accessibleDescription(), QString("Saved file could not open"));
    auto* appearance = window.findChild<choscordb::AppearanceController*>();
    QVERIFY(appearance);
    window.showToast("Export complete", choscordb::ToastVariant::Success);
    QVERIFY(QMetaObject::invokeMethod(appearance, "warningChanged", Qt::DirectConnection,
                                      Q_ARG(QString, QString{})));
    auto* toast = window.findChild<choscordb::ToastRegion*>("toastRegion");
    QVERIFY(toast->text().contains("Export complete"));
    QCOMPARE(preferences->accessibleDescription(), QString("Settings storage failed"));
}
