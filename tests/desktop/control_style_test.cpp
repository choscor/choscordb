#include "design_system/control_style.h"
#include "design_system/theme_manager.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
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
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStyleOptionButton>
#include <QTabBar>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

class ControlStyleTest final : public QObject {
    Q_OBJECT
  private slots:
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
                    if (luminance < 210 && (!dark || luminance > 70) &&
                        qAbs(color.red() - color.green()) < 5 &&
                        qAbs(color.blue() - color.green()) < 5) {
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
        QTest::newRow("light-checked") << false << false << QColor("#171717");
        QTest::newRow("light-mixed") << false << true << QColor("#171717");
        QTest::newRow("dark-checked") << true << false << QColor("#e5e5e5");
        QTest::newRow("dark-mixed") << true << true << QColor("#e5e5e5");
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
        QCOMPARE(button.height(), 32);
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
        QCOMPARE(field.palette().color(QPalette::Active, QPalette::Highlight), QColor("#e5e5e5"));
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
        tree.resize(300, 120);
        root.show();
        QCoreApplication::processEvents();
        QCOMPARE(tree.visualItemRect(tree.topLevelItem(0)).height(), 28);
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
        QCOMPARE(root.grab().toImage().pixelColor(38, 55), QColor("#737373"));
        const auto original = field.grab().toImage().pixelColor(0, 16);
        field.setProperty("invalid", true);
        QCoreApplication::processEvents();
        QCOMPARE(field.grab().toImage().pixelColor(0, 16), QColor("#e7000b"));
        QCOMPARE(root.grab().toImage().pixelColor(38, 55), QColor("#e7000b"));
        field.setProperty("invalid", false);
        QCoreApplication::processEvents();
        QCOMPARE(field.grab().toImage().pixelColor(0, 16), original);
        QCOMPARE(root.grab().toImage().pixelColor(38, 55), QColor("#737373"));
    }
    void tableRowsHonorReferenceLineBoxAndPadding() {
        using namespace choscordb::design;
        QWidget root;
        ThemeManager theme;
        theme.applyTo(root);
        QTableWidget table(3, 2, &root);
        table.ensurePolished();
        QCOMPARE(table.verticalHeader()->defaultSectionSize(), 37);
    }
    void unusedHeaderGutterUsesThemeSurface_data() {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<QColor>("expected");
        QTest::newRow("light") << false << QColor("#ffffff");
        QTest::newRow("dark") << true << QColor("#0a0a0a");
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
        for (int y = 0; y < 16; ++y) {
            const auto alpha = image.pixelColor(image.width() / 2, y).alpha();
            if (alpha > 0 && alpha < 255) {
                ++translucent;
            }
        }
        QVERIFY(translucent > 0);
        QCOMPARE(image.pixelColor(image.width() / 2, 20).alpha(), 255);
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
        QCOMPARE(root.grab().toImage().pixelColor(38, 55), QColor("#737373"));
        QCOMPARE(field.geometry(), QRect(40, 40, 160, 32));
        field.clearFocus();
        QApplication::processEvents();
        QCOMPARE(root.grab().toImage().pixelColor(38, 55), QColor("#ffffff"));
    }
    void keyboardFocusPrimitiveUsesContrastSafeContinuousOutline() {
        choscordb::design::ControlStyle style;
        QCheckBox checkbox;
        QStyleOptionFocusRect option;
        option.initFrom(&checkbox);
        option.rect = QRect(0, 0, 40, 24);
        option.state =
            QStyle::State_Enabled | QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange;
        option.palette.setColor(QPalette::Dark, QColor("#737373"));
        QImage image(40, 24, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        style.drawPrimitive(QStyle::PE_FrameFocusRect, &option, &painter, &checkbox);
        painter.end();
        QCOMPARE(image.pixelColor(20, 1), QColor("#737373"));
        QCOMPARE(image.pixelColor(21, 1), QColor("#737373"));
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
        QCOMPARE(badge.grab().toImage().pixelColor(badge.width() / 2, 2), QColor("#171717"));
        QCOMPARE(progress.height(), 4);
        QCOMPARE(progress.grab().toImage().pixelColor(20, 2), QColor("#171717"));
        QCOMPARE(progress.grab().toImage().pixelColor(160, 2), QColor("#f5f5f5"));
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
    void tabsToolsAndTableHeadersUseNovaDimensions() {
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
        QCOMPARE(tabs.sizeHint().height(), 32);
        tool.resize(tool.sizeHint());
        QCOMPARE(tool.height(), 32);
        QCOMPARE(table.horizontalHeader()->height(), 40);
        QTest::mouseClick(&tool, Qt::LeftButton);
        QVERIFY(tool.isChecked());
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
        QCOMPARE(menu.grab().toImage().pixelColor(menu.width() / 2, 20), QColor("#171717"));
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
        QCOMPARE(field.grab().toImage().pixelColor(80, 0), QColor("#e7000b"));
        QCOMPARE(combo.sizeHint().height(), 32);
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
        palette.setColor(QPalette::Accent, QColor("#171717"));
        palette.setColor(QPalette::HighlightedText, QColor("#fafafa"));
        palette.setColor(QPalette::Mid, QColor("#e5e5e5"));
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
        QCOMPARE(mixed.pixelColor(indicator.center().x(), indicator.top() + 3), QColor("#171717"));
        QCOMPARE(mixed.pixelColor(indicator.center()), QColor("#fafafa"));
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
