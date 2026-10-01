#include "design_system/toast_region/toast_region.h"
#include "widgets/preferences_dialog/preferences_dialog.h"
#include "widgets/query_settings_dialog/query_settings_dialog.h"
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QtTest>
class QuerySettingsDialogTest : public QObject {
    Q_OBJECT
  private slots:
    void openQuerySettingsKeepsLaterPreferencesVisibilitySave() {
        using namespace choscordb;
        EngineAdapter adapter;
        QuerySettingsDialog query(&adapter);
        QSignalSpy queryConfirmed(&query, &QuerySettingsDialog::queryPreferencesConfirmed);
        query.show();
        auto* queryApply = query.findChild<QPushButton*>("querySettingsApply");
        QTRY_VERIFY(queryApply->isEnabled());
        QCOMPARE(queryConfirmed.count(), 1);
        PreferencesDialog preferences(&adapter, {});
        preferences.show();
        auto* preferencesApply = preferences.findChild<QPushButton*>("preferencesApply");
        QTRY_VERIFY(preferencesApply->isEnabled());
        preferences.findChild<QCheckBox*>("preferencesShowSystemSchemas")->setChecked(true);
        preferencesApply->click();
        QTRY_VERIFY(!preferences.isVisible());
        query.findChild<QSpinBox*>("queryPageSize")->setValue(456);
        queryApply->click();
        QTRY_COMPARE(queryConfirmed.count(), 2);
        const auto saved = qvariant_cast<QueryPreferences>(queryConfirmed.last().at(0));
        QCOMPARE(saved.pageSize, quint32(456));
        QVERIFY(saved.showSystemSchemas);
    }
    void changingPageSizePreservesSystemSchemaVisibility() {
        using namespace choscordb;
        EngineAdapter adapter;
        QueryPreferences initial;
        initial.showSystemSchemas = true;
        QSignalSpy ready(&adapter, &EngineAdapter::queryPreferencesReady);
        QVERIFY(adapter.setQueryPreferences(initial, 901));
        QTRY_COMPARE(ready.count(), 1);
        QuerySettingsDialog dialog(&adapter);
        QSignalSpy confirmed(&dialog, &QuerySettingsDialog::queryPreferencesConfirmed);
        dialog.show();
        auto* apply = dialog.findChild<QPushButton*>("querySettingsApply");
        QTRY_VERIFY(apply->isEnabled());
        QCOMPARE(confirmed.count(), 1);
        dialog.findChild<QSpinBox*>("queryPageSize")->setValue(456);
        apply->click();
        QTRY_COMPARE(confirmed.count(), 2);
        const auto saved = qvariant_cast<QueryPreferences>(confirmed.last().at(0));
        QCOMPARE(saved.pageSize, quint32(456));
        QVERIFY(saved.showSystemSchemas);
    }
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
        auto* status = dialog.findChild<QLabel*>("querySettingsStatus");
        QVERIFY(status);
        QVERIFY(!status->text().isEmpty());
        QVERIFY(status->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
        QVERIFY(dialog.findChild<choscordb::ToastRegion*>("toastRegion") == nullptr);
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
