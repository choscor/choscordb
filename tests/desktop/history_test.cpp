#include "bridge/engine_adapter.h"
#include "models/history_model.h"
#include "widgets/history_dock.h"
#include <QCheckBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QtTest>
class HistoryTest : public QObject {
    Q_OBJECT
  private slots:
    void byteShortPagesAdvanceByReturnedRowsAndReturnToVisitedOffsets() {
        choscordb::EngineAdapter adapter;
        QSignalSpy listed(&adapter, &choscordb::EngineAdapter::historyListed);
        choscordb::HistoryDock dock(&adapter);
        auto* table = dock.findChild<QTableView*>("historyTable");
        auto* model = qobject_cast<choscordb::HistoryModel*>(table->model());
        auto* next = dock.findChild<QPushButton*>("historyNext");
        auto* previous = dock.findChild<QPushButton*>("historyPrevious");
        auto* range = dock.findChild<QLabel*>("historyRange");
        QTRY_VERIFY(dock.findChild<QCheckBox*>("recordHistory")->isEnabled());
        QTRY_COMPARE(listed.count(), 1);
        choscordb::SavedHistoryEntry entry;
        entry.id = "one";
        entry.sql = "SELECT 1";
        model->setEntries({entry, entry, entry});
        table->selectRow(0);
        QVERIFY(next->isEnabled());
        next->click();
        QTRY_COMPARE(range->text(), QString("After row 3"));
        model->setEntries({entry});
        table->selectRow(0);
        QVERIFY(next->isEnabled());
        next->click();
        QTRY_COMPARE(range->text(), QString("After row 4"));
        QVERIFY(previous->isEnabled());
        previous->click();
        QTRY_COMPARE(range->text(), QString("After row 3"));
        previous->click();
        QTRY_COMPARE(range->text(), QString("No rows"));
        QVERIFY(!previous->isEnabled());
    }
    void modelKeepsFullSqlAndDistinguishesUnknownRows() {
        choscordb::HistoryModel model;
        choscordb::SavedHistoryEntry entry;
        entry.id = "one";
        entry.sql = QString(70000, QChar('x'));
        entry.status = "completed";
        model.setEntries({entry});
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0, 1)).toString(), QString("Unsaved connection"));
        QCOMPARE(model.data(model.index(0, 5)).toString(), QString::fromUtf8("—"));
        QVERIFY(model.data(model.index(0, 2)).toString().size() < 300);
        QCOMPARE(model.entry(0)->sql, entry.sql);
        entry.profileId = "missing-profile";
        entry.hasRowCount = true;
        model.setEntries({entry});
        QCOMPARE(model.data(model.index(0, 1)).toString(), entry.profileId);
        QCOMPARE(model.data(model.index(0, 5)).toString(), QString("0"));
        model.setProfileNames({{"missing-profile", "Analytics"}});
        QCOMPARE(model.data(model.index(0, 1)).toString(), QString("Analytics"));
    }
    void dockLoadsEmptyPolicyAndOpensFullSqlWithoutExecution() {
        choscordb::EngineAdapter adapter;
        QSignalSpy policies(&adapter, &choscordb::EngineAdapter::historyPolicyReady);
        choscordb::HistoryDock dock(&adapter);
        dock.show();
        QTRY_VERIFY(!policies.isEmpty());
        auto* record = dock.findChild<QCheckBox*>("recordHistory");
        QTRY_VERIFY(record->isEnabled());
        QVERIFY(record->isChecked());
        auto* table = dock.findChild<QTableView*>("historyTable");
        auto* model = qobject_cast<choscordb::HistoryModel*>(table->model());
        choscordb::SavedHistoryEntry entry;
        entry.id = "one";
        entry.sql = QString(70000, QChar('x'));
        entry.profileId = "profile";
        model->setEntries({entry});
        table->selectRow(0);
        auto* preview = dock.findChild<QPlainTextEdit*>("historyPreview");
        QVERIFY(preview->toPlainText().size() <= 65536);
        QVERIFY(dock.findChild<QLabel*>("historyPreviewNotice")->text().contains("truncated"));
        QSignalSpy opened(&dock, &choscordb::HistoryDock::openRequested);
        dock.findChild<QPushButton*>("openHistoryQuery")->click();
        QCOMPARE(opened.count(), 1);
        QCOMPARE(qvariant_cast<choscordb::SavedHistoryEntry>(opened.at(0).at(0)).sql, entry.sql);
        record->click();
        QTRY_VERIFY(record->isEnabled());
        QVERIFY(!record->isChecked());
        // Old policy deliveries and unrelated failures cannot overwrite current state.
        const auto oldToken = policies.at(0).at(0).toULongLong();
        emit adapter.historyPolicyReady(oldToken, choscordb::HistoryPolicy{});
        QVERIFY(!record->isChecked());
        auto* status = dock.findChild<QLabel*>("historyStatus");
        const auto previousStatus = status->text();
        emit adapter.recoveryFailed(oldToken, "stale failure");
        QCOMPARE(status->text(), previousStatus);
        emit adapter.historyListed(oldToken, {});
        QCOMPARE(model->rowCount(), 1);
        entry.sql = QString(65535, QChar('x')) + QString::fromUtf8("😀") + "tail";
        model->setEntries({entry});
        table->selectRow(0);
        QCOMPARE(preview->toPlainText().size(), 65535);
        QVERIFY(!preview->toPlainText().back().isHighSurrogate());
        auto* later = dock.findChild<QPushButton*>("historyPreviewNext");
        auto* earlier = dock.findChild<QPushButton*>("historyPreviewPrevious");
        later->click();
        QCOMPARE(preview->toPlainText(), QString::fromUtf8("😀tail"));
        QVERIFY(!later->isEnabled());
        QVERIFY(earlier->isEnabled());
        earlier->click();
        QCOMPARE(preview->toPlainText(), QString(65535, QChar('x')));
        QVERIFY(!earlier->isEnabled());
        dock.findChild<QPushButton*>("openHistoryQuery")->click();
        QCOMPARE(qvariant_cast<choscordb::SavedHistoryEntry>(opened.last().at(0)).sql, entry.sql);
        later->click();
        entry.sql = "SELECT 'fresh selection'";
        model->setEntries({entry});
        table->selectRow(0);
        QCOMPARE(preview->toPlainText(), entry.sql);
        QVERIFY(!earlier->isEnabled());
        QVERIFY(!later->isEnabled());
        adapter.shutdown();
        dock.refresh();
        QTRY_VERIFY(!status->text().isEmpty());
        QVERIFY(!status->text().contains("Loading"));
        QCOMPARE(model->rowCount(), 1);
        QVERIFY(!record->isChecked());
    }
};
QTEST_MAIN(HistoryTest)
#include "history_test.moc"
