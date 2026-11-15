#include "bridge/engine_adapter.h"
#include "confirmation_checks.h"
#include "design_system/button/button.h"
#include "design_system/button_group/button_group.h"
#include "design_system/control_style.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/metrics/metrics.h"
#include "design_system/tabs/tab_add_corner.h"
#include "design_system/theme_manager.h"
#include "models/history_model.h"
#include "widgets/export_dialog/export_dialog.h"
#include "widgets/history_dock/history_dock.h"
#include "widgets/preferences_dialog/preferences_dialog.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/search_panel/search_panel.h"
#include "widgets/sql_editor/sql_editor.h"
#include "widgets/value_detail_dialog/value_detail_dialog.h"
#include <QFormLayout>
#include <QLabel>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <memory>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QImage>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTabBar>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

class SecondaryDesignTest final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QApplication::setStyle(new choscordb::design::ControlStyle); }

    void mysqlExecutionRangeKeepsEscapedSemicolonInsideStatement() {
        const QString sql = QStringLiteral("SELECT 'a\\';b'; SELECT 2;");
        const auto range = choscordb::EngineAdapter::executionRange(
            sql, 12, 0, 0, choscordb::RunScope::Current, "mysql");
        QVERIFY(range.valid);
        QCOMPARE(sql.mid(range.start, range.end - range.start), QString("SELECT 'a\\';b';"));
        const auto destructive = choscordb::EngineAdapter::executionRange(
            "# note\nDELETE FROM t", 9, 0, 0, choscordb::RunScope::Current, "mysql");
        QVERIFY(destructive.confirmation);
    }

    void exportDialogOffersMysqlDialect() {
        choscordb::EngineAdapter adapter;
        choscordb::ExportDialog dialog(&adapter);
        auto* dialect = dialog.findChild<QComboBox*>("exportDialect");
        QVERIFY(dialect);
        QVERIFY(dialect->findData("mysql") >= 0);
    }

    void secondaryDialogsUseSharedActionsAndKeepRejectionSafe_data() {
        QTest::addColumn<QString>("surface");
        QTest::addColumn<QString>("closeName");
        QTest::newRow("export") << QString("export") << QString("exportCancel");
        QTest::newRow("value detail") << QString("value") << QString("valueClose");
        QTest::newRow("preferences") << QString("preferences") << QString("preferencesClose");
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
            dialog = std::make_unique<PreferencesDialog>(&adapter, QList<ShortcutDescriptor>{});
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
        QCOMPARE(dialog->isModal(), surface != "value");
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
            auto* close = dialog->findChild<QPushButton*>(
                dialog == &preferences ? QString("preferencesClose") : QString("exportCancel"));
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
                 QColor("#f8f8f8"));
        QCOMPARE(preferences.height(), 452);
        auto* tabs = preferences.findChild<QTabWidget*>("preferencesSections");
        const int inset = design::spacing(design::Spacing::Six);
        QCOMPARE(tabs->tabBar()->mapTo(&preferences, QPoint{}).x(), inset);
        QCOMPARE(tabs->tabBar()->x(), design::spacing(design::Spacing::Two));
        for (const auto mode : {design::ThemeMode::Light, design::ThemeMode::Dark}) {
            theme.setMode(mode);
            theme.applyTo(preferences);
            QTest::qWait(20);
            const auto capture = preferences.grab();
            const auto point =
                tabs->mapTo(&preferences, QPoint(tabs->width() / 2, tabs->height() - 20));
            QCOMPARE(capture.toImage().pixelColor(point * capture.devicePixelRatio()),
                     mode == design::ThemeMode::Light ? QColor("#ffffff") : QColor("#0f0f0f"));
        }
        preferences.close();
    }

    void compactPreferencesKeepScrollablePagesUsable() {
        using namespace choscordb;
        EngineAdapter adapter;
        QList<ShortcutDescriptor> shortcuts;
        for (int index = 0; index < 20; ++index)
            shortcuts.append(
                {QString::number(index), QString("Synthetic action %1").arg(index), {}});
        PreferencesDialog preferences(&adapter, shortcuts);
        design::ThemeManager theme;
        theme.applyTo(preferences);
        preferences.show();
        QCoreApplication::processEvents();
        QCOMPARE(preferences.height(), 452);
        auto* tabs = preferences.findChild<QTabWidget*>("preferencesSections");
        QVERIFY(tabs);
        tabs->setCurrentIndex(5);
        QCoreApplication::processEvents();
        auto* scroll = qobject_cast<QScrollArea*>(tabs->currentWidget());
        QVERIFY(scroll);
        auto* lastShortcut = scroll->findChild<QKeySequenceEdit*>("shortcut_19");
        QVERIFY(lastShortcut);
        QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QCoreApplication::processEvents();
        QVERIFY(scroll->viewport()->rect().contains(lastShortcut->mapTo(
            scroll->viewport(), QPoint(lastShortcut->width() / 2, lastShortcut->height() - 1))));
        auto* apply = preferences.findChild<QPushButton*>("preferencesApply");
        QVERIFY(apply);
        QVERIFY(preferences.rect().contains(
            QRect(apply->mapTo(&preferences, QPoint{}), apply->size())));
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
        for (const auto& choice :
             {std::pair{"profileDriverPostgres", choscordb::design::Icon::PostgreSQL},
              std::pair{"profileDriverSqlite", choscordb::design::Icon::SQLite}}) {
            auto* button = dialog.findChild<QPushButton*>(choice.first);
            QVERIFY(button);
            const auto image = button->icon().pixmap(24, 24).toImage();
            QCOMPARE(image, choscordb::design::themedIcon(choice.second, Qt::black, 24)
                                .pixmap(24, 24)
                                .toImage());
            QVERIFY(image !=
                    choscordb::design::themedIcon(choscordb::design::Icon::Database, Qt::black, 24)
                        .pixmap(24, 24)
                        .toImage());
        }
        auto* driver = dialog.findChild<QComboBox*>("profileDriver");
        QTRY_VERIFY(driver->isEnabled());
        driver->setCurrentIndex(driver->findData("postgres"));
        QCoreApplication::processEvents();
        QVERIFY(dialog.width() >= 560);
        QVERIFY(
            dialog.width() <=
            choscordb::design::dialogInitialSize(choscordb::design::DialogSize::Profiles).width());
        QVERIFY(!dialog.findChild<QListWidget*>("profileList")->isVisible());
        auto* save = dialog.findChild<QPushButton*>("profileSaveConnect");
        QVERIFY(save->isVisible());
        QVERIFY(dialog.rect().contains(QRect(save->mapTo(&dialog, QPoint()), save->size())));
        QVERIFY(dialog.findChild<QLineEdit*>("profilePassword")->isVisible());
        auto* form = dialog.findChild<QFormLayout*>();
        QCOMPARE(qobject_cast<QLabel*>(
                     form->labelForField(
                         dialog.findChild<QLineEdit*>("profilePassword")->parentWidget()))
                     ->text()
                     .remove('&'),
                 QString("Password (optional)"));
    }

    void mariadbProfileChoiceUsesServerFieldsDefaultPortAndSealIcon() {
        using namespace choscordb;
        EngineAdapter adapter;
        ProfileDialog dialog(&adapter);
        dialog.show();
        auto* driver = dialog.findChild<QComboBox*>("profileDriver");
        QTRY_VERIFY(driver->isEnabled());
        dialog.findChild<QPushButton*>("profileDriverPostgres")->click();
        auto* port = dialog.findChild<QSpinBox*>("profilePort");
        QCOMPARE(port->value(), 5432);
        auto* mariadb = dialog.findChild<QPushButton*>("profileDriverMariadb");
        QVERIFY(mariadb);
        mariadb->click();
        QCOMPARE(driver->currentData().toString(), QString("mariadb"));
        QVERIFY(mariadb->isChecked());
        QVERIFY(!dialog.findChild<QPushButton*>("profileDriverMysql")->isChecked());
        QCOMPARE(port->value(), 3306);
        QVERIFY(dialog.findChild<QLineEdit*>("profileHost")->isVisible());
        QVERIFY(!dialog.findChild<QLineEdit*>("profilePath")->isVisible());
        QVERIFY(dialog.findChild<QCheckBox*>("profileSshEnabled")->isVisible());
        auto* form = dialog.findChild<QFormLayout*>();
        QCOMPARE(
            qobject_cast<QLabel*>(
                form->labelForField(dialog.findChild<QLineEdit*>("profileUser")->parentWidget()))
                ->text()
                .remove('&'),
            QString("Username (optional)"));
        const auto icon = mariadb->icon().pixmap(QSize(32, 32), 2.0).toImage();
        bool brown = false;
        for (int y = 0; y < icon.height(); ++y)
            for (int x = 0; x < icon.width(); ++x) {
                const auto pixel = icon.pixelColor(x, y);
                brown |= pixel.alpha() > 200 && qAbs(pixel.red() - 0xc0) < 16 &&
                         qAbs(pixel.green() - 0x76) < 16 && qAbs(pixel.blue() - 0x5a) < 16;
            }
        QVERIFY2(brown, "The MariaDB choice shows the MariaDB seal");
    }

    void mysqlProfileChoiceUsesServerFieldsAndDefaultPortWithSsh() {
        using namespace choscordb;
        EngineAdapter adapter;
        ProfileDialog dialog(&adapter);
        dialog.show();
        auto* driver = dialog.findChild<QComboBox*>("profileDriver");
        QTRY_VERIFY(driver->isEnabled());
        auto* mysql = dialog.findChild<QPushButton*>("profileDriverMysql");
        QVERIFY(mysql);
        mysql->click();
        QCOMPARE(driver->currentData().toString(), QString("mysql"));
        QVERIFY(mysql->isChecked());
        QVERIFY(dialog.findChild<QLineEdit*>("profileHost")->isVisible());
        QVERIFY(dialog.findChild<QLineEdit*>("profilePassword")->isVisible());
        QVERIFY(!dialog.findChild<QLineEdit*>("profilePath")->isVisible());
        auto* port = dialog.findChild<QSpinBox*>("profilePort");
        QCOMPARE(port->value(), 3306);
        QVERIFY(dialog.findChild<QCheckBox*>("profileSshEnabled")->isVisible());
        auto* user = dialog.findChild<QLineEdit*>("profileUser");
        auto* database = dialog.findChild<QLineEdit*>("profileDatabase");
        QVERIFY(user && database);
        const auto caption = [&dialog](QWidget* field) {
            for (auto* label : dialog.findChildren<QLabel*>())
                if (label->buddy() &&
                    (label->buddy() == field || label->buddy()->isAncestorOf(field)))
                    return label->text().remove(QLatin1Char('&'));
            return QString();
        };
        auto* databaseHelp = dialog.findChild<QLabel*>("profileDatabaseHelp");
        QVERIFY(databaseHelp);
        QCOMPARE(caption(user), QString("Username (optional)"));
        QCOMPARE(databaseHelp->text(), QString("If empty, the connection opens no database."));
        dialog.findChild<QPushButton*>("profileDriverPostgres")->click();
        QCOMPARE(port->value(), 5432);
        QVERIFY(dialog.findChild<QPushButton*>("profileDriverPostgres")->isChecked());
        QVERIFY(!mysql->isChecked());
        QCOMPARE(caption(user), QString("Username"));
        QCOMPARE(databaseHelp->text(), QString("If empty, the database name is the username."));
        auto* ssh = dialog.findChild<QCheckBox*>("profileSshEnabled");
        ssh->setChecked(true);
        mysql->click();
        auto* mysqlSsh = dialog.findChild<QCheckBox*>("profileSshEnabled");
        QVERIFY(mysqlSsh->isVisible());
        mysqlSsh->setChecked(true);
        QVERIFY(dialog.findChild<QLineEdit*>("profileSshHost")->isVisible());
        auto* authentication = dialog.findChild<QComboBox*>("profileSshAuthentication");
        auto* identity = dialog.findChild<QLineEdit*>("profileSshIdentityFile");
        auto* secret = dialog.findChild<QLineEdit*>("profileSshSecret");
        auto* saveCredentials = dialog.findChild<QCheckBox*>("profileSaveCredentials");
        QVERIFY(authentication && identity && secret && saveCredentials);
        QCOMPARE(authentication->count(), 3);
        authentication->setCurrentIndex(authentication->findData("agent"));
        QVERIFY(!identity->isVisible());
        QVERIFY(!secret->isVisible());
        authentication->setCurrentIndex(authentication->findData("public_key"));
        QVERIFY(identity->isVisible());
        QVERIFY(secret->isVisible());
        QCOMPARE(secret->echoMode(), QLineEdit::Password);
        secret->setText("key-passphrase");
        authentication->setCurrentIndex(authentication->findData("password"));
        QVERIFY(!identity->isVisible());
        QVERIFY(secret->isVisible());
        QVERIFY(secret->text().isEmpty());
        QVERIFY(saveCredentials->isVisible());
        QVERIFY(!saveCredentials->isChecked());
        port->setValue(3307);
        dialog.findChild<QPushButton*>("profileDriverSqlite")->click();
        mysql->click();
        QCOMPARE(port->value(), 3307);
    }

    void postgresSshFormSavesAndRestoresTunnelSettings() {
        using namespace choscordb;
        EngineAdapter adapter;
        ProfileDialog dialog(&adapter);
        dialog.show();
        auto* save = dialog.findChild<QPushButton*>("profileSave");
        QTRY_VERIFY(save->isEnabled());
        dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
        dialog.findChild<QLineEdit*>("profileName")->setText("Tunnel UI");
        dialog.findChild<QLineEdit*>("profileDatabase")->setText("app");
        dialog.findChild<QLineEdit*>("profileUser")->setText("dbuser");
        auto* enabled = dialog.findChild<QCheckBox*>("profileSshEnabled");
        QVERIFY(enabled);
        QVERIFY(!enabled->isChecked());
        auto* host = dialog.findChild<QLineEdit*>("profileSshHost");
        QVERIFY(!host->isVisible());
        enabled->setChecked(true);
        QVERIFY(host->isVisible());
        host->setText("bastion.example");
        auto* port = dialog.findChild<QSpinBox*>("profileSshPort");
        QCOMPARE(port->value(), 22);
        port->setValue(2222);
        dialog.findChild<QLineEdit*>("profileSshUser")->setText("operator");
        dialog.findChild<QComboBox*>("profileSshAuthentication")
            ->setCurrentIndex(
                dialog.findChild<QComboBox*>("profileSshAuthentication")->findData("public_key"));
        dialog.findChild<QLineEdit*>("profileSshIdentityFile")->setText("/keys/test");
        QSignalSpy saved(&adapter, &EngineAdapter::profileSaved);
        save->click();
        QTRY_COMPARE(saved.count(), 1);
        const auto profile = qvariant_cast<SavedProfile>(saved.first().at(1));
        QVERIFY(profile.sshEnabled);
        QCOMPARE(profile.sshHost, QString("bastion.example"));
        QCOMPARE(profile.sshPort, quint16(2222));
        QCOMPARE(profile.sshUser, QString("operator"));
        QCOMPARE(profile.sshAuthentication, QString("public_key"));
        QCOMPARE(profile.sshIdentityFile, QString("/keys/test"));
        QTRY_VERIFY(save->isEnabled());
        QCOMPARE(host->text(), QString("bastion.example"));
        QCOMPARE(port->value(), 2222);
        enabled->setChecked(false);
        save->click();
        QTRY_COMPARE(saved.count(), 2);
        QVERIFY(!qvariant_cast<SavedProfile>(saved.at(1).at(1)).sshEnabled);
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
        const auto prompt = confirmNextPrompt();
        next.close();
        QCOMPARE(*prompt, QString("Discard changes?"));
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
        auto* footer = dialog.findChild<design::DialogSections*>("profileSections")
                           ->findChild<QWidget*>("dialogSectionsFooter");
        const auto footerColor = [&] {
            const auto image = footer->grab().toImage();
            return image.pixelColor(qRound(4 * image.devicePixelRatio()),
                                    qRound(4 * image.devicePixelRatio()));
        };
        QCOMPARE(surface(), QColor("#ffffff"));
        QCOMPARE(footerColor(), theme.resolvedTheme().colors.elevated);
        theme.setMode(design::ThemeMode::Dark);
        theme.applyTo(dialog);
        QTRY_COMPARE(surface(), QColor("#0f0f0f"));
        QTRY_COMPARE(footerColor(), theme.resolvedTheme().colors.elevated);
        theme.setMode(design::ThemeMode::Light);
        theme.applyTo(dialog);
        QCOMPARE(surface(), QColor("#ffffff"));
        QCOMPARE(footerColor(), theme.resolvedTheme().colors.elevated);
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
        QCOMPARE(sqlite->height(), sqlite->width());
        QVERIFY(sqlite->isChecked());
        const auto selectedImage = sqlite->grab().toImage();
        QCOMPARE(selectedImage.pixelColor(selectedImage.width() / 2,
                                          qRound(6 * sqlite->devicePixelRatioF())),
                 QColor("#f5f5f5"));
        auto* path = dialog.findChild<QLineEdit*>("profilePath");
        auto* host = dialog.findChild<QLineEdit*>("profileHost");
        path->setText("/tmp/分析.db");
        QTest::mouseClick(postgres, Qt::LeftButton);
        QVERIFY(postgres->isChecked());
        QVERIFY(host->isVisible());
        QVERIFY(!path->isVisible());
        host->setText("database.example");
        QVERIFY(!dialog.findChild<QCheckBox*>("profileUseTls"));
        QVERIFY(!dialog.findChild<QComboBox*>("profileTls"));
        sqlite->setFocus();
        QTest::keyClick(sqlite, Qt::Key_Space);
        QVERIFY(sqlite->isChecked());
        QCOMPARE(path->text(), QString("/tmp/分析.db"));
        QTest::mouseClick(postgres, Qt::LeftButton);
        QCOMPARE(host->text(), QString("database.example"));
        QVERIFY(!dialog.findChild<QComboBox*>("profileTls"));
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
        QCOMPARE(save->variant(), design::ButtonVariant::Secondary);
        QCOMPARE(connect->variant(), design::ButtonVariant::Default);
        QCOMPARE(remove->variant(), design::ButtonVariant::Destructive);
        QTRY_VERIFY(save->isEnabled());
        QImage rendered(connect->size(), QImage::Format_ARGB32_Premultiplied);
        rendered.fill(Qt::transparent);
        connect->render(&rendered);
        QCOMPARE(rendered.pixelColor(rendered.width() / 2, 0), QColor("#045ede"));
        const auto end = rendered.pixelColor(rendered.width() / 2, rendered.height() - 2);
        QVERIFY2(qAbs(end.red() - 0x05) <= 6 && qAbs(end.green() - 0x6d) <= 6 &&
                     qAbs(end.blue() - 0xff) <= 6,
                 qPrintable(end.name()));
        QVERIFY(dialog.isModal());
        dialog.findChild<QLineEdit*>("profileName")->setText("Analysis");
        dialog.findChild<QLineEdit*>("profilePath")->setText(":memory:");
        save->click();
        QTRY_COMPARE(saved.count(), 1);
        QCOMPARE(qvariant_cast<SavedProfile>(saved.first().at(1)).name, QString("Analysis"));
    }
    void tabAddButtonFollowsTheLastTabWithoutAFrameOfOverlap() {
        QTabWidget tabs;
        tabs.resize(900, 300);
        auto* corner = new choscordb::design::TabAddCorner(&tabs);
        tabs.addTab(new QWidget, "first");
        tabs.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tabs));
        QCoreApplication::processEvents();
        for (int index = 1; index < 4; ++index) {
            tabs.addTab(new QWidget, QString("tab %1").arg(index));
            const auto lastTab = tabs.tabBar()->tabRect(tabs.count() - 1);
            const auto lastTabEnd = tabs.tabBar()->mapTo(&tabs, lastTab.topRight()).x();
            QVERIFY2(corner->geometry().left() > lastTabEnd,
                     qPrintable(QString("corner %1, last tab end %2")
                                    .arg(corner->geometry().left())
                                    .arg(lastTabEnd)));
        }
        auto* bar = tabs.tabBar();
        bar->move(bar->x(), bar->y() + choscordb::design::spacing(choscordb::design::Spacing::Two));
        QCOMPARE(corner->y(), bar->mapTo(&tabs, QPoint()).y());
    }
    void fieldHelpIsVisibleTextNotATooltip() {
        choscordb::EngineAdapter adapter;
        choscordb::ExportDialog dialog(&adapter);
        auto* help = dialog.findChild<QLabel*>("exportIdentifierHelp");
        QVERIFY(help);
        QCOMPARE(help->property("designRole").toString(), QString("description"));
        for (auto* field : dialog.findChildren<QLineEdit*>())
            QVERIFY2(field->toolTip().isEmpty(), qPrintable(field->objectName()));
        choscordb::ProfileDialog profile(&adapter);
        QVERIFY(profile.findChild<QLabel*>("profileSshHostHelp"));
        QVERIFY(profile.findChild<QLineEdit*>("profileSshHost")->toolTip().isEmpty());
    }
    void menuAndSelectRowsUseTheKumoMenuRowHeight() {
        using namespace choscordb;
        QWidget host;
        design::ThemeManager theme;
        theme.installOn(qApp);
        QMenu menu(&host);
        menu.addAction("Copy selected cells");
        menu.addAction("Delete rows");
        menu.ensurePolished();
        QCOMPARE(menu.actionGeometry(menu.actions().first()).height(),
                 design::dimension(design::Dimension::MenuRow));
        QComboBox select(&host);
        select.addItems({"CSV", "JSON"});
        host.show();
        select.showPopup();
        auto* view = select.view();
        QTRY_VERIFY(view->isVisible());
        QCOMPARE(view->visualRect(view->model()->index(0, 0)).height(),
                 design::dimension(design::Dimension::MenuRow));
        select.hidePopup();
    }
    void previousAndNextButtonsShowTheSameChevrons() {
        using namespace choscordb;
        EngineAdapter adapter;
        ValueDetailDialog value(&adapter);
        QWidget searchHost;
        SearchPanel search([] { return nullptr; }, &searchHost);
        const QList<QPair<QWidget*, QString>> owners{{&value, "value"}, {&search, "search"}};
        for (const auto& [owner, prefix] : owners) {
            for (const auto& name : {prefix + "Previous", prefix + "Next"}) {
                auto* button = owner->findChild<QPushButton*>(name);
                QVERIFY2(button, qPrintable(name));
                QVERIFY2(!button->icon().isNull(), qPrintable(name));
                QVERIFY2(!button->text().isEmpty(), qPrintable(name));
            }
        }
    }
    void keyboardFocusShowsTheRingOnRowsCellsTabsAndToolButtons() {
        using namespace choscordb;
        QWidget root;
        design::ThemeManager theme;
        theme.setMode(design::ThemeMode::Light);
        theme.installOn(qApp);
        auto* layout = new QVBoxLayout(&root);
        auto* list = new QListWidget(&root);
        list->addItems({"orders", "customers"});
        auto* table = new QTableWidget(2, 2, &root);
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 2; ++column)
                table->setItem(row, column, new QTableWidgetItem("value"));
        auto* tree = new QTreeWidget(&root);
        tree->setHeaderHidden(true);
        new QTreeWidgetItem(tree, QStringList{"public"});
        new QTreeWidgetItem(tree, QStringList{"sales"});
        auto* tabs = new QTabBar(&root);
        tabs->setExpanding(false);
        tabs->addTab("Columns");
        tabs->addTab("Indexes");
        auto* tool = new QToolButton(&root);
        tool->setText("Refresh");
        for (QWidget* widget : QList<QWidget*>{list, table, tree, tabs, tool})
            layout->addWidget(widget);
        root.resize(400, 640);
        root.show();
        root.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&root));
        list->setCurrentRow(0);
        table->setCurrentCell(0, 0);
        tree->setCurrentItem(tree->topLevelItem(0));
        const auto ring = design::resolvedThemeForWidget(root).colors.brand;
        table->setTabKeyNavigation(false);
        const auto tabTo = [](QWidget* widget) {
            int press = 0;
            do
                QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
            while (!widget->hasFocus() && ++press < 8);
            QCoreApplication::processEvents();
            return widget->hasFocus();
        };
        const auto ringPixels = [&](QWidget* widget, QRect area) {
            if (!tabTo(widget))
                return -1;
            const auto image = widget->grab().toImage();
            const auto scale = widget->devicePixelRatioF();
            int count = 0;
            for (int y = area.top(); y <= area.bottom(); ++y)
                for (int x = area.left(); x <= area.right(); ++x)
                    count += image.pixelColor(qRound(x * scale), qRound(y * scale)) == ring;
            return count;
        };
        const auto inViewport = [](QAbstractItemView* view, QRect rect) {
            return rect.translated(view->viewport()->mapTo(view, QPoint()));
        };
        list->setFocus(Qt::MouseFocusReason);
        QCoreApplication::processEvents();
        QVERIFY(list->hasFocus());
        const auto pointerFocus = list->grab().toImage();
        const auto row = inViewport(list, list->visualItemRect(list->item(0)));
        int pointerRingPixels = 0;
        for (int y = row.top(); y <= row.bottom(); ++y)
            for (int x = row.left(); x <= row.right(); ++x)
                pointerRingPixels +=
                    pointerFocus.pixelColor(qRound(x * list->devicePixelRatioF()),
                                            qRound(y * list->devicePixelRatioF())) == ring;
        QCOMPARE(pointerRingPixels, 0);
        QVERIFY(!root.testAttribute(Qt::WA_KeyboardFocusChange));
        QVERIFY2(ringPixels(list, inViewport(list, list->visualItemRect(list->item(0)))) > 20,
                 "list row");
        QVERIFY2(ringPixels(table, inViewport(table, table->visualItemRect(table->item(0, 0)))) >
                     20,
                 "table cell");
        QVERIFY2(ringPixels(tree, inViewport(tree, tree->visualItemRect(tree->topLevelItem(0)))) >
                     20,
                 "tree row");
        QVERIFY2(ringPixels(tabs, tabs->tabRect(tabs->currentIndex())) > 20, "pane tab");
        QVERIFY(tabTo(tool));
        const auto frame = root.grab().toImage();
        const auto around = tool->geometry().adjusted(-4, -4, 4, 4);
        int toolRing = 0;
        for (int y = around.top(); y <= around.bottom(); ++y)
            for (int x = around.left(); x <= around.right(); ++x)
                toolRing += frame.pixelColor(qRound(x * root.devicePixelRatioF()),
                                             qRound(y * root.devicePixelRatioF())) == ring;
        QVERIFY2(toolRing > 20, "tool button");
    }
};

QTEST_MAIN(SecondaryDesignTest)
#include "secondary_design_test.moc"
