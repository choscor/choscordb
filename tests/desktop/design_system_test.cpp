#include "design_system/icons.h"
#include "design_system/platform_accessibility.h"
#include "design_system/theme_manager.h"

#include <QApplication>
#include <QDir>
#include <QEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
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
    void toastVariantsHaveDistinctColoredSurfacesInBothThemes() {
        using namespace choscordb::design;
        ThemeManager manager;
        for (const auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            manager.setMode(mode);
            const auto sheet = applicationStyleSheet(manager.resolvedTheme(), manager.metrics());
            const auto expected = mode == ThemeMode::Light
                                      ? QStringList{manager.resolvedTheme().colors.success.name(),
                                                    "#fff3d6", "#fdecea"}
                                      : QStringList{manager.resolvedTheme().colors.success.name(),
                                                    "#473b22", "#492d2b"};
            const auto borders = QStringList{manager.resolvedTheme().colors.success.name(),
                                             manager.resolvedTheme().colors.warning.name(),
                                             manager.resolvedTheme().colors.danger.name()};
            int index = 0;
            for (const auto* variant : {"success", "warning", "danger"}) {
                const auto selector =
                    QStringLiteral("QLabel#toastRegion[variant=\"%1\"]").arg(variant);
                const auto rule = sheet.mid(sheet.indexOf(selector)).section('}', 0, 0);
                QVERIFY2(
                    rule.contains(QStringLiteral("background-color: %1").arg(expected.at(index))),
                    qPrintable(rule));
                QVERIFY2(rule.contains(
                             QStringLiteral("border-left: 4px solid %1").arg(borders.at(index))),
                         qPrintable(rule));
                ++index;
            }
            QVERIFY(sheet.contains(QStringLiteral("QLabel#progressToast[variant=\"progress\"]")));
        }
    }
    void dockTitleUsesThemeText() {
        using namespace choscordb::design;
        ThemeManager manager;
        for (const auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            manager.setMode(mode);
            const auto sheet = applicationStyleSheet(manager.resolvedTheme(), manager.metrics());
            const auto titleRule = sheet.mid(sheet.indexOf(QStringLiteral("QDockWidget::title")));
            QVERIFY(titleRule.startsWith(QStringLiteral("QDockWidget::title")));
            QVERIFY2(
                titleRule.section('}', 0, 0).contains(
                    QStringLiteral("color: %1;").arg(manager.resolvedTheme().colors.text.name())),
                qPrintable(titleRule.section('}', 0, 0)));
        }
    }

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

    void platformUiTypographyUsesReferenceFontAndPreservesMonospace() {
        using namespace choscordb::design;
        const auto ui = resolveTypography(TypographyRole::Ui);
        QCOMPARE(ui.family(), QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
        QCOMPARE(QFontInfo(ui).family(),
                 QFontInfo(QFontDatabase::systemFont(QFontDatabase::GeneralFont)).family());
        QCOMPARE(ui.pixelSize(), 13);
        QCOMPARE(ui.weight(), QFont::Normal);
        const auto heading = resolveTypography(TypographyRole::Heading);
        QCOMPARE(heading.family(), ui.family());
        QCOMPARE(heading.pixelSize(), 14);
        QCOMPARE(heading.weight(), QFont::DemiBold);
        auto expectedMonospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        expectedMonospace.setPixelSize(13);
        QCOMPARE(resolveTypography(TypographyRole::Monospace), expectedMonospace);
    }

    void tokensExposeCopyableReferenceValuesAndDefinitionSources() {
        using namespace choscordb::design;
        const auto tokens = designTokens(ResolvedAppearance::Light);
        const auto value = [&tokens](const QString& name) {
            for (const auto& token : tokens) {
                if (token.name == name) {
                    return token.value;
                }
            }
            return QString{};
        };
        QCOMPARE(value("color.background"), QString("#f6f7f8"));
        QCOMPARE(value("radius.lg"), QString("7px"));
        QCOMPARE(value("radius.xl"), QString("8px"));
        QCOMPARE(value("spacing.2.5"), QString("10px"));
        QCOMPARE(value("focus.width"), QString("2px"));
        QCOMPARE(value("motion.popup"), QString("100ms"));
        QCOMPARE(value("typography.ui.lineHeight"), QString("18px"));
        QCOMPARE(value("elevation.md.blur"), QString("40px"));
        for (const auto& token : tokens) {
            QVERIFY2(
                QFile::exists(
                    QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("../../" + token.source)),
                qPrintable(token.source));
        }
        QCOMPARE(resolveMetrics(Density::Compact, false).controlRadius, 7);
        QCOMPARE(resolveMetrics(Density::Comfortable, false).dialogRadius, 8);
    }

    void pinnedGreenPaletteIgnoresLegacyAccent() {
        using namespace choscordb::design;
        const auto light =
            resolveColors(ResolvedAppearance::Light, Accent::presetColor(AccentPreset::Cobalt));
        const auto dark =
            resolveColors(ResolvedAppearance::Dark, Accent::presetColor(AccentPreset::Rose));
        QCOMPARE(light.canvas, QColor("#f6f7f8"));
        QCOMPARE(light.text, QColor("#222b32"));
        QCOMPARE(dark.canvas, QColor("#171d20"));
        QCOMPARE(dark.surface, QColor("#20272b"));
        QCOMPARE(light,
                 resolveColors(ResolvedAppearance::Light, Accent::custom(QColor("#2468b2"))));
        QCOMPARE(dark, resolveColors(ResolvedAppearance::Dark,
                                     Accent::presetColor(AccentPreset::Cobalt)));
    }

    void semanticSurfaceRolesRetainReferenceDistinctions() {
        using namespace choscordb::design;
        const auto light = resolveColors(ResolvedAppearance::Light, {});
        const auto dark = resolveColors(ResolvedAppearance::Dark, {});
        QCOMPARE(light.background, QColor("#f6f7f8"));
        QCOMPARE(light.primary, QColor("#287f66"));
        QCOMPARE(light.secondary, QColor("#f2f2f2"));
        QCOMPARE(light.ring, QColor("#287f66"));
        QCOMPARE(light.destructive, QColor("#c45d58"));
        QCOMPARE(dark.background, QColor("#171d20"));
        QCOMPARE(dark.card, QColor("#20272b"));
        QCOMPARE(dark.muted, QColor("#303030"));
        QCOMPARE(dark.destructive, QColor("#c45d58"));
        QCOMPARE(dark.border, QColor("#343e43"));
        QCOMPARE(dark.input, QColor("#343e43"));
        QCOMPARE(dark.sidebarPrimary, QColor("#65b493"));
    }

    void obsoleteDensityPreservesChoiceWithoutChangingMetrics() {
        choscordb::design::ThemeManager manager;
        const auto compact = manager.metrics();
        QCOMPARE(compact.grid, 4);
        QCOMPARE(compact.controlHeight, 33);
        QCOMPARE(compact.dataRowHeight, 29);
        QCOMPARE(compact.navigationRowHeight, 33);
        QCOMPARE(compact.workspaceChromeHeight, 35);
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
        QCOMPARE(comfortable, compact);
        QCOMPARE(manager.density(), choscordb::design::Density::Comfortable);
        QCOMPARE(comfortable.controlHeight, 33);
        QCOMPARE(comfortable.dataRowHeight, 29);
        QCOMPARE(comfortable.workspaceChromeHeight, 35);
        QCOMPARE(comfortable.dialogContentSpacing, 16);
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
        QCOMPARE(manager.metrics().controlHeight, 33);
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
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("min-height: 33px")));
        QVERIFY(!qApp->styleSheet().contains('%'));
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("QLabel[state=\"error\"]")));

        manager.setMode(ThemeMode::Dark);
        manager.setDensity(Density::Comfortable);
        QCOMPARE(qApp->palette().color(QPalette::Window), manager.resolvedTheme().colors.canvas);
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("min-height: 33px")));
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
