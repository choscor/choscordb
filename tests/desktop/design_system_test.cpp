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

class DesignSystemTest final : public QObject {
    Q_OBJECT

  private slots:
    void idleStatusLineUsesNeutralSurface() {
        using namespace choscordb::design;
        StatusLine line;
        const auto colors = resolvedThemeForWidget(line).colors;
        QCOMPARE(line.palette().color(QPalette::Window), colors.muted);
        QCOMPARE(line.palette().color(QPalette::WindowText), colors.mutedText);
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
        QCOMPARE(line.palette().color(QPalette::Window), resolvedThemeForWidget(line).colors.muted);
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
            const auto sheet = applicationStyleSheet(manager.resolvedTheme(), manager.metrics());
            const auto expected = mode == ThemeMode::Light
                                      ? QStringList{manager.resolvedTheme().colors.success.name(),
                                                    "#fff3d6", "#fdecea"}
                                      : QStringList{manager.resolvedTheme().colors.success.name(),
                                                    "#473b22", "#492d2b"};
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

    void pinnedGreenPaletteResolvesFixedCanvasRoles() {
        using namespace choscordb::design;
        const auto light = resolveColors(ResolvedAppearance::Light);
        const auto dark = resolveColors(ResolvedAppearance::Dark);
        QCOMPARE(light.canvas, QColor("#f6f7f8"));
        QCOMPARE(light.text, QColor("#222b32"));
        QCOMPARE(dark.canvas, QColor("#171d20"));
        QCOMPARE(dark.surface, QColor("#20272b"));
    }

    void semanticSurfaceRolesRetainReferenceDistinctions() {
        using namespace choscordb::design;
        const auto light = resolveColors(ResolvedAppearance::Light);
        const auto dark = resolveColors(ResolvedAppearance::Dark);
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

    void paletteResolvesReadableComponentRoles() {
        using namespace choscordb::design;
        for (const auto appearance : {ResolvedAppearance::Light, ResolvedAppearance::Dark}) {
            const auto colors = resolveColors(appearance);
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

    void accessibilityPoliciesOverrideRenderingWithoutDeletingChoices() {
        using namespace choscordb::design;
        ThemeManager manager;
        manager.setMode(ThemeMode::Dark);
        manager.setDensity(Density::Comfortable);

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
