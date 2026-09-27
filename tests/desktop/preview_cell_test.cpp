#include "design_system/table/table_style.h"
#include "preview_test.h"
#include "tools/preview/preview_window.h"
#include <QAction>
#include <QComboBox>
#include <QPointer>
#include <QStandardItemModel>
#include <QTableView>
#include <QTableWidget>
#include <QtTest>

void PreviewTest::tableHoverPreservesBackgroundInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("tables"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* table = host->findChild<QTableWidget*>();
        QVERIFY(table);
        table->setMouseTracking(true);
        for (int row = 0; row < 2; ++row) {
            const auto cell = table->visualItemRect(table->item(row, 0));
            const auto before = table->viewport()->grab().toImage();
            QTest::mouseMove(table->viewport(), cell.center());
            QCoreApplication::processEvents();
            const auto after = table->viewport()->grab().toImage();
            const auto sample = cell.topLeft() + QPoint(3, 3);
            QCOMPARE(after.pixelColor(sample), before.pixelColor(sample));
        }
    }
}

void PreviewTest::typedTableSpecimenUsesSharedControlsInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("tables"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* table = host->findChild<QTableWidget*>("previewResultTable");
        QVERIFY(table);
        QVERIFY(table->itemDelegate());
        QVERIFY(table->item(0, 1)->data(Qt::UserRole + 20).toStringList().contains("true"));
        QCOMPARE(table->item(1, 1)->data(Qt::UserRole + 22).toString(),
                 QString("Open customers.id"));
        QVERIFY(!(table->item(1, 1)->flags() & Qt::ItemIsEditable));
        QVERIFY(table->actions().size() > 0);
    }
}

void PreviewTest::typedTableDropdownAndLinkActionWorkThroughView() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("tables"));
    window.show();
    QCoreApplication::processEvents();
    auto* host = window.findChild<QWidget*>("previewLight");
    QVERIFY(host);
    auto* table = host->findChild<QTableWidget*>("previewResultTable");
    QVERIFY(table);
    table->setCurrentCell(0, 1);
    table->editItem(table->item(0, 1));
    auto* editor = table->findChild<QComboBox*>();
    QVERIFY(editor);
    QVERIFY(!editor->isEditable());
    QCOMPARE(editor->count(), 3);
    QCOMPARE(editor->itemText(0), QString("true"));
    QCOMPARE(editor->itemText(1), QString("false"));
    QCOMPARE(editor->itemText(2), QString("NULL"));
    QPointer<QComboBox> editorGuard = editor;
    QTest::keyClick(editor, Qt::Key_Escape);
    QTRY_VERIFY(editorGuard.isNull() || !editorGuard->isVisible());
    QCOMPARE(table->item(0, 1)->text(), QString("true"));

    table->setCurrentCell(1, 1);
    auto* action = [&] {
        for (auto* candidate : table->actions())
            if (candidate->objectName() == "resultCellOpenReference")
                return candidate;
        return static_cast<QAction*>(nullptr);
    }();
    QVERIFY(action);
    QVERIFY(action->isEnabled());
    QCOMPARE(action->text(), QString("Open customers.id"));
    QSignalSpy activation(table->itemDelegate(), SIGNAL(linkActivated(QModelIndex)));
    QVERIFY(activation.isValid());
    action->trigger();
    QCOMPARE(activation.count(), 1);
    QCOMPARE(activation.takeFirst().at(0).value<QModelIndex>(), table->model()->index(1, 1));
    table->setFocus();
    QTest::keyClick(table, Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(activation.count(), 1);
    QCOMPARE(activation.takeFirst().at(0).value<QModelIndex>(), table->model()->index(1, 1));
    const QRect referenceCell = table->visualItemRect(table->item(1, 1));
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {},
                      referenceCell.topLeft() + QPoint(12, referenceCell.height() / 2));
    QCOMPARE(activation.count(), 0);
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {},
                      referenceCell.topRight() + QPoint(-8, referenceCell.height() / 2));
    QCOMPARE(activation.count(), 1);
    QCOMPARE(activation.takeFirst().at(0).value<QModelIndex>(), table->model()->index(1, 1));
}

void PreviewTest::resultTableActionTracksModelReplacement() {
    QTableView table;
    auto* delegate = choscordb::design::configureResultTable(table);
    QStandardItemModel model(1, 1);
    model.setData(model.index(0, 0), "42");
    model.setData(model.index(0, 0), "Open customers.id",
                  choscordb::design::ForeignKeyLinkLabelRole);
    table.setModel(&model);
    table.show();
    QCoreApplication::processEvents();
    table.setCurrentIndex(model.index(0, 0));
    auto* action = table.findChild<QAction*>("resultCellOpenReference");
    QVERIFY(action);
    QVERIFY(action->isEnabled());
    QSignalSpy activation(delegate, &choscordb::design::ResultTableDelegate::linkActivated);
    action->trigger();
    QCOMPARE(activation.count(), 1);
    model.setData(model.index(0, 0), QVariant(), choscordb::design::ForeignKeyLinkLabelRole);
    QVERIFY(!action->isEnabled());
}

void PreviewTest::typedTableDropdownStagesSelection() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("tables"));
    window.show();
    QCoreApplication::processEvents();
    auto* host = window.findChild<QWidget*>("previewLight");
    QVERIFY(host);
    auto* table = host->findChild<QTableWidget*>("previewResultTable");
    QVERIFY(table);
    table->editItem(table->item(0, 1));
    auto* editor = table->findChild<QComboBox*>("resultCellChoiceEditor");
    QVERIFY(editor);
    QTest::keyClick(editor, Qt::Key_Down);
    QCOMPARE(editor->currentText(), QString("false"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTRY_COMPARE(table->item(0, 1)->text(), QString("false"));
}
