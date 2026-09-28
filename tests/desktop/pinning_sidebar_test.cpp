#include "app/main_window.h"
#include "app/navigator_controller.h"
#include "app/pin_store.h"
#include "design_system/theme.h"
#include "models/navigator_model.h"
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWheelEvent>

class PinningSidebarTest final : public QObject {
    Q_OBJECT

  private slots:
    void pinnedSectionShowsEmptyStateWithoutSelection() {
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
        window.show();

        auto* panel = window.findChild<QWidget*>("connectionsPanel");
        auto* section = window.findChild<QWidget*>("pinnedSection");
        auto* list = window.findChild<QListWidget*>("pinnedList");
        auto* empty = window.findChild<QLabel*>("pinnedEmpty");
        auto* filter = window.findChild<QLineEdit*>("navigatorFilter");
        QVERIFY(panel && section && list && empty && filter);
        auto* scroll = window.findChild<QScrollArea*>("connectionsScroll");
        auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
        QVERIFY(scroll && layout);
        QCOMPARE(scroll->widget(), panel);
        QCOMPARE(layout->itemAt(1)->widget(), section);
        QCOMPARE(layout->itemAt(2)->widget()->findChild<QLineEdit*>("navigatorFilter"), filter);
        QVERIFY(!section->isVisible());
        QVERIFY(!section->accessibleName().isEmpty());
        QVERIFY(!empty->isVisible());
        QVERIFY(empty->text().contains("pin", Qt::CaseInsensitive));
        QCOMPARE(list->count(), 0);
        QVERIFY(!list->isVisible());
    }

    void restoredPinsScrollAndIgnoreObjectFilter() {
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        const auto path = storage.filePath("settings.sqlite");
        QList<choscordb::PinRecord> pins;
        for (int index = 0; index < 31; ++index) {
            const auto name = QStringLiteral("object_%1").arg(index);
            pins.append(choscordb::PinRecord{
                .profileId = QStringLiteral("saved-profile"),
                .profileName = QStringLiteral("Saved profile"),
                .objectId = QStringLiteral("pg:relation:%1").arg(index),
                .name = name,
                .qualifiedName = QStringLiteral("public.%1").arg(name),
                .kind = QStringLiteral("table"),
                .parentObjectId = QStringLiteral("pg:schema:1"),
                .ancestryIds = {QStringLiteral("pg:database:1"), QStringLiteral("pg:schema:1")},
                .ancestryNames = {QStringLiteral("database"), QStringLiteral("public")}});
        }
        QString error;
        QVERIFY2(choscordb::PinStore(path).save(pins, &error), qPrintable(error));

        choscordb::MainWindow window(nullptr, path);
        window.show();
        auto* list = window.findChild<QListWidget*>("pinnedList");
        auto* scroll = window.findChild<QScrollArea*>("connectionsScroll");
        auto* section = window.findChild<QWidget*>("pinnedSection");
        auto* empty = window.findChild<QLabel*>("pinnedEmpty");
        auto* filter = window.findChild<QLineEdit*>("navigatorFilter");
        QVERIFY(list && scroll && section && empty && filter);
        QTRY_COMPARE(list->count(), 31);
        QVERIFY(section->isVisible());
        QVERIFY(list->isVisible());
        QVERIFY(!empty->isVisible());
        QVERIFY(!list->accessibleName().isEmpty());
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QCOMPARE(list->verticalScrollBar()->maximum(), 0);
        QCOMPARE(list->item(0)->text(), QStringLiteral("object_0"));
        QCOMPARE(list->item(0)->data(choscordb::NavigatorModel::KindRole).toString(),
                 QStringLiteral("table"));
        QVERIFY(list->item(0)->toolTip().contains("Saved profile"));
        auto* first = list->item(0);
        const auto hoverRow = list->visualItemRect(first);
        QTest::mouseMove(list->viewport(), QPoint(1, 1));
        QTest::mouseMove(list->viewport(), hoverRow.center());
        QCoreApplication::processEvents();
        const auto hovered = list->viewport()->grab().toImage();
        const auto muted = choscordb::design::resolvedThemeForWidget(*list).colors.muted;
        QCOMPARE(hovered.pixelColor(hoverRow.right() - 8, hoverRow.center().y()), muted);
        QVERIFY(hovered.pixelColor(hoverRow.left() + 2, hoverRow.center().y()) != muted);
        QCOMPARE(hovered.pixelColor(hoverRow.left() + 8, hoverRow.center().y()), muted);
        QVERIFY(hovered.pixelColor(hoverRow.left() + 8, hoverRow.top() + 1) != muted);
        QCOMPARE(hovered.pixelColor(hoverRow.left() + 8, hoverRow.top() + 4), muted);
        list->setCurrentItem(first);
        QCoreApplication::processEvents();
        const auto selected = list->viewport()->grab().toImage();
        const QRect textArea(hoverRow.left() + 30, hoverRow.top() + 4, 90, hoverRow.height() - 8);
        QCOMPARE(hovered.copy(textArea), selected.copy(textArea));
        list->setCurrentItem(nullptr);
        const auto name = first->text();
        first->setText({});
        QCoreApplication::processEvents();
        const auto row = list->visualItemRect(first);
        const auto withIcon = list->viewport()->grab(row).toImage();
        first->setData(choscordb::NavigatorModel::KindRole, QStringLiteral("group"));
        QCoreApplication::processEvents();
        QVERIFY(withIcon != list->viewport()->grab(row).toImage());
        first->setData(choscordb::NavigatorModel::KindRole, QStringLiteral("table"));
        first->setText(name);
        auto* navigator = window.findChild<choscordb::NavigatorController*>();
        auto* tree = window.findChild<QTreeView*>("databaseNavigator");
        QVERIFY(navigator && tree);
        QVERIFY(navigator->model()->addConnection(777, QStringLiteral("Sample connection")));
        navigator->setVisibleConnections({777});
        QTRY_COMPARE(tree->model()->rowCount(), 1);
        QTRY_COMPARE(list->visualItemRect(first).height(),
                     tree->visualRect(tree->model()->index(0, 0)).height());

        scroll->verticalScrollBar()->setValue(0);
        const auto pinPoint = list->viewport()->rect().center();
        QWheelEvent pinWheel(pinPoint, list->viewport()->mapToGlobal(pinPoint), {}, {0, -120},
                             Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(list->viewport(), &pinWheel);
        QTRY_VERIFY(scroll->verticalScrollBar()->value() > 0);

        filter->setText("different_object");
        QTRY_COMPARE(list->count(), 31);
        QVERIFY(list->isVisible());
        QVERIFY(list->item(0)->text().contains("object_0"));
    }

    void shortPinnedAndObjectSectionsStayTogetherAtTop() {
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        const auto path = storage.filePath("settings.sqlite");
        const choscordb::PinRecord pin{
            .profileId = QStringLiteral("saved-profile"),
            .profileName = QStringLiteral("Saved profile"),
            .objectId = QStringLiteral("pg:relation:1"),
            .name = QStringLiteral("orders"),
            .qualifiedName = QStringLiteral("public.orders"),
            .kind = QStringLiteral("table"),
            .parentObjectId = QStringLiteral("pg:schema:1"),
            .ancestryIds = {QStringLiteral("pg:database:1"), QStringLiteral("pg:schema:1")},
            .ancestryNames = {QStringLiteral("database"), QStringLiteral("public")}};
        QString error;
        QVERIFY2(choscordb::PinStore(path).save({pin}, &error), qPrintable(error));

        choscordb::MainWindow window(nullptr, path);
        window.resize(1280, 1000);
        window.show();
        auto* panel = window.findChild<QWidget*>("connectionsPanel");
        auto* pinned = window.findChild<QWidget*>("pinnedSection");
        auto* list = window.findChild<QListWidget*>("pinnedList");
        auto* filter = window.findChild<QLineEdit*>("navigatorFilter");
        auto* tree = window.findChild<QTreeView*>("databaseNavigator");
        QVERIFY(panel && pinned && list && filter && tree);
        QTRY_COMPARE(list->count(), 1);
        QTRY_VERIFY(pinned->height() >= list->mapTo(pinned, QPoint(0, list->height())).y());
        auto* navigator = window.findChild<choscordb::NavigatorController*>();
        QVERIFY(navigator);
        QVERIFY(navigator->model()->addConnection(777, QStringLiteral("Sample connection")));
        navigator->setVisibleConnections({777});
        QTRY_COMPARE(tree->model()->rowCount(), 1);
        QTRY_VERIFY(tree->height() > 8);
        QTRY_VERIFY(!window.findChild<QLabel*>("sidebarObjectsEmpty")->isVisible());
        auto* objects = filter->parentWidget();
        QVERIFY(objects);
        const int sectionGap = objects->mapTo(panel, QPoint()).y() -
                               pinned->mapTo(panel, QPoint(0, pinned->height())).y();
        const int pinnedContentGap =
            objects->mapTo(panel, QPoint()).y() - list->mapTo(panel, QPoint(0, list->height())).y();
        const int filterInset = filter->mapTo(objects, QPoint()).y();
        const int treeGap = tree->mapTo(objects, QPoint()).y() -
                            filter->mapTo(objects, QPoint(0, filter->height())).y();
        QVERIFY2(sectionGap < 40, "Pinned and Schema & Objects must stay together");
        QVERIFY2(pinnedContentGap < 60, "Pinned rows must stay near the next heading");
        QVERIFY2(filterInset < 60, "Object filter must stay near its section heading");
        QVERIFY2(treeGap < 40, "Object tree must stay below its filter");
    }

    void wrappedObjectEmptyMessageIsFullyVisibleBelowPins() {
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        const auto path = storage.filePath("settings.sqlite");
        const choscordb::PinRecord pin{
            .profileId = QStringLiteral("saved-profile"),
            .profileName = QStringLiteral("Saved profile"),
            .objectId = QStringLiteral("pg:relation:1"),
            .name = QStringLiteral("orders"),
            .qualifiedName = QStringLiteral("public.orders"),
            .kind = QStringLiteral("table"),
            .parentObjectId = QStringLiteral("pg:schema:1"),
            .ancestryIds = {QStringLiteral("pg:database:1"), QStringLiteral("pg:schema:1")},
            .ancestryNames = {QStringLiteral("database"), QStringLiteral("public")}};
        auto secondPin = pin;
        secondPin.objectId = QStringLiteral("pg:relation:2");
        secondPin.name = QStringLiteral("customers");
        secondPin.qualifiedName = QStringLiteral("public.customers");
        QString error;
        QVERIFY2(choscordb::PinStore(path).save({pin, secondPin}, &error), qPrintable(error));

        choscordb::MainWindow window(nullptr, path);
        window.resize(1280, 900);
        auto* navigator = window.findChild<choscordb::NavigatorController*>();
        QVERIFY(navigator);
        QVERIFY(navigator->model()->addConnection(777, QStringLiteral("Sample connection")));
        navigator->setVisibleConnections({777});
        auto* tree = window.findChild<QTreeView*>("databaseNavigator");
        auto* empty = window.findChild<QLabel*>("sidebarObjectsEmpty");
        QVERIFY(tree && empty);
        QTRY_COMPARE(tree->model()->rowCount(), 1);
        QVERIFY(empty->isHidden());
        window.show();
        auto* list = window.findChild<QListWidget*>("pinnedList");
        auto* scroll = window.findChild<QScrollArea*>("connectionsScroll");
        QVERIFY(list && empty && tree && scroll);
        QTRY_COMPARE(list->count(), 2);
        window.resize(960, 640);
        QTRY_COMPARE(tree->model()->rowCount(), 1);
        navigator->setVisibleConnections({});
        QTRY_COMPARE(tree->model()->rowCount(), 0);
        QTRY_VERIFY(tree->height() <= 8);
        QTRY_VERIFY(empty->width() > 0);
        QTRY_VERIFY(empty->isVisible() && empty->width() > 0);
        scroll->setFixedWidth(200);
        scroll->setFixedHeight(220);
        QTRY_VERIFY(empty->width() < 200);
        QVERIFY(empty->text().contains("No database selected"));
        QVERIFY2(empty->height() >= empty->heightForWidth(empty->width()),
                 "Wrapped object empty message must fit its rendered lines");
    }

    void navigatorCurrentRowRevealsInSharedScroll() {
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
        window.show();
        auto* navigator = window.findChild<choscordb::NavigatorController*>();
        auto* tree = window.findChild<QTreeView*>("databaseNavigator");
        auto* scroll = window.findChild<QScrollArea*>("connectionsScroll");
        QVERIFY(navigator && tree && scroll);
        QTest::qWait(100);
        QList<quint64> ids;
        for (quint64 id = 1; id <= 31; ++id) {
            QVERIFY(navigator->model()->addConnection(id, QStringLiteral("Connection %1").arg(id)));
            ids.append(id);
        }
        navigator->setVisibleConnections(ids);
        QTRY_COMPARE(tree->model()->rowCount(), 31);
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QCOMPARE(tree->verticalScrollBar()->maximum(), 0);
        scroll->verticalScrollBar()->setValue(0);
        const auto treePoint = tree->viewport()->rect().center();
        QWheelEvent treeWheel(treePoint, tree->viewport()->mapToGlobal(treePoint), {}, {0, -120},
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(tree->viewport(), &treeWheel);
        QTRY_VERIFY(scroll->verticalScrollBar()->value() > 0);
        scroll->verticalScrollBar()->setValue(0);
        tree->setCurrentIndex(tree->model()->index(30, 0));
        QTRY_VERIFY(scroll->verticalScrollBar()->value() > 0);
    }
};

QTEST_MAIN(PinningSidebarTest)
#include "pinning_sidebar_test.moc"
