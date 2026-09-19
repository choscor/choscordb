#include "app/main_window.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "models/navigator_model.h"
#include "widgets/editor_completion/editor_completion.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QCompleter>
#include <QTabWidget>
#include <QtTest>
class EditorCompletionTest : public QObject {
    Q_OBJECT
  private slots:
    void modifiedReturnRunsItsActionWithoutInsertingSuggestion_data() {
        QTest::addColumn<bool>("enabled");
        QTest::newRow("enabled run") << true;
        QTest::newRow("disabled run") << false;
    }
    void modifiedReturnRunsItsActionWithoutInsertingSuggestion() {
        QFETCH(bool, enabled);
        choscordb::SqlEditor editor;
        QAction run(&editor);
        run.setShortcut(QKeySequence("Ctrl+Return"));
        run.setEnabled(enabled);
        editor.addAction(&run);
        QSignalSpy triggered(&run, &QAction::triggered);
        editor.show();
        editor.setFocus();
        QTRY_VERIFY(editor.hasFocus());
        choscordb::EditorCompletionController controller;
        controller.setEditor(&editor);
        editor.setText("sel");
        editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, 3);
        controller.requestCompletion();
        auto* popup = controller.findChild<QCompleter*>()->popup();
        QTRY_VERIFY(popup->isVisible());
        QTest::keyClick(popup, Qt::Key_Return, Qt::ControlModifier);
        if (enabled) {
            QTRY_COMPARE(triggered.count(), 1);
            QCOMPARE(editor.text(), QString("sel"));
        } else {
            QTRY_VERIFY(!popup->isVisible());
            QCoreApplication::processEvents();
            QVERIFY(!editor.text().contains("SELECT"));
        }
    }
    void typingTriggersCompletionWithoutAnExplicitRequest() {
        choscordb::SqlEditor editor;
        editor.show();
        editor.setFocus();
        QTRY_VERIFY(editor.hasFocus());
        choscordb::EditorCompletionController controller;
        controller.setEditor(&editor);
        controller.setCatalog(
            choscordb::CompletionService({{"users", "\"main\".\"users\"", "table"}}));
        QTest::keyClicks(&editor, "us");
        auto* popup = controller.findChild<QCompleter*>()->popup();
        QTRY_VERIFY(popup->isVisible());
        QCOMPARE(popup->model()->index(0, 0).data(Qt::UserRole).toString(),
                 QString("\"main\".\"users\""));
        QTest::keyClick(popup, Qt::Key_Escape);
        QCOMPARE(editor.text(), QString("us"));
    }
    void duplicateNamesShowTheirDistinctQualifiedPaths() {
        choscordb::SqlEditor editor;
        editor.show();
        editor.setFocus();
        QTRY_VERIFY(editor.hasFocus());
        choscordb::EditorCompletionController controller;
        controller.setEditor(&editor);
        controller.setCatalog(
            choscordb::CompletionService({{"users", "\"public\".\"users\"", "table"},
                                          {"users", "\"audit\".\"users\"", "table"}}));
        editor.setText("us");
        editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, 2);
        controller.requestCompletion();
        auto* completer = controller.findChild<QCompleter*>();
        QTRY_VERIFY(completer->popup()->isVisible());
        const auto first = completer->completionModel()->index(0, 0).data().toString();
        const auto second = completer->completionModel()->index(1, 0).data().toString();
        QVERIFY(first != second);
        QVERIFY(first.contains("audit") || second.contains("audit"));
        QVERIFY(first.contains("public") || second.contains("public"));
    }
    void mainWindowUsesActiveNavigatorMetadata() {
        choscordb::MainWindow window;
        window.show();
        auto* action = window.findChild<QAction*>("completeSql");
        QVERIFY(action);
        auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
        QSignalSpy connected(workspace, &choscordb::QueryWorkspace::connectionReady);
        workspace->connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 1);
        const auto id = connected.first().at(0).toULongLong();
        auto* adapter = workspace->adapter();
        int completed = 0, metadataRequests = 0;
        connect(
            adapter, &choscordb::EngineAdapter::eventReady, &window,
            [&](const choscordb::BridgeEvent& event) {
                const auto kind =
                    QString::fromUtf8(event.kind.data(), qsizetype(event.kind.size()));
                if (kind == "query_finished")
                    ++completed;
                if (kind == "metadata")
                    ++metadataRequests;
            },
            Qt::DirectConnection);
        auto query = adapter->execute(id, "CREATE TABLE users (name TEXT)");
        QVERIFY(query);
        adapter->fetchPage(*query);
        QTRY_COMPARE(completed, 1);
        adapter->releaseQuery(*query);
        auto* model = window.findChild<choscordb::NavigatorModel*>();
        QVERIFY(model);
        auto root = model->index(0, 0);
        model->fetchMore(root);
        QTRY_COMPARE(model->index(0, 0, root).data(choscordb::NavigatorModel::KindRole).toString(),
                     QString("database"));
        auto schema = model->index(0, 0, root);
        model->fetchMore(schema);
        QTRY_COMPARE(
            model->index(0, 0, schema).data(choscordb::NavigatorModel::KindRole).toString(),
            QString("group"));
        auto tables = model->index(0, 0, schema);
        model->fetchMore(tables);
        QTRY_COMPARE(
            model->index(0, 0, tables).data(choscordb::NavigatorModel::KindRole).toString(),
            QString("table"));
        auto table = model->index(0, 0, tables);
        model->fetchMore(table);
        QTRY_COMPARE(model->index(0, 0, table).data(choscordb::NavigatorModel::KindRole).toString(),
                     QString("column"));
        auto* tabs = window.findChild<QTabWidget*>("editorTabs");
        auto* editor = qobject_cast<choscordb::SqlEditor*>(tabs->currentWidget());
        editor->setText("ma");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 2);
        editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());
        action->trigger();
        auto* controller = window.findChild<choscordb::EditorCompletionController*>();
        QVERIFY(controller);
        auto* completer = controller->findChild<QCompleter*>();
        QTRY_VERIFY(completer->popup()->isVisible());
        QCOMPARE(completer->completionModel()->rowCount(), 1);
        QVERIFY(completer->completionModel()->index(0, 0).data().toString().startsWith("main"));
        auto* selectorForNone = window.findChild<QComboBox*>("connectionSelector");
        const int selectedIndex = selectorForNone->currentIndex();
        int noneIndex = -1;
        for (int i = 0; i < selectorForNone->count(); ++i)
            if (!selectorForNone->itemData(i).isValid())
                noneIndex = i;
        QVERIFY(noneIndex >= 0);
        selectorForNone->setCurrentIndex(noneIndex);
        window.activateWindow();
        editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());
        action->trigger();
        QTRY_COMPARE(completer->completionModel()->rowCount(), 0);
        QVERIFY(!completer->popup()->isVisible());
        selectorForNone->setCurrentIndex(selectedIndex);
        const int beforeTyping = metadataRequests;
        editor->setText("SELECT users.na");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 15);
        window.activateWindow();
        editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());
        action->trigger();
        QTRY_VERIFY(completer->popup()->isVisible());
        QCOMPARE(completer->completionModel()->rowCount(), 1);
        QCOMPARE(completer->completionModel()->index(0, 0).data(Qt::UserRole).toString(),
                 QString("\"main\".\"users\".\"name\""));
        QCOMPARE(metadataRequests, beforeTyping);
        workspace->connectSqlite(":memory:");
        QTRY_COMPARE(connected.count(), 2);
        QCOMPARE(selectorForNone->currentData().toULongLong(), id);
        selectorForNone->setCurrentIndex(selectorForNone->findData(connected.last().at(0)));
        editor->setText("us");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 2);
        window.activateWindow();
        editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());
        action->trigger();
        QTRY_COMPARE(completer->completionModel()->rowCount(), 0);
        QVERIFY(!completer->popup()->isVisible());
        auto* selector = window.findChild<QComboBox*>("connectionSelector");
        selector->setCurrentIndex(selector->findData(QVariant::fromValue<qulonglong>(id)));
        window.activateWindow();
        editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());
        action->trigger();
        QTRY_VERIFY(completer->popup()->isVisible());
        QVERIFY(completer->completionModel()->index(0, 0).data().toString().startsWith("users"));
        QVERIFY(window.grab().save("native-completion.png"));
        QVERIFY(completer->popup()->grab().save("native-completion-popup.png"));
        model->refresh(root);
        QVERIFY(!completer->popup()->isVisible());
        adapter->disconnectConnection(id);
        QTRY_COMPARE(model->rowCount(), 1);
    }
    void quotedCompletionReplacesQualifiedPrefixAndUndoesOnce() {
        choscordb::SqlEditor editor;
        editor.resize(700, 400);
        editor.show();
        editor.setFocus();
        QTRY_VERIFY(editor.hasFocus());
        choscordb::EditorCompletionController controller;
        controller.setEditor(&editor);
        controller.setCatalog(
            choscordb::CompletionService({{"é name", "\"main\".\"é name\"", "table"}}));
        editor.setText("SELECT * FROM main.é");
        editor.SendScintilla(QsciScintilla::SCI_GOTOPOS,
                             editor.SendScintilla(QsciScintilla::SCI_GETLENGTH));
        controller.requestCompletion();
        auto* completer = controller.findChild<QCompleter*>();
        QTRY_VERIFY_WITH_TIMEOUT(completer->popup()->isVisible(), 2000);
        QCOMPARE(completer->completionModel()->rowCount(), 1);
        completer->popup()->setCurrentIndex(completer->completionModel()->index(0, 0));
        QTest::keyClick(completer->popup(), Qt::Key_Return);
        QTRY_COMPARE(editor.text(), QString("SELECT * FROM \"main\".\"é name\""));
        editor.undo();
        QCOMPARE(editor.text(), QString("SELECT * FROM main.é"));
    }
};
QTEST_MAIN(EditorCompletionTest)
#include "editor_completion_test.moc"
