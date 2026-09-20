#include "design_system/button/button.h"
#include "design_system/control_style.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/field/field.h"
#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/text/text.h"
#include "tools/preview/preview_window.h"

#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/toast_region/toast_region.h"
#include "models/navigator_model.h"
#include "models/result_table_model.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
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
    void editorResultsSplitUsesEqualPanesInBothThemes() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("separators-splitters"));
        window.show();
        QCoreApplication::processEvents();
        for (const auto* name : {"previewLight", "previewDark"}) {
            auto* host = window.findChild<QWidget*>(name);
            QVERIFY(host);
            auto* splitter = host->findChild<QSplitter*>("previewEditorResultsSplit");
            QVERIFY(splitter);
            QCOMPARE(splitter->orientation(), Qt::Vertical);
            const auto sizes = splitter->sizes();
            QVERIFY(qAbs(sizes[0] - sizes[1]) <= 2);
        }
    }

    void documentTabSpecimenShowsFixedWidthTabsInBothThemes() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("tabs"));
        window.show();
        QCoreApplication::processEvents();
        for (const auto* theme : {"previewLight", "previewDark"}) {
            auto* host = window.findChild<QWidget*>(theme);
            QVERIFY(host);
            auto* tabs = host->findChild<QTabWidget*>();
            QVERIFY(tabs);
            QCOMPARE(tabs->tabText(0), QString("abc.sql"));
            QVERIFY(!tabs->tabIcon(0).isNull());
            QCOMPARE(tabs->tabBar()->tabRect(0).width(), tabs->tabBar()->tabRect(1).width());
            QVERIFY(tabs->tabBar()->tabRect(0).width() <= 118);
        }
    }
    void richTextParagraphsHaveCompactSpacing() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("textareas"));
        auto* editor = window.findChild<QTextEdit*>();
        QVERIFY(editor);
        editor->moveCursor(QTextCursor::End);
        QTest::keyClick(editor, Qt::Key_Return);
        editor->insertPlainText("Next line");
        const auto first = editor->document()->firstBlock();
        const auto second = first.next();
        QVERIFY(second.isValid());
        QCOMPARE(first.blockFormat().bottomMargin(), 0.0);
        QCOMPARE(second.blockFormat().topMargin(), 0.0);
    }
    void showToastOpensTransientToastAtViewportCorner() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("feedback"));
        window.show();
        auto* host = window.findChild<QWidget*>("previewLight");
        QVERIFY(host);
        auto* scroll = host->findChild<QScrollArea*>("previewContentScroll");
        auto* toast = host->findChild<choscordb::ToastRegion*>("toastRegion");
        auto* showToast = host->findChild<QPushButton*>("previewToast_success");
        QVERIFY(scroll && toast && showToast);
        QCOMPARE(showToast->text(), QString("Show success toast"));
        QVERIFY(toast->isHidden());
        showToast->click();
        QTRY_VERIFY(toast->isVisible());
        QVERIFY(toast->text().contains("Your changes have been saved."));
        QCOMPARE(toast->parentWidget(), scroll->viewport());
        QVERIFY(toast->geometry().right() <= scroll->viewport()->width() - 8);
        QVERIFY(toast->geometry().bottom() <= scroll->viewport()->height() - 8);
        QVERIFY(toast->geometry().right() > scroll->viewport()->width() / 2);
        QVERIFY(toast->geometry().bottom() > scroll->viewport()->height() / 2);
        window.resize(window.width() + 160, window.height() + 80);
        QCoreApplication::processEvents();
        QCOMPARE(toast->geometry().right(), scroll->viewport()->width() - 17);
        QCOMPARE(toast->geometry().bottom(), scroll->viewport()->height() - 17);
        QTRY_VERIFY_WITH_TIMEOUT(toast->isHidden(), 8000);
    }
    void toastPortalIsPresentInBothThemes() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("feedback"));
        window.show();
        for (const auto* name : {"previewLight", "previewDark"}) {
            auto* host = window.findChild<QWidget*>(name);
            QVERIFY(host);
            auto* scroll = host->findChild<QScrollArea*>("previewContentScroll");
            auto* toast = host->findChild<choscordb::ToastRegion*>("toastRegion");
            QVERIFY(scroll && toast);
            toast->showToast("Saved", "Portal specimen", choscordb::ToastVariant::Success, 0);
            QCOMPARE(toast->parentWidget(), scroll->viewport());
            QCOMPARE(toast->geometry().right(), scroll->viewport()->width() - 17);
            QCOMPARE(toast->geometry().bottom(), scroll->viewport()->height() - 17);
        }
    }
    void progressToastHasPersistentIndicatorInBothThemes() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("feedback"));
        window.show();
        for (const auto* name : {"previewLight", "previewDark"}) {
            auto* host = window.findChild<QWidget*>(name);
            auto* start = host->findChild<QPushButton*>("previewToast_progress");
            auto* finish = host->findChild<QPushButton*>("previewToast_progressDone");
            QVERIFY(start && finish);
            start->click();
            auto* progress = host->findChild<choscordb::ToastRegion*>("progressToast");
            QVERIFY(progress);
            QVERIFY(progress->isVisible());
            QVERIFY(progress->findChild<QProgressBar*>()->isVisible());
            QVERIFY(!progress->findChild<QTimer*>()->isActive());
            QVERIFY(progress->isVisible());
            progress->clearNotice();
            progress->showProgress("Exporting", "Another batch…");
            QTest::qWait(250);
            QVERIFY(progress->isVisible());
            finish->click();
            QTRY_VERIFY(progress->isHidden());
        }
    }
    void toastCanAttachAcrossWidgetTrees() {
        QWidget source;
        QWidget host;
        host.resize(500, 300);
        host.show();
        choscordb::ToastRegion toast(&source);
        toast.attachTo(&host);
        toast.showToast("Saved", "Moved to overlay", choscordb::ToastVariant::Success, 5000);
        QCOMPARE(toast.parentWidget(), &host);
        QCOMPARE(toast.geometry().right(), host.width() - 17);
        QCOMPARE(toast.geometry().bottom(), host.height() - 17);
        host.resize(600, 400);
        QCoreApplication::processEvents();
        QCOMPARE(toast.geometry().right(), host.width() - 17);
        QCOMPARE(toast.geometry().bottom(), host.height() - 17);
    }
    void toastVariantsShowTitleBodyAndUseConfiguredTimeout() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("feedback"));
        window.show();
        auto* host = window.findChild<QWidget*>("previewLight");
        QVERIFY(host);
        auto* toast = host->findChild<choscordb::ToastRegion*>("toastRegion");
        auto* opacity = toast ? toast->findChild<QGraphicsOpacityEffect*>() : nullptr;
        auto* seconds = host->findChild<QSpinBox*>("previewToastSeconds");
        QVERIFY(toast && opacity && seconds);
        seconds->setValue(1);
        for (const auto& variant : {"success", "warning", "danger"}) {
            auto* button = host->findChild<QPushButton*>(QString("previewToast_%1").arg(variant));
            QVERIFY(button);
            button->click();
            QTRY_VERIFY(toast->isVisible());
            QTRY_VERIFY(opacity->opacity() > 0.9);
            QCOMPARE(toast->property("variant").toString(), QString(variant));
            QVERIFY(toast->text().contains("<b>"));
            QVERIFY(toast->text().contains("<br/>"));
        }
        QTRY_VERIFY_WITH_TIMEOUT(toast->isHidden(), 2000);
        toast->showToast("Warning", "Persistent warning", choscordb::ToastVariant::Warning, 0);
        QVERIFY(toast->text().contains("Persistent warning"));
        QCOMPARE(toast->property("variant").toString(), QString("warning"));
        QTRY_VERIFY(opacity->opacity() > 0.9);
        QTest::qWait(1200);
        QVERIFY(toast->isVisible());
        toast->clearNotice();
        QTRY_VERIFY(opacity->opacity() < 0.1);
        QTRY_VERIFY(toast->isHidden());
        toast->showToast("Saved", "First", choscordb::ToastVariant::Success, 5000);
        toast->clearNotice();
        toast->showToast("Error", "Replacement", choscordb::ToastVariant::Danger, 5000);
        QTRY_VERIFY(opacity->opacity() > 0.9);
        QVERIFY(toast->isVisible());
        QVERIFY(toast->text().contains("Replacement"));
        QCOMPARE(toast->property("variant").toString(), QString("danger"));
    }
    void nonmodalDialogSurfaceHasNoOutline() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("nonmodal"));
        window.show();
        auto* host = window.findChild<QWidget*>("previewLight");
        QVERIFY(host);
        auto* open = host->findChild<QPushButton*>("previewOpenDialog");
        auto* dialog = host->findChild<QDialog*>("previewActualDialog");
        QVERIFY(open && dialog);
        open->click();
        QTRY_VERIFY(dialog->isVisible());
        const auto image = visibleSurfaceSnapshot(*dialog);
        const int y = image.height() / 2;
        QCOMPARE(image.pixelColor(0, y), QColor("#ffffff"));
        QCOMPARE(image.pixelColor(1, y), QColor("#ffffff"));
        QCOMPARE(image.pixelColor(2, y), QColor("#ffffff"));
        dialog->reject();
    }
    void nonmodalDialogGrowsWhenDescriptionWraps() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("nonmodal"));
        window.show();
        for (const auto* theme : {"previewLight", "previewDark"}) {
            auto* host = window.findChild<QWidget*>(theme);
            QVERIFY(host);
            auto* open = host->findChild<QPushButton*>("previewOpenDialog");
            auto* dialog = host->findChild<choscordb::DialogShell*>("previewActualDialog");
            QVERIFY(open && dialog);
            open->click();
            QTRY_VERIFY(dialog->isVisible());
            auto* description =
                host->findChild<choscordb::design::Text*>("previewDialogDescription");
            QVERIFY(description);
            for (const int width : {338, 280}) {
                dialog->resize(width, 1);
                QCoreApplication::processEvents();
                QVERIFY(description->heightForWidth(description->width()) >
                        description->sizeHint().height());
                QVERIFY(description->height() >= description->heightForWidth(description->width()));
                for (auto* button : dialog->findChildren<QPushButton*>()) {
                    QVERIFY(dialog->rect().contains(
                        QRect(button->mapTo(dialog, QPoint{}), button->size())));
                }
            }
            dialog->reject();
        }
    }
    void galleryOpenKeepsAppModalityAndNativeCorners_data() {
        QTest::addColumn<QString>("specimen");
        QTest::newRow("panel") << QString("dialogs");
        QTest::newRow("confirmation") << QString("confirmations");
    }
    void galleryOpenKeepsAppModalityAndNativeCorners() {
        QFETCH(QString, specimen);
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen(specimen));
        window.resize(1280, 900);
        window.show();
        window.activateWindow();
        auto* host = window.findChild<QWidget*>("previewLight");
        QVERIFY(host);
        auto* open = host->findChild<QPushButton*>("previewOpenDialog");
        auto* dialog = host->findChild<QDialog*>("previewActualDialog");
        QVERIFY(open && dialog);
        open->setFocus(Qt::TabFocusReason);
        QTRY_VERIFY(open->hasFocus());
        QSignalSpy finished(dialog, &QDialog::finished);
        QTest::mouseClick(open, Qt::LeftButton);
        QTRY_VERIFY(dialog->isVisible());
        QCOMPARE(finished.count(), 0); // Opening remains asynchronous.
        QCOMPARE(dialog->windowModality(), Qt::ApplicationModal);
        if (QGuiApplication::platformName() == "cocoa") {
            QVERIFY(QTest::qWaitForWindowExposed(dialog));
            QTest::qWait(150);
            const auto position = dialog->mapToGlobal(QPoint());
            const auto capture = dialog->screen()->grabWindow(0, position.x(), position.y(),
                                                              dialog->width(), dialog->height());
            QVERIFY2(!capture.isNull(),
                     "Actual OS-composited corner capture is required on Cocoa.");
            const auto captureDirectory = qEnvironmentVariable("CHOSCORDB_TEST_CAPTURE_DIR");
            if (!captureDirectory.isEmpty()) {
                QVERIFY(QDir().mkpath(captureDirectory));
                QVERIFY(
                    capture.save(QDir(captureDirectory)
                                     .filePath(QString("native-gallery-%1.png").arg(specimen))));
            }
            const auto pixel = capture.toImage().pixelColor(qRound(5 * capture.devicePixelRatio()),
                                                            qRound(5 * capture.devicePixelRatio()));
            QVERIFY2(pixel.red() > 240 && pixel.green() > 240 && pixel.blue() > 240,
                     qPrintable(pixel.name()));
        }
        QTest::keyClick(dialog, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->isVisible());
        QCOMPARE(finished.count(), 1);
        QTRY_VERIFY(open->hasFocus());
    }

    void scrollSpecimensKeepTheirIndependentThemePaper() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("selects"));
        window.resize(1280, 900);
        window.show();
        QCoreApplication::processEvents();
        for (const auto& [name, expected] : {std::pair{"previewLight", QColor("#ffffff")},
                                             std::pair{"previewDark", QColor("#20272b")}}) {
            auto* host = window.findChild<QWidget*>(name);
            auto* content = host->findChild<QWidget*>("previewContent");
            QVERIFY(content);
            const auto snapshot = content->grab().toImage();
            QCOMPARE(snapshot.pixelColor(snapshot.width() / 2, snapshot.height() - 20), expected);
        }
    }

    void narrowGalleryKeepsNavigationAndActionsReachable() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("buttons"));
        window.resize(960, 640);
        window.show();
        QCoreApplication::processEvents();
        QCOMPARE(window.size(), QSize(960, 640));
        auto* exportButton = window.findChild<QPushButton*>("previewExport");
        QVERIFY(exportButton);
        QCOMPARE(exportButton->visibleRegion(), QRegion(exportButton->rect()));
        QVERIFY(window.rect().contains(
            QRect(exportButton->mapTo(&window, QPoint()), exportButton->size())));
        auto* light = window.findChild<QWidget*>("previewLight");
        QVERIFY(light);
        auto* scroll = light->findChild<QScrollArea*>("previewContentScroll");
        QVERIFY2(scroll, "Overflow must scroll inside the specimen at the minimum window size.");
        auto* last = light->findChild<QPushButton*>("button-size-icon-lg");
        QVERIFY(last);
        auto* previous = light->findChild<QPushButton*>("button-size-icon");
        QVERIFY(previous);
        previous->setFocus(Qt::TabFocusReason);
        QTRY_VERIFY(previous->hasFocus());
        QTest::keyClick(previous, Qt::Key_Tab);
        QTRY_VERIFY(last->hasFocus());
        const QRect viewport(scroll->viewport()->mapToGlobal(QPoint()), scroll->viewport()->size());
        QVERIFY(viewport.contains(QRect(last->mapToGlobal(QPoint()), last->size())));
        QSignalSpy clicked(last, &QPushButton::clicked);
        QTest::keyClick(last, Qt::Key_Space);
        QCOMPARE(clicked.count(), 1);
    }

    void standaloneCapturesRequestedThemeAndViewport_data() {
        QTest::addColumn<QString>("theme");
        QTest::addColumn<QSize>("viewport");
        QTest::addColumn<QString>("specimen");
        QTest::newRow("dark-narrow") << QString("dark") << QSize(960, 640) << QString("fields");
        QTest::newRow("light-wide") << QString("light") << QSize(1280, 900) << QString("fields");
        QTest::newRow("dark-modal") << QString("dark") << QSize(960, 640) << QString("dialogs");
        QTest::newRow("dark-nested-scroll")
            << QString("dark") << QSize(960, 640) << QString("scrolling");
        QTest::newRow("dark-tabs") << QString("dark") << QSize(960, 640) << QString("tabs");
        QTest::newRow("light-tabs") << QString("light") << QSize(1280, 900) << QString("tabs");
    }

    void standaloneCapturesRequestedThemeAndViewport() {
        QFETCH(QString, theme);
        QFETCH(QSize, viewport);
        QFETCH(QString, specimen);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("viewport.png");
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("XDG_CONFIG_HOME", directory.filePath("config"));
        environment.insert("XDG_DATA_HOME", directory.filePath("data"));
        process.setProcessEnvironment(environment);
        process.start(QCoreApplication::applicationDirPath() + "/choscordb-component-gallery",
                      {"--export", path, "--specimen", specimen, "--theme", theme, "--width",
                       QString::number(viewport.width()), "--height",
                       QString::number(viewport.height())});
        QVERIFY(process.waitForStarted());
        QVERIFY(process.waitForFinished(5000));
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
        const QImage capture(path);
        QCOMPARE(capture.size(), viewport);
        QCOMPARE(capture.pixelColor(2, 2).lightness() < 128, theme == "dark");
        QCOMPARE(capture.pixelColor(viewport.width() / 2, viewport.height() / 2).lightness() < 128,
                 theme == "dark");
        QCOMPARE(capture.pixelColor(viewport.width() / 2, viewport.height() - 20).lightness() < 128,
                 theme == "dark");
        QFile metadata(path + ".json");
        QVERIFY(metadata.open(QIODevice::ReadOnly));
        const auto record = QJsonDocument::fromJson(metadata.readAll()).object();
        QCOMPARE(record.value("themes").toString().toLower(), theme);
        QCOMPARE(record.value("logicalWidth").toInt(), viewport.width());
        QCOMPARE(record.value("logicalHeight").toInt(), viewport.height());
        if (specimen == "fields" || specimen == "tabs") {
            QRect scrollViewport, control;
            for (const auto value : record.value("controls").toArray()) {
                const auto item = value.toObject();
                const QRect bounds(item.value("x").toInt(), item.value("y").toInt(),
                                   item.value("width").toInt(), item.value("height").toInt());
                const auto name = item.value("name").toString();
                if (name == "qt_scrollarea_viewport" && scrollViewport.isNull())
                    scrollViewport = bounds;
                if (name == (specimen == "fields" ? "field-editable" : "previewObjectTabs"))
                    control = bounds;
            }
            QVERIFY(!control.isNull());
            QVERIFY2(scrollViewport.contains(control),
                     "Export clips a field edge or tab indicator");
        }
    }

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
        QTest::newRow("tooltip") << QString("tooltip-popover");
        QTest::newRow("selector") << QString("selects");
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
        if (specimen == "selects") {
            auto* select = light->findChild<QComboBox*>();
            QVERIFY(select);
            select->showPopup();
            QCoreApplication::processEvents();
            QVERIFY(select->view()->isVisible());
            // Export uses logical pixels; native Cocoa grabs use device pixels.
            witness = textInkPatch(select->view()->grab().toImage().scaled(select->view()->size()));
            select->hidePopup();
        } else if (specimen == "dialogs") {
            light->findChild<QPushButton*>("previewOpenDialog")->click();
            auto* dialog = light->findChild<QDialog*>("previewActualDialog");
            QVERIFY(dialog);
            QVERIFY(dialog->isVisible());
            QLabel* heading = nullptr;
            for (auto* label : dialog->findChildren<QLabel*>()) {
                if (label->text() == "Panel details")
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
            const auto menuSnapshot =
                menu->grab().toImage().scaled(menu->size()).convertToFormat(QImage::Format_ARGB32);
            witness = textInkPatch(menuSnapshot.copy(menu->actionGeometry(action)));
            menu->hide();
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
        const auto backdrop = QImage(path).pixelColor(320, 880);
        // Reference panel paper plus translucent tint; blur/8-bit compositing may round by two.
        QVERIFY(qAbs(backdrop.red() - 183) <= 2);
        QVERIFY(qAbs(backdrop.green() - 189) <= 2);
        QVERIFY(qAbs(backdrop.blue() - 192) <= 2);
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

    void displayedIconsRasterizeAtTargetScale_data() {
        QTest::addColumn<int>("size");
        QTest::addColumn<qreal>("scale");
        for (const int size : {12, 14, 16, 20, 24}) {
            for (const qreal scale : {qreal(1), qreal(1.5), qreal(2)})
                QTest::newRow(qPrintable(QString("%1px-%2x").arg(size).arg(scale)))
                    << size << scale;
        }
    }
    void displayedIconsRasterizeAtTargetScale() {
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
    void iconsShowNamedProductionAssetsAtSupportedSizes() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("icons"));
        for (const auto* theme : {"previewLight", "previewDark"}) {
            auto* host = window.findChild<QWidget*>(theme);
            QVERIFY(host);
            for (const auto* name : {"database", "cancel", "square"}) {
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

    void sidebarTabSpecimenUsesProductionContextInBothThemes() {
        using namespace choscordb::design;
        PreviewWindow window;
        QVERIFY(window.selectSpecimen("buttons"));
        for (const auto* name : {"previewLight", "previewDark"}) {
            auto* host = window.findChild<QWidget*>(name);
            QVERIFY(host);
            auto* tab = host->findChild<Button*>("previewSidebarTab");
            QVERIFY(tab);
            QCOMPARE(tab->buttonContext(), ButtonContext::SidebarTab);
            QVERIFY(tab->isChecked());
        }
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
        QVERIFY(window.specimenIds().contains("dialogs"));
        QVERIFY(!window.specimenIds().contains("paging"));
        QVERIFY(!window.specimenIds().contains("paging-unknown"));
        QVERIFY(window.selectSpecimen("dialogs"));
        QVERIFY(!window.selectSpecimen("missing"));
        auto* search = window.findChild<QLineEdit*>("previewSearch");
        search->setText("password");
        QCOMPARE(window.visibleSections(), QStringList({"Components"}));
    }

    void fieldValidationIsBelowInputAndSelectInBothThemes() {
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
        QVERIFY(QApplication::clipboard()->text().contains("#f6f7f8", Qt::CaseInsensitive));
        QVERIFY(
            QApplication::clipboard()->text().contains("desktop/design_system/tokens/tokens.cpp"));
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

    void componentFamiliesAreRendered() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("dock"));
        auto* light = window.findChild<QWidget*>("previewLight");
        QVERIFY(light->findChild<QDockWidget*>());

        QVERIFY(window.selectSpecimen("lists-navigation"));
        for (const auto* name : {"previewLight", "previewDark"}) {
            auto* tree = window.findChild<QWidget*>(name)->findChild<QTreeView*>();
            QVERIFY(tree);
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
    }

    void dialogSectionsPreviewUsesRealComponentInBothThemes() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("dialog-sections"));
        window.show();
        for (const auto* name : {"previewLight", "previewDark"}) {
            auto* host = window.findChild<QWidget*>(name);
            QVERIFY(host);
            auto* open = host->findChild<QPushButton*>("previewOpenDialogSections");
            auto* dialog = host->findChild<QDialog*>("previewDialogSectionsModal");
            auto* sections =
                host->findChild<choscordb::design::DialogSections*>("previewDialogSections");
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
            dialog->reject();
        }
    }

    void navigationProfileRowsShowRegularAndSelectedStates() {
        choscordb::design::PreviewWindow window;
        QVERIFY(window.selectSpecimen("navigation-profile-row"));
        window.show();
        QCoreApplication::processEvents();
        for (const auto* name : {"previewLight", "previewDark"}) {
            auto* host = window.findChild<QWidget*>(name);
            QVERIFY(host);
            auto* list = host->findChild<QListWidget*>("previewNavigationProfiles");
            QVERIFY(list);
            QCOMPARE(list->count(), 2);
            QCOMPARE(list->spacing(), choscordb::design::spacing(choscordb::design::Spacing::Half));
            QCOMPARE(list->currentRow(), 1);
            QVERIFY(list->visualItemRect(list->item(1)).bottom() < list->viewport()->height());
            QCOMPARE(list->item(0)->text(), QString("test sqlite\nSQLite"));
            QCOMPARE(list->item(1)
                         ->data(choscordb::design::NavigationProfileDelegate::DriverRole)
                         .toString(),
                     QString("sqlite"));
            QVERIFY(
                dynamic_cast<choscordb::design::NavigationProfileDelegate*>(list->itemDelegate()));
            const auto height = list->visualItemRect(list->item(0)).height();
            QVERIFY(height >= 40 && height <= 44);
        }
    }

    void dockSpecimenRendersThemedTitleAndButtons() {
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

    void navigationTreeTogglesAndRenamesFromMenu() {
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
        auto* menu = tree->findChild<QMenu*>();
        QVERIFY(menu);
        auto* rename = menu->actions().isEmpty() ? nullptr : menu->actions().first();
        QVERIFY(rename);
        QCOMPARE(rename->text(), QStringLiteral("Rename"));
        const QPoint visibleMenu =
            menu->geometry().topLeft() + menu->actionGeometry(rename).topLeft();
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
};

QTEST_MAIN(PreviewTest)
#include "preview_test.moc"
