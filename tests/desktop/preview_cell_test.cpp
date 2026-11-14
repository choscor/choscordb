#include "design_system/table/table_style.h"
#include "design_system/theme.h"
#include "preview_test.h"
#include "tools/preview/preview_window.h"
#include <QAction>
#include <QComboBox>
#include <QHeaderView>
#include <QPointer>
#include <QStandardItemModel>
#include <QTableView>
#include <QTableWidget>
#include <QtTest>

void PreviewTest::tableHoverUsesHoverOverlayInBothThemes() {
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
        table->clearSelection();
        const auto colors = choscordb::design::resolvedThemeForWidget(*table).colors;
        const auto raised = choscordb::design::composite(colors.tint, colors.base);
        for (int row = 0; row < 2; ++row) {
            const auto cell = table->visualItemRect(table->item(row, 0));
            QTest::mouseMove(table->viewport(), cell.center());
            QCoreApplication::processEvents();
            const auto after = table->viewport()->grab().toImage();
            const auto sample = (cell.topLeft() + QPoint(3, 3)) * after.devicePixelRatio();
            QCOMPARE(after.pixelColor(sample), raised);
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

void PreviewTest::choiceEditorSpecimenShowsRealControlInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("tables"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* table = host->findChild<QTableWidget*>("previewChoiceEditorTable");
        QVERIFY(table);
        auto* choice = table->findChild<QComboBox*>("resultCellChoiceEditor");
        QVERIFY(choice);
        QVERIFY(choice->isVisible());
        QCOMPARE(choice->count(), 2);
        QCOMPARE(choice->currentText(), QString("true"));
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

void PreviewTest::changedCellsShowAnEdgeMarkAndInsertedRowsOnlyATint() {
    using choscordb::design::CellChange;
    QTableView table;
    choscordb::design::configureResultTable(table);
    QStandardItemModel model(3, 1);
    const CellChange changes[] = {CellChange::Changed, CellChange::Inserted, CellChange::None};
    for (int row = 0; row < 3; ++row) {
        model.setData(model.index(row, 0), "value");
        model.setData(model.index(row, 0), int(changes[row]), choscordb::design::CellChangeRole);
    }
    table.setModel(&model);
    table.show();
    QCoreApplication::processEvents();
    const auto colors = choscordb::design::resolvedThemeForWidget(table).colors;
    const auto image = table.viewport()->grab().toImage();
    const auto edge = [&](int row) {
        const auto rect = table.visualRect(model.index(row, 0));
        return image.pixelColor(QPoint(rect.left() + 1, rect.center().y()) *
                                table.viewport()->devicePixelRatioF());
    };
    QCOMPARE(edge(0), colors.textWarning);
    const auto inserted = table.visualRect(model.index(1, 0));
    QCOMPARE(edge(1), image.pixelColor(QPoint(inserted.right() - 2, inserted.center().y()) *
                                       table.viewport()->devicePixelRatioF()));
    QVERIFY(edge(2) != colors.textWarning && edge(2) != colors.textSuccess);
}

void PreviewTest::dataTablesStripeRowsAndEndTheRowHeaderAtTheLastRow() {
    using namespace choscordb::design;
    const ResolvedTheme theme{ResolvedAppearance::Light, resolveColors(ResolvedAppearance::Light),
                              false};
    QWidget host;
    host.setPalette(applicationPalette(theme));
    host.setStyleSheet(applicationStyleSheet(theme));
    auto* table = new QTableView(&host);
    choscordb::design::configureDataTable(*table);
    QStandardItemModel model(2, 1);
    table->setModel(&model);
    host.resize(300, 300);
    table->resize(300, 300);
    host.show();
    QCoreApplication::processEvents();
    const auto image = table->grab().toImage();
    const auto ratio = image.devicePixelRatio();
    const auto rowColor = [&](int row) {
        const auto rect = table->visualRect(model.index(row, 0));
        const auto point = table->viewport()->mapTo(table, rect.center());
        return image.pixelColor(point * ratio);
    };
    QVERIFY(rowColor(0) != rowColor(1));
    auto* header = table->verticalHeader();
    const int below = header->sectionViewportPosition(1) + header->sectionSize(1) + 20;
    const auto empty = header->mapTo(table, QPoint(header->width() - 1, below));
    QCOMPARE(image.pixelColor(empty * ratio), table->palette().color(QPalette::Base));
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

void PreviewTest::tableHeadersCanBeDraggedInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("tables"));
    window.show();
    for (const auto* theme : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(theme);
        auto* table = host->findChild<QTableWidget*>("previewResultTable");
        QVERIFY(table);
        auto* header = table->horizontalHeader();
        QCOMPARE(header->sectionResizeMode(0), QHeaderView::Interactive);
        const int oldWidth = header->sectionSize(0);
        header->resizeSection(0, oldWidth + 32);
        QCOMPARE(header->sectionSize(0), oldWidth + 32);
    }
}
