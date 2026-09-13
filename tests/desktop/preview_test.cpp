#include "design_system/components.h"
#include "design_system/control_style.h"
#include "design_system/preview_window.h"

#include "models/result_table_model.h"
#include "widgets/confirmation_dialog.h"
#include "widgets/sql_editor.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCompleter>
#include <QDialog>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>

namespace {
QImage visibleSurfaceSnapshot(QWidget& widget) {
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget.render(&image);
    return image.convertToFormat(QImage::Format_ARGB32);
}

// Pick text ink from the independently opened public surface. Transparent
// corners and the solid surface background cannot satisfy this witness.
QImage textInkPatch(const QImage& source, bool lightInk = false) {
    QRect bounds;
    int inkPixels = 0;
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            const auto color = source.pixelColor(x, y);
            if (color.alpha() >= 200 &&
                (lightInk ? color.lightness() > 192 : color.lightness() < 64)) {
                bounds = bounds.united(QRect(x, y, 1, 1));
                ++inkPixels;
            }
        }
    }
    if (inkPixels < 20 || bounds.width() < 20 || bounds.height() < 5) {
        return {};
    }
    return source.copy(bounds.adjusted(-1, -1, 1, 1).intersected(source.rect()));
}

// Search rather than duplicating the exporter's placement calculation. The
// expected content comes from a real open widget; only the exporter can place
// it in the resulting PNG.
bool containsExactPatch(const QImage& capture, const QImage& witness) {
    if (witness.isNull() || capture.width() < witness.width() ||
        capture.height() < witness.height()) {
        return false;
    }
    const auto scene = capture.convertToFormat(QImage::Format_ARGB32);
    const auto patch = witness.convertToFormat(QImage::Format_ARGB32);
    QPoint anchor;
    for (int y = 0; y < patch.height(); ++y) {
        for (int x = 0; x < patch.width(); ++x) {
            if (patch.pixel(x, y) != patch.pixel(0, 0)) {
                anchor = QPoint(x, y);
                break;
            }
        }
        if (!anchor.isNull())
            break;
    }
    for (int y = 0; y <= scene.height() - patch.height(); ++y) {
        for (int x = 0; x <= scene.width() - patch.width(); ++x) {
            if (scene.pixel(x + anchor.x(), y + anchor.y()) != patch.pixel(anchor))
                continue;
            bool matches = true;
            for (int row = 0; row < patch.height(); ++row) {
                if (std::memcmp(scene.constScanLine(y + row) + x * 4, patch.constScanLine(row),
                                static_cast<std::size_t>(patch.width()) * 4) != 0) {
                    matches = false;
                    break;
                }
            }
            if (matches)
                return true;
        }
    }
    return false;
}
} // namespace

class PreviewTest final : public QObject {
    Q_OBJECT

  private slots:
    void confirmationSpecimenUsesProductionCancellationBoundary() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("confirmations"));
        window.show();
        auto* open = window.findChild<QPushButton*>("previewOpenDialog");
        QVERIFY(open);
        open->click();
        auto* confirmation =
            window.findChild<choscordb::ConfirmationDialog*>("previewActualDialog");
        QVERIFY(confirmation);
        QVERIFY(confirmation->isModal());
        auto* cancel = confirmation->button(QMessageBox::Cancel);
        QVERIFY(cancel);
        QCOMPARE(confirmation->defaultButton(), cancel);
        QTest::keyClick(confirmation, Qt::Key_Escape);
        QTRY_VERIFY(!confirmation->isVisible());
        auto* status = open->parentWidget()->findChild<QLabel*>("previewConfirmationStatus");
        QVERIFY(status);
        QCOMPARE(status->text(), QString("Cancelled."));
    }

    void initTestCase() { QApplication::setStyle(new choscordb::design::ControlStyle); }

    void exportedPopupContainsItsVisibleContent_data() {
        QTest::addColumn<QString>("specimen");
        QTest::newRow("modal") << QString("dialogs");
        QTest::newRow("menu") << QString("menus");
        QTest::newRow("completion") << QString("completion");
        QTest::newRow("tooltip") << QString("tooltip-popover");
    }

    void exportedPopupContainsItsVisibleContent() {
        QFETCH(QString, specimen);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        choscordb::design::PreviewWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(window.selectSpecimen(specimen));
        QCoreApplication::processEvents();
        auto* light = window.findChild<QWidget*>("previewLight");
        QImage witness;
        if (specimen == "dialogs") {
            light->findChild<QPushButton*>("previewOpenDialog")->click();
            auto* dialog = light->findChild<QDialog*>("previewActualDialog");
            QVERIFY(dialog);
            QVERIFY(dialog->isVisible());
            QLabel* heading = nullptr;
            for (auto* label : dialog->findChildren<QLabel*>()) {
                if (label->text() == "Connection details")
                    heading = label;
            }
            QVERIFY(heading);
            const auto bounds = QRect(heading->mapTo(dialog, QPoint()), heading->size());
            witness = textInkPatch(visibleSurfaceSnapshot(*dialog).copy(bounds));
            dialog->reject();
        } else if (specimen == "menus") {
            light->findChild<QPushButton*>("previewOpenMenu")->click();
            auto* menu = light->findChild<QMenu*>("previewActualMenu");
            QVERIFY(menu);
            QVERIFY(menu->isVisible());
            QAction* action = nullptr;
            for (auto* candidate : menu->actions()) {
                if (candidate->text() == "Wrap text")
                    action = candidate;
            }
            QVERIFY(action);
            QCoreApplication::processEvents();
            const auto menuSnapshot = menu->grab().toImage().convertToFormat(QImage::Format_ARGB32);
            witness = textInkPatch(menuSnapshot.copy(menu->actionGeometry(action)));
            menu->hide();
        } else if (specimen == "completion") {
            light->findChild<QPushButton*>("previewOpenCompletion")->click();
            auto* popup = light->findChild<QCompleter*>()->popup();
            QTRY_VERIFY(popup->isVisible());
            const auto index = popup->model()->index(0, 0);
            QVERIFY(index.data().toString().contains("synthetic_customers"));
            witness = textInkPatch(
                visibleSurfaceSnapshot(*popup->viewport()).copy(popup->visualRect(index)));
            popup->hide();
        } else {
            light->findChild<QPushButton*>("previewOpenTooltip")->click();
            auto* tooltip = window.findChild<QWidget*>("designTooltip");
            QVERIFY(tooltip);
            QVERIFY(tooltip->isVisible());
            QVERIFY(tooltip->accessibleName().contains("Synthetic help"));
            witness = textInkPatch(visibleSurfaceSnapshot(*tooltip), true);
            tooltip->hide();
        }
        QVERIFY2(!witness.isNull(), "The actual popup must supply a nonblank text-ink witness");
        const auto path = directory.filePath("surface.png");
        QVERIFY(window.exportCapture(path, false));
        QVERIFY2(containsExactPatch(QImage(path), witness),
                 "Exported pixels are missing the independently observed popup content");
    }

    void tooltipUsesTheProductionSurfaceInBothThemes() {
        choscordb::design::PreviewWindow window;
        window.show();
        QVERIFY(window.selectSpecimen("tooltip-popover"));
        QCoreApplication::processEvents();
        auto* light = window.findChild<QWidget*>("previewLight");
        light->findChild<QPushButton*>("previewOpenTooltip")->click();
        auto* tooltip = window.findChild<QWidget*>("designTooltip");
        QVERIFY(tooltip);
        QVERIFY(tooltip->isVisible());
        QVERIFY(tooltip->accessibleName().contains("Synthetic help"));
        const auto lightColor = tooltip->grab().toImage().pixelColor(10, 10);
        auto* dark = window.findChild<QWidget*>("previewDark");
        dark->findChild<QPushButton*>("previewOpenTooltip")->click();
        tooltip = window.findChild<QWidget*>("designTooltip");
        QVERIFY(tooltip);
        QVERIFY(tooltip->isVisible());
        const auto darkColor = tooltip->grab().toImage().pixelColor(10, 10);
        QVERIFY(lightColor.lightness() < 128);
        QVERIFY(darkColor.lightness() > 128);
    }

    void exportCapturesTheRealCompletionPopup() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("completion"));
        const auto path = directory.filePath("completion.png");
        QVERIFY(window.exportCapture(path));
        QFile metadata(path + ".json");
        QVERIFY(metadata.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(metadata.readAll()).object().value("surface").toString(),
                 QString("completion-popup"));
        QCOMPARE(QImage(path).size(), QSize(1280, 900));
    }

    void exportsRenderActualModalAndMenuSurfaces() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("dialogs"));
        const auto path = directory.filePath("modal.png");
        QVERIFY(window.exportCapture(path, false));
        QFile metadata(path + ".json");
        QVERIFY(metadata.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(metadata.readAll()).object().value("surface").toString(),
                 QString("modal"));
        QCOMPARE(QImage(path).pixelColor(8, 880), QColor(229, 229, 229));
        QVERIFY(window.selectSpecimen("menus"));
        const auto menuPath = directory.filePath("menu.png");
        QVERIFY(window.exportCapture(menuPath, false));
        QFile menuMetadata(menuPath + ".json");
        QVERIFY(menuMetadata.open(QIODevice::ReadOnly));
        QCOMPARE(
            QJsonDocument::fromJson(menuMetadata.readAll()).object().value("surface").toString(),
            QString("menu"));
        QVERIFY(QImage(menuPath) != QImage(path));
        QVERIFY(window.selectSpecimen("tooltip-popover"));
        const auto tooltipPath = directory.filePath("tooltip.png");
        QVERIFY(window.exportCapture(tooltipPath, false));
        QFile tooltipMetadata(tooltipPath + ".json");
        QVERIFY(tooltipMetadata.open(QIODevice::ReadOnly));
        QCOMPARE(
            QJsonDocument::fromJson(tooltipMetadata.readAll()).object().value("surface").toString(),
            QString("tooltip"));
    }

    void completionUsesTheRealOfflinePopup() {
        choscordb::design::PreviewWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(window.selectSpecimen("completion"));
        // Present the newly constructed editor before simulating the user's click.
        QCoreApplication::processEvents();
        auto* light = window.findChild<QWidget*>("previewLight");
        auto* open = light->findChild<QPushButton*>("previewOpenCompletion");
        QVERIFY(open);
        open->click();
        auto* completer = light->findChild<QCompleter*>();
        QVERIFY(completer);
        auto* popup = completer->popup();
        QCOMPARE(popup->objectName(), QString("sqlCompletionPopup"));
        QTRY_VERIFY(popup->isVisible());
        QVERIFY(popup->model()->rowCount() > 0);
        QTest::keyClick(popup, Qt::Key_Escape);
        QTRY_VERIFY(!popup->isVisible());
    }

    void standaloneExportsWithoutAProfile() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("standalone.png");
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("XDG_CONFIG_HOME", directory.filePath("config"));
        environment.insert("XDG_DATA_HOME", directory.filePath("data"));
        process.setProcessEnvironment(environment);
        process.start(QCoreApplication::applicationDirPath() + "/choscordb-component-gallery",
                      {"--export", path, "--specimen", "buttons", "--single"});
        QVERIFY(process.waitForStarted());
        const bool finished = process.waitForFinished(5000);
        if (!finished) {
            process.kill();
            process.waitForFinished();
        }
        QVERIFY2(finished, "Standalone export did not exit; CLI export is missing");
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(QImage(path).size(), QSize(640, 900));
    }

    void iconsShowNamedProductionAssetsAtSupportedSizes() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("icons"));
        auto* light = window.findChild<QWidget*>("previewLight");
        for (int size : {12, 14, 16, 20, 24}) {
            auto* icon = light->findChild<QLabel*>(QString("icon-database-%1").arg(size));
            QVERIFY(icon);
            QVERIFY(!icon->pixmap().isNull());
            QCOMPARE(icon->pixmap().size(), QSize(size, size));
        }
    }

    void databaseEditorsUseIndependentPaperColors_data() {
        QTest::addColumn<QString>("specimen");
        QTest::newRow("SQL editor") << QString("sql-editor");
        QTest::newRow("completion editor") << QString("completion");
        QTest::newRow("editor preferences") << QString("editor-preferences");
    }

    void databaseEditorsUseIndependentPaperColors() {
        QFETCH(QString, specimen);
        choscordb::design::PreviewWindow window;
        window.show();
        QVERIFY(window.selectSpecimen(specimen));
        QCoreApplication::processEvents();
        auto* light = window.findChild<QWidget*>("previewLight");
        auto* dark = window.findChild<QWidget*>("previewDark");
        auto* lightEditor = light->findChild<choscordb::SqlEditor*>();
        auto* darkEditor = dark->findChild<choscordb::SqlEditor*>();
        QVERIFY(lightEditor);
        QVERIFY(darkEditor);
        QCOMPARE(lightEditor->SendScintilla(QsciScintillaBase::SCI_STYLEGETBACK, 0),
                 long(0xffffff));
        QCOMPARE(darkEditor->SendScintilla(QsciScintillaBase::SCI_STYLEGETBACK, 0), long(0x171717));
    }

    void databaseFixturesRetainNullEmptyAndEditorSemantics() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("results"));
        auto* light = window.findChild<QWidget*>("previewLight");
        auto* table = light->findChild<QTableView*>("previewResults");
        QVERIFY(table);
        auto* model = qobject_cast<choscordb::ResultTableModel*>(table->model());
        QVERIFY(model);
        QCOMPARE(model->data(model->index(0, 1)).toString(), QString("NULL"));
        QCOMPARE(model->data(model->index(1, 1)).toString(), QString(""));
        QCOMPARE(model->data(model->index(0, 1), Qt::UserRole).toBool(), true);
        QCOMPARE(model->data(model->index(1, 1), Qt::UserRole).toBool(), false);
        QVERIFY(model->data(model->index(2, 1)).toString().contains("open to load"));
        QVERIFY(window.selectSpecimen("sql-editor"));
        auto* editor = light->findChild<choscordb::SqlEditor*>("previewSqlEditor");
        QVERIFY(editor);
        QVERIFY(editor->text().contains("SELECT"));
        QVERIFY(editor->text().contains("synthetic"));
        editor->setText("SELECT 2;");
        QCOMPARE(editor->text(), QString("SELECT 2;"));
    }

    void examplesOpenActualDismissibleSurfaces() {
        choscordb::design::PreviewWindow window;
        window.show();
        QVERIFY(window.selectSpecimen("dialogs"));
        auto* light = window.findChild<QWidget*>("previewLight");
        auto* open = light->findChild<QPushButton*>("previewOpenDialog");
        QVERIFY(open);
        open->click();
        auto* dialog = window.findChild<QDialog*>("previewActualDialog");
        QVERIFY(dialog);
        QVERIFY(dialog->isVisible());
        QVERIFY(dialog->isModal());
        QTest::keyClick(dialog, Qt::Key_Escape);
        QVERIFY(!dialog->isVisible());
        QCOMPARE(dialog->result(), int(QDialog::Rejected));
        QVERIFY(window.selectSpecimen("menus"));
        auto* menuButton = light->findChild<QPushButton*>("previewOpenMenu");
        QVERIFY(menuButton);
        menuButton->click();
        auto* menu = light->findChild<QMenu*>("previewActualMenu");
        QVERIFY(menu);
        QVERIFY(menu->isVisible());
        QTest::keyClick(menu, Qt::Key_Escape);
        QVERIFY(!menu->isVisible());
    }

    void fieldSpecimenRetainsEditingAndValidationStates() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("fields"));
        auto* light = window.findChild<QWidget*>("previewLight");
        auto* editable = light->findChild<QLineEdit*>("field-editable");
        QVERIFY(editable);
        QTest::keyClicks(editable, "analyst");
        QCOMPARE(editable->text(), QString("analyst"));
        auto* password = light->findChild<QLineEdit*>("field-password");
        QVERIFY(password);
        QCOMPARE(password->echoMode(), QLineEdit::Password);
        auto* readOnly = light->findChild<QLineEdit*>("field-readonly");
        QVERIFY(readOnly);
        QVERIFY(readOnly->isReadOnly());
        auto* invalid = light->findChild<QLineEdit*>("field-invalid");
        QVERIFY(invalid);
        QVERIFY(invalid->property("invalid").toBool());
        auto* error = light->findChild<QLabel*>("field-error");
        QVERIFY(error);
        QVERIFY(error->text().contains("required"));
    }

    void buttonsUseProductionVariantsAndStates() {
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
        auto* largest = light->findChild<Button*>("button-size-icon-lg");
        QVERIFY(largest);
        QCOMPARE(largest->buttonSize(), ButtonSize::IconLarge);
    }

    void individualSpecimensAreSelectableAndSearchable() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.specimenIds().contains("buttons"));
        QVERIFY(window.specimenIds().contains("results"));
        QVERIFY(window.specimenIds().contains("completion"));
        QVERIFY(window.selectSpecimen("results"));
        QVERIFY(!window.selectSpecimen("missing"));
        auto* search = window.findChild<QLineEdit*>("previewSearch");
        search->setText("password");
        QCOMPARE(window.visibleSections(), QStringList({"Components", "Compositions"}));
    }

    void tokensExposeCopyableValuesAndSources() {
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
        QVERIFY(QApplication::clipboard()->text().contains("#ffffff", Qt::CaseInsensitive));
        QVERIFY(QApplication::clipboard()->text().contains("desktop/design_system/theme.cpp"));
    }

    void exportsAreDeterministicAndFailuresVisible() {
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

    void comparisonThemesAreIndependent() {
        const auto applicationPalette = qApp->palette();
        const auto applicationStyle = qApp->styleSheet();
        choscordb::design::PreviewWindow window;
        auto* light = window.findChild<QWidget*>("previewLight");
        auto* dark = window.findChild<QWidget*>("previewDark");
        QVERIFY(light);
        QVERIFY(dark);
        QVERIFY(light->palette().color(QPalette::Window) !=
                dark->palette().color(QPalette::Window));
        const auto darkPalette = dark->palette();
        auto changed = light->palette();
        changed.setColor(QPalette::Window, Qt::red);
        light->setPalette(changed);
        QCOMPARE(dark->palette(), darkPalette);
        QCOMPARE(qApp->palette(), applicationPalette);
        QCOMPARE(qApp->styleSheet(), applicationStyle);
    }

    void navigationIsSearchable() {
        choscordb::design::PreviewWindow window;
        QCOMPARE(window.visibleSections(),
                 QStringList({"Tokens", "Typography", "Icons", "Components", "Compositions",
                              "Database UI"}));
        auto* search = window.findChild<QLineEdit*>("previewSearch");
        QVERIFY(search);
        search->setText("dataBASE");
        QCOMPARE(window.visibleSections(), QStringList({"Database UI"}));
        search->setText("no matching specimen");
        QVERIFY(window.visibleSections().isEmpty());
        search->clear();
        QCOMPARE(window.visibleSections().size(), 6);
    }
};

QTEST_MAIN(PreviewTest)
#include "preview_test.moc"
