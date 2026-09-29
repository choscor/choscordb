#include "app/main_window.h"
#include "app/workspace_recovery.h"
#include "design_system/toast_region/toast_region.h"
#include "navigator_sql_workspace_test.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

void NavigatorSqlWorkspaceTest::savedPanelFiltersFolderTreeAndReusesEditedTab() {
    QStandardPaths::setTestModeEnabled(true);
    const auto directory =
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath("com.choscor.ChoscorDB/sql");
    QVERIFY(QDir().mkpath(directory + "/nested"));
    const auto name = QStringLiteral("sidebar-test-%1.sql").arg(QCoreApplication::applicationPid());
    const auto path = QDir(directory).filePath(name);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("SELECT 13;"), qint64(10));
    file.close();
    QFile nested(QDir(directory + "/nested").filePath(name));
    QVERIFY(nested.open(QIODevice::WriteOnly));
    nested.write("SELECT 99;");
    nested.close();
    choscordb::MainWindow window;
    window.show();
    if (auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>())
        QTRY_VERIFY(recovery->isReady());
    window.findChild<QPushButton*>("sidebarSaved")->click();
    auto* list = window.findChild<QTreeWidget*>("sidebarSavedFiles");
    QVERIFY(list);
    auto* search = window.findChild<QLineEdit*>("sidebarSavedSearch");
    QVERIFY(search);
    QList<QTreeWidgetItem*> matches;
    QTRY_COMPARE((matches = list->findItems(name, Qt::MatchExactly | Qt::MatchRecursive)).size(),
                 2);
    auto* selected = matches.at(0)->parent() ? matches.at(1) : matches.at(0);
    QVERIFY(!selected->parent());
    auto* nestedItem = matches.at(0)->parent() ? matches.at(0) : matches.at(1);
    QCOMPARE(nestedItem->parent()->text(0), QString("nested"));
    search->setText("NESTED");
    QVERIFY(selected->isHidden());
    QVERIFY(!nestedItem->isHidden());
    QVERIFY(!nestedItem->parent()->isHidden());
    nestedItem->parent()->setExpanded(false);
    search->setText(name.toUpper());
    QVERIFY(nestedItem->parent()->isExpanded());
    QVERIFY(!nestedItem->isHidden());
    QVERIFY(!nestedItem->parent()->isHidden());
    QVERIFY(!selected->isHidden());
    search->setText("no-matching-file");
    QVERIFY(nestedItem->parent()->isHidden());
    search->clear();
    QVERIFY(!selected->isHidden());
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    const int beforeFolder = tabs->count();
    emit list->itemClicked(nestedItem->parent(), 0);
    QCOMPARE(tabs->count(), beforeFolder);
    QMetaObject::invokeMethod(list, "itemClicked", Q_ARG(QTreeWidgetItem*, selected),
                              Q_ARG(int, 0));
    QMetaObject::invokeMethod(list, "itemActivated", Q_ARG(QTreeWidgetItem*, selected),
                              Q_ARG(int, 0));
    QTRY_COMPARE(tabs->count(), 1);
    auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(editor);
    QTRY_COMPARE(editor->text(), QString("SELECT 13;"));
    editor->setText("unsaved edit");
    QMetaObject::invokeMethod(list, "itemClicked", Q_ARG(QTreeWidgetItem*, selected),
                              Q_ARG(int, 0));
    QCOMPARE(tabs->count(), 1);
    QCOMPARE(editor->text(), QString("unsaved edit"));
    emit list->itemClicked(nestedItem, 0);
    QTRY_COMPARE(tabs->count(), 2);
    auto* nestedEditor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(nestedEditor && nestedEditor != editor);
    QTRY_COMPARE(nestedEditor->text(), QString("SELECT 99;"));
    QCOMPARE(editor->text(), QString("unsaved edit"));
    file.remove();
    nested.remove();
}

void NavigatorSqlWorkspaceTest::savedPanelRapidDistinctFilesOpenBothAndFocusLatest() {
    QStandardPaths::setTestModeEnabled(true);
    const auto directory =
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath("com.choscor.ChoscorDB/sql");
    QVERIFY(QDir().mkpath(directory));
    const auto prefix = QStringLiteral("sidebar-rapid-%1-").arg(QCoreApplication::applicationPid());
    const auto firstName = prefix + "a.sql";
    const auto secondName = prefix + "b.sql";
    const auto firstPath = QDir(directory).filePath(firstName);
    const auto secondPath = QDir(directory).filePath(secondName);
    QFile first(firstPath);
    QVERIFY(first.open(QIODevice::WriteOnly));
    QCOMPARE(first.write("SELECT 'first';"), qint64(15));
    first.close();
    QFile second(secondPath);
    QVERIFY(second.open(QIODevice::WriteOnly));
    QCOMPARE(second.write("SELECT 'second';"), qint64(16));
    second.close();

    choscordb::MainWindow window;
    window.show();
    if (auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>())
        QTRY_VERIFY(recovery->isReady());
    window.findChild<QPushButton*>("sidebarSaved")->click();
    auto* list = window.findChild<QTreeWidget*>("sidebarSavedFiles");
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QVERIFY(list && tabs);
    QList<QTreeWidgetItem*> firstItems;
    QList<QTreeWidgetItem*> secondItems;
    QTRY_COMPARE(
        (firstItems = list->findItems(firstName, Qt::MatchExactly | Qt::MatchRecursive)).size(), 1);
    QTRY_COMPARE(
        (secondItems = list->findItems(secondName, Qt::MatchExactly | Qt::MatchRecursive)).size(),
        1);
    emit list->itemClicked(firstItems.front(), 0);
    emit list->itemClicked(secondItems.front(), 0);
    emit list->itemClicked(secondItems.front(), 0);
    QTRY_COMPARE(tabs->count(), 2);
    const QSet<QString> expectedPaths{QDir::fromNativeSeparators(firstPath),
                                      QDir::fromNativeSeparators(secondPath)};
    QTRY_VERIFY([&] {
        QSet<QString> paths;
        for (int i = 0; i < tabs->count(); ++i) {
            auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->widget(i));
            if (!editor || editor->text().isEmpty())
                return false;
            paths.insert(QDir::fromNativeSeparators(editor->filePath()));
        }
        return paths == expectedPaths;
    }());
    auto* current = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
    QVERIFY(current);
    QCOMPARE(QDir::fromNativeSeparators(current->filePath()),
             QDir::fromNativeSeparators(secondPath));
    QVERIFY(first.remove());
    QVERIFY(second.remove());
}

void NavigatorSqlWorkspaceTest::savedPanelRejectsFileReplacedBySymlink() {
#ifndef Q_OS_UNIX
    QSKIP("This case requires filesystem symlinks.");
#else
    QStandardPaths::setTestModeEnabled(true);
    const auto directory =
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath("com.choscor.ChoscorDB/sql");
    QVERIFY(QDir().mkpath(directory));
    const auto name =
        QStringLiteral("sidebar-replaced-%1.sql").arg(QCoreApplication::applicationPid());
    const auto path = QDir(directory).filePath(name);
    QFile original(path);
    QVERIFY(original.open(QIODevice::WriteOnly));
    original.write("SELECT 1;");
    original.close();
    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    const auto target = outside.filePath("outside.sql");
    QFile secret(target);
    QVERIFY(secret.open(QIODevice::WriteOnly));
    secret.write("SELECT 'outside';");
    secret.close();

    choscordb::MainWindow window;
    window.show();
    if (auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>())
        QTRY_VERIFY(recovery->isReady());
    window.findChild<QPushButton*>("sidebarSaved")->click();
    auto* list = window.findChild<QTreeWidget*>("sidebarSavedFiles");
    QVERIFY(list);
    QList<QTreeWidgetItem*> matches;
    QTRY_COMPARE((matches = list->findItems(name, Qt::MatchExactly | Qt::MatchRecursive)).size(),
                 1);
    QVERIFY(original.remove());
    QVERIFY(QFile::link(target, path));
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* toast = window.findChild<choscordb::ToastRegion*>("toastRegion");
    QVERIFY(tabs && toast);
    const int before = tabs->count();
    emit list->itemClicked(matches.front(), 0);
    QTRY_VERIFY(toast->text().contains("Could not open"));
    QCOMPARE(tabs->count(), before);
    QVERIFY(QFile::remove(path));
#endif
}
