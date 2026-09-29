#include "design_system/toast_region/toast_region.h"
#include "preview_test.h"
#include "tools/preview/preview_window.h"

#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <QtTest>

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
    QVERIFY(toast.isVisible());
    QVERIFY(toast.accessibleDescription().contains("invalid command input"));
    toast.showToast("Synced", "Latest pending notice", choscordb::ToastVariant::Success, 0);
    QTest::qWait(220);
    QVERIFY(toast.accessibleDescription().contains("invalid command input"));
    toast.clearPinnedToast();
    QTRY_VERIFY(toast.accessibleDescription().contains("Latest pending notice"));
    QCOMPARE(toast.property("variant").toString(), QString("success"));
    QVERIFY(toast.isVisible());
    toast.showPinnedToast("Could not restore workspace", "still unresolved",
                          choscordb::ToastVariant::Danger);
    toast.clearNotice();
    toast.clearPinnedToast();
    QTRY_VERIFY(toast.isHidden());

    toast.showToast("Saved", "Timed notice", choscordb::ToastVariant::Success, 100);
    toast.showPinnedToast("Could not restore workspace", "try again",
                          choscordb::ToastVariant::Danger);
    QTest::qWait(150);
    QVERIFY(toast.text().contains("Could not restore workspace"));
    toast.clearPinnedToast();
    QVERIFY(toast.isVisible());
    QVERIFY(toast.text().contains("Timed notice"));
    QTRY_VERIFY_WITH_TIMEOUT(toast.isHidden(), 1000);
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
    auto* dismiss = toast.findChild<QToolButton*>("toastDismiss");
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
    }
}
