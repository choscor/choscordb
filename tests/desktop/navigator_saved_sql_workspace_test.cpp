#include "app/main_window.h"
#include "app/workspace_recovery.h"
#include "bridge/completion_service.h"
#include "bridge/rust_text.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/menu/embedded_popup.h"
#include "design_system/status_line/status_line.h"
#include "design_system/toast_region/toast_region.h"
#include "navigator_sql_workspace_test.h"
#include "widgets/editor_completion/editor_completion.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAbstractItemView>
#include <QAction>
#include <QCompleter>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <Qsci/qsciscintilla.h>
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
    QTRY_VERIFY(selected->isHidden());
    QVERIFY(!nestedItem->isHidden());
    QVERIFY(!nestedItem->parent()->isHidden());
    nestedItem->parent()->setExpanded(false);
    search->setText(name.toUpper());
    QTRY_VERIFY(nestedItem->parent()->isExpanded());
    QVERIFY(!nestedItem->isHidden());
    QVERIFY(!nestedItem->parent()->isHidden());
    QVERIFY(!selected->isHidden());
    search->setText("no-matching-file");
    QTRY_VERIFY(nestedItem->parent()->isHidden());
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
    QVERIFY(tabs);
    const int before = tabs->count();
    emit list->itemClicked(matches.front(), 0);
    choscordb::ConfirmationDialog* error = nullptr;
    QTRY_VERIFY((error = window.findChild<choscordb::ConfirmationDialog*>("errorFeedbackDialog")) &&
                error->isVisible());
    QVERIFY(error->text().contains("Could not open"));
    error->reject();
    QCOMPARE(tabs->count(), before);
    QVERIFY(QFile::remove(path));
#endif
}

void NavigatorSqlWorkspaceTest::editorFileErrorsUseTheSharedErrorFeedback() {
    choscordb::MainWindow window;
    window.show();
    window.findChild<QAction*>("newQuery")->trigger();
    auto* editor = window.findChild<choscordb::SqlEditor*>();
    QVERIFY(editor);
    emit editor->fileSaved("/tmp/report.sql", "The disk is full.");
    choscordb::ConfirmationDialog* error = nullptr;
    QTRY_VERIFY((error = window.findChild<choscordb::ConfirmationDialog*>("errorFeedbackDialog")) &&
                error->isVisible());
    QVERIFY(error->text().contains("The disk is full."));
    QVERIFY(error->text().contains("Could not save"));
    error->reject();
    QTRY_VERIFY(!window.findChild<choscordb::ConfirmationDialog*>("errorFeedbackDialog"));
    emit editor->fileOpened("/tmp/report.sql", "The file is missing.");
    QTRY_VERIFY((error = window.findChild<choscordb::ConfirmationDialog*>("errorFeedbackDialog")) &&
                error->isVisible());
    QVERIFY(error->text().contains("The file is missing."));
    QVERIFY(error->text().contains("Could not open"));
    error->reject();
}

void NavigatorSqlWorkspaceTest::savedPanelUsesTheFolderBesideTheApplicationDatabase() {
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    QVERIFY(QDir().mkpath(storage.filePath("sql")));
    QFile file(storage.filePath("sql/beside-database.sql"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("SELECT 7;"), qint64(9));
    file.close();
    choscordb::MainWindow window(nullptr, storage.filePath("workspace.sqlite"));
    window.show();
    if (auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>())
        QTRY_VERIFY(recovery->isReady());
    window.findChild<QPushButton*>("sidebarSaved")->click();
    auto* list = window.findChild<QTreeWidget*>("sidebarSavedFiles");
    QVERIFY(list);
    QTRY_COMPARE(
        list->findItems("beside-database.sql", Qt::MatchExactly | Qt::MatchRecursive).size(), 1);
}

namespace {
bool triggerSavedMenu(QTreeWidget* list, const QPoint& point, const char* name) {
    bool triggered = false;
    QTimer::singleShot(0, list, [&] {
        auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
        if (!menu)
            return;
        auto* action = menu->findChild<QAction*>(name);
        menu->close();
        if (action) {
            action->trigger();
            triggered = true;
        }
    });
    emit list->customContextMenuRequested(point);
    return triggered;
}
QStringList topLevelNames(QTreeWidget* list) {
    QStringList names;
    for (int i = 0; i < list->topLevelItemCount(); ++i)
        names << list->topLevelItem(i)->text(0);
    return names;
}
QDialog* promptDialog(QWidget& window, const char* name) {
    for (auto* dialog : window.findChildren<QDialog*>(name))
        if (dialog->isVisible())
            return dialog;
    return nullptr;
}
}

void NavigatorSqlWorkspaceTest::savedQueryFoldersSortAndKeywordSnippets() {
    QTemporaryDir storage;
    QVERIFY(storage.isValid());
    QVERIFY(QDir().mkpath(storage.filePath("sql")));
    const auto write = [&](const char* name, const char* sql, int ageSeconds) {
        QFile file(storage.filePath(QStringLiteral("sql/") + name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(sql), qint64(qstrlen(sql)));
        QVERIFY(file.flush());
        QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(-ageSeconds),
                                 QFileDevice::FileModificationTime));
    };
    write("a.sql", "SELECT 'a';", 600);
    write("b.sql", "SELECT 'b';", 60);
    choscordb::MainWindow window(nullptr, storage.filePath("workspace.sqlite"));
    window.show();
    if (auto* recovery = window.findChild<choscordb::WorkspaceRecoveryController*>())
        QTRY_VERIFY(recovery->isReady());
    window.findChild<QPushButton*>("sidebarSaved")->click();
    auto* list = window.findChild<QTreeWidget*>("sidebarSavedFiles");
    QVERIFY(list);
    QTRY_COMPARE(topLevelNames(list), QStringList({"a.sql", "b.sql"}));
    const QPoint blank(5, list->viewport()->height() - 5);
    QVERIFY(triggerSavedMenu(list, blank, "savedSort_modified"));
    QTRY_COMPARE(topLevelNames(list), QStringList({"b.sql", "a.sql"}));

    QVERIFY(triggerSavedMenu(list, blank, "savedNewFolder"));
    QDialog* dialog = nullptr;
    QTRY_VERIFY((dialog = promptDialog(window, "savedFolderPrompt")));
    dialog->findChild<QLineEdit*>("savedFolderPromptField")->setText("reports");
    dialog->findChild<QPushButton*>("savedFolderPromptAccept")->click();
    QTRY_COMPARE(topLevelNames(list), QStringList({"reports", "b.sql", "a.sql"}));
    QVERIFY(QDir(storage.filePath("sql/reports")).exists());
    QVERIFY(triggerSavedMenu(list, blank, "savedNewFolder"));
    QTRY_VERIFY((dialog = promptDialog(window, "savedFolderPrompt")));
    dialog->findChild<QLineEdit*>("savedFolderPromptField")->setText("reports");
    dialog->findChild<QPushButton*>("savedFolderPromptAccept")->click();
    const auto fieldError = [dialog] {
        for (auto* label : dialog->findChildren<QLabel*>())
            if (label->property("designRole").toString() == "fieldError" && label->isVisible())
                return label->text();
        return QString();
    };
    QTRY_COMPARE(fieldError(), QString("A folder or file with this name exists."));
    dialog->findChild<QPushButton*>("savedFolderPromptCancel")->click();
    QTRY_VERIFY(!promptDialog(window, "savedFolderPrompt"));

    auto* fileA = list->findItems("a.sql", Qt::MatchExactly).value(0);
    QVERIFY(fileA);
    QVERIFY(triggerSavedMenu(list, list->visualItemRect(fileA).center(), "savedSetKeyword"));
    QTRY_VERIFY((dialog = promptDialog(window, "savedKeywordPrompt")));
    dialog->findChild<QLineEdit*>("savedKeywordPromptField")->setText("pick_a");
    dialog->findChild<QPushButton*>("savedKeywordPromptAccept")->click();
    QTRY_VERIFY(list->findItems("a.sql", Qt::MatchExactly).value(0) &&
                list->findItems("a.sql", Qt::MatchExactly)
                    .value(0)
                    ->toolTip(0)
                    .contains("Keyword: pick_a"));
    QFile keyed(storage.filePath("sql/a.sql"));
    QVERIFY(keyed.open(QIODevice::ReadOnly));
    QCOMPARE(keyed.readAll(), QByteArray("-- keyword: pick_a\nSELECT 'a';"));

    window.findChild<QAction*>("newQuery")->trigger();
    auto* editor = qobject_cast<choscordb::SqlEditor*>(
        window.findChild<QTabWidget*>("editorTabs")->currentWidget());
    QVERIFY(editor);
    editor->setText("pick");
    editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 4L);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    auto* completion = window.findChild<choscordb::EditorCompletionController*>();
    QVERIFY(completion);
    auto* popup = completion->findChild<QCompleter*>()->popup();
    QSignalSpy catalogChanges(completion, &choscordb::EditorCompletionController::partialCatalog);
    completion->requestCompletion();
    const auto completionState = [&] {
        const auto root = storage.filePath("sql").toUtf8();
        const auto listing = choscordb::saved_sql_list_directory(
            choscordb::bridge_detail::utf8View(root), choscordb::bridge_detail::utf8View("name"));
        QList<choscordb::CompletionCandidate> snippets;
        QStringList labels;
        for (const auto& snippet : listing.snippets) {
            snippets.append({choscordb::bridge_detail::fromRust(snippet.label),
                             choscordb::bridge_detail::fromRust(snippet.insert_text),
                             choscordb::bridge_detail::fromRust(snippet.kind)});
            labels << snippets.constLast().label + "/" + snippets.constLast().kind;
        }
        const auto page = choscordb::CompletionService(snippets).complete("pick", 4, true);
        QStringList items;
        for (const auto& item : page.items)
            items << item.label;
        return QString("focus=%1 cursor=%2 anchor=%3 rows=%4 text=%5 error=%6 snippets=[%7] "
                       "valid=%8 items=[%9]")
            .arg(editor->hasFocus())
            .arg(editor->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS))
            .arg(editor->SendScintilla(QsciScintilla::SCI_GETANCHOR))
            .arg(popup->model()->rowCount())
            .arg(editor->text(), choscordb::bridge_detail::fromRust(listing.error),
                 labels.join(','))
            .arg(page.valid)
            .arg(items.join(','))
            .append(QString(" catalogSignals=%1").arg(catalogChanges.count()));
    };
    const bool shown = QTest::qWaitFor([popup] { return popup->isVisible(); });
    QString retry;
    if (!shown) {
        const auto first = completionState();
        QTest::qWait(2000);
        completion->requestCompletion();
        retry =
            first +
            QString(" retryShown=%1").arg(QTest::qWaitFor([popup] { return popup->isVisible(); }));
    }
    QVERIFY2(shown, qPrintable(retry));
    QVERIFY2(popup->model()->index(0, 0).data().toString().startsWith("pick_a"),
             qPrintable(popup->model()->index(0, 0).data().toString()));
    QTest::keyClick(popup, Qt::Key_Return);
    QTRY_COMPARE(editor->text(), QString("SELECT 'a';"));
}
