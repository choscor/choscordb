#include "preview_test.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/control_style.h"
#include "design_system/dialog_presentation/dialog_presentation.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "design_system/field/field.h"
#include "design_system/history_row/history_row.h"
#include "design_system/menu/menu.h"
#include "design_system/navigation_profile_row/navigation_profile_row.h"
#include "design_system/tabs/tab_add_corner.h"
#include "design_system/text/text.h"
#include "design_system/theme_manager.h"
#include "design_system/toast_region/toast_region.h"
#include "models/navigator_model.h"
#include "models/result_table_model.h"
#include "preview_test_helpers.h"
#include "tools/preview/preview_window.h"
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
#include <QtTest>
#include <cstring>

namespace {

// Pick text ink from the independently opened public surface. Transparent
// corners and the solid surface background cannot satisfy this witness.
QImage textInkPatch(const QImage& source, bool lightInk = false, int darkCutoff = 64) {
    QRect bounds;
    int inkPixels = 0;
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            const auto color = source.pixelColor(x, y);
            if (color.alpha() >= 200 &&
                (lightInk ? color.lightness() > 192 : color.lightness() < darkCutoff)) {
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

void PreviewTest::editorResultsSplitUsesEqualPanesInBothThemes() {
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

void PreviewTest::navigationTreeSpecimenUsesRealTreeInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("lists-navigation"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* tree = host->findChild<QTreeView*>("previewNavigationTree");
        QVERIFY(tree);
        QVERIFY(tree->isVisible());
        QVERIFY(tree->currentIndex().isValid());
        const auto child = tree->model()->index(0, 0, tree->model()->index(0, 0));
        const auto row = tree->visualRect(child);
        QVERIFY(row.isValid());
        QTest::mouseMove(tree->viewport(), QPoint(1, 1));
        QTest::mouseMove(tree->viewport(), row.center());
        QCoreApplication::processEvents();
        QCOMPARE(tree->viewport()->grab().toImage().pixelColor(row.right() - 8, row.center().y()),
                 choscordb::design::resolvedThemeForWidget(*tree).colors.muted);
    }
}

void PreviewTest::recentHistoryRowsUseSharedDelegateInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("recent-history-row"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* list = host->findChild<QListWidget*>("previewRecentHistoryRows");
        QVERIFY(list);
        QVERIFY(list->isVisible());
        list->setFixedWidth(260);
        QCoreApplication::processEvents();
        QCOMPARE(list->count(), 3);
        QVERIFY(list->itemDelegate());
        QVERIFY(dynamic_cast<choscordb::design::RecentHistoryRowDelegate*>(list->itemDelegate()));
        QCOMPARE(list->item(0)->data(Qt::UserRole + 72).toString(),
                 QString("SELECT *\nFROM customers\nWHERE id = 6;"));
        QCOMPARE(list->item(0)->data(Qt::UserRole + 73).toString(), QString("Example Postgres"));
        QCOMPARE(list->item(0)->data(Qt::UserRole + 74).toString(), QString("Sep 26, 14:24"));
        QCOMPARE(list->item(0)->data(Qt::UserRole + 75).toString(), QString("completed"));
        QCOMPARE(list->item(1)->data(Qt::UserRole + 75).toString(), QString("failed"));
        const auto row = list->visualItemRect(list->item(0));
        QVERIFY2(row.height() <= 70, qPrintable(QString::number(row.height())));
        QVERIFY(list->visualItemRect(list->item(2)).bottom() < list->viewport()->height());
        const auto beforeSqlChange = list->viewport()->grab(row).toImage();
        list->item(0)->setData(choscordb::design::RecentHistoryRowDelegate::SqlRole,
                               QString("DELETE *\nFROM customers\nWHERE id = 7;"));
        QCoreApplication::processEvents();
        QVERIFY(beforeSqlChange != list->viewport()->grab(row).toImage());
        const auto beforeDriverChange = list->viewport()->grab(row).toImage();
        list->item(0)->setData(choscordb::design::RecentHistoryRowDelegate::DriverRole,
                               QString("sqlite"));
        QCoreApplication::processEvents();
        QVERIFY(beforeDriverChange != list->viewport()->grab(row).toImage());
        const auto beforeDateChange = list->viewport()->grab(row).toImage();
        list->item(0)->setData(choscordb::design::RecentHistoryRowDelegate::WhenRole,
                               QString("Oct 17, 08:41"));
        QCoreApplication::processEvents();
        QVERIFY(beforeDateChange != list->viewport()->grab(row).toImage());
        QTest::mouseMove(list->viewport(), QPoint(1, 1));
        QTest::mouseMove(list->viewport(), row.center());
        QCoreApplication::processEvents();
        QCOMPARE(list->viewport()->grab().toImage().pixelColor(row.right() - 5, row.center().y()),
                 choscordb::design::resolvedThemeForWidget(*list).colors.muted);
        const auto colors = choscordb::design::resolvedThemeForWidget(*list).colors;
        const auto image = list->viewport()->grab().toImage();
        int successPixels = 0;
        int dangerPixels = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                successPixels += image.pixelColor(x, y) == colors.successSurface;
                dangerPixels += image.pixelColor(x, y) == colors.dangerSurface;
            }
        }
        QVERIFY(successPixels > 20);
        QVERIFY(dangerPixels > 20);
    }
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
        QCOMPARE(tabs->tabText(0), QString("abc.sql"));
        QCOMPARE(qobject_cast<QLabel*>(tabs->widget(0))->text(),
                 QString("Neutral document chrome"));
        QVERIFY(!tabs->tabIcon(0).isNull());
        QCOMPARE(tabs->tabBar()->tabRect(0).width(), tabs->tabBar()->tabRect(1).width());
        QVERIFY(tabs->tabBar()->tabRect(0).width() <= 118);
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
        QVERIFY(addPoint.x() - tabs->tabBar()->tabRect(0).right() <= 14);
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

void PreviewTest::richTextParagraphsHaveCompactSpacing() {
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

void PreviewTest::showToastOpensTransientToastAtViewportCorner() {
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

void PreviewTest::toastPortalIsPresentInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("feedback"));
    window.show();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        const auto expectedSurface = QString::fromLatin1(name) == QStringLiteral("previewLight")
                                         ? QStringLiteral("#287f66")
                                         : QStringLiteral("#65b493");
        const auto successRule =
            host->styleSheet()
                .section(QStringLiteral("QLabel#toastRegion[variant=\"success\"]"), 1)
                .section('}', 0, 0);
        QVERIFY2(successRule.contains(QStringLiteral("background-color: %1").arg(expectedSurface)),
                 qPrintable(successRule));
        auto* scroll = host->findChild<QScrollArea*>("previewContentScroll");
        auto* toast = host->findChild<choscordb::ToastRegion*>("toastRegion");
        QVERIFY(scroll && toast);
        toast->showToast("Saved", "Portal specimen", choscordb::ToastVariant::Success, 0);
        QTest::qWait(200);
        const auto sample = toast->mapTo(&window, QPoint(toast->width() - 20,
                                                       toast->height() - 20));
        const auto capture = window.grab();
        const auto scale = capture.devicePixelRatioF();
        QCOMPARE(capture.toImage().pixelColor(qRound(sample.x() * scale),
                                              qRound(sample.y() * scale)),
                 QColor(expectedSurface));
        QCOMPARE(toast->parentWidget(), scroll->viewport());
        QCOMPARE(toast->geometry().right(), scroll->viewport()->width() - 17);
        QCOMPARE(toast->geometry().bottom(), scroll->viewport()->height() - 17);
        auto* dismiss = toast->findChild<QToolButton*>("toastDismiss");
        QVERIFY(dismiss);
        QCOMPARE(dismiss->accessibleName(), QString("Dismiss notification"));
        QVERIFY(dismiss->isVisible());
        QVERIFY(dismiss->geometry().right() > toast->width() / 2);
        QVERIFY(dismiss->geometry().top() < toast->height() / 2);
        QVERIFY(toast->contentsRect().right() < dismiss->geometry().left());
        QTest::mouseClick(dismiss, Qt::LeftButton);
        QTRY_VERIFY(toast->isHidden());
        toast->showToast("Warning",
                         "Suggestions use loaded navigator objects. Expand nodes for "
                         "more names; large catalogs may be limited.",
                         choscordb::ToastVariant::Warning, 0);
        QVERIFY(toast->isVisible());
        QVERIFY(dismiss->isVisible());
        QTest::mouseClick(dismiss, Qt::LeftButton);
        QTRY_VERIFY(toast->isHidden());
    }
}

void PreviewTest::progressToastHasPersistentIndicatorInBothThemes() {
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
        QCOMPARE(progress->geometry().right(), progress->parentWidget()->width() - 17);
        QCOMPARE(progress->geometry().bottom(), progress->parentWidget()->height() - 17);
        QVERIFY(progress->isVisible());
        progress->clearNotice();
        progress->showProgress("Exporting", "Another batch…");
        QTest::qWait(250);
        QVERIFY(progress->isVisible());
        finish->click();
        QTRY_VERIFY(progress->isHidden());
    }
}

void PreviewTest::toastCanAttachAcrossWidgetTrees() {
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

void PreviewTest::windowToastClearsDestroyedModalOwner() {
    QWidget host;
    host.resize(800, 600);
    host.show();
    auto* first = new choscordb::DialogShell(&host);
    first->setAttribute(Qt::WA_DeleteOnClose);
    first->setAppModal();
    first->open();
    auto* toast = choscordb::windowToast(first);
    toast->showToast("Saved", "First modal", choscordb::ToastVariant::Success, 0);
    QCOMPARE(toast->parentWidget(), &host);
    QCOMPARE(toast->property("embeddedPopupOwner").value<QObject*>(), first);
    QPointer<choscordb::DialogShell> destroyed(first);
    first->close();
    QTRY_VERIFY(!destroyed);
    QVERIFY(!toast->property("embeddedPopupOwner").value<QObject*>());
    auto* second = new choscordb::DialogShell(&host);
    second->setAppModal();
    second->open();
    QCOMPARE(choscordb::windowToast(second), toast);
    QCOMPARE(toast->property("embeddedPopupOwner").value<QObject*>(), second);
    QTest::mouseClick(toast->findChild<QToolButton*>("toastDismiss"), Qt::LeftButton);
    QTRY_VERIFY(toast->isHidden());
}

void PreviewTest::toastVariantsShowTitleBodyAndUseConfiguredTimeout() {
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

void PreviewTest::nonmodalDialogSurfaceHasNoOutline() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("nonmodal"));
    window.show();
    auto* host = window.findChild<QWidget*>("previewLight");
    QVERIFY(host);
    auto* open = host->findChild<QPushButton*>("previewOpenDialog");
    auto* dialog = previewSurface<QDialog>(host);
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

void PreviewTest::nonmodalDialogGrowsWhenDescriptionWraps() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("nonmodal"));
    window.show();
    for (const auto* theme : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(theme);
        QVERIFY(host);
        auto* open = host->findChild<QPushButton*>("previewOpenDialog");
        auto* dialog = previewSurface<choscordb::DialogShell>(host);
        QVERIFY(open && dialog);
        open->click();
        QTRY_VERIFY(dialog->isVisible());
        auto* description = host->findChild<choscordb::design::Text*>("previewDialogDescription");
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

void PreviewTest::galleryOpenKeepsOverlayInsideWindow_data() {
    QTest::addColumn<QString>("specimen");
    QTest::addColumn<bool>("dark");
    QTest::newRow("panel-light") << QString("dialogs") << false;
    QTest::newRow("panel-dark") << QString("dialogs") << true;
    QTest::newRow("confirmation-light") << QString("confirmations") << false;
    QTest::newRow("confirmation-dark") << QString("confirmations") << true;
}

void PreviewTest::galleryOpenKeepsOverlayInsideWindow() {
    QFETCH(QString, specimen);
    QFETCH(bool, dark);
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen(specimen));
    window.resize(1280, 900);
    window.show();
    window.activateWindow();
    auto* host = window.findChild<QWidget*>(dark ? "previewDark" : "previewLight");
    QVERIFY(host);
    auto* open = host->findChild<QPushButton*>("previewOpenDialog");
    auto* dialog = previewSurface<QDialog>(host);
    QVERIFY(open && dialog);
    open->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(open->hasFocus());
    QSignalSpy finished(dialog, &QDialog::finished);
    QTest::mouseClick(open, Qt::LeftButton);
    QTRY_VERIFY(dialog->isVisible());
    QCOMPARE(finished.count(), 0); // Opening remains asynchronous.
    QVERIFY(!dialog->isWindow());
    QCOMPARE(dialog->window(), &window);
    QVERIFY(window.rect().contains(QRect(dialog->mapTo(&window, QPoint()), dialog->size())));
    if (QGuiApplication::platformName() == "cocoa") {
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(150);
        const auto position = dialog->mapToGlobal(QPoint());
        const auto capture = dialog->screen()->grabWindow(0, position.x(), position.y(),
                                                          dialog->width(), dialog->height());
        QVERIFY2(!capture.isNull(), "Actual OS-composited corner capture is required on Cocoa.");
        const auto captureDirectory = qEnvironmentVariable("CHOSCORDB_TEST_CAPTURE_DIR");
        if (!captureDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(captureDirectory));
            QVERIFY(capture.save(
                QDir(captureDirectory).filePath(QString("native-gallery-%1.png").arg(specimen))));
        }
        const auto pixel = capture.toImage().pixelColor(qRound(5 * capture.devicePixelRatio()),
                                                        qRound(5 * capture.devicePixelRatio()));
        const auto expected = choscordb::design::resolvedThemeForWidget(*dialog).colors.popover;
        QVERIFY2(qAbs(pixel.red() - expected.red()) < 10 &&
                     qAbs(pixel.green() - expected.green()) < 10 &&
                     qAbs(pixel.blue() - expected.blue()) < 10,
                 qPrintable(pixel.name()));
    }
    QTest::keyClick(dialog, Qt::Key_Escape);
    QTRY_VERIFY(!dialog->isVisible());
    QCOMPARE(finished.count(), 1);
    QTRY_VERIFY(open->hasFocus());
}

void PreviewTest::moreButtonUsesThemeSurface() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("tool-buttons"));
    window.resize(1280, 900);
    window.show();
    QCoreApplication::processEvents();
    for (const auto& [name, expected] : {std::pair{"previewLight", QColor("#ffffff")},
                                         std::pair{"previewDark", QColor("#20272b")}}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* button = host->findChild<QToolButton*>("previewMoreButton");
        QVERIFY(button);
        QVERIFY(button->isVisible());
        QCOMPARE(button->text(), QString("More"));
        const auto snapshot = visibleSurfaceSnapshot(*button);
        QCOMPARE(snapshot.pixelColor(5, button->height() / 2), expected);
    }
}

void PreviewTest::scrollSpecimensKeepTheirIndependentThemePaper() {
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

void PreviewTest::narrowGalleryKeepsNavigationAndActionsReachable() {
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

void PreviewTest::standaloneCapturesRequestedThemeAndViewport_data() {
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

void PreviewTest::standaloneCapturesRequestedThemeAndViewport() {
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
        QVERIFY2(scrollViewport.contains(control), "Export clips a field edge or tab indicator");
    }
}

void PreviewTest::confirmationSpecimenUsesProductionCancellationBoundary() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("confirmations"));
    window.show();
    auto* open = window.findChild<QPushButton*>("previewOpenDialog");
    QVERIFY(open);
    open->click();
    auto* confirmation = qobject_cast<choscordb::ConfirmationDialog*>(
        open->property("previewSurface").value<QObject*>());
    QVERIFY(confirmation);
    QCOMPARE(choscordb::design::DialogPresentation::activeDialog(), confirmation);
    auto* cancel = confirmation->button(QMessageBox::Cancel);
    QVERIFY(cancel);
    QCOMPARE(confirmation->defaultButton(), cancel);
    QTest::keyClick(confirmation, Qt::Key_Escape);
    QTRY_VERIFY(!confirmation->isVisible());
    auto* status = open->parentWidget()->findChild<QLabel*>("previewConfirmationStatus");
    QVERIFY(status);
    QCOMPARE(status->text(), QString("Cancelled."));
}

void PreviewTest::initTestCase() {
    QApplication::setStyle(new choscordb::design::ControlStyle);
}

void PreviewTest::exportedPopupContainsItsVisibleContent_data() {
    QTest::addColumn<QString>("specimen");
    QTest::newRow("modal") << QString("dialogs");
    QTest::newRow("menu") << QString("menus");
    QTest::newRow("tooltip") << QString("tooltip-popover");
    QTest::newRow("selector") << QString("selects");
}

void PreviewTest::exportedPopupContainsItsVisibleContent() {
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
        auto* dialog = previewSurface<QDialog>(light);
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
        auto* menu = previewSurface<QMenu>(light, "previewOpenMenu");
        QVERIFY(menu);
        QVERIFY(menu->isVisible());
        QAction* action = nullptr;
        for (auto* candidate : menu->actions()) {
            if (candidate->text() == "Wrap text")
                action = candidate;
        }
        QVERIFY(action);
        for (int attempt = 0; attempt < 50 && witness.isNull(); ++attempt) {
            QCoreApplication::processEvents();
            const auto menuSnapshot =
                menu->grab().toImage().scaled(menu->size()).convertToFormat(QImage::Format_ARGB32);
            witness = textInkPatch(menuSnapshot.copy(menu->actionGeometry(action)), false, 160);
            if (witness.isNull())
                QTest::qWait(20);
        }
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

void PreviewTest::tooltipUsesTheProductionSurfaceInBothThemes() {
    choscordb::design::PreviewWindow window;
    window.show();
    QVERIFY(window.selectSpecimen("tooltip-popover"));
    QCoreApplication::processEvents();
    auto* light = window.findChild<QWidget*>("previewLight");
    light->findChild<QPushButton*>("previewOpenTooltip")->click();
    auto* tooltip = window.findChild<QWidget*>("designTooltip");
    QVERIFY(tooltip);
    QVERIFY(tooltip->isVisible());
    QVERIFY(!tooltip->isWindow());
    QCOMPARE(tooltip->window(), &window);
    QVERIFY(tooltip->accessibleName().contains("Synthetic help"));
    const auto lightColor = tooltip->grab().toImage().pixelColor(10, 10);
    auto* dark = window.findChild<QWidget*>("previewDark");
    dark->findChild<QPushButton*>("previewOpenTooltip")->click();
    tooltip = window.findChild<QWidget*>("designTooltip");
    QVERIFY(tooltip);
    QVERIFY(tooltip->isVisible());
    QVERIFY(!tooltip->isWindow());
    QCOMPARE(tooltip->window(), &window);
    const auto darkColor = tooltip->grab().toImage().pixelColor(10, 10);
    QVERIFY(lightColor.lightness() < 128);
    QVERIFY(darkColor.lightness() > 128);
    for (auto* host : {light, dark}) {
        auto* items = host->findChild<QListWidget*>("previewTooltipItems");
        QVERIFY(items);
        const auto point = items->visualItemRect(items->item(0)).center();
        QHelpEvent help(QEvent::ToolTip, point, items->viewport()->mapToGlobal(point));
        QApplication::sendEvent(items->viewport(), &help);
        tooltip = window.findChild<QWidget*>("designTooltip");
        QVERIFY(tooltip);
        QVERIFY(tooltip->isVisible());
        QVERIFY(!tooltip->isWindow());
        QCOMPARE(tooltip->window(), &window);
        QCOMPARE(tooltip->accessibleName(), QString("Example Postgres"));
    }
}

void PreviewTest::exportsRenderActualModalAndMenuSurfaces() {
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
    QCOMPARE(QJsonDocument::fromJson(menuMetadata.readAll()).object().value("surface").toString(),
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

void PreviewTest::standaloneExportsWithoutAProfile() {
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

QTEST_MAIN(PreviewTest)

void PreviewTest::contextMenuSpecimenUsesCursorInBothThemes() {
    using namespace choscordb::design;
    PreviewWindow window;
    window.resize(1200, 900);
    window.show();
    QVERIFY(window.selectSpecimen("menus"));
    QVERIFY(QTest::qWaitForWindowActive(&window));
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* content = host->findChild<QWidget*>("previewContent");
        QVERIFY(content);
        const QPoint local(60, 80);
        const QPoint cursor = content->mapToGlobal(local);
        QContextMenuEvent request(QContextMenuEvent::Mouse, local, cursor);
        QApplication::sendEvent(content, &request);
        auto* menu = previewSurface<QMenu>(host, "previewOpenMenu");
        QVERIFY(menu);
        QVERIFY(menu->isVisible());
        QCOMPARE(menu->mapToGlobal(QPoint(detail::menuShadowMargin(), detail::menuShadowMargin())),
                 cursor);
        QTest::mouseClick(&window, Qt::LeftButton, {}, QPoint(5, 5));
        QVERIFY(!menu->isVisible());
    }
}

void PreviewTest::tableHoverPreservesBackgroundInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("tables"));
    window.show();
    QCoreApplication::processEvents();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* table = host->findChild<QTableWidget*>();
        QVERIFY(table);
        table->setMouseTracking(true);
        for (int row = 0; row < 2; ++row) {
            const auto cell = table->visualItemRect(table->item(row, 0));
            const auto before = table->viewport()->grab().toImage();
            QTest::mouseMove(table->viewport(), cell.center());
            QCoreApplication::processEvents();
            const auto after = table->viewport()->grab().toImage();
            const auto sample = cell.topLeft() + QPoint(3, 3);
            QCOMPARE(after.pixelColor(sample), before.pixelColor(sample));
        }
    }
}
