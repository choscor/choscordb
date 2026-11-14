#include "control_style_test.h"
#include "design_system/colors/colors.h"
#include "design_system/control_style.h"
#include "design_system/menu/menu.h"
#include "design_system/metrics/metrics.h"
#include "design_system/theme.h"
#include "design_system/theme_manager.h"
#include "ink_checks.h"
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QStyle>
#include <QVBoxLayout>
#include <QtTest>

void ControlStyleTest::fieldsUseTheKumoInputSurface() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Dark);
    theme.applyTo(root);
    auto* layout = new QVBoxLayout(&root);
    auto* field = new QLineEdit(&root);
    auto* area = new QPlainTextEdit(&root);
    auto* select = new QComboBox(&root);
    select->addItem("PostgreSQL");
    layout->addWidget(field);
    layout->addWidget(area);
    layout->addWidget(select);
    root.resize(320, 260);
    root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    const auto colors = resolvedThemeForWidget(root).colors;
    const auto pixel = [](QWidget* widget, QPoint point) {
        return widget->grab().toImage().pixelColor(point);
    };
    QCOMPARE(QFontInfo(field->font()).pixelSize(), 14);
    QCOMPARE(QFontInfo(select->font()).pixelSize(), 14);
    QCOMPARE(pixel(field, QPoint(field->width() / 2, field->height() / 2)), colors.control);
    QCOMPARE(pixel(area, QPoint(area->width() / 2, area->height() / 2)), colors.control);
    QCOMPARE(pixel(select, QPoint(select->width() / 3, select->height() / 2)), colors.control);
    field->setFocus(Qt::MouseFocusReason);
    QTRY_VERIFY(field->hasFocus());
    QVERIFY(pixel(field, QPoint(field->width() / 2, 0)) != colors.line);
    field->clearFocus();
    QCOMPARE(pixel(field, QPoint(field->width() / 2, 0)), colors.line);
    QTest::mouseMove(field, field->rect().center());
    QCOMPARE(pixel(field, QPoint(field->width() / 2, 0)), colors.line);
    QTest::mouseMove(select, select->rect().center());
    QTRY_COMPARE(pixel(select, QPoint(select->width() / 3, select->height() / 2)), colors.tint);
    field->setProperty("invalid", true);
    field->style()->unpolish(field);
    field->style()->polish(field);
    QCOMPARE(pixel(field, QPoint(field->width() / 2, 0)), colors.danger);
    select->showPopup();
    QTRY_VERIFY(select->view()->isVisible());
    const auto popup = select->view()->grab().toImage();
    QCOMPARE(popup.pixelColor(popup.width() - 4, popup.height() - 4), colors.control);
    select->hidePopup();
}

void ControlStyleTest::selectShowsTheSubtleKumoUpDownCaret() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    auto* layout = new QVBoxLayout(&root);
    auto* select = new QComboBox(&root);
    select->addItem("PostgreSQL");
    layout->addWidget(select);
    root.resize(240, 80);
    root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    root.setFocus();
    const auto colors = resolvedThemeForWidget(root).colors;
    const auto image = root.grab(select->geometry()).toImage();
    const int right = select->width() - spacing(Spacing::Three);
    const int left = right - dimension(Dimension::Icon);
    const int middle = select->height() / 2;
    int above = 0;
    int below = 0;
    int darker = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = left; x < right; ++x)
            if (image.pixelColor(x, y).lightness() < colors.textSubtle.lightness() - 24)
                ++darker;
            else if (choscordb::test::blendedInk(image.pixelColor(x, y), colors.textSubtle,
                                                 colors.control))
                ++(y < middle ? above : below);
    QCOMPARE(darker, 0);
    QVERIFY2(above > 4 && below > 4, qPrintable(QStringLiteral("%1 %2").arg(above).arg(below)));
    select->showPopup();
    QTRY_VERIFY(select->view()->isVisible());
    QCOMPARE(select->view()->viewport()->geometry().top(), 1 + spacing(Spacing::OneHalf));
    const auto popup = select->view()->grab().toImage();
    QCOMPARE(popup.pixelColor(popup.width() / 2, 0), composite(colors.line, colors.base));
    select->hidePopup();
}

void ControlStyleTest::checksAndRadiosKeepTheKumoLabelGap() {
    using namespace choscordb::design;
    ControlStyle style;
    QCOMPARE(style.pixelMetric(QStyle::PM_CheckBoxLabelSpacing), spacing(Spacing::Two));
    QCOMPARE(style.pixelMetric(QStyle::PM_RadioButtonLabelSpacing), spacing(Spacing::Two));
}

void ControlStyleTest::darkSwitchUsesTheKumoPaletteTrackAndThumb() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Dark);
    theme.applyTo(root);
    QCheckBox toggle("Save query history", &root);
    toggle.setProperty("designRole", "switch");
    toggle.resize(220, 32);
    root.resize(240, 60);
    root.show();
    toggle.clearFocus();
    QStyleOptionButton option;
    option.initFrom(&toggle);
    const auto indicator =
        toggle.style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, &toggle);
    const auto off = toggle.grab().toImage();
    QCOMPARE(off.pixelColor(indicator.left() + 9, indicator.top() + 9), QColor("#1f1f1f"));
    QCOMPARE(off.pixelColor(indicator.left() + 27, indicator.top() + 9), QColor("#404040"));
    toggle.setChecked(true);
    const auto on = toggle.grab().toImage();
    QCOMPARE(on.pixelColor(indicator.left() + 9, indicator.top() + 9), QColor("#155dfc"));
    QCOMPARE(on.pixelColor(indicator.left() + 27, indicator.top() + 9), QColor("#8ec5ff"));
}

void ControlStyleTest::menusUseTheKumoDropdownPopup() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Dark);
    theme.applyTo(root);
    QMenu menu(&root);
    menu.addAction("Copy");
    auto* disabled = menu.addAction("Paste");
    disabled->setEnabled(false);
    root.resize(300, 300);
    root.show();
    menu.popup(root.mapToGlobal(QPoint(20, 20)));
    QTRY_VERIFY(menu.isVisible());
    const auto colors = resolvedThemeForWidget(root).colors;
    const int margin = detail::menuShadowMargin();
    const auto image = menu.grab().toImage();
    const auto copy = menu.actionGeometry(menu.actions().first());
    QCOMPARE(copy.top(), margin + 1 + spacing(Spacing::OneHalf));
    QCOMPARE(image.pixelColor(copy.right() - 2, copy.center().y()), colors.control);
    QCOMPARE(image.pixelColor(image.width() / 2, margin), colors.line);
    QCOMPARE(QFontInfo(menu.font()).pixelSize(), 14);
    menu.close();
}

void ControlStyleTest::tooltipPointsTheKumoArrowAtItsOwner() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.applyTo(root);
    auto* field = new QLineEdit(&root);
    field->setToolTip("Synthetic tooltip");
    field->setGeometry(40, 160, 120, 32);
    root.resize(320, 240);
    root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    QHelpEvent help(QEvent::ToolTip, QPoint(10, 10), field->mapToGlobal(QPoint(10, 10)));
    QApplication::sendEvent(field, &help);
    QApplication::processEvents();
    auto* tooltip = root.findChild<QWidget*>("designTooltip");
    QVERIFY(tooltip);
    QVERIFY(tooltip->isVisible());
    const auto image = tooltip->grab().toImage();
    const auto lowestOpaqueRow = [&image](int x) {
        int lowest = -1;
        for (int y = 0; y < image.height(); ++y)
            if (image.pixelColor(x, y).alpha() == 255)
                lowest = y;
        return lowest;
    };
    const int tip = field->x() + field->width() / 2 - tooltip->x();
    const int body = lowestOpaqueRow(tip + 30);
    QVERIFY(body > 0);
    QVERIFY(lowestOpaqueRow(tip) - body >= 4);
    QVERIFY(lowestOpaqueRow(tip) - body <= 5);
    QCOMPARE(image.pixelColor(tip, body + 3), resolvedThemeForWidget(root).colors.base);
    QVERIFY(field->y() - tooltip->y() - lowestOpaqueRow(tip) >= 3);
}
