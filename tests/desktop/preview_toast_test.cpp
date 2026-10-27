#include "design_system/button/button.h"
#include "design_system/metrics/metrics.h"
#include "design_system/status_line/status_line.h"
#include "design_system/toast_region/toast_region.h"
#include "dialog_sections_checks.h"
#include "preview_test.h"
#include "tools/preview/preview_window.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QEnterEvent>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest>

void PreviewTest::toastCardUsesTokenInsetsWidthAndSharedDismissButton() {
    using namespace choscordb::design;
    QWidget host;
    host.resize(800, 480);
    host.show();
    choscordb::ToastRegion toast(&host);
    toast.attachTo(&host);
    toast.showToast("Connection test succeeded.", "Example Postgres answered.",
                    choscordb::ToastVariant::Success, 0);
    QTRY_VERIFY(toast.isVisible());
    QCOMPARE(toast.width(), dimension(Dimension::ToastWidth));
    auto* dismiss = toast.findChild<Button*>("toastDismiss");
    QVERIFY(dismiss);
    QCOMPARE(dismiss->variant(), ButtonVariant::Ghost);
    QCOMPARE(dismiss->buttonSize(), ButtonSize::IconSmall);
    QCOMPARE(dismiss->accessibleName(), QString("Dismiss notification"));
    QCOMPARE(dismiss->toolTip(), QString("Dismiss notification"));
    auto* icon = toast.findChild<QLabel*>("toastIcon");
    QVERIFY(icon);
    const int inset = spacing(Spacing::Three);
    QCOMPARE(icon->geometry().left(), inset);
    QCOMPARE(dismiss->geometry().top(), inset);
    QCOMPARE(toast.width() - 1 - dismiss->geometry().right(), inset);
    // The icon and the X sit on the title line.
    QCOMPARE(icon->geometry().center().y(), dismiss->geometry().center().y());
    QVERIFY(toast.contentsRect().top() >= inset);
    QVERIFY(toast.height() - toast.contentsRect().bottom() - 1 >= inset);
    QTest::mouseClick(dismiss, Qt::LeftButton);
    QTRY_VERIFY(toast.isHidden());
}

void PreviewTest::replacingToastShowsLatestNoticeAndCountWithoutQueueing() {
    using choscordb::ToastVariant;
    QWidget host;
    host.resize(480, 320);
    host.show();
    choscordb::ToastRegion toast(&host);
    toast.attachTo(&host);
    toast.showReplacingToast("Warning", "First background failure.", ToastVariant::Warning);
    QTRY_VERIFY(toast.isVisible());
    QVERIFY(toast.accessibleDescription().contains("First background failure."));
    toast.showReplacingToast("Error", "Second background failure.", ToastVariant::Danger);
    QVERIFY(toast.accessibleDescription().contains("Second background failure."));
    QVERIFY(!toast.accessibleDescription().contains("First background failure."));
    QVERIFY(toast.accessibleDescription().contains("2 notices"));
    QCOMPARE(toast.property("variant").toString(), QString("danger"));
    // A resolved message leaves the notice and its count; the notice hides with the last one.
    toast.clearReplacingToast("First background failure.");
    QVERIFY(toast.isVisible());
    QVERIFY(toast.accessibleDescription().contains("Second background failure."));
    QVERIFY(!toast.accessibleDescription().contains("notices"));
    toast.showReplacingToast("Warning", "Third background failure.", ToastVariant::Warning);
    toast.clearReplacingToast("Third background failure.");
    QVERIFY(toast.accessibleDescription().contains("Second background failure."));
    toast.clearReplacingToast("Second background failure.");
    QTRY_VERIFY(toast.isHidden());
    // A completion notice in front keeps one replacing notice in the queue.
    toast.showToast("Saved", "The file was saved.", ToastVariant::Success, 0);
    toast.showReplacingToast("Warning", "Queued one.", ToastVariant::Warning);
    toast.showReplacingToast("Warning", "Queued two.", ToastVariant::Warning);
    QVERIFY(toast.accessibleDescription().contains("The file was saved."));
    QTest::mouseClick(toast.findChild<QAbstractButton*>("toastDismiss"), Qt::LeftButton);
    QTRY_VERIFY(toast.accessibleDescription().contains("Queued two."));
    QVERIFY(toast.accessibleDescription().contains("2 notices"));
    // A later ordinary notice shows in front; the replacing notice waits, then returns.
    toast.showToast("Exported", "The export finished.", ToastVariant::Success, 0);
    QVERIFY(toast.accessibleDescription().contains("The export finished."));
    QTest::mouseClick(toast.findChild<QAbstractButton*>("toastDismiss"), Qt::LeftButton);
    QTRY_VERIFY(toast.accessibleDescription().contains("Queued two."));
    QTest::mouseClick(toast.findChild<QAbstractButton*>("toastDismiss"), Qt::LeftButton);
    QTRY_VERIFY(toast.isHidden());
    // A dismissed replacing notice starts its count again.
    toast.showReplacingToast("Warning", "Fresh notice.", ToastVariant::Warning);
    QTRY_VERIFY(toast.isVisible());
    QVERIFY(!toast.accessibleDescription().contains("notices"));
}

void PreviewTest::pinnedToastKeepsPriorityAndRestoresLatestNotice() {
    QWidget host;
    host.resize(480, 320);
    host.show();
    choscordb::ToastRegion toast(&host);
    toast.attachTo(&host);
    toast.showToast("Saved", "Earlier notice", choscordb::ToastVariant::Success, 0);
    toast.showPinnedToast("Could not restore workspace", "invalid command input",
                          choscordb::ToastVariant::Danger);
    QCOMPARE(toast.property("variant").toString(), QString("danger"));
    QVERIFY(toast.accessibleDescription().contains("invalid command input"));
    toast.showToast("Background warning", "First pending notice", choscordb::ToastVariant::Warning,
                    0);
    QVERIFY(toast.text().contains("Could not restore workspace"));
    toast.showProgress("Loading", "Latest pending notice");
    QVERIFY(toast.text().contains("Could not restore workspace"));
    toast.clearNotice();
    QVERIFY(toast.text().contains("Could not restore workspace"));
    toast.showToast("Synced", "Latest pending notice", choscordb::ToastVariant::Success, 0);
    toast.clearPinnedToast();
    QVERIFY(toast.text().contains("Earlier notice"));
    auto* dismiss = toast.findChild<QAbstractButton*>("toastDismiss");
    QVERIFY(dismiss);
    QTest::mouseClick(dismiss, Qt::LeftButton);
    QVERIFY(toast.text().contains("First pending notice"));
    QTest::mouseClick(dismiss, Qt::LeftButton);
    QCOMPARE(toast.property("variant").toString(), QString("progress"));
    QTest::mouseClick(dismiss, Qt::LeftButton);
    QCOMPARE(toast.property("variant").toString(), QString("success"));
    QVERIFY(toast.text().contains("Latest pending notice"));
    QTest::mouseClick(dismiss, Qt::LeftButton);
    QTRY_VERIFY(toast.isHidden());
    toast.showToast("Saved", "Timed notice", choscordb::ToastVariant::Success, 100);
    toast.showPinnedToast("Could not restore workspace", "try again",
                          choscordb::ToastVariant::Danger);
    toast.clearPinnedToast();
    QVERIFY(toast.text().contains("Timed notice"));
    auto* timer = toast.findChild<QTimer*>();
    QVERIFY(timer->interval() >= 10000);
    QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
    QTRY_VERIFY(toast.isHidden());
}

void PreviewTest::dismissingPinnedToastPreservesLaterOrdinaryNotice() {
    QWidget host;
    host.resize(480, 320);
    host.show();
    choscordb::ToastRegion toast(&host);
    toast.attachTo(&host);
    toast.showPinnedToast("Could not save workspace", "permission denied",
                          choscordb::ToastVariant::Danger);
    toast.showToast("Saved query", "Query export complete", choscordb::ToastVariant::Success, 0);
    auto* dismiss = toast.findChild<QAbstractButton*>("toastDismiss");
    QVERIFY(dismiss);
    QTest::mouseClick(dismiss, Qt::LeftButton);
    QTRY_VERIFY(toast.isVisible() && toast.text().contains("Query export complete"));
    toast.clearPinnedToast();
    QVERIFY(toast.isVisible());
    QVERIFY(toast.accessibleDescription().contains("Query export complete"));
    toast.showPinnedToast("Could not close workspace", "disk full",
                          choscordb::ToastVariant::Danger);
    QVERIFY(toast.accessibleDescription().contains("disk full"));
}

void PreviewTest::pinnedToastSpecimenIsPresentInBothThemes() {
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("feedback"));
    window.show();
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* toast = host->findChild<choscordb::ToastRegion*>("toastRegion");
        auto* showPin = host->findChild<QPushButton*>("previewToast_pinned");
        auto* resolvePin = host->findChild<QPushButton*>("previewToast_resolvePinned");
        QVERIFY(toast && showPin && resolvePin);
        showPin->click();
        QTRY_VERIFY(toast->isVisible());
        QCOMPARE(toast->property("variant").toString(), QString("danger"));
        QVERIFY(toast->text().contains("Workspace recovery failed"));
        QVERIFY(toast->text().contains("invalid command input"));
        QVERIFY(!toast->findChild<QTimer*>()->isActive());
        toast->showToast("Saved", "Queued while recovery is unresolved",
                         choscordb::ToastVariant::Success, 0);
        QVERIFY(toast->text().contains("Workspace recovery failed"));
        resolvePin->click();
        QVERIFY(toast->text().contains("Queued while recovery is unresolved"));
        toast->clearNotice();
        auto* replacing = host->findChild<QPushButton*>("previewToast_replacing");
        QVERIFY(replacing);
        replacing->click();
        replacing->click();
        QTRY_VERIFY(toast->isVisible());
        QVERIFY(toast->text().contains("Pin rebind failed"));
        QVERIFY(toast->text().contains("2 notices"));
        toast->clearNotice();
        QTRY_VERIFY(toast->isHidden());
    }
}

void PreviewTest::ordinaryToastsWaitForReadingAndKeepEveryNotice() {
    QWidget host;
    host.resize(480, 320);
    host.show();
    choscordb::ToastRegion toast(&host);
    toast.attachTo(&host);
    toast.showToast("Saved", "First notice", choscordb::ToastVariant::Success, 100);
    auto* timer = toast.findChild<QTimer*>();
    QVERIFY(timer);
    QVERIFY(timer->interval() >= 10000);
    toast.showToast("Error", "Second notice", choscordb::ToastVariant::Danger);
    toast.showToast("Saved", "Third notice", choscordb::ToastVariant::Success);
    QVERIFY(toast.text().contains("First notice"));
    auto* dismiss = toast.findChild<QAbstractButton*>("toastDismiss");
    QVERIFY(dismiss);
    QTest::mouseClick(dismiss, Qt::LeftButton);
    QVERIFY(toast.text().contains("Second notice"));
    QVERIFY(!timer->isActive());
    QTest::mouseClick(dismiss, Qt::LeftButton);
    QVERIFY(toast.text().contains("Third notice"));
    QVERIFY(timer->interval() >= 10000);
    QTest::mouseClick(dismiss, Qt::LeftButton);
    QTRY_VERIFY(toast.isHidden());
}

void PreviewTest::hiddenHostPreservesUnreadToasts() {
    QWidget host;
    host.resize(480, 320);
    choscordb::ToastRegion toast(&host);
    toast.attachTo(&host);
    toast.showToast("Saved", "First hidden notice", choscordb::ToastVariant::Success);
    toast.showToast("Saved", "Second hidden notice", choscordb::ToastVariant::Success);
    QVERIFY(toast.text().contains("First hidden notice"));
    QVERIFY(!toast.findChild<QTimer*>()->isActive());
    host.show();
    QTRY_VERIFY(toast.isVisible());
    toast.findChild<QAbstractButton*>("toastDismiss")->click();
    QVERIFY(toast.text().contains("Second hidden notice"));
}

void PreviewTest::toastReadingPausesOnHoverAndFocus() {
    QWidget host;
    host.resize(480, 320);
    host.show();
    host.activateWindow();
    choscordb::ToastRegion toast(&host);
    toast.attachTo(&host);
    toast.showToast("Saved", QString(400, 'x'), choscordb::ToastVariant::Success);
    auto* timer = toast.findChild<QTimer*>();
    QVERIFY(timer);
    QVERIFY(timer->interval() >= 26000);
    QEnterEvent enter{QPointF(), QPointF(), QPointF()};
    QApplication::sendEvent(&toast, &enter);
    QVERIFY(!timer->isActive());
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&toast, &leave);
    QTRY_VERIFY(timer->isActive());
    auto* dismiss = toast.findChild<QAbstractButton*>("toastDismiss");
    dismiss->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(dismiss->hasFocus());
    QTRY_VERIFY(!timer->isActive());
    dismiss->clearFocus();
    QTRY_VERIFY(timer->isActive());
    QVERIFY(timer->remainingTime() > 10000);
}

void PreviewTest::longToastKeepsFullDetailsInsideReadableSurface() {
    QWidget host;
    host.resize(480, 320);
    host.show();
    choscordb::ToastRegion toast(&host);
    toast.attachTo(&host);
    const QString body = QString("backend detail ").repeated(300) + "FINAL DETAIL";
    toast.showToast("Could not save workspace", body, choscordb::ToastVariant::Danger);
    QVERIFY(host.rect().contains(toast.geometry()));
    auto* details = toast.findChild<QToolButton*>("toastDetails");
    QVERIFY(details && details->isVisible());
    details->click();
    auto* dialog = host.findChild<QDialog*>("toastDetailsDialog");
    QVERIFY(dialog && dialog->isVisible());
    auto* text = dialog->findChild<QPlainTextEdit*>();
    QVERIFY(text && text->isReadOnly());
    QVERIFY(text->toPlainText().contains(body));
    QCOMPARE(headerHeadline(*dialog), QString("Notification details"));
    QCOMPARE(sectionedSpacing(*dialog), compactSectionedSpacing());
    QCOMPARE(footerOverrides(*dialog), QString());
    QVERIFY(dialog->findChild<QPushButton*>("toastDetailsClose"));
    auto* dismiss = headerDismiss(*dialog, "toastDetailsDismiss");
    QVERIFY(dismiss);
    dismiss->click();
    QVERIFY(!dialog->isVisible());
    QVERIFY(toast.isVisible());
    QVERIFY(toast.accessibleDescription().contains("FINAL DETAIL"));
}

void PreviewTest::statusLineLongErrorKeepsControlsAndFullDetails() {
    QWidget host;
    host.resize(480, 320);
    auto* layout = new QVBoxLayout(&host);
    auto* line = new choscordb::design::StatusLine(&host);
    line->setFixedWidth(400);
    layout->addWidget(line);
    auto* retry = new QPushButton("Retry", &host);
    layout->addWidget(retry);
    const QString message = QString("storage failure detail ").repeated(300) + "FINAL DETAIL";
    line->setMessage(message);
    host.show();
    QCoreApplication::processEvents();
    QVERIFY(line->height() <= 160);
    QVERIFY(retry->isVisible());
    auto* details = line->findChild<QToolButton*>("statusDetails");
    QVERIFY(details && details->isVisible());
    details->click();
    auto* dialog = host.findChild<QDialog*>("statusDetailsDialog");
    QVERIFY(dialog && dialog->isVisible());
    auto* text = dialog->findChild<QPlainTextEdit*>();
    QVERIFY(text && text->isReadOnly());
    QCOMPARE(text->toPlainText(), message);
    QCOMPARE(line->accessibleDescription(), message);
    QCOMPARE(headerHeadline(*dialog), QString("Status details"));
    QCOMPARE(sectionedSpacing(*dialog), compactSectionedSpacing());
    QCOMPARE(footerOverrides(*dialog), QString());
    QVERIFY(dialog->findChild<QPushButton*>("statusDetailsClose"));
    auto* dismiss = headerDismiss(*dialog, "statusDetailsDismiss");
    QVERIFY(dismiss);
    dismiss->click();
    QVERIFY(!dialog->isVisible());
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
        auto* timer = toast->findChild<QTimer*>();
        if (QString(variant) == "success")
            QVERIFY(timer->interval() >= 10000);
        else
            QVERIFY(!timer->isActive());
        QTest::mouseClick(toast->findChild<QAbstractButton*>("toastDismiss"), Qt::LeftButton);
        QTRY_VERIFY(toast->isHidden());
    }
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
