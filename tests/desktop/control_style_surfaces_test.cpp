#include "control_style_test.h"
#include "design_system/control_style.h"
#include "design_system/menu/menu.h"
#include "design_system/theme_manager.h"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFontComboBox>
#include <QHeaderView>
#include <QHelpEvent>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
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
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

void ControlStyleTest::scopedDarkButtonsAndTextSelectionUseSemanticColors() {
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

void ControlStyleTest::navigationRowsUseCompactGeometry() {
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
    QCOMPARE(tree.visualItemRect(tree.topLevelItem(0)).height(), 28);
    tree.setCurrentItem(tree.topLevelItem(0));
    const auto row = tree.visualItemRect(tree.topLevelItem(0));
    QCOMPARE(tree.viewport()->grab().toImage().pixelColor(row.right() - 12, row.center().y()),
             QColor("#ccebdc"));
    QTest::keyClick(&tree, Qt::Key_Down);
    QCOMPARE(tree.currentItem()->text(0), QString("Second connection"));
    const auto secondRow = tree.visualItemRect(tree.currentItem());
    QCOMPARE(tree.viewport()->grab().toImage().pixelColor(secondRow.right() - 12,
                                                          secondRow.center().y()),
             QColor("#ccebdc"));
}

void ControlStyleTest::treeHoverFillsSquareRowCorners() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    QTreeWidget tree(&root);
    tree.setHeaderHidden(true);
    tree.addTopLevelItem(new QTreeWidgetItem({"Connection"}));
    tree.resize(300, 120);
    root.show();
    QCoreApplication::processEvents();
    const auto row = tree.visualItemRect(tree.topLevelItem(0));
    QTest::mouseMove(tree.viewport(), QPoint(row.center().x(), row.bottom() + 10));
    QTest::mouseMove(tree.viewport(), row.center());
    QTRY_COMPARE_WITH_TIMEOUT(([&] {
                                  const auto image = tree.viewport()->grab().toImage();
                                  const auto scale = image.devicePixelRatio();
                                  return image.pixelColor(qRound(row.right() * scale),
                                                          qRound(row.top() * scale));
                              }()),
                              QColor("#f2f2f2"), 1000);
}

void ControlStyleTest::listSelectionFillsSquareRowCorners() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    theme.applyTo(root);
    QListWidget list(&root);
    list.addItems({"Connections", "Query history"});
    list.setCurrentRow(1);
    list.resize(300, 120);
    root.show();
    QCoreApplication::processEvents();
    const auto row = list.visualItemRect(list.item(1));
    const auto image = list.viewport()->grab().toImage();
    const auto scale = image.devicePixelRatio();
    QCOMPARE(image.pixelColor(qRound(row.right() * scale), qRound(row.top() * scale)),
             QColor("#ccebdc"));
}

void ControlStyleTest::validationStateUpdatesAnAlreadyVisibleField() {
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

void ControlStyleTest::tableRowsHonorReferenceLineBoxAndPadding() {
    using namespace choscordb::design;
    QWidget root;
    ThemeManager theme;
    theme.applyTo(root);
    QTableWidget table(3, 2, &root);
    table.ensurePolished();
    QCOMPARE(table.verticalHeader()->defaultSectionSize(), 29);
}

void ControlStyleTest::unusedHeaderGutterUsesThemeSurface_data() {
    using namespace choscordb::design;
    QTest::addColumn<bool>("dark");
    QTest::addColumn<QColor>("expected");
    QTest::newRow("light") << false << resolveColors(ResolvedAppearance::Light, {}).muted;
    QTest::newRow("dark") << true << resolveColors(ResolvedAppearance::Dark, {}).muted;
}

void ControlStyleTest::unusedHeaderGutterUsesThemeSurface() {
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

void ControlStyleTest::tableHeaderColumnsHaveSeparators() {
    using namespace choscordb::design;
    const auto appearance = ResolvedAppearance::Light;
    const ResolvedTheme theme{appearance, resolveColors(appearance, {}), false};
    QWidget root;
    root.setStyleSheet(controlStyleSheet(theme));
    QTableWidget table(1, 2, &root);
    table.setHorizontalHeaderLabels({"id · int8", "value · text"});
    table.setColumnWidth(0, 100);
    table.setColumnWidth(1, 100);
    table.resize(240, 120);
    root.resize(260, 140);
    root.show();
    QApplication::processEvents();
    auto* header = table.horizontalHeader();
    const auto image = header->grab().toImage();
    const int boundary = header->sectionViewportPosition(0) + header->sectionSize(0) - 1;
    QCOMPARE(image.pixelColor(boundary, header->height() / 2), theme.colors.border);
}

void ControlStyleTest::rowNumberColumnHasVerticalSeparator() {
    using namespace choscordb::design;
    const auto appearance = ResolvedAppearance::Light;
    const ResolvedTheme theme{appearance, resolveColors(appearance, {}), false};
    QWidget root;
    root.setStyleSheet(controlStyleSheet(theme));
    QTableWidget table(2, 1, &root);
    table.resize(240, 160);
    root.resize(260, 180);
    root.show();
    QApplication::processEvents();
    auto* header = table.verticalHeader();
    const auto image = header->grab().toImage();
    const int edge = image.width() - 1;
    QCOMPARE(image.pixelColor(edge, header->sectionSize(0) / 2), theme.colors.border);
    QCOMPARE(image.pixelColor(edge, image.height() - 10), theme.colors.border);
    QAbstractButton* corner = nullptr;
    for (auto* button : table.findChildren<QAbstractButton*>()) {
        if (button->inherits("QTableCornerButton")) {
            corner = button;
            break;
        }
    }
    QVERIFY(corner);
    const auto cornerImage = corner->grab().toImage();
    QCOMPARE(cornerImage.pixelColor(cornerImage.width() - 1, cornerImage.height() / 2),
             theme.colors.border);
}

void ControlStyleTest::tableHeaderAndTabTooltipsUseSharedSurface() {
    QWidget root;
    QTableWidget table(1, 1, &root);
    table.setGeometry(10, 10, 240, 120);
    auto* cell = new QTableWidgetItem("value");
    cell->setToolTip("Cell help");
    table.setItem(0, 0, cell);
    auto* heading = new QTableWidgetItem("column");
    heading->setToolTip("Column help");
    table.setHorizontalHeaderItem(0, heading);
    QTabBar tabs(&root);
    tabs.setGeometry(10, 150, 240, 40);
    tabs.addTab("Query");
    tabs.setTabToolTip(0, "Query help");
    root.resize(280, 240);
    root.show();
    QApplication::processEvents();
    const auto check = [&root](QWidget* target, QPoint point, const QString& expected) {
        QHelpEvent help(QEvent::ToolTip, point, target->mapToGlobal(point));
        QApplication::sendEvent(target, &help);
        auto* tooltip = root.findChild<QWidget*>("designTooltip");
        QVERIFY(tooltip);
        QVERIFY(tooltip->isVisible());
        QCOMPARE(tooltip->accessibleName(), expected);
    };
    check(table.viewport(), table.visualItemRect(cell).center(), "Cell help");
    check(table.horizontalHeader()->viewport(), QPoint(20, 10), "Column help");
    check(&tabs, tabs.tabRect(0).center(), "Query help");
}

void ControlStyleTest::itemTooltipsUseSharedSurface() {
    QWidget root;
    QListWidget list(&root);
    list.setGeometry(20, 60, 220, 140);
    auto* first = new QListWidgetItem("Example Postgres", &list);
    first->setToolTip("Example Postgres");
    auto* second = new QListWidgetItem("Analytics", &list);
    second->setToolTip("Analytics database");
    root.resize(280, 240);
    root.show();
    QApplication::processEvents();
    for (auto* item : {first, second}) {
        const auto point = list.visualItemRect(item).center();
        QHelpEvent help(QEvent::ToolTip, point, list.viewport()->mapToGlobal(point));
        QApplication::sendEvent(list.viewport(), &help);
        auto* tooltip = root.findChild<QWidget*>("designTooltip");
        QVERIFY(tooltip);
        QVERIFY(tooltip->isVisible());
        QCOMPARE(tooltip->accessibleName(), item->toolTip());
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(list.viewport(), &leave);
        QVERIFY(!tooltip->isVisible());
    }
}

void ControlStyleTest::unbrokenTooltipWrapsAndOwnerDestructionDismissesIt() {
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

void ControlStyleTest::actualTooltipHasArrowAndDismissesOnOwnerLeave() {
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

void ControlStyleTest::menuHasRenderedTranslucentElevationOutsidePanel() {
    using namespace choscordb::design;
    const ResolvedTheme theme{ResolvedAppearance::Light,
                              resolveColors(ResolvedAppearance::Light, {}), false};
    QWidget root;
    root.setProperty("designTheme", QVariant::fromValue(theme));
    root.setStyleSheet(controlStyleSheet(theme));
    root.resize(640, 400);
    root.show();
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
    QCOMPARE(
        image.pixelColor(image.width() / 2, menu.actionGeometry(menu.actions().first()).top() - 2)
            .alpha(),
        255);
    QTest::keyClick(&menu, Qt::Key_Escape);
    QVERIFY(!menu.isVisible());
}

void ControlStyleTest::treeBranchesUseHollowChevronsAndNoLeafDecoration() {
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

void ControlStyleTest::fieldKeyboardFocusPaintsOutsideWithoutMovingText() {
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

void ControlStyleTest::keyboardFocusPrimitiveUsesContrastSafeContinuousOutline() {
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

void ControlStyleTest::badgeAndProgressUseCompactReferenceGeometry() {
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
    QCOMPARE(badge.grab().toImage().pixelColor(badge.width() / 2, 2), QColor("#ccebdc"));
    QCOMPARE(progress.height(), 5);
    QCOMPARE(progress.grab().toImage().pixelColor(20, 2), QColor("#287f66"));
    QCOMPARE(progress.grab().toImage().pixelColor(160, 2), QColor("#f2f2f2"));
}

void ControlStyleTest::applicationInstallationUsesProductionControlStyle() {
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

void ControlStyleTest::directionalPrimitiveUsesHollowLucideChevron() {
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

void ControlStyleTest::initTestCase() {
    QApplication::setStyle(new choscordb::design::ControlStyle);
}

void ControlStyleTest::tabsToolsAndTableHeadersUseCompactPaneGeometry() {
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

void ControlStyleTest::toolButtonMenuPanelOpensBesideItsButton() {
    using namespace choscordb::design;
    const ResolvedTheme theme{ResolvedAppearance::Light,
                              resolveColors(ResolvedAppearance::Light, {}), false};
    QWidget root;
    root.setStyleSheet(controlStyleSheet(theme));
    root.resize(600, 500);
    root.move(100, 100);
    QToolButton button(&root);
    button.setText("More");
    button.move(140, 140);
    QMenu menu(&button);
    menu.addAction("Commit");
    button.setMenu(&menu);
    root.show();
    menu.popup(button.mapToGlobal(QPoint(0, button.height())));
    QCoreApplication::processEvents();
    const int margin = detail::menuShadowMargin();
    QCOMPARE(menu.mapToGlobal(QPoint()).x() + margin, button.mapToGlobal(QPoint()).x());
    QCOMPARE(menu.mapToGlobal(QPoint()).y() + margin,
             button.mapToGlobal(QPoint(0, button.height() + 2)).y());
    menu.close();
}

void ControlStyleTest::menusTabsAndScrollbarsUseSharedSurfacesAndRemainInteractive() {
    using namespace choscordb::design;
    ControlStyle style;
    const ResolvedTheme theme{ResolvedAppearance::Dark, resolveColors(ResolvedAppearance::Dark, {}),
                              false};
    QWidget root;
    root.setFont(resolveTypography(TypographyRole::Ui));
    root.setStyle(&style);
    root.setPalette(applicationPalette(theme));
    root.setProperty("designTheme", QVariant::fromValue(theme));
    root.setStyleSheet(controlStyleSheet(theme));
    QScrollBar scroll(Qt::Vertical, &root);
    scroll.setRange(0, 100);
    scroll.resize(scroll.sizeHint().width(), 100);
    QTabBar tabs(&root);
    tabs.addTab("Results");
    tabs.addTab("Messages");
    tabs.move(30, 10);
    root.resize(640, 400);
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

void ControlStyleTest::fieldsExposeInvalidBorderAndPreserveReadOnlyAndPopupInput() {
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

void ControlStyleTest::checkedAndMixedIndicatorsRenderSemanticFill() {
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
    const auto indicator = box.style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, &box);
    const auto mixed = box.grab().toImage();
    QCOMPARE(mixed.pixelColor(indicator.center().x(), indicator.top() + 3), QColor("#287f66"));
    QCOMPARE(mixed.pixelColor(indicator.center()), QColor("#ffffff"));
    box.setCheckState(Qt::Unchecked);
    const auto unchecked = box.grab().toImage();
    QCOMPARE(unchecked.pixelColor(indicator.center()), QColor("#ffffff"));
    box.setCheckState(Qt::Checked);
    QVERIFY(box.grab().toImage() != mixed);
}

void ControlStyleTest::checkedRadioUsesCheckboxAccentAndSize() {
    choscordb::design::ControlStyle style;
    QRadioButton radio("Radio option");
    radio.setStyle(&style);
    QPalette palette;
    palette.setColor(QPalette::Window, Qt::white);
    palette.setColor(QPalette::Accent, QColor("#287f66"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Mid, QColor("#65b493"));
    radio.setPalette(palette);
    radio.resize(150, 32);
    radio.setChecked(true);
    radio.show();
    QStyleOptionButton option;
    option.initFrom(&radio);
    const auto indicator =
        radio.style()->subElementRect(QStyle::SE_RadioButtonIndicator, &option, &radio);
    QCOMPARE(indicator.size(), QSize(18, 18));
    const auto image = radio.grab().toImage();
    QCOMPARE(image.pixelColor(indicator.center().x(), indicator.top() + 3), QColor("#287f66"));
    QCOMPARE(image.pixelColor(indicator.center()), QColor(Qt::white));
}

void ControlStyleTest::disabledCheckedIndicatorUsesMutedGreenOutlineAndCheck() {
    choscordb::design::ControlStyle style;
    QCheckBox box;
    QStyleOptionButton option;
    option.initFrom(&box);
    option.rect = QRect(0, 0, 16, 16);
    option.state = QStyle::State_On;
    option.palette.setColor(QPalette::Accent, QColor("#287f66"));
    option.palette.setColor(QPalette::HighlightedText, Qt::white);
    QImage image(16, 16, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    style.drawPrimitive(QStyle::PE_IndicatorCheckBox, &option, &painter, &box);
    painter.end();
    QCOMPARE(image.pixelColor(8, 3), QColor(Qt::white));
    const auto outline = image.pixelColor(8, 0);
    QVERIFY(outline.green() > outline.red());
    QVERIFY(outline.green() > outline.blue());
    const auto check = image.pixelColor(6, 10);
    QVERIFY(check.green() > check.red());
    QVERIFY(check.green() > check.blue());
}

void ControlStyleTest::checkboxUsesReferenceGeometryAndKeyboardMixedState() {
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

void ControlStyleTest::contextMenuUsesCursorAndDismissesOutside() {
    using namespace choscordb::design;
    QWidget owner;
    ThemeManager theme;
    theme.applyTo(owner);
    owner.setGeometry(100, 100, 900, 650);
    QToolButton origin(&owner);
    origin.setGeometry(20, 20, 120, 32);
    QPushButton outside("Outside", &owner);
    outside.setGeometry(650, 550, 120, 32);
    QMenu menu(&origin);
    menu.addAction("Close");
    menu.addAction("Close others");
    owner.show();
    QVERIFY(QTest::qWaitForWindowActive(&owner));
    for (const QPoint local : {QPoint(20, 20), QPoint(200, 150), QPoint(420, 300)}) {
        const QPoint cursor = owner.mapToGlobal(local);
        popupContextMenu(menu, cursor);
        QVERIFY(menu.isVisible());
        QCOMPARE(menu.mapToGlobal(QPoint(detail::menuShadowMargin(), detail::menuShadowMargin())),
                 cursor);
        QTest::mouseClick(&outside, Qt::LeftButton);
        QVERIFY(!menu.isVisible());
    }
}

void ControlStyleTest::standardTextContextMenusUseCursorAndDismissOutside() {
    using namespace choscordb::design;
    QWidget owner;
    ThemeManager theme;
    theme.applyTo(owner);
    owner.setGeometry(100, 100, 900, 650);
    QLineEdit line("Synthetic text", &owner);
    QPlainTextEdit plain("Synthetic text", &owner);
    QTextEdit rich("Synthetic text", &owner);
    line.setGeometry(20, 20, 500, 32);
    plain.setGeometry(20, 70, 500, 150);
    rich.setGeometry(20, 240, 500, 150);
    QPushButton outside("Outside", &owner);
    outside.setGeometry(650, 550, 120, 32);
    owner.show();
    QVERIFY(QTest::qWaitForWindowActive(&owner));
    for (QWidget* target : {static_cast<QWidget*>(&line), plain.viewport(), rich.viewport()}) {
        const QPoint cursor = target->mapToGlobal(QPoint(100, 15));
        // Support both asynchronous menus and a nested menu event loop.
        bool inspected = false;
        QTimer::singleShot(0, &owner, [&, cursor] {
            inspected = true;
            QMenu* visible = nullptr;
            for (auto* widget : QApplication::allWidgets())
                if (auto* menu = qobject_cast<QMenu*>(widget); menu && menu->isVisible())
                    visible = menu;
            QVERIFY(visible);
            const QPoint panel = visible->mapToGlobal(
                QPoint(detail::menuShadowMargin(), detail::menuShadowMargin()));
            QTest::mouseClick(&outside, Qt::LeftButton);
            QVERIFY(!visible->isVisible());
            QCOMPARE(panel, cursor);
        });
        QContextMenuEvent request(QContextMenuEvent::Mouse, QPoint(100, 15), cursor);
        QApplication::sendEvent(target, &request);
        QTRY_VERIFY(inspected);
    }
}

void ControlStyleTest::contextMenuShadowClickDismisses() {
    using namespace choscordb::design;
    QWidget owner;
    ThemeManager theme;
    theme.applyTo(owner);
    owner.resize(800, 600);
    QMenu menu(&owner);
    menu.addAction("Close");
    owner.show();
    QVERIFY(QTest::qWaitForWindowActive(&owner));
    popupContextMenu(menu, owner.mapToGlobal(QPoint(200, 150)));
    QVERIFY(menu.isVisible());
    QTest::mouseClick(&menu, Qt::LeftButton, {}, QPoint(2, 2));
    QVERIFY(!menu.isVisible());
}

void ControlStyleTest::scrolledTextContextMenuCopiesTheClickedLink() {
    using namespace choscordb::design;
    QWidget owner;
    ThemeManager theme;
    theme.applyTo(owner);
    owner.resize(1000, 800);
    QTextEdit rich(&owner);
    rich.setGeometry(100, 100, 600, 400);
    rich.setTextInteractionFlags(Qt::TextBrowserInteraction);
    rich.setHtml(QString("<p>Earlier paragraph</p>").repeated(40) +
                 R"(<p><a href="https://example.invalid/target">Target link</a></p>)");
    owner.show();
    QVERIFY(QTest::qWaitForWindowActive(&owner));
    auto cursor = rich.document()->find("Target link");
    QVERIFY(!cursor.isNull());
    cursor.setPosition(cursor.selectionStart() + 2);
    rich.setTextCursor(cursor);
    rich.ensureCursorVisible();
    QVERIFY(rich.verticalScrollBar()->value() > 0);
    const QPoint point = rich.cursorRect(cursor).center();
    QCOMPARE(rich.anchorAt(point), QString("https://example.invalid/target"));
    bool inspected = false;
    QTimer::singleShot(0, &owner, [&] {
        inspected = true;
        QMenu* menu = nullptr;
        for (auto* candidate : owner.findChildren<QMenu*>())
            if (candidate->isVisible())
                menu = candidate;
        QVERIFY(menu);
        auto* copy = menu->findChild<QAction*>("link-copy");
        QVERIFY(copy);
        const bool enabled = copy->isEnabled();
        QApplication::clipboard()->clear();
        copy->trigger();
        menu->hide();
        QVERIFY(enabled);
        QCOMPARE(QApplication::clipboard()->text(), QString("https://example.invalid/target"));
    });
    QContextMenuEvent request(QContextMenuEvent::Mouse, point, rich.viewport()->mapToGlobal(point));
    QApplication::sendEvent(rich.viewport(), &request);
    QTRY_VERIFY(inspected);
}

void ControlStyleTest::nativeToolbarContextMenuUsesCursor() {
    using namespace choscordb::design;
    QMainWindow owner;
    ThemeManager theme;
    theme.applyTo(owner);
    owner.resize(1000, 800);
    auto* toolbar = owner.addToolBar("Navigation");
    toolbar->addAction("Synthetic action");
    owner.show();
    QVERIFY(QTest::qWaitForWindowActive(&owner));
    const QPoint cursor = toolbar->mapToGlobal(QPoint(200, 10));
    bool inspected = false;
    QTimer::singleShot(0, &owner, [&] {
        inspected = true;
        QMenu* menu = nullptr;
        for (auto* candidate : owner.findChildren<QMenu*>())
            if (candidate->isVisible())
                menu = candidate;
        QVERIFY(menu);
        const auto panel =
            menu->mapToGlobal(QPoint(detail::menuShadowMargin(), detail::menuShadowMargin()));
        const bool hasToggle = menu->actions().contains(toolbar->toggleViewAction());
        QTest::mouseClick(&owner, Qt::LeftButton, {}, QPoint(800, 700));
        QVERIFY(!menu->isVisible());
        QVERIFY(hasToggle);
        QCOMPARE(panel, cursor);
    });
    QContextMenuEvent request(QContextMenuEvent::Mouse, owner.mapFromGlobal(cursor), cursor);
    QApplication::sendEvent(&owner, &request);
    QTRY_VERIFY(inspected);
}
