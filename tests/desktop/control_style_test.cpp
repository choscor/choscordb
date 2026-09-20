#include "control_style_test.h"
#include "design_system/control_style.h"
#include "design_system/menu/menu.h"
#include "design_system/theme_manager.h"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFontComboBox>
#include <QHeaderView>
#include <QHelpEvent>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStyleOptionButton>
#include <QStyledItemDelegate>
#include <QSvgRenderer>
#include <QTabBar>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

namespace {
class FontProbeDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    mutable QFont::Weight paintedWeight = QFont::Bold;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem effective = option;
        initStyleOption(&effective, index);
        paintedWeight = effective.font.weight();
        QStyledItemDelegate::paint(painter, option, index);
    }
};
void saveNativeSurface(QWidget& widget, const QString& filename) {
    const auto directory = qEnvironmentVariable("CHOSCORDB_TEST_CAPTURE_DIR");
    if (directory.isEmpty() || QGuiApplication::platformName() != "cocoa")
        return;
    QTest::qWait(300); // Let native popup open/close animations reach their final frame.
    const auto topLeft = widget.mapToGlobal(QPoint());
    const auto image =
        widget.screen()->grabWindow(0, topLeft.x(), topLeft.y(), widget.width(), widget.height());
    QVERIFY(!image.isNull());
    QVERIFY(QDir().mkpath(directory));
    QVERIFY(image.save(QDir(directory).filePath(filename)));
}
} // namespace

void ControlStyleTest::menuCheckmarkUsesTheSharedVectorPath() {
    using namespace choscordb::design;
    ControlStyle style;
    QStyleOptionMenuItem option;
    option.rect = QRect(0, 0, 16, 16);
    option.state = QStyle::State_Enabled | QStyle::State_On;
    option.palette.setColor(QPalette::ButtonText, Qt::black);
    QImage actual(32, 32, QImage::Format_ARGB32_Premultiplied);
    actual.setDevicePixelRatio(2);
    actual.fill(Qt::transparent);
    QPainter painter(&actual);
    style.drawPrimitive(QStyle::PE_IndicatorMenuCheckMark, &option, &painter);
    painter.end();
    QImage expected(actual.size(), actual.format());
    expected.setDevicePixelRatio(2);
    expected.fill(Qt::transparent);
    QSvgRenderer svg(
        QByteArray("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
                   "stroke='#000000' stroke-width='1.6' stroke-linecap='round' "
                   "stroke-linejoin='round'><path d='m5 12 4 4L19 6'/></svg>"));
    QPainter reference(&expected);
    svg.render(&reference, QRectF(0, 0, 16, 16));
    reference.end();
    QCOMPARE(actual, expected);
}

void ControlStyleTest::treePointerSelectionDoesNotFrameTheEntireViewport() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    QTreeWidget tree(&root);
    tree.setHeaderHidden(true);
    tree.addTopLevelItem(new QTreeWidgetItem({"Connection"}));
    tree.addTopLevelItem(new QTreeWidgetItem({"Other connection"}));
    tree.resize(300, 180);
    root.resize(340, 220);
    root.show();
    QVERIFY(QTest::qWaitForWindowActive(&root));
    QTest::mouseClick(tree.viewport(), Qt::LeftButton, {},
                      tree.visualItemRect(tree.topLevelItem(0)).center());
    QTRY_VERIFY(tree.hasFocus());
    QCoreApplication::processEvents();
    QCOMPARE(tree.currentItem(), tree.topLevelItem(0));
    const auto image = tree.grab().toImage();
    const auto scale = image.devicePixelRatio();
    QCOMPARE(image.pixelColor(0, qRound(150 * scale)), QColor("#ffffff"));
    saveNativeSurface(tree, "native-tree-pointer-focus.png");
    QTest::keyClick(&tree, Qt::Key_Down);
    QCOMPARE(tree.currentItem(), tree.topLevelItem(1));
}

void ControlStyleTest::comboPopupUsesOneBorderAndFilledSelection() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    QComboBox combo(&root);
    combo.addItems({"PostgreSQL", "SQLite"});
    combo.resize(280, 33);
    root.resize(340, 150);
    root.show();
    QVERIFY(QTest::qWaitForWindowActive(&root));
    combo.showPopup();
    QCoreApplication::processEvents();
    auto* view = combo.view();
    const auto openArrow = combo.grab().toImage();
    combo.hidePopup();
    QCoreApplication::processEvents();
    const auto dpr = openArrow.devicePixelRatio();
    const auto arrowArea = QRect(qRound((combo.width() - 24) * dpr), 0, qRound(24 * dpr),
                                 qRound(combo.height() * dpr));
    QVERIFY(combo.grab().toImage().copy(arrowArea) != openArrow.copy(arrowArea));
    combo.showPopup();
    QCoreApplication::processEvents();
    QCOMPARE(
        view->window()->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, view->window()),
        0);
    const auto viewImage = view->grab().toImage();
    QCOMPARE(viewImage.pixelColor(viewImage.width() - 1, viewImage.height() / 2),
             QColor("#e7ebed"));
    QCOMPARE(viewImage.pixelColor(viewImage.width() / 2, viewImage.height() - 1),
             QColor("#e7ebed"));
    const auto image = view->viewport()->grab().toImage();
    const auto scale = image.devicePixelRatio();
    const auto selected =
        view->visualRect(view->currentIndex()).intersected(view->viewport()->rect());
    QVERIFY(!selected.isEmpty());
    QCOMPARE(image.pixelColor(qRound((selected.right() - 15) * scale),
                              qRound(selected.center().y() * scale)),
             QColor("#ccebdc"));
    saveNativeSurface(*view->window(), "native-combo-popup.png");
    QTest::keyClick(view, Qt::Key_Down);
    QTest::keyClick(view, Qt::Key_Return);
    QCOMPARE(combo.currentText(), QString("SQLite"));
    QVERIFY(!view->isVisible());
    theme.setMode(ThemeMode::Dark);
    theme.applyTo(root);
    combo.showPopup();
    QCoreApplication::processEvents();
    const auto dark = view->viewport()->grab().toImage();
    const auto darkScale = dark.devicePixelRatio();
    const auto darkSelection =
        view->visualRect(view->currentIndex()).intersected(view->viewport()->rect());
    QCOMPARE(dark.pixelColor(qRound((darkSelection.right() - 15) * darkScale),
                             qRound(darkSelection.center().y() * darkScale)),
             QColor("#254b38"));
    saveNativeSurface(*view->window(), "native-combo-popup-dark.png");
    QTest::keyClick(view, Qt::Key_Escape);
    QVERIFY(!view->isVisible());
    QCOMPARE(combo.currentText(), QString("SQLite"));
}

void ControlStyleTest::comboPopupDelegatePaintsNormalWeight() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.applyTo(root);
    QComboBox combo(&root);
    combo.addItems({"PostgreSQL", "SQLite"});
    auto* fontProbe = new FontProbeDelegate(&combo);
    combo.setItemDelegate(fontProbe);
    root.show();
    combo.showPopup();
    combo.view()->viewport()->grab();
    QCOMPARE(fontProbe->paintedWeight, QFont::Normal);
    combo.hidePopup();
}

void ControlStyleTest::appMenusSuppressDuplicateNativeShadows_data() {
    QTest::addColumn<bool>("rtl");
    QTest::newRow("left-to-right") << false;
    QTest::newRow("right-to-left") << true;
}

void ControlStyleTest::appMenusSuppressDuplicateNativeShadows() {
    QFETCH(bool, rtl);
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    root.setGeometry(100, 100, 1200, 700);
    root.show();
    QVERIFY(QTest::qWaitForWindowActive(&root));
    QMenu menu(&root);
    menu.setLayoutDirection(rtl ? Qt::RightToLeft : Qt::LeftToRight);
    menu.addAction("Run query");
    auto* wrap = menu.addAction("Wrap text");
    wrap->setCheckable(true);
    wrap->setChecked(true);
    menu.addAction("Unavailable action")->setEnabled(false);
    menu.addSeparator();
    auto* submenu = menu.addMenu("Export format");
    submenu->addAction("CSV");
    submenu->addAction("JSON");
    menu.popup(root.mapToGlobal(QPoint(400, 80)));
    menu.setActiveAction(submenu->menuAction());
    QTest::keyClick(&menu, rtl ? Qt::Key_Left : Qt::Key_Right);
    QTRY_VERIFY(submenu->isVisible());
    const auto menuImage = menu.grab().toImage();
    const int inset = qRound(detail::menuShadowMargin() * menuImage.devicePixelRatio());
    const QRect panel(inset, inset, menuImage.width() - 2 * inset, menuImage.height() - 2 * inset);
    int strongestOutsideAlpha = 0;
    for (int y = 0; y < menuImage.height(); ++y)
        for (int x = 0; x < menuImage.width(); ++x)
            if (!panel.contains(x, y))
                strongestOutsideAlpha =
                    qMax(strongestOutsideAlpha, menuImage.pixelColor(x, y).alpha());
    QVERIFY2(strongestOutsideAlpha >= 10 && strongestOutsideAlpha <= 30,
             qPrintable(QString("Menu shadow outside panel: %1").arg(strongestOutsideAlpha)));
    QVERIFY(menu.windowFlags().testFlag(Qt::NoDropShadowWindowHint));
    QVERIFY(submenu->windowFlags().testFlag(Qt::NoDropShadowWindowHint));
    saveNativeSurface(root,
                      rtl ? "native-menu-and-submenu-rtl.png" : "native-menu-and-submenu.png");
    const auto paintedBounds = [](QMenu& popup) {
        const auto capture = popup.grab();
        const auto image = capture.toImage();
        QRect opaque;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y).alpha() == 255)
                    opaque = opaque.united(QRect(x, y, 1, 1));
        const auto scale = capture.devicePixelRatio();
        return QRect(
            popup.mapToGlobal(QPoint(qRound(opaque.x() / scale), qRound(opaque.y() / scale))),
            QSize(qRound(opaque.width() / scale), qRound(opaque.height() / scale)));
    };
    const auto parentPanel = paintedBounds(menu);
    const auto childPanel = paintedBounds(*submenu);
    const auto gap = childPanel.center().x() > parentPanel.center().x()
                         ? childPanel.left() - parentPanel.right()
                         : parentPanel.left() - childPanel.right();
    QVERIFY2(gap >= -8 && gap <= 8, qPrintable(QString("Submenu gap: %1px").arg(gap)));
    QCOMPARE(submenu->actionGeometry(submenu->actions().first()).width(), 243);
    submenu->hide();
    menu.hide();
}

void ControlStyleTest::selectArrowUsesForegroundInkAndPreservesPopupInput() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    QComboBox combo(&root);
    combo.addItems({"SQLite", "PostgreSQL"});
    combo.resize(200, 33);
    root.resize(230, 70);
    root.show();
    QCoreApplication::processEvents();
    QStyleOptionComboBox option;
    option.initFrom(&combo);
    option.editable = combo.isEditable();
    option.frame = combo.hasFrame();
    const auto textArea = combo.style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                        QStyle::SC_ComboBoxEditField, &combo);
    const auto buttonArea = combo.style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                          QStyle::SC_ComboBoxArrow, &combo);
    QVERIFY(textArea.right() >= buttonArea.left() - 12);
    const auto capture = combo.grab();
    const auto image = capture.toImage();
    const auto scale = capture.devicePixelRatio();
    QColor darkest(Qt::white);
    int right = -1;
    for (int y = qRound(8 * scale); y < qRound(25 * scale); ++y)
        for (int x = qRound((combo.width() - 24) * scale); x < qRound((combo.width() - 2) * scale);
             ++x) {
            const auto pixel = image.pixelColor(x, y);
            if (pixel.lightness() < darkest.lightness())
                darkest = pixel;
            if (pixel.lightness() < 150)
                right = qMax(right, x);
        }
    QVERIFY2(darkest.lightness() < 75, qPrintable(darkest.name()));
    QVERIFY(right >= 0);
    QTest::mouseClick(&combo, Qt::LeftButton, {}, QPoint(combo.width() - 10, 16));
    QVERIFY(combo.view()->isVisible());
    combo.hidePopup();
    combo.setEnabled(false);
    QVERIFY(combo.grab().toImage() != image);
    QTest::mouseClick(&combo, Qt::LeftButton, {}, QPoint(combo.width() - 10, 16));
    QVERIFY(!combo.view()->isVisible());
    QCOMPARE(combo.currentText(), QString("SQLite"));
}

void ControlStyleTest::menuPanelHasReferenceMinimumWidthOutsideItsShadow() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    QMenu menu(&root);
    auto* action = menu.addAction("Copy");
    QSignalSpy triggered(action, &QAction::triggered);
    menu.popup(QPoint(100, 100));
    QCoreApplication::processEvents();
    const auto capture = menu.grab();
    const auto image = capture.toImage();
    const auto scale = capture.devicePixelRatio();
    const auto y = qRound(menu.actionGeometry(action).center().y() * scale);
    int first = image.width(), last = -1;
    for (int x = 0; x < image.width(); ++x)
        if (image.pixelColor(x, y).alpha() == 255) {
            first = qMin(first, x);
            last = qMax(last, x);
        }
    QCOMPARE(qRound((last - first + 1) / scale), 255);
    QTest::keyClick(&menu, Qt::Key_Down);
    QTest::keyClick(&menu, Qt::Key_Return);
    QCOMPARE(triggered.count(), 1);
}

void ControlStyleTest::paneTabsPaintReferenceInsetsWithoutMovingDocumentTabs() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    QTabBar tabs(&root);
    tabs.setExpanding(false);
    tabs.addTab("Columns");
    tabs.addTab("Indexes");
    tabs.resize(320, 35);
    root.resize(340, 80);
    root.show();
    QCoreApplication::processEvents();
    const auto capture = tabs.grab();
    const auto image = capture.toImage();
    const auto scale = capture.devicePixelRatio();
    QCOMPARE(image.pixelColor(qRound(4 * scale), qRound(12 * scale)), QColor("#f2f2f2"));
    QCOMPARE(image.pixelColor(qRound(10 * scale), qRound(12 * scale)), QColor("#ffffff"));
    QCOMPARE(tabs.height(), 35);
    QTest::mouseClick(&tabs, Qt::LeftButton, {}, tabs.tabRect(1).center());
    QCOMPARE(tabs.currentIndex(), 1);
    tabs.setProperty("designTabVariant", "document");
    tabs.style()->unpolish(&tabs);
    tabs.style()->polish(&tabs);
    tabs.setCurrentIndex(0);
    tabs.resize(320, 33);
    const auto document = tabs.grab().toImage();
    QCOMPARE(document.pixelColor(qRound(4 * scale), qRound(12 * scale)), QColor("#ffffff"));
    QCOMPARE(tabs.height(), 33);
}

void ControlStyleTest::documentOverflowDoesNotPaintTornEdges() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.applyTo(root);
    QTabBar tabs(&root);
    tabs.setProperty("designTabVariant", "document");
    tabs.setExpanding(false);
    tabs.setTabsClosable(true);
    for (int i = 0; i < 8; ++i)
        tabs.addTab(QString("History query %1").arg(i));
    tabs.resize(350, 33);
    root.resize(350, 60);
    root.show();
    tabs.setCurrentIndex(7);
    QCoreApplication::processEvents();
    QVERIFY(tabs.findChild<QToolButton*>("ScrollLeftButton")->isVisible());
    QStyleOptionTab option;
    option.initFrom(&tabs);
    option.rect = QRect(0, 0, 8, 33);
    for (auto edge : {QStyle::PE_IndicatorTabTearLeft, QStyle::PE_IndicatorTabTearRight}) {
        QImage image(8, 33, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        tabs.style()->drawPrimitive(edge, &option, &painter, &tabs);
        painter.end();
        QImage clear(image.size(), image.format());
        clear.fill(Qt::transparent);
        QCOMPARE(image, clear);
    }
}

void ControlStyleTest::closableDocumentTabsUseCompactHeight() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.applyTo(root);
    QTabBar tabs(&root);
    tabs.setProperty("designTabVariant", "document");
    tabs.setExpanding(false);
    tabs.setTabsClosable(true);
    tabs.addTab("Query · modified");
    tabs.addTab("A much longer history query title");
    root.show();
    QCoreApplication::processEvents();
    QCOMPARE(tabs.sizeHint().height(), 33);
    QCOMPARE(tabs.tabRect(0).height(), 33);
    const auto side = static_cast<QTabBar::ButtonPosition>(
        tabs.style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition));
    auto* close = tabs.tabButton(0, side);
    QVERIFY(close);
    QVERIFY(tabs.tabRect(0).contains(close->geometry()));
    QCOMPARE(tabs.tabRect(0).width(), tabs.tabRect(1).width());
    QVERIFY2(tabs.tabRect(0).width() <= 118, qPrintable(QString::number(tabs.tabRect(0).width())));
}

void ControlStyleTest::inputTrackingResetsBodyTrackingAndRemainsEditable() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.applyTo(root);
    QLineEdit field(&root);
    field.ensurePolished();
    QVERIFY2(root.font().letterSpacing() < 0,
             qPrintable(QString::number(root.font().letterSpacing())));
    QCOMPARE(field.font().letterSpacing(), 0.0);
    QCOMPARE(field.font().pixelSize(), 12);
    QCOMPARE(field.font().weight(), QFont::Medium);
    QTest::keyClicks(&field, "Connection");
    QCOMPARE(field.text(), QString("Connection"));
}

void ControlStyleTest::referenceSwitchMovesItsThumbAndKeepsKeyboardSemantics() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    QCheckBox toggle("Save query history", &root);
    toggle.setProperty("designRole", "switch");
    toggle.resize(220, 32);
    root.resize(240, 60);
    root.show();
    toggle.clearFocus();
    QStyleOptionButton option;
    option.initFrom(&toggle);
    auto indicator = toggle.style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, &toggle);
    QCOMPARE(indicator.size(), QSize(32, 19));
    const auto unchecked = toggle.grab().toImage();
    QCOMPARE(unchecked.pixelColor(indicator.left() + 9, indicator.top() + 9), QColor(Qt::white));
    QTest::keyClick(&toggle, Qt::Key_Space);
    QVERIFY(toggle.isChecked());
    toggle.clearFocus();
    const auto checked = toggle.grab().toImage();
    QCOMPARE(checked.pixelColor(indicator.left() + 9, indicator.top() + 9), QColor("#287f66"));
    QCOMPARE(checked.pixelColor(indicator.left() + 22, indicator.top() + 9), QColor(Qt::white));
    toggle.setEnabled(false);
    QTest::keyClick(&toggle, Qt::Key_Space);
    QVERIFY(toggle.isChecked());
}

void ControlStyleTest::tabCloseUsesNeutralGlyphAndStillDispatches() {
    using namespace choscordb::design;
    QWidget host;
    ThemeManager theme;
    theme.applyTo(host);
    QTabBar tabs(&host);
    tabs.setTabsClosable(true);
    tabs.addTab("Query");
    tabs.resize(180, 32);
    host.show();
    const auto side = static_cast<QTabBar::ButtonPosition>(
        tabs.style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition));
    auto* close = tabs.tabButton(0, side);
    QVERIFY(close);
    const auto image = close->grab().toImage();
    int ink = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const auto pixel = image.pixelColor(x, y);
            QVERIFY(qAbs(pixel.red() - pixel.green()) < 10);
            if (pixel.lightness() < 180)
                ++ink;
        }
    QVERIFY(ink > 8);
    QSignalSpy requested(&tabs, &QTabBar::tabCloseRequested);
    QTest::mouseClick(close, Qt::LeftButton);
    QCOMPARE(requested.count(), 1);
    QCOMPARE(requested.at(0).at(0).toInt(), 0);
}

void ControlStyleTest::standardConfirmationVariantsKeepSafeDefaultDistinct() {
    using namespace choscordb::design;
    QWidget host;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(host);
    QPushButton destructive("Delete", &host);
    destructive.setProperty("variant", "destructive");
    destructive.resize(120, 32);
    auto tint = destructive.grab().toImage().pixelColor(60, 5);
    QVERIFY(tint.red() > tint.green() + 15);
    QPushButton cancel("Cancel", &host);
    cancel.setProperty("variant", "outline");
    cancel.setDefault(true);
    cancel.resize(120, 32);
    QVERIFY(cancel.grab().toImage().pixelColor(60, 5).lightness() > 230);
}

void ControlStyleTest::scopedSelectAndSpinRenderArrows_data() {
    QTest::addColumn<bool>("dark");
    QTest::newRow("light") << false;
    QTest::newRow("dark") << true;
}

void ControlStyleTest::scopedSelectAndSpinRenderArrows() {
    QFETCH(bool, dark);
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(dark ? ThemeMode::Dark : ThemeMode::Light);
    theme.applyTo(root);
    QComboBox combo(&root);
    combo.addItems({"SQLite", "PostgreSQL"});
    combo.setGeometry(10, 10, 180, 32);
    QSpinBox spin(&root);
    spin.setRange(0, 10);
    spin.setValue(5);
    spin.setGeometry(10, 60, 180, 64);
    root.resize(220, 140);
    root.show();
    QApplication::processEvents();
    auto glyphPixels = [dark](const QImage& image, const QRect& rectangle) {
        int count = 0;
        for (int y = rectangle.top(); y <= rectangle.bottom(); ++y) {
            for (int x = rectangle.left(); x <= rectangle.right(); ++x) {
                const auto color = image.pixelColor(x, y);
                const auto luminance = color.lightness();
                if (((!dark && luminance < 210) || (dark && luminance > 70)) &&
                    qAbs(color.red() - color.green()) < 20 &&
                    qAbs(color.blue() - color.green()) < 20) {
                    ++count;
                }
            }
        }
        return count;
    };
    QStyleOptionComboBox comboOption;
    comboOption.initFrom(&combo);
    const auto comboArrow = combo.style()->subControlRect(QStyle::CC_ComboBox, &comboOption,
                                                          QStyle::SC_ComboBoxArrow, &combo);
    QVERIFY(glyphPixels(combo.grab().toImage(), comboArrow.adjusted(4, 4, -4, -4)) >= 8);
    QStyleOptionSpinBox spinOption;
    spinOption.initFrom(&spin);
    const auto up =
        spin.style()->subControlRect(QStyle::CC_SpinBox, &spinOption, QStyle::SC_SpinBoxUp, &spin);
    const auto down = spin.style()->subControlRect(QStyle::CC_SpinBox, &spinOption,
                                                   QStyle::SC_SpinBoxDown, &spin);
    QCOMPARE(spin.font().weight(), QFont::Normal);
    QVERIFY(down.top() > up.bottom());
    QVERIFY(down.center().y() - up.center().y() <= 18);
    QVERIFY(glyphPixels(spin.grab().toImage(), up.adjusted(2, 2, -2, -2)) >= 5);
    QVERIFY(glyphPixels(spin.grab().toImage(), down.adjusted(2, 2, -2, -2)) >= 5);
    QTest::mouseClick(&spin, Qt::LeftButton, {}, up.center());
    QCOMPARE(spin.value(), 6);
    QTest::mouseClick(&combo, Qt::LeftButton, {}, comboArrow.center());
    QVERIFY(combo.view()->isVisible());
    combo.hidePopup();
    combo.setEditable(true);
    combo.resize(210, 32);
    comboOption.initFrom(&combo);
    comboOption.editable = true;
    const auto resizedArrow = combo.style()->subControlRect(QStyle::CC_ComboBox, &comboOption,
                                                            QStyle::SC_ComboBoxArrow, &combo);
    QVERIFY(glyphPixels(combo.grab().toImage(), resizedArrow.adjusted(4, 4, -4, -4)) >= 8);
    const auto enabledUp = spin.grab().toImage().copy(up.adjusted(2, 2, -2, -2));
    spin.setValue(spin.maximum());
    const auto disabledUp = spin.grab().toImage().copy(up.adjusted(2, 2, -2, -2));
    QVERIFY(disabledUp != enabledUp);
    spin.setWrapping(true);
    QCOMPARE(spin.grab().toImage().copy(up.adjusted(2, 2, -2, -2)), enabledUp);
    spin.setReadOnly(true);
    QCOMPARE(spin.grab().toImage().copy(up.adjusted(2, 2, -2, -2)), disabledUp);
    spin.setButtonSymbols(QAbstractSpinBox::NoButtons);
    QCOMPARE(glyphPixels(spin.grab().toImage(), QRect(up.center() - QPoint(4, 4), QSize(8, 8))), 0);
    QFontComboBox fonts(&root);
    fonts.setGeometry(10, 100, 180, 32);
    fonts.show();
    QStyleOptionComboBox fontOption;
    fontOption.initFrom(&fonts);
    fontOption.editable = true;
    const auto fontArrow = fonts.style()->subControlRect(QStyle::CC_ComboBox, &fontOption,
                                                         QStyle::SC_ComboBoxArrow, &fonts);
    QVERIFY(glyphPixels(fonts.grab().toImage(), fontArrow.adjusted(4, 4, -4, -4)) >= 8);
    const auto beforeThemeChange = combo.grab().toImage().copy(resizedArrow);
    theme.setMode(dark ? ThemeMode::Light : ThemeMode::Dark);
    theme.applyTo(root);
    QApplication::processEvents();
    QVERIFY(combo.grab().toImage().copy(resizedArrow) != beforeThemeChange);
    QCOMPARE(combo.findChildren<QWidget*>("designControlGlyphs", Qt::FindDirectChildrenOnly).size(),
             1);
}

void ControlStyleTest::scopedCheckboxUsesSemanticFill_data() {
    QTest::addColumn<bool>("dark");
    QTest::addColumn<bool>("mixed");
    QTest::addColumn<QColor>("expected");
    QTest::newRow("light-checked") << false << false << QColor("#287f66");
    QTest::newRow("light-mixed") << false << true << QColor("#287f66");
    QTest::newRow("dark-checked") << true << false << QColor("#65b493");
    QTest::newRow("dark-mixed") << true << true << QColor("#65b493");
}

void ControlStyleTest::scopedCheckboxUsesSemanticFill() {
    QFETCH(bool, dark);
    QFETCH(bool, mixed);
    QFETCH(QColor, expected);
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(dark ? ThemeMode::Dark : ThemeMode::Light);
    theme.applyTo(root);
    QCheckBox checkbox("Scoped checkbox", &root);
    checkbox.setTristate(true);
    checkbox.setCheckState(mixed ? Qt::PartiallyChecked : Qt::Checked);
    checkbox.resize(200, 32);
    root.resize(220, 60);
    root.show();
    checkbox.clearFocus();
    QStyleOptionButton option;
    option.initFrom(&checkbox);
    const auto indicator =
        checkbox.style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, &checkbox);
    const auto image = checkbox.grab().toImage();
    QCOMPARE(image.pixelColor(indicator.center().x(), indicator.top() + 3), expected);
}

QTEST_MAIN(ControlStyleTest)
