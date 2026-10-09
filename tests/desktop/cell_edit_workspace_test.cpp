#include "app/main_window.h"
#include "app/object_data_workspace.h"
#include "app/query_workspace.h"
#include "design_system/menu/embedded_popup.h"
#include "models/result_table_model.h"
#include <QAction>
#include <QClipboard>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QTextCursor>
#include <QTimer>
#include <QtTest>

using namespace choscordb;
class CellEditWorkspaceTest : public QObject {
    Q_OBJECT
    static void openCell(QTableView* grid, const QModelIndex& index) {
        QTimer::singleShot(0, grid, [] {
            auto* menu = qobject_cast<QMenu*>(design::detail::activeEmbeddedPopup());
            QVERIFY(menu);
            const auto actions = menu->actions();
            menu->close();
            QVERIFY(actions.size() > 5);
            QCOMPARE(actions[0]->text(), QString("Edit cell…"));
            QVERIFY(actions[1]->isSeparator());
            QVERIFY(actions[2]->menu());
            QCOMPARE(actions[2]->menu()->objectName(), QString("resultQuickFilter"));
            QCOMPARE(actions[3]->objectName(), QString("viewCellJson"));
            QCOMPARE(actions[4]->objectName(), QString("viewRowJson"));
            QCOMPARE(actions[5]->objectName(), QString("viewTableJson"));
            auto* edit = actions[0];
            QVERIFY(edit->isEnabled());
            edit->trigger();
        });
        grid->customContextMenuRequested(grid->visualRect(index).center());
    }
  private slots:
    void dragDropCannotNormalizeTheLiteralDraft() {
        MainWindow window;
        ObjectDataWorkspace data(window.findChild<QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        ResultColumn column{};
        column.name = "note";
        column.databaseType = "text";
        QVERIFY(model->setPage({column}, {{QString("A\r\nB")}}, 0));
        model->setEditableColumns({true}, true, true);
        openCell(grid, model->index(0, 0));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        text->moveCursor(QTextCursor::End);
        const auto position = text->cursorRect().center();
        QMimeData mime;
        mime.setText("X\r\nY");
        QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(text->viewport(), &enter);
        QDropEvent drop(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(text->viewport(), &drop);
        QVERIFY(!drop.isAccepted());
        QTest::keyClicks(text, "!");
        sheet->findChild<QPushButton*>("cellEditSave")->click();
        QTRY_VERIFY(!sheet->isVisible());
        QCOMPARE(std::get<QString>(*model->cellValue(model->index(0, 0))), QString("A\r\nB!"));
    }
    void cutAndPasteRetainsTheLiteralSelectedText() {
        MainWindow window;
        ObjectDataWorkspace data(window.findChild<QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        ResultColumn column{};
        column.name = "note";
        column.databaseType = "text";
        const QString original = QString::fromUtf8("a\xc2\xa0"
                                                   "b\r\nc");
        QVERIFY(model->setPage({column}, {{original}}, 0));
        model->setEditableColumns({true}, true, true);
        openCell(grid, model->index(0, 0));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        auto cursor = text->textCursor();
        cursor.setPosition(0);
        cursor.setPosition(4, QTextCursor::KeepAnchor);
        text->setTextCursor(cursor);
        text->cut();
        text->moveCursor(QTextCursor::End);
        text->paste();
        sheet->findChild<QPushButton*>("cellEditSave")->click();
        QTRY_VERIFY(!sheet->isVisible());
        QCOMPARE(std::get<QString>(*model->cellValue(model->index(0, 0))),
                 QString::fromUtf8("ca\xc2\xa0"
                                   "b\r\n"));
    }
    void anOpenMenuDisablesTheActionAfterItsClickedPageIsReplaced() {
        MainWindow window;
        ObjectDataWorkspace data(window.findChild<QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        ResultColumn column{};
        column.name = "note";
        column.databaseType = "text";
        QVERIFY(model->setPage({column}, {{QString("old")}}, 0));
        model->setEditableColumns({true}, true, true);
        QTimer::singleShot(0, grid, [&] {
            auto* menu = qobject_cast<QMenu*>(design::detail::activeEmbeddedPopup());
            QVERIFY(menu);
            auto* action = menu->findChild<QAction*>("editCell");
            const bool wasEnabled = action && action->isEnabled();
            const bool reset = model->setPage({column}, {{QString("new")}}, 0);
            model->setEditableColumns({true}, true, true);
            const bool nowDisabled = action && !action->isEnabled();
            menu->close();
            QVERIFY(wasEnabled && reset);
            QVERIFY(nowDisabled);
            action->setEnabled(true);
            action->trigger();
        });
        grid->customContextMenuRequested(grid->visualRect(model->index(0, 0)).center());
        QVERIFY(!data.findChild<QDialog*>("cellEditSheet"));
        QCOMPARE(model->index(0, 0).data().toString(), QString("new"));
        QVERIFY(!model->hasPendingEdits());
    }
    void replacementUndoRedoPreservesLiteralDraft_data() {
        QTest::addColumn<QString>("action");
        QTest::newRow("undo") << QString("undo");
        QTest::newRow("redo") << QString("redo");
        QTest::newRow("branch after undo") << QString("branch");
    }
    void replacementUndoRedoPreservesLiteralDraft() {
        QFETCH(QString, action);
        MainWindow window;
        ObjectDataWorkspace data(window.findChild<QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        ResultColumn column{};
        column.name = "note";
        column.databaseType = "text";
        const QString original = "A\r\nB\rC\nD";
        QVERIFY(model->setPage({column}, {{original}}, 0));
        model->setEditableColumns({true}, true, true);
        openCell(grid, model->index(0, 0));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        const QString pasted = QString::fromUtf8("\r\nP\xc2\xa0"
                                                 "Q\rR\xe2\x80\xa8"
                                                 "S\xe2\x80\xa9");
        QApplication::clipboard()->setText(pasted);
        auto cursor = text->textCursor();
        cursor.setPosition(1);
        cursor.setPosition(6, QTextCursor::KeepAnchor);
        text->setTextCursor(cursor);
        text->paste();
        text->undo();
        QCOMPARE(text->toPlainText(), QString("A\nB\nC\nD"));
        QString expected = original;
        if (action == "redo") {
            text->redo();
            expected = "A" + pasted + "D";
        } else if (action == "branch") {
            text->moveCursor(QTextCursor::End);
            QTest::keyClicks(text, "!");
            text->redo();
            expected += "!";
        }
        sheet->findChild<QPushButton*>("cellEditSave")->click();
        QTRY_VERIFY(!sheet->isVisible());
        QCOMPARE(std::get<QString>(*model->cellValue(model->index(0, 0))), expected);
    }
    void undoKeepsSeparatelyEditedCrAndLfPositions() {
        MainWindow window;
        ObjectDataWorkspace data(window.findChild<QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        ResultColumn column{};
        column.name = "note";
        column.databaseType = "text";
        QVERIFY(model->setPage({column}, {{QString("a\rb")}}, 0));
        model->setEditableColumns({true}, true, true);
        openCell(grid, model->index(0, 0));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        auto cursor = text->textCursor();
        cursor.setPosition(2);
        text->setTextCursor(cursor);
        QTest::keyClick(text, Qt::Key_Return);
        cursor.setPosition(1);
        cursor.setPosition(3, QTextCursor::KeepAnchor);
        text->setTextCursor(cursor);
        QTest::keyClicks(text, "X");
        text->undo();
        QCOMPARE(text->toPlainText(), QString("a\n\nb"));
        text->moveCursor(QTextCursor::End);
        QTest::keyClicks(text, "Y");
        sheet->findChild<QPushButton*>("cellEditSave")->click();
        QTRY_VERIFY(!sheet->isVisible());
        QCOMPARE(std::get<QString>(*model->cellValue(model->index(0, 0))), QString("a\r\nbY"));
    }
    void pastedTextRetainsItsLiteralSeparatorsBesideExistingContent() {
        MainWindow window;
        ObjectDataWorkspace data(window.findChild<QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        ResultColumn column{};
        column.name = "note";
        column.databaseType = "text";
        QVERIFY(model->setPage({column}, {{QString("old\r\nkeep")}}, 0));
        model->setEditableColumns({true}, true, true);
        openCell(grid, model->index(0, 0));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        const QString pasted = QString::fromUtf8("_a\xc2\xa0"
                                                 "b\r\nc\rd\xe2\x80\xa8"
                                                 "e\xe2\x80\xa9"
                                                 "f_");
        QApplication::clipboard()->setText(pasted);
        auto cursor = text->textCursor();
        cursor.setPosition(3);
        text->setTextCursor(cursor);
        text->paste();
        QCOMPARE(model->index(0, 0).data(Qt::EditRole).toString(), QString("old\r\nkeep"));
        sheet->findChild<QPushButton*>("cellEditSave")->click();
        QTRY_VERIFY(!sheet->isVisible());
        QCOMPARE(std::get<QString>(*model->cellValue(model->index(0, 0))),
                 "old" + pasted + "\r\nkeep");
    }
    void savePreservesUntouchedUnicodeWhitespaceAndLineSeparators() {
        MainWindow window;
        ObjectDataWorkspace data(window.findChild<QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        ResultColumn column{};
        column.name = "note";
        column.databaseType = "text";
        const QString original = QString::fromUtf8("a\xc2\xa0"
                                                   "b\r\nc\rd\xe2\x80\xa8"
                                                   "e\xe2\x80\xa9"
                                                   "f");
        QVERIFY(model->setPage({column}, {{original}}, 0));
        model->setEditableColumns({true}, true, true);
        openCell(grid, model->index(0, 0));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        text->moveCursor(QTextCursor::End);
        QTest::keyClicks(text, "!");
        sheet->findChild<QPushButton*>("cellEditSave")->click();
        QTRY_VERIFY(!sheet->isVisible());
        QCOMPARE(std::get<QString>(*model->cellValue(model->index(0, 0))), original + "!");
    }
    void resourceRefusalRetainsExactDraftAndAllowsCorrection() {
        MainWindow window;
        ObjectDataWorkspace data(window.findChild<QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        ResultColumn column{};
        column.name = "note";
        column.databaseType = "text";
        QVERIFY(model->setPage({column}, {{QString("original")}}, 0));
        model->setEditableColumns({true}, true, true);
        QVERIFY(model->setByteBudget(model->residentBytes()));
        openCell(grid, model->index(0, 0));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        auto* save = sheet->findChild<QPushButton*>("cellEditSave");
        text->selectAll();
        QTest::keyClicks(text, "  keep this draft exactly  ");
        QTest::keyClick(text, Qt::Key_Return);
        QTest::keyClicks(text, "second line");
        const QString draft = "  keep this draft exactly  \nsecond line";
        save->click();
        QTRY_VERIFY(save->isEnabled());
        QVERIFY(sheet->isVisible());
        QCOMPARE(text->toPlainText(), draft);
        QVERIFY(text->accessibleDescription().contains("memory budget"));
        QCOMPARE(model->index(0, 0).data().toString(), QString("original"));
        QVERIFY(!model->hasPendingEdits());
        QVERIFY(model->setByteBudget(ResultTableModel::defaultBytes()));
        save->click();
        QTRY_VERIFY(!sheet->isVisible());
        QCOMPARE(model->index(0, 0).data(Qt::EditRole).toString(), draft);
        QVERIFY(model->hasPendingEdits());
    }
    void pendingSaveCannotResurrectDismissedOrReplacedDrafts() {
        MainWindow window;
        ObjectDataWorkspace data(window.findChild<QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        ResultColumn column{};
        column.name = "note";
        column.databaseType = "text";
        QVERIFY(model->setPage({column}, {{QString("original")}}, 0));
        model->setEditableColumns({true}, true, true);
        openCell(grid, model->index(0, 0));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        auto* save = sheet->findChild<QPushButton*>("cellEditSave");
        text->selectAll();
        QTest::keyClicks(text, "canceled value");
        save->click();
        QVERIFY(!save->isEnabled());
        sheet->reject();
        openCell(grid, model->index(0, 0));
        QTRY_VERIFY(save->isEnabled());
        QVERIFY(sheet->isVisible());
        QCOMPARE(text->toPlainText(), QString("original"));
        QVERIFY(!model->hasPendingEdits());
        text->selectAll();
        QTest::keyClicks(text, "obsolete page value");
        save->click();
        QVERIFY(!save->isEnabled());
        QVERIFY(model->setPage({column}, {{QString("replacement")}}, 0));
        model->setEditableColumns({true}, true, true);
        QVERIFY(!sheet->isVisible());
        QTRY_VERIFY(save->isEnabled());
        QVERIFY(!sheet->isVisible());
        QCOMPARE(model->index(0, 0).data().toString(), QString("replacement"));
        QVERIFY(!model->hasPendingEdits());
        openCell(grid, model->index(0, 0));
        QCOMPARE(text->toPlainText(), QString("replacement"));
        QTest::keyClicks(text, " doomed");
        save->click();
        QVERIFY(!save->isEnabled());
        QPointer<QDialog> sheetGuard(sheet);
        delete data.findChild<QueryWorkspace*>();
        QVERIFY(sheetGuard.isNull());
        QCoreApplication::processEvents();
        QVERIFY(!design::detail::activeEmbeddedPopup());
    }
    void dirtyDismissalPreservesPriorStagingAndFocus_data() {
        QTest::addColumn<QString>("dismissal");
        for (const auto& path : {"Cancel", "Close", "Escape", "Backdrop"})
            QTest::newRow(path) << QString(path);
    }
    void dirtyDismissalPreservesPriorStagingAndFocus() {
        QFETCH(QString, dismissal);
        MainWindow window;
        ObjectDataWorkspace data(window.findChild<QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        ResultColumn column{};
        column.name = "note";
        column.databaseType = "text";
        QVERIFY(model->setPage({column}, {{QString("first")}, {QString("second")}}, 0));
        model->setEditableColumns({true}, true, true);
        const QString staged = "  staged\nwith whitespace  ";
        QVERIFY(model->setData(model->index(1, 0), staged));
        QVERIFY(model->setData(model->index(0, 0), "unrelated"));
        grid->setCurrentIndex(model->index(0, 0));
        grid->selectionModel()->select(model->index(1, 0), QItemSelectionModel::Select);
        grid->setFocus();
        openCell(grid, model->index(1, 0));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        QCOMPARE(text->toPlainText(), staged);
        text->selectAll();
        QTest::keyClicks(text, "discard this draft");
        if (dismissal == "Cancel")
            sheet->findChild<QPushButton*>("cellEditCancel")->click();
        else if (dismissal == "Close")
            sheet->findChild<QPushButton*>("rightSheetClose")->click();
        else if (dismissal == "Escape")
            QTest::keyClick(text, Qt::Key_Escape);
        else {
            auto* backdrop = data.findChild<QWidget*>("modalBackdrop");
            QVERIFY(backdrop && backdrop->isVisible());
            QTest::mouseClick(backdrop, Qt::LeftButton, {}, QPoint(2, 2));
        }
        QVERIFY(!sheet->isVisible());
        QCOMPARE(model->index(1, 0).data(Qt::EditRole).toString(), staged);
        QCOMPARE(model->index(0, 0).data().toString(), QString("unrelated"));
        QCOMPARE(grid->currentIndex(), model->index(0, 0));
        QTRY_VERIFY(grid->hasFocus());
        openCell(grid, model->index(1, 0));
        QCOMPARE(text->toPlainText(), staged);
        sheet->findChild<QPushButton*>("cellEditSave")->click();
        QVERIFY(!sheet->isVisible());
        QCOMPARE(model->index(1, 0).data(Qt::EditRole).toString(), staged);
    }
    void targetRemovalDiscardsTheDraft() {
        MainWindow window;
        ObjectDataWorkspace data(window.findChild<QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        ResultColumn column{};
        column.name = "note";
        column.databaseType = "text";
        QVERIFY(model->setPage({column}, {{QString("original")}}, 0));
        model->setEditableColumns({true}, true, true);
        QVERIFY(model->addRow());
        openCell(grid, model->index(1, 0));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        QTest::keyClicks(sheet->findChild<QPlainTextEdit*>("cellEditText"), "draft");
        model->markDeleted({model->index(1, 0)}, true);
        QCOMPARE(model->rowCount(), 1);
        QVERIFY(!sheet->isVisible());
        QCOMPARE(model->index(0, 0).data().toString(), QString("original"));
    }
    void workspaceTeardownReleasesItsVisibleEditor() {
        MainWindow window;
        ObjectDataWorkspace data(window.findChild<QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* workspace = data.findChild<QueryWorkspace*>();
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        ResultColumn column{};
        column.name = "note";
        column.databaseType = "text";
        QVERIFY(model->setPage({column}, {{QString("original")}}, 0));
        model->setEditableColumns({true}, true, true);
        openCell(grid, model->index(0, 0));
        QPointer<QDialog> sheet = data.findChild<QDialog*>("cellEditSheet");
        QVERIFY(sheet && sheet->isVisible());
        delete workspace;
        QVERIFY(sheet.isNull());
    }
    void externalTargetChangeClosesDraftWithoutOverwritingNewValue() {
        MainWindow window;
        ObjectDataWorkspace data(window.findChild<QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        ResultColumn column{};
        column.name = "note";
        column.databaseType = "text";
        QVERIFY(model->setPage({column}, {{QString("first")}, {QString("second")}}, 0));
        model->setEditableColumns({true}, true, true);
        openCell(grid, model->index(0, 0));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        QTest::keyClicks(text, " draft");
        QVERIFY(model->setData(model->index(1, 0), "other staged"));
        QVERIFY(sheet->isVisible());
        QVERIFY(model->setData(model->index(0, 0), "newer external value"));
        QVERIFY(!sheet->isVisible());
        sheet->findChild<QPushButton*>("cellEditSave")->click();
        QCOMPARE(model->index(0, 0).data().toString(), QString("newer external value"));
        QCOMPARE(model->index(1, 0).data().toString(), QString("other staged"));
        openCell(grid, model->index(0, 0));
        QCOMPARE(text->toPlainText(), QString("newer external value"));
        sheet->reject();
    }
    void saveStagesOnlyClickedCellAndRetainsInvalidDraft_data() {
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("original");
        QTest::addColumn<QString>("other");
        QTest::addColumn<QString>("invalid");
        QTest::addColumn<QString>("guidance");
        QTest::addColumn<QString>("corrected");
        QTest::newRow("integer") << QString("integer") << QString("20") << QString("11")
                                 << QString("invalid") << QString("whole number") << QString("42");
        QTest::newRow("boolean") << QString("boolean") << QString("false") << QString("false")
                                 << QString("maybe") << QString("true, false") << QString("true");
        QTest::newRow("float") << QString("double precision") << QString("20") << QString("11")
                               << QString("1e999") << QString("real number") << QString("42.5");
    }
    void saveStagesOnlyClickedCellAndRetainsInvalidDraft() {
        QFETCH(QString, type);
        QFETCH(QString, original);
        QFETCH(QString, other);
        QFETCH(QString, invalid);
        QFETCH(QString, guidance);
        QFETCH(QString, corrected);
        MainWindow window;
        ObjectDataWorkspace data(window.findChild<QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        ResultColumn column{};
        column.name = "count";
        column.databaseType = type;
        const Cell first = type == "boolean"            ? Cell(true)
                           : type == "double precision" ? Cell(10.0)
                                                        : Cell(qint64(10));
        const Cell second = type == "boolean"            ? Cell(false)
                            : type == "double precision" ? Cell(20.0)
                                                         : Cell(qint64(20));
        QVERIFY(model->setPage({column}, {{first}, {second}}, 0));
        model->setEditableColumns({true}, true, true);
        QVERIFY(model->setData(model->index(0, 0), other));
        grid->setCurrentIndex(model->index(0, 0));
        openCell(grid, model->index(1, 0));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        auto* save = sheet->findChild<QPushButton*>("cellEditSave");
        text->selectAll();
        QTest::keyClicks(text, invalid);
        save->click();
        auto* status = sheet->findChild<QLabel*>("cellEditStatus");
        QTRY_VERIFY(status->text().contains(guidance, Qt::CaseInsensitive));
        QVERIFY(sheet->isVisible());
        QCOMPARE(text->toPlainText(), invalid);
        QVERIFY(!text->accessibleDescription().isEmpty());
        QCOMPARE(model->index(1, 0).data(Qt::EditRole).toString(), original);
        text->selectAll();
        QTest::keyClicks(text, corrected);
        save->click();
        QTRY_VERIFY(!sheet->isVisible());
        QCOMPARE(model->index(0, 0).data().toString(), other);
        QCOMPARE(model->index(1, 0).data(Qt::EditRole).toString(), corrected);
        QVERIFY(model->hasPendingEdits());
    }
    void clickedCellShowsCurrentMultilineDraftWithoutStaging() {
        MainWindow window;
        ObjectDataWorkspace data(window.findChild<QueryWorkspace*>());
        data.resize(800, 500);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<ResultTableModel*>(grid->model());
        ResultColumn column{};
        column.name = "note";
        column.databaseType = "text";
        QVERIFY(model->setPage({column},
                               {{QString("selected")}, {QString("  first\nsecond \t\" ")}}, 0));
        model->setEditableColumns({true}, true, true);
        QVERIFY(model->setData(model->index(0, 0), "previous staged"));
        grid->setCurrentIndex(model->index(0, 0));
        grid->setFocus();
        openCell(grid, model->index(1, 0));
        auto* sheet = data.findChild<QDialog*>("cellEditSheet");
        QVERIFY(sheet);
        QVERIFY(sheet->isVisible());
        auto* text = sheet->findChild<QPlainTextEdit*>("cellEditText");
        QVERIFY(text);
        QCOMPARE(text->toPlainText(), QString("  first\nsecond \t\" "));
        QTRY_VERIFY(text->hasFocus());
        QTest::keyClick(text, Qt::Key_End, Qt::ControlModifier);
        QTest::keyClick(text, Qt::Key_Return);
        QVERIFY(text->toPlainText().endsWith('\n'));
        QCOMPARE(model->index(1, 0).data(Qt::EditRole).toString(),
                 QString("  first\nsecond \t\" "));
        sheet->findChild<QPushButton*>("cellEditCancel")->click();
        QVERIFY(!sheet->isVisible());
        QCOMPARE(model->index(0, 0).data().toString(), QString("previous staged"));
        QCOMPARE(grid->currentIndex(), model->index(0, 0));
        QTRY_VERIFY(grid->hasFocus());
    }
};
QTEST_MAIN(CellEditWorkspaceTest)
#include "cell_edit_workspace_test.moc"
