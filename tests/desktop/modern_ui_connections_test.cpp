#include "app/main_window.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "design_system/menu/menu.h"
#include "design_system/theme_manager.h"
#include "models/navigator_model.h"
#include "modern_ui_test.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>

void ModernUiTest::sidebarConnectionDoesNotColorUnavailableSqlTargetFooter() {
    choscordb::MainWindow window;
    window.show();
    auto* newQuery = window.findChild<QAction*>("newQuery");
    QTRY_VERIFY(newQuery->isEnabled());
    newQuery->trigger();
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* theme = window.findChild<choscordb::design::ThemeManager*>();
    auto* footer = window.findChild<QWidget*>("sqlResultFooter");
    auto* profiles = window.findChild<QListWidget*>("savedConnections");
    QVERIFY(editor && workspace && theme && footer && profiles);
    editor->setConnectionTarget(quint64{0xF00D}, "Unavailable target");
    QCOMPARE(footer->palette().color(QPalette::Window),
             theme->resolvedTheme().colors.dangerSurface);

    choscordb::SavedProfile profile;
    profile.id = "sidebar-only";
    profile.name = "Sidebar Only";
    profile.path = ":memory:";
    workspace->adapter()->saveProfile(profile, 8200);
    QTRY_COMPARE(profiles->count(), 1);
    QSignalSpy opened(workspace, &choscordb::QueryWorkspace::connectionReady);
    QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                      profiles->visualItemRect(profiles->item(0)).center());
    QTRY_COMPARE(opened.count(), 1);
    QTRY_VERIFY(window.browsingConnection().has_value());
    QVERIFY(editor->connectionTarget() == quint64{0xF00D});
    QCOMPARE(footer->palette().color(QPalette::Window),
             theme->resolvedTheme().colors.dangerSurface);
    for (const auto mode :
         {choscordb::design::ThemeMode::Light, choscordb::design::ThemeMode::Dark}) {
        theme->setMode(mode);
        const auto colors = theme->resolvedTheme().colors;
        QCOMPARE(footer->palette().color(QPalette::Window), colors.dangerSurface);
        QVERIFY(choscordb::design::contrastRatio(colors.text, colors.dangerSurface) >= 4.5);
        editor->setConnectionTarget(*window.browsingConnection(), "Sidebar Only");
        QCOMPARE(footer->palette().color(QPalette::Window), colors.successSurface);
        QVERIFY(choscordb::design::contrastRatio(colors.text, colors.successSurface) >= 4.5);
        editor->setConnectionTarget(quint64{0xF00D}, "Unavailable target");
    }
    const auto browsingId = *window.browsingConnection();
    editor->setConnectionTarget(browsingId, "Sidebar Only");
    QCOMPARE(footer->palette().color(QPalette::Window),
             theme->resolvedTheme().colors.successSurface);
    QVERIFY(workspace->adapter()->disconnectConnection(browsingId));
    QTRY_COMPARE(footer->palette().color(QPalette::Window),
                 theme->resolvedTheme().colors.dangerSurface);
    QSignalSpy reconnected(workspace, &choscordb::QueryWorkspace::connectionReady);
    workspace->connectSqlite(":memory:");
    QTRY_COMPARE(reconnected.count(), 1);
    QCOMPARE(footer->palette().color(QPalette::Window),
             theme->resolvedTheme().colors.dangerSurface);
    editor->setConnectionTarget(reconnected.at(0).at(0).toULongLong(), "Reconnected");
    QCOMPARE(footer->palette().color(QPalette::Window),
             theme->resolvedTheme().colors.successSurface);
    editor->setConnectionTarget(quint64{0xF00D}, "Unavailable target");
    theme->setForcedContrast(true);
    QCOMPARE(footer->palette().color(QPalette::Window),
             theme->resolvedTheme().colors.dangerSurface);
    QVERIFY(footer->accessibleName().contains("unavailable", Qt::CaseInsensitive));
    QVERIFY(choscordb::design::contrastRatio(theme->resolvedTheme().colors.text,
                                             footer->palette().color(QPalette::Window)) >= 4.5);
    editor->setConnectionTarget(reconnected.at(0).at(0).toULongLong(), "Reconnected");
    QCOMPARE(footer->palette().color(QPalette::Window),
             theme->resolvedTheme().colors.successSurface);
    QCOMPARE(footer->accessibleName(), QString("SQL target available"));
}

void ModernUiTest::savedProfileSelectionKeepsEditorTargetAndShowsMultipleTrees() {
    choscordb::MainWindow window;
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* profiles = window.findChild<QListWidget*>("savedConnections");
    auto* tree = window.findChild<QTreeView*>("databaseNavigator");
    auto* selector = window.findChild<QComboBox*>("connectionSelector");
    QVERIFY(workspace && profiles && tree && selector);
    QSignalSpy opened(workspace, &choscordb::QueryWorkspace::connectionReady);
    choscordb::SavedProfile profile;
    profile.id = "browse-alpha";
    profile.name = "Browse Alpha";
    profile.path = ":memory:";
    workspace->adapter()->saveProfile(profile, 8101);
    profile.id = "browse-beta";
    profile.name = "Browse Beta";
    workspace->adapter()->saveProfile(profile, 8102);
    QTRY_COMPARE(profiles->count(), 2);
    auto click = [profiles](const QString& name) {
        for (int row = 0; row < profiles->count(); ++row)
            if (profiles->item(row)->text().startsWith(name)) {
                QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                                  profiles->visualItemRect(profiles->item(row)).center());
                return;
            }
    };
    click("Browse Alpha");
    QTRY_VERIFY(window.browsingConnection().has_value());
    const auto first = *window.browsingConnection();
    QTRY_COMPARE(tree->model()->rowCount(), 1);
    QVERIFY(tree->model()->index(0, 0).data().toString().contains("Browse Alpha"));
    const auto editorTarget = selector->currentData();
    click("Browse Beta");
    QTRY_COMPARE(tree->model()->rowCount(), 2);
    QTRY_COMPARE(opened.count(), 2);
    QVERIFY(tree->model()->index(0, 0).data().toString().contains("Browse Alpha"));
    QVERIFY(tree->model()->index(1, 0).data().toString().contains("Browse Beta"));
    QVERIFY(profiles->item(0)->isSelected());
    QVERIFY(profiles->item(1)->isSelected());
    QCOMPARE(selector->currentData(), editorTarget);
    click("Browse Alpha");
    QTRY_COMPARE(tree->model()->rowCount(), 1);
    QVERIFY(tree->model()->index(0, 0).data().toString().contains("Browse Beta"));
    QVERIFY(!profiles->item(0)->isSelected());
    QVERIFY(profiles->item(1)->isSelected());
    QVERIFY(selector->findData(QVariant::fromValue<qulonglong>(first)) >= 0);
    click("Browse Alpha");
    QTRY_COMPARE(tree->model()->rowCount(), 2);
    QCOMPARE(opened.count(), 2);
    QVERIFY(selector->findData(QVariant::fromValue<qulonglong>(first)) >= 0);
    profiles->setCurrentItem(profiles->item(1), QItemSelectionModel::NoUpdate);
    profiles->setFocus();
    QTest::keyClick(profiles, Qt::Key_Return);
    QTRY_COMPARE(tree->model()->rowCount(), 1);
    QVERIFY(profiles->item(0)->isSelected());
    QVERIFY(!profiles->item(1)->isSelected());
    profiles->setCurrentItem(profiles->item(0), QItemSelectionModel::NoUpdate);
    QTest::keyClick(profiles, Qt::Key_Space);
    QTRY_COMPARE(tree->model()->rowCount(), 0);
    QVERIFY(!profiles->item(0)->isSelected());
    click("Browse Beta");
    click("Browse Alpha");
    QTRY_COMPARE(tree->model()->rowCount(), 2);
    QVERIFY(tree->model()->index(0, 0).data().toString().contains("Browse Alpha"));
    QVERIFY(tree->model()->index(1, 0).data().toString().contains("Browse Beta"));
    QCOMPARE(opened.count(), 2);
    QVERIFY(workspace->adapter()->disconnectConnection(first));
    QTRY_COMPARE(tree->model()->rowCount(), 1);
    QVERIFY(tree->model()->index(0, 0).data().toString().contains("Browse Beta"));
    QVERIFY(!profiles->item(0)->isSelected());
    QVERIFY(profiles->item(1)->isSelected());
}

void ModernUiTest::savedProfileSelectionSurvivesRefreshEditAndDeletionOnlyForExistingIds() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto store = directory.filePath("profiles.sqlite");
    {
        choscordb::MainWindow window(nullptr, store);
        window.show();
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        auto* profiles = window.findChild<QListWidget*>("savedConnections");
        auto* tree = window.findChild<QTreeView*>("databaseNavigator");
        QVERIFY(workspace && profiles && tree);
        choscordb::SavedProfile alpha;
        alpha.id = "stable-alpha";
        alpha.name = "Stable Alpha";
        alpha.path = ":memory:";
        auto beta = alpha;
        beta.id = "stable-beta";
        beta.name = "Stable Beta";
        workspace->adapter()->saveProfile(alpha, 8120);
        workspace->adapter()->saveProfile(beta, 8121);
        QTRY_COMPARE(profiles->count(), 2);
        auto find = [profiles](const QString& id) -> QListWidgetItem* {
            for (int row = 0; row < profiles->count(); ++row) {
                auto* item = profiles->item(row);
                if (item->data(Qt::UserRole).value<choscordb::SavedProfile>().id == id)
                    return item;
            }
            return nullptr;
        };
        for (const auto& id : {alpha.id, beta.id}) {
            auto* item = find(id);
            QVERIFY(item);
            QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                              profiles->visualItemRect(item).center());
        }
        QTRY_COMPARE(tree->model()->rowCount(), 2);
        window.findChild<QAction*>("refreshSavedConnections")->trigger();
        QTRY_VERIFY(find(alpha.id) && find(beta.id));
        QVERIFY(find(alpha.id)->isSelected());
        QVERIFY(find(beta.id)->isSelected());
        beta.name = "Edited Beta";
        workspace->adapter()->saveProfile(beta, 8122);
        QTRY_VERIFY(find(beta.id) && find(beta.id)->text() == "Edited Beta");
        QVERIFY(find(alpha.id)->isSelected());
        QVERIFY(find(beta.id)->isSelected());
        workspace->adapter()->deleteProfile(alpha.id, 8123);
        QTRY_COMPARE(profiles->count(), 1);
        QCOMPARE(profiles->item(0)->data(Qt::UserRole).value<choscordb::SavedProfile>().id,
                 beta.id);
        QVERIFY(profiles->item(0)->isSelected());
        QTRY_COMPARE(tree->model()->rowCount(), 1);
        QVERIFY(tree->model()->index(0, 0).data().toString().contains("Edited Beta"));
    }
    choscordb::MainWindow restarted(nullptr, store);
    restarted.show();
    auto* profiles = restarted.findChild<QListWidget*>("savedConnections");
    auto* tree = restarted.findChild<QTreeView*>("databaseNavigator");
    QTRY_COMPARE(profiles->count(), 1);
    QVERIFY(!profiles->item(0)->isSelected());
    QCOMPARE(tree->model()->rowCount(), 0);
}

void ModernUiTest::failedSidebarOpenShowsReasonAndClearsBrowseSelection() {
    choscordb::MainWindow window;
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* profiles = window.findChild<QListWidget*>("savedConnections");
    QVERIFY(workspace && profiles);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    choscordb::SavedProfile profile;
    profile.id = "broken-sidebar";
    profile.name = "Broken Sidebar";
    profile.path = directory.filePath("missing/database.sqlite");
    workspace->adapter()->saveProfile(profile, 8103);
    QTRY_COMPARE(profiles->count(), 1);
    QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                      profiles->visualItemRect(profiles->item(0)).center());
    QTRY_VERIFY(window.findChild<QMessageBox*>("sidebarConnectionFailure"));
    auto* dialog = window.findChild<QMessageBox*>("sidebarConnectionFailure");
    QVERIFY(dialog->text().contains("Broken Sidebar"));
    QVERIFY(dialog->text().contains("unable to open", Qt::CaseInsensitive));
    QVERIFY(!window.browsingConnection().has_value());
    QVERIFY(!profiles->item(0)->isSelected());
    dialog->accept();
}

void ModernUiTest::failedSidebarOpenRestoresLastSuccessfulProfile() {
    choscordb::MainWindow window;
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* profiles = window.findChild<QListWidget*>("savedConnections");
    auto* tree = window.findChild<QTreeView*>("databaseNavigator");
    QVERIFY(workspace && profiles && tree);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    choscordb::SavedProfile valid;
    valid.id = "valid-browse";
    valid.name = "Valid Browse";
    valid.path = ":memory:";
    workspace->adapter()->saveProfile(valid, 8104);
    auto broken = valid;
    broken.id = "invalid-browse";
    broken.name = "Invalid Browse";
    broken.path = directory.filePath("missing/database.sqlite");
    workspace->adapter()->saveProfile(broken, 8105);
    QTRY_COMPARE(profiles->count(), 2);
    auto click = [profiles](const QString& name) {
        for (int row = 0; row < profiles->count(); ++row)
            if (profiles->item(row)->text().startsWith(name)) {
                QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                                  profiles->visualItemRect(profiles->item(row)).center());
                return;
            }
    };
    click("Valid Browse");
    QTRY_VERIFY(window.browsingConnection().has_value());
    const auto validId = *window.browsingConnection();
    click("Invalid Browse");
    QTRY_VERIFY(window.findChild<QMessageBox*>("sidebarConnectionFailure"));
    QCOMPARE(window.browsingConnection(), std::optional<quint64>(validId));
    auto* validItem = profiles->item(0)->text().startsWith("Valid Browse") ? profiles->item(0)
                                                                           : profiles->item(1);
    auto* invalidItem = validItem == profiles->item(0) ? profiles->item(1) : profiles->item(0);
    QVERIFY(validItem->isSelected());
    QVERIFY(!invalidItem->isSelected());
    QTRY_COMPARE(tree->model()->rowCount(), 1);
    QVERIFY(tree->model()->index(0, 0).data().toString().contains("Valid Browse"));
    window.findChild<QMessageBox*>("sidebarConnectionFailure")->accept();
}

void ModernUiTest::rejectedSidebarOpenKeepsOtherRootsAndShowsOneReason() {
    choscordb::MainWindow window;
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* profiles = window.findChild<QListWidget*>("savedConnections");
    auto* tree = window.findChild<QTreeView*>("databaseNavigator");
    QVERIFY(workspace && profiles && tree);
    QSignalSpy opened(workspace, &choscordb::QueryWorkspace::connectionReady);
    choscordb::SavedProfile alpha;
    alpha.id = "accepted-sidebar";
    alpha.name = "Accepted Sidebar";
    alpha.path = ":memory:";
    auto beta = alpha;
    beta.id = "rejected-sidebar";
    beta.name = "Rejected Sidebar";
    workspace->adapter()->saveProfile(alpha, 8110);
    workspace->adapter()->saveProfile(beta, 8111);
    QTRY_COMPARE(profiles->count(), 2);
    auto click = [profiles](const QString& name) {
        for (int row = 0; row < profiles->count(); ++row)
            if (profiles->item(row)->text() == name) {
                QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                                  profiles->visualItemRect(profiles->item(row)).center());
                return;
            }
    };
    click(alpha.name);
    QTRY_COMPARE(tree->model()->rowCount(), 1);
    QTRY_COMPARE(opened.count(), 1);
    QTRY_VERIFY(window.browsingConnection().has_value());
    const auto first = *window.browsingConnection();
    workspace->setExternalWork(true);
    click(beta.name);
    QTRY_VERIFY(window.findChild<QMessageBox*>("sidebarConnectionFailure"));
    const auto dialogs = window.findChildren<QMessageBox*>("sidebarConnectionFailure");
    QCOMPARE(dialogs.size(), 1);
    QVERIFY(dialogs.first()->text().contains(beta.name));
    QVERIFY(dialogs.first()->text().contains("Finish or cancel active database work"));
    QCOMPARE(window.browsingConnection(), std::optional<quint64>(first));
    QCOMPARE(tree->model()->rowCount(), 1);
    QCOMPARE(opened.count(), 1);
    QVERIFY(profiles->item(0)->isSelected());
    QVERIFY(!profiles->item(1)->isSelected());
    workspace->setExternalWork(false);
    dialogs.first()->accept();
}

void ModernUiTest::removedPendingSidebarOpenCannotRestoreItsRoot() {
    choscordb::MainWindow window;
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* profiles = window.findChild<QListWidget*>("savedConnections");
    auto* tree = window.findChild<QTreeView*>("databaseNavigator");
    auto* filter = window.findChild<QLineEdit*>("navigatorFilter");
    auto* status = window.findChild<QLabel*>("navigatorStatus");
    QVERIFY(workspace && profiles && tree && filter && status);
    QVERIFY(status->isHidden());
    QVERIFY(status->text().isEmpty());
    choscordb::SavedProfile alpha;
    alpha.id = "visible-session";
    alpha.name = "Alpha Visible Session";
    alpha.path = ":memory:";
    auto beta = alpha;
    beta.id = "removed-attempt";
    beta.name = "Beta Removed Attempt";
    workspace->adapter()->saveProfile(alpha, 8130);
    workspace->adapter()->saveProfile(beta, 8131);
    QTRY_COMPARE(profiles->count(), 2);
    QSignalSpy opened(workspace, &choscordb::QueryWorkspace::connectionReady);
    auto click = [profiles](int row) {
        QTest::mouseClick(profiles->viewport(), Qt::LeftButton, Qt::NoModifier,
                          profiles->visualItemRect(profiles->item(row)).center());
    };
    click(0);
    QTRY_COMPARE(opened.count(), 1);
    QVERIFY(status->isHidden());
    QVERIFY(status->text().isEmpty());
    click(1);
    QCOMPARE(tree->model()->rowCount(), 2);
    QCOMPARE(tree->model()->index(1, 0).data(choscordb::NavigatorModel::KindRole).toString(),
             QString("loading"));
    filter->setText("needle");
    QVERIFY(status->text().contains("Search", Qt::CaseInsensitive));
    QVERIFY(!status->isHidden());
    QVERIFY(status->accessibleName().contains(status->text()));
    filter->clear();
    QVERIFY(status->isHidden());
    QVERIFY(status->text().isEmpty());
    QVERIFY(status->accessibleName().isEmpty());
    click(1);
    QCOMPARE(tree->model()->rowCount(), 1);
    QVERIFY(!profiles->item(1)->isSelected());
    QTRY_COMPARE(opened.count(), 2);
    QCOMPARE(tree->model()->rowCount(), 1);
    QVERIFY(!profiles->item(1)->isSelected());
    QVERIFY(status->isHidden());
    QVERIFY(status->text().isEmpty());
    click(1);
    QTRY_COMPARE(tree->model()->rowCount(), 2);
    QCOMPARE(opened.count(), 2);
}

void ModernUiTest::savedConnectionMenuDuplicatesTheChosenProfileWithoutConnecting() {
    choscordb::MainWindow window;
    window.show();
    auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
    auto* profiles = window.findChild<QListWidget*>("savedConnections");
    choscordb::SavedProfile profile;
    profile.id = "alpha";
    profile.name = "Alpha";
    profile.path = ":memory:";
    workspace->adapter()->saveProfile(profile, 991);
    profile.id = "beta";
    profile.name = "Beta";
    workspace->adapter()->saveProfile(profile, 992);
    QTRY_COMPARE(profiles->count(), 2);
    QListWidgetItem* chosen = nullptr;
    for (int i = 0; i < profiles->count(); ++i)
        if (profiles->item(i)->text().startsWith("Beta"))
            chosen = profiles->item(i);
    QVERIFY(chosen);
    QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
    const auto position = profiles->visualItemRect(chosen).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position,
                            profiles->viewport()->mapToGlobal(position));
    QApplication::sendEvent(profiles->viewport(), &event);
    auto* menu = window.findChild<QMenu*>("savedConnectionMenu");
    QVERIFY(menu);
    QTRY_VERIFY(menu->isVisible());
    QVERIFY(!chosen->isSelected());
    const QPoint panel =
        menu->mapToGlobal(QPoint()) + QPoint(choscordb::design::detail::menuShadowMargin(),
                                             choscordb::design::detail::menuShadowMargin());
    QCOMPARE(panel, event.globalPos());
    auto* duplicate = menu->findChild<QAction*>("duplicateSavedConnection");
    QVERIFY(duplicate);
    duplicate->trigger();
    menu->close();
    QTRY_COMPARE(profiles->count(), 3);
    bool found = false;
    for (int i = 0; i < profiles->count(); ++i)
        found |= profiles->item(i)->text().startsWith("Beta copy");
    QVERIFY(found);
    QCOMPARE(connected.count(), 0);
    window.findChild<QDialog*>("profileDialog")->reject();
}
