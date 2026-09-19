#include "design_system/control_style.h"
#include "design_system/menu/menu.h"
#include "design_system/theme_manager.h"

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
#include <QMenu>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStyleOptionButton>
#include <QSvgRenderer>
#include <QTabBar>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

namespace {
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

class ControlStyleTest final : public QObject {
    Q_OBJECT
  private slots:
    void menuCheckmarkUsesTheSharedVectorPath() {
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
    void treePointerSelectionDoesNotFrameTheEntireViewport() {
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
    void comboPopupUsesOneBorderAndFilledSelection() {
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
        const auto image = view->viewport()->grab().toImage();
        const auto scale = image.devicePixelRatio();
        const auto selected =
            view->visualRect(view->currentIndex()).intersected(view->viewport()->rect());
        QVERIFY(!selected.isEmpty());
        QCOMPARE(image.pixelColor(qRound((selected.right() - 15) * scale),
                                  qRound(selected.center().y() * scale)),
                 QColor("#eaf4ef"));
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
                 QColor("#283e34"));
        saveNativeSurface(*view->window(), "native-combo-popup-dark.png");
        QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(!view->isVisible());
        QCOMPARE(combo.currentText(), QString("SQLite"));
    }
    void appMenusSuppressDuplicateNativeShadows_data() {
        QTest::addColumn<bool>("rtl");
        QTest::newRow("left-to-right") << false;
        QTest::newRow("right-to-left") << true;
    }
    void appMenusSuppressDuplicateNativeShadows() {
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
    void selectArrowUsesForegroundInkAndPreservesPopupInput() {
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
        const auto capture = combo.grab();
        const auto image = capture.toImage();
        const auto scale = capture.devicePixelRatio();
        QColor darkest(Qt::white);
        int right = -1;
        for (int y = qRound(8 * scale); y < qRound(25 * scale); ++y)
            for (int x = qRound((combo.width() - 24) * scale);
                 x < qRound((combo.width() - 2) * scale); ++x) {
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
    void menuPanelHasReferenceMinimumWidthOutsideItsShadow() {
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
    void paneTabsPaintReferenceInsetsWithoutMovingDocumentTabs() {
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
        QCOMPARE(image.pixelColor(qRound(4 * scale), qRound(12 * scale)), QColor("#f2f5f4"));
        QCOMPARE(image.pixelColor(qRound(10 * scale), qRound(12 * scale)), QColor("#ffffff"));
        QCOMPARE(tabs.height(), 35);
        QTest::mouseClick(&tabs, Qt::LeftButton, {}, tabs.tabRect(1).center());
        QCOMPARE(tabs.currentIndex(), 1);
        tabs.setProperty("designTabVariant", "document");
        tabs.style()->unpolish(&tabs);
        tabs.style()->polish(&tabs);
        tabs.setCurrentIndex(0);
        tabs.resize(320, 39);
        const auto document = tabs.grab().toImage();
        QCOMPARE(document.pixelColor(qRound(4 * scale), qRound(12 * scale)), QColor("#ffffff"));
        QCOMPARE(tabs.height(), 39);
    }
    void inputTrackingResetsBodyTrackingAndRemainsEditable() {
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
    void referenceSwitchMovesItsThumbAndKeepsKeyboardSemantics() {
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
        auto indicator =
            toggle.style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, &toggle);
        QCOMPARE(indicator.size(), QSize(32, 19));
        const auto unchecked = toggle.grab().toImage();
        QCOMPARE(unchecked.pixelColor(indicator.left() + 9, indicator.top() + 9),
                 QColor(Qt::white));
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
    void tabCloseUsesNeutralGlyphAndStillDispatches() {
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
    void standardConfirmationVariantsKeepSafeDefaultDistinct() {
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
    void scopedSelectAndSpinRenderArrows_data() {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void scopedSelectAndSpinRenderArrows() {
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
        spin.setGeometry(10, 60, 180, 32);
        root.resize(220, 120);
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
        const auto up = spin.style()->subControlRect(QStyle::CC_SpinBox, &spinOption,
                                                     QStyle::SC_SpinBoxUp, &spin);
        const auto down = spin.style()->subControlRect(QStyle::CC_SpinBox, &spinOption,
                                                       QStyle::SC_SpinBoxDown, &spin);
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
        QCOMPARE(glyphPixels(spin.grab().toImage(), QRect(up.center() - QPoint(4, 4), QSize(8, 8))),
                 0);
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
        QCOMPARE(
            combo.findChildren<QWidget*>("designControlGlyphs", Qt::FindDirectChildrenOnly).size(),
            1);
    }
    void scopedCheckboxUsesSemanticFill_data() {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<bool>("mixed");
        QTest::addColumn<QColor>("expected");
        QTest::newRow("light-checked") << false << false << QColor("#287f66");
        QTest::newRow("light-mixed") << false << true << QColor("#287f66");
        QTest::newRow("dark-checked") << true << false << QColor("#65b493");
        QTest::newRow("dark-mixed") << true << true << QColor("#65b493");
    }
    void scopedCheckboxUsesSemanticFill() {
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
    void scopedDarkButtonsAndTextSelectionUseSemanticColors() {
        using namespace choscordb::design;
        QWidget root;
        ThemeManager theme;
        theme.setMode(ThemeMode::Dark);
        theme.applyTo(root);
        QPushButton button("MMMM", &root);
        button.resize(120, 32);
        const auto image = button.grab().toImage();
        QCOMPARE(button.height(), 33);
        int brightText = 0;
        for (int y = 6; y < 26; ++y)
            for (int x = 15; x < 105; ++x)
                if (image.pixelColor(x, y).lightness() > 180)
                    ++brightText;
        QVERIFY(brightText > 20);
        QLineEdit field("MMMM", &root);
        field.setGeometry(0, 40, 200, 32);
        root.resize(300, 120);
        root.show();
        root.activateWindow();
        field.setFocus();
        field.selectAll();
        QCoreApplication::processEvents();
        QCOMPARE(field.palette().color(QPalette::Active, QPalette::Highlight), QColor("#65b493"));
        const auto selected = field.grab().toImage();
        int selectedInk = 0;
        for (int y = 8; y < 24; ++y)
            for (int x = 18; x < 42; ++x)
                if (selected.pixelColor(x, y).lightness() < 100)
                    ++selectedInk;
        QVERIFY(selectedInk > 10);
    }
    void navigationRowsUseTheSeparateReferenceSize() {
        using namespace choscordb::design;
        QWidget root;
        ThemeManager theme;
        theme.applyTo(root);
        QTreeWidget tree(&root);
        tree.setHeaderHidden(true);
        tree.addTopLevelItem(new QTreeWidgetItem(QStringList{"Synthetic connection"}));
        tree.addTopLevelItem(new QTreeWidgetItem(QStringList{"Second connection"}));
        tree.resize(300, 120);
        root.show();
        QCoreApplication::processEvents();
        QCOMPARE(tree.visualItemRect(tree.topLevelItem(0)).height(), 33);
        tree.setCurrentItem(tree.topLevelItem(0));
        const auto row = tree.visualItemRect(tree.topLevelItem(0));
        QCOMPARE(tree.viewport()->grab().toImage().pixelColor(row.right() - 12, row.center().y()),
                 QColor("#eaf4ef"));
        QTest::keyClick(&tree, Qt::Key_Down);
        QCOMPARE(tree.currentItem()->text(0), QString("Second connection"));
        const auto secondRow = tree.visualItemRect(tree.currentItem());
        QCOMPARE(tree.viewport()->grab().toImage().pixelColor(secondRow.right() - 12,
                                                              secondRow.center().y()),
                 QColor("#eaf4ef"));
    }
    void validationStateUpdatesAnAlreadyVisibleField() {
        using namespace choscordb::design;
        QWidget root;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(root);
        QLineEdit field("Value", &root);
        field.setGeometry(40, 40, 200, 32);
        root.resize(300, 120);
        root.show();
        root.activateWindow();
        QCoreApplication::processEvents();
        field.clearFocus();
        field.setFocus(Qt::TabFocusReason);
        QCoreApplication::processEvents();
        QCOMPARE(root.grab().toImage().pixelColor(38, 55), QColor("#287f66"));
        const auto original = field.grab().toImage().pixelColor(0, 16);
        field.setProperty("invalid", true);
        QCoreApplication::processEvents();
        QCOMPARE(field.grab().toImage().pixelColor(0, 16), QColor("#c45d58"));
        QCOMPARE(root.grab().toImage().pixelColor(38, 55), QColor("#c45d58"));
        field.setProperty("invalid", false);
        QCoreApplication::processEvents();
        QCOMPARE(field.grab().toImage().pixelColor(0, 16), original);
        QCOMPARE(root.grab().toImage().pixelColor(38, 55), QColor("#287f66"));
    }
    void tableRowsHonorReferenceLineBoxAndPadding() {
        using namespace choscordb::design;
        QWidget root;
        ThemeManager theme;
        theme.applyTo(root);
        QTableWidget table(3, 2, &root);
        table.ensurePolished();
        QCOMPARE(table.verticalHeader()->defaultSectionSize(), 29);
    }
    void unusedHeaderGutterUsesThemeSurface_data() {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<QColor>("expected");
        QTest::newRow("light") << false << QColor("#f6f7f8");
        QTest::newRow("dark") << true << QColor("#171d20");
    }
    void unusedHeaderGutterUsesThemeSurface() {
        QFETCH(bool, dark);
        QFETCH(QColor, expected);
        using namespace choscordb::design;
        const auto appearance = dark ? ResolvedAppearance::Dark : ResolvedAppearance::Light;
        const ResolvedTheme theme{appearance, resolveColors(appearance, {}), false};
        QWidget root;
        root.setStyleSheet(controlStyleSheet(theme));
        QTableWidget table(1, 1, &root);
        table.setHorizontalHeaderLabels({"Value"});
        table.resize(240, 200);
        root.resize(260, 220);
        root.show();
        QApplication::processEvents();
        auto* header = table.verticalHeader();
        const auto image = header->grab().toImage();
        QCOMPARE(image.pixelColor(image.width() / 2, image.height() - 10), expected);
    }
    void unbrokenTooltipWrapsAndOwnerDestructionDismissesIt() {
        QWidget root;
        auto* field = new QLineEdit(&root);
        const QString identifier(120, QChar('M'));
        field->setToolTip(identifier);
        field->setGeometry(20, 200, 180, 32);
        root.resize(240, 260);
        root.show();
        QHelpEvent help(QEvent::ToolTip, QPoint(10, 10), field->mapToGlobal(QPoint(10, 10)));
        QApplication::sendEvent(field, &help);
        QApplication::processEvents();
        auto* tooltip = root.findChild<QWidget*>("designTooltip");
        QVERIFY(tooltip != nullptr);
        QVERIFY(tooltip->isVisible());
        QVERIFY(tooltip->width() <= 320);
        QVERIFY(tooltip->height() >= 66);
        QCOMPARE(tooltip->accessibleName(), identifier);
        const auto image = tooltip->grab().toImage();
        int lowerTextPixels = 0;
        for (int y = 40; y < image.height() - 12; ++y) {
            for (int x = 12; x < image.width() - 12; ++x) {
                if (image.pixelColor(x, y).lightness() > 128) {
                    ++lowerTextPixels;
                }
            }
        }
        QVERIFY(lowerTextPixels > 20);
        delete field;
        QVERIFY(!tooltip->isVisible());
    }
    void actualTooltipHasArrowAndDismissesOnOwnerLeave() {
        QWidget root;
        QLineEdit field(&root);
        field.setToolTip("Synthetic tooltip");
        field.setGeometry(20, 70, 180, 32);
        root.resize(240, 140);
        root.show();
        QHelpEvent help(QEvent::ToolTip, QPoint(10, 10), field.mapToGlobal(QPoint(10, 10)));
        QApplication::sendEvent(&field, &help);
        QApplication::processEvents();
        auto* tooltip = root.findChild<QWidget*>("designTooltip");
        QVERIFY(tooltip != nullptr);
        QVERIFY(tooltip->isVisible());
        const auto image = tooltip->grab().toImage();
        QVERIFY(image.pixelColor(image.width() / 2, image.height() - 3).alpha() > 0);
        QCOMPARE(image.pixelColor(2, image.height() - 3).alpha(), 0);
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(&field, &leave);
        QVERIFY(!tooltip->isVisible());
    }
    void menuHasRenderedTranslucentElevationOutsidePanel() {
        using namespace choscordb::design;
        const ResolvedTheme theme{ResolvedAppearance::Light,
                                  resolveColors(ResolvedAppearance::Light, {}), false};
        QWidget root;
        root.setStyleSheet(controlStyleSheet(theme));
        QMenu menu(&root);
        menu.addAction("Copy synthetic value");
        menu.popup(QPoint(50, 50));
        QApplication::processEvents();
        QVERIFY(menu.isVisible());
        const auto image = menu.grab().toImage();
        QCOMPARE(image.pixelColor(image.width() / 2, image.height() - 1).alpha(), 0);
        int translucent = 0;
        for (int y = 0; y < menu.actionGeometry(menu.actions().first()).top() - 6; ++y) {
            const auto alpha = image.pixelColor(image.width() / 2, y).alpha();
            if (alpha > 0 && alpha < 255) {
                ++translucent;
            }
        }
        QVERIFY(translucent > 0);
        QCOMPARE(image
                     .pixelColor(image.width() / 2,
                                 menu.actionGeometry(menu.actions().first()).top() - 2)
                     .alpha(),
                 255);
        QTest::keyClick(&menu, Qt::Key_Escape);
        QVERIFY(!menu.isVisible());
    }
    void treeBranchesUseHollowChevronsAndNoLeafDecoration() {
        choscordb::design::ControlStyle style;
        QWidget tree;
        QStyleOption option;
        option.initFrom(&tree);
        option.rect = QRect(0, 0, 16, 16);
        option.state = QStyle::State_Enabled | QStyle::State_Children | QStyle::State_Open;
        option.palette.setColor(QPalette::ButtonText, Qt::black);
        QImage image(16, 16, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        style.drawPrimitive(QStyle::PE_IndicatorBranch, &option, &painter, &tree);
        painter.end();
        QCOMPARE(image.pixelColor(8, 6), QColor(Qt::white));
        QVERIFY(image.pixelColor(8, 9).lightness() < 100);
        option.state = QStyle::State_Enabled;
        image.fill(Qt::white);
        painter.begin(&image);
        style.drawPrimitive(QStyle::PE_IndicatorBranch, &option, &painter, &tree);
        painter.end();
        QCOMPARE(image.pixelColor(8, 9), QColor(Qt::white));
    }
    void fieldKeyboardFocusPaintsOutsideWithoutMovingText() {
        using namespace choscordb::design;
        const ResolvedTheme theme{ResolvedAppearance::Light,
                                  resolveColors(ResolvedAppearance::Light, {}), false};
        QWidget root;
        root.setPalette(applicationPalette(theme));
        root.setStyleSheet(controlStyleSheet(theme));
        QLineEdit field("Stable text", &root);
        field.setGeometry(40, 40, 160, 32);
        root.resize(240, 120);
        root.show();
        root.activateWindow();
        QApplication::processEvents();
        field.clearFocus();
        field.setFocus(Qt::TabFocusReason);
        QApplication::processEvents();
        QVERIFY(field.hasFocus());
        QCOMPARE(root.grab().toImage().pixelColor(38, 55), QColor("#287f66"));
        QCOMPARE(field.geometry(), QRect(40, 40, 160, 32));
        field.clearFocus();
        QApplication::processEvents();
        QCOMPARE(root.grab().toImage().pixelColor(38, 55), QColor("#f6f7f8"));
    }
    void keyboardFocusPrimitiveUsesContrastSafeContinuousOutline() {
        choscordb::design::ControlStyle style;
        QCheckBox checkbox;
        QStyleOptionFocusRect option;
        option.initFrom(&checkbox);
        option.rect = QRect(0, 0, 40, 24);
        option.state =
            QStyle::State_Enabled | QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange;
        option.palette.setColor(QPalette::Dark, QColor("#287f66"));
        QImage image(40, 24, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        style.drawPrimitive(QStyle::PE_FrameFocusRect, &option, &painter, &checkbox);
        painter.end();
        QCOMPARE(image.pixelColor(20, 1), QColor("#287f66"));
        QCOMPARE(image.pixelColor(21, 1), QColor("#287f66"));
        QCOMPARE(image.pixelColor(20, 12), QColor(Qt::white));
    }
    void badgeAndProgressUseCompactReferenceGeometry() {
        using namespace choscordb::design;
        const ResolvedTheme theme{ResolvedAppearance::Light,
                                  resolveColors(ResolvedAppearance::Light, {}), false};
        QWidget root;
        root.setFont(resolveTypography(TypographyRole::Ui));
        root.setStyleSheet(controlStyleSheet(theme));
        QLabel badge("Connected", &root);
        badge.setProperty("designRole", "badge");
        badge.setProperty("variant", "default");
        QProgressBar progress(&root);
        progress.setRange(0, 100);
        progress.setValue(50);
        progress.setTextVisible(false);
        progress.move(0, 40);
        root.resize(220, 90);
        root.show();
        badge.resize(badge.sizeHint());
        progress.resize(180, progress.sizeHint().height());
        QCOMPARE(badge.height(), 20);
        QCOMPARE(badge.grab().toImage().pixelColor(badge.width() / 2, 2), QColor("#eaf4ef"));
        QCOMPARE(progress.height(), 5);
        QCOMPARE(progress.grab().toImage().pixelColor(20, 2), QColor("#287f66"));
        QCOMPARE(progress.grab().toImage().pixelColor(160, 2), QColor("#f2f5f4"));
    }
    void applicationInstallationUsesProductionControlStyle() {
        qApp->setStyleSheet(QString());
        QApplication::setStyle("Fusion");
        choscordb::design::ThemeManager manager;
        manager.setMode(choscordb::design::ThemeMode::Light);
        manager.installOn(qApp);
        QCheckBox checkbox("Production installation");
        checkbox.ensurePolished();
        QCOMPARE(checkbox.style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, &checkbox), 16);
        qApp->setStyleSheet(QString());
    }
    void directionalPrimitiveUsesHollowLucideChevron() {
        choscordb::design::ControlStyle style;
        QComboBox combo;
        QStyleOption option;
        option.initFrom(&combo);
        option.rect = QRect(0, 0, 16, 16);
        option.state = QStyle::State_Enabled;
        option.palette.setColor(QPalette::ButtonText, Qt::black);
        QImage image(16, 16, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        style.drawPrimitive(QStyle::PE_IndicatorArrowDown, &option, &painter, &combo);
        painter.end();
        QCOMPARE(image.pixelColor(8, 6), QColor(Qt::white));
        QVERIFY(image.pixelColor(8, 9).lightness() < 100);
    }
    void initTestCase() { QApplication::setStyle(new choscordb::design::ControlStyle); }
    void tabsToolsAndTableHeadersUseCompactPaneGeometry() {
        using namespace choscordb::design;
        const ResolvedTheme theme{ResolvedAppearance::Light,
                                  resolveColors(ResolvedAppearance::Light, {}), false};
        QWidget root;
        root.setFont(resolveTypography(TypographyRole::Ui));
        root.setStyleSheet(controlStyleSheet(theme));
        QTabBar tabs(&root);
        tabs.addTab("Results");
        tabs.addTab("Messages");
        QToolButton tool(&root);
        tool.setText("Run");
        tool.setCheckable(true);
        tool.move(0, 50);
        QTableWidget table(2, 2, &root);
        table.setHorizontalHeaderLabels({"Value", "Type"});
        table.move(0, 100);
        root.resize(300, 300);
        root.show();
        QCOMPARE(tabs.sizeHint().height(), 35);
        tool.resize(tool.sizeHint());
        QCOMPARE(tool.height(), 29);
        QCOMPARE(table.horizontalHeader()->height(), 30);
        QTest::mouseClick(&tool, Qt::LeftButton);
        QVERIFY(tool.isChecked());
    }
    void toolButtonMenuPanelOpensBesideItsButton() {
        using namespace choscordb::design;
        const ResolvedTheme theme{ResolvedAppearance::Light,
                                  resolveColors(ResolvedAppearance::Light, {}), false};
        QWidget root;
        root.setStyleSheet(controlStyleSheet(theme));
        root.resize(600, 300);
        root.move(100, 100);
        QToolButton button(&root);
        button.setText("More");
        button.move(40, 40);
        QMenu menu(&button);
        menu.addAction("Commit");
        button.setMenu(&menu);
        root.show();
        menu.popup(button.mapToGlobal(QPoint(0, button.height())));
        QCoreApplication::processEvents();
        const int margin = detail::menuShadowMargin();
        QCOMPARE(menu.geometry().left() + margin, button.mapToGlobal(QPoint()).x());
        QCOMPARE(menu.geometry().top() + margin,
                 button.mapToGlobal(QPoint(0, button.height() + 2)).y());
        menu.close();
    }
    void menusTabsAndScrollbarsUseSharedSurfacesAndRemainInteractive() {
        using namespace choscordb::design;
        ControlStyle style;
        const ResolvedTheme theme{ResolvedAppearance::Dark,
                                  resolveColors(ResolvedAppearance::Dark, {}), false};
        QWidget root;
        root.setFont(resolveTypography(TypographyRole::Ui));
        root.setStyle(&style);
        root.setPalette(applicationPalette(theme));
        root.setStyleSheet(controlStyleSheet(theme));
        QScrollBar scroll(Qt::Vertical, &root);
        scroll.setRange(0, 100);
        scroll.resize(scroll.sizeHint().width(), 100);
        QTabBar tabs(&root);
        tabs.addTab("Results");
        tabs.addTab("Messages");
        tabs.move(30, 10);
        root.resize(260, 140);
        root.show();
        QCOMPARE(scroll.sizeHint().width(), 10);
        QTest::keyClick(&scroll, Qt::Key_Down);
        QCOMPARE(scroll.value(), 1);
        QTest::mouseClick(&tabs, Qt::LeftButton, {}, tabs.tabRect(1).center());
        QCOMPARE(tabs.currentIndex(), 1);
        QMenu menu(&root);
        auto* action = menu.addAction("Copy value");
        menu.addSeparator();
        auto* disabled = menu.addAction("Unavailable");
        disabled->setEnabled(false);
        QSignalSpy triggered(action, &QAction::triggered);
        menu.popup(root.mapToGlobal(QPoint(20, 20)));
        QVERIFY(menu.isVisible());
        const auto menuImage = menu.grab().toImage();
        const auto scale = menuImage.devicePixelRatio();
        QCOMPARE(menuImage.pixelColor(qRound(menu.width() / 2.0 * scale),
                                      qRound((menu.actionGeometry(action).top() - 2) * scale)),
                 QColor("#20272b"));
        QTest::keyClick(&menu, Qt::Key_Down);
        QTest::keyClick(&menu, Qt::Key_Return);
        QCOMPARE(triggered.count(), 1);
        QVERIFY(!menu.isVisible());
    }
    void fieldsExposeInvalidBorderAndPreserveReadOnlyAndPopupInput() {
        using namespace choscordb::design;
        ControlStyle style;
        const ResolvedTheme theme{ResolvedAppearance::Light,
                                  resolveColors(ResolvedAppearance::Light, {}), false};
        QWidget root;
        root.setFont(resolveTypography(TypographyRole::Ui));
        root.setStyle(&style);
        root.setPalette(applicationPalette(theme));
        root.setStyleSheet(controlStyleSheet(theme));
        QLineEdit field("Original", &root);
        field.setGeometry(10, 10, 160, 32);
        field.setProperty("invalid", true);
        field.setReadOnly(true);
        QComboBox combo(&root);
        combo.addItems({"SQLite", "PostgreSQL"});
        combo.move(10, 60);
        root.resize(220, 120);
        root.show();
        QTest::keyClicks(&field, "changed");
        QCOMPARE(field.text(), QString("Original"));
        QCOMPARE(field.grab().toImage().pixelColor(80, 0), QColor("#c45d58"));
        QCOMPARE(combo.sizeHint().height(), 33);
        combo.showPopup();
        QVERIFY(combo.view()->isVisible());
        QTest::keyClick(combo.view(), Qt::Key_Down);
        QTest::keyClick(combo.view(), Qt::Key_Return);
        QCOMPARE(combo.currentText(), QString("PostgreSQL"));
        QVERIFY(!combo.view()->isVisible());
    }
    void checkedAndMixedIndicatorsRenderSemanticFill() {
        choscordb::design::ControlStyle style;
        QCheckBox box("Synthetic");
        box.setStyle(&style);
        QPalette palette;
        palette.setColor(QPalette::Window, QColor("#ffffff"));
        palette.setColor(QPalette::Accent, QColor("#287f66"));
        palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        palette.setColor(QPalette::Mid, QColor("#65b493"));
        box.setPalette(palette);
        box.resize(140, 32);
        box.setTristate(true);
        box.setCheckState(Qt::PartiallyChecked);
        box.show();
        box.clearFocus();
        QStyleOptionButton option;
        option.initFrom(&box);
        const auto indicator =
            box.style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, &box);
        const auto mixed = box.grab().toImage();
        QCOMPARE(mixed.pixelColor(indicator.center().x(), indicator.top() + 3), QColor("#287f66"));
        QCOMPARE(mixed.pixelColor(indicator.center()), QColor("#ffffff"));
        box.setCheckState(Qt::Unchecked);
        const auto unchecked = box.grab().toImage();
        QCOMPARE(unchecked.pixelColor(indicator.center()), QColor("#ffffff"));
        box.setCheckState(Qt::Checked);
        QVERIFY(box.grab().toImage() != mixed);
    }
    void checkboxUsesReferenceGeometryAndKeyboardMixedState() {
        choscordb::design::ControlStyle style;
        QCheckBox box("Record history");
        box.setStyle(&style);
        box.setTristate(true);
        box.resize(180, 32);
        box.show();
        QCOMPARE(box.style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, &box), 16);
        QCOMPARE(box.style()->pixelMetric(QStyle::PM_IndicatorHeight, nullptr, &box), 16);
        QTest::keyClick(&box, Qt::Key_Space);
        QCOMPARE(box.checkState(), Qt::PartiallyChecked);
        QTest::keyClick(&box, Qt::Key_Space);
        QCOMPARE(box.checkState(), Qt::Checked);
        box.setEnabled(false);
        QTest::keyClick(&box, Qt::Key_Space);
        QCOMPARE(box.checkState(), Qt::Checked);
    }
};
QTEST_MAIN(ControlStyleTest)
#include "control_style_test.moc"
