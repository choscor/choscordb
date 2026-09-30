#include "app/main_window.h"
#include "app/navigator_controller.h"
#include "app/pin_store.h"
#include "app/pinned_tree_model.h"
#include "app/query_workspace.h"
#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
#include "models/navigator_model.h"
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {
choscordb::PinRecord pin(int number = 1) {
    const auto name = QStringLiteral("object_%1").arg(number);
    return {.profileId = QStringLiteral("saved-profile"),
            .profileName = QStringLiteral("Saved profile"),
            .objectId = QStringLiteral("pg:relation:%1").arg(number),
            .name = name,
            .qualifiedName = QStringLiteral("public.%1").arg(name),
            .kind = QStringLiteral("table"),
            .parentObjectId = QStringLiteral("pg:schema:1"),
            .ancestryIds = {QStringLiteral("pg:database:1"), QStringLiteral("pg:schema:1")},
            .ancestryNames = {QStringLiteral("database"), QStringLiteral("public")}};
}
bool savePins(const QString& path, const QList<choscordb::PinRecord>& pins) {
    QString error;
    return choscordb::PinStore(path).save(pins, &error);
}
} // namespace

class PinningSidebarTest final : public QObject {
    Q_OBJECT
  private slots:
    void restoredPinsUseNavigationRows() {
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        const auto path = storage.filePath("settings.sqlite");
        QVERIFY(savePins(path, {pin()}));
        choscordb::MainWindow window(nullptr, path);
        window.show();
        auto* pins = window.findChild<QTreeView*>("pinnedList");
        QVERIFY2(pins, "Pinned rows should use the same navigation tree as Schema & Objects");
        QTRY_COMPARE(pins->model()->rowCount(), 1);
        const auto root = pins->model()->index(0, 0);
        QCOMPARE(root.data(Qt::DisplayRole).toString(), QStringLiteral("object_1"));
        QCOMPARE(root.data(choscordb::NavigatorModel::KindRole).toString(),
                 QStringLiteral("table"));
        QVERIFY(pins->model()->hasChildren(root));
        QVERIFY(!pins->isExpanded(root));
    }

    void expandedPinnedTableShowsOnlyColumnsAndFitsVisibleRows() {
        using namespace choscordb;
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        const auto path = storage.filePath("settings.sqlite");
        const auto tablePin = pin();
        PinRecord schemaPin = tablePin;
        schemaPin.objectId = "pg:schema:1";
        schemaPin.name = "public";
        schemaPin.qualifiedName = "public";
        schemaPin.kind = "schema";
        schemaPin.parentObjectId = "pg:database:1";
        schemaPin.ancestryIds = {"pg:database:1"};
        schemaPin.ancestryNames = {"database"};
        QVERIFY(savePins(path, {tablePin, schemaPin}));
        MainWindow window(nullptr, path);
        window.resize(960, 640);
        window.show();
        auto* pins = window.findChild<QTreeView*>("pinnedList");
        auto* navigator = window.findChild<NavigatorController*>();
        auto* workspace = window.findChild<QueryWorkspace*>();
        QVERIFY(pins && navigator && workspace);
        auto* pinnedModel = qobject_cast<PinnedTreeModel*>(pins->model());
        QVERIFY(pinnedModel);
        auto* model = navigator->model();
        QObject::disconnect(model, &NavigatorModel::childrenRequested, workspace->adapter(),
                            &EngineAdapter::loadMetadata);
        QVERIFY(model->addConnection(927, "Synthetic database"));
        const auto connection = model->index(0, 0);
        model->fetchMore(connection);
        QVERIFY(
            model->applyChildren(927, {}, model->pendingRequestToken(connection),
                                 {{"pg:database:1", "database", "database", "database", true}}));
        const auto database = model->index(0, 0, connection);
        model->fetchMore(database);
        QVERIFY(model->applyChildren(927, "pg:database:1", model->pendingRequestToken(database),
                                     {{"pg:schema:1", "public", "public", "schema", true}}));
        const auto schema = model->index(0, 0, database);
        model->fetchMore(schema);
        QVERIFY(model->applyChildren(
            927, "pg:schema:1", model->pendingRequestToken(schema),
            {{"pg:relation:1", "object_1", "public.object_1", "table", true}}));
        const auto table = model->index(0, 0, schema);
        model->fetchMore(table);
        NavigatorObject column{"column-id", "created_at", "public.object_1.created_at", "column",
                               false};
        column.databaseType = "timestamp with time zone";
        QVERIFY(model->applyChildren(
            927, "pg:relation:1", model->pendingRequestToken(table),
            {column,
             {"key-id", "object_1_pkey", "public.object_1.object_1_pkey", "key", false},
             {"index-id", "object_1_idx", "public.object_1.object_1_idx", "index", false}}));
        QTest::qWait(200);
        QVERIFY(pinnedModel->setResolved(PinStore::identityKey(tablePin), table));
        const auto root = pinnedModel->index(0, 0);
        {
            const QSignalBlocker blocker(pins);
            pins->expand(root);
        }
        pinnedModel->setStatus(PinStore::identityKey(tablePin), "Connected");
        QTRY_VERIFY(pins->isExpanded(root));
        QCOMPARE(pinnedModel->rowCount(root), 3);
        QVERIFY(!pins->isRowHidden(0, root));
        QTRY_VERIFY(pins->isRowHidden(1, root));
        QTRY_VERIFY(pins->isRowHidden(2, root));
        const auto pinnedColumn = pinnedModel->index(0, 0, root);
        QTRY_COMPARE(pins->visualRect(pinnedColumn).height(), pins->visualRect(root).height());
        QTRY_COMPARE(pins->verticalScrollBar()->maximum(), 0);
        pins->setFixedWidth(240);
        QCoreApplication::processEvents();
        const int wideHeight = pins->height();
        pins->setFixedWidth(150);
        QTRY_COMPARE(pins->height(), wideHeight);
        QTRY_COMPARE(pins->verticalScrollBar()->maximum(), 0);
        pins->setFixedWidth(300);
        QTRY_COMPARE(pins->height(), wideHeight);
        QTRY_COMPARE(pins->verticalScrollBar()->maximum(), 0);

        QVERIFY(pinnedModel->setResolved(PinStore::identityKey(schemaPin), schema));
        const auto pinnedSchema = pinnedModel->index(1, 0);
        const auto nestedTable = pinnedModel->index(0, 0, pinnedSchema);
        {
            const QSignalBlocker blocker(pins);
            pins->expand(pinnedSchema);
            pins->expand(nestedTable);
        }
        pinnedModel->setStatus(PinStore::identityKey(schemaPin), "Connected");
        QTRY_VERIFY(pins->isExpanded(nestedTable));
        QCOMPARE(pinnedModel->rowCount(nestedTable), 3);
        QVERIFY(!pins->isRowHidden(0, nestedTable));
        QTRY_VERIFY(pins->isRowHidden(1, nestedTable));
        QTRY_VERIFY(pins->isRowHidden(2, nestedTable));
        QTRY_COMPARE(pins->verticalScrollBar()->maximum(), 0);
        const auto lastColumn = pinnedModel->index(0, 0, nestedTable);
        QTRY_VERIFY(pins->visualRect(lastColumn).isValid());
        const int unusedHeight =
            pins->viewport()->height() - (pins->visualRect(lastColumn).bottom() + 1);
        QVERIFY2(
            unusedHeight <= design::spacing(design::Spacing::Two),
            qPrintable(
                QStringLiteral("Pinned tree leaves %1 px after its last row").arg(unusedHeight)));
    }

    void pinnedSectionIsHiddenWithoutPins() {
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        choscordb::MainWindow window(nullptr, storage.filePath("settings.sqlite"));
        window.show();
        auto* panel = window.findChild<QWidget*>("connectionsPanel");
        auto* section = window.findChild<QWidget*>("pinnedSection");
        auto* pins = window.findChild<QTreeView*>("pinnedList");
        auto* empty = window.findChild<QLabel*>("pinnedEmpty");
        auto* filter = window.findChild<QLineEdit*>("navigatorFilter");
        auto* scroll = window.findChild<QScrollArea*>("connectionsScroll");
        QVERIFY(panel && section && pins && empty && filter && scroll);
        auto* layout = qobject_cast<QVBoxLayout*>(panel->layout());
        QVERIFY(layout);
        QCOMPARE(scroll->widget(), panel);
        QCOMPARE(layout->itemAt(1)->widget(), section);
        QCOMPARE(layout->itemAt(2)->widget()->findChild<QLineEdit*>("navigatorFilter"), filter);
        QVERIFY(!section->isVisible());
        QTRY_COMPARE(layout->itemAt(2)->widget()->geometry().top() -
                         (layout->itemAt(0)->widget()->geometry().bottom() + 1),
                     16);
        QVERIFY(!section->accessibleName().isEmpty());
        QVERIFY(empty->text().contains("pin", Qt::CaseInsensitive));
        QCOMPARE(pins->model()->rowCount(), 0);
    }

    void restoredPinsScrollAndIgnoreObjectFilter() {
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        const auto path = storage.filePath("settings.sqlite");
        QList<choscordb::PinRecord> records;
        for (int number = 0; number < 31; ++number)
            records.append(pin(number));
        QVERIFY(savePins(path, records));
        choscordb::MainWindow window(nullptr, path);
        window.resize(960, 640);
        window.show();
        auto* pins = window.findChild<QTreeView*>("pinnedList");
        auto* scroll = window.findChild<QScrollArea*>("connectionsScroll");
        auto* section = window.findChild<QWidget*>("pinnedSection");
        auto* filter = window.findChild<QLineEdit*>("navigatorFilter");
        QVERIFY(pins && scroll && section && filter);
        QTRY_COMPARE(pins->model()->rowCount(), 31);
        QTRY_VERIFY(section->isVisible() && pins->isVisible());
        QVERIFY(!pins->accessibleName().isEmpty());
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QCOMPARE(pins->verticalScrollBar()->maximum(), 0);
        const auto first = pins->model()->index(0, 0);
        QCOMPARE(first.data().toString(), QStringLiteral("object_0"));
        QVERIFY(first.data(Qt::ToolTipRole).toString().contains("Saved profile"));
        scroll->verticalScrollBar()->setValue(0);
        const auto point = pins->viewport()->rect().center();
        QWheelEvent wheel(point, pins->viewport()->mapToGlobal(point), {}, {0, -120}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(pins->viewport(), &wheel);
        QTRY_VERIFY(scroll->verticalScrollBar()->value() > 0);
        filter->setText("different_object");
        QCOMPARE(pins->model()->rowCount(), 31);
        QCOMPARE(first.data().toString(), QStringLiteral("object_0"));
    }

    void captionsAndSectionsHaveRequestedSpacing() {
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        const auto path = storage.filePath("settings.sqlite");
        QVERIFY(savePins(path, {pin()}));
        for (const auto mode :
             {choscordb::design::ThemeMode::Light, choscordb::design::ThemeMode::Dark}) {
            for (const auto density :
                 {choscordb::design::Density::Compact, choscordb::design::Density::Comfortable}) {
                choscordb::MainWindow window(nullptr, path);
                window.resize(960, density == choscordb::design::Density::Compact ? 640 : 900);
                auto* theme = window.findChild<choscordb::design::ThemeManager*>();
                QVERIFY(theme);
                theme->setMode(mode);
                theme->setDensity(density);
                window.show();
                auto* workspace = window.findChild<choscordb::QueryWorkspace*>();
                auto* saved = window.findChild<QListWidget*>("savedConnections");
                QVERIFY(workspace && saved);
                if (saved->count() == 0) {
                    choscordb::SavedProfile profile;
                    profile.id = QStringLiteral("saved-profile");
                    profile.name = QStringLiteral("Saved profile");
                    profile.path = storage.filePath("database.sqlite");
                    workspace->adapter()->saveProfile(profile, 8101);
                }
                QTRY_COMPARE(saved->count(), 1);
                auto* savedEmpty = window.findChild<QLabel*>("sidebarConnectionsEmpty");
                QVERIFY(savedEmpty);
                QTRY_VERIFY(!savedEmpty->isVisible());
                QTRY_VERIFY(saved->geometry().top() < 60);
                auto* section = window.findChild<QWidget*>("pinnedSection");
                auto* pins = window.findChild<QTreeView*>("pinnedList");
                auto* filter = window.findChild<QLineEdit*>("navigatorFilter");
                QVERIFY(section && pins && filter);
                QTRY_COMPARE(pins->model()->rowCount(), 1);
                QTRY_VERIFY(section->isVisible() && pins->isVisible());
                const auto* caption = section->findChild<choscordb::design::Text*>();
                const auto* savedCaption =
                    window.findChild<choscordb::design::Text*>("navigatorTitle");
                auto* objectSection = filter->parentWidget();
                const auto* objectCaption = objectSection->findChild<choscordb::design::Text*>();
                QVERIFY(caption && savedCaption && objectCaption);
                const auto row = pins->visualRect(pins->model()->index(0, 0));
                QVERIFY(row.height() > 0);
                const int pinGap = pins->viewport()->mapTo(section, row.topLeft()).y() -
                                   caption->mapTo(section, QPoint(0, caption->height())).y();
                auto* connectionSection = savedCaption->parentWidget();
                QCOMPARE(section->geometry().top() - (connectionSection->geometry().bottom() + 1),
                         16);
                QCOMPARE(pinGap, 1);
                QCOMPARE(caption->height(), caption->sizeHint().height());
                QCOMPARE(objectSection->geometry().top() - (section->geometry().bottom() + 1), 16);
            }
        }
    }

    void wrappedObjectEmptyMessageIsFullyVisibleBelowPins() {
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        const auto path = storage.filePath("settings.sqlite");
        QVERIFY(savePins(path, {pin(1), pin(2)}));
        choscordb::MainWindow window(nullptr, path);
        window.resize(960, 640);
        window.show();
        auto* navigator = window.findChild<choscordb::NavigatorController*>();
        auto* tree = window.findChild<QTreeView*>("databaseNavigator");
        auto* empty = window.findChild<QLabel*>("sidebarObjectsEmpty");
        auto* pins = window.findChild<QTreeView*>("pinnedList");
        auto* scroll = window.findChild<QScrollArea*>("connectionsScroll");
        QVERIFY(navigator && tree && empty && pins && scroll);
        QTRY_COMPARE(pins->model()->rowCount(), 2);
        QVERIFY(navigator->model()->addConnection(777, QStringLiteral("Sample connection")));
        navigator->setVisibleConnections({777});
        QTRY_COMPARE(tree->model()->rowCount(), 1);
        navigator->setVisibleConnections({});
        QTRY_COMPARE(tree->model()->rowCount(), 0);
        QTRY_VERIFY(tree->height() <= 8);
        scroll->setFixedWidth(200);
        scroll->setFixedHeight(220);
        QTRY_VERIFY(empty->isVisible() && empty->width() > 0 && empty->width() < 200);
        QVERIFY(empty->text().contains("No database selected"));
        QVERIFY(empty->height() >= empty->heightForWidth(empty->width()));
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
        for (quint64 id = 1; id <= 60; ++id) {
            QVERIFY(navigator->model()->addConnection(id, QStringLiteral("Connection %1").arg(id)));
            ids.append(id);
        }
        navigator->setVisibleConnections(ids);
        QTRY_COMPARE(tree->model()->rowCount(), 60);
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QCOMPARE(tree->verticalScrollBar()->maximum(), 0);
        scroll->verticalScrollBar()->setValue(0);
        tree->setCurrentIndex(tree->model()->index(59, 0));
        QTRY_VERIFY(scroll->verticalScrollBar()->value() > 0);
    }

    void savedConnectionsUseOnlyTheSharedVerticalScroll() {
        using namespace choscordb;
        QTemporaryDir storage;
        QVERIFY(storage.isValid());
        MainWindow window(nullptr, storage.filePath("settings.sqlite"));
        window.resize(960, 640);
        window.show();
        auto* workspace = window.findChild<QueryWorkspace*>();
        auto* saved = window.findChild<QListWidget*>("savedConnections");
        auto* scroll = window.findChild<QScrollArea*>("connectionsScroll");
        auto* theme = window.findChild<design::ThemeManager*>();
        QVERIFY(workspace && saved && scroll && theme);
        SavedProfile profile;
        profile.id = "profile-0";
        profile.name = "Connection 0";
        profile.path = ":memory:";
        workspace->adapter()->saveProfile(profile, 9100);
        QTRY_COMPARE(saved->count(), 1);
        QTRY_COMPARE(saved->verticalScrollBar()->maximum(), 0);
        QTRY_COMPARE(scroll->verticalScrollBar()->maximum(), 0);
        QTRY_VERIFY(saved->viewport()->rect().contains(saved->visualItemRect(saved->item(0))));

        for (int number = 1; number < 32; ++number) {
            profile.id = QStringLiteral("profile-%1").arg(number);
            profile.name = QStringLiteral("Connection %1").arg(number);
            workspace->adapter()->saveProfile(profile, 9100 + number);
        }
        QTRY_COMPARE(saved->count(), 32);
        for (const auto mode : {design::ThemeMode::Light, design::ThemeMode::Dark}) {
            for (const auto density : {design::Density::Compact, design::Density::Comfortable}) {
                theme->setMode(mode);
                theme->setDensity(density);
                QTRY_COMPARE(saved->verticalScrollBar()->maximum(), 0);
                QTRY_VERIFY(saved->viewport()->rect().contains(
                    saved->visualItemRect(saved->item(saved->count() - 1))));
                QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
                QCOMPARE(saved->horizontalScrollBar()->maximum(), 0);
                QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
                int visibleBars = 0;
                for (const auto* bar : scroll->findChildren<QScrollBar*>())
                    visibleBars += bar->isVisible();
                QCOMPARE(visibleBars, 1);

                scroll->verticalScrollBar()->setValue(0);
                const auto point = saved->visualItemRect(saved->item(0)).center();
                QWheelEvent wheel(point, saved->viewport()->mapToGlobal(point), {}, {0, -120},
                                  Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(saved->viewport(), &wheel);
                QTRY_VERIFY(scroll->verticalScrollBar()->value() > 0);
                QCOMPARE(saved->verticalScrollBar()->value(), 0);
            }
        }
        saved->setCurrentRow(0);
        scroll->verticalScrollBar()->setValue(0);
        saved->setFocus();
        QTest::keyClick(saved, Qt::Key_End);
        QTRY_COMPARE(saved->currentRow(), 31);
        const auto currentRowIsVisible = [saved, scroll] {
            const auto row = saved->visualItemRect(saved->currentItem());
            return scroll->viewport()->rect().contains(
                QRect(saved->viewport()->mapTo(scroll->viewport(), row.topLeft()), row.size()));
        };
        QTRY_VERIFY(currentRowIsVisible());
        QTest::keyClick(saved, Qt::Key_Home);
        QTRY_COMPARE(saved->currentRow(), 0);
        QTRY_VERIFY(currentRowIsVisible());
        QSignalSpy opened(workspace, &QueryWorkspace::connectionReady);
        saved->setCurrentRow(1, QItemSelectionModel::NoUpdate);
        QTest::keyClick(saved, Qt::Key_Space);
        QTRY_COMPARE(opened.count(), 1);
        saved->setCurrentRow(5, QItemSelectionModel::NoUpdate);
        QTest::keyClick(saved, Qt::Key_Space);
        QTRY_COMPARE(opened.count(), 2);
        QTRY_COMPARE(saved->selectedItems().size(), 2);
        saved->setCurrentRow(0, QItemSelectionModel::NoUpdate);
        const auto selected = saved->selectedItems();
        for (const Qt::KeyboardModifiers modifiers :
             {Qt::KeyboardModifiers{}, Qt::KeyboardModifiers{Qt::ShiftModifier},
              Qt::KeyboardModifiers{Qt::ControlModifier},
              Qt::KeyboardModifiers{Qt::ShiftModifier | Qt::ControlModifier}}) {
            const auto firstRow = saved->visualItemRect(saved->item(0));
            QTest::keyClick(saved, Qt::Key_PageDown, modifiers);
            QVERIFY(saved->currentRow() > 0 && saved->currentRow() < 31);
            const int pageDistance =
                saved->visualItemRect(saved->currentItem()).center().y() - firstRow.center().y();
            QVERIFY(pageDistance <= scroll->viewport()->height());
            QVERIFY(pageDistance >= scroll->viewport()->height() - 2 * firstRow.height());
            QCOMPARE(saved->selectedItems(), selected);
            QTRY_VERIFY(currentRowIsVisible());
            QTest::keyClick(saved, Qt::Key_PageUp, modifiers);
            QTRY_COMPARE(saved->currentRow(), 0);
            QCOMPARE(saved->selectedItems(), selected);
            QTRY_VERIFY(currentRowIsVisible());
        }
        for (int number = 1; number < 32; ++number)
            workspace->adapter()->deleteProfile(QStringLiteral("profile-%1").arg(number),
                                                9200 + number);
        QTRY_COMPARE(saved->count(), 1);
        QTRY_COMPARE(scroll->verticalScrollBar()->maximum(), 0);
        QTRY_VERIFY(saved->viewport()->rect().contains(saved->visualItemRect(saved->item(0))));
        workspace->adapter()->deleteProfile("profile-0", 9300);
        QTRY_COMPARE(saved->count(), 0);
        QTRY_COMPARE(saved->height(), 0);
        QTRY_VERIFY(window.findChild<QLabel*>("sidebarConnectionsEmpty")->isVisible());
    }
};

QTEST_MAIN(PinningSidebarTest)
#include "pinning_sidebar_test.moc"
