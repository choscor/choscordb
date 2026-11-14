#include "design_system/button/button.h"
#include "design_system/colors/colors.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/fonts/fonts.h"
#include "design_system/history_row/history_row.h"
#include "design_system/icons.h"
#include "design_system/kbd/key_hints.h"
#include "design_system/metrics/metrics.h"
#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/panel_state/panel_state.h"
#include "design_system/quick_search/quick_search_dialog.h"
#include "design_system/right_sheet/right_sheet.h"
#include "design_system/spinner/spinner.h"
#include "design_system/status_line/status_line.h"
#include "design_system/tabs/segmented_control.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include "design_system/theme_manager.h"
#include "design_system/toast_region/toast_region.h"
#include "design_system/tree/navigation_tree_view.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGraphicsEffect>
#include <QGridLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QStyleOptionButton>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>

namespace {
int softShadowRowsBelowBody(const QImage& image) {
    const int column = image.width() / 4;
    int bodyBottom = -1;
    for (int y = 0; y < image.height(); ++y)
        if (image.pixelColor(column, y).alpha() == 255)
            bodyBottom = y;
    if (bodyBottom <= 0)
        return 0;
    int rows = 0;
    for (int y = bodyBottom + 1; y < image.height(); ++y) {
        const int alpha = image.pixelColor(column, y).alpha();
        rows += alpha > 0 && alpha < 255;
    }
    return rows;
}
bool darkensBelow(QWidget& window, const QWidget& widget) {
    const QRect rect(widget.mapTo(&window, QPoint(0, 0)), widget.size());
    auto* effect = widget.graphicsEffect();
    if (!effect || rect.bottom() + 3 >= window.height())
        return false;
    const auto pixelBelow = [&] {
        const auto image = window.grab().toImage();
        const qreal ratio = image.devicePixelRatio();
        return image.pixelColor(qRound(rect.center().x() * ratio),
                                qRound((rect.bottom() + 2) * ratio));
    };
    effect->setEnabled(false);
    const auto plain = pixelBelow();
    effect->setEnabled(true);
    return pixelBelow().lightness() < plain.lightness();
}
}

class KumoComponentsTest final : public QObject {
    Q_OBJECT
  private slots:
    void modalDialogShellHasTheKumoLineRing() {
        using namespace choscordb::design;
        QWidget owner;
        owner.resize(800, 600);
        ThemeManager theme;
        theme.setMode(ThemeMode::Dark);
        theme.applyTo(owner);
        owner.show();
        choscordb::DialogShell shell(&owner);
        shell.setAppModal();
        auto* sections = shell.createSections();
        sections->bodyLayout()->addWidget(new QLineEdit(sections));
        shell.show();
        QCoreApplication::processEvents();
        const auto image = shell.grab().toImage();
        const auto colors = resolvedThemeForWidget(shell).colors;
        const int y = qRound(image.height() / 2.0 * image.devicePixelRatio());
        QCOMPARE(image.pixelColor(0, y), colors.line);
        shell.reject();
    }
    void rightSheetEdgeIsTheKumoLine() {
        using namespace choscordb::design;
        QWidget owner;
        owner.resize(900, 600);
        ThemeManager theme;
        theme.setMode(ThemeMode::Dark);
        theme.applyTo(owner);
        owner.show();
        RightSheet sheet(&owner);
        sheet.setTitle("Details");
        sheet.setBody(new QPlainTextEdit(&sheet));
        sheet.sections()->addAction(DialogActionRole::Dismiss, "Close");
        sheet.open();
        QTRY_VERIFY(sheet.isVisible());
        const auto image = sheet.grab().toImage();
        QCOMPARE(image.pixelColor(0, qRound(4 * image.devicePixelRatio())),
                 resolvedThemeForWidget(sheet).colors.line);
        QCOMPARE(image.pixelColor(qRound(2 * image.devicePixelRatio()), image.height() - 1),
                 resolvedThemeForWidget(sheet).colors.elevated);
        auto* close = sheet.findChild<QPushButton*>("rightSheetClose");
        QVERIFY(close);
        QCOMPARE(close->accessibleName(), QString("Close Details"));
        sheet.reject();
    }
    void rightSheetBodyStartsAtTheTitleInset() {
        using namespace choscordb::design;
        QWidget owner;
        owner.resize(900, 600);
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(owner);
        owner.show();
        RightSheet sheet(&owner);
        sheet.setTitle("Details");
        sheet.setBody(new QPlainTextEdit(&sheet));
        sheet.open();
        QTRY_VERIFY(sheet.isVisible());
        QCoreApplication::processEvents();
        auto* title = sheet.findChild<QWidget*>("rightSheetTitle");
        auto* body = sheet.findChild<QWidget*>("rightSheetBody");
        QCOMPARE(body->mapTo(&sheet, QPoint()).x(), title->mapTo(&sheet, QPoint()).x());
        QVERIFY(body->mapTo(&sheet, QPoint()).y() >=
                title->mapTo(&sheet, QPoint()).y() + title->height() + spacing(Spacing::Four));
        sheet.reject();
    }
    void dialogStatusLineLeavesTheBodyToAnExpandingContent() {
        using namespace choscordb::design;
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* sections = new DialogSections(&host);
        layout->addWidget(sections);
        auto* content = new QScrollArea(sections);
        sections->bodyLayout()->addWidget(content);
        sections->addStatusLine();
        host.resize(600, 500);
        host.show();
        QCoreApplication::processEvents();
        auto* body = content->parentWidget();
        QCOMPARE(content->geometry().bottom() + 1 + body->layout()->contentsMargins().bottom(),
                 body->height());
    }
    void confirmationStatusIconUsesTheLargeIconSize() {
        QWidget parent;
        parent.resize(640, 480);
        choscordb::design::ThemeManager theme;
        theme.setMode(choscordb::design::ThemeMode::Light);
        theme.applyTo(parent);
        parent.show();
        choscordb::ConfirmationDialog dialog(QMessageBox::Warning, "Discard changes?",
                                             "The changes in this dialog are not saved.",
                                             QMessageBox::Discard | QMessageBox::Cancel, &parent);
        dialog.show();
        QApplication::processEvents();
        const int large = choscordb::design::dimension(choscordb::design::Dimension::IconLarge);
        QCOMPARE(dialog.iconPixmap().deviceIndependentSize().toSize(), QSize(large, large));
        dialog.reject();
    }
    void loneCloseButtonIsOutlineWhenTheDialogOpens() {
        QWidget parent;
        parent.resize(640, 480);
        choscordb::design::ThemeManager theme;
        theme.setMode(choscordb::design::ThemeMode::Light);
        theme.applyTo(parent);
        parent.show();
        choscordb::ConfirmationDialog dialog(QMessageBox::Critical, "Something went wrong",
                                             "1 error:\n• Could not open", QMessageBox::Close,
                                             &parent);
        dialog.open();
        QTRY_VERIFY(dialog.isVisible());
        QCoreApplication::processEvents();
        auto* close = dialog.buttons().constFirst();
        const auto image = close->grab().toImage();
        const auto fill =
            image.pixelColor(QPoint(4, close->height() / 2) * image.devicePixelRatio());
        QVERIFY2(fill.blue() - fill.red() < 40,
                 qPrintable(QString("Close is filled %1").arg(fill.name())));
        dialog.reject();
    }
    void segmentedControlMatchesPaneTabs() {
        using namespace choscordb::design;
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            QWidget root;
            root.setProperty("designSurface", "sidebar");
            root.setAttribute(Qt::WA_StyledBackground);
            ThemeManager theme;
            theme.setMode(mode);
            theme.applyTo(root);
            auto* layout = new QVBoxLayout(&root);
            auto* control = new SegmentedControl(&root);
            auto* first = new Button({}, control);
            auto* second = new Button({}, control);
            first->setDesignIcon(Icon::Database);
            second->setDesignIcon(Icon::File);
            control->addSegment(first);
            control->addSegment(second);
            layout->addWidget(control);
            root.resize(240, 60);
            root.show();
            first->setChecked(true);
            QCoreApplication::processEvents();
            const auto colors = resolvedThemeForWidget(*control).colors;
            QCOMPARE(control->height(), dimension(Dimension::Tab));
            QCOMPARE(first->height(), dimension(Dimension::Tab) - 2 * spacing(Spacing::Half));
            QVERIFY(first->isCheckable() && second->isCheckable());
            QCOMPARE(first->buttonContext(), ButtonContext::Segment);
            QCOMPARE(first->buttonSize(), ButtonSize::Icon);
            const auto capture = control->grab();
            const auto image = capture.toImage();
            const auto at = [&](QPoint point) {
                return image.pixelColor(point * capture.devicePixelRatio());
            };
            const int middle = control->height() / 2;
            QCOMPARE(at({1, middle}), colors.recessed);
            QCOMPARE(at({first->geometry().left() + 4, middle}), colors.base);
            QCOMPARE(at({first->geometry().left(), middle}),
                     composite(colors.line, colors.recessed));
            QCOMPARE(at({second->geometry().left() + 4, middle}), colors.recessed);
            QTest::mouseMove(second, second->rect().center());
            QCoreApplication::processEvents();
            const auto hovered = control->grab().toImage();
            QCOMPARE(hovered.pixelColor(QPoint(second->geometry().left() + 4, middle) *
                                        capture.devicePixelRatio()),
                     colors.recessed);
        }
    }
    void sidebarTreeUsesBodyText() {
        using namespace choscordb::design;
        QWidget root;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(root);
        NavigationTreeView tree(&root);
        root.show();
        tree.ensurePolished();
        QCOMPARE(tree.font().pixelSize(), resolveTypography(TextStyle::Body).pixelSize());
    }
    void dialogSectionsUseTheKumoDialogPaddingAndGap() {
        choscordb::design::DialogSections sections;
        const auto body = sections.bodyLayout()->contentsMargins();
        QCOMPARE(body.left(), 24);
        QCOMPARE(body.right(), 24);
        QCOMPARE(body.top(), 16);
        QCOMPARE(body.bottom(), 24);
        QCOMPARE(sections.bodyLayout()->spacing(), 16);
        const auto header = sections.headerLayout()->contentsMargins();
        QCOMPARE(header.left(), 24);
        QCOMPARE(header.top(), 16);
        QCOMPARE(header.bottom(), 16);
        QCOMPARE(header.right(), 24);
    }
    void confirmationUsesTheKumoBaseDialogWidthAndPadding() {
        QWidget owner;
        owner.resize(960, 640);
        owner.show();
        choscordb::ConfirmationDialog dialog(QMessageBox::Question, "Confirm", "Continue?",
                                             QMessageBox::Cancel, &owner);
        dialog.show();
        QCoreApplication::processEvents();
        QCOMPARE(dialog.width(), 384);
        auto* grid = dialog.findChild<QGridLayout*>();
        QVERIFY(grid);
        QCOMPARE(grid->contentsMargins(), QMargins(24, 24, 24, 24));
        QCOMPARE(grid->verticalSpacing(), 16);
        QCOMPARE(grid->horizontalSpacing(), 12);
        dialog.reject();
    }
    void quickSearchUsesTheKumoCommandPalette() {
        using namespace choscordb::design;
        QWidget owner;
        owner.resize(1200, 800);
        owner.setAutoFillBackground(true);
        ThemeManager theme;
        theme.applyTo(owner);
        owner.show();
        QVERIFY(QTest::qWaitForWindowExposed(&owner));
        const auto ownerPixel = owner.grab().toImage().pixelColor(20, 700);
        QuickSearchDialog dialog(&owner);
        dialog.setResults({{"Table", "orders", "main", "a", Icon::Table},
                           {"Table", "customers", "main", "b", Icon::Table}});
        dialog.show();
        QCoreApplication::processEvents();
        const auto colors = resolvedThemeForWidget(dialog).colors;
        QCOMPARE(dialog.width(), 672);
        const auto surface = dialog.grab().toImage();
        QVERIFY(surface.pixelColor(2, 2).alpha() > 0);
        auto* header = dialog.findChild<QWidget*>("quickSearchHeader");
        QVERIFY(header);
        QVERIFY(header->isAncestorOf(dialog.findChild<QLineEdit*>("quickSearchInput")));
        QVERIFY(header->findChild<QLabel*>("quickSearchIcon"));
        QCOMPARE(header->layout()->contentsMargins(), QMargins(16, 12, 16, 12));
        auto* footer = dialog.findChild<QWidget*>("quickSearchFooter");
        QVERIFY(footer);
        QVERIFY(footer->isAncestorOf(dialog.findChild<QWidget*>("quickSearchKeyHints")));
        QCOMPARE(footer->grab().toImage().pixelColor(2, footer->height() / 2), colors.elevated);
        auto* list = dialog.findChild<QListWidget*>("quickSearchResults");
        QVERIFY(list);
        QCOMPARE(list->item(0)->sizeHint().height(), dimension(Dimension::MenuRow));
        auto* row = list->itemWidget(list->item(0));
        auto* title = row->findChild<QLabel*>("quickSearchResultTitle");
        auto* detail = row->findChild<QLabel*>("quickSearchResultDetail");
        QVERIFY(detail->geometry().left() > title->geometry().right());
        QCOMPARE(detail->geometry().center().y(), title->geometry().center().y());
        QCOMPARE(detail->font().pixelSize(), 13);
        list->setCurrentRow(0);
        QCoreApplication::processEvents();
        const auto rect = list->visualItemRect(list->item(0));
        QCOMPARE(list->viewport()->geometry().left(), 8);
        const auto viewport = list->viewport()->grab().toImage();
        QCOMPARE(viewport.pixelColor(rect.left() + 3, rect.center().y()), colors.overlay);
        QWidget* backdrop = nullptr;
        for (auto* candidate : owner.findChildren<QWidget*>("modalBackdrop"))
            if (candidate->isVisible())
                backdrop = candidate;
        QVERIFY(backdrop);
        const auto dim = owner.grab().toImage().pixelColor(20, 700);
        const auto scrim = colors.overlay;
        const auto mix = [](int over, int under) { return qRound(over * 0.8 + under * 0.2); };
        QVERIFY(qAbs(dim.red() - mix(scrim.red(), ownerPixel.red())) <= 1);
        dialog.reject();
    }
    void dangerToastUsesTheKumoToastCard() {
        using namespace choscordb::design;
        QWidget owner;
        owner.resize(900, 600);
        ThemeManager theme;
        theme.setMode(ThemeMode::Dark);
        theme.applyTo(owner);
        owner.show();
        choscordb::ToastRegion toast;
        toast.attachTo(&owner);
        toast.showToast("Query failed", "Synthetic detail", choscordb::ToastVariant::Danger);
        QTRY_VERIFY(toast.isVisible());
        const auto colors = resolvedThemeForWidget(toast).colors;
        QCOMPARE(owner.width() - toast.geometry().right() - 1, 32);
        QCOMPARE(owner.height() - toast.geometry().bottom() - 1, 32);
        QCOMPARE(toast.contentsMargins().top() > 0, true);
        auto* icon = toast.findChild<QLabel*>("toastIcon");
        auto* dismiss = toast.findChild<QPushButton*>("toastDismiss");
        QVERIFY(icon && dismiss);
        QCOMPARE(icon->x(), 16);
        QCOMPARE(icon->y(), 18);
        QCOMPARE(dismiss->y(), 8);
        QCOMPARE(toast.width() - dismiss->geometry().right() - 1, 8);
        QCOMPARE(toast.contentsMargins().left(), 16 + 16 + 8);
        QVERIFY(toast.text().contains(QStringLiteral("font-weight:500")));
        QVERIFY(toast.text().contains(colors.textDanger.name()));
        QVERIFY(toast.text().contains(QStringLiteral("#b0b0b0")));
        const auto danger = toast.grab().toImage();
        const int middle = qRound(danger.height() / 2.0 * danger.devicePixelRatio());
        const auto dangerEdge = danger.pixelColor(0, middle);
        QVERIFY(dangerEdge.red() > dangerEdge.green() + 8);
        QVERIFY(danger.pixelColor(2, 2).alpha() < 255);
        choscordb::ToastRegion progressToast;
        progressToast.attachTo(&owner);
        progressToast.showProgress("Exporting", "Synthetic rows");
        QTRY_VERIFY(progressToast.isVisible());
        auto* progressSpinner = progressToast.findChild<Spinner*>("toastSpinner");
        QVERIFY(progressSpinner && progressSpinner->isVisible());
        QVERIFY(!progressToast.findChild<QLabel*>("toastIcon")->isVisible());
        QVERIFY(!toast.findChild<Spinner*>("toastSpinner")->isVisible());
        const auto progress = progressToast.grab().toImage();
        QCOMPARE(progress.pixelColor(0, middle), colors.line);
        QCOMPARE(progress.pixelColor(progress.width() / 2, 3), colors.base);
    }
    void statusLineUsesTheKumoSecondaryBanner() {
        using namespace choscordb::design;
        QWidget owner;
        ThemeManager theme;
        theme.setMode(ThemeMode::Dark);
        theme.applyTo(owner);
        auto* line = new StatusLine(&owner);
        line->setMessage("Synthetic status");
        owner.show();
        QCoreApplication::processEvents();
        QCOMPARE(line->palette().color(QPalette::Window), QColor("#1b1b1b"));
        QCOMPARE(line->palette().color(QPalette::WindowText), QColor("#b4b4b4"));
        QCOMPARE(line->font().pixelSize(), 13);
    }
    void badgesUseTheKumoBadgeVariants() {
        using namespace choscordb::design;
        QWidget owner;
        ThemeManager theme;
        theme.applyTo(owner);
        auto* layout = new QVBoxLayout(&owner);
        QList<QLabel*> badges;
        for (const auto* variant : {"", "secondary", "outline", "error", "success", "warning"}) {
            auto* badge = new QLabel("Badge", &owner);
            badge->setProperty("designRole", "badge");
            if (*variant)
                badge->setProperty("variant", variant);
            layout->addWidget(badge);
            badges.append(badge);
        }
        owner.show();
        QCoreApplication::processEvents();
        const auto colors = resolvedThemeForWidget(owner).colors;
        const QList<QColor> fills{colors.badgeInverted, colors.fill,        colors.base,
                                  colors.dangerTint,    colors.successTint, colors.warningTint};
        for (int i = 0; i < badges.size(); ++i) {
            const auto image = badges[i]->grab().toImage();
            const auto scale = image.devicePixelRatio();
            const auto fill =
                image.pixelColor(qRound(badges[i]->width() / 2.0 * scale), qRound(3 * scale));
            QVERIFY2(qAbs(fill.red() - fills[i].red()) <= 2 &&
                         qAbs(fill.green() - fills[i].green()) <= 2 &&
                         qAbs(fill.blue() - fills[i].blue()) <= 2,
                     qPrintable(fill.name() + " " + fills[i].name()));
        }
        const auto primary = owner.grab(badges[0]->geometry()).toImage();
        const auto primaryScale = primary.devicePixelRatio();
        QVERIFY(primary.pixelColor(qRound(2 * primaryScale), qRound(1 * primaryScale)).lightness() >
                128);
        QCOMPARE(badges[0]->palette().color(QPalette::WindowText), colors.textBadgeInverted);
        QCOMPARE(badges[1]->palette().color(QPalette::WindowText), colors.textBadgeNeutralSubtle);
        QCOMPARE(badges[3]->palette().color(QPalette::WindowText), colors.textDanger);
    }
    void keyHintsUseTheKumoInlineCode() {
        using namespace choscordb::design;
        QWidget owner;
        ThemeManager theme;
        theme.applyTo(owner);
        auto* layout = new QVBoxLayout(&owner);
        auto* hints = new KeyHints({{"Esc", "Close"}}, &owner);
        layout->addWidget(hints);
        owner.show();
        QCoreApplication::processEvents();
        QLabel* key = nullptr;
        for (auto* label : hints->findChildren<QLabel*>())
            if (label->property("designRole") == "kbd")
                key = label;
        QVERIFY(key);
        const auto colors = resolvedThemeForWidget(owner).colors;
        const auto mono = resolveTypography(TextStyle::Callout, FontDesign::Monospaced);
        QCOMPARE(key->font().family(), mono.family());
        QCOMPARE(key->font().pixelSize(), 13);
        QCOMPARE(key->font().weight(), QFont::Normal);
        const auto image = owner.grab(QRect(key->mapTo(&owner, QPoint()), key->size())).toImage();
        const auto scale = image.devicePixelRatio();
        QCOMPARE(image.pixelColor(qRound(key->width() / 2.0 * scale), 0), colors.hairline);
        QCOMPARE(image.pixelColor(qRound(key->width() / 2.0 * scale), qRound(2 * scale)),
                 colors.base);
    }
    void spinnerPaintsTheKumoLoaderTrackRing() {
        using namespace choscordb::design;
        QWidget owner;
        ThemeManager theme;
        theme.setReducedMotion(true);
        theme.applyTo(owner);
        auto* layout = new QVBoxLayout(&owner);
        auto* spinner = new Spinner(&owner);
        layout->addWidget(spinner);
        owner.show();
        QCoreApplication::processEvents();
        const auto image = spinner->grab().toImage();
        const auto scale = image.devicePixelRatio();
        const int size = spinner->width();
        const qreal radius = size * 9.5 / 24;
        const QPointF center(size / 2.0, size / 2.0);
        QCOMPARE(image.pixelColor(qRound(center.x() * scale), qRound(center.y() * scale)).alpha(),
                 0);
        int ringPixels = 0;
        int faintest = 255;
        QColor strongest(Qt::transparent);
        for (int step = 0; step < 64; ++step) {
            const qreal angle = step * M_PI / 32;
            const QPointF point = center + QPointF(qCos(angle), qSin(angle)) * radius;
            const auto pixel =
                image.pixelColor(qFloor(point.x() * scale), qFloor(point.y() * scale));
            if (pixel.alpha() > 0)
                ++ringPixels;
            faintest = qMin(faintest, pixel.alpha());
            if (pixel.alpha() > strongest.alpha())
                strongest = pixel;
        }
        QCOMPARE(ringPixels, 64);
        QVERIFY(faintest < 64);
        QVERIFY(strongest.alpha() > 200);
        const auto subtle = resolvedThemeForWidget(owner).colors.textSubtle;
        QVERIFY(qAbs(strongest.red() - subtle.red()) <= 8);
    }
    void progressBarUsesTheKumoMeterTrack() {
        using namespace choscordb::design;
        QWidget owner;
        ThemeManager theme;
        theme.applyTo(owner);
        auto* layout = new QVBoxLayout(&owner);
        auto* bar = new QProgressBar(&owner);
        bar->setTextVisible(false);
        bar->setRange(0, 100);
        bar->setValue(25);
        layout->addWidget(bar);
        owner.resize(320, 80);
        owner.show();
        QCoreApplication::processEvents();
        const auto colors = resolvedThemeForWidget(owner).colors;
        const auto image = owner.grab(bar->geometry()).toImage();
        const auto scale = image.devicePixelRatio();
        const int middle = qRound(bar->height() / 2.0 * scale);
        QCOMPARE(image.pixelColor(qRound(bar->width() * 0.75 * scale), middle), colors.fill);
        QVERIFY(image.pixelColor(qRound((bar->width() - 1) * scale), 0) != colors.fill);
    }
    void panelStateIsPlainCenteredTextWithoutACard() {
        using namespace choscordb::design;
        QWidget owner;
        ThemeManager theme;
        theme.applyTo(owner);
        auto* layout = new QVBoxLayout(&owner);
        auto* state = createPanelStateText("No saved queries.", &owner);
        layout->addWidget(state);
        owner.resize(360, 200);
        owner.show();
        QCoreApplication::processEvents();
        const auto colors = resolvedThemeForWidget(owner).colors;
        const auto image = owner.grab(state->geometry()).toImage();
        const auto scale = image.devicePixelRatio();
        const int middle = qRound(state->height() / 2.0 * scale);
        QCOMPARE(image.pixelColor(0, middle), image.pixelColor(qRound(3 * scale), middle));
        QVERIFY(image.pixelColor(0, middle) != colors.fill);
        QCOMPARE(state->alignment() & Qt::AlignHCenter, Qt::AlignHCenter);
        QCOMPARE(state->contentsRect().top() - state->rect().top() >= 24, true);
        QCOMPARE(state->contentsRect().left() - state->rect().left() >= 16, true);
    }
    void formLabelsUseTheKumoLabel() {
        using namespace choscordb::design;
        QWidget owner;
        ThemeManager theme;
        theme.applyTo(owner);
        auto* layout = new QVBoxLayout(&owner);
        auto* sections = new DialogSections(&owner);
        layout->addWidget(sections);
        auto* form = sections->addForm();
        auto* field = new QLineEdit(sections);
        sections->addFormRow(form, "Host", field);
        owner.show();
        QCoreApplication::processEvents();
        auto* caption = qobject_cast<QLabel*>(form->labelForField(field));
        QVERIFY(caption);
        QCOMPARE(caption->font().pixelSize(), 14);
        QCOMPARE(caption->font().weight(), QFont::Medium);
        QCOMPARE(caption->palette().color(QPalette::WindowText),
                 resolvedThemeForWidget(owner).colors.textDefault);
    }
    void rowDelegatesShowTheRingOnlyAfterKeyboardFocus() {
        using namespace choscordb::design;
        QListWidget owner;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(owner);
        owner.addItem("orders");
        owner.show();
        QCoreApplication::processEvents();
        const auto ring = resolvedThemeForWidget(owner).colors.brand;
        const auto ringPixels = [&](const QStyledItemDelegate& delegate) {
            QStyleOptionViewItem option;
            option.initFrom(&owner);
            option.widget = &owner;
            option.rect = QRect(0, 0, 320, 2 * dimension(Dimension::Row));
            option.state =
                QStyle::State_Enabled | QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange;
            QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            delegate.paint(&painter, option, owner.model()->index(0, 0));
            painter.end();
            int count = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    count += image.pixelColor(x, y) == ring;
            return count;
        };
        const NavigationProfileDelegate profile;
        const RecentHistoryRowDelegate history;
        owner.setAttribute(Qt::WA_KeyboardFocusChange, false);
        QCOMPARE(ringPixels(profile), 0);
        QCOMPARE(ringPixels(history), 0);
        owner.setAttribute(Qt::WA_KeyboardFocusChange, true);
        QVERIFY(ringPixels(profile) > 20);
        QVERIFY(ringPixels(history) > 20);
    }
    void choiceControlsShowTheRingAfterTabFocus() {
        using namespace choscordb::design;
        QWidget owner;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.installOn(qApp);
        auto* layout = new QVBoxLayout(&owner);
        auto* start = new QLineEdit(&owner);
        auto* check = new QCheckBox("Remember", &owner);
        auto* radio = new QRadioButton("Read only", &owner);
        auto* toggle = new QCheckBox("Keep alive", &owner);
        toggle->setProperty("designRole", "switch");
        for (QWidget* widget : QList<QWidget*>{start, check, radio, toggle})
            layout->addWidget(widget);
        owner.resize(320, 200);
        owner.show();
        owner.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&owner));
        start->setFocus(Qt::MouseFocusReason);
        const auto ring = resolvedThemeForWidget(owner).colors.brand;
        const auto ringPixels = [&](QAbstractButton* widget) {
            QStyleOptionButton option;
            option.initFrom(widget);
            const auto element = qobject_cast<QRadioButton*>(widget)
                                     ? QStyle::SE_RadioButtonIndicator
                                     : QStyle::SE_CheckBoxIndicator;
            const auto indicator = widget->style()->subElementRect(element, &option, widget);
            const int reach = 2 * focusSpec().ringWidth;
            const auto image = owner
                                   .grab(indicator.adjusted(-reach, -reach, reach, reach)
                                             .translated(widget->mapTo(&owner, QPoint())))
                                   .toImage();
            int count = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    count += image.pixelColor(x, y) == ring;
            return count;
        };
        for (QAbstractButton* widget : QList<QAbstractButton*>{check, radio, toggle}) {
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
            QCoreApplication::processEvents();
            QVERIFY(widget->hasFocus());
            QVERIFY2(ringPixels(widget) > 8, qPrintable(widget->text()));
        }
    }
    void stockButtonsShowTheRingOnlyAfterKeyboardFocus() {
        using namespace choscordb::design;
        QWidget owner;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.installOn(qApp);
        auto* layout = new QVBoxLayout(&owner);
        auto* start = new QLineEdit(&owner);
        auto* button = new QPushButton("Apply", &owner);
        button->setFocusPolicy(Qt::StrongFocus);
        layout->addWidget(start);
        layout->addWidget(button);
        owner.resize(320, 120);
        owner.show();
        owner.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&owner));
        const auto ring = resolvedThemeForWidget(owner).colors.brand;
        const auto ringPixels = [&] {
            const int reach = 2 * focusSpec().ringWidth;
            const auto image = owner
                                   .grab(QRect(button->mapTo(&owner, QPoint()), button->size())
                                             .adjusted(-reach, -reach, reach, reach))
                                   .toImage();
            int count = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    count += image.pixelColor(x, y) == ring;
            return count;
        };
        QTest::mouseClick(button, Qt::LeftButton);
        QCoreApplication::processEvents();
        QVERIFY(button->hasFocus());
        QCOMPARE(ringPixels(), 0);
        start->setFocus(Qt::MouseFocusReason);
        QTest::keyClick(start, Qt::Key_Tab);
        QCoreApplication::processEvents();
        QVERIFY(button->hasFocus());
        QVERIFY(ringPixels() > 8);
    }
    void selectPopupsTooltipsAndToastsCastAShadow() {
        using namespace choscordb::design;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.installOn(qApp);
        QWidget owner;
        auto* layout = new QVBoxLayout(&owner);
        auto* select = new QComboBox(&owner);
        select->addItems({"PostgreSQL", "MySQL"});
        auto* hint = new QPushButton("Run", &owner);
        hint->setToolTip("Run the statement");
        layout->addWidget(select);
        layout->addWidget(hint);
        owner.resize(480, 320);
        owner.show();
        QVERIFY(QTest::qWaitForWindowExposed(&owner));
        select->showPopup();
        QTRY_VERIFY(select->view()->isVisible());
        QVERIFY(darkensBelow(owner, *select->view()->parentWidget()));
        select->hidePopup();

        QHelpEvent help(QEvent::ToolTip, hint->rect().center(),
                        hint->mapToGlobal(hint->rect().center()));
        QCoreApplication::sendEvent(hint, &help);
        QCoreApplication::processEvents();
        auto* tooltip = owner.findChild<QWidget*>("designTooltip");
        QVERIFY(tooltip && tooltip->isVisible());
        QVERIFY(softShadowRowsBelowBody(tooltip->grab().toImage()) >= 2);

        choscordb::ToastRegion toast;
        toast.attachTo(&owner);
        toast.showToast("Synthetic notice", {}, choscordb::ToastVariant::Success);
        QTRY_VERIFY(toast.isVisible());
        QVERIFY(darkensBelow(owner, toast));
    }
    void checkedControlsShareTheKumoTint() {
        using namespace choscordb::design;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.installOn(qApp);
        QWidget owner;
        auto* layout = new QVBoxLayout(&owner);
        auto* painted = new Button("Wrap", &owner);
        painted->setVariant(ButtonVariant::Secondary);
        auto* stock = new QPushButton("Wrap", &owner);
        auto* tool = new QToolButton(&owner);
        tool->setText("Wrap");
        tool->setToolButtonStyle(Qt::ToolButtonTextOnly);
        for (QAbstractButton* button : QList<QAbstractButton*>{painted, stock, tool}) {
            button->setCheckable(true);
            button->setChecked(true);
            layout->addWidget(button);
        }
        owner.resize(320, 200);
        owner.show();
        QVERIFY(QTest::qWaitForWindowExposed(&owner));
        QTest::mouseMove(&owner, QPoint(owner.width() - 1, owner.height() - 1));
        const auto tint = resolvedThemeForWidget(owner).colors.tint;
        for (QAbstractButton* button : QList<QAbstractButton*>{painted, stock, tool}) {
            const auto scale = owner.devicePixelRatioF();
            QTRY_COMPARE(owner.grab(QRect(button->mapTo(&owner, QPoint()), button->size()))
                             .toImage()
                             .pixelColor(qRound(6 * scale), qRound(button->height() / 2.0 * scale)),
                         tint);
        }
    }
    void disabledRowsAndCheckLabelsUseTheInactiveText() {
        using namespace choscordb::design;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.installOn(qApp);
        QWidget owner;
        auto* layout = new QVBoxLayout(&owner);
        auto* list = new QListWidget(&owner);
        auto* row = new QListWidgetItem("orders_archive", list);
        row->setFlags(row->flags() & ~Qt::ItemIsEnabled);
        auto* check = new QCheckBox("Remember the password", &owner);
        check->setEnabled(false);
        layout->addWidget(list);
        layout->addWidget(check);
        owner.resize(320, 240);
        owner.show();
        QVERIFY(QTest::qWaitForWindowExposed(&owner));
        const auto colors = resolvedThemeForWidget(owner).colors;
        const auto countInk = [](const QImage& image, const QColor& ink) {
            int count = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    count += image.pixelColor(x, y).rgb() == ink.rgb();
            return count;
        };
        const auto rowImage = list->viewport()->grab(list->visualItemRect(row)).toImage();
        const auto checkImage = check->grab().toImage();
        for (const auto& image : {rowImage, checkImage}) {
            QVERIFY(countInk(image, colors.textInactive) > 0);
            QCOMPARE(countInk(image, colors.textDefault), 0);
        }
    }
    void tabCloseMarkIsThePhosphorCloseIcon() {
        using namespace choscordb::design;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.installOn(qApp);
        QTabBar tabs;
        tabs.addTab("orders");
        tabs.show();
        QCoreApplication::processEvents();
        const int size = dimension(Dimension::IconSmall) + 2 * spacing(Spacing::One);
        QStyleOption option;
        option.initFrom(&tabs);
        option.rect = QRect(0, 0, size, size);
        option.state = QStyle::State_Enabled;
        QImage painted(size, size, QImage::Format_ARGB32_Premultiplied);
        painted.fill(Qt::white);
        QPainter painter(&painted);
        tabs.style()->drawPrimitive(QStyle::PE_IndicatorTabClose, &option, &painter, &tabs);
        painter.end();
        const int icon = dimension(Dimension::IconSmall);
        QImage expected(size, size, QImage::Format_ARGB32_Premultiplied);
        expected.fill(Qt::white);
        QPainter iconPainter(&expected);
        themedIcon(Icon::Close, resolvedThemeForWidget(tabs).colors.textSubtle, icon)
            .paint(&iconPainter, QRect((size - icon) / 2, (size - icon) / 2, icon, icon));
        iconPainter.end();
        QCOMPARE(painted, expected);
    }
    void disabledControlsFollowTheDocumentedDisabledRule() {
        using namespace choscordb::design;
        QWidget owner;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.installOn(qApp);
        auto* layout = new QVBoxLayout(&owner);
        auto* stock = new QPushButton("Run", &owner);
        auto* tool = new QToolButton(&owner);
        tool->setText("Run");
        tool->setToolButtonStyle(Qt::ToolButtonTextOnly);
        auto* painted = new Button("Run", &owner);
        painted->setVariant(ButtonVariant::Ghost);
        for (QWidget* widget : QList<QWidget*>{stock, tool, painted}) {
            widget->setEnabled(false);
            layout->addWidget(widget);
        }
        owner.show();
        QCoreApplication::processEvents();
        const auto colors = resolvedThemeForWidget(owner).colors;
        const auto darkest = [](const QImage& image) {
            QColor result = Qt::white;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (image.pixelColor(x, y).alpha() == 255 &&
                        image.pixelColor(x, y).lightness() < result.lightness())
                        result = image.pixelColor(x, y);
            return result;
        };
        const auto near = [](const QColor& actual, const QColor& expected) {
            return qAbs(actual.red() - expected.red()) <= 3 &&
                   qAbs(actual.green() - expected.green()) <= 3 &&
                   qAbs(actual.blue() - expected.blue()) <= 3;
        };
        const auto background = owner.palette().color(QPalette::Window);
        QVERIFY2(near(darkest(owner.grab(stock->geometry()).toImage()), colors.textInactive),
                 qPrintable(darkest(owner.grab(stock->geometry()).toImage()).name()));
        QVERIFY2(near(darkest(owner.grab(tool->geometry()).toImage()), colors.textInactive),
                 qPrintable(darkest(owner.grab(tool->geometry()).toImage()).name()));
        const auto faded = composite(withOpacity(colors.textDefault, 0.5), background);
        QVERIFY2(near(darkest(owner.grab(painted->geometry()).toImage()), faded),
                 qPrintable(darkest(owner.grab(painted->geometry()).toImage()).name()));

        QMenu menu;
        menu.addAction("Copy")->setEnabled(false);
        menu.show();
        QCoreApplication::processEvents();
        QVERIFY2(near(darkest(menu.grab().toImage()), colors.textInactive),
                 qPrintable(darkest(menu.grab().toImage()).name()));
    }
    void bannerButtonsShowAVisibleHoverFillUnderThePointer() {
        using namespace choscordb::design;
        QWidget owner;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(owner);
        auto* layout = new QVBoxLayout(&owner);
        auto* line = new StatusLine(&owner);
        layout->addWidget(line);
        auto* previous = new Button({}, line);
        auto* next = new Button({}, line);
        line->configure({.previous = previous, .next = next});
        line->setMessage("Synthetic status");
        owner.resize(480, 80);
        owner.show();
        QCoreApplication::processEvents();
        const auto sample = [](QWidget& host, QWidget* button) {
            const auto image =
                host.grab(QRect(button->mapTo(&host, QPoint()), button->size())).toImage();
            return image.pixelColor(image.width() / 6, image.height() / 2).name();
        };
        const auto colors = resolvedThemeForWidget(owner).colors;
        QCOMPARE(sample(owner, previous), secondaryBanner(colors).fill.name());
        QTest::mouseMove(previous, previous->rect().center());
        QTRY_COMPARE(sample(owner, previous), colors.fill.name());
        QVERIFY(contrastRatio(colors.fill, secondaryBanner(colors).fill) > 1.05);

        choscordb::ToastRegion toast;
        toast.attachTo(&owner);
        toast.showToast("Synthetic notice", {}, choscordb::ToastVariant::Success);
        QTRY_VERIFY(toast.isVisible());
        QCOMPARE(toast.property("designSurface").toString(), QString("banner"));
        QVERIFY(toast.findChild<QPushButton*>("toastDismiss"));
    }
    void everyButtonFamilyShowsFeedbackUnderThePointer() {
        using namespace choscordb::design;
        QWidget owner;
        ThemeManager theme;
        theme.setMode(ThemeMode::Light);
        theme.applyTo(owner);
        auto* layout = new QVBoxLayout(&owner);
        QList<QAbstractButton*> buttons;
        for (const auto variant :
             {ButtonVariant::Default, ButtonVariant::Secondary, ButtonVariant::Outline,
              ButtonVariant::Ghost, ButtonVariant::Destructive}) {
            auto* button = new Button("Run", &owner);
            button->setVariant(variant);
            buttons.append(button);
        }
        auto* tool = new QToolButton(&owner);
        tool->setText("Run");
        tool->setToolButtonStyle(Qt::ToolButtonTextOnly);
        buttons.append(tool);
        for (auto* button : buttons)
            layout->addWidget(button);
        owner.show();
        QCoreApplication::processEvents();
        for (auto* button : buttons) {
            const auto area = QRect(button->mapTo(&owner, QPoint()), button->size());
            const auto name = QStringLiteral("%1 %2")
                                  .arg(button->metaObject()->className())
                                  .arg(buttons.indexOf(button));
            const auto rest = owner.grab(area).toImage();
            QTest::mouseMove(button, button->rect().center());
            QTest::mousePress(button, Qt::LeftButton, {}, button->rect().center());
            QCoreApplication::processEvents();
            const auto pressed = owner.grab(area).toImage();
            QTest::mouseRelease(button, Qt::LeftButton, {}, button->rect().center());
            QVERIFY2(pressed != rest, qPrintable(name));
            QTRY_VERIFY2(owner.grab(area).toImage() == pressed, qPrintable(name));
            QTest::mouseMove(&owner, QPoint(owner.width() - 1, owner.height() - 1));
            QTRY_VERIFY2(owner.grab(area).toImage() == rest, qPrintable(name));
        }
    }
};
QTEST_MAIN(KumoComponentsTest)
#include "kumo_components_test.moc"
