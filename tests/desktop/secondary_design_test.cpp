#include "bridge/engine_adapter.h"
#include "design_system/components.h"
#include "design_system/control_style.h"
#include "design_system/theme_manager.h"
#include "models/history_model.h"
#include "widgets/export_dialog.h"
#include "widgets/history_dock.h"
#include "widgets/profile_dialog.h"
#include "widgets/query_settings_dialog.h"
#include "widgets/search_panel.h"
#include "widgets/sql_editor.h"
#include "widgets/value_detail_dialog.h"
#include <QTableView>
#include <QVBoxLayout>
#include <memory>

#include <QApplication>
#include <QComboBox>
#include <QImage>
#include <QLineEdit>
#include <QSignalSpy>
#include <QtTest>

class SecondaryDesignTest final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QApplication::setStyle(new choscordb::design::ControlStyle); }

    void secondaryDialogsUseSharedActionsAndKeepRejectionSafe_data() {
        QTest::addColumn<QString>("surface");
        QTest::addColumn<QString>("closeName");
        QTest::newRow("export") << QString("export") << QString("exportClose");
        QTest::newRow("value detail") << QString("value") << QString("valueClose");
        QTest::newRow("query settings") << QString("query") << QString("querySettingsCancel");
    }

    void secondaryDialogsUseSharedActionsAndKeepRejectionSafe() {
        QFETCH(QString, surface);
        QFETCH(QString, closeName);
        using namespace choscordb;
        EngineAdapter adapter;
        std::unique_ptr<QDialog> dialog;
        if (surface == "export")
            dialog = std::make_unique<ExportDialog>(&adapter);
        else if (surface == "value")
            dialog = std::make_unique<ValueDetailDialog>(&adapter);
        else
            dialog = std::make_unique<QuerySettingsDialog>(&adapter);
        design::ThemeManager theme;
        theme.applyTo(*dialog);
        QSignalSpy accepted(dialog.get(), &QDialog::accepted);
        QSignalSpy rejected(dialog.get(), &QDialog::rejected);
        dialog->show();
        const auto buttons = dialog->findChildren<QPushButton*>();
        QVERIFY(buttons.size() >= 3);
        for (auto* button : buttons) {
            QVERIFY2(qobject_cast<design::Button*>(button), qPrintable(button->text()));
        }
        auto* close = dialog->findChild<design::Button*>(closeName);
        QVERIFY(close);
        QCOMPARE(close->variant(), design::ButtonVariant::Outline);
        if (surface != "query")
            QVERIFY(!dialog->isModal());
        close->click();
        QCOMPARE(accepted.count(), 0);
        QCOMPARE(rejected.count(), 1);
        QVERIFY(!dialog->isVisible());
    }

    void searchActionsAreGroupedAndRetainFindAndFocusBehavior() {
        using namespace choscordb;
        QWidget window;
        auto* layout = new QVBoxLayout(&window);
        SqlEditor editor(&window);
        editor.setText("SELECT 1;\nSELECT 2;");
        SearchPanel panel([&editor] { return &editor; }, &window);
        layout->addWidget(&panel);
        layout->addWidget(&editor);
        design::ThemeManager theme;
        theme.applyTo(window);
        window.show();
        panel.showFind();
        auto* group = panel.findChild<design::ButtonGroup*>("searchNavigation");
        QVERIFY(group);
        QCOMPARE(group->findChildren<design::Button*>().size(), 2);
        auto* next = panel.findChild<design::Button*>("searchNext");
        auto* close = panel.findChild<design::Button*>("searchClose");
        QVERIFY(next);
        QVERIFY(close);
        QCOMPARE(close->variant(), design::ButtonVariant::Ghost);
        panel.findChild<QLineEdit*>("searchNeedle")->setText("SELECT");
        next->click();
        QTRY_COMPARE(editor.selectedText(), QString("SELECT"));
        close->click();
        QVERIFY(!panel.isVisible());
        QTRY_VERIFY(editor.hasFocus());
    }

    void historyActionsKeepTheSelectedSqlAndSharedHierarchy() {
        using namespace choscordb;
        EngineAdapter adapter;
        QSignalSpy listed(&adapter, &EngineAdapter::historyListed);
        HistoryDock dock(&adapter);
        QSignalSpy opened(&dock, &HistoryDock::openRequested);
        design::ThemeManager theme;
        theme.applyTo(dock);
        dock.show();
        auto* clear = dock.findChild<design::Button*>("clearHistory");
        auto* open = dock.findChild<design::Button*>("openHistoryQuery");
        auto* paging = dock.findChild<design::ButtonGroup*>("historyPaging");
        QVERIFY(clear);
        QVERIFY(open);
        QVERIFY(paging);
        QCOMPARE(clear->variant(), design::ButtonVariant::Destructive);
        QCOMPARE(open->variant(), design::ButtonVariant::Default);
        QCOMPARE(paging->findChildren<design::Button*>().size(), 2);
        QTRY_COMPARE(listed.count(), 1);
        auto* table = dock.findChild<QTableView*>("historyTable");
        auto* model = qobject_cast<HistoryModel*>(table->model());
        QVERIFY(model);
        SavedHistoryEntry entry;
        entry.id = "shared-action";
        entry.sql = "SELECT 'Việt Nam';";
        entry.status = "completed";
        model->setEntries({entry});
        table->selectRow(0);
        QVERIFY(open->isEnabled());
        open->click();
        QCOMPARE(opened.count(), 1);
        QCOMPARE(qvariant_cast<SavedHistoryEntry>(opened.first().first()).sql, entry.sql);
    }

    void postgresProfileFormRetainsTheEstablishedWindowSize() {
        choscordb::EngineAdapter adapter;
        choscordb::ProfileDialog dialog(&adapter);
        choscordb::design::ThemeManager theme;
        theme.setMode(choscordb::design::ThemeMode::Light);
        theme.applyTo(dialog);
        dialog.show();
        auto* driver = dialog.findChild<QComboBox*>("profileDriver");
        QTRY_VERIFY(driver->isEnabled());
        driver->setCurrentIndex(driver->findData("postgres"));
        QCoreApplication::processEvents();
        QCOMPARE(dialog.size(), QSize(780, 540));
        QVERIFY(dialog.findChild<QLineEdit*>("profilePassword")->isVisible());
    }

    void profileActionsUseSharedVariantsAndStillSaveTheDraft() {
        using namespace choscordb;
        EngineAdapter adapter;
        QSignalSpy saved(&adapter, &EngineAdapter::profileSaved);
        ProfileDialog dialog(&adapter);
        design::ThemeManager theme;
        theme.setMode(design::ThemeMode::Light);
        theme.applyTo(dialog);
        dialog.show();
        auto* save = dialog.findChild<design::Button*>("profileSave");
        auto* connect = dialog.findChild<design::Button*>("profileConnect");
        auto* remove = dialog.findChild<design::Button*>("profileDelete");
        QVERIFY(save);
        QVERIFY(connect);
        QVERIFY(remove);
        QCOMPARE(save->variant(), design::ButtonVariant::Secondary);
        QCOMPARE(connect->variant(), design::ButtonVariant::Default);
        QCOMPARE(remove->variant(), design::ButtonVariant::Destructive);
        QTRY_VERIFY(save->isEnabled());
        QImage rendered(connect->size(), QImage::Format_ARGB32_Premultiplied);
        rendered.fill(Qt::transparent);
        connect->render(&rendered);
        QCOMPARE(rendered.pixelColor(rendered.width() / 2, 6), QColor("#171717"));
        QVERIFY(!dialog.isModal());
        dialog.findChild<QLineEdit*>("profileName")->setText("Analysis");
        dialog.findChild<QLineEdit*>("profilePath")->setText(":memory:");
        save->click();
        QTRY_COMPARE(saved.count(), 1);
        QCOMPARE(qvariant_cast<SavedProfile>(saved.first().at(1)).name, QString("Analysis"));
    }
};

QTEST_MAIN(SecondaryDesignTest)
#include "secondary_design_test.moc"
