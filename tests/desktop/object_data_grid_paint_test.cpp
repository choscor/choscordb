#include "app/main_window.h"
#include "app/object_data_workspace.h"
#include "app/query_workspace.h"
#include "design_system/metrics/metrics.h"
#include "models/result_table_model.h"
#include <QApplication>
#include <QHeaderView>
#include <QMouseEvent>
#include <QTableView>
#include <QtTest>
#include <algorithm>
namespace {
struct HeaderFixture {
    choscordb::MainWindow window;
    choscordb::ObjectDataWorkspace data{window.findChild<choscordb::QueryWorkspace*>()};
    QHeaderView* header = nullptr;
    bool ready() {
        data.resize(500, 400);
        data.show();
        if (!QTest::qWaitForWindowExposed(&data))
            return false;
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        choscordb::ResultColumn first{}, second{};
        first.name = "sku";
        second.name = "name";
        if (!model->setPage({first, second}, {{QString("MOU-WLS"), QString("Wireless mouse")}}, 0))
            return false;
        header = grid->horizontalHeader();
        header->setSectionsClickable(true);
        QCoreApplication::processEvents();
        return true;
    }
    QRect section(int index) const {
        return {header->sectionViewportPosition(index), 0, header->sectionSize(index),
                header->height()};
    }
    QImage grab(int index) const {
        QCoreApplication::processEvents();
        return header->viewport()->grab(section(index)).toImage();
    }
};

}

class ObjectDataGridPaintTest : public QObject {
    Q_OBJECT
  private slots:
    void hoveringCellPreservesTheRowBackgroundAndText() {
        using namespace choscordb::design;
        choscordb::MainWindow window;
        auto* sql = window.findChild<choscordb::QueryWorkspace*>();
        choscordb::ObjectDataWorkspace data(sql);
        data.resize(500, 400);
        data.show();
        auto* grid = data.findChild<QTableView*>("objectDataResults");
        auto* model = qobject_cast<choscordb::ResultTableModel*>(grid->model());
        choscordb::ResultColumn first{}, second{};
        first.name = "sku";
        second.name = "name";
        QVERIFY(
            model->setPage({first, second}, {{QString("MOU-WLS"), QString("Wireless mouse")}}, 0));
        grid->setColumnWidth(0, dimension(Dimension::TableColumn));
        QCoreApplication::processEvents();
        const auto firstCell = grid->visualRect(model->index(0, 0));
        const auto secondCell = grid->visualRect(model->index(0, 1));
        const auto textLeft = [&] {
            const auto image = grid->viewport()->grab().toImage();
            const int background =
                image.pixelColor(firstCell.right() - 4, firstCell.center().y()).lightness();
            int darkest = background;
            for (int x = firstCell.left() + 2; x < firstCell.right() - 2; ++x)
                for (int y = firstCell.top() + 2; y < firstCell.bottom() - 2; ++y)
                    darkest = qMin(darkest, image.pixelColor(x, y).lightness());
            const int cutoff = background - (background - darkest) * 6 / 10;
            for (int x = firstCell.left() + 2; x < firstCell.right() - 2; ++x)
                for (int y = firstCell.top() + 2; y < firstCell.bottom() - 2; ++y)
                    if (image.pixelColor(x, y).lightness() < cutoff)
                        return x;
            return -1;
        };
        const int normalTextLeft = textLeft();
        QVERIFY2(normalTextLeft >= firstCell.left() + spacing(Spacing::Three),
                 qPrintable(QString("text %1 cell %2").arg(normalTextLeft).arg(firstCell.left())));
        const auto normal = grid->viewport()->grab().toImage();
        QMouseEvent hover(QEvent::MouseMove, firstCell.center(),
                          grid->viewport()->mapToGlobal(firstCell.center()), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(grid->viewport(), &hover);
        QCoreApplication::processEvents();
        const auto hovered = grid->viewport()->grab().toImage();
        QCOMPARE(hovered.pixelColor(secondCell.right() - 4, secondCell.center().y()),
                 normal.pixelColor(secondCell.right() - 4, secondCell.center().y()));
        QCOMPARE(textLeft(), normalTextLeft);
    }

    void resultHeaderSectionHasNoKumoHoverOrPressedFill() {
        HeaderFixture fixture;
        QVERIFY(fixture.ready());
        auto* viewport = fixture.header->viewport();
        const auto inside = fixture.section(1).center();
        QTest::mouseMove(viewport, fixture.section(0).center());
        const auto rest = fixture.grab(1);
        QTest::mouseMove(viewport, inside);
        QCOMPARE(fixture.grab(1), rest);
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, inside);
        QCOMPARE(fixture.grab(1), rest);
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, inside);
    }

    void resultHeaderSortMarkUsesTheSmallIconSizeAndShowsItsDirection() {
        using namespace choscordb::design;
        HeaderFixture fixture;
        QVERIFY(fixture.ready());
        fixture.header->setSortIndicatorShown(false);
        const auto plain = fixture.grab(0);
        const auto mark = [&](Qt::SortOrder order) {
            fixture.header->setSortIndicator(0, order);
            fixture.header->setSortIndicatorShown(true);
            const auto marked = fixture.grab(0);
            QRect box;
            QList<int> widths(marked.height(), 0);
            for (int y = 0; y < marked.height(); ++y)
                for (int x = 0; x < marked.width(); ++x)
                    if (marked.pixel(x, y) != plain.pixel(x, y)) {
                        box |= QRect(x, y, 1, 1);
                        ++widths[y];
                    }
            const auto widest = std::max_element(widths.cbegin(), widths.cend()) - widths.cbegin();
            return std::pair{box, int(widest)};
        };
        const auto [up, upWidest] = mark(Qt::AscendingOrder);
        const auto [down, downWidest] = mark(Qt::DescendingOrder);
        const int minimum = dimension(Dimension::IconSmall) / 2;
        QVERIFY2(up.height() >= minimum, qPrintable(QString::number(up.height())));
        QVERIFY2(down.height() >= minimum, qPrintable(QString::number(down.height())));
        QVERIFY(upWidest < up.center().y());
        QVERIFY(downWidest > down.center().y());
    }
};
QTEST_MAIN(ObjectDataGridPaintTest)
#include "object_data_grid_paint_test.moc"
