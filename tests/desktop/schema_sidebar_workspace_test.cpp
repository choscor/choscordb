#include "app/main_window.h"
#include "app/main_window_widgets.h"
#include "app/navigator_controller.h"
#include "app/object_explorer.h"
#include "app/query_workspace.h"
#include "app/workspace_recovery.h"
#include "bridge/engine_adapter.h"
#include "design_system/fonts/fonts.h"
#include "design_system/icons.h"
#include "design_system/theme_manager.h"
#include "models/navigator_model.h"
#include <QAction>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QMenu>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QStyleOptionViewItem>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTreeView>
#include <QtTest>
#include <algorithm>

class SchemaSidebarWorkspaceTest final : public QObject {
    Q_OBJECT
  private slots:
    void tableAndViewIconsFollowTheNavigatorAndTabTheme();
    void columnRowsShowDeclaredTypesWithoutLosingTheirNames();
};

void SchemaSidebarWorkspaceTest::tableAndViewIconsFollowTheNavigatorAndTabTheme() {
    using namespace choscordb;
    QTemporaryDir storage;
    MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    auto* recovery = window.findChild<WorkspaceRecoveryController*>();
    QVERIFY(recovery);
    QTRY_VERIFY(recovery->isReady());
    QCoreApplication::processEvents();
    auto* tree = window.findChild<QTreeView*>("databaseNavigator");
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    auto* theme = window.findChild<design::ThemeManager*>();
    auto* navigator = window.findChild<NavigatorController*>();
    auto* workspace = window.findChild<QueryWorkspace*>();
    QVERIFY(tree && tabs && theme && navigator && workspace);
    auto* delegate = static_cast<main_window_detail::NavigatorIconDelegate*>(tree->itemDelegate());
    QVERIFY(delegate);
    auto* model = navigator->model();
    QObject::disconnect(model, &NavigatorModel::childrenRequested, workspace->adapter(),
                        &EngineAdapter::loadMetadata);
    navigator->addConnection(928, "Synthetic database");
    navigator->setSelectedConnection(928);
    const auto root = model->index(0, 0);
    model->fetchMore(root);
    QTRY_VERIFY(model->pendingRequestToken(root) != 0);
    QVERIFY(model->applyChildren(928, {}, model->pendingRequestToken(root),
                                 {{"table", "orders", "main.orders", "table", false},
                                  {"view", "summary", "main.summary", "view", false},
                                  {"index", "orders_idx", "main.orders_idx", "index", false},
                                  {"function", "rebuild", "main.rebuild", "function", false}}));
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(tree->model());
    QVERIFY(proxy);
    const auto visibleRoot = proxy->mapFromSource(root);
    tree->expand(visibleRoot);
    QCOMPARE(proxy->rowCount(visibleRoot), 4);
    const auto firstRow = tree->visualRect(proxy->index(0, 0, visibleRoot));
    const auto secondRow = tree->visualRect(proxy->index(1, 0, visibleRoot));
    QVERIFY(firstRow.isValid() && secondRow.isValid());
    QCOMPARE(secondRow.top(), firstRow.bottom() + 1);
    QVERIFY2(firstRow.height() == choscordb::design::dimension(choscordb::design::Dimension::Row),
             qPrintable(QStringLiteral("Navigation row is %1 px high").arg(firstRow.height())));
    const auto checkRows = [&] {
        const auto color = theme->resolvedTheme().colors.fgMuted;
        for (int row = 0; row < 4; ++row) {
            const auto index = proxy->index(row, 0, visibleRoot);
            QCOMPARE(index.data(NavigatorModel::KindRole).toString(),
                     QStringList({"table", "view", "index", "function"}).at(row));
            QStyleOptionViewItem option;
            option.widget = tree;
            option.rect = tree->visualRect(index);
            QVERIFY(option.rect.isValid());
            delegate->initStyleOption(&option, index);
            const auto expected = row == 0   ? design::Icon::Grid2x2
                                  : row == 1 ? design::Icon::Eye
                                  : row == 2 ? design::Icon::Key
                                             : design::Icon::File;
            const int size = objectIconSize();
            QCOMPARE(option.decorationSize, QSize(size, size));
            QCOMPARE(option.icon.pixmap(size, size).toImage(),
                     design::themedIcon(expected, color, size).pixmap(size, size).toImage());
        }
    };
    checkRows();
    emit window.objectContextSelected(11, "main.orders", "orders", "table");
    emit window.objectContextSelected(11, "main.summary", "summary", "view");
    emit window.objectContextSelected(11, "main.rebuild", "rebuild", "function");
    QCOMPARE(tabs->count(), 3);
    const auto checkTabs = [&] {
        const auto color = theme->resolvedTheme().colors.fgMuted;
        for (int row = 0; row < tabs->count(); ++row) {
            const auto expected = row == 0   ? design::Icon::Grid2x2
                                  : row == 1 ? design::Icon::Eye
                                             : design::Icon::File;
            const int size = objectIconSize();
            QCOMPARE(tabs->tabIcon(row).pixmap(size, size).toImage(),
                     design::themedIcon(expected, color, size).pixmap(size, size).toImage());
        }
    };
    checkTabs();
    theme->setMode(design::ThemeMode::Dark);
    checkRows();
    checkTabs();
}

void SchemaSidebarWorkspaceTest::columnRowsShowDeclaredTypesWithoutLosingTheirNames() {
    using namespace choscordb;
    QTemporaryDir storage;
    MainWindow window(nullptr, storage.filePath("settings.sqlite"));
    window.show();
    auto* recovery = window.findChild<WorkspaceRecoveryController*>();
    QVERIFY(recovery);
    QTRY_VERIFY(recovery->isReady());
    QCoreApplication::processEvents();
    auto* tree = window.findChild<QTreeView*>("databaseNavigator");
    auto* navigator = window.findChild<NavigatorController*>();
    auto* theme = window.findChild<design::ThemeManager*>();
    auto* workspace = window.findChild<QueryWorkspace*>();
    QVERIFY(tree && navigator && theme && workspace);
    auto* model = navigator->model();
    QObject::disconnect(model, &NavigatorModel::childrenRequested, workspace->adapter(),
                        &EngineAdapter::loadMetadata);
    navigator->addConnection(927, "Synthetic database");
    navigator->setSelectedConnection(927);
    const auto root = model->index(0, 0);
    model->fetchMore(root);
    QTRY_VERIFY(model->pendingRequestToken(root) != 0);
    QVERIFY(model->applyChildren(927, {}, model->pendingRequestToken(root),
                                 {{"table", "orders", "main.orders", "table", true}}));
    const auto table = model->index(0, 0, root);
    model->fetchMore(table);
    QTRY_VERIFY(model->pendingRequestToken(table) != 0);
    NavigatorObject shortName{"short", "a", "main.orders.a", "column", false};
    shortName.databaseType = "VARCHAR(255)";
    NavigatorObject longName{"long", "extremely_long_customer_record_identifier",
                             "main.orders.extremely_long_customer_record_identifier", "column",
                             false};
    longName.databaseType = "VARCHAR(255)";
    NavigatorObject longType{"long-type", "b", "main.orders.b", "column", false};
    longType.databaseType = "timestamp(6) with time zone and an extended declared suffix";
    NavigatorObject noType{"blank", "untyped", "main.orders.untyped", "column", false};
    QVERIFY(model->applyChildren(927, "table", model->pendingRequestToken(table),
                                 {shortName, longName, longType, noType}));
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(tree->model());
    QVERIFY(proxy);
    tree->expand(proxy->mapFromSource(root));
    tree->expand(proxy->mapFromSource(table));
    const auto visibleTable = proxy->mapFromSource(table);
    QCOMPARE(proxy->rowCount(visibleTable), 4);
    const QPersistentModelIndex shortRow = proxy->index(0, 0, visibleTable);
    const QPersistentModelIndex longRow = proxy->index(1, 0, visibleTable);
    const QPersistentModelIndex longTypeRow = proxy->index(2, 0, visibleTable);
    const QPersistentModelIndex blankRow = proxy->index(3, 0, visibleTable);
    QCOMPARE(longRow.data(Qt::DisplayRole).toString(), longName.name);
    QCOMPARE(longRow.data(NavigatorModel::ObjectIdRole).toString(), longName.id);
    QCOMPARE(longRow.data(Qt::ToolTipRole).toString(),
             QString("extremely_long_customer_record_identifier — VARCHAR(255)"));
    QCOMPARE(longRow.data(Qt::AccessibleDescriptionRole).toString(),
             longRow.data(Qt::ToolTipRole).toString());
    QCOMPARE(blankRow.data(NavigatorModel::DatabaseTypeRole).toString(), QString());
    QCOMPARE(longTypeRow.data(Qt::ToolTipRole).toString(),
             QString("b — timestamp(6) with time zone and an extended declared suffix"));

    const auto verifyPaint = [&] {
        tree->setFixedWidth(220);
        QCoreApplication::processEvents();
        QTRY_COMPARE(tree->verticalScrollBar()->maximum(), 0);
        const auto shortRect = tree->visualRect(shortRow);
        const auto longRect = tree->visualRect(longRow);
        const auto longTypeRect = tree->visualRect(longTypeRow);
        const auto blankRect = tree->visualRect(blankRow);
        QVERIFY(shortRect.isValid() && longRect.isValid() && longTypeRect.isValid() &&
                blankRect.isValid());
        QCOMPARE(shortRect.height(), blankRect.height());
        QCOMPARE(longRect.height(), blankRect.height());
        QCOMPARE(longTypeRect.height(), blankRect.height());
        const QFontMetrics detailMetrics(
            design::resolveTypography(design::TypographyRole::NavigationDetail));
        QVERIFY(detailMetrics.horizontalAdvance(longType.databaseType) > longTypeRect.width());
        // Paint the same delegate at high resolution so small antialiased
        // glyphs have interior pixels for the exact semantic-color assertion.
        // Cropping a logical viewport rectangle also mismatches high-DPI grabs.
        QImage detailStrip(longTypeRect.size() * 4, QImage::Format_ARGB32_Premultiplied);
        detailStrip.setDevicePixelRatio(4);
        detailStrip.fill(Qt::transparent);
        QStyleOptionViewItem paintOption;
        paintOption.widget = tree;
        paintOption.rect = QRect(QPoint{}, longTypeRect.size());
        QPainter painter(&detailStrip);
        tree->itemDelegate()->paint(&painter, paintOption, longTypeRow);
        painter.end();
        const auto muted = theme->resolvedTheme().colors.fgMuted;
        int mutedPixels = 0;
        for (int y = 0; y < detailStrip.height(); ++y)
            for (int x = 0; x < detailStrip.width(); ++x)
                mutedPixels += detailStrip.pixelColor(x, y) == muted;
        QVERIFY(mutedPixels > 0);
        QVERIFY(blankRect.bottom() < tree->viewport()->height());
        QStyleOptionViewItem option;
        option.widget = tree;
        static_cast<main_window_detail::NavigatorIconDelegate*>(tree->itemDelegate())
            ->initStyleOption(&option, longRow);
        QVERIFY(option.icon.isNull());
        QVERIFY(!(option.features & QStyleOptionViewItem::HasDecoration));
    };
    verifyPaint();
    theme->setMode(design::ThemeMode::Dark);
    verifyPaint();

    const int wideHeight = tree->height();
    tree->setFixedWidth(150);
    QTRY_COMPARE(tree->height(), wideHeight);
    QTRY_COMPARE(tree->verticalScrollBar()->maximum(), 0);
    tree->setFixedWidth(300);
    QTRY_COMPARE(tree->height(), wideHeight);
    QTRY_COMPARE(tree->verticalScrollBar()->maximum(), 0);

    tree->setCurrentIndex(longRow);
    QCOMPARE(tree->currentIndex(), QModelIndex(longRow));
    auto* panes = window.findChild<QTabBar*>("objectTabs");
    auto* tabs = window.findChild<QTabWidget*>("editorTabs");
    QVERIFY(panes && tabs);
    QCOMPARE(panes->currentIndex(), 0);
    QVERIFY(qobject_cast<ObjectExplorer*>(tabs->currentWidget()));
    QMenu menu;
    navigator->populateContextMenu(&menu, proxy->mapToSource(longRow));
    auto* pin = menu.findChild<QAction*>("pinObject");
    QVERIFY(pin);
    QCOMPARE(pin->text(), QString("Pin"));
}

QTEST_MAIN(SchemaSidebarWorkspaceTest)
#include "schema_sidebar_workspace_test.moc"
