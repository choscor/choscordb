#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/control_style.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/field/field.h"
#include "design_system/icons.h"
#include "design_system/menu/embedded_popup.h"
#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/right_sheet/right_sheet.h"
#include "design_system/tabs/tab_add_corner.h"
#include "design_system/text/text.h"
#include "design_system/toast_region/toast_region.h"
#include "design_system/tree/navigation_tree_view.h"
#include "models/navigator_model.h"
#include "models/result_table_model.h"
#include "preview_test.h"
#include "preview_test_helpers.h"
#include "tools/preview/preview_window.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFocusEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QSvgRenderer>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QWheelEvent>
#include <QtTest>
#include <cstring>

void PreviewTest::codePreviewTextAreaUsesSharedVariantInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("textareas"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* code = host->findChild<QPlainTextEdit*>("previewCodePreview");
        QVERIFY(code);
        QVERIFY(code->isVisible());
        QVERIFY(code->isReadOnly());
        QCOMPARE(code->property("designRole").toString(), QString("codePreview"));
        QCOMPARE(code->frameShape(), QFrame::NoFrame);
        QVERIFY(code->toPlainText().contains("CREATE TABLE example"));
        QCOMPARE(code->viewport()->geometry().top(), 0);
        QCOMPARE(code->viewport()->geometry().left(), 0);
    }
}
void PreviewTest::workspaceToolbarSpecimenUsesMutedSurfaceInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("toolbar"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* toolbar = host->findChild<QToolBar*>("previewWorkspaceToolbar");
        QVERIFY(toolbar && toolbar->isVisible());
        QCOMPARE(toolbar->property("designToolbarVariant").toString(), QString("workspace"));
        auto* surface = toolbar->parentWidget();
        QCOMPARE(surface->property("designToolbarSurface").toString(), QString("workspace"));
        const auto sample =
            toolbar->mapTo(surface, QPoint(toolbar->width() - 20, toolbar->height() / 2));
        QCOMPARE(surface->grab().toImage().pixelColor(sample),
                 choscordb::design::resolvedThemeForWidget(*toolbar).colors.muted);
    }
}

void PreviewTest::displayedIconsRasterizeAtTargetScale_data() {
    QTest::addColumn<int>("size");
    QTest::addColumn<qreal>("scale");
    for (const int size : {12, 14, 16, 20, 24}) {
        for (const qreal scale : {qreal(1), qreal(1.5), qreal(2)})
            QTest::newRow(qPrintable(QString("%1px-%2x").arg(size).arg(scale))) << size << scale;
    }
}

void PreviewTest::displayedIconsRasterizeAtTargetScale() {
    QFETCH(int, size);
    QFETCH(qreal, scale);
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("icons"));
    auto* host = window.findChild<QWidget*>("previewLight");
    auto* display = host->findChild<QWidget*>(QString("icon-database-%1").arg(size));
    QVERIFY(display);
    display->setFixedSize(size, size);
    display->ensurePolished();
    const auto physical = QSizeF(size * scale, size * scale).toSize();
    QImage actual(physical, QImage::Format_ARGB32_Premultiplied);
    actual.setDevicePixelRatio(scale);
    actual.fill(Qt::transparent);
    QPainter painter(&actual);
    display->render(&painter, QPoint{}, QRegion{}, QWidget::DrawChildren);
    painter.end();
    QCOMPARE(actual.pixelColor(0, 0).alpha(), 0);

    QImage expected(physical, QImage::Format_ARGB32_Premultiplied);
    expected.setDevicePixelRatio(scale);
    expected.fill(Qt::transparent);
    QPainter vectorPainter(&expected);
    // Real gallery display versus the independent literal MVP database path.
    QSvgRenderer reference(
        QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" "
                          "fill=\"none\" stroke=\"#222b32\" stroke-width=\"1.6\" "
                          "stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\""
                          "M4 6c0-4 16-4 16 0s-16 4-16 0m0 0v12c0 4 16 4 16 0V6"
                          "M4 12c0 4 16 4 16 0\"/></svg>"));
    reference.render(&vectorPainter, QRectF(0, 0, size, size));
    vectorPainter.end();
    QCOMPARE(actual, expected);
}

void PreviewTest::iconsShowNamedProductionAssetsAtSupportedSizes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("icons"));
    for (const auto* theme : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(theme);
        QVERIFY(host);
        for (const auto* name : {"database", "cancel", "square", "postgresql", "sqlite"}) {
            for (int size : {12, 14, 16, 20, 24}) {
                const auto icons =
                    host->findChildren<QLabel*>(QString("icon-%1-%2").arg(name).arg(size));
                QCOMPARE(icons.size(), 1);
                auto* icon = icons.front();
                QCOMPARE(icon->sizeHint(), QSize(size, size));
                QCOMPARE(icon->accessibleName(), QString("%1 · %2 pixels").arg(name).arg(size));
            }
        }
    }
}

void PreviewTest::examplesOpenActualDismissibleSurfaces() {
    choscordb::design::PreviewWindow window;
    window.show();
    QVERIFY(window.selectSpecimen("dialogs"));
    auto* light = window.findChild<QWidget*>("previewLight");
    auto* open = light->findChild<QPushButton*>("previewOpenDialog");
    QVERIFY(open);
    open->click();
    auto* dialog = previewSurface<QDialog>(light);
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    QCOMPARE(choscordb::design::DialogPresentation::activeDialog(), dialog);
    QTest::keyClick(dialog, Qt::Key_Escape);
    QVERIFY(!dialog->isVisible());
    QCOMPARE(dialog->result(), int(QDialog::Rejected));
    QVERIFY(window.selectSpecimen("menus"));
    auto* menuButton = light->findChild<QPushButton*>("previewOpenMenu");
    QVERIFY(menuButton);
    menuButton->click();
    auto* menu = previewSurface<QMenu>(light, "previewOpenMenu");
    QVERIFY(menu);
    QVERIFY(menu->isVisible());
    QTest::keyClick(menu, Qt::Key_Escape);
    QVERIFY(!menu->isVisible());
}

void PreviewTest::fieldSpecimenRetainsEditingAndValidationStates() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("fields"));
    auto* light = window.findChild<QWidget*>("previewLight");
    auto* editable = light->findChild<QLineEdit*>("field-editable");
    QVERIFY(editable);
    QCOMPARE(editable->font().weight(), int(QFont::Normal));
    QTest::keyClicks(editable, "analyst");
    QCOMPARE(editable->text(), QString("analyst"));
    QCOMPARE(editable->font().weight(), int(QFont::Medium));
    auto* password = light->findChild<QLineEdit*>("field-password");
    QVERIFY(password);
    QCOMPARE(password->echoMode(), QLineEdit::Password);
    auto* toggle = password->findChild<QAction*>("field-password-toggle");
    QVERIFY(toggle);
    auto* iconButton = password->findChild<QToolButton*>();
    QVERIFY(iconButton);
    window.show();
    QCoreApplication::processEvents();
    QVERIFY(qAbs(iconButton->geometry().center().y() - password->rect().center().y()) <= 1);
    const auto hiddenIcon = toggle->icon().pixmap(16, 16).toImage();
    QVERIFY(!hiddenIcon.isNull());
    toggle->trigger();
    QCOMPARE(password->echoMode(), QLineEdit::Normal);
    const auto shownIcon = toggle->icon().pixmap(16, 16).toImage();
    QVERIFY(!shownIcon.isNull());
    QVERIFY(hiddenIcon != shownIcon);
    toggle->trigger();
    QCOMPARE(password->echoMode(), QLineEdit::Password);
    QCOMPARE(toggle->icon().pixmap(16, 16).toImage(), hiddenIcon);
    auto* readOnly = light->findChild<QLineEdit*>("field-readonly");
    QVERIFY(readOnly);
    QVERIFY(readOnly->isReadOnly());
    for (const auto* theme : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(theme);
        QVERIFY(host);
        auto* invalid = host->findChild<QLineEdit*>("field-invalid");
        QVERIFY(invalid);
        QVERIFY(invalid->property("invalid").toBool());
        auto* validation = dynamic_cast<choscordb::design::FieldValidation*>(
            host->findChild<QWidget*>("field-validation"));
        QVERIFY(validation);
        QCOMPARE(validation->control(), invalid);
        QCOMPARE(validation->error(), QString("A value is required."));
        QCOMPARE(invalid->accessibleDescription(), QString("A value is required."));
        auto* error = validation->findChild<QLabel*>();
        QVERIFY(error);
        QCOMPARE(error->text(), QString("A value is required."));
        QCOMPARE(error->property("designRole").toString(), QString("fieldError"));
        QVERIFY(error->isVisible());
    }
}

void PreviewTest::sidebarTabSpecimenUsesProductionContextInBothThemes() {
    using namespace choscordb::design;
    PreviewWindow window;
    QVERIFY(window.selectSpecimen("buttons"));
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* tab = host->findChild<Button*>("previewSidebarTab");
        QVERIFY(tab);
        auto* navigator = host->findChild<QWidget*>("navigatorBody");
        QVERIFY(navigator);
        QVERIFY(navigator->findChild<QListWidget*>("savedConnections"));
        QCOMPARE(tab->buttonContext(), ButtonContext::SidebarTab);
        QVERIFY(tab->isChecked());
    }
}

void PreviewTest::sidebarTabFocusRingSurvivesOnlyKeyboardActivation() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("buttons"));
    window.show();
    QApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* tab = host->findChild<choscordb::design::Button*>("previewSidebarTab");
        QVERIFY(tab);
        tab->setFocus(Qt::MouseFocusReason);
        auto focus = [tab](Qt::FocusReason reason) {
            QFocusEvent event(QEvent::FocusIn, reason);
            QApplication::sendEvent(tab, &event);
        };
        focus(Qt::MouseFocusReason);
        const auto mouse = visibleSurfaceSnapshot(*tab);
        focus(Qt::ActiveWindowFocusReason);
        QCOMPARE(visibleSurfaceSnapshot(*tab), mouse);
        focus(Qt::TabFocusReason);
        const auto keyboard = visibleSurfaceSnapshot(*tab);
        QVERIFY(keyboard != mouse);
        focus(Qt::ActiveWindowFocusReason);
        QCOMPARE(visibleSurfaceSnapshot(*tab), keyboard);
        focus(Qt::MouseFocusReason);
        QCOMPARE(visibleSurfaceSnapshot(*tab), mouse);
    }
}

void PreviewTest::buttonsUseProductionVariantsAndStates() {
    using namespace choscordb::design;
    PreviewWindow window;
    QVERIFY(window.selectSpecimen("buttons"));
    auto* light = window.findChild<QWidget*>("previewLight");
    auto* destructive = light->findChild<Button*>("button-normal-destructive");
    QVERIFY(destructive);
    QCOMPARE(destructive->variant(), ButtonVariant::Destructive);
    auto* pressed = light->findChild<Button*>("button-pressed-default");
    QVERIFY(pressed);
    QVERIFY(pressed->isDown());
    auto* loading = light->findChild<Button*>("button-loading-default");
    QVERIFY(loading);
    QVERIFY(loading->isLoading());
    auto* disabled = light->findChild<Button*>("button-disabled-default");
    QVERIFY(disabled);
    QVERIFY(!disabled->isEnabled());
    auto* smallest = light->findChild<Button*>("button-size-xs");
    QVERIFY(smallest);
    QCOMPARE(smallest->buttonSize(), ButtonSize::ExtraSmall);
    QCOMPARE(smallest->font().pixelSize(),
             typographySpec(TypographyRole::SectionCaption).pixelSize);
    QCOMPARE(pressed->font().pixelSize(), typographySpec(TypographyRole::Ui).pixelSize);
    auto* largest = light->findChild<Button*>("button-size-icon-lg");
    QVERIFY(largest);
    QCOMPARE(largest->buttonSize(), ButtonSize::IconLarge);
}

void PreviewTest::individualSpecimensAreSelectableAndSearchable() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.specimenIds().contains("buttons"));
    QVERIFY(window.specimenIds().contains("dialogs"));
    QVERIFY(!window.specimenIds().contains("paging"));
    QVERIFY(!window.specimenIds().contains("paging-unknown"));
    QVERIFY(window.selectSpecimen("dialogs"));
    QVERIFY(!window.selectSpecimen("missing"));
    auto* search = window.findChild<QLineEdit*>("previewSearch");
    search->setText("password");
    QCOMPARE(window.visibleSections(), QStringList({"Components"}));
}

void PreviewTest::fieldValidationIsBelowInputAndSelectInBothThemes() {
    choscordb::design::PreviewWindow window;
    for (const auto& specimen : {"fields", "selects"}) {
        QVERIFY(window.selectSpecimen(specimen));
        for (const auto& theme : {"previewLight", "previewDark"}) {
            auto* host = window.findChild<QWidget*>(theme);
            QVERIFY(host);
            auto* validated =
                dynamic_cast<choscordb::design::FieldValidation*>(host->findChild<QWidget*>(
                    specimen == QStringLiteral("fields") ? "field-validation"
                                                         : "select-validation"));
            QVERIFY(validated);
            QVERIFY(validated->control());
            QVERIFY(validated->control()->property("invalid").toBool());
            auto* message = validated->findChild<QLabel*>();
            QVERIFY(message);
            QVERIFY(!message->isHidden());
            validated->resize(300, validated->sizeHint().height());
            validated->layout()->activate();
            QVERIFY(message->geometry().top() >= validated->control()->geometry().bottom());
            validated->setError({});
            QVERIFY(!validated->control()->property("invalid").toBool());
            QVERIFY(message->isHidden());
        }
    }
}

void PreviewTest::tokensExposeCopyableValuesAndSources() {
    choscordb::design::PreviewWindow window;
    auto* light = window.findChild<QWidget*>("previewLight");
    auto* table = light->findChild<QTableWidget*>("previewTokens");
    QVERIFY(table);
    const auto matches = table->findItems("color.background", Qt::MatchExactly);
    QCOMPARE(matches.size(), 1);
    table->setCurrentCell(matches.front()->row(), 0);
    auto* copy = light->findChild<QPushButton*>("previewCopyToken");
    QVERIFY(copy);
    copy->click();
    QVERIFY(QApplication::clipboard()->text().contains("background"));
    QVERIFY(QApplication::clipboard()->text().contains("#f6f7f8", Qt::CaseInsensitive));
    QVERIFY(QApplication::clipboard()->text().contains("desktop/design_system/tokens/tokens.cpp"));
}

void PreviewTest::exportsAreDeterministicAndFailuresVisible() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSection("Typography"));
    const auto first = directory.filePath("first.png");
    QVERIFY(window.exportCapture(first));
    QImage firstImage(first);
    QCOMPARE(firstImage.size(), QSize(1280, 900));
    const auto second = directory.filePath("second.png");
    QVERIFY(window.exportCapture(second));
    QCOMPARE(QImage(second), firstImage);
    const auto single = directory.filePath("single.png");
    QVERIFY(window.exportCapture(single, false));
    QCOMPARE(QImage(single).size(), QSize(640, 900));
    QVERIFY(window.selectSection("Icons"));
    const auto icons = directory.filePath("icons.png");
    QVERIFY(window.exportCapture(icons));
    QVERIFY(QImage(icons) != firstImage);
    QVERIFY(!window.exportCapture(directory.filePath("missing/capture.png")));
    auto* status = window.findChild<QLabel*>("previewExportStatus");
    QVERIFY(status);
    QVERIFY(status->text().contains("failed", Qt::CaseInsensitive));
    QVERIFY(!window.selectSection("Unknown"));
}

void PreviewTest::comparisonThemesAreIndependent() {
    const auto applicationPalette = qApp->palette();
    const auto applicationStyle = qApp->styleSheet();
    choscordb::design::PreviewWindow window;
    auto* light = window.findChild<QWidget*>("previewLight");
    auto* dark = window.findChild<QWidget*>("previewDark");
    QVERIFY(light);
    QVERIFY(dark);
    QVERIFY(light->palette().color(QPalette::Window) != dark->palette().color(QPalette::Window));
    const auto darkPalette = dark->palette();
    auto changed = light->palette();
    changed.setColor(QPalette::Window, Qt::red);
    light->setPalette(changed);
    QCOMPARE(dark->palette(), darkPalette);
    QCOMPARE(qApp->palette(), applicationPalette);
    QCOMPARE(qApp->styleSheet(), applicationStyle);
}

void PreviewTest::navigationIsSearchable() {
    choscordb::design::PreviewWindow window;
    QCOMPARE(window.visibleSections(),
             QStringList({"Tokens", "Typography", "Icons", "Components"}));
    QVERIFY(window.specimenIds().contains("dialogs"));
    QVERIFY(window.specimenIds().contains("menus"));
    QVERIFY(window.specimenIds().contains("feedback"));
    QVERIFY(!window.specimenIds().contains("paging"));
    QVERIFY(!window.specimenIds().contains("results"));
    QVERIFY(!window.specimenIds().contains("connection-form"));
    QVERIFY(!window.selectSection("Compositions"));
    QVERIFY(!window.selectSection("Database UI"));
    auto* search = window.findChild<QLineEdit*>("previewSearch");
    QVERIFY(search);
    search->setText("dialog");
    QCOMPARE(window.visibleSections(), QStringList({"Components"}));
    search->setText("dataBASE");
    QVERIFY(window.visibleSections().isEmpty());
    search->clear();
    QCOMPARE(window.visibleSections().size(), 4);
}

void PreviewTest::componentFamiliesAreRendered() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("dock"));
    auto* light = window.findChild<QWidget*>("previewLight");
    QVERIFY(light->findChild<QDockWidget*>());

    QVERIFY(window.selectSpecimen("lists-navigation"));
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* tree = window.findChild<QWidget*>(name)->findChild<QTreeView*>();
        QVERIFY(tree);
        QVERIFY(dynamic_cast<choscordb::design::NavigationTreeView*>(tree));
        QCOMPARE(tree->property("designSurface").toString(), QString("sidebar"));
        QVERIFY(tree->currentIndex().isValid());
        QCOMPARE(tree->indentation(), 9);
        QVERIFY(tree->model()->index(0, 0).data(Qt::DecorationRole).isNull());
        QVERIFY(!tree->model()
                     ->index(0, 0, tree->model()->index(0, 0))
                     .data(Qt::DecorationRole)
                     .isNull());
    }
    QVERIFY(window.selectSpecimen("numeric-fields"));
    QVERIFY(light->findChild<QDoubleSpinBox*>());
    QVERIFY(window.selectSpecimen("textareas"));
    QVERIFY(light->findChild<QTextEdit*>());
    QVERIFY(window.selectSpecimen("checks-toggles"));
    QVERIFY(light->findChild<QRadioButton*>());
    QVERIFY(window.selectSpecimen("separators-splitters"));
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host->findChild<QDockWidget*>("previewResizableSidebar"));
    }
    bool hasVerticalSeparator = false;
    for (auto* frame : light->findChildren<QFrame*>())
        hasVerticalSeparator |= frame->frameShape() == QFrame::VLine;
    QVERIFY(hasVerticalSeparator);
    QVERIFY(window.selectSpecimen("tabs"));
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        auto* tabs = host->findChild<QTabBar*>("previewObjectTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 5);
        QCOMPARE(tabs->currentIndex(), 3);
        QCOMPARE(tabs->tabText(3), QString("DDL"));
    }
}

void PreviewTest::dialogSectionsPreviewUsesRealComponentInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("dialog-sections"));
    window.show();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* open = host->findChild<QPushButton*>("previewOpenDialogSections");
        auto* dialog = previewSurface<QDialog>(host, "previewOpenDialogSections");
        auto* sections =
            dialog->findChild<choscordb::design::DialogSections*>("previewDialogSections");
        QVERIFY(open && dialog && sections);
        auto* dismiss =
            sections->findChild<choscordb::design::Button*>("previewDialogSectionsDismiss");
        QVERIFY(dismiss);
        QCOMPARE(dismiss->text(), QString());
        QCOMPARE(dismiss->buttonSize(), choscordb::design::ButtonSize::IconSmall);
        QVERIFY(!dismiss->icon().isNull());
        open->click();
        QTRY_VERIFY(dialog->isVisible());
        QCOMPARE(sections->geometry(), dialog->rect());
        QCOMPARE(sections->bodyLayout()->contentsMargins().left(),
                 choscordb::design::spacing(choscordb::design::Spacing::Three));
        QCOMPARE(sections->bodyLayout()->contentsMargins().right(),
                 choscordb::design::spacing(choscordb::design::Spacing::Three));
        QVERIFY(sections->headerLayout()->parentWidget()->height() < 66);
        QVERIFY(sections->footerLayout()->parentWidget()->height() < 70);
        QVERIFY(sections->bodyLayout()->parentWidget()->height() > 0);
        auto* hostField = dialog->findChild<QLineEdit*>("previewProfileHost");
        auto* port = dialog->findChild<QSpinBox*>("previewProfilePort");
        auto* database = dialog->findChild<QLineEdit*>("previewProfileDatabase");
        auto* username = dialog->findChild<QLineEdit*>("previewProfileUsername");
        auto* password = dialog->findChild<QLineEdit*>("previewProfilePassword");
        QVERIFY(hostField && port && database && username && password);
        const auto rowCenter = [](QWidget* control) { return control->geometry().center().y(); };
        QVERIFY(qAbs(rowCenter(hostField) - rowCenter(port)) <= 4);
        QVERIFY(qAbs(rowCenter(hostField) - rowCenter(database)) <= 4);
        QVERIFY(qAbs(rowCenter(username) - rowCenter(password)) <= 4);
        QVERIFY(port->width() < hostField->width());
        QCOMPARE(password->echoMode(), QLineEdit::Password);
        auto* saveCredentials = dialog->findChild<QCheckBox*>("previewProfileSaveCredentials");
        QVERIFY(saveCredentials);
        QVERIFY(!dialog->findChild<QCheckBox*>("previewProfileUseTls"));
        dialog->reject();
    }
}

void PreviewTest::navigationProfileRowsShowRegularAndSelectedStates() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("navigation-profile-row"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* list = host->findChild<QListWidget*>("previewNavigationProfiles");
        QVERIFY(list);
        QCOMPARE(list->count(), 3);
        QCOMPARE(list->item(2)->text(), QString("test mysql"));
        QCOMPARE(list->item(2)->toolTip(), QString("MySQL · Dolphin icon"));
        const auto mysqlRow = list->visualItemRect(list->item(2));
        const auto branded = list->viewport()->grab(mysqlRow).toImage();
        list->item(2)->setData(choscordb::design::NavigationProfileDelegate::DriverRole, "unknown");
        const auto generic = list->viewport()->grab(mysqlRow).toImage();
        QVERIFY(branded != generic);
        list->item(2)->setData(choscordb::design::NavigationProfileDelegate::DriverRole, "mysql");
        QCOMPARE(list->item(2)
                     ->data(choscordb::design::NavigationProfileDelegate::DriverRole)
                     .toString(),
                 QString("mysql"));
        QCOMPARE(list->spacing(), choscordb::design::spacing(choscordb::design::Spacing::Half));
        QCOMPARE(list->currentRow(), 1);
        QCOMPARE(list->selectionMode(), QAbstractItemView::MultiSelection);
        QVERIFY(list->item(0)->isSelected());
        QVERIFY(list->item(1)->isSelected());
        QVERIFY(!list->item(2)->isSelected());
        const auto colors = choscordb::design::resolvedThemeForWidget(*list).colors;
        const auto firstSelectedRow = list->visualItemRect(list->item(0));
        const auto selectedRow = list->visualItemRect(list->item(1));
        const auto regularRow = list->visualItemRect(list->item(2));
        const auto image = list->viewport()->grab().toImage();
        QCOMPARE(image.pixelColor(firstSelectedRow.right() - 30, firstSelectedRow.center().y()),
                 colors.sidebarAccent);
        QCOMPARE(image.pixelColor(selectedRow.right() - 30, selectedRow.center().y()),
                 colors.sidebarAccent);
        QCOMPARE(image.pixelColor(regularRow.right() - 30, regularRow.center().y()),
                 colors.sidebar);
        QVERIFY(list->visualItemRect(list->item(1)).bottom() < list->viewport()->height());
        QCOMPARE(list->item(0)->text(), QString("test sqlite"));
        QCOMPARE(list->item(1)
                     ->data(choscordb::design::NavigationProfileDelegate::DriverRole)
                     .toString(),
                 QString("postgres"));
        QVERIFY(dynamic_cast<choscordb::design::NavigationProfileDelegate*>(list->itemDelegate()));
        QCOMPARE(choscordb::design::resolveTypography(
                     choscordb::design::TypographyRole::NavigationDetail)
                     .pixelSize(),
                 10);
        const auto height = list->visualItemRect(list->item(0)).height();
        QVERIFY(height >= 32 && height <= 36);
    }
}

void PreviewTest::dockSpecimenRendersThemedTitleAndButtons() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("dock"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        auto* dock = host->findChild<QDockWidget*>();
        QVERIFY(dock);
        auto* title = dock->findChild<QLabel*>("dockTitleLabel");
        QVERIFY(title);
        const auto theme = choscordb::design::resolvedThemeForWidget(*host);
        const auto image = dock->grab().toImage();
        QCOMPARE(image.pixelColor(image.width() / 2, image.height() / 2), theme.colors.surface);
        for (const auto* buttonName : {"dockFloatButton", "dockCloseButton"}) {
            auto* button = dock->findChild<QToolButton*>(buttonName);
            QVERIFY(button);
            QVERIFY(!button->icon().isNull());
            const auto buttonImage = button->grab().toImage();
            int paintedColumns = 0;
            for (int x = 0; x < buttonImage.width(); ++x) {
                bool painted = false;
                for (int y = 0; y < buttonImage.height(); ++y)
                    painted |= buttonImage.pixelColor(x, y) != theme.colors.surface;
                paintedColumns += painted;
            }
            QVERIFY2(paintedColumns >= 7, qPrintable(buttonName));
        }
    }
}

void PreviewTest::navigationTreeTogglesAndRenamesFromMenu() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("lists-navigation"));
    auto* tree = window.findChild<QWidget*>("previewLight")->findChild<QTreeView*>();
    QVERIFY(tree);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    for (const auto* theme : {"previewLight", "previewDark"}) {
        auto* specimen = window.findChild<QWidget*>(theme)->findChild<QTreeView*>();
        QVERIFY(specimen);
        const auto root = specimen->model()->index(0, 0);
        const auto child = specimen->model()->index(0, 0, root);
        QVERIFY(specimen->visualRect(root).height() >= 28);
        QVERIFY(specimen->visualRect(root).height() <= 32);
        QVERIFY(specimen->visualRect(child).left() - specimen->visualRect(root).left() <= 20);
    }
    const auto group = tree->model()->index(0, 0);
    const auto nested = tree->model()->index(0, 0, group);
    QVERIFY(tree->visualRect(group).height() <= 32);
    QVERIFY(tree->visualRect(nested).left() - tree->visualRect(group).left() <= 20);
    const QPoint label = tree->visualRect(group).center() + QPoint(20, 0);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, label);
    QVERIFY(!tree->isExpanded(group));
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, label);
    QVERIFY(tree->isExpanded(group));
    QTest::mouseDClick(tree->viewport(), Qt::LeftButton, {}, label);
    QVERIFY(!tree->findChild<QLineEdit*>());
    QSignalSpy menuRequested(tree, &QWidget::customContextMenuRequested);
    QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, label,
                                   tree->viewport()->mapToGlobal(label));
    QApplication::sendEvent(tree->viewport(), &contextEvent);
    QCOMPARE(menuRequested.size(), 1);
    auto* menu = qobject_cast<QMenu*>(choscordb::design::detail::activeEmbeddedPopup());
    QVERIFY(menu);
    auto* rename = menu->actions().isEmpty() ? nullptr : menu->actions().first();
    QVERIFY(rename);
    QCOMPARE(rename->text(), QStringLiteral("Rename"));
    const QPoint visibleMenu = menu->mapToGlobal(menu->actionGeometry(rename).topLeft());
    QVERIFY(qAbs(visibleMenu.y() - contextEvent.globalY()) <= 10);
    rename->trigger();
    QTRY_VERIFY(tree->findChild<QLineEdit*>());
    auto* editor = tree->findChild<QLineEdit*>();
    QVERIFY(editor);
    QVERIFY(tree->isExpanded(group));
    editor->selectAll();
    QTest::keyClicks(editor, "Renamed group");
    QCOMPARE(editor->text(), QStringLiteral("Renamed group"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTRY_COMPARE(group.data().toString(), QStringLiteral("Renamed group"));
}

void PreviewTest::popupSpecimensStayInsideWindowInBothThemes() {
    choscordb::design::PreviewWindow window;
    window.resize(960, 640);
    window.show();
    for (const auto* specimen : {"menus", "selects"}) {
        QVERIFY(window.selectSpecimen(specimen));
        for (const auto* theme : {"previewLight", "previewDark"}) {
            auto* host = window.findChild<QWidget*>(theme);
            QVERIFY(host);
            QWidget* popup = nullptr;
            QComboBox* select = nullptr;
            if (QString(specimen) == "menus") {
                auto* menu = previewSurface<QMenu>(host, "previewOpenMenu");
                QVERIFY(menu);
                host->findChild<QPushButton*>("previewOpenMenu")->click();
                popup = menu;
            } else {
                select = host->findChild<QComboBox*>();
                QVERIFY(select);
                select->setFocus();
                QWheelEvent wheel(select->rect().center(),
                                  select->mapToGlobal(select->rect().center()), {}, QPoint(0, -120),
                                  Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
                QApplication::sendEvent(select, &wheel);
                QCOMPARE(select->currentIndex(), 0);
                select->showPopup();
                popup = select->view()->parentWidget();
            }
            QTRY_VERIFY(popup->isVisible());
            QVERIFY(!popup->isWindow());
            QCOMPARE(popup->window(), &window);
            QVERIFY(window.rect().contains(QRect(popup->mapTo(&window, QPoint()), popup->size())));
            if (select)
                select->hidePopup();
            else
                popup->hide();
        }
    }
}

void PreviewTest::rightSheetSpecimenUsesModalBoundaryInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("right-sheet"));
    window.resize(1100, 740);
    window.show();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* open = host->findChild<QPushButton*>("previewOpenRightSheet");
        QVERIFY(open);
        open->setFocus();
        open->click();
        auto* sheet = qobject_cast<choscordb::design::RightSheet*>(
            open->property("previewSurface").value<QObject*>());
        QVERIFY(sheet);
        QTRY_VERIFY(sheet->isVisible());
        QCOMPARE(sheet->parentWidget(), &window);
        QCOMPARE(sheet->geometry().right(), window.rect().right());
        QCOMPARE(sheet->height(), window.height());
        QVERIFY(window.findChild<QWidget*>("modalBackdrop"));
        QTRY_VERIFY(sheet->isAncestorOf(QApplication::focusWidget()));
        auto* close = sheet->findChild<QPushButton*>("rightSheetClose");
        QVERIFY(close);
        QCOMPARE(close->accessibleName(), QString("Close sheet"));
        QCOMPARE(sheet->accessibleName(), QString("Example details"));
        QVERIFY(sheet->findChild<QWidget*>("rightSheetBody"));
        auto* footer = sheet->findChild<QWidget*>("rightSheetFooter");
        QVERIFY(footer);
        QCOMPARE(footer->accessibleName(), QString("Sheet actions"));
        QCOMPARE(footer->geometry().bottom(), sheet->rect().bottom());
        const auto footerColor = footer->grab(QRect(1, 1, 1, 1)).toImage().pixelColor(0, 0);
        QCOMPARE(footerColor, choscordb::design::resolvedThemeForWidget(*sheet).colors.muted);
        QSignalSpy backgroundClicks(open, &QPushButton::clicked);
        QTest::mouseClick(open, Qt::LeftButton);
        QCOMPARE(backgroundClicks.count(), 0);
        QVERIFY(sheet->isVisible());
        window.resize(1000, 650);
        QTRY_COMPARE(sheet->geometry().right(), window.rect().right());
        QTRY_COMPARE(sheet->height(), window.height());
        close->click();
        QTRY_VERIFY(!sheet->isVisible());
        QTRY_VERIFY(open->hasFocus());
        open->click();
        QTRY_VERIFY(sheet->isVisible());
        QTest::keyClick(sheet, Qt::Key_Escape);
        QTRY_VERIFY(!sheet->isVisible());
        QTRY_VERIFY(open->hasFocus());
        open->click();
        QTRY_VERIFY(sheet->isVisible());
        QWidget* backdrop = nullptr;
        for (auto* candidate : window.findChildren<QWidget*>("modalBackdrop")) {
            if (candidate->isVisible())
                backdrop = candidate;
        }
        QVERIFY(backdrop);
        QVERIFY(backdrop->isVisible());
        QCOMPARE(choscordb::design::DialogPresentation::activeDialog(&window), sheet);
        QTest::mouseClick(backdrop, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        QTRY_VERIFY(!sheet->isVisible());
        QTRY_VERIFY(open->hasFocus());
    }
    QWidget narrowOwner;
    narrowOwner.resize(300, 420);
    narrowOwner.show();
    choscordb::design::RightSheet narrowSheet(&narrowOwner);
    narrowSheet.setTitle("Details");
    narrowSheet.setBody(new QPlainTextEdit(&narrowSheet));
    auto* narrowAction = new choscordb::design::Button("Done", &narrowSheet);
    narrowSheet.footerLayout()->addWidget(narrowAction);
    narrowSheet.open();
    QTRY_COMPARE(narrowSheet.geometry(), QRect(0, 0, 300, 420));
    QVERIFY(narrowSheet.rect().contains(
        QRect(narrowAction->mapTo(&narrowSheet, QPoint()), narrowAction->size())));
    narrowSheet.reject();
}

void PreviewTest::dialogSectionsHaveMutedPaddedFooterInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("dialog-sections"));
    window.show();
    for (const auto* theme : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(theme);
        auto* open = host->findChild<QPushButton*>("previewOpenDialogSections");
        auto* dialog = previewSurface<QDialog>(host, "previewOpenDialogSections");
        QVERIFY(open && dialog);
        open->click();
        QTRY_VERIFY(dialog->isVisible());
        auto* sections = dialog->findChild<choscordb::design::DialogSections*>();
        QVERIFY(sections);
        const auto headerMargins = sections->headerLayout()->contentsMargins();
        const auto footerMargins = sections->footerLayout()->contentsMargins();
        QVERIFY(headerMargins.right() <= 8);
        QVERIFY(footerMargins.top() >= 10);
        QVERIFY(footerMargins.bottom() >= 10);
        auto* footer = sections->footerLayout()->parentWidget();
        auto* body = sections->bodyLayout()->parentWidget();
        const auto footerColor = footer->grab().toImage().pixelColor(20, footer->height() / 2);
        const auto bodyColor = body->grab().toImage().pixelColor(20, body->height() - 10);
        QVERIFY(footerColor != bodyColor);
        QVERIFY(qAbs(footerColor.red() - footerColor.green()) <= 2);
        QVERIFY(qAbs(footerColor.green() - footerColor.blue()) <= 2);
        dialog->reject();
    }
}

void PreviewTest::modalSpecimenNamesTheDialogSurface() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("dialogs"));
    auto* selector = window.findChild<QComboBox*>("previewSpecimen");
    QVERIFY(selector);
    QCOMPARE(selector->currentText(), QString("Modal dialog"));
}

void PreviewTest::documentTabSpecimenShowsFixedWidthTabsInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("tabs"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* theme : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(theme);
        QVERIFY(host);
        auto* tabs = host->findChild<QTabWidget*>();
        QVERIFY(tabs);
        auto* corner = dynamic_cast<choscordb::design::TabAddCorner*>(
            tabs->findChild<QWidget*>("tabAddCorner"));
        QVERIFY(corner);
        QVERIFY(corner->addButton()->isVisible());
        QCOMPARE(corner->addButton()->variant(), choscordb::design::ButtonVariant::Ghost);
        QCOMPARE(corner->grab().toImage().pixelColor(1, corner->height() / 2),
                 choscordb::design::resolvedThemeForWidget(*corner).colors.muted);
        QCOMPARE(tabs->tabText(0), QString("Untitled query 1"));
        QCOMPARE(tabs->tabText(1), QString("orders"));
        QVERIFY(!tabs->tabIcon(1).isNull());
        QCOMPARE(qobject_cast<QLabel*>(tabs->widget(0))->text(),
                 QString("Neutral document chrome"));
        QVERIFY(!tabs->tabIcon(0).isNull());
        QCOMPARE(tabs->tabBar()->font().pixelSize(), 11);
        QCOMPARE(tabs->tabBar()->tabRect(0).width(), tabs->tabBar()->tabRect(1).width());
        QVERIFY(tabs->tabBar()->tabRect(0).width() >= 138);
        QVERIFY(tabs->tabBar()->tabRect(0).width() <= 150);
        QCOMPARE(tabs->tabBar()->style()->pixelMetric(QStyle::PM_TabBarIconSize, nullptr,
                                                      tabs->tabBar()),
                 12);
        const auto selected = tabs->tabBar()->tabRect(0);
        const auto tabPixels = tabs->tabBar()->grab().toImage();
        const auto background = tabPixels.pixelColor(selected.left() + 38, selected.top() + 3);
        int leftTextInk = 0;
        for (int y = selected.center().y() - 6; y <= selected.center().y() + 6; ++y)
            for (int x = selected.left() + 33; x <= selected.left() + 45; ++x)
                leftTextInk +=
                    qAbs(tabPixels.pixelColor(x, y).lightness() - background.lightness()) > 50;
        QVERIFY(leftTextInk > 5);
        tabs->setTabText(0, {});
        tabs->setTabIcon(0, {});
        const auto withoutIcon = tabs->tabBar()->grab().toImage();
        tabs->setTabIcon(0, choscordb::design::themedIcon(
                                choscordb::design::Icon::Table,
                                choscordb::design::resolvedThemeForWidget(*tabs).colors.mutedText,
                                16));
        const auto withTableIcon = tabs->tabBar()->grab().toImage();
        int iconPixels = 0;
        for (int y = selected.center().y() - 8; y <= selected.center().y() + 8; ++y)
            for (int x = selected.left() + 4; x <= selected.left() + 28; ++x)
                iconPixels += withTableIcon.pixelColor(x, y) != withoutIcon.pixelColor(x, y);
        QVERIFY2(iconPixels > 10, qPrintable(QString("Table icon pixels: %1").arg(iconPixels)));
        tabs->setTabText(0, "Untitled query 1");
        QVERIFY(qAbs(corner->addButton()->geometry().center().y() - corner->rect().center().y()) <=
                1);
        auto* paneTabs = host->findChild<QTabBar*>("previewObjectTabs");
        QVERIFY(paneTabs);
        const auto paneRect = paneTabs->tabRect(paneTabs->currentIndex());
        const auto paneImage = paneTabs->grab().toImage();
        const auto colors = choscordb::design::resolvedThemeForWidget(*paneTabs).colors;
        QCOMPARE(paneImage.pixelColor(paneRect.left() + 1, paneRect.top() + 1), colors.surface);
        while (tabs->count() > 1)
            tabs->removeTab(tabs->count() - 1);
        QCoreApplication::processEvents();
        const auto addPoint = corner->addButton()->mapTo(tabs, QPoint(1, 2));
        QVERIFY(addPoint.x() - tabs->tabBar()->tabRect(0).right() <= 6);
        QCOMPARE(tabs->grab().toImage().pixelColor(addPoint),
                 choscordb::design::resolvedThemeForWidget(*tabs).colors.muted);
        const QPoint hoverLocal(5, corner->addButton()->height() / 2);
        const auto hoverPoint = corner->addButton()->mapTo(tabs, hoverLocal);
        const auto normalImage = tabs->grab().toImage();
        QTest::mouseMove(corner->addButton(), hoverLocal);
        QCoreApplication::processEvents();
        const auto hoverImage = tabs->grab().toImage();
        QCOMPARE(hoverImage.pixelColor(hoverPoint), normalImage.pixelColor(hoverPoint));
        const auto buttonTopLeft = corner->addButton()->mapTo(tabs, QPoint());
        const QRect iconArea(buttonTopLeft.x() + 7, buttonTopLeft.y() + 7, 16, 16);
        int changedIconPixels = 0;
        for (int y = iconArea.top(); y <= iconArea.bottom(); ++y)
            for (int x = iconArea.left(); x <= iconArea.right(); ++x)
                changedIconPixels += hoverImage.pixelColor(x, y) != normalImage.pixelColor(x, y);
        QVERIFY(changedIconPixels > 0);
    }
}

void PreviewTest::documentTabsShowKeyboardFocusInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("tabs"));
    window.show();
    for (const auto* theme : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(theme);
        auto* tabs = host->findChild<QTabWidget*>();
        QVERIFY(tabs);
        auto* selector = window.findChild<QComboBox*>("previewSpecimen");
        QVERIFY(selector);
        selector->setFocus();
        QCoreApplication::processEvents();
        QVERIFY(!tabs->tabBar()->hasFocus());
        const auto before = tabs->tabBar()->grab().toImage();
        tabs->tabBar()->setFocus(Qt::TabFocusReason);
        QCoreApplication::processEvents();
        QVERIFY(tabs->tabBar()->hasFocus());
        QVERIFY(tabs->tabBar()->grab().toImage() != before);
    }
}
