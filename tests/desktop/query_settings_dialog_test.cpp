#include "widgets/query_settings_dialog/query_settings_dialog.h"
#include "design_system/toast_region/toast_region.h"
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QtTest>
class QuerySettingsDialogTest : public QObject {
    Q_OBJECT
  private slots:
    void submittedSaveSurvivesDialogClosing() {
        choscordb::EngineAdapter adapter;
        QSignalSpy ready(&adapter, &choscordb::EngineAdapter::queryPreferencesReady);
        auto* dialog = new choscordb::QuerySettingsDialog(&adapter);
        QPointer<choscordb::QuerySettingsDialog> alive(dialog);
        dialog->show();
        auto* apply = dialog->findChild<QPushButton*>("querySettingsApply");
        QTRY_VERIFY(apply->isEnabled());
        dialog->findChild<QSpinBox*>("queryPageSize")->setValue(456);
        quint64 submitted = 0;
        connect(dialog, &choscordb::QuerySettingsDialog::queryPreferencesSaveSubmitted, &adapter,
                [dialog, &submitted](quint64 token) {
                    submitted = token;
                    dialog->close();
                    dialog->deleteLater();
                });
        apply->click();
        QVERIFY(submitted != 0);
        QTRY_VERIFY(!alive);
        QTRY_COMPARE(ready.count(), 2);
        QCOMPARE(ready.last().at(0).toULongLong(), submitted);
        QCOMPARE(qvariant_cast<choscordb::QueryPreferences>(ready.last().at(1)).pageSize,
                 quint32(456));
    }
    void saveCorrelatesBeforeSubmissionAndPreservesFailedDraft() {
        choscordb::EngineAdapter adapter;
        choscordb::QuerySettingsDialog dialog(&adapter);
        QSignalSpy submitted(&dialog,
                             &choscordb::QuerySettingsDialog::queryPreferencesSaveSubmitted);
        QSignalSpy confirmed(&dialog, &choscordb::QuerySettingsDialog::queryPreferencesConfirmed);
        dialog.show();
        auto* apply = dialog.findChild<QPushButton*>("querySettingsApply");
        QTRY_VERIFY(apply->isEnabled());
        QCOMPARE(confirmed.count(), 1);
        auto* size = dialog.findChild<QSpinBox*>("queryPageSize");
        auto* timeout = dialog.findChild<QSpinBox*>("queryTimeoutSeconds");
        QCOMPARE(size->value(),
                 int(choscordb::EngineAdapter::queryPreferenceLimits().defaultPageSize));
        size->setValue(321);
        timeout->setValue(7);
        apply->click();
        QCOMPARE(submitted.count(), 1);
        QTRY_COMPARE(confirmed.count(), 2);
        QCOMPARE(qvariant_cast<choscordb::QueryPreferences>(confirmed.last().at(0)).pageSize,
                 quint32(321));
        adapter.shutdown();
        connect(&adapter, &choscordb::EngineAdapter::recoveryFailed, &dialog,
                [&](quint64 token, const QString&) {
                    QCOMPARE(submitted.count(), 2);
                    QCOMPARE(token, submitted.last().at(0).toULongLong());
                });
        size->setValue(432);
        apply->click();
        QTRY_VERIFY(apply->isEnabled());
        QCOMPARE(submitted.count(), 2);
        QCOMPARE(confirmed.count(), 2);
        QCOMPARE(size->value(), 432);
        auto* toast = dialog.findChild<choscordb::ToastRegion*>("toastRegion");
        QVERIFY(toast);
        QTRY_VERIFY(toast->isVisible());
        QCOMPARE(toast->property("variant").toString(), QString("danger"));
        QVERIFY(!toast->accessibleDescription().isEmpty());
        QVERIFY(dialog.findChild<QLabel*>("querySettingsStatus") == nullptr);
        emit adapter.queryPreferencesReady(submitted.first().at(0).toULongLong(),
                                           choscordb::QueryPreferences{});
        QCOMPARE(size->value(), 432);
        QCOMPARE(confirmed.count(), 2);
    }
    void failedLoadRequiresResetAndCancelDoesNotSubmit() {
        choscordb::EngineAdapter adapter;
        adapter.shutdown();
        choscordb::QuerySettingsDialog dialog(&adapter);
        QSignalSpy submitted(&dialog,
                             &choscordb::QuerySettingsDialog::queryPreferencesSaveSubmitted);
        auto* reset = dialog.findChild<QPushButton*>("querySettingsReset");
        auto* apply = dialog.findChild<QPushButton*>("querySettingsApply");
        QTRY_VERIFY(reset->isEnabled());
        QVERIFY(!apply->isEnabled());
        reset->click();
        QVERIFY(apply->isEnabled());
        QCOMPARE(dialog.findChild<QSpinBox*>("queryPageSize")->value(),
                 int(choscordb::EngineAdapter::queryPreferenceLimits().defaultPageSize));
        dialog.reject();
        QCOMPARE(submitted.count(), 0);
    }
};
QTEST_MAIN(QuerySettingsDialogTest)
#include "query_settings_dialog_test.moc"
