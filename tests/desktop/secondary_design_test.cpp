#include "bridge/engine_adapter.h"
#include "design_system/button/button.h"
#include "design_system/button_group/button_group.h"
#include "design_system/control_style.h"
#include "design_system/theme_manager.h"
#include "models/history_model.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/history_dock/history_dock.h"
#include "widgets/preferences_dialog/preferences_dialog.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/query_settings_dialog/query_settings_dialog.h"
#include "widgets/search_panel/search_panel.h"
#include "widgets/sql_editor/sql_editor.h"
#include "widgets/value_detail_dialog/value_detail_dialog.h"
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <memory>

#include <QApplication>
#include <QComboBox>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
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
        QCOMPARE(dialog->isModal(), surface == "export");
        close->click();
        QCOMPARE(accepted.count(), 0);
        QCOMPARE(rejected.count(), 1);
        QVERIFY(!dialog->isVisible());
    }

    void modalPanelsKeepReferenceChromeAndRightAlignedActions() {
        using namespace choscordb;
        EngineAdapter adapter;
        PreferencesDialog preferences(&adapter, {});
        ExportDialog exportDialog(&adapter);
        design::ThemeManager theme;
        for (auto* dialog : QList<QDialog*>{&preferences, &exportDialog}) {
            theme.applyTo(*dialog);
            dialog->show();
            QTest::qWait(20);
            const auto prefix = dialog == &preferences ? QString("preferences") : QString("export");
            auto* dismiss = dialog->findChild<design::Button*>(prefix + "Dismiss");
            QVERIFY(dismiss);
            QVERIFY(!dismiss->accessibleName().isEmpty());
            auto* close = dialog->findChild<QPushButton*>(prefix + "Close");
            auto* primary = dialog->findChild<QPushButton*>(
                prefix + (dialog == &preferences ? "Apply" : "Start"));
            QVERIFY(close);
            QVERIFY(primary);
            QVERIFY(close->mapTo(dialog, QPoint{}).x() < primary->mapTo(dialog, QPoint{}).x());
            QVERIFY(primary->mapTo(dialog, QPoint{}).x() > dialog->width() / 2);
            QSignalSpy rejected(dialog, &QDialog::rejected);
            dismiss->click();
            QCOMPARE(rejected.count(), 1);
            QVERIFY(!dialog->isVisible());
        }
        preferences.show();
        theme.setMode(design::ThemeMode::Light);
        theme.applyTo(preferences);
        QTest::qWait(20);
        const auto footerCapture = preferences.grab();
        QCOMPARE(footerCapture.toImage().pixelColor(
                     QPoint(preferences.width() / 2, preferences.height() - 5) *
                     footerCapture.devicePixelRatio()),
                 QColor("#f2f5f4"));
        QCOMPARE(preferences.height(), 412);
        auto* tabs = preferences.findChild<QTabWidget*>("preferencesSections");
        QCOMPARE(tabs->mapTo(&preferences, QPoint{}).x(), 0);
        QCOMPARE(tabs->width(), preferences.width());
        for (const auto mode : {design::ThemeMode::Light, design::ThemeMode::Dark}) {
            theme.setMode(mode);
            theme.applyTo(preferences);
            QTest::qWait(20);
            const auto capture = preferences.grab();
            const auto point =
                tabs->mapTo(&preferences, QPoint(tabs->width() / 2, tabs->height() - 20));
            QCOMPARE(capture.toImage().pixelColor(point * capture.devicePixelRatio()),
                     mode == design::ThemeMode::Light ? QColor("#ffffff") : QColor("#20272b"));
        }
        preferences.close();
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

    void postgresProfileFormUsesCompactSingleFormAndKeepsActionsReachable() {
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
        QCOMPARE(dialog.width(), 600);
        QVERIFY(!dialog.findChild<QListWidget*>("profileList")->isVisible());
        auto* save = dialog.findChild<QPushButton*>("profileSaveConnect");
        QVERIFY(save->isVisible());
        QVERIFY(dialog.rect().contains(QRect(save->mapTo(&dialog, QPoint()), save->size())));
        QVERIFY(dialog.findChild<QLineEdit*>("profilePassword")->isVisible());
    }

    void closingDuringInitialProfileLoadKeepsTheServiceUsable() {
        using namespace choscordb;
        EngineAdapter adapter;
        QSignalSpy listed(&adapter, &EngineAdapter::profilesReady);
        {
            ProfileDialog dialog(&adapter);
            dialog.show();
            dialog.close();
            QVERIFY(!dialog.isVisible());
        }
        QTRY_VERIFY(!listed.isEmpty());
        ProfileDialog next(&adapter);
        next.show();
        QTRY_VERIFY(next.findChild<QPushButton*>("profileTest")->isEnabled());
        QTest::mouseClick(next.findChild<QPushButton*>("profileDriverPostgres"), Qt::LeftButton);
        QVERIFY(next.findChild<QLineEdit*>("profileHost")->isVisible());
        next.close();
        QVERIFY(!next.isVisible());
    }

    void connectionFormSurfaceTracksLiveTheme() {
        using namespace choscordb;
        EngineAdapter adapter;
        ProfileDialog dialog(&adapter);
        design::ThemeManager theme;
        theme.setMode(design::ThemeMode::Light);
        theme.applyTo(dialog);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        const auto surface = [&] {
            const auto capture = dialog.grab();
            return capture.toImage().pixelColor(
                qRound(4 * capture.devicePixelRatio()),
                qRound(dialog.height() / 2.0 * capture.devicePixelRatio()));
        };
        QCOMPARE(surface(), QColor("#ffffff"));
        theme.setMode(design::ThemeMode::Dark);
        theme.applyTo(dialog);
        QTRY_COMPARE(surface(), QColor("#20272b"));
        theme.setMode(design::ThemeMode::Light);
        theme.applyTo(dialog);
        QCOMPARE(surface(), QColor("#ffffff"));
    }

    void connectionDriverChoicesPreserveEachDraft() {
        using namespace choscordb;
        EngineAdapter adapter;
        ProfileDialog dialog(&adapter);
        design::ThemeManager theme;
        QCOMPARE(theme.mode(), design::ThemeMode::System);
        QVERIFY(!theme.resolvedTheme().forcedContrast);
        theme.applyTo(dialog);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto* sqlite = dialog.findChild<design::Button*>("profileDriverSqlite");
        auto* postgres = dialog.findChild<design::Button*>("profileDriverPostgres");
        QVERIFY(sqlite);
        QVERIFY(postgres);
        QTRY_VERIFY(sqlite->isEnabled());
        QVERIFY(sqlite->isVisible());
        QVERIFY(postgres->isVisible());
        QCOMPARE(sqlite->height(), 44);
        QVERIFY(sqlite->isChecked());
        const auto selectedImage = sqlite->grab().toImage();
        QCOMPARE(selectedImage.pixelColor(selectedImage.width() / 2,
                                          qRound(6 * sqlite->devicePixelRatioF())),
                 QColor("#eaf4ef"));
        auto* path = dialog.findChild<QLineEdit*>("profilePath");
        auto* host = dialog.findChild<QLineEdit*>("profileHost");
        path->setText("/tmp/分析.db");
        QTest::mouseClick(postgres, Qt::LeftButton);
        QVERIFY(postgres->isChecked());
        QVERIFY(host->isVisible());
        QVERIFY(!path->isVisible());
        host->setText("database.example");
        auto* security = dialog.findChild<QPushButton*>("profileSecurity");
        auto* tls = dialog.findChild<QComboBox*>("profileTls");
        QVERIFY(!tls->isVisible());
        security->setFocus();
        QTest::keyClick(security, Qt::Key_Space);
        QVERIFY(tls->isVisible());
        sqlite->setFocus();
        QTest::keyClick(sqlite, Qt::Key_Space);
        QVERIFY(sqlite->isChecked());
        QCOMPARE(path->text(), QString("/tmp/分析.db"));
        QTest::mouseClick(postgres, Qt::LeftButton);
        QCOMPARE(host->text(), QString("database.example"));
        QCOMPARE(dialog.findChild<QComboBox*>("profileDriver")->currentData().toString(),
                 QString("postgres"));
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
        auto* connect = dialog.findChild<design::Button*>("profileSaveConnect");
        auto* remove = dialog.findChild<design::Button*>("profileDelete");
        QVERIFY(save);
        QVERIFY(connect);
        QVERIFY(remove);
        QCOMPARE(save->variant(), design::ButtonVariant::Outline);
        QCOMPARE(connect->variant(), design::ButtonVariant::Default);
        QCOMPARE(remove->variant(), design::ButtonVariant::Destructive);
        QTRY_VERIFY(save->isEnabled());
        QImage rendered(connect->size(), QImage::Format_ARGB32_Premultiplied);
        rendered.fill(Qt::transparent);
        connect->render(&rendered);
        QCOMPARE(rendered.pixelColor(rendered.width() / 2, 6), QColor("#287f66"));
        QVERIFY(dialog.isModal());
        dialog.findChild<QLineEdit*>("profileName")->setText("Analysis");
        dialog.findChild<QLineEdit*>("profilePath")->setText(":memory:");
        save->click();
        QTRY_COMPARE(saved.count(), 1);
        QCOMPARE(qvariant_cast<SavedProfile>(saved.first().at(1)).name, QString("Analysis"));
    }
};

QTEST_MAIN(SecondaryDesignTest)
#include "secondary_design_test.moc"
