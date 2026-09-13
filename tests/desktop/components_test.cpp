#include "design_system/components.h"
#include "design_system/icons.h"
#include "design_system/theme_manager.h"
#include <QHBoxLayout>
#include <QSignalSpy>
#include <QtTest>

class ComponentsTest final : public QObject {
    Q_OBJECT
  private slots:
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
        QVERIFY(light.pixelColor(16, 16).lightness() < 100);
        theme.setMode(ThemeMode::Dark);
        theme.applyTo(host);
        const auto dark = button.grab().toImage();
        QVERIFY(dark.pixelColor(16, 16).lightness() > 180);
        button.setVariant(ButtonVariant::Default);
        const auto primary = button.grab().toImage();
        QVERIFY(primary.pixelColor(16, 16).lightness() < 100);
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
        QCOMPARE(button.sizeHint(), QSize(32, 32));
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
        QCOMPARE(button.size(), QSize(100, 32));
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
                for (int x = 8; x < image.width() - 8; ++x)
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
        QCOMPARE(button.size(), QSize(32, 32));
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
        QCOMPARE(button.grab().toImage().pixelColor(50, 5), QColor("#171717"));
        button.setVariant(ButtonVariant::Secondary);
        QCOMPARE(button.grab().toImage().pixelColor(50, 5), QColor("#f5f5f5"));
        button.setVariant(ButtonVariant::Outline);
        QCOMPARE(button.grab().toImage().pixelColor(50, 5), QColor("#ffffff"));
        theme.setMode(ThemeMode::Dark);
        theme.applyTo(host);
        button.setVariant(ButtonVariant::Default);
        QCOMPARE(button.grab().toImage().pixelColor(50, 5), QColor("#e5e5e5"));
    }
    void buttonsHaveReferenceSizes() {
        using namespace choscordb::design;
        Button button("Save");
        QCOMPARE(button.sizeHint().height(), 32);
        button.setButtonSize(ButtonSize::ExtraSmall);
        QCOMPARE(button.sizeHint().height(), 24);
        button.setButtonSize(ButtonSize::Small);
        QCOMPARE(button.sizeHint().height(), 28);
        button.setButtonSize(ButtonSize::Large);
        QCOMPARE(button.sizeHint().height(), 36);
        button.setButtonSize(ButtonSize::Icon);
        QCOMPARE(button.sizeHint(), QSize(32, 32));
        button.setButtonSize(ButtonSize::IconLarge);
        QCOMPARE(button.sizeHint(), QSize(36, 36));
    }
};
QTEST_MAIN(ComponentsTest)
#include "components_test.moc"
