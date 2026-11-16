#include "app/main_window.h"
#include "app/navigator_controller.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "dialog_layout_guard.h"
#include "models/navigator_model.h"
#include "widgets/structure_editor/structure_editor.h"
#include "widgets/structure_editor_dialog/structure_editor_dialog.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <functional>

using namespace choscordb;

class ObjectStructureModeTest final : public QObject {
    Q_OBJECT
  private slots:
    void editStructureOnTheObjectTab();
    void designNewTableKeepsTheDialog();
    void designNewTableOffersRetryAfterAFailure();
    void editModeKeepsThePaneLoadAlive();
    void loadFailureShowsTheError();
    void reviewShowsTheCurrentSql();
    void gridEditShortcutsReviewAndDiscard();
    void disconnectKeepsTheStagedChanges();
    void disconnectDuringApplyReportsTheUnknownOutcome();

  private:
    struct Fixture {
        QTemporaryDir storage;
        std::unique_ptr<MainWindow> window;
        EngineAdapter* adapter = nullptr;
        NavigatorController* navigator = nullptr;
        quint64 connection = 0;
        bool finished = false;
        QList<qint64> counts;
        QModelIndex schema, table, view;
    };
    static void open(Fixture& fixture);
    static void run(Fixture& fixture, const char* sql);
    static QString answerDialog(const std::function<void()>& press, const QString& choice,
                                QString* review = nullptr);
    static ObjectExplorer* editTable(Fixture& fixture);
    static QWidget* addChange(StructureEditor* editor, const QString& code);
    static QWidget* addEmailColumn(StructureEditor* editor);
};

ObjectExplorer* ObjectStructureModeTest::editTable(Fixture& fixture) {
    QMenu menu;
    fixture.navigator->populateContextMenu(&menu, fixture.table);
    auto* entry = menu.findChild<QAction*>("editStructure");
    if (!entry || !entry->isEnabled())
        return nullptr;
    entry->trigger();
    auto* tabs = fixture.window->findChild<QTabWidget*>("editorTabs");
    return qobject_cast<ObjectExplorer*>(tabs->currentWidget());
}

QWidget* ObjectStructureModeTest::addChange(StructureEditor* editor, const QString& code) {
    auto* add = editor->findChild<QPushButton*>("structureEditorAdd");
    if (!QTest::qWaitFor([add] { return add->isEnabled(); }, 5000))
        return nullptr;
    for (auto* action : add->menu()->actions())
        if (action->objectName() == "structureEditorAdd_" + code) {
            if (!action->isEnabled() || !action->isVisible())
                return nullptr;
            action->trigger();
            const auto frames = editor->findChildren<QWidget*>(
                QRegularExpression("^structureChange_\\d+$"), Qt::FindChildrenRecursively);
            return frames.isEmpty() ? nullptr : frames.last();
        }
    return nullptr;
}

QWidget* ObjectStructureModeTest::addEmailColumn(StructureEditor* editor) {
    auto* column = addChange(editor, "add_column");
    if (!column)
        return nullptr;
    column->findChild<QLineEdit*>("structureField_name")->setText("email");
    column->findChild<QComboBox*>("structureField_type")->setEditText("TEXT");
    return column;
}

void ObjectStructureModeTest::run(Fixture& fixture, const char* sql) {
    fixture.finished = false;
    const auto query = fixture.adapter->execute(fixture.connection, sql);
    QVERIFY(query);
    fixture.adapter->fetchPage(*query);
    QTRY_VERIFY(fixture.finished);
    fixture.adapter->releaseQuery(*query);
}

void ObjectStructureModeTest::open(Fixture& fixture) {
    fixture.window =
        std::make_unique<MainWindow>(nullptr, fixture.storage.filePath("settings.sqlite"));
    fixture.window->show();
    auto* workspace = fixture.window->findChild<QueryWorkspace*>();
    QSignalSpy connected(workspace, &QueryWorkspace::connectionReady);
    workspace->connectSqlite(":memory:");
    QTRY_COMPARE(connected.count(), 1);
    fixture.connection = connected.first().at(0).toULongLong();
    fixture.adapter = workspace->adapter();
    connect(fixture.adapter, &EngineAdapter::eventReady, fixture.window.get(),
            [&fixture](const BridgeEvent& event) {
                if (event.kind == "query_finished")
                    fixture.finished = true;
                if ((event.kind == "stored_page" || event.kind == "page") && !event.cells.empty())
                    fixture.counts.push_back(event.cells[0].integer);
            });
    for (const char* sql : {"CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT)",
                            "INSERT INTO items (name) VALUES ('a')",
                            "CREATE VIEW item_names AS SELECT name FROM items"})
        run(fixture, sql);
    fixture.navigator = fixture.window->findChild<NavigatorController*>();
    auto* model = fixture.navigator->model();
    const auto root = model->index(0, 0);
    model->fetchMore(root);
    QTRY_VERIFY(root.data(NavigatorModel::ChildrenLoadedRole).toBool());
    fixture.schema = model->index(0, 0, root);
    model->fetchMore(fixture.schema);
    QTRY_VERIFY(fixture.schema.data(NavigatorModel::ChildrenLoadedRole).toBool());
    for (int row = 0; row < model->rowCount(fixture.schema); ++row) {
        const auto group = model->index(row, 0, fixture.schema);
        model->fetchMore(group);
        QTRY_VERIFY(group.data(NavigatorModel::ChildrenLoadedRole).toBool());
    }
    fixture.table = model->index(0, 0, model->index(0, 0, fixture.schema));
    QCOMPARE(fixture.table.data().toString(), QString("items"));
    fixture.view = model->index(0, 0, model->index(1, 0, fixture.schema));
}

QString ObjectStructureModeTest::answerDialog(const std::function<void()>& press,
                                              const QString& choice, QString* review) {
    QString seen;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&] {
        auto* dialog = qobject_cast<QDialog*>(design::DialogPresentation::activeDialog());
        if (!dialog || !seen.isEmpty())
            return;
        if (auto* preview = dialog->findChild<QPlainTextEdit*>("structureChangeReview")) {
            seen = dialog->windowTitle();
            if (review)
                *review = preview->toPlainText();
            if (auto* apply = dialog->findChild<QPushButton*>("structureChangeApply");
                apply && apply->text() == choice)
                apply->click();
            else
                dialog->reject();
            return;
        }
        auto* box = qobject_cast<QMessageBox*>(dialog);
        seen = box ? box->text() : dialog->objectName();
        if (box)
            for (auto* button : box->buttons())
                if (button->text() == choice) {
                    button->click();
                    return;
                }
        dialog->reject();
    });
    poll.start(10);
    press();
    if (!QTest::qWaitFor([&] { return !seen.isEmpty(); }, 2000))
        return {};
    return seen;
}

void ObjectStructureModeTest::editStructureOnTheObjectTab() {
    Fixture fixture;
    open(fixture);
    auto& window = *fixture.window;
    QMenu viewMenu;
    fixture.navigator->populateContextMenu(&viewMenu, fixture.view);
    auto* viewStructure = viewMenu.findChild<QAction*>("editStructure");
    QVERIFY(viewStructure && !viewStructure->isEnabled());
    QCOMPARE(viewStructure->toolTip(), QString("Only tables have a structure to edit."));

    const auto editStructure = [&] {
        QMenu menu;
        fixture.navigator->populateContextMenu(&menu, fixture.table);
        auto* entry = menu.findChild<QAction*>("editStructure");
        QVERIFY(entry && entry->isEnabled());
        entry->trigger();
    };
    editStructure();
    QVERIFY(!window.findChild<StructureEditorDialog*>());
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* explorer = qobject_cast<ObjectExplorer*>(tabs->currentWidget());
    QVERIFY(explorer);
    QCOMPARE(explorer->property("objectType").toString(), QString("table"));
    auto* editor = explorer->findChild<StructureEditor*>("objectStructureEditor");
    QVERIFY(editor && editor->isVisible());
    auto* edit = explorer->findChild<QPushButton*>("objectStructureEdit");
    auto* review = explorer->findChild<QPushButton*>("objectStructureReview");
    auto* discard = explorer->findChild<QPushButton*>("objectStructureDiscard");
    QVERIFY(edit && review && discard);
    QVERIFY(edit->isHidden() && review->isVisible() && discard->isVisible());
    QVERIFY(!review->isEnabled());
    auto* add = editor->findChild<QPushButton*>("structureEditorAdd");
    QTRY_VERIFY(add->isEnabled());

    const auto changeAction = [&](const QString& code) -> QAction* {
        for (auto* action : add->menu()->actions())
            if (action->objectName() == "structureEditorAdd_" + code)
                return action;
        return nullptr;
    };
    const auto addChange = [&](const QString& code) -> QWidget* {
        auto* action = changeAction(code);
        if (!action || !action->isEnabled() || !action->isVisible())
            return nullptr;
        action->trigger();
        const auto frames = editor->findChildren<QWidget*>(
            QRegularExpression("^structureChange_\\d+$"), Qt::FindChildrenRecursively);
        return frames.isEmpty() ? nullptr : frames.last();
    };
    auto* panes = explorer->findChild<QTabBar*>("objectTabs");
    QCOMPARE(panes->currentIndex(), 0);
    QVERIFY(changeAction("add_column")->isVisible());
    QVERIFY(!changeAction("add_index")->isVisible());
    QVERIFY(!changeAction("primary_key")->isVisible());
    panes->setCurrentIndex(2);
    QVERIFY(editor->isVisible());
    QVERIFY(!changeAction("add_column")->isVisible());
    auto* primaryKey = changeAction("primary_key");
    QVERIFY(primaryKey->isVisible() && !primaryKey->isEnabled());
    QCOMPARE(primaryKey->toolTip(), QString("SQLite needs a table rebuild for this change."));
    panes->setCurrentIndex(3);
    QVERIFY(!editor->isVisible());
    QVERIFY(!review->isVisible() && !discard->isVisible());
    panes->setCurrentIndex(0);
    QVERIFY(editor->isVisible());

    auto* column = addChange("add_column");
    QVERIFY(column);
    column->findChild<QLineEdit*>("structureField_name")->setText("email");
    column->findChild<QComboBox*>("structureField_type")->setEditText("TEXT");
    panes->setCurrentIndex(1);
    auto* index = addChange("add_index");
    QVERIFY(index);
    index->findChild<QLineEdit*>("structureField_name")->setText("items_email");
    index->findChild<QLineEdit*>("structureField_columns")->setText("email");
    const QString ddl("ALTER TABLE \"main\".\"items\" ADD COLUMN \"email\" TEXT;\n\n"
                      "CREATE INDEX \"main\".\"items_email\" ON \"items\" (\"email\");");
    auto* sql = editor->findChild<QPlainTextEdit*>("structureEditorSql");
    QTRY_COMPARE(sql->toPlainText(), ddl);
    QCOMPARE(column->findChild<QLabel*>("structureChangeTitle")->text(),
             QString("Add column email"));
    QTRY_VERIFY(review->isEnabled());

    QString reviewed;
    QCOMPARE(answerDialog([&] { review->click(); }, "Cancel", &reviewed),
             QString("Review structure changes"));
    QCOMPARE(reviewed, ddl);
    QVERIFY(explorer->findChild<StructureEditor*>("objectStructureEditor"));

    QCOMPARE(answerDialog([&] { window.close(); }, "Cancel"),
             QString("The staged structure changes are not applied."));
    QVERIFY(window.isVisible());
    QCOMPARE(answerDialog([&] { emit tabs->tabCloseRequested(tabs->indexOf(explorer)); }, "Cancel"),
             QString("The staged structure changes are not applied."));
    QCOMPARE(tabs->currentWidget(), explorer);

    QCOMPARE(answerDialog([&] { review->click(); }, "Apply changes"),
             QString("Review structure changes"));
    QTRY_VERIFY(!explorer->findChild<StructureEditor*>("objectStructureEditor"));
    QVERIFY(edit->isVisible() && review->isHidden() && discard->isHidden());
    fixture.counts.clear();
    run(fixture, "SELECT count(*) FROM pragma_table_info('items') WHERE name = 'email'");
    run(fixture, "SELECT count(*) FROM pragma_index_list('items') WHERE name = 'items_email'");
    QTRY_COMPARE(fixture.counts, (QList<qint64>{1, 1}));

    panes->setCurrentIndex(0);
    edit->click();
    editor = explorer->findChild<StructureEditor*>("objectStructureEditor");
    QVERIFY(editor && editor->isVisible());
    add = editor->findChild<QPushButton*>("structureEditorAdd");
    QTRY_VERIFY(add->isEnabled());
    column = addChange("add_column");
    QVERIFY(column);
    column->findChild<QLineEdit*>("structureField_name")->setText("email");
    auto* error = editor->findChild<QLabel*>("structureEditorError");
    QTRY_COMPARE(error->text(), QString("Change 1: The table already has a column email."));
    QVERIFY(!review->isEnabled());
    column->findChild<QLineEdit*>("structureField_name")->setText("code");
    column->findChild<QCheckBox*>("structureField_not_null")->setChecked(true);
    QTRY_VERIFY(review->isEnabled() && error->isHidden());
    QSignalSpy reloaded(fixture.adapter, &EngineAdapter::objectInspectionReady);
    QCOMPARE(answerDialog([&] { review->click(); }, "Apply changes"),
             QString("Review structure changes"));
    QTRY_VERIFY2(error->text().startsWith("Statement 1:") && error->text().contains("NOT NULL"),
                 qPrintable(error->text()));
    QVERIFY(error->isVisible());
    const auto reloadedPanes = [&] {
        QSet<int> panes;
        for (const auto& reply : reloaded)
            panes.insert(int(qvariant_cast<ObjectInspection>(reply.at(3)).pane));
        return panes;
    };
    QTRY_COMPARE(reloadedPanes(),
                 (QSet<int>{int(ObjectInspectionPane::Columns), int(ObjectInspectionPane::Indexes),
                            int(ObjectInspectionPane::Keys)}));
    QCOMPARE(explorer->findChild<StructureEditor*>("objectStructureEditor"), editor);
    QTRY_VERIFY(review->isEnabled());
    QVERIFY(error->text().startsWith("Statement 1:"));
    column->findChild<QLineEdit*>("structureField_name")->setText("email");
    QTRY_COMPARE(error->text(), QString("Change 1: The table already has a column email."));
    column->findChild<QLineEdit*>("structureField_name")->setText("code");
    QTRY_VERIFY(review->isEnabled() && error->isHidden());

    QCOMPARE(answerDialog([&] { discard->click(); }, "Discard changes"),
             QString("The staged structure changes are not applied."));
    QTRY_VERIFY(!explorer->findChild<StructureEditor*>("objectStructureEditor"));
    QVERIFY(edit->isVisible());

    edit->click();
    QVERIFY(explorer->findChild<StructureEditor*>("objectStructureEditor"));
    discard->click();
    QTRY_VERIFY(!explorer->findChild<StructureEditor*>("objectStructureEditor"));

    editStructure();
    QCOMPARE(tabs->currentWidget(), explorer);
    QVERIFY(explorer->findChild<StructureEditor*>("objectStructureEditor"));
    discard->click();
    QTRY_VERIFY(!explorer->findChild<StructureEditor*>("objectStructureEditor"));
}

void ObjectStructureModeTest::designNewTableKeepsTheDialog() {
    Fixture fixture;
    open(fixture);
    auto& window = *fixture.window;
    QMenu menu;
    fixture.navigator->populateContextMenu(&menu, fixture.schema);
    auto* design = menu.findChild<QAction*>("designNewTable");
    QVERIFY(design && design->isEnabled());
    design->trigger();
    auto* dialog = window.findChild<StructureEditorDialog*>("structureEditorDialog");
    QVERIFY(dialog);
    auto* editor = dialog->findChild<StructureEditor*>();
    QVERIFY(editor);
    auto* add = dialog->findChild<QPushButton*>("structureEditorAdd");
    const auto addChange = [&](const QString& code) -> QWidget* {
        for (auto* action : add->menu()->actions())
            if (action->objectName() == "structureEditorAdd_" + code) {
                if (!action->isEnabled() || !action->isVisible())
                    return nullptr;
                action->trigger();
                const auto frames = dialog->findChildren<QWidget*>(
                    QRegularExpression("^structureChange_\\d+$"), Qt::FindChildrenRecursively);
                return frames.isEmpty() ? nullptr : frames.last();
            }
        return nullptr;
    };
    QVERIFY(!addChange("drop_column"));
    auto* column = addChange("add_column");
    QVERIFY(column);
    column->findChild<QLineEdit*>("structureField_name")->setText("id");
    column->findChild<QComboBox*>("structureField_type")->setEditText("INTEGER");
    auto* key = addChange("primary_key");
    QVERIFY(key);
    key->findChild<QLineEdit*>("structureField_columns")->setText("id");
    auto* apply = dialog->findChild<QPushButton*>("structureEditorApply");
    QTRY_COMPARE(dialog->findChild<QLabel*>("structureEditorError")->text(),
                 QString("Enter a name for the new table."));
    QVERIFY(!apply->isEnabled());
    dialog->findChild<QLineEdit*>("structureEditorTableName")->setText("made");
    QTRY_VERIFY(apply->isEnabled());
    apply->click();
    QTRY_VERIFY(!window.findChild<StructureEditorDialog*>("structureEditorDialog"));
    fixture.counts.clear();
    run(fixture, "SELECT count(*) FROM pragma_table_info('made') WHERE name = 'id' AND pk = 1");
    QTRY_COMPARE(fixture.counts, QList<qint64>{1});
}

void ObjectStructureModeTest::designNewTableOffersRetryAfterAFailure() {
    Fixture fixture;
    open(fixture);
    QMenu menu;
    fixture.navigator->populateContextMenu(&menu, fixture.schema);
    menu.findChild<QAction*>("designNewTable")->trigger();
    auto* dialog = fixture.window->findChild<StructureEditorDialog*>("structureEditorDialog");
    QVERIFY(dialog);
    auto* editor = dialog->findChild<StructureEditor*>();
    QVERIFY(addEmailColumn(editor));
    dialog->findChild<QLineEdit*>("structureEditorTableName")->setText("items");
    auto* apply = dialog->findChild<QPushButton*>("structureEditorApply");
    QTRY_VERIFY(apply->isEnabled());
    apply->click();
    QTRY_COMPARE(apply->text(), QString("Retry"));
    auto* error = editor->findChild<QLabel*>("structureEditorError");
    QVERIFY2(error->isVisible() && error->text().contains("already exists"),
             qPrintable(error->text()));
    QVERIFY(!dialog->findChild<QWidget*>("structureEditorStatusLine")->isVisible());
    QVERIFY(dialog->isVisible());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    const auto problems = test::dialogLayoutProblems(*dialog);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
}

void ObjectStructureModeTest::editModeKeepsThePaneLoadAlive() {
    Fixture fixture;
    open(fixture);
    auto* explorer = editTable(fixture);
    QVERIFY(explorer);
    auto* editor = explorer->findChild<StructureEditor*>("objectStructureEditor");
    QVERIFY(editor);
    QTRY_VERIFY(editor->findChild<QPushButton*>("structureEditorAdd")->isEnabled());
    explorer->findChild<QPushButton*>("objectStructureDiscard")->click();
    QTRY_VERIFY(!explorer->findChild<StructureEditor*>("objectStructureEditor"));
    QTRY_VERIFY(explorer->panelState() != PanelState::Loading);
    auto* status = explorer->findChild<QLabel*>("objectStatus");
    const auto paneStatus = status->text();
    QVERIFY(!paneStatus.isEmpty());
    explorer->findChild<QPushButton*>("objectStructureEdit")->click();
    editor = explorer->findChild<StructureEditor*>("objectStructureEditor");
    QVERIFY(editor);
    QTRY_VERIFY(editor->findChild<QPushButton*>("structureEditorAdd")->isEnabled());
    QTRY_COMPARE(status->text(), paneStatus);

    explorer->findChild<QPushButton*>("objectStructureDiscard")->click();
    QTRY_VERIFY(!explorer->findChild<StructureEditor*>("objectStructureEditor"));
    auto* panes = explorer->findChild<QTabBar*>("objectTabs");
    panes->setCurrentIndex(int(ObjectInspectionPane::Keys));
    explorer->findChild<QPushButton*>("objectStructureEdit")->click();
    QTRY_VERIFY(explorer->panelState() != PanelState::Loading);
    editor = explorer->findChild<StructureEditor*>("objectStructureEditor");
    QTRY_VERIFY(editor->findChild<QPushButton*>("structureEditorAdd")->isEnabled());
    QCOMPARE(explorer->property("objectLabel").toString(), fixture.table.data().toString());
}

void ObjectStructureModeTest::loadFailureShowsTheError() {
    Fixture fixture;
    open(fixture);
    auto* explorer = editTable(fixture);
    QVERIFY(explorer);
    explorer->findChild<QPushButton*>("objectStructureDiscard")->click();
    QTRY_VERIFY(!explorer->findChild<StructureEditor*>("objectStructureEditor"));
    run(fixture, "DROP VIEW item_names");
    run(fixture, "DROP TABLE items");
    explorer->findChild<QPushButton*>("objectStructureEdit")->click();
    auto* editor = explorer->findChild<StructureEditor*>("objectStructureEditor");
    QVERIFY(editor);
    auto* error = editor->findChild<QLabel*>("structureEditorError");
    QTRY_VERIFY(error->isVisible() && !error->text().isEmpty());
    auto* status = explorer->findChild<QLabel*>("objectStatus");
    QCOMPARE(status->text(), error->text());
    QVERIFY(!editor->findChild<QPushButton*>("structureEditorAdd")->isEnabled());
    QVERIFY(!explorer->findChild<QPushButton*>("objectStructureReview")->isEnabled());
}

void ObjectStructureModeTest::reviewShowsTheCurrentSql() {
    Fixture fixture;
    open(fixture);
    auto* explorer = editTable(fixture);
    QVERIFY(explorer);
    auto* editor = explorer->findChild<StructureEditor*>("objectStructureEditor");
    auto* column = addEmailColumn(editor);
    QVERIFY(column);
    auto* review = explorer->findChild<QPushButton*>("objectStructureReview");
    QTRY_VERIFY(review->isEnabled());
    column->findChild<QLineEdit*>("structureField_name")->setText("phone");
    QString reviewed;
    QCOMPARE(answerDialog([&] { review->click(); }, "Apply changes", &reviewed),
             QString("Review structure changes"));
    QCOMPARE(reviewed, QString("ALTER TABLE \"main\".\"items\" ADD COLUMN \"phone\" TEXT;"));
    QTRY_VERIFY(!explorer->findChild<StructureEditor*>("objectStructureEditor"));
    fixture.counts.clear();
    run(fixture, "SELECT count(*) FROM pragma_table_info('items') WHERE name = 'phone'");
    QTRY_COMPARE(fixture.counts, QList<qint64>{1});
}

void ObjectStructureModeTest::gridEditShortcutsReviewAndDiscard() {
    Fixture fixture;
    open(fixture);
    auto* explorer = editTable(fixture);
    QVERIFY(explorer);
    auto* editor = explorer->findChild<StructureEditor*>("objectStructureEditor");
    auto* column = addEmailColumn(editor);
    QVERIFY(column);
    QTRY_VERIFY(explorer->findChild<QPushButton*>("objectStructureReview")->isEnabled());
    auto* field = column->findChild<QLineEdit*>("structureField_name");
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    QCOMPARE(
        answerDialog(
            [&] { QTest::keyClick(field, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier); },
            "Cancel"),
        QString("Review structure changes"));
    QCOMPARE(answerDialog(
                 [&] {
                     QTest::keyClick(field, Qt::Key_Backspace,
                                     Qt::ControlModifier | Qt::ShiftModifier);
                 },
                 "Discard changes"),
             QString("The staged structure changes are not applied."));
    QTRY_VERIFY(!explorer->findChild<StructureEditor*>("objectStructureEditor"));
}

void ObjectStructureModeTest::disconnectKeepsTheStagedChanges() {
    Fixture fixture;
    open(fixture);
    auto* explorer = editTable(fixture);
    QVERIFY(explorer);
    auto* editor = explorer->findChild<StructureEditor*>("objectStructureEditor");
    QVERIFY(addEmailColumn(editor));
    auto* review = explorer->findChild<QPushButton*>("objectStructureReview");
    QTRY_VERIFY(review->isEnabled());
    QVERIFY(fixture.adapter->disconnectConnection(fixture.connection));
    QTRY_VERIFY(explorer->needsConnection());
    QCOMPARE(explorer->findChild<StructureEditor*>("objectStructureEditor"), editor);
    auto* error = editor->findChild<QLabel*>("structureEditorError");
    QTRY_VERIFY2(error->isVisible() && error->text().startsWith("The connection closed."),
                 qPrintable(error->text()));
    QVERIFY(!review->isEnabled());
    auto* discard = explorer->findChild<QPushButton*>("objectStructureDiscard");
    QCOMPARE(answerDialog([&] { discard->click(); }, "Discard changes"),
             QString("The staged structure changes are not applied."));
    QTRY_VERIFY(!explorer->findChild<StructureEditor*>("objectStructureEditor"));
}

void ObjectStructureModeTest::disconnectDuringApplyReportsTheUnknownOutcome() {
    Fixture fixture;
    open(fixture);
    auto* explorer = editTable(fixture);
    QVERIFY(explorer);
    auto* editor = explorer->findChild<StructureEditor*>("objectStructureEditor");
    QVERIFY(addEmailColumn(editor));
    auto* review = explorer->findChild<QPushButton*>("objectStructureReview");
    QTRY_VERIFY(review->isEnabled());
    QCOMPARE(answerDialog([&] { review->click(); }, "Apply changes"),
             QString("Review structure changes"));
    QVERIFY(editor->isApplying());
    auto* tabs = fixture.window->findChild<QTabWidget*>("editorTabs");
    emit tabs->tabCloseRequested(tabs->indexOf(explorer));
    QVERIFY(tabs->indexOf(explorer) >= 0);
    auto* status = explorer->findChild<QLabel*>("objectStatus");
    QCOMPARE(status->text(), QString("Wait for the structure changes to finish."));
    BridgeEvent disconnected{};
    disconnected.kind = "disconnected";
    disconnected.id = fixture.connection;
    emit fixture.adapter->eventReady(disconnected);
    QCOMPARE(explorer->findChild<StructureEditor*>("objectStructureEditor"), editor);
    auto* error = editor->findChild<QLabel*>("structureEditorError");
    QVERIFY2(error->isVisible() && error->text().contains("Their outcome is unknown."),
             qPrintable(error->text()));
    QVERIFY(!editor->isApplying());
    QVERIFY(!review->isEnabled());
}

QTEST_MAIN(ObjectStructureModeTest)
#include "object_structure_mode_test.moc"
