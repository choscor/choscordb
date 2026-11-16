#include "control_style_test.h"
#include "design_system/colors/colors.h"
#include "design_system/icons.h"
#include "design_system/metrics/metrics.h"
#include "design_system/tabs/tabs_style.h"
#include "design_system/theme.h"
#include "design_system/theme_manager.h"
#include <QHoverEvent>
#include <QTabBar>
#include <QVBoxLayout>
#include <QtTest>

void ControlStyleTest::paneTabsUseTheKumoSegmentedTrack() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    QTabBar tabs(&root);
    tabs.addTab("Columns");
    tabs.addTab("Indexes");
    tabs.move(10, 10);
    root.resize(300, 80);
    root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    const auto colors = resolvedThemeForWidget(root).colors;
    QCOMPARE(QFontInfo(tabs.font()).pixelSize(), 14);
    QCOMPARE(tabs.font().weight(), QFont::Medium);
    const auto image = tabs.grab().toImage();
    const auto second = tabs.tabRect(1);
    QCOMPARE(image.pixelColor(second.center().x(), 1), colors.recessed);
    const auto first = tabs.tabRect(0);
    const auto ring = image.pixelColor(first.center().x(), first.top() + spacing(Spacing::Half));
    QVERIFY2(ring.lightness() >= composite(colors.line, colors.recessed).lightness() &&
                 ring.lightness() <= composite(colors.line, colors.base).lightness(),
             qPrintable(ring.name()));
}

void ControlStyleTest::documentTabsUseTheKumoUnderline() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    DocumentTabBar tabs(&root);
    tabs.setProperty("designTabVariant", "document");
    tabs.addTab("orders.sql");
    tabs.addTab("customers.sql");
    tabs.move(10, 10);
    root.resize(500, 80);
    root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    tabs.resize(tabs.sizeHint());
    root.resize(qMax(root.width(), tabs.width() + 20), 80);
    QCoreApplication::processEvents();
    QTest::mouseMove(&root, QPoint(root.width() - 5, 75));
    const auto colors = resolvedThemeForWidget(root).colors;
    QCOMPARE(QFontInfo(tabs.font()).pixelSize(), 14);
    QCOMPARE(tabs.font().weight(), QFont::Medium);
    const auto first = tabs.tabRect(0);
    const auto second = tabs.tabRect(1);
    const auto image = tabs.grab().toImage();
    QCOMPARE(image.pixelColor(first.center().x(), first.bottom()), colors.brand);
    QCOMPARE(image.pixelColor(first.center().x(), first.bottom() - 1), colors.brand);
    QCOMPARE(image.pixelColor(second.center().x(), second.bottom() - 1), colors.base);
    QCOMPARE(image.pixelColor(first.right(), first.top() + 4), colors.base);
    const QPointF hover(second.left() + 3, second.center().y());
    QHoverEvent move(QEvent::HoverMove, hover, tabs.mapToGlobal(hover), QPointF(-1, -1));
    QCoreApplication::sendEvent(&tabs, &move);
    QTRY_COMPARE(tabs.grab().toImage().pixelColor(second.left() + 3, second.top() + 4),
                 colors.tint);
}

void ControlStyleTest::documentTabsFitAnUntitledQueryTitle() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    DocumentTabBar tabs(&root);
    tabs.setProperty("designTabVariant", "document");
    tabs.setTabsClosable(true);
    const QString title("Untitled query 3 •");
    tabs.addTab(themedIcon(Icon::Code, Qt::black, dimension(Dimension::Icon)), title);
    root.resize(600, 80);
    root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    tabs.resize(tabs.sizeHint());
    auto* close = tabs.tabButton(0, QTabBar::RightSide);
    QVERIFY(close);
    const int needed = 2 * spacing(Spacing::Two) + dimension(Dimension::Icon) +
                       2 * spacing(Spacing::One) + close->width() +
                       QFontMetrics(tabs.font()).horizontalAdvance(title);
    QVERIFY2(tabs.tabRect(0).width() >= needed,
             qPrintable(QString("%1 < %2").arg(tabs.tabRect(0).width()).arg(needed)));
}

void ControlStyleTest::documentTabIconsUseTheRegularIconSize() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    DocumentTabBar tabs(&root);
    tabs.setProperty("designTabVariant", "document");
    tabs.addTab(themedIcon(Icon::Table, Qt::black, dimension(Dimension::Icon)), QString());
    root.resize(400, 80);
    root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    tabs.resize(tabs.sizeHint());
    const auto capture = tabs.grab();
    const auto image = capture.toImage();
    const qreal scale = capture.devicePixelRatio();
    const QRect tab = tabs.tabRect(0);
    int left = tab.right(), right = -1;
    for (int y = tab.top(); y < tab.bottom() - 2; ++y)
        for (int x = tab.left(); x <= tab.right(); ++x)
            if (image.pixelColor(QPoint(x, y) * scale).lightness() < 128) {
                left = qMin(left, x);
                right = qMax(right, x);
            }
    QVERIFY2(right - left + 1 >= dimension(Dimension::Icon) - 3,
             qPrintable(QString("icon %1 px").arg(right - left + 1)));
}

void ControlStyleTest::unsavedDocumentTabDotUsesTheBrandColor() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    DocumentTabBar tabs(&root);
    tabs.setProperty("designTabVariant", "document");
    tabs.addTab(QStringLiteral("Untitled query 3 •"));
    root.resize(400, 80);
    root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    tabs.resize(tabs.sizeHint());
    const auto capture = tabs.grab();
    const auto image = capture.toImage();
    const qreal scale = capture.devicePixelRatio();
    const auto brand = resolvedThemeForWidget(tabs).colors.brand;
    const QRect tab = tabs.tabRect(0);
    int brandPixels = 0;
    for (int y = tab.top(); y < tab.bottom() - 2; ++y)
        for (int x = tab.left(); x <= tab.right(); ++x) {
            const auto color = image.pixelColor(QPoint(x, y) * scale);
            if (qAbs(color.hue() - brand.hue()) <= 12 && color.saturation() > 120)
                ++brandPixels;
        }
    QVERIFY2(brandPixels >= 4, qPrintable(QString::number(brandPixels)));
}

void ControlStyleTest::documentTabToneDotUsesTheConnectionTone() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    DocumentTabBar tabs(&root);
    tabs.setProperty("designTabVariant", "document");
    tabs.addTab(QStringLiteral("orders.sql"));
    tabs.addTab(QStringLiteral("customers.sql"));
    root.resize(500, 80);
    root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    tabs.resize(tabs.sizeHint());
    const auto danger = statusToneColor(resolvedThemeForWidget(tabs).colors, StatusTone::Danger);
    const auto tonePixels = [&tabs, &danger](int index) {
        const auto capture = tabs.grab();
        const auto image = capture.toImage();
        const QRect tab = tabs.tabRect(index);
        int count = 0;
        for (int y = tab.top(); y < tab.bottom() - 2; ++y)
            for (int x = tab.left(); x <= tab.right(); ++x) {
                const auto color = image.pixelColor(QPoint(x, y) * capture.devicePixelRatio());
                if (qAbs(color.hue() - danger.hue()) <= 12 && color.saturation() > 120)
                    ++count;
            }
        return count;
    };
    const int untagged = tonePixels(0);
    const int sibling = tonePixels(1);
    tabs.setTabTag(0, StatusTone::Danger, QStringLiteral("Production"));
    QCOMPARE(tabs.tabTagLabel(0), QStringLiteral("Production"));
    QCOMPARE(tabs.tabTone(0), std::optional<StatusTone>(StatusTone::Danger));
    const int tagged = tonePixels(0);
    QVERIFY2(tagged >= untagged + spacing(Spacing::Two),
             qPrintable(QStringLiteral("%1 %2").arg(untagged).arg(tagged)));
    QCOMPARE(tonePixels(1), sibling);
    tabs.setTabTag(0, std::nullopt, {});
    QVERIFY(tabs.tabTagLabel(0).isEmpty());
    QCOMPARE(tonePixels(0), untagged);
}
