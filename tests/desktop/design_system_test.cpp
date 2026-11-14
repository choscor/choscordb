#include "design_system/button/button.h"
#include "design_system/icons.h"
#include "design_system/platform_accessibility.h"
#include "design_system/spinner/spinner.h"
#include "design_system/status_line/status_line.h"
#include "design_system/theme_manager.h"
#include "design_system/toast_region/toast_region.h"
#include "icon_resource_check.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QHash>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyleOptionButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
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
}

class DesignSystemTest final : public QObject {
    Q_OBJECT

  private slots:
    void idleStatusLineUsesNeutralSurface() {
        using namespace choscordb::design;
        StatusLine line;
        const auto colors = resolvedThemeForWidget(line).colors;
        QCOMPARE(line.palette().color(QPalette::Window), secondaryBanner(colors).fill);
        QCOMPARE(line.palette().color(QPalette::WindowText), secondaryBanner(colors).text);
    }
    void statusLineNoticeExpiresOnlyWhileItStillShows() {
        using namespace choscordb::design;
        StatusLine line;
        QLabel source, outcome, duration, memory, page, rows;
        QPushButton previous, next;
        line.configure({&source, &outcome, &duration, &memory, &page, &rows, &previous, &next});
        line.resize(1200, 80);
        line.show();
        line.setContent({"orders", "Executed", {}, {}, {}, {}}, StatusLine::State::Success);
        line.showNotice("Copied 3 rows", 30);
        QCOMPARE(outcome.text(), QString("Copied 3 rows"));
        QTRY_COMPARE(outcome.text(), QString("Executed"));
        bool passed = false;
        line.showNotice("Copied 3 rows", 30);
        line.showNotice("Reconnecting…", 0);
        QTimer::singleShot(60, &line, [&passed] { passed = true; });
        QTRY_VERIFY(passed);
        QCOMPARE(outcome.text(), QString("Reconnecting…"));
        line.showNotice("Copied 3 rows", 30);
        line.setMessage("Newer message");
        passed = false;
        QTimer::singleShot(60, &line, [&passed] { passed = true; });
        QTRY_VERIFY(passed);
        QCOMPARE(outcome.text(), QString("Newer message"));
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
                 choscordb::design::composite(resolvedThemeForWidget(line).colors.dangerTint,
                                              resolvedThemeForWidget(line).colors.base));
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
        auto* loading = line.findChild<choscordb::design::Spinner*>("statusLoadingIcon");
        QVERIFY(loading && loading->isVisible());
        QVERIFY(line.rect().contains(loading->geometry()));
        QCOMPARE(line.palette().color(QPalette::Window),
                 choscordb::design::secondaryBanner(resolvedThemeForWidget(line).colors).fill);
        line.setMessage("Finish active work before changing panes.");
        QVERIFY(outcome.accessibleName().contains("Finish active work"));
        QVERIFY(source.accessibleName().contains("orders"));
        line.setMessage({});
        QVERIFY(outcome.accessibleName().contains(error));
        line.setBusy(false);
        QCOMPARE(line.height(), height);
        QCOMPARE(line.palette().color(QPalette::Window),
                 choscordb::design::composite(resolvedThemeForWidget(line).colors.dangerTint,
                                              resolvedThemeForWidget(line).colors.base));
        QSignalSpy clicked(&next, &QPushButton::clicked);
        next.click();
        QCOMPARE(clicked.count(), 1);
        line.setContent({"orders", "Completed", {}, {}, "Page 2", "10 rows"},
                        StatusLine::State::Success);
        QVERIFY(details->isHidden());
        QVERIFY(loading->isHidden());
        QVERIFY(outcome.textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
        QCOMPARE(line.height(), height);
        QCOMPARE(line.palette().color(QPalette::Window),
                 choscordb::design::secondaryBanner(resolvedThemeForWidget(line).colors).fill);
        QCOMPARE(outcome.property("statusLineState").toString(), QString("success"));
        QCOMPARE(rows.property("statusLineState").toString(), QString("neutral"));
    }
    void dialogStatusTextStartsAtTheLineInsetWithoutASpinnerGap() {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* status = new choscordb::design::StatusLine(&host);
        layout->addWidget(status);
        host.resize(420, 120);
        host.show();
        status->showProgress("Saving connection…");
        QCoreApplication::processEvents();
        auto* message = status->findChild<QLabel*>("statusMessage");
        auto* spinner = status->findChild<choscordb::design::Spinner*>();
        QVERIFY(message && spinner && spinner->isVisible());
        QVERIFY(message->geometry().left() > spinner->geometry().right());
        status->showError("Could not save the connection.");
        QCoreApplication::processEvents();
        QVERIFY(!spinner->isVisible());
        QCOMPARE(message->geometry().left(), status->layout()->contentsMargins().left());
        status->showInfo("Defaults restored.");
        QCoreApplication::processEvents();
        QCOMPARE(message->geometry().left(), status->layout()->contentsMargins().left());
    }
    void spinnerTurnsWhileVisibleAndStopsForReducedMotion() {
        using namespace choscordb::design;
        QWidget host;
        ResolvedTheme theme{ResolvedAppearance::Light, resolveColors(ResolvedAppearance::Light),
                            false};
        host.setProperty("designTheme", QVariant::fromValue(theme));
        auto* spinner = new Spinner(&host);
        QCOMPARE(spinner->size(), QSize(dimension(Dimension::Icon), dimension(Dimension::Icon)));
        QVERIFY(!spinner->isTurning());
        host.show();
        QVERIFY(spinner->isTurning());
        QTRY_VERIFY(spinner->angle() > 0.0);
        spinner->hide();
        QVERIFY(!spinner->isTurning());
        theme.reducedMotion = true;
        host.setProperty("designTheme", QVariant::fromValue(theme));
        spinner->show();
        QVERIFY(!spinner->isTurning());
        QCOMPARE(spinner->angle(), 0.0);
    }
    void toastVariantsHaveDistinctColoredSurfacesInBothThemes() {
        using namespace choscordb::design;
        ThemeManager manager;
        for (const auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            manager.setMode(mode);
            const auto sheet = applicationStyleSheet(manager.resolvedTheme());
            const auto& colors = manager.resolvedTheme().colors;
            const QStringList expected{
                css(composite(withOpacity(colors.successTint, 0.2), colors.base)),
                css(composite(withOpacity(colors.warningTint, 0.5), colors.base)),
                css(composite(withOpacity(colors.dangerTint, 0.5), colors.base))};
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
                         QStringLiteral("color: %1;")
                             .arg(css(manager.resolvedTheme().colors.textDefault))),
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
        QCOMPARE(value("color.kumo-canvas"), QString("#fbfbfb"));
        QCOMPARE(value("radius.md"), QString("6px"));
        QCOMPARE(value("radius.lg"), QString("8px"));
        QCOMPARE(value("size.row"), QString("34px"));
        QCOMPARE(value("focus.width"), QString("2px"));
        QCOMPARE(value("motion.popup"), QString("150ms"));
        QCOMPARE(value("typography.body.lineHeight"), QString("21px"));
        QCOMPARE(value("elevation.popover.blur"), QString("15px"));
        for (const auto& token : tokens) {
            QVERIFY2(
                QFile::exists(
                    QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("../../" + token.source)),
                qPrintable(token.source));
        }
    }

    void scaleUsesTheKumoClassValues() {
        using namespace choscordb::design;
        QCOMPARE(dimension(Dimension::ControlExtraSmall), 20);
        QCOMPARE(dimension(Dimension::ControlSmall), 26);
        QCOMPARE(dimension(Dimension::Control), 36);
        QCOMPARE(dimension(Dimension::Row), 34);
        QCOMPARE(dimension(Dimension::TableRow), 32);
        QCOMPARE(dimension(Dimension::MenuRow), 33);
        QCOMPARE(dimension(Dimension::Header), 36);
        QCOMPARE(dimension(Dimension::Tab), 36);
        QCOMPARE(dimension(Dimension::DocumentTab), 30);
        QCOMPARE(dimension(Dimension::Toolbar), 36);
        QCOMPARE(dimension(Dimension::IconSmall), 14);
        QCOMPARE(dimension(Dimension::Icon), 16);
        QCOMPARE(dimension(Dimension::Checkbox), 16);
        QCOMPARE(dimension(Dimension::Badge), 20);
        QCOMPARE(dimension(Dimension::SwitchWidth), 36);
        QCOMPARE(dimension(Dimension::SwitchHeight), 18);
        QCOMPARE(dimension(Dimension::SwitchThumb), 18);
        QCOMPARE(dimension(Dimension::SwitchRadius), 5);
        QCOMPARE(dimension(Dimension::Progress), 8);
        QCOMPARE(dimension(Dimension::Scrollbar), 8);
        QCOMPARE(dimension(Dimension::MenuWidth), 144);
        QCOMPARE(dimension(Dimension::ToastWidth), 340);
        QCOMPARE(radius(Radius::Small), 4);
        QCOMPARE(radius(Radius::Medium), 6);
        QCOMPARE(radius(Radius::Large), 8);
        QCOMPARE(radius(Radius::ExtraLarge), 12);
        const QList<std::pair<Spacing, int>> scale{
            {Spacing::Half, 2},  {Spacing::One, 4},      {Spacing::OneHalf, 6},
            {Spacing::Two, 8},   {Spacing::TwoHalf, 10}, {Spacing::Three, 12},
            {Spacing::Four, 16}, {Spacing::Six, 24},     {Spacing::Eight, 32}};
        for (const auto& [value, pixels] : scale)
            QCOMPARE(spacing(value), pixels);
        const auto popover = elevation(Elevation::Popover);
        QCOMPARE(popover.size(), 2);
        QCOMPARE(popover.at(0).y, 10);
        QCOMPARE(popover.at(0).blur, 15);
        QCOMPARE(popover.at(0).spread, -3);
        QCOMPARE(elevation(Elevation::Tooltip).at(0).y, 4);
        QCOMPARE(elevation(Elevation::Dialog).at(0).opacity, 0.03);
        QCOMPARE(motionSpec(Motion::Interaction).durationMs, 100);
        QCOMPARE(motionSpec(Motion::Popup).durationMs, 150);
        QCOMPARE(motionSpec(Motion::Popup, true).durationMs, 0);
        QCOMPARE(motionSpec(Motion::LoaderTurn).durationMs, 2000);
        QCOMPARE(motionSpec(Motion::LoaderDash).durationMs, 1500);
        QCOMPARE(motionSpec(Motion::LoaderTurn, true).durationMs, 0);
    }

    void motionEasingDrivesTheButtonHoverAndTheToastFade() {
        using namespace choscordb::design;
        QEasingCurve standard(QEasingCurve::BezierSpline);
        standard.addCubicBezierSegment(QPointF(0.4, 0), QPointF(0.2, 1), QPointF(1, 1));
        const auto sameCurve = [&standard](const QEasingCurve& curve) {
            for (const qreal progress : {0.25, 0.5, 0.75})
                if (qAbs(curve.valueForProgress(progress) - standard.valueForProgress(progress)) >
                    0.001)
                    return false;
            return true;
        };
        QVERIFY(sameCurve(motionSpec(Motion::Interaction).easing));
        QVERIFY(sameCurve(motionSpec(Motion::Popup).easing));
        QCOMPARE(motionSpec(Motion::LoaderTurn).easing.type(), QEasingCurve::Linear);

        Button button(QStringLiteral("Run"));
        button.show();
        QEnterEvent enter(QPointF(1, 1), QPointF(1, 1), QPointF(1, 1));
        QCoreApplication::sendEvent(&button, &enter);
        const auto* hover = button.findChild<QVariantAnimation*>();
        QVERIFY(hover);
        QVERIFY(sameCurve(hover->easingCurve()));

        QWidget host;
        auto* toast = new choscordb::ToastRegion(&host);
        const auto* fade =
            toast->findChild<QVariantAnimation*>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(fade);
        QVERIFY(sameCurve(fade->easingCurve()));
    }

    void layoutMetricsKeepWindowDefaults() {
        using namespace choscordb::design;
        const auto layout = layoutMetrics();
        QCOMPARE(layout.narrowWorkspaceWidth, 1100);
        QCOMPARE(layout.defaultWorkspaceWidth, 1280);
        QCOMPARE(layout.minimumWorkspaceWidth, 960);
        QVERIFY(!resolveTypography(TextStyle::Body, FontDesign::Monospaced).family().isEmpty());
    }

    void colorTokensAreTheKumoTableInBothModes() {
        using namespace choscordb::design;
        const QList<std::tuple<QString, QString, QString>> expected{
            {"kumo-canvas", "#fbfbfb", "#030303"},
            {"kumo-elevated", "#f8f8f8", "#060606"},
            {"kumo-recessed", "#f3f3f3", "#0b0b0b"},
            {"kumo-overlay", "#fbfbfb", "#262626"},
            {"kumo-base", "#ffffff", "#0f0f0f"},
            {"kumo-control", "#ffffff", "#171717"},
            {"kumo-tint", "#f5f5f5", "#262626"},
            {"kumo-fill", "#e5e5e5", "#262626"},
            {"kumo-contrast", "#060606", "#fafafa"},
            {"kumo-badge-inverted", "#0a0a0a", "#ffffff"},
            {"kumo-brand", "#056dff", "#045ede"},
            {"kumo-brand-hover", "#1447e6", "#1447e6"},
            {"kumo-line", "#0a0a0a1a", "#333333"},
            {"kumo-hairline", "#e9e9e9", "#262626"},
            {"kumo-focus", "#0b0b0b", "#e9e9e9"},
            {"kumo-shadow-edge", "#0000001f", "#ffffff1a"},
            {"kumo-shadow-drop", "#00000014", "#0000004c"},
            {"kumo-arrow-edge", "#0a0a0a1a", "#00000000"},
            {"kumo-arrow-stroke", "#00000000", "#333333"},
            {"kumo-danger", "#fb2c36", "#e7000b"},
            {"kumo-success-tint", "#dcfce791", "#0c542b33"},
            {"kumo-warning-tint", "#ffe89433", "#5731005e"},
            {"kumo-danger-tint", "#ffe2e26b", "#9900032b"},
            {"text-kumo-default", "#171717", "#f5f5f5"},
            {"text-kumo-strong", "#0a0a0a", "#fafafa"},
            {"text-kumo-inverse", "#f5f5f5", "#171717"},
            {"text-kumo-subtle", "#737373", "#a1a1a1"},
            {"text-kumo-inactive", "#d4d4d4", "#525252"},
            {"text-kumo-placeholder", "#a1a1a1", "#737373"},
            {"text-kumo-success", "#006045", "#a4f4cf"},
            {"text-kumo-danger", "#c10007", "#ff6467"},
            {"text-kumo-warning", "#bd6500", "#ff8904"},
            {"text-kumo-badge-inverted", "#ffffff", "#000000"},
            {"text-kumo-badge-neutral-subtle", "#262626", "#e5e5e5"},
            {"white", "#ffffff", "#ffffff"},
            {"code-keyword", "#d73a49", "#a0a0a0"},
            {"code-string", "#032f62", "#99ffe4"},
            {"code-number", "#005cc5", "#ffc799"},
            {"code-comment", "#6a737d", "#8b8b8b94"},
            {"code-key", "#005cc5", "#ffc799"},
        };
        const auto cssHex = [](const QColor& color) {
            auto text = color.name(QColor::HexRgb);
            if (color.alpha() < 255)
                text += QStringLiteral("%1").arg(color.alpha(), 2, 16, QLatin1Char('0'));
            return text;
        };
        const auto light = colorTokens(resolveColors(ResolvedAppearance::Light));
        const auto dark = colorTokens(resolveColors(ResolvedAppearance::Dark));
        QCOMPARE(light.size(), expected.size());
        QCOMPARE(dark.size(), expected.size());
        for (qsizetype index = 0; index < expected.size(); ++index) {
            const auto& [name, lightValue, darkValue] = expected.at(index);
            QCOMPARE(light.at(index).first, name);
            QCOMPARE(dark.at(index).first, name);
            QCOMPARE(cssHex(light.at(index).second), lightValue);
            QCOMPARE(cssHex(dark.at(index).second), darkValue);
        }
        QPalette contrast;
        contrast.setColor(QPalette::Window, Qt::black);
        contrast.setColor(QPalette::WindowText, Qt::white);
        contrast.setColor(QPalette::Base, Qt::black);
        contrast.setColor(QPalette::Highlight, Qt::cyan);
        contrast.setColor(QPalette::HighlightedText, Qt::black);
        const auto forced = colorTokens(resolveForcedContrastColors(contrast));
        QCOMPARE(forced.size(), expected.size());
        for (qsizetype index = 0; index < expected.size(); ++index) {
            QCOMPARE(forced.at(index).first, std::get<0>(expected.at(index)));
            QVERIFY2(forced.at(index).second.isValid(), qPrintable(forced.at(index).first));
        }
        QStringList catalog;
        for (const auto& token : designTokens(ResolvedAppearance::Light))
            if (token.name.startsWith(QStringLiteral("color.")))
                catalog.append(token.name.mid(6));
        QStringList names;
        for (const auto& row : expected)
            names.append(std::get<0>(row));
        QCOMPARE(catalog, names);
    }

    void dialogBackdropIsTheRecessedSurfaceAtEightyPercent() {
        using namespace choscordb::design;
        for (const auto appearance : {ResolvedAppearance::Light, ResolvedAppearance::Dark}) {
            const auto colors = resolveColors(appearance);
            const auto backdrop = dialogBackdrop(colors, choscordb::design::Scrim::Dialog);
            QCOMPARE(backdrop.rgb(), colors.recessed.rgb());
            QCOMPARE(backdrop.alpha(), 204);
        }
    }

    void stateStepsKeepTheirMinimumContrast() {
        using namespace choscordb::design;
        for (const auto appearance : {ResolvedAppearance::Light, ResolvedAppearance::Dark}) {
            const auto c = resolveColors(appearance);
            const auto banner = secondaryBanner(c).fill;
            const QList<std::tuple<const char*, QColor, QColor, double>> steps{
                {"line/base", c.line, c.base, 1.2},
                {"hover tint/base", c.tint, c.base, 1.05},
                {"selection tint/base", c.tint, c.base, 1.05},
                {"sidebar hover base/elevated", c.base, c.elevated, 1.02},
                {"banner hover fill/banner", c.fill, banner, 1.05},
                {"focus ring brand/base", c.brand, c.base, 3.0},
            };
            for (const auto& [name, step, surface, minimum] : steps)
                QVERIFY2(
                    contrastRatio(step, surface) >= minimum,
                    qPrintable(
                        QStringLiteral("%1 %2: %3")
                            .arg(appearance == ResolvedAppearance::Light ? "Light" : "Dark", name)
                            .arg(contrastRatio(step, surface))));
        }
    }

    void kumoTextPairsThatPassKeepTheirContrast() {
        using namespace choscordb::design;
        for (const auto appearance : {ResolvedAppearance::Light, ResolvedAppearance::Dark}) {
            const auto c = resolveColors(appearance);
            const QList<std::tuple<const char*, QColor, QColor>> text{
                {"default/canvas", c.textDefault, c.canvas},
                {"default/base", c.textDefault, c.base},
                {"default/elevated", c.textDefault, c.elevated},
                {"default/tint", c.textDefault, c.tint},
                {"subtle/base", c.textSubtle, c.base},
                {"success/base", c.textSuccess, c.base},
                {"danger/base", c.textDanger, c.base},
                {"white/brand", c.white, c.brand},
                {"white/brand-hover", c.white, c.brandHover},
                {"badge-inverted/badge", c.textBadgeInverted, c.badgeInverted},
                {"badge-neutral-subtle/fill", c.textBadgeNeutralSubtle, c.fill},
                {"code-keyword/base", c.codeKeyword, c.base},
                {"code-string/base", c.codeString, c.base},
                {"code-number/base", c.codeNumber, c.base},
                {"code-comment/base", c.codeComment, c.base}};
            for (const auto& [name, foreground, background] : text)
                QVERIFY2(contrastRatio(foreground, background) >= 4.5, name);
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
        QCOMPARE(manager.resolvedTheme().colors.canvas, QColor("#000000"));
        QCOMPARE(manager.resolvedTheme().colors.brand, QColor("#FFFF00"));
        QCOMPARE(manager.mode(), ThemeMode::Dark);

        highContrast.setColor(QPalette::Highlight, QColor("#00FFFF"));
        manager.setSystemPalette(highContrast);
        QCOMPARE(manager.resolvedTheme().colors.brand, QColor("#00FFFF"));

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
        QCOMPARE(qApp->palette().color(QPalette::Window), manager.resolvedTheme().colors.canvas);
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("min-height: 30px")));
        QVERIFY(!qApp->styleSheet().contains('%'));
        QVERIFY(!qApp->styleSheet().contains('@'));
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("QLabel[state=\"error\"]")));

        manager.setMode(ThemeMode::Dark);
        QCOMPARE(qApp->palette().color(QPalette::Window), manager.resolvedTheme().colors.canvas);
        QVERIFY(qApp->styleSheet().contains(css(manager.resolvedTheme().colors.brand)));

        manager.installOn(nullptr);
        qApp->setPalette(originalPalette);
        qApp->setStyleSheet(originalStyleSheet);
    }
    void liveThemeChangeShowsTheBusyCursorWhileWidgetsRestyle() {
        using namespace choscordb::design;
        const auto originalPalette = qApp->palette();
        const auto originalStyleSheet = qApp->styleSheet();
        ThemeManager manager;
        manager.installOn(qApp);
        class StyleWatcher final : public QWidget {
          public:
            bool busyDuringRestyle = false;

          protected:
            void changeEvent(QEvent* event) override {
                if (event->type() == QEvent::StyleChange && QGuiApplication::overrideCursor())
                    busyDuringRestyle =
                        busyDuringRestyle ||
                        QGuiApplication::overrideCursor()->shape() == Qt::WaitCursor;
                QWidget::changeEvent(event);
            }
        } watcher;
        watcher.show();
        manager.setMode(ThemeMode::Dark);
        QVERIFY(watcher.busyDuringRestyle);
        QVERIFY(!QGuiApplication::overrideCursor());
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
