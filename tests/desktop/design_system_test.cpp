#include "design_system/button/button.h"
#include "design_system/icons.h"
#include "design_system/platform_accessibility.h"
#include "design_system/status_line/status_line.h"
#include "design_system/theme_manager.h"
#include "icon_resource_check.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyleOptionButton>
#include <QToolButton>
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

namespace {
QString css(const QColor& color) {
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}
} // namespace

class DesignSystemTest final : public QObject {
    Q_OBJECT

  private slots:
    void idleStatusLineUsesNeutralSurface() {
        using namespace choscordb::design;
        StatusLine line;
        const auto colors = resolvedThemeForWidget(line).colors;
        QCOMPARE(line.palette().color(QPalette::Window), colors.surfaceRaised);
        QCOMPARE(line.palette().color(QPalette::WindowText), colors.fgMuted);
    }
    void structuredStatusContentIsPlainAndComplete() {
        using namespace choscordb::design;
        StatusLine line;
        QLabel source, outcome, duration, memory, page, rows;
        QPushButton previous, next;
        line.configure({&source, &outcome, &duration, &memory, &page, &rows, &previous, &next});
        line.resize(1200, 80);
        line.show();
        const QString error = "Failed: <backend>\n[Code: 42]";
        line.setContent(
            {"orders", error, "Executed in 12 ms", "4 KiB visible", "Page 2", "10 rows"},
            StatusLine::State::Error);
        QCOMPARE(source.text(), QString("orders"));
        QCOMPARE(outcome.text(), QString("Failed: <backend> [Code: 42]"));
        QCOMPARE(outcome.textFormat(), Qt::PlainText);
        QVERIFY(line.toolTip().contains(error));
        QVERIFY(line.accessibleDescription().contains("4 KiB visible"));
        QVERIFY(memory.geometry().right() < page.geometry().left());
        QVERIFY(rows.geometry().right() < previous.mapTo(&line, QPoint()).x());
        QCOMPARE(line.palette().color(QPalette::Window),
                 resolvedThemeForWidget(line).colors.dangerSurface);
    }
    void narrowStatusLineDropsMemoryThenDurationAndRestores() {
        using namespace choscordb::design;
        StatusLine line;
        QLabel source, outcome, duration, memory, page, rows;
        Button previous({}), next({});
        line.configure({&source, &outcome, &duration, &memory, &page, &rows, &previous, &next});
        line.setContent({"very long source name for a SQL document",
                         "Failed: a very long backend explanation", "Executed in 123 ms",
                         "456 KiB visible", "Page 21", "100 rows"},
                        StatusLine::State::Error);
        line.resize(1200, line.height());
        line.show();
        QVERIFY(memory.isVisible());
        // Find the overflow boundary using this platform's actual font metrics.
        // The compact Details control shares the row with execution metrics.
        int compactWidth = line.width();
        while (memory.isVisible() && compactWidth > 230)
            line.resize(--compactWidth, line.height());
        QVERIFY(!memory.isVisible());
        QVERIFY(duration.isVisible());
        QVERIFY(outcome.isVisible());
        QVERIFY(page.isVisible() && rows.isVisible());
        line.resize(230, line.height());
        QVERIFY(!duration.isVisible());
        QVERIFY(outcome.isVisible());
        QVERIFY(!outcome.text().isEmpty());
        QVERIFY(line.rect().contains(QRect(next.mapTo(&line, QPoint()), next.size())));
        QVERIFY(line.rect().contains(QRect(previous.mapTo(&line, QPoint()), previous.size())));
        QSignalSpy clicked(&next, &QPushButton::clicked);
        QTest::mouseClick(&next, Qt::LeftButton);
        QCOMPARE(clicked.count(), 1);
        QVERIFY(outcome.accessibleDescription().contains("456 KiB visible"));
        line.resize(1200, line.height());
        QVERIFY(memory.isVisible() && duration.isVisible());
        QCOMPARE(source.text(), QString("very long source name for a SQL document"));
        QCOMPARE(previous.variant(), ButtonVariant::Ghost);
        QCOMPARE(next.buttonSize(), ButtonSize::IconSmall);
    }
    void structuredStatusRetainsDetailsPagingAndTransientFeedback() {
        using namespace choscordb::design;
        StatusLine line;
        QLabel source, outcome, page, rows;
        Button previous({}), next({});
        line.configure({&source, &outcome, nullptr, nullptr, &page, &rows, &previous, &next});
        const QString error = QString("backend failure <detail>\n").repeated(30) + "FINAL DETAIL";
        line.setContent({"orders", error, {}, {}, "Page 2", "10 rows"}, StatusLine::State::Error);
        line.resize(300, line.height());
        line.show();
        const int height = line.height();
        auto* details = line.findChild<QToolButton*>("statusDetails");
        QVERIFY(details && details->isVisible());
        QVERIFY(line.rect().contains(details->geometry()));
        QVERIFY(line.rect().contains(QRect(next.mapTo(&line, QPoint()), next.size())));
        QVERIFY(details->geometry().right() < previous.mapTo(&line, QPoint()).x());
        details->click();
        auto* dialog = line.findChild<QDialog*>("statusDetailsDialog");
        QVERIFY(dialog && dialog->isVisible());
        auto* text = dialog->findChild<QPlainTextEdit*>("statusDetailsText");
        QVERIFY(text && text->isReadOnly());
        QCOMPARE(text->toPlainText(), line.accessibleDescription());
        QVERIFY(text->toPlainText().contains(error));
        dialog->reject();
        line.setBusy(true);
        auto* loading = line.findChild<QLabel*>("statusLoadingIcon");
        QVERIFY(loading && loading->isVisible());
        QVERIFY(line.rect().contains(loading->geometry()));
        QCOMPARE(line.palette().color(QPalette::Window),
                 resolvedThemeForWidget(line).colors.surfaceRaised);
        line.setMessage("Finish active work before changing panes.");
        QVERIFY(outcome.accessibleName().contains("Finish active work"));
        QVERIFY(source.accessibleName().contains("orders"));
        line.setMessage({});
        QVERIFY(outcome.accessibleName().contains(error));
        line.setBusy(false);
        QCOMPARE(line.height(), height);
        QCOMPARE(line.palette().color(QPalette::Window),
                 resolvedThemeForWidget(line).colors.dangerSurface);
        QSignalSpy clicked(&next, &QPushButton::clicked);
        next.click();
        QCOMPARE(clicked.count(), 1);
        line.setContent({"orders", "Completed", {}, {}, "Page 2", "10 rows"},
                        StatusLine::State::Success);
        QVERIFY(details->isHidden());
        QVERIFY(loading->isHidden());
        QVERIFY(outcome.textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
        QCOMPARE(line.height(), height);
    }
    void toastVariantsHaveDistinctColoredSurfacesInBothThemes() {
        using namespace choscordb::design;
        ThemeManager manager;
        for (const auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            manager.setMode(mode);
            const auto sheet = applicationStyleSheet(manager.resolvedTheme());
            const auto& colors = manager.resolvedTheme().colors;
            const QStringList expected{css(colors.successSurface), css(colors.warningSurface),
                                       css(colors.dangerSurface)};
            int index = 0;
            for (const auto* variant : {"success", "warning", "danger"}) {
                const auto selector =
                    QStringLiteral("QLabel#toastRegion[variant=\"%1\"]").arg(variant);
                const auto rule = sheet.mid(sheet.indexOf(selector)).section('}', 0, 0);
                QVERIFY2(
                    rule.contains(QStringLiteral("background-color: %1").arg(expected.at(index))),
                    qPrintable(rule));
                QVERIFY2(!rule.contains(QStringLiteral("border-left")), qPrintable(rule));
                ++index;
            }
            const auto progressRule =
                sheet
                    .mid(
                        sheet.indexOf(QStringLiteral("QLabel#progressToast[variant=\"progress\"]")))
                    .section('}', 0, 0);
            QVERIFY2(!progressRule.contains(QStringLiteral("border-left")),
                     qPrintable(progressRule));
        }
    }
    void dockTitleUsesThemeText() {
        using namespace choscordb::design;
        ThemeManager manager;
        for (const auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            manager.setMode(mode);
            const auto sheet = applicationStyleSheet(manager.resolvedTheme());
            const auto titleRule = sheet.mid(sheet.indexOf(QStringLiteral("QDockWidget::title")));
            QVERIFY(titleRule.startsWith(QStringLiteral("QDockWidget::title")));
            QVERIFY2(titleRule.section('}', 0, 0).contains(
                         QStringLiteral("color: %1;").arg(css(manager.resolvedTheme().colors.fg))),
                     qPrintable(titleRule.section('}', 0, 0)));
        }
    }

    void explicitAndSystemThemesResolvePredictably() {
        choscordb::design::ThemeManager manager;

        QCOMPARE(manager.mode(), choscordb::design::ThemeMode::System);
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
        const auto ui = resolveTypography(TypographyRole::Body);
        QCOMPARE(ui.family(), QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
        QCOMPARE(QFontInfo(ui).family(),
                 QFontInfo(QFontDatabase::systemFont(QFontDatabase::GeneralFont)).family());
        QCOMPARE(ui.pixelSize(), 13);
        QCOMPARE(ui.weight(), QFont::Normal);
        const auto heading = resolveTypography(TypographyRole::Title);
        QCOMPARE(heading.family(), ui.family());
        QCOMPARE(heading.pixelSize(), 14);
        QCOMPARE(heading.weight(), QFont::DemiBold);
        auto expectedMonospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        expectedMonospace.setPixelSize(13);
        QCOMPARE(resolveTypography(TypographyRole::Mono), expectedMonospace);
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
        QCOMPARE(value("color.bg"), QString("#f6f7f8"));
        QCOMPARE(value("radius.md"), QString("6px"));
        QCOMPARE(value("radius.lg"), QString("10px"));
        QCOMPARE(value("size.row"), QString("28px"));
        QCOMPARE(value("focus.width"), QString("2px"));
        QCOMPARE(value("motion.popup"), QString("100ms"));
        QCOMPARE(value("typography.body.lineHeight"), QString("18px"));
        QCOMPARE(value("elevation.popover.blur"), QString("40px"));
        for (const auto& token : tokens) {
            QVERIFY2(
                QFile::exists(
                    QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("../../" + token.source)),
                qPrintable(token.source));
        }
    }

    void compactScaleUsesTheFourPixelGrid() {
        using namespace choscordb::design;
        QCOMPARE(dimension(Dimension::ControlExtraSmall), 24);
        QCOMPARE(dimension(Dimension::ControlSmall), 28);
        QCOMPARE(dimension(Dimension::Control), 32);
        QCOMPARE(dimension(Dimension::Row), 28);
        QCOMPARE(dimension(Dimension::Header), 28);
        QCOMPARE(dimension(Dimension::Tab), 32);
        QCOMPARE(dimension(Dimension::Toolbar), 32);
        QCOMPARE(dimension(Dimension::QuickSearchRow), 44);
        QCOMPARE(dimension(Dimension::IconSmall), 12);
        QCOMPARE(dimension(Dimension::Icon), 16);
        QCOMPARE(dimension(Dimension::Checkbox), 16);
        QCOMPARE(dimension(Dimension::SwitchWidth), 32);
        QCOMPARE(dimension(Dimension::SwitchHeight), 18);
        QCOMPARE(dimension(Dimension::SwitchThumb), 12);
        QCOMPARE(dimension(Dimension::Progress), 4);
        QCOMPARE(dimension(Dimension::Scrollbar), 10);
        QCOMPARE(dimension(Dimension::DocumentTabWidth), 124);
        QCOMPARE(radius(Radius::Small), 4);
        QCOMPARE(radius(Radius::Medium), 6);
        QCOMPARE(radius(Radius::Large), 10);
        for (const auto value : {Spacing::Half, Spacing::One, Spacing::OneHalf, Spacing::Two,
                                 Spacing::Three, Spacing::Four, Spacing::Six, Spacing::Eight})
            QCOMPARE(spacing(value) % 2, 0);
    }

    void pinnedPaletteResolvesFixedCanvasRoles() {
        using namespace choscordb::design;
        const auto light = resolveColors(ResolvedAppearance::Light);
        const auto dark = resolveColors(ResolvedAppearance::Dark);
        QCOMPARE(light.bg, QColor("#f6f7f8"));
        QCOMPARE(light.fg, QColor("#222b32"));
        QCOMPARE(dark.bg, QColor("#171d20"));
        QCOMPARE(dark.surface, QColor("#20272b"));
    }

    void semanticSurfaceRolesRetainReferenceDistinctions() {
        using namespace choscordb::design;
        const auto light = resolveColors(ResolvedAppearance::Light);
        const auto dark = resolveColors(ResolvedAppearance::Dark);
        QCOMPARE(light.bg, QColor("#f6f7f8"));
        QCOMPARE(light.primary, QColor("#c2410c"));
        QCOMPARE(light.surfaceRaised, QColor("#eef1f2"));
        QCOMPARE(light.ring, QColor("#c2410c"));
        QCOMPARE(dark.bg, QColor("#171d20"));
        QCOMPARE(dark.surface, QColor("#20272b"));
        QCOMPARE(dark.surfaceRaised, QColor("#283135"));
        QCOMPARE(dark.border, QColor("#343e43"));
        QCOMPARE(dark.primary, QColor("#ff8a18"));
    }

    void layoutMetricsKeepWindowDefaults() {
        using namespace choscordb::design;
        const auto layout = layoutMetrics();
        QCOMPARE(layout.narrowWorkspaceWidth, 1100);
        QCOMPARE(layout.defaultWorkspaceWidth, 1280);
        QCOMPARE(layout.minimumWorkspaceWidth, 960);
        QCOMPARE(dialogInitialSize(DialogSize::Ddl), QSize(700, 500));
        QVERIFY(!resolveTypography(TypographyRole::Mono).family().isEmpty());
    }

    void paletteResolvesReadableComponentRoles() {
        using namespace choscordb::design;
        for (const auto appearance : {ResolvedAppearance::Light, ResolvedAppearance::Dark}) {
            const auto colors = resolveColors(appearance);
            QVERIFY(contrastRatio(colors.primaryFg, colors.primary) >= 4.5);
            QVERIFY(contrastRatio(colors.primaryFg, colors.primaryHover) >= 4.5);
            QVERIFY(contrastRatio(colors.primaryFg, colors.primaryPressed) >= 4.5);
            QVERIFY(contrastRatio(colors.primaryHover, colors.bg) >= 3.0);
            QVERIFY(contrastRatio(colors.primaryPressed, colors.bg) >= 3.0);
            QVERIFY(contrastRatio(colors.primary, colors.surface) >= 3.0);
            QVERIFY(contrastRatio(colors.primaryHover, colors.surface) >= 3.0);
            QVERIFY(contrastRatio(colors.primaryPressed, colors.surface) >= 3.0);
            QVERIFY(contrastRatio(colors.ring, colors.bg) >= 3.0);
            QVERIFY(contrastRatio(colors.ring, colors.surface) >= 3.0);
            QVERIFY(contrastRatio(colors.fg, colors.selection) >= 4.5);
            QVERIFY(contrastRatio(colors.fg, colors.selection) >= 4.5);
        }
    }

    void colorVocabularyIsExactlyTheSpecifiedSet() {
        using namespace choscordb::design;
        const QStringList expected{"bg",
                                   "surface",
                                   "surface-raised",
                                   "sidebar",
                                   "fg",
                                   "fg-muted",
                                   "fg-disabled",
                                   "border",
                                   "primary",
                                   "primary-hover",
                                   "primary-pressed",
                                   "primary-fg",
                                   "selection",
                                   "ring",
                                   "success",
                                   "success-surface",
                                   "warning",
                                   "warning-surface",
                                   "danger",
                                   "danger-surface",
                                   "backdrop",
                                   "switch-track",
                                   "code-keyword",
                                   "code-string",
                                   "code-number",
                                   "code-comment"};
        QPalette contrast;
        contrast.setColor(QPalette::Window, Qt::black);
        contrast.setColor(QPalette::WindowText, Qt::white);
        contrast.setColor(QPalette::Base, Qt::black);
        contrast.setColor(QPalette::Text, Qt::white);
        contrast.setColor(QPalette::Highlight, Qt::cyan);
        contrast.setColor(QPalette::HighlightedText, Qt::black);
        for (const auto& colors :
             {resolveColors(ResolvedAppearance::Light), resolveColors(ResolvedAppearance::Dark),
              resolveForcedContrastColors(contrast)}) {
            QStringList names;
            for (const auto& [name, value] : colorTokens(colors)) {
                names.append(name);
                QVERIFY2(value.isValid(), qPrintable(name));
            }
            QCOMPARE(names, expected);
        }
        QStringList catalog;
        for (const auto& token : designTokens(ResolvedAppearance::Light))
            if (token.name.startsWith(QStringLiteral("color.")))
                catalog.append(token.name.mid(6));
        QCOMPARE(catalog, expected);
    }

    void textAndFocusPairsMeetContrastFloors() {
        using namespace choscordb::design;
        for (const auto appearance : {ResolvedAppearance::Light, ResolvedAppearance::Dark}) {
            const auto c = resolveColors(appearance);
            const QList<std::tuple<const char*, QColor, QColor>> text{
                {"fg/bg", c.fg, c.bg},
                {"fg/surface", c.fg, c.surface},
                {"fg/surface-raised", c.fg, c.surfaceRaised},
                {"fg/selection", c.fg, c.selection},
                {"fg-muted/bg", c.fgMuted, c.bg},
                {"fg-muted/surface", c.fgMuted, c.surface},
                {"fg-muted/surface-raised", c.fgMuted, c.surfaceRaised},
                {"primary-fg/primary", c.primaryFg, c.primary},
                {"primary-fg/primary-hover", c.primaryFg, c.primaryHover},
                {"primary-fg/primary-pressed", c.primaryFg, c.primaryPressed},
                {"primary/surface", c.primary, c.surface},
                {"success/success-surface", c.success, c.successSurface},
                {"success/surface", c.success, c.surface},
                {"warning/warning-surface", c.warning, c.warningSurface},
                {"warning/surface", c.warning, c.surface},
                {"danger/danger-surface", c.danger, c.dangerSurface},
                {"danger/surface", c.danger, c.surface},
                {"code-keyword/surface", c.codeKeyword, c.surface},
                {"code-string/surface", c.codeString, c.surface},
                {"code-number/surface", c.codeNumber, c.surface},
                {"code-comment/surface", c.codeComment, c.surface}};
            for (const auto& [name, foreground, background] : text)
                QVERIFY2(contrastRatio(foreground, background) >= 4.5, name);
            QVERIFY(contrastRatio(c.ring, c.bg) >= 3.0);
            QVERIFY(contrastRatio(c.ring, c.surface) >= 3.0);
        }
    }

    void orangeBrandReservesGreenForSuccess() {
        using namespace choscordb::design;
        const auto light = resolveColors(ResolvedAppearance::Light);
        const auto dark = resolveColors(ResolvedAppearance::Dark);
        QCOMPARE(light.primary, QColor("#c2410c"));
        QCOMPARE(dark.primary, QColor("#ff8a18"));
        for (const auto& colors : {light, dark}) {
            QCOMPARE(colors.ring, colors.primary);
            QCOMPARE(colors.codeString, colors.primary);
            for (const auto& [name, value] : colorTokens(colors)) {
                if (name == QStringLiteral("success") || name == QStringLiteral("success-surface"))
                    continue;
                const int hue = value.hsvHue();
                QVERIFY2(hue < 90 || hue > 170, qPrintable(name));
            }
            const int successHue = colors.success.hsvHue();
            QVERIFY(successHue >= 90 && successHue <= 170);
        }
    }

    void accessibilityPoliciesOverrideRenderingWithoutDeletingChoices() {
        using namespace choscordb::design;
        ThemeManager manager;
        manager.setMode(ThemeMode::Dark);

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
        QCOMPARE(manager.resolvedTheme().colors.bg, QColor("#000000"));
        QCOMPARE(manager.resolvedTheme().colors.ring, QColor("#FFFF00"));
        QCOMPARE(manager.mode(), ThemeMode::Dark);

        highContrast.setColor(QPalette::Highlight, QColor("#00FFFF"));
        manager.setSystemPalette(highContrast);
        QCOMPARE(manager.resolvedTheme().colors.ring, QColor("#00FFFF"));

        manager.setReducedMotion(true);
        QVERIFY(manager.reducedMotion());
        QCOMPARE(motionSpec(Motion::Interaction, manager.reducedMotion()).durationMs, 0);
        QCOMPARE(policyChanged.count(), 2);

        manager.setForcedContrast(false);
        QVERIFY(!manager.resolvedTheme().forcedContrast);
        QCOMPARE(manager.mode(), ThemeMode::Dark);
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
        QCOMPARE(qApp->palette().color(QPalette::Window), manager.resolvedTheme().colors.bg);
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("min-height: 30px")));
        QVERIFY(!qApp->styleSheet().contains('%'));
        QVERIFY(!qApp->styleSheet().contains('@'));
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("QLabel[state=\"error\"]")));

        manager.setMode(ThemeMode::Dark);
        QCOMPARE(qApp->palette().color(QPalette::Window), manager.resolvedTheme().colors.bg);
        QVERIFY(qApp->styleSheet().contains(css(manager.resolvedTheme().colors.ring)));

        manager.installOn(nullptr);
        qApp->setPalette(originalPalette);
        qApp->setStyleSheet(originalStyleSheet);
    }
    void reusableControlStatesRemainKeyboardObservableInEveryTheme() {
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

        manager.installOn(nullptr);
        qApp->setPalette(originalPalette);
        qApp->setStyleSheet(originalStyleSheet);
    }

    void requiredSemanticIconsRenderAtCommonOpticalSizes() {
        using namespace choscordb::design;
        for (const auto role : {Icon::AppMark, Icon::Run, Icon::Cancel, Icon::Add}) {
            QVERIFY(iconResourcePath(role).startsWith(":/icons/"));
            QVERIFY(QFile::exists(iconResourcePath(role)));
            QVERIFY(choscordb::test::iconResourceDecodes(role));
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
