#include "design_system/column_row/column_row.h"
#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/theme.h"
#include "design_system/tree/navigation_tree_view.h"
#include "ink_checks.h"
#include "preview_test.h"
#include "tools/preview/preview_window.h"
#include <QLabel>
#include <QListWidget>
#include <QStyleOptionViewItem>
#include <QTreeView>
#include <QtTest>

namespace {
QColor sidebarHoverFor(const QWidget& widget) {
    const auto colors = choscordb::design::resolvedThemeForWidget(widget).colors;
    return choscordb::design::composite(colors.tint, colors.elevated);
}
}

void PreviewTest::columnRowSpecimenUsesRealDelegateInBothThemes() {
    using namespace choscordb::design;
    PreviewWindow window;
    QVERIFY(window.selectSpecimen("column-row"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* tree = host->findChild<QTreeView*>("previewColumnRows");
        QVERIFY(tree && tree->isVisible());
        QVERIFY(dynamic_cast<ColumnRowDelegate*>(tree->itemDelegate()));
        QCOMPARE(tree->model()->rowCount(), 7);
        QCOMPARE(tree->model()->index(0, 0).data().toString(), QString("id"));
        QCOMPARE(tree->model()->index(0, 0).data(Qt::UserRole + 1).toString(), QString("bigint"));
        QCOMPARE(tree->model()->index(1, 0).data().toString(), QString("name"));
        QCOMPARE(tree->model()->index(1, 0).data(Qt::UserRole + 1).toString(), QString("text"));
        QCOMPARE(tree->model()->index(4, 0).data(Qt::UserRole + 1).toString(),
                 QString("timestamp(6) with time zone and extended suffix"));
        QVERIFY(tree->visualRect(tree->model()->index(5, 0)).isValid());
        QCOMPARE(tree->model()->index(6, 0).data(Qt::UserRole + 1).toString(),
                 QString("timestamp with time zone"));

        auto* pinned = host->findChild<QTreeView*>("pinnedList");
        QVERIFY(pinned && pinned->isVisible());
        QVERIFY(dynamic_cast<NavigationTreeView*>(pinned));
        QVERIFY(dynamic_cast<ColumnRowDelegate*>(pinned->itemDelegate()));
        const auto id = pinned->model()->index(0, 0);
        const auto next = pinned->model()->index(1, 0);
        QCOMPARE(pinned->visualRect(next).top(), pinned->visualRect(id).bottom() + 1);
        QCOMPARE(pinned->visualRect(id).height(),
                 choscordb::design::dimension(choscordb::design::Dimension::Row));
        QCOMPARE(id.data().toString(), QString("id"));
        QCOMPARE(id.data(Qt::UserRole + 1).toString(), QString("bigint"));
        QCOMPARE(pinned->visualRect(id).height(),
                 pinned->visualRect(pinned->model()->index(1, 0)).height());
        const auto pinnedImage = pinned->viewport()->grab().toImage();
        QCOMPARE(pinnedImage.pixelColor(pinnedImage.width() - 8, pinnedImage.height() - 8).alpha(),
                 0);

        pinned->setFixedWidth(160);
        pinned->doItemsLayout();
        QCoreApplication::processEvents();
        const auto nested = pinned->model()->index(
            0, 0, pinned->model()->index(0, 0, pinned->model()->index(1, 0)));
        QVERIFY(nested.isValid());
        const QRect nestedRow = pinned->visualRect(nested);
        QCOMPARE(nestedRow.height(), pinned->visualRect(id).height());
        const QImage nestedBefore = pinned->viewport()->grab(nestedRow).toImage();
        pinned->model()->setData(nested, "timestamp with time zonf", Qt::UserRole + 1);
        QCoreApplication::processEvents();
        QCOMPARE(pinned->viewport()->grab(nestedRow).toImage(), nestedBefore);
        pinned->model()->setData(nested, "cimestamp with time zone", Qt::UserRole + 1);
        QCoreApplication::processEvents();
        const QImage nestedAfter = pinned->viewport()->grab(nestedRow).toImage();
        const int nameRegionWidth = nestedBefore.width() * 2 / 5;
        QCOMPARE(nestedBefore.copy(0, 0, nameRegionWidth, nestedBefore.height()),
                 nestedAfter.copy(0, 0, nameRegionWidth, nestedAfter.height()));
        QVERIFY(nestedBefore != nestedAfter);

        tree->setFixedWidth(440);
        auto column = tree->model()->index(4, 0);
        tree->model()->setData(column, "created_at");
        tree->model()->setData(column, QString{}, Qt::UserRole + 1);
        QCoreApplication::processEvents();
        const QRect row = tree->visualRect(column);
        const QImage withoutType = tree->viewport()->grab(row).toImage();
        tree->model()->setData(column, "timestamp with time zone", Qt::UserRole + 1);
        QCoreApplication::processEvents();
        QCOMPARE(row.height(), tree->visualRect(tree->model()->index(5, 0)).height());
        const QImage first = tree->viewport()->grab(row).toImage();
        QVERIFY(withoutType != first);
        tree->model()->setData(column, "timestamp with time zonf", Qt::UserRole + 1);
        QCoreApplication::processEvents();
        const QImage second = tree->viewport()->grab(row).toImage();
        QCOMPARE(first.copy(0, 0, first.width() / 2, first.height()),
                 second.copy(0, 0, second.width() / 2, second.height()));
        QVERIFY(first != second);

        const auto rightmostTypeInk = [&withoutType](const QImage& image) {
            for (int x = image.width() - 1; x >= 0; --x)
                for (int y = 0; y < image.height(); ++y)
                    if (image.pixelColor(x, y) != withoutType.pixelColor(x, y))
                        return x;
            return -1;
        };
        const int longTypeRight = rightmostTypeInk(second);
        QVERIFY(longTypeRight >= 0);
        tree->model()->setData(column, "bigint", Qt::UserRole + 1);
        QCoreApplication::processEvents();
        const int shortTypeRight = rightmostTypeInk(tree->viewport()->grab(row).toImage());
        QVERIFY(shortTypeRight >= 0);
        QVERIFY(qAbs(longTypeRight - shortTypeRight) <=
                qCeil(spacing(Spacing::One) * second.devicePixelRatio()));
    }
}

void PreviewTest::statusRowSpecimenShowsTrailingIconAndRedFailedRow() {
    using namespace choscordb::design;
    PreviewWindow window;
    QVERIFY(window.selectSpecimen("column-row"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* tree = window.findChild<QWidget*>(name)->findChild<QTreeView*>("previewStatusRows");
        QVERIFY(tree && tree->isVisible());
        QVERIFY(dynamic_cast<ColumnRowDelegate*>(tree->itemDelegate()));
        const auto colors = resolvedThemeForWidget(*tree).colors;
        const auto image = tree->viewport()->grab().toImage();
        const auto scale = image.devicePixelRatio();
        const auto pixel = [&](int x, int y) {
            return image.pixelColor(qRound(x * scale), qRound(y * scale));
        };
        const auto distance = [](const QColor& a, const QColor& b) {
            return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) +
                   qAbs(a.blue() - b.blue());
        };

        const auto iconInk = [&](const QRect& row, const QColor& color) {
            const int size = dimension(Dimension::IconSmall);
            int count = 0;
            for (int y = row.center().y() - size / 2; y <= row.center().y() + size / 2; ++y)
                for (int x = row.right() - size - spacing(Spacing::Two); x <= row.right(); ++x)
                    count += choscordb::test::blendedInk(pixel(x, y), color,
                                                         pixel(row.center().x(), row.top() + 2));
            return count;
        };
        QStringList names;
        for (int row = 0; row < tree->model()->rowCount(); ++row)
            names << tree->model()->index(row, 0).data().toString();
        QCOMPARE(names, QStringList({"orders", "customers", "archive", "invoices"}));
        const auto failed = tree->visualRect(tree->model()->index(0, 0));
        const auto loading = tree->visualRect(tree->model()->index(1, 0));
        const auto unavailable = tree->visualRect(tree->model()->index(2, 0));
        const auto plain = tree->visualRect(tree->model()->index(3, 0));
        const auto failedFill = pixel(failed.center().x(), failed.top() + 2);
        const auto expectedFill =
            composite(colors.dangerTint, pixel(plain.center().x(), plain.top() + 2));
        QVERIFY2(qAbs(failedFill.red() - expectedFill.red()) <= 1 &&
                     qAbs(failedFill.green() - expectedFill.green()) <= 1 &&
                     qAbs(failedFill.blue() - expectedFill.blue()) <= 1,
                 qPrintable(failedFill.name()));
        QVERIFY(pixel(loading.center().x(), loading.top() + 2) != colors.dangerTint);
        QVERIFY(iconInk(failed, colors.textDanger) > 0);
        QVERIFY(iconInk(loading, colors.textDanger) < iconInk(loading, colors.textSubtle));
        QVERIFY(iconInk(loading, colors.textSubtle) > 0);
        QVERIFY(iconInk(unavailable, colors.textSubtle) > 0);
        QCOMPARE(iconInk(plain, colors.textSubtle), 0);
        int redName = 0;
        for (int y = failed.top(); y <= failed.bottom(); ++y)
            for (int x = failed.left(); x < failed.center().x(); ++x)
                redName += distance(pixel(x, y), colors.textDanger) < 120;
        QVERIFY(redName > 0);
    }
}

void PreviewTest::navigationTreeSpecimenUsesRealTreeInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("lists-navigation"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* tree = host->findChild<QTreeView*>("previewNavigationTree");
        QVERIFY(tree);
        QVERIFY(tree->isVisible());
        QVERIFY(tree->currentIndex().isValid());
        const auto decorated = tree->model()->index(0, 0, tree->model()->index(0, 0));
        QStyleOptionViewItem item;
        item.initFrom(tree);
        item.widget = tree;
        item.rect = tree->visualRect(decorated);
        item.text = decorated.data().toString();
        item.icon = qvariant_cast<QIcon>(decorated.data(Qt::DecorationRole));
        item.decorationSize = item.icon.actualSize(tree->iconSize());
        item.features = QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration;
        item.decorationAlignment = Qt::AlignLeft | Qt::AlignVCenter;
        const auto iconRect =
            tree->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &item, tree);
        const auto textRect =
            tree->style()->subElementRect(QStyle::SE_ItemViewItemText, &item, tree);
        const int textInset =
            tree->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, &item, tree) + 1;
        QCOMPARE(textRect.left() + textInset - iconRect.right() - 1, 2);
        const auto parentRow = tree->visualRect(tree->model()->index(0, 0));
        QCOMPARE(parentRow.height(),
                 choscordb::design::dimension(choscordb::design::Dimension::Row));
        const auto treeImage = tree->viewport()->grab().toImage();
        QVERIFY(parentRow.left() > 6);
        QCOMPARE(treeImage.pixelColor(parentRow.left() - 1, parentRow.bottom() - 4),
                 choscordb::design::resolvedThemeForWidget(*tree).colors.tint);
        const auto branchInk = choscordb::design::resolvedThemeForWidget(*tree).colors.textSubtle;
        bool hasMutedBranch = false;
        for (int y = parentRow.top(); y <= parentRow.bottom(); ++y)
            for (int x = 0; x < parentRow.left(); ++x)
                hasMutedBranch |= treeImage.pixelColor(x, y) == branchInk;
        QVERIFY(hasMutedBranch);
        const auto child = tree->model()->index(0, 0, tree->model()->index(0, 0));
        const auto row = tree->visualRect(child);
        QVERIFY(row.isValid());
        QCOMPARE(row.height(), choscordb::design::dimension(choscordb::design::Dimension::Row));
        QTest::mouseMove(tree->viewport(), QPoint(1, 1));
        QTest::mouseMove(tree->viewport(), row.center());
        QCoreApplication::processEvents();
        QCOMPARE(tree->viewport()->grab().toImage().pixelColor(row.right() - 8, row.center().y()),
                 sidebarHoverFor(*tree));
        const auto hoveredImage = tree->viewport()->grab().toImage();
        QVERIFY(hoveredImage.pixelColor(tree->viewport()->width() / 2, row.top()) !=
                sidebarHoverFor(*tree));
        QCOMPARE(hoveredImage.pixelColor(row.right() - 12, row.top() + 1), sidebarHoverFor(*tree));
        QCOMPARE(hoveredImage.pixelColor(row.right() - 12, row.bottom() - 1),
                 sidebarHoverFor(*tree));
        auto* sidebarList = host->findChild<QListWidget*>("previewSidebarList");
        QVERIFY(sidebarList && sidebarList->isVisible());
        QCOMPARE(sidebarList->property("designSurface").toString(), QString("sidebar"));
        const auto listRow = sidebarList->visualItemRect(sidebarList->item(0));
        QTest::mouseMove(sidebarList->viewport(), QPoint(1, 1));
        QTest::mouseMove(sidebarList->viewport(), listRow.center());
        QCoreApplication::processEvents();
        QCOMPARE(sidebarList->viewport()->grab().toImage().pixelColor(listRow.right() - 8,
                                                                      listRow.center().y()),
                 sidebarHoverFor(*sidebarList));
    }
    QVERIFY(window.selectSpecimen("navigation-profile-row"));
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* list =
            window.findChild<QWidget*>(name)->findChild<QListWidget*>("previewNavigationProfiles");
        QVERIFY(list &&
                dynamic_cast<choscordb::design::NavigationProfileDelegate*>(list->itemDelegate()));
        QCOMPARE(list->selectionMode(), QAbstractItemView::MultiSelection);
        QVERIFY(list->item(0)->isSelected());
        QVERIFY(list->item(1)->isSelected());
        QVERIFY(!list->item(2)->isSelected());
        QVERIFY(list->visualItemRect(list->item(1)).height() <= 36);
    }
}

void PreviewTest::driverBadgeAndConnectionTileUseNeutralTokensInBothThemes() {
    using namespace choscordb::design;
    PreviewWindow window;
    QVERIFY(window.selectSpecimen("feedback"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        const auto colors = resolvedThemeForWidget(*host).colors;
        auto* logo = host->findChild<QLabel*>("previewDriverLogo");
        auto* badge = host->findChild<QLabel*>("previewDriverBadge");
        QVERIFY(logo && badge);
        QVERIFY(!logo->pixmap().isNull());
        QCOMPARE(badge->property("variant").toString(), QString("outline"));
        const auto image = badge->grab().toImage();
        const auto scale = image.devicePixelRatio();
        QCOMPARE(image.pixelColor(qRound(badge->width() / 2 * scale), qRound(2 * scale)),
                 colors.base);
    }
    QVERIFY(window.selectSpecimen("navigation-profile-row"));
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* list = host->findChild<QListWidget*>("previewNavigationProfiles");
        QVERIFY(list);
        for (int row = 0; row < list->count(); ++row) {
            const auto rect = list->visualItemRect(list->item(row));
            const auto image = list->viewport()->grab(rect).toImage();
            const auto scale = image.devicePixelRatio();
            const int middle = rect.height() / 2;
            const auto background = image.pixelColor(qRound((spacing(Spacing::One) + 2) * scale),
                                                     qRound(middle * scale));
            const int iconLeft = spacing(Spacing::One) + spacing(Spacing::Three);
            const int iconSize = dimension(Dimension::Icon);
            QCOMPARE(image.pixelColor(qRound((iconLeft - 2) * scale), qRound(middle * scale)),
                     background);
            int logoPixels = 0;
            for (int y = middle - iconSize / 2 + 1; y < middle + iconSize / 2; ++y)
                for (int x = iconLeft; x < iconLeft + iconSize; ++x)
                    logoPixels +=
                        image.pixelColor(qRound(x * scale), qRound(y * scale)) != background;
            QVERIFY(logoPixels > 0);
        }
    }
}
