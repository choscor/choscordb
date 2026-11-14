#include "design_system/button/button.h"
#include "design_system/button_group/button_group.h"
#include "design_system/column_row/column_row.h"
#include "design_system/control_style.h"
#include "design_system/history_row/history_row.h"
#include "design_system/icons.h"
#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/theme_manager.h"
#include "design_system/tree/navigation_tree_view.h"
#include <QCursor>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPainter>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QStyleOption>
#include <QTabBar>
#include <QTreeView>
#include <QTreeWidget>
#include <QtTest>

namespace {
bool withinChannels(const QColor& color, const QColor& first, const QColor& second) {
    const auto inside = [](int value, int a, int b) {
        return value >= std::min(a, b) - 1 && value <= std::max(a, b) + 1;
    };
    return inside(color.red(), first.red(), second.red()) &&
           inside(color.green(), first.green(), second.green()) &&
           inside(color.blue(), first.blue(), second.blue());
}
QString selectionOutline(const QImage& image, int x, const QColor& fill) {
    for (const QColor backdrop : {QColor(Qt::white), QColor(Qt::black)}) {
        QImage flat(image.size(), QImage::Format_RGB32);
        flat.fill(backdrop);
        flat.setDevicePixelRatio(image.devicePixelRatio());
        QPainter painter(&flat);
        painter.drawImage(0, 0, image);
        painter.end();
        const auto background = flat.pixelColor(x, flat.height() - 1);
        const auto opaqueFill = choscordb::design::composite(fill, backdrop);
        for (int y = 0; y < flat.height(); ++y)
            if (!withinChannels(flat.pixelColor(x, y), opaqueFill, background))
                return QString("outline pixel %1 at y=%2 over %3")
                    .arg(flat.pixelColor(x, y).name())
                    .arg(y)
                    .arg(backdrop.name());
    }
    return {};
}
}

class ComponentsTest final : public QObject {
    Q_OBJECT
  private slots:
    void paneTabsAreSegmentedAndDocumentTabsAreUnderlined() {
        using namespace choscordb::design;
        QWidget root;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(root);
        QTabBar tabs(&root);
        tabs.setExpanding(false);
        tabs.addTab("Columns");
        tabs.addTab("Indexes");
        tabs.setCurrentIndex(1);
        tabs.resize(250, 35);
        root.resize(270, 80);
        root.show();
        QCoreApplication::processEvents();
        const auto rect = tabs.tabRect(1);
        auto capture = tabs.grab();
        auto image = capture.toImage();
        const auto scale = capture.devicePixelRatio();
        const auto at = [&](int x, int y) {
            return image.pixelColor(qRound(x * scale), qRound(y * scale));
        };
        const auto colors = resolvedThemeForWidget(tabs).colors;
        QCOMPARE(at(rect.left() + 8, rect.center().y()), colors.base);
        QCOMPARE(at(rect.center().x(), rect.top()), colors.recessed);
        QCOMPARE(at(rect.left(), rect.top() + spacing(Spacing::Half)), colors.recessed);
        tabs.setProperty("designTabVariant", "document");
        tabs.style()->unpolish(&tabs);
        tabs.style()->polish(&tabs);
        capture = tabs.grab();
        image = capture.toImage();
        const auto documentRect = tabs.tabRect(1);
        QCOMPARE(at(documentRect.center().x(), documentRect.top()), colors.base);
        QCOMPARE(at(documentRect.left() + 8, documentRect.top()), colors.base);
        QCOMPARE(at(documentRect.left(), documentRect.top()), colors.base);
        QCOMPARE(at(documentRect.center().x(), documentRect.bottom()), colors.brand);
    }
    void sidebarTreeSelectionHasRoundedGrayFill() {
        using namespace choscordb::design;
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            QWidget root;
            root.setProperty("designSurface", "sidebar");
            root.setAttribute(Qt::WA_StyledBackground);
            ThemeManager theme;
            theme.setMode(mode);
            theme.applyTo(root);
            ControlStyle style;
            NavigationTreeView tree(&root);
            tree.setStyle(&style);
            tree.setObjectName("databaseNavigator");
            QStandardItemModel model;
            model.appendRow(new QStandardItem("Tables"));
            model.appendRow(new QStandardItem("Views"));
            for (int index = 0; index < 20; ++index)
                model.appendRow(new QStandardItem(QString("Object %1").arg(index)));
            tree.setModel(&model);
            tree.setHeaderHidden(true);
            tree.resize(220, 80);
            root.resize(230, 100);
            root.show();
            tree.setCurrentIndex(model.index(0, 0));
            tree.selectionModel()->select(model.index(0, 0), QItemSelectionModel::ClearAndSelect |
                                                                 QItemSelectionModel::Rows);
            QCoreApplication::processEvents();
            const auto rect = tree.visualRect(model.index(0, 0));
            const auto image = tree.viewport()->grab().toImage();
            const auto colors = resolvedThemeForWidget(tree).colors;
            QCOMPARE(image.pixelColor(rect.right() - 8, rect.center().y()), colors.tint);
            QCOMPARE(image.pixelColor(6, rect.center().y()), colors.tint);
            QCOMPARE(image.pixelColor(10, rect.top() + 3), colors.tint);
            const auto outline = selectionOutline(
                image, qRound((rect.right() - 8) * image.devicePixelRatio()), colors.tint);
            QVERIFY2(outline.isEmpty(), qPrintable(outline));
            QVERIFY(image.pixelColor(6, rect.top() + 1) != colors.elevated);
            const auto hoverRect = tree.visualRect(model.index(1, 0));
            QVERIFY(tree.viewport()->hasMouseTracking());
            QTest::mouseMove(tree.viewport(), QPoint(1, 1));
            QTest::mouseMove(tree.viewport(), hoverRect.center());
            QCoreApplication::processEvents();
            const auto hovered = tree.viewport()->grab().toImage();
            QCOMPARE(hovered.pixelColor(hoverRect.right() - 8, hoverRect.center().y()),
                     composite(colors.tint, colors.elevated));
            tree.verticalScrollBar()->setValue(tree.verticalScrollBar()->value() + 2);
            QCoreApplication::processEvents();
            const auto underPointer = tree.indexAt(hoverRect.center());
            QVERIFY(underPointer.isValid());
            QVERIFY(underPointer != model.index(1, 0));
            const auto scrolled = tree.viewport()->grab().toImage();
            QCOMPARE(scrolled.pixelColor(tree.visualRect(underPointer).right() - 8,
                                         hoverRect.center().y()),
                     composite(colors.tint, colors.elevated));
        }
    }
    void sidebarTreeTopLevelBranchSitsInsideHighlight() {
        using namespace choscordb::design;
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            QWidget root;
            root.setProperty("designSurface", "sidebar");
            root.setAttribute(Qt::WA_StyledBackground);
            ThemeManager theme;
            theme.setMode(mode);
            theme.applyTo(root);
            NavigationTreeView tree(&root);
            QStandardItemModel model;
            auto* parent = new QStandardItem("Connection");
            parent->appendRow(new QStandardItem("main"));
            model.appendRow(parent);
            tree.setModel(&model);
            tree.setHeaderHidden(true);
            tree.resize(220, 80);
            root.resize(230, 100);
            root.show();
            tree.expandAll();
            tree.selectionModel()->select(model.index(0, 0), QItemSelectionModel::ClearAndSelect |
                                                                 QItemSelectionModel::Rows);
            QCoreApplication::processEvents();
            const auto image = tree.grab().toImage();
            const auto colors = resolvedThemeForWidget(tree).colors;
            const auto rect = tree.visualRect(model.index(0, 0));
            const int y = tree.viewport()->mapTo(&tree, rect.center()).y();
            const auto pixel = [&](int x, int row) {
                return image.pixelColor(QPoint(x, row) * image.devicePixelRatio());
            };
            int highlightLeft = 0;
            while (highlightLeft < rect.left() && pixel(highlightLeft, y - 5) != colors.tint)
                ++highlightLeft;
            QVERIFY2(highlightLeft < rect.left(), "the selected row has no highlight");
            int inkLeft = 0;
            const auto ink = [&](int x) {
                for (int row = y - 3; row <= y + 3; ++row)
                    if (pixel(x, row) != colors.tint && pixel(x, row) != colors.elevated)
                        return true;
                return false;
            };
            while (inkLeft < rect.left() && !ink(inkLeft))
                ++inkLeft;
            QVERIFY2(inkLeft < rect.left(), "the expand chevron was not painted");
            QVERIFY2(highlightLeft >= spacing(Spacing::One), "the highlight lost its leading gap");
            QVERIFY2(inkLeft >= highlightLeft + spacing(Spacing::Half),
                     qPrintable(QString("chevron starts at %1, highlight at %2")
                                    .arg(inkLeft)
                                    .arg(highlightLeft)));
            tree.resize(260, 90);
            theme.applyTo(root);
            QCoreApplication::processEvents();
            QCOMPARE(tree.viewport()->x(), spacing(Spacing::One));
        }
    }
    void sidebarTreeColumnDetailSitsInsideHighlight() {
        using namespace choscordb::design;
        constexpr int detailRole = Qt::UserRole + 1;
        QWidget root;
        root.setProperty("designSurface", "sidebar");
        root.setAttribute(Qt::WA_StyledBackground);
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(root);
        NavigationTreeView tree(&root);
        tree.setItemDelegate(new ColumnRowDelegate(detailRole, &tree));
        QStandardItemModel model;
        auto* column = new QStandardItem("unit_price");
        column->setData("NUMERIC", detailRole);
        model.appendRow(column);
        tree.setModel(&model);
        tree.setHeaderHidden(true);
        tree.resize(220, 60);
        root.resize(230, 70);
        root.show();
        tree.selectionModel()->select(model.index(0, 0), QItemSelectionModel::ClearAndSelect |
                                                             QItemSelectionModel::Rows);
        QCoreApplication::processEvents();
        const auto image = tree.viewport()->grab().toImage();
        const auto colors = resolvedThemeForWidget(tree).colors;
        const auto rect = tree.visualRect(model.index(0, 0));
        const auto pixel = [&](int x, int row) {
            return image.pixelColor(QPoint(x, row) * image.devicePixelRatio());
        };
        int highlightRight = tree.viewport()->width() - 1;
        while (highlightRight > rect.center().x() &&
               pixel(highlightRight, rect.center().y() - 5) != colors.tint)
            --highlightRight;
        int inkRight = highlightRight;
        const auto ink = [&](int x) {
            for (int row = rect.center().y() - 3; row <= rect.center().y() + 3; ++row)
                if (pixel(x, row) != colors.tint && pixel(x, row) != colors.elevated)
                    return true;
            return false;
        };
        while (inkRight > rect.center().x() && !ink(inkRight))
            --inkRight;
        QVERIFY2(inkRight > rect.center().x(), "the column detail was not painted");
        QVERIFY2(
            inkRight <= highlightRight - spacing(Spacing::Half),
            qPrintable(
                QString("detail ends at %1, highlight at %2").arg(inkRight).arg(highlightRight)));
        bool fgDetail = false;
        bool mutedDetail = false;
        for (int x = inkRight - 40; x <= inkRight; ++x)
            for (int row = rect.top(); row <= rect.bottom(); ++row) {
                fgDetail |= pixel(x, row) == colors.textDefault;
                mutedDetail |= pixel(x, row) == colors.textSubtle;
            }
        QVERIFY(fgDetail);
        QVERIFY(!mutedDetail);
    }
    void sidebarTreeLeaveFromBlankSpaceDoesNotLeaveStaleHover() {
        using namespace choscordb::design;
        QWidget root;
        root.setProperty("designSurface", "sidebar");
        root.setAttribute(Qt::WA_StyledBackground);
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(root);
        NavigationTreeView tree(&root);
        tree.setObjectName("databaseNavigator");
        QStandardItemModel model;
        model.appendRow(new QStandardItem("First"));
        tree.setModel(&model);
        tree.setHeaderHidden(true);
        tree.resize(220, 80);
        root.resize(230, 100);
        root.show();
        QCoreApplication::processEvents();
        const QPoint blank(15, tree.viewport()->height() - 5);
        QVERIFY(!tree.indexAt(blank).isValid());
        QTest::mouseMove(tree.viewport(), blank);
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(tree.viewport(), &leave);
        for (int index = 0; index < 20; ++index)
            model.appendRow(new QStandardItem(QString("Later %1").arg(index)));
        QCoreApplication::processEvents();
        tree.verticalScrollBar()->setValue(1);
        QCoreApplication::processEvents();
        const auto row = tree.indexAt(blank);
        QVERIFY(row.isValid());
        const auto image = root.grab().toImage();
        QCOMPARE(image.pixelColor(tree.viewport()->mapTo(
                                      &root, QPoint(tree.visualRect(row).right() - 8, blank.y())) *
                                  image.devicePixelRatio()),
                 resolvedThemeForWidget(tree).colors.elevated);
    }
    void sidebarSavedFilesSelectionUsesSidebarSelection() {
        using namespace choscordb::design;
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            QWidget root;
            root.setProperty("designSurface", "sidebar");
            root.setAttribute(Qt::WA_StyledBackground);
            ThemeManager theme;
            theme.setMode(mode);
            theme.applyTo(root);
            ControlStyle style;
            QTreeWidget tree(&root);
            tree.setStyle(&style);
            tree.setObjectName("sidebarSavedFiles");
            tree.setProperty("designSurface", "sidebar");
            tree.setHeaderHidden(true);
            auto* item = new QTreeWidgetItem(&tree, {"Saved query"});
            tree.move(10, 10);
            tree.resize(220, 80);
            root.resize(240, 100);
            root.show();
            tree.setCurrentItem(item);
            tree.selectionModel()->select(tree.indexFromItem(item),
                                          QItemSelectionModel::ClearAndSelect |
                                              QItemSelectionModel::Rows);
            QCoreApplication::processEvents();
            const auto rect = tree.visualItemRect(item);
            const auto image = tree.viewport()->grab().toImage();
            QCOMPARE(image.pixelColor(rect.right() - 8, rect.center().y()),
                     resolvedThemeForWidget(tree).colors.tint);
            auto* other = new QTreeWidgetItem(&tree, {"Other query"});
            const auto otherRect = tree.visualItemRect(other);
            QTest::mouseMove(tree.viewport(), QPoint(1, tree.viewport()->height() - 2));
            QTest::mouseMove(tree.viewport(), otherRect.center());
            QCoreApplication::processEvents();
            QTRY_COMPARE_WITH_TIMEOUT(tree.viewport()->grab().toImage().pixelColor(
                                          otherRect.right() - 8, otherRect.center().y()),
                                      composite(resolvedThemeForWidget(tree).colors.tint,
                                                resolvedThemeForWidget(tree).colors.elevated),
                                      1000);
        }
    }
    void selectedConnectionRowUsesNeutralColors() {
        using namespace choscordb::design;
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            QWidget root;
            ThemeManager theme;
            theme.setMode(mode);
            theme.applyTo(root);
            QListWidget list(&root);
            list.setObjectName("savedConnections");
            list.setProperty("designSurface", "sidebar");
            list.setItemDelegate(new NavigationProfileDelegate(&list));
            auto* item = new QListWidgetItem("Example Postgres\nPostgreSQL", &list);
            item->setData(NavigationProfileDelegate::DriverRole, "postgres");
            list.resize(240, 60);
            root.resize(250, 70);
            root.show();
            list.setCurrentItem(item);
            list.selectionModel()->select(list.indexFromItem(item),
                                          QItemSelectionModel::ClearAndSelect);
            QCoreApplication::processEvents();
            const auto row = list.visualItemRect(item);
            const auto image = list.viewport()->grab().toImage();
            const auto colors = resolvedThemeForWidget(list).colors;
            QCOMPARE(image.pixelColor(row.left() + 7, row.center().y()), colors.tint);
            const auto outline = selectionOutline(
                image, qRound((row.left() + 7) * image.devicePixelRatio()), colors.tint);
            QVERIFY2(outline.isEmpty(), qPrintable(outline));
        }
    }
    void adjacentSelectedConnectionRowsLeaveTheTreeSeam() {
        using namespace choscordb::design;
        QWidget root;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(root);
        QListWidget list(&root);
        list.setObjectName("savedConnections");
        list.setProperty("designSurface", "sidebar");
        list.setSelectionMode(QAbstractItemView::MultiSelection);
        list.setItemDelegate(new NavigationProfileDelegate(&list));
        for (const auto* name : {"Commerce", "Logistics"}) {
            auto* item = new QListWidgetItem(name, &list);
            item->setData(NavigationProfileDelegate::DriverRole, "sqlite");
            item->setSelected(true);
        }
        list.resize(240, 80);
        root.resize(250, 90);
        root.show();
        QCoreApplication::processEvents();
        const auto first = list.visualItemRect(list.item(0));
        const auto second = list.visualItemRect(list.item(1));
        const auto image = list.viewport()->grab().toImage();
        const int scale = qRound(image.devicePixelRatio());
        const int x = first.left() + spacing(Spacing::One) + spacing(Spacing::Two) * 3 / 2 +
                      dimension(Dimension::ControlExtraSmall);
        int seam = 0;
        for (int y = first.center().y() * scale; y <= second.center().y() * scale; ++y)
            seam += image.pixelColor(x * scale, y).alpha() < 255;
        const int gap = layoutMetrics().navigationHighlightGap;
        QVERIFY2(seam >= gap * scale && seam <= (gap + 1) * scale,
                 qPrintable(QString("seam=%1").arg(seam)));
    }
    void selectedHistoryRowUsesSidebarSelection() {
        using namespace choscordb::design;
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            QWidget root;
            ThemeManager theme;
            theme.setMode(mode);
            theme.applyTo(root);
            QListWidget list(&root);
            list.setObjectName("sidebarHistoryItems");
            list.setProperty("designSurface", "sidebar");
            list.setItemDelegate(new RecentHistoryRowDelegate(&list));
            auto* item = new QListWidgetItem("select 1", &list);
            item->setData(RecentHistoryRowDelegate::SqlRole, "select 1");
            item->setData(RecentHistoryRowDelegate::StatusRole, "completed");
            list.resize(240, 120);
            root.resize(250, 130);
            root.show();
            list.setCurrentItem(item);
            list.selectionModel()->select(list.indexFromItem(item),
                                          QItemSelectionModel::ClearAndSelect);
            QTest::mouseMove(list.viewport(),
                             QPoint(list.viewport()->width() - 1, list.viewport()->height() - 1));
            QCoreApplication::processEvents();
            const auto row = list.visualItemRect(item);
            const auto image = list.viewport()->grab().toImage();
            const auto colors = resolvedThemeForWidget(list).colors;
            QCOMPARE(image.pixelColor(row.left() + 8, row.center().y()), colors.tint);
            const auto outline = selectionOutline(
                image, qRound((row.left() + 8) * image.devicePixelRatio()), colors.tint);
            QVERIFY2(outline.isEmpty(), qPrintable(outline));
            QTest::mouseMove(list.viewport(), row.center());
            QCoreApplication::processEvents();
            QCOMPARE(list.viewport()->grab().toImage().pixelColor(row.left() + 8, row.center().y()),
                     composite(colors.tint, colors.tint));
        }
    }
    void idleConnectionRowShowsTheSidebarBehindIt() {
        using namespace choscordb::design;
        const auto cursor = QCursor::pos();
        const auto restoreCursor = qScopeGuard([cursor] { QCursor::setPos(cursor); });
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            QWidget root;
            root.setObjectName("navigatorBody");
            ThemeManager theme;
            theme.setMode(mode);
            theme.applyTo(root);
            QListWidget list(&root);
            list.setObjectName("savedConnections");
            list.setProperty("designSurface", "sidebar");
            list.setItemDelegate(new NavigationProfileDelegate(&list));
            auto* item = new QListWidgetItem("Example Postgres\nPostgreSQL", &list);
            item->setData(NavigationProfileDelegate::DriverRole, "postgres");
            list.resize(240, 80);
            root.resize(250, 90);
            QCursor::setPos(-1000, -1000);
            root.show();
            list.clearSelection();
            QCoreApplication::processEvents();
            const auto row = list.visualItemRect(item);
            const auto image = root.grab().toImage();
            const auto offset = list.viewport()->mapTo(&root, QPoint());
            const QPoint inRow = offset + QPoint(row.left() + 3, row.center().y());
            const QPoint belowRows = offset + QPoint(row.left() + 3, row.bottom() + 20);
            QCOMPARE(image.pixelColor(inRow), image.pixelColor(belowRows));
        }
    }
    void connectionRowsAreCompactAndShowOnlyCenteredNames() {
        using namespace choscordb::design;
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            QWidget root;
            ThemeManager theme;
            theme.setMode(mode);
            theme.applyTo(root);
            QListWidget list(&root);
            list.setProperty("designSurface", "sidebar");
            list.setItemDelegate(new NavigationProfileDelegate(&list));
            auto* item = new QListWidgetItem("Example Postgres\nPostgreSQL", &list);
            item->setData(NavigationProfileDelegate::DriverRole, "postgres");
            list.resize(240, 55);
            root.resize(250, 65);
            root.show();
            list.setCurrentItem(item);
            QCoreApplication::processEvents();
            const auto row = list.visualItemRect(item);
            QVERIFY(row.height() <= 36);
            const auto withDetail = list.viewport()->grab(row).toImage();
            const auto ink = resolvedThemeForWidget(list).colors.textDefault;
            int inkTop = withDetail.height();
            int inkBottom = -1;
            for (int y = 0; y < withDetail.height(); ++y)
                for (int x = 42; x < 150; ++x) {
                    const auto pixel = withDetail.pixelColor(x, y);
                    if (qAbs(pixel.red() - ink.red()) <= 10 &&
                        qAbs(pixel.green() - ink.green()) <= 10 &&
                        qAbs(pixel.blue() - ink.blue()) <= 10) {
                        inkTop = qMin(inkTop, y);
                        inkBottom = qMax(inkBottom, y);
                    }
                }
            QVERIFY(inkBottom >= inkTop);
            QVERIFY(qAbs((inkTop + inkBottom) / 2 - withDetail.height() / 2) <= 2);
            item->setText("Example Postgres\nDifferent detail");
            QCoreApplication::processEvents();
            QCOMPARE(list.viewport()->grab(row).toImage(), withDetail);
        }
    }
    void navigationBranchesUseMutedInkInBothThemes() {
        using namespace choscordb::design;
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            QWidget root;
            ThemeManager theme;
            theme.setMode(mode);
            theme.applyTo(root);
            NavigationTreeView tree(&root);
            ControlStyle style;
            tree.setStyle(&style);
            QStyleOption branch;
            branch.rect = QRect(0, 0, 20, 20);
            branch.state = QStyle::State_Enabled | QStyle::State_Children;
            branch.palette = tree.palette();
            QImage image(branch.rect.size(), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            style.drawPrimitive(QStyle::PE_IndicatorBranch, &branch, &painter, &tree);
            painter.end();
            const auto muted = resolvedThemeForWidget(tree).colors.textSubtle;
            int inkPixels = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const auto pixel = image.pixelColor(x, y);
                    if (pixel.alpha() > 120 && qAbs(pixel.red() - muted.red()) <= 8 &&
                        qAbs(pixel.green() - muted.green()) <= 8 &&
                        qAbs(pixel.blue() - muted.blue()) <= 8)
                        ++inkPixels;
                }
            QVERIFY(inkPixels > 0);

            QStandardItemModel model;
            auto* parent = new QStandardItem("Group");
            parent->appendRow(new QStandardItem("Child"));
            model.appendRow(parent);
            tree.setModel(&model);
            tree.setHeaderHidden(true);
            tree.resize(220, 90);
            root.resize(230, 100);
            root.show();
            QCoreApplication::processEvents();
            const auto row = tree.visualRect(model.index(0, 0));
            const auto rendered = tree.viewport()->grab().toImage();
            bool mutedBranch = false;
            for (int y = row.top(); y <= row.bottom(); ++y)
                for (int x = 0; x < row.left(); ++x)
                    mutedBranch |= rendered.pixelColor(x, y) == muted;
            QVERIFY(mutedBranch);
        }
    }
    void primaryKeyboardFocusContrastsWithTheActionFill_data() {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<QColor>("expectedRing");
        QTest::addColumn<bool>("native");
        QTest::newRow("light") << false << QColor("#045ede") << false;
        QTest::newRow("native-light") << false << QColor("#045ede") << true;
        QTest::newRow("dark") << true << QColor("#0351c1") << false;
        QTest::newRow("native-dark") << true << QColor("#0351c1") << true;
    }
    void primaryKeyboardFocusContrastsWithTheActionFill() {
        QFETCH(bool, dark);
        QFETCH(QColor, expectedRing);
        QFETCH(bool, native);
        using namespace choscordb::design;
        ControlStyle controlStyle;
        QWidget root;
        ThemeManager theme;
        theme.setMode(dark ? ThemeMode::Dark : ThemeMode::Light);
        theme.applyTo(root);
        auto* button = native ? new QPushButton("", &root) : new Button("", &root);
        if (native) {
            button->setProperty("variant", "default");
            button->setStyle(&controlStyle);
        }
        Button other("Other", &root);
        button->resize(100, 33);
        button->move(0, focusSpec().ringWidth * 4);
        other.move(0, 60);
        root.resize(160, 120);
        root.show();
        root.activateWindow();
        other.setFocus(Qt::TabFocusReason);
        QTRY_VERIFY(other.hasFocus());
        const auto normal = button->grab().toImage();
        button->setFocus(Qt::TabFocusReason);
        QTRY_VERIFY(button->hasFocus());
        const auto focused = native ? root.grab().toImage() : button->grab().toImage();
        if (native) {
            const auto ring = resolvedThemeForWidget(*button).colors.brand;
            QTRY_COMPARE(root.grab().toImage().pixelColor(50, button->y() - 1), ring);
        } else {
            QCOMPARE(focused.pixelColor(50, 0), expectedRing);
            QCOMPARE(focused.pixelColor(50, 1), expectedRing);
        }
        QVERIFY(focused != normal);
        QSignalSpy clicked(button, &QPushButton::clicked);
        QTest::keyClick(button, Qt::Key_Space);
        QCOMPARE(clicked.count(), 1);
    }
    void buttonTextUsesTheKumoSizeClassAtMediumWeight() {
        using namespace choscordb::design;
        Button button("Save");
        QCOMPARE(button.font().pixelSize(), 14);
        QCOMPARE(button.font().weight(), QFont::Medium);
        QCOMPARE(button.font().family(), QString("Inter"));
        for (const auto size : {ButtonSize::ExtraSmall, ButtonSize::Small}) {
            button.setButtonSize(size);
            QCOMPARE(button.font().pixelSize(), 12);
            QCOMPARE(button.font().weight(), QFont::Medium);
        }
        button.setButtonSize(ButtonSize::Small);
        const auto plain = button.sizeHint().width();
        button.setDesignIcon(Icon::Run);
        QCOMPARE(button.height(), 26);
        QCOMPARE(button.sizeHint().width(),
                 plain - spacing(Spacing::Two) + spacing(Spacing::OneHalf) +
                     dimension(Dimension::IconSmall) + spacing(Spacing::One));
    }
    void primaryUsesTheKumoEmphasisGradientAndRing() {
        using namespace choscordb::design;
        QWidget root;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(root);
        Button button("", &root);
        button.resize(100, 36);
        root.resize(150, 80);
        root.show();
        button.clearFocus();
        const auto near = [](const QColor& actual, const QColor& expected) {
            return qAbs(actual.red() - expected.red()) <= 6 &&
                   qAbs(actual.green() - expected.green()) <= 6 &&
                   qAbs(actual.blue() - expected.blue()) <= 6;
        };
        const auto rest = button.grab().toImage();
        QCOMPARE(rest.pixelColor(50, 0), QColor("#045ede"));
        QCOMPARE(rest.pixelColor(50, 1), QColor("#619eff"));
        QVERIFY2(near(rest.pixelColor(50, 2), QColor("#3c86ff")),
                 qPrintable(rest.pixelColor(50, 2).name()));
        QVERIFY2(near(rest.pixelColor(50, 34), QColor("#056dff")),
                 qPrintable(rest.pixelColor(50, 34).name()));
        QTest::mouseMove(&button, button.rect().center());
        QTRY_VERIFY(near(button.grab().toImage().pixelColor(50, 2), QColor("#619eff")));
        QTest::mousePress(&button, Qt::LeftButton);
        QVERIFY(near(button.grab().toImage().pixelColor(50, 2), QColor("#619eff")));
        QTest::mouseRelease(&button, Qt::LeftButton);
        button.setVariant(ButtonVariant::Destructive);
        QTest::mouseMove(&root, QPoint(140, 70));
        QTRY_COMPARE(button.grab().toImage().pixelColor(50, 0), QColor("#da252e"));
        QVERIFY(near(button.grab().toImage().pixelColor(50, 34), QColor("#fb2c36")));
    }
    void compactIconActionsRespectReferenceLineBoxAndDispatch() {
        using namespace choscordb::design;
        Button button("Run");
        button.setButtonSize(ButtonSize::Small);
        QCOMPARE(button.height(), 26);
        button.setDesignIcon(Icon::Run);
        QCOMPARE(button.height(), 26);
        button.resize(75, button.height());
        button.show();
        QSignalSpy clicked(&button, &QPushButton::clicked);
        QTest::mouseClick(&button, Qt::LeftButton);
        QCOMPARE(clicked.count(), 1);
        button.setButtonSize(ButtonSize::Default);
        QCOMPARE(button.height(), 36);
        Button customIcon("Run");
        customIcon.setButtonSize(ButtonSize::Small);
        customIcon.setIcon(themedIcon(Icon::Run, Qt::black, 16));
        QCOMPARE(customIcon.height(), 26);
    }
    void semanticButtonIconFollowsLiveThemeAndVariant() {
        using namespace choscordb::design;
        QWidget host;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(host);
        Button button("", &host);
        button.setVariant(ButtonVariant::Outline);
        button.setButtonSize(ButtonSize::Icon);
        button.setDesignIcon(Icon::Add);
        const auto light = button.grab().toImage();
        QVERIFY(light.pixelColor(light.width() / 2, light.height() / 2).lightness() < 100);
        theme.setMode(ThemeMode::Dark);
        theme.applyTo(host);
        const auto dark = button.grab().toImage();
        QVERIFY(dark.pixelColor(dark.width() / 2, dark.height() / 2).lightness() > 180);
        button.setVariant(ButtonVariant::Default);
        const auto primary = button.grab().toImage();
        QVERIFY(primary.pixelColor(primary.width() / 2, primary.height() / 2).lightness() > 180);
    }
    void destructiveTextIsWhiteOnTheDangerEmphasis() {
        using namespace choscordb::design;
        QWidget host;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(host);
        Button button("MMMM", &host);
        button.setVariant(ButtonVariant::Destructive);
        button.resize(120, 36);
        const auto image = button.grab().toImage();
        int white = 0;
        for (int y = 6; y < 30; ++y)
            for (int x = 15; x < 105; ++x)
                white += image.pixelColor(x, y) == QColor(Qt::white);
        QVERIFY(white > 0);
    }
    void naturalTextIconButtonsKeepTheFullLabel() {
        using namespace choscordb::design;
        QWidget host;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(host);
        Button button("Done", &host);
        button.setIcon(themedIcon(Icon::Add, QColor("#fafafa"), 16));
        button.resize(button.sizeHint());
        const auto natural = button.grab().toImage();
        button.resize(button.width() + 40, button.height());
        const auto expanded = button.grab().toImage();
        auto pixels = [](const QImage& image) {
            int count = 0;
            for (int y = 7; y < image.height() - 7; ++y)
                for (int x = 10; x < image.width() - 10; ++x)
                    if (const auto c = image.pixelColor(x, y);
                        c.red() > 230 && c.green() > 230 && c.blue() > 230)
                        ++count;
            return count;
        };
        QVERIFY(pixels(expanded) > 30);
        QCOMPARE(pixels(natural), pixels(expanded));
    }
    void loadingReservesALeadingIndicatorSlot() {
        using namespace choscordb::design;
        Button button("Save");
        const int normalWidth = button.sizeHint().width();
        button.setLoading(true);
        QCOMPARE(button.sizeHint().width(),
                 normalWidth + dimension(Dimension::Icon) + spacing(Spacing::One));
        button.setButtonSize(ButtonSize::Icon);
        QCOMPARE(button.sizeHint(), QSize(36, 36));
    }
    void mouseFocusDoesNotPaintTheKeyboardRing() {
        using namespace choscordb::design;
        QWidget host;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(host);
        Button button("", &host);
        Button other("Other", &host);
        button.resize(100, 32);
        other.move(0, 40);
        host.resize(200, 100);
        host.show();
        host.activateWindow();
        other.setFocus(Qt::TabFocusReason);
        QTRY_VERIFY(other.hasFocus());
        const auto normal = button.grab().toImage();
        button.setFocus(Qt::MouseFocusReason);
        QTRY_VERIFY(button.hasFocus());
        QCOMPARE(button.grab().toImage(), normal);
        other.setFocus(Qt::TabFocusReason);
        button.setFocus(Qt::TabFocusReason);
        QTRY_VERIFY(button.hasFocus());
        QVERIFY(button.grab().toImage() != normal);
        QCOMPARE(button.size(), QSize(100, 36));
    }
    void groupedActionsAreJoinedAndKeyboardAccessible() {
        using namespace choscordb::design;
        ButtonGroup group;
        Button previous("Previous");
        Button next("Next");
        previous.setVariant(ButtonVariant::Outline);
        next.setVariant(ButtonVariant::Outline);
        group.addButton(&previous);
        group.addButton(&next);
        group.show();
        QCoreApplication::processEvents();
        QCOMPARE(previous.parentWidget(), &group);
        QCOMPARE(next.x(), previous.x() + previous.width());
        const auto image = group.grab().toImage();
        const int scale = qRound(image.devicePixelRatio());
        const int seam = next.x() * scale;
        const int sampleY = 5 * scale;
        QVERIFY(image.pixelColor(seam, sampleY) != image.pixelColor(seam - scale, sampleY));
        previous.setFocus(Qt::TabFocusReason);
        QTest::keyClick(&previous, Qt::Key_Tab);
        QVERIFY(next.hasFocus());
        QSignalSpy clicked(&next, &QPushButton::clicked);
        QTest::keyClick(&next, Qt::Key_Space);
        QCOMPARE(clicked.count(), 1);
    }
    void naturalExtraSmallButtonDoesNotElideItsLabel() {
        using namespace choscordb::design;
        QWidget host;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(host);
        Button button("WWW", &host);
        button.setButtonSize(ButtonSize::ExtraSmall);
        button.resize(button.sizeHint());
        const auto natural = button.grab().toImage();
        button.resize(button.width() + 40, button.height());
        const auto expanded = button.grab().toImage();
        auto textPixels = [](const QImage& image) {
            const auto fill = image.pixelColor(4, image.height() / 2);
            const auto distance = [](const QColor& a, const QColor& b) {
                return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) +
                       qAbs(a.blue() - b.blue());
            };
            int count = 0;
            for (int y = 6; y < image.height() - 6; ++y)
                for (int x = 4; x < image.width() - 4; ++x)
                    if (const auto c = image.pixelColor(x, y);
                        distance(c, Qt::white) < distance(c, fill))
                        ++count;
            return count;
        };
        QVERIFY(textPixels(expanded) > 30);
        QCOMPARE(textPixels(natural), textPixels(expanded));
    }
    void iconVariantsRemainSquareInsideExpandingLayouts() {
        using namespace choscordb::design;
        QWidget host;
        QHBoxLayout layout(&host);
        Button button("", &host);
        button.setButtonSize(ButtonSize::Icon);
        layout.addWidget(&button);
        host.resize(300, 80);
        host.show();
        QCoreApplication::processEvents();
        QCOMPARE(button.size(), QSize(36, 36));
        button.setButtonSize(ButtonSize::Default);
        button.setText("A wider text action");
        QCoreApplication::processEvents();
        QVERIFY(button.width() > 32);
    }
    void forcedContrastReachesNestedCustomButtons() {
        using namespace choscordb::design;
        QWidget host;
        QWidget nested(&host);
        Button button("", &nested);
        button.resize(100, 32);
        ThemeManager theme;
        QPalette system;
        system.setColor(QPalette::Window, Qt::black);
        system.setColor(QPalette::Base, Qt::black);
        system.setColor(QPalette::WindowText, Qt::white);
        system.setColor(QPalette::Highlight, Qt::cyan);
        system.setColor(QPalette::HighlightedText, Qt::black);
        theme.setSystemPalette(system);
        theme.setForcedContrast(true);
        theme.applyTo(host);
        QCOMPARE(button.grab().toImage().pixelColor(50, 5), QColor(Qt::cyan));
    }
    void loadingHasVisibleFeedbackAndKeepsTheActionLabel() {
        using namespace choscordb::design;
        Button button("Save");
        button.resize(100, 32);
        button.setEnabled(false);
        const auto disabled = button.grab().toImage();
        button.setLoading(true);
        const auto loading = button.grab().toImage();
        QVERIFY(loading != disabled);
        QCOMPARE(button.text(), QString("Save"));
        QCOMPARE(button.accessibleDescription(), QString("Loading"));
    }
    void loadingPreventsDispatchAndRestoresEnabledState() {
        using namespace choscordb::design;
        Button button("Save");
        QSignalSpy clicked(&button, &QPushButton::clicked);
        button.setLoading(true);
        QVERIFY(button.isLoading());
        QVERIFY(!button.isEnabled());
        button.click();
        QCOMPARE(clicked.count(), 0);
        button.setLoading(false);
        QVERIFY(button.isEnabled());
        button.click();
        QCOMPARE(clicked.count(), 1);
        button.setEnabled(false);
        button.setLoading(true);
        button.setLoading(false);
        QVERIFY(!button.isEnabled());
    }
    void buttonVariantsRenderTheKumoSurfaces() {
        using namespace choscordb::design;
        QWidget host;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(host);
        Button button("", &host);
        button.resize(100, 36);
        host.resize(150, 80);
        host.show();
        QCoreApplication::processEvents();
        const auto colors = resolvedThemeForWidget(button).colors;
        QWidget idleHost;
        theme.applyTo(idleHost);
        Button idle("", &idleHost);
        idle.resize(100, 36);
        idle.setVariant(ButtonVariant::Ghost);
        QCOMPARE(idle.grab().toImage().pixelColor(50, 18).alpha(), 0);
        idle.setVariant(ButtonVariant::Outline);
        QCOMPARE(idle.grab().toImage().pixelColor(50, 18).alpha(), 0);
        QCOMPARE(idle.grab().toImage().pixelColor(50, 0).rgba(), colors.line.rgba());
        button.setVariant(ButtonVariant::Secondary);
        QCOMPARE(button.grab().toImage().pixelColor(50, 18), colors.base);
        QCOMPARE(button.grab().toImage().pixelColor(50, 0).rgba(), colors.line.rgba());
        QTest::mouseMove(&button, button.rect().center());
        QTRY_COMPARE(button.grab().toImage().pixelColor(50, 18), colors.tint);
        button.setVariant(ButtonVariant::Ghost);
        QTRY_COMPARE(button.grab().toImage().pixelColor(50, 18), colors.tint);
        button.setVariant(ButtonVariant::Secondary);
        button.setEnabled(false);
        QCOMPARE(button.grab().toImage().pixelColor(50, 18).alpha(), 64);
        QCOMPARE(button.grab().toImage().pixelColor(0, 0).alpha(), 0);
        button.setButtonSize(ButtonSize::Small);
        button.setEnabled(true);
        QVERIFY(button.grab().toImage().pixelColor(1, 1).alpha() > 0);
    }
    void buttonsHaveReferenceSizes() {
        using namespace choscordb::design;
        Button button("Save");
        QCOMPARE(button.sizeHint().height(), 36);
        button.setButtonSize(ButtonSize::ExtraSmall);
        QCOMPARE(button.sizeHint().height(), 20);
        button.setButtonSize(ButtonSize::Small);
        QCOMPARE(button.sizeHint().height(), 26);
        button.setDesignIcon(Icon::Add);
        QCOMPARE(button.sizeHint().height(), 26);
        button.setButtonSize(ButtonSize::IconExtraSmall);
        QCOMPARE(button.sizeHint(), QSize(14, 14));
        button.setButtonSize(ButtonSize::Icon);
        QCOMPARE(button.sizeHint(), QSize(36, 36));
    }
};
QTEST_MAIN(ComponentsTest)
#include "components_test.moc"
