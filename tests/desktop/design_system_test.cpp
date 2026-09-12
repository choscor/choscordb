#include "design_system/icons.h"
#include "design_system/platform_accessibility.h"
#include "design_system/theme_manager.h"

#include <QApplication>
#include <QEnterEvent>
#include <QFile>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyleOptionButton>
#include <QVBoxLayout>
#include <QtTest>

class InspectableButton final : public QPushButton {
  public:
    using QPushButton::QPushButton;
    [[nodiscard]] QStyleOptionButton styleOption() const {
        QStyleOptionButton option;
        initStyleOption(&option);
        return option;
    }
};

class DesignSystemTest final : public QObject {
    Q_OBJECT

  private slots:
    void explicitAndSystemThemesResolvePredictably() {
        choscordb::design::ThemeManager manager;

        QCOMPARE(manager.mode(), choscordb::design::ThemeMode::System);
        QCOMPARE(manager.density(), choscordb::design::Density::Compact);
        QCOMPARE(manager.resolvedTheme().appearance, choscordb::design::ResolvedAppearance::Light);

        QSignalSpy changed(&manager, &choscordb::design::ThemeManager::themeChanged);
        manager.setSystemAppearance(choscordb::design::ResolvedAppearance::Dark);
        QCOMPARE(manager.resolvedTheme().appearance, choscordb::design::ResolvedAppearance::Dark);
        QCOMPARE(changed.count(), 1);

        manager.setMode(choscordb::design::ThemeMode::Light);
        QCOMPARE(manager.resolvedTheme().appearance, choscordb::design::ResolvedAppearance::Light);
        const auto explicitThemeNotifications = changed.count();
        manager.setSystemAppearance(choscordb::design::ResolvedAppearance::Light);
        QCOMPARE(manager.resolvedTheme().appearance, choscordb::design::ResolvedAppearance::Light);
        QCOMPARE(changed.count(), explicitThemeNotifications);
    }

    void densityResolvesTheWholeMetricFamily() {
        choscordb::design::ThemeManager manager;
        const auto compact = manager.metrics();
        QCOMPARE(compact.grid, 4);
        QCOMPARE(compact.controlHeight, 32);
        QCOMPARE(compact.dataRowHeight, 28);
        QCOMPARE(compact.workspaceChromeHeight, 36);
        QCOMPARE(compact.dialogContentSpacing, 16);
        QCOMPARE(compact.narrowWorkspaceWidth, 1100);
        QCOMPARE(compact.defaultWorkspaceWidth, 1280);
        QCOMPARE(compact.minimumWorkspaceWidth, 960);
        QCOMPARE(choscordb::design::dialogInitialSize(choscordb::design::DialogSize::Ddl),
                 QSize(700, 500));
        QVERIFY(!choscordb::design::resolveTypography(choscordb::design::TypographyRole::Monospace)
                     .family()
                     .isEmpty());

        QSignalSpy changed(&manager, &choscordb::design::ThemeManager::metricsChanged);
        manager.setDensity(choscordb::design::Density::Comfortable);
        const auto comfortable = manager.metrics();
        QCOMPARE(comfortable.controlHeight, 40);
        QCOMPARE(comfortable.dataRowHeight, 36);
        QCOMPARE(comfortable.workspaceChromeHeight, 44);
        QCOMPARE(comfortable.dialogContentSpacing, 20);
        QCOMPARE(changed.count(), 1);

        manager.setDensity(choscordb::design::Density::Comfortable);
        QCOMPARE(changed.count(), 1);
    }

    void accentsResolveReadableComponentRoles() {
        using namespace choscordb::design;
        constexpr AccentPreset presets[] = {
            AccentPreset::Cobalt, AccentPreset::Azure,  AccentPreset::Teal, AccentPreset::Green,
            AccentPreset::Violet, AccentPreset::Orange, AccentPreset::Rose};
        for (const auto appearance : {ResolvedAppearance::Light, ResolvedAppearance::Dark}) {
            for (const auto preset : presets) {
                const auto accent = Accent::presetColor(preset);
                QVERIFY2(validateAccent(accent, appearance).accepted,
                         qPrintable(validateAccent(accent, appearance).reason));
                const auto colors = resolveColors(appearance, accent);
                QVERIFY(contrastRatio(colors.actionText, colors.action) >= 4.5);
                QVERIFY(contrastRatio(colors.actionText, colors.actionHover) >= 4.5);
                QVERIFY(contrastRatio(colors.actionText, colors.actionPressed) >= 4.5);
                QVERIFY(contrastRatio(colors.actionHover, colors.canvas) >= 3.0);
                QVERIFY(contrastRatio(colors.actionPressed, colors.canvas) >= 3.0);
                QVERIFY(contrastRatio(colors.action, colors.surface) >= 3.0);
                QVERIFY(contrastRatio(colors.actionHover, colors.surface) >= 3.0);
                QVERIFY(contrastRatio(colors.actionPressed, colors.surface) >= 3.0);
                QVERIFY(contrastRatio(colors.focus, colors.canvas) >= 3.0);
                QVERIFY(contrastRatio(colors.focus, colors.surface) >= 3.0);
                QVERIFY(contrastRatio(colors.selectionText, colors.selection) >= 4.5);
                QVERIFY(contrastRatio(colors.text, colors.subtleAccent) >= 4.5);
            }
        }
    }

    void inaccessibleCustomAccentKeepsTheLastValidPreview() {
        using namespace choscordb::design;
        ThemeManager manager;
        const auto valid = Accent::custom(QColor("#2468B2"));
        QVERIFY(manager.setAccent(valid).accepted);
        const auto lastValidTheme = manager.resolvedTheme();
        QSignalSpy changed(&manager, &ThemeManager::themeChanged);

        const auto malformed = manager.setAccent(Accent::custom(QColor{}));
        QVERIFY(!malformed.accepted);
        QVERIFY(!malformed.reason.isEmpty());
        QCOMPARE(manager.accent(), valid);

        const auto invalid = manager.setAccent(Accent::custom(QColor("#FFFFFF")));
        QVERIFY(!invalid.accepted);
        QVERIFY(!invalid.reason.isEmpty());
        QCOMPARE(manager.accent(), valid);
        QCOMPARE(manager.resolvedTheme(), lastValidTheme);
        QCOMPARE(changed.count(), 0);

        ThemeManager darkManager;
        darkManager.setMode(ThemeMode::Dark);
        const auto themeFragile = darkManager.setAccent(Accent::custom(QColor("#FFFFFF")));
        QVERIFY(!themeFragile.accepted);
        QVERIFY(!themeFragile.reason.isEmpty());
    }

    void accessibilityPoliciesOverrideRenderingWithoutDeletingChoices() {
        using namespace choscordb::design;
        ThemeManager manager;
        manager.setMode(ThemeMode::Dark);
        manager.setDensity(Density::Comfortable);
        const auto chosenAccent = Accent::presetColor(AccentPreset::Rose);
        QVERIFY(manager.setAccent(chosenAccent).accepted);

        QPalette highContrast;
        highContrast.setColor(QPalette::Window, QColor("#000000"));
        highContrast.setColor(QPalette::Base, QColor("#000000"));
        highContrast.setColor(QPalette::WindowText, QColor("#FFFFFF"));
        highContrast.setColor(QPalette::Highlight, QColor("#FFFF00"));
        highContrast.setColor(QPalette::HighlightedText, QColor("#000000"));
        manager.setSystemPalette(highContrast);

        QSignalSpy policyChanged(&manager, &ThemeManager::accessibilityPolicyChanged);
        manager.setForcedContrast(true);
        QVERIFY(manager.resolvedTheme().forcedContrast);
        QCOMPARE(manager.resolvedTheme().colors.canvas, QColor("#000000"));
        QCOMPARE(manager.resolvedTheme().colors.focus, QColor("#FFFF00"));
        QCOMPARE(manager.mode(), ThemeMode::Dark);
        QCOMPARE(manager.accent(), chosenAccent);

        highContrast.setColor(QPalette::Highlight, QColor("#00FFFF"));
        manager.setSystemPalette(highContrast);
        QCOMPARE(manager.resolvedTheme().colors.focus, QColor("#00FFFF"));

        manager.setReducedMotion(true);
        QVERIFY(!manager.metrics().animationsEnabled);
        QCOMPARE(manager.metrics().animationDurationMs, 0);
        QCOMPARE(manager.density(), Density::Comfortable);
        QCOMPARE(manager.metrics().controlHeight, 40);
        QCOMPARE(policyChanged.count(), 2);

        manager.setForcedContrast(false);
        QVERIFY(!manager.resolvedTheme().forcedContrast);
        QCOMPARE(manager.mode(), ThemeMode::Dark);
        QCOMPARE(manager.accent(), chosenAccent);
    }

    void platformAccessibilityReaderProducesBoundedBooleanPolicy() {
        const auto preferences = choscordb::design::readPlatformAccessibilityPreferences();
        QVERIFY(preferences.forcedContrast == true || preferences.forcedContrast == false);
        QVERIFY(preferences.reducedMotion == true || preferences.reducedMotion == false);
    }

    void applicationStylingIsGeneratedFromResolvedTokensAndUpdatesLive() {
        using namespace choscordb::design;
        const auto originalPalette = qApp->palette();
        const auto originalStyleSheet = qApp->styleSheet();

        ThemeManager manager;
        manager.installOn(qApp);
        QCOMPARE(qApp->palette().color(QPalette::Window), manager.resolvedTheme().colors.canvas);
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("min-height: 32px")));
        QVERIFY(!qApp->styleSheet().contains('%'));
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("QLabel[state=\"error\"]")));

        manager.setMode(ThemeMode::Dark);
        manager.setDensity(Density::Comfortable);
        QCOMPARE(qApp->palette().color(QPalette::Window), manager.resolvedTheme().colors.canvas);
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("min-height: 40px")));
        QVERIFY(qApp->styleSheet().contains(manager.resolvedTheme().colors.focus.name()));

        manager.installOn(nullptr);
        qApp->setPalette(originalPalette);
        qApp->setStyleSheet(originalStyleSheet);
    }
    void reusableControlStatesRemainKeyboardObservableInEveryThemeAndDensity() {
        using namespace choscordb::design;
        const auto originalPalette = qApp->palette();
        const auto originalStyleSheet = qApp->styleSheet();
        ThemeManager manager;
        manager.installOn(qApp);
        QWidget host;
        QVBoxLayout layout(&host);
        InspectableButton button("Run", &host);
        button.setAccessibleName("Run statement");
        button.setCheckable(true);
        layout.addWidget(&button);
        host.show();

        for (const auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            manager.setMode(mode);
            for (const auto density : {Density::Compact, Density::Comfortable}) {
                manager.setDensity(density);
                button.setEnabled(true);
                button.setChecked(false);
                button.setFocus();
                QCoreApplication::processEvents();
                QVERIFY(button.styleOption().state.testFlag(QStyle::State_HasFocus));
                QCOMPARE(button.accessibleName(), QString("Run statement"));

                QEnterEvent enter(QPointF(1, 1), QPointF(1, 1), QPointF(1, 1));
                QCoreApplication::sendEvent(&button, &enter);
                button.setAttribute(Qt::WA_UnderMouse, true);
                QVERIFY(button.testAttribute(Qt::WA_UnderMouse));

                QTest::mousePress(&button, Qt::LeftButton);
                QVERIFY(button.styleOption().state.testFlag(QStyle::State_Sunken));
                QTest::mouseRelease(&button, Qt::LeftButton);
                QVERIFY(button.isChecked());
                QVERIFY(button.styleOption().state.testFlag(QStyle::State_On));

                button.setEnabled(false);
                QVERIFY(!button.styleOption().state.testFlag(QStyle::State_Enabled));
            }
        }

        manager.installOn(nullptr);
        qApp->setPalette(originalPalette);
        qApp->setStyleSheet(originalStyleSheet);
    }

    void requiredSemanticIconsRenderAtCommonOpticalSizes() {
        using namespace choscordb::design;
        for (const auto role : {Icon::AppMark, Icon::Run, Icon::Cancel, Icon::Add}) {
            QVERIFY(iconResourcePath(role).startsWith(":/icons/"));
            QVERIFY(QFile::exists(iconResourcePath(role)));
            QVERIFY(iconResourceDecodes(role));
            QVERIFY(!themedIcon(role, QColor("#2F7DD3"), 16).isNull());
            QVERIFY(!themedIcon(role, QColor("#2F7DD3"), 20).isNull());
            const auto highDpi = themedIcon(role, QColor("#2F7DD3"), 16).pixmap(QSize(16, 16), 2.0);
            QCOMPARE(highDpi.devicePixelRatio(), 2.0);
            QCOMPARE(highDpi.size(), QSize(32, 32));
        }
    }
};

QTEST_MAIN(DesignSystemTest)
#include "design_system_test.moc"
