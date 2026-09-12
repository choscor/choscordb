#include "app/query_settings.h"
#include "widgets/query_settings_dialog.h"
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>
class QuerySettingsControllerTest : public QObject {
    Q_OBJECT
  private slots:
    void initialLoadGatesAndUsesPersistedPreferences() {
        QTemporaryDir directory;
        const auto path = directory.filePath("metadata.sqlite");
        {
            choscordb::EngineAdapter seed(nullptr, path);
            QSignalSpy written(&seed, &choscordb::EngineAdapter::queryPreferencesReady);
            choscordb::QueryPreferences value;
            value.pageSize = 234;
            value.timeoutSeconds = 9;
            QVERIFY(seed.setQueryPreferences(value, 5));
            QTRY_COMPARE(written.count(), 1);
        }
        QWidget window;
        choscordb::EngineAdapter adapter(nullptr, path);
        choscordb::QuerySettingsController controller(&adapter, &window);
        QVERIFY(!controller.isReady());
        QSignalSpy ready(&controller, &choscordb::QuerySettingsController::readyChanged);
        QTRY_VERIFY(controller.isReady());
        QCOMPARE(ready.count(), 1);
        QCOMPARE(controller.preferences().pageSize, quint32(234));
        QCOMPARE(controller.preferences().timeoutSeconds, quint32(9));
        choscordb::QueryPreferences unrelated;
        unrelated.pageSize = 789;
        emit adapter.queryPreferencesReady(5, unrelated);
        QCOMPARE(controller.preferences().pageSize, quint32(234));
    }
    void closedDialogSaveAppliesOnceAndOldResponsesAreIgnored() {
        QWidget window;
        choscordb::EngineAdapter adapter;
        choscordb::QuerySettingsController controller(&adapter, &window);
        QTRY_VERIFY(controller.isReady());
        controller.open();
        auto* dialog = window.findChild<choscordb::QuerySettingsDialog*>("querySettingsDialog");
        QVERIFY(dialog);
        auto* apply = dialog->findChild<QPushButton*>("querySettingsApply");
        QTRY_VERIFY(apply->isEnabled());
        QSignalSpy changed(&controller, &choscordb::QuerySettingsController::preferencesChanged);
        QSignalSpy submitted(dialog,
                             &choscordb::QuerySettingsDialog::queryPreferencesSaveSubmitted);
        dialog->findChild<QSpinBox*>("queryPageSize")->setValue(345);
        apply->click();
        QCOMPARE(submitted.count(), 1);
        const auto token = submitted.first().at(0).toULongLong();
        QPointer<choscordb::QuerySettingsDialog> alive(dialog);
        dialog->close();
        QTRY_VERIFY(!alive);
        QTRY_COMPARE(controller.preferences().pageSize, quint32(345));
        QCOMPARE(changed.count(), 1);
        choscordb::QueryPreferences stale;
        stale.pageSize = 999;
        emit adapter.queryPreferencesReady(token, stale);
        emit adapter.recoveryFailed(token, "late");
        QCOMPARE(controller.preferences().pageSize, quint32(345));
        QCOMPARE(changed.count(), 1);
    }
    void failedInitialLoadKeepsDefaultsAndEndsItsCorrelation() {
        QWidget window;
        choscordb::EngineAdapter adapter;
        adapter.shutdown();
        QSignalSpy errors(&adapter, &choscordb::EngineAdapter::recoveryFailed);
        choscordb::QuerySettingsController controller(&adapter, &window);
        QSignalSpy failed(&controller, &choscordb::QuerySettingsController::failed);
        QTRY_VERIFY(controller.isReady());
        QCOMPARE(failed.count(), 1);
        QVERIFY(!errors.isEmpty());
        const auto token = errors.last().at(0).toULongLong();
        QCOMPARE(controller.preferences().pageSize,
                 choscordb::EngineAdapter::queryPreferenceLimits().defaultPageSize);
        choscordb::QueryPreferences stale;
        stale.pageSize = 999;
        emit adapter.queryPreferencesReady(token, stale);
        emit adapter.recoveryFailed(token, "stale failure");
        QCOMPARE(failed.count(), 1);
        QCOMPARE(controller.preferences().pageSize,
                 choscordb::EngineAdapter::queryPreferenceLimits().defaultPageSize);
    }
};
QTEST_MAIN(QuerySettingsControllerTest)
#include "query_settings_controller_test.moc"
