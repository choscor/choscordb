#include "design_system/button/button.h"
#include "design_system/button_group/button_group.h"
#include "design_system/icons.h"
#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/theme_manager.h"
#include "design_system/tree/navigation_tree_view.h"
#include <QHBoxLayout>
#include <QListWidget>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTabBar>
#include <QTreeView>
#include <QTreeWidget>
#include <QtTest>

class ComponentsTest final : public QObject {
    Q_OBJECT
  private slots:
    void sidebarSelectionUsesNeutralFill() {
        using namespace choscordb::design;
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            QWidget root;
            ThemeManager theme;
            theme.setMode(mode);
            theme.applyTo(root);
            Button button("", &root);
            button.setVariant(ButtonVariant::Ghost);
            button.setButtonContext(ButtonContext::SidebarTab);
            button.setCheckable(true);
            button.resize(100, 29);
            root.show();
            button.setChecked(true);
            const auto selected = button.grab().toImage();
            const auto colors = resolvedThemeForWidget(button).colors;
            QCOMPARE(selected.pixelColor(20, 14), colors.muted);
            button.setChecked(false);
            QVERIFY(button.grab().toImage().pixelColor(20, 14) != colors.muted);
        }
    }
    void selectedTabsHaveRoundedTopCorners() {
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
        auto image = tabs.grab().toImage();
        const auto colors = resolvedThemeForWidget(tabs).colors;
        int topOfFill = rect.top();
        while (topOfFill < rect.top() + 10 &&
               image.pixelColor(rect.left() + 8, topOfFill) == colors.muted)
            ++topOfFill;
        QVERIFY(topOfFill < rect.top() + 10);
        QCOMPARE(image.pixelColor(rect.left() + 8, topOfFill), colors.surface);
        QCOMPARE(image.pixelColor(rect.left() + 1, topOfFill), colors.muted);
        tabs.setProperty("designTabVariant", "document");
        tabs.style()->unpolish(&tabs);
        tabs.style()->polish(&tabs);
        image = tabs.grab().toImage();
        const auto documentRect = tabs.tabRect(1);
        int documentTop = documentRect.top();
        while (documentTop < documentRect.top() + 10 &&
               image.pixelColor(documentRect.left() + 8, documentTop) == colors.muted)
            ++documentTop;
        QVERIFY(documentTop < documentRect.top() + 10);
        QCOMPARE(image.pixelColor(documentRect.left() + 8, documentTop), colors.mutedText);
        QCOMPARE(image.pixelColor(documentRect.left() + 1, documentTop), colors.muted);
    }
    void sidebarTreeSelectionHasNeutralRoundedFill() {
        using namespace choscordb::design;
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            QWidget root;
            root.setProperty("designSurface", "sidebar");
            root.setAttribute(Qt::WA_StyledBackground);
            ThemeManager theme;
            theme.setMode(mode);
            theme.applyTo(root);
            NavigationTreeView tree(&root);
            tree.setObjectName("databaseNavigator");
            QStandardItemModel model;
            model.appendRow(new QStandardItem("Tables"));
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
            QCOMPARE(image.pixelColor(rect.right() - 8, rect.center().y()), colors.muted);
            QCOMPARE(image.pixelColor(6, rect.center().y()), colors.muted);
            QCOMPARE(image.pixelColor(10, rect.top() + 3), colors.muted);
            QVERIFY(image.pixelColor(6, rect.top() + 3) != colors.muted);
        }
    }
    void sidebarSavedFilesSelectionUsesNeutralFill() {
        using namespace choscordb::design;
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            QWidget root;
            root.setProperty("designSurface", "sidebar");
            root.setAttribute(Qt::WA_StyledBackground);
            ThemeManager theme;
            theme.setMode(mode);
            theme.applyTo(root);
            QTreeWidget tree(&root);
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
                     resolvedThemeForWidget(tree).colors.muted);
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
            QCOMPARE(image.pixelColor(row.left() + 3, row.center().y()), colors.muted);
        }
    }
    void primaryKeyboardFocusContrastsWithTheActionFill_data() {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<QColor>("expectedRing");
        QTest::addColumn<bool>("native");
        QTest::newRow("light") << false << QColor("#ffffff") << false;
        QTest::newRow("native-light") << false << QColor("#ffffff") << true;
        QTest::newRow("dark") << true << QColor("#12231b") << false;
        QTest::newRow("native-dark") << true << QColor("#12231b") << true;
    }
    void primaryKeyboardFocusContrastsWithTheActionFill() {
        QFETCH(bool, dark);
        QFETCH(QColor, expectedRing);
        QFETCH(bool, native);
        using namespace choscordb::design;
        QWidget root;
        ThemeManager theme;
        theme.setMode(dark ? ThemeMode::Dark : ThemeMode::Light);
        theme.applyTo(root);
        auto* button = native ? new QPushButton("", &root) : new Button("", &root);
        if (native)
            button->setProperty("variant", "default");
        Button other("Other", &root);
        button->resize(100, 33);
        other.move(0, 45);
        root.resize(160, 100);
        root.show();
        root.activateWindow();
        other.setFocus(Qt::TabFocusReason);
        QTRY_VERIFY(other.hasFocus());
        const auto normal = button->grab().toImage();
        button->setFocus(Qt::TabFocusReason);
        QTRY_VERIFY(button->hasFocus());
        const auto focused = button->grab().toImage();
        QCOMPARE(focused.pixelColor(50, native ? 0 : 1), expectedRing);
        QVERIFY(focused != normal);
        QSignalSpy clicked(button, &QPushButton::clicked);
        QTest::keyClick(button, Qt::Key_Space);
        QCOMPARE(clicked.count(), 1);
    }
    void editorActionContextUsesReferenceFontAndPadding() {
        using namespace choscordb::design;
        Button button("Save");
        button.setButtonSize(ButtonSize::Small);
        const auto footerWidth = button.sizeHint().width();
        QCOMPARE(button.font().pixelSize(), 11);
        button.setButtonContext(ButtonContext::EditorAction);
        QCOMPARE(button.font().pixelSize(), 12);
        QCOMPARE(button.font().letterSpacing(), 0.0);
        QCOMPARE(button.height(), 29);
        QVERIFY(button.sizeHint().width() >= footerWidth + 6);
        button.setDesignIcon(Icon::Run);
        QCOMPARE(button.height(), 30);
    }
    void primaryPressDoesNotShiftReferenceGeometry() {
        using namespace choscordb::design;
        QWidget root;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(root);
        Button button("Run", &root);
        button.setButtonSize(ButtonSize::Small);
        button.setDesignIcon(Icon::Run);
        button.resize(75, 30);
        root.resize(150, 80);
        root.show();
        button.clearFocus();
        QTest::mouseMove(&button, button.rect().center());
        const auto normal = button.grab().toImage();
        QTest::mousePress(&button, Qt::LeftButton);
        QCOMPARE(button.grab().toImage(), normal);
        QTest::mouseRelease(&button, Qt::LeftButton);
    }
    void compactIconActionsRespectReferenceLineBoxAndDispatch() {
        using namespace choscordb::design;
        Button button("Run");
        button.setButtonSize(ButtonSize::Small);
        QCOMPARE(button.height(), 29);
        button.setDesignIcon(Icon::Run);
        QCOMPARE(button.height(), 30);
        button.resize(75, button.height());
        button.show();
        QSignalSpy clicked(&button, &QPushButton::clicked);
        QTest::mouseClick(&button, Qt::LeftButton);
        QCOMPARE(clicked.count(), 1);
        button.setButtonSize(ButtonSize::Default);
        QCOMPARE(button.height(), 36);
        Button customIcon("Run");
        customIcon.setButtonSize(ButtonSize::Small);
        customIcon.setIcon(themedIcon(Icon::Run, Qt::black, 18));
        QCOMPARE(customIcon.height(), 30);
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
        QVERIFY(light.pixelColor(18, 18).lightness() < 100);
        theme.setMode(ThemeMode::Dark);
        theme.applyTo(host);
        const auto dark = button.grab().toImage();
        QVERIFY(dark.pixelColor(18, 18).lightness() > 180);
        button.setVariant(ButtonVariant::Default);
        const auto primary = button.grab().toImage();
        QVERIFY(primary.pixelColor(18, 18).lightness() < 100);
    }
    void destructiveTextRemainsReadableOnItsTintedSurface() {
        using namespace choscordb::design;
        QWidget host;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(host);
        Button button("MMMM", &host);
        button.setVariant(ButtonVariant::Destructive);
        button.resize(120, 32);
        const auto image = button.grab().toImage();
        const auto background = image.pixelColor(60, 4);
        QColor ink = background;
        for (int y = 6; y < 26; ++y)
            for (int x = 15; x < 105; ++x)
                if (image.pixelColor(x, y).lightness() < ink.lightness())
                    ink = image.pixelColor(x, y);
        QVERIFY(ink != background);
        QVERIFY(contrastRatio(ink, background) >= 4.5);
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
                    if (image.pixelColor(x, y).lightness() > 150)
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
        QCOMPARE(button.sizeHint().width(), normalWidth + 22);
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
        QCOMPARE(button.size(), QSize(100, 33));
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
            int count = 0;
            for (int y = 6; y < image.height() - 6; ++y)
                for (int x = 4; x < image.width() - 4; ++x)
                    if (image.pixelColor(x, y).lightness() > 150)
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
    void buttonVariantsRenderTheirReferenceSurfaces() {
        using namespace choscordb::design;
        QWidget host;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(host);
        Button button("", &host);
        button.resize(100, 32);
        host.show();
        QCoreApplication::processEvents();
        QCOMPARE(button.grab().toImage().pixelColor(50, 5), QColor("#287f66"));
        button.setVariant(ButtonVariant::Secondary);
        QCOMPARE(button.grab().toImage().pixelColor(50, 5), QColor("#f2f2f2"));
        button.setVariant(ButtonVariant::Outline);
        QCOMPARE(button.grab().toImage().pixelColor(50, 5), QColor("#ffffff"));
        theme.setMode(ThemeMode::Dark);
        theme.applyTo(host);
        button.setVariant(ButtonVariant::Default);
        QCOMPARE(button.grab().toImage().pixelColor(50, 5), QColor("#65b493"));
    }
    void buttonsHaveReferenceSizes() {
        using namespace choscordb::design;
        Button button("Save");
        QCOMPARE(button.sizeHint().height(), 33);
        button.setButtonSize(ButtonSize::ExtraSmall);
        QCOMPARE(button.sizeHint().height(), 25);
        button.setButtonSize(ButtonSize::Small);
        QCOMPARE(button.sizeHint().height(), 29);
        button.setButtonSize(ButtonSize::Large);
        QCOMPARE(button.sizeHint().height(), 37);
        button.setButtonSize(ButtonSize::Icon);
        QCOMPARE(button.sizeHint(), QSize(36, 36));
        button.setButtonSize(ButtonSize::IconLarge);
        QCOMPARE(button.sizeHint(), QSize(40, 40));
    }
};
QTEST_MAIN(ComponentsTest)
#include "components_test.moc"
