#include "bridge/engine_adapter.h"
#include "confirmation_checks.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/field/field.h"
#include "design_system/spinner/spinner.h"
#include "design_system/toast_region/toast_region.h"
#include "dialog_sections_checks.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "workspace_test.h"
#include "workspace_test_fixture.h"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>

void WorkspaceTest::connectionOperationsShowProgressInTheStatusLine() {
    WorkspaceFixture f;
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
    QVERIFY(dialog);
    QVERIFY(!f.parent.findChild<QDialog*>("profileProgressDialog"));
    auto* statusLine = dialog->findChild<QWidget*>("profileStatusLine");
    auto* statusText = dialog->findChild<QLabel*>("profileStatus");
    auto* spinner = statusLine->findChild<choscordb::design::Spinner*>();
    QVERIFY(statusLine && statusText && spinner);
    const auto busyWith = [&](const QString& message) {
        return statusLine->isVisible() && spinner->isVisible() && statusText->text() == message &&
               !dialog->findChild<QPushButton*>("profileTest")->isEnabled();
    };
    QTRY_VERIFY(dialog->findChild<QPushButton*>("profileTest")->isEnabled());
    auto* name = dialog->findChild<QLineEdit*>("profileName");
    name->setText("Test profile");
    auto* path = dialog->findChild<QLineEdit*>("profilePath");
    dialog->findChild<QPushButton*>("profileTest")->click();
    auto* pathValidation = dynamic_cast<choscordb::design::FieldValidation*>(path->parentWidget());
    QVERIFY(pathValidation);
    QVERIFY(pathValidation->error().contains("database file path"));
    QVERIFY(!spinner->isVisible());
    path->setText("file:example.db?mode=banana");
    dialog->findChild<QPushButton*>("profileTest")->click();
    QVERIFY(pathValidation->error().contains("SQLite file URI"));
    QVERIFY(!spinner->isVisible());
    path->setText("file:example.db?mode=ro");
    name->setText(QString(1025, QChar('n')));
    dialog->findChild<QPushButton*>("profileTest")->click();
    auto* nameValidation = dynamic_cast<choscordb::design::FieldValidation*>(name->parentWidget());
    QVERIFY(nameValidation && nameValidation->error().contains("too long"));
    QVERIFY(pathValidation->error().isEmpty());
    name->setText("Test profile");
    path->setText(":memory:");
    QVERIFY(pathValidation->error().isEmpty());
    dialog->findChild<QPushButton*>("profileTest")->click();
    QVERIFY(busyWith("Testing the connection…"));
    QTRY_VERIFY(!spinner->isVisible());
    QVERIFY(!statusLine->isVisible());
    auto* feedback =
        f.parent.findChild<choscordb::ToastRegion*>("toastRegion", Qt::FindDirectChildrenOnly);
    QVERIFY(feedback && feedback->isVisible());
    QCOMPARE(feedback->parentWidget(), &f.parent);
    QCOMPARE(feedback->property("variant").toString(), QString("success"));
    QCOMPARE(feedback->geometry().right(), f.parent.width() - 33);
    QCOMPARE(feedback->geometry().bottom(), f.parent.height() - 33);
    QVERIFY(feedback->text().contains("Connection test succeeded."));
    feedback->findChild<QAbstractButton*>("toastDismiss")->click();
    QTRY_VERIFY(feedback->isHidden());
    auto* save = dialog->findChild<QPushButton*>("profileSave");
    save->click();
    QVERIFY(busyWith("Saving connection…"));
    QTRY_VERIFY(save->isEnabled());
    QTRY_VERIFY(!spinner->isVisible());
    QVERIFY(feedback->text().contains("Connection saved."));
    QVERIFY(!dialog->findChild<QWidget*>("profileStatusLine")->isVisible());
    QTest::mouseClick(feedback->findChild<QAbstractButton*>("toastDismiss"), Qt::LeftButton);
    QTRY_VERIFY(!feedback->isVisible());

    path->setText(QDir::temp().filePath("choscordb-missing-dir/missing/db.sqlite"));
    dialog->findChild<QPushButton*>("profileTest")->click();
    QTRY_VERIFY(!spinner->isVisible());
    QTRY_VERIFY(dialog->findChild<QWidget*>("profileStatusLine")->isVisible());
    QVERIFY(!dialog->findChild<QLabel*>("profileStatus")->text().isEmpty());
    QVERIFY(dialog->findChild<QLabel*>("profileStatus")->text() !=
            QString("Connection test succeeded."));
    QVERIFY(!feedback->isVisible());
}

void WorkspaceTest::connectionDraftUsesTheSharedDiscardPrompt() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    const QString expected("The changes in this dialog are not saved.");
    auto* name = dialog.findChild<QLineEdit*>("profileName");
    QTRY_VERIFY(name->isEnabled());
    QTest::keyClicks(name, "Draft");
    {
        const auto prompt = confirmNextPrompt();
        dialog.findChild<QPushButton*>("profileNew")->click();
        QCOMPARE(*prompt, QString("Discard changes?"));
        QCOMPARE(prompt.text(), expected);
    }
    QVERIFY(name->text().isEmpty());
    QTest::keyClicks(name, "Draft");
    int prompts = 0;
    QTimer watch;
    QObject::connect(&watch, &QTimer::timeout, &watch, [&prompts] {
        for (auto* widget : QApplication::allWidgets())
            if (auto* box = qobject_cast<QMessageBox*>(widget); box && box->isVisible()) {
                ++prompts;
                if (auto* action = confirmationAction(box))
                    action->click();
                else
                    box->reject();
            }
    });
    watch.start(5);
    dialog.findChild<QPushButton*>("profileCancel")->click();
    QTRY_VERIFY(!dialog.isVisible());
    QCOMPARE(prompts, 1);
}

void WorkspaceTest::connectionSshFormShowsOnlyBasicSettings() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    QVERIFY(dialog.width() > 560);
    QVERIFY(dialog.height() > 440);
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    QVERIFY(!dialog.findChild<QComboBox*>("profileTls"));
    QVERIFY(!dialog.findChild<QCheckBox*>("profileUseTls"));
    QVERIFY(!dialog.findChild<QPushButton*>("profileSecurity"));
    auto* sections = dialog.findChild<choscordb::design::DialogSections*>("profileSections");
    QVERIFY(sections);
    QCOMPARE(footerOverrides(dialog), QString());
    auto* scroll = dialog.findChild<QScrollArea*>("profileFormScroll");
    dialog.show();
    QCoreApplication::processEvents();
    auto* host = dialog.findChild<QLineEdit*>("profileHost");
    auto* port = dialog.findChild<QSpinBox*>("profilePort");
    auto* database = dialog.findChild<QLineEdit*>("profileDatabase");
    auto* user = dialog.findChild<QLineEdit*>("profileUser");
    auto* password = dialog.findChild<QLineEdit*>("profilePassword");
    const QList<QPair<QString, QWidget*>> rows{{"Host", host},
                                               {"Port", port},
                                               {"Database (optional)", database},
                                               {"Username", user},
                                               {"Password (optional)", password}};
    int previousTop = -1;
    for (const auto& [text, field] : rows) {
        QLabel* caption = nullptr;
        for (auto* label : dialog.findChildren<QLabel*>())
            if (label->buddy() && (label->buddy() == field || label->buddy()->isAncestorOf(field)))
                caption = label;
        QVERIFY2(caption, qPrintable(text));
        QCOMPARE(caption->text().remove(QLatin1Char('&')), text);
        QVERIFY(caption->mapTo(&dialog, QPoint()).x() + caption->width() <=
                field->mapTo(&dialog, QPoint()).x());
        QCOMPARE(field->mapTo(&dialog, QPoint()).x(), host->mapTo(&dialog, QPoint()).x());
        const int top = field->mapTo(&dialog, QPoint()).y();
        QVERIFY2(top > previousTop, qPrintable(text));
        previousTop = top;
    }
    QCOMPARE(scroll->geometry().right(), sections->bodyLayout()->parentWidget()->rect().right());
    dialog.findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    for (const char* name : {"profileSshHost", "profileSshUser"})
        QVERIFY(dialog.findChild<QLineEdit*>(name)->isVisibleTo(&dialog));
    for (const char* name : {"profileSshRemoteHost", "profileSshLocalHost", "profileSshAgentSocket",
                             "profileSshKnownHosts", "profileProxyHost"})
        QVERIFY2(!dialog.findChild<QLineEdit*>(name), name);
    QVERIFY(!dialog.findChild<QSpinBox*>("profileSshLocalPort"));
    QVERIFY(!dialog.findChild<QCheckBox*>("profileProxyEnabled"));
    dialog.resize(560, 440);
    QCoreApplication::processEvents();
    QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
    QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
    QVERIFY(sections->headerLayout()->parentWidget()->isVisible());
    QVERIFY(sections->findChild<QWidget*>("dialogSectionsFooter")->isVisible());
}

void WorkspaceTest::connectionFormHasNoTlsSettings() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog.show();
    QCoreApplication::processEvents();
    QVERIFY(!dialog.findChild<QCheckBox*>("profileUseTls"));
    QVERIFY(!dialog.findChild<QComboBox*>("profileTls"));
    QVERIFY(!dialog.findChild<QLineEdit*>("profileRootCertificate"));
    QVERIFY(!dialog.findChild<QLineEdit*>("profileTlsClientIdentity"));
    QVERIFY(!dialog.findChild<QLineEdit*>("profileTlsSecret")->isVisibleTo(&dialog));
    QCOMPARE(dialog.findChild<QScrollArea*>("profileFormScroll")->horizontalScrollBar()->maximum(),
             0);
}

void WorkspaceTest::savedTlsVerificationSurvivesProfileEdit() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    choscordb::EngineAdapter adapter(nullptr, directory.filePath("metadata.sqlite"));
    choscordb::ProfileDialog dialog(&adapter);
    choscordb::SavedProfile legacy;
    legacy.name = "Saved verified TLS";
    legacy.driver = "postgres";
    legacy.host = "localhost";
    legacy.user = "operator";
    legacy.tls = "verify_full";
    legacy.rootCertificate = "/tmp/ca.pem";
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::profileSaved);
    QTRY_VERIFY(dialog.findChild<QPushButton*>("profileSave")->isEnabled());
    dialog.saveDraft(legacy);
    QTRY_COMPARE(saved.count(), 1);
    dialog.show();
    QCoreApplication::processEvents();
    QVERIFY(!dialog.findChild<QLineEdit*>("profileTlsSecret")->isVisibleTo(&dialog));
    const auto first = qvariant_cast<choscordb::SavedProfile>(saved.at(0).at(1));
    QCOMPARE(first.tls, QString("verify_full"));
    QCOMPARE(first.rootCertificate, QString("/tmp/ca.pem"));
    dialog.findChild<QLineEdit*>("profileName")->setText("Renamed verified TLS");
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    save->click();
    QTRY_COMPARE(saved.count(), 2);
    const auto updated = qvariant_cast<choscordb::SavedProfile>(saved.at(1).at(1));
    QCOMPARE(updated.tls, QString("verify_full"));
    QCOMPARE(updated.rootCertificate, QString("/tmp/ca.pem"));
}

void WorkspaceTest::profileEnvironmentColorAndSafetySurviveEdit() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    choscordb::EngineAdapter adapter(nullptr, directory.filePath("metadata.sqlite"));
    choscordb::ProfileDialog dialog(&adapter);
    choscordb::SavedProfile profile;
    profile.name = "Tagged";
    profile.driver = "sqlite";
    profile.path = ":memory:";
    profile.environment = "custom";
    profile.environmentLabel = "QA";
    profile.safety = "read_only";
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::profileSaved);
    QTRY_VERIFY(dialog.findChild<QPushButton*>("profileSave")->isEnabled());
    dialog.saveDraft(profile);
    QTRY_COMPARE(saved.count(), 1);
    const auto first = qvariant_cast<choscordb::SavedProfile>(saved.at(0).at(1));
    QCOMPARE(first.tagLabel, QString("QA"));
    QCOMPARE(first.tone, QString("brand"));
    QCOMPARE(first.color, QString("automatic"));
    dialog.show();
    QCoreApplication::processEvents();
    auto* environment = dialog.findChild<QComboBox*>("profileEnvironment");
    auto* label = dialog.findChild<QLineEdit*>("profileEnvironmentLabel");
    auto* color = dialog.findChild<QComboBox*>("profileColor");
    auto* safety = dialog.findChild<QComboBox*>("profileSafety");
    QVERIFY(environment && label && color && safety);
    QCOMPARE(environment->currentData().toString(), QString("custom"));
    QCOMPARE(label->text(), QString("QA"));
    QVERIFY(label->isVisibleTo(&dialog));
    QCOMPARE(safety->currentData().toString(), QString("read_only"));
    environment->setCurrentIndex(environment->findData("production"));
    QVERIFY(!label->isVisibleTo(&dialog));
    color->setCurrentIndex(color->findData("blue"));
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    save->click();
    QTRY_COMPARE(saved.count(), 2);
    const auto updated = qvariant_cast<choscordb::SavedProfile>(saved.at(1).at(1));
    QCOMPARE(updated.environment, QString("production"));
    QVERIFY(updated.environmentLabel.isEmpty());
    QCOMPARE(updated.tagLabel, QString("Production"));
    QCOMPARE(updated.tone, QString("brand"));
    QCOMPARE(updated.safety, QString("read_only"));
}

void WorkspaceTest::savedDisabledTlsSurvivesProfileEdit() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    choscordb::EngineAdapter adapter(nullptr, directory.filePath("metadata.sqlite"));
    choscordb::ProfileDialog dialog(&adapter);
    choscordb::SavedProfile legacy;
    legacy.name = "Saved plaintext";
    legacy.driver = "mysql";
    legacy.host = "localhost";
    legacy.tls = "disable";
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::profileSaved);
    QTRY_VERIFY(dialog.findChild<QPushButton*>("profileSave")->isEnabled());
    dialog.saveDraft(legacy);
    QTRY_COMPARE(saved.count(), 1);
    dialog.findChild<QLineEdit*>("profileName")->setText("Renamed plaintext");
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    save->click();
    QTRY_COMPARE(saved.count(), 2);
    const auto updated = qvariant_cast<choscordb::SavedProfile>(saved.at(1).at(1));
    QCOMPARE(updated.tls, QString("disable"));
}

void WorkspaceTest::newServerProfilePrefersTlsWithoutSetup_data() {
    QTest::addColumn<QString>("driver");
    QTest::addColumn<QString>("host");
    QTest::newRow("postgres-tcp") << QString("postgres") << QString("localhost");
    QTest::newRow("mysql-tcp") << QString("mysql") << QString("localhost");
    QTest::newRow("postgres-socket") << QString("postgres") << QString("/tmp");
    QTest::newRow("mysql-socket") << QString("mysql") << QString("/tmp/mysql.sock");
}

void WorkspaceTest::newServerProfilePrefersTlsWithoutSetup() {
    QFETCH(QString, driver);
    QFETCH(QString, host);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    choscordb::EngineAdapter adapter(nullptr, directory.filePath("metadata.sqlite"));
    choscordb::ProfileDialog dialog(&adapter);
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    auto* driverChoice = dialog.findChild<QComboBox*>("profileDriver");
    driverChoice->setCurrentIndex(driverChoice->findData(driver));
    QVERIFY(!dialog.findChild<QComboBox*>("profileTls"));
    QVERIFY(!dialog.findChild<QCheckBox*>("profileUseTls"));
    QVERIFY(!dialog.findChild<QLineEdit*>("profileRootCertificate"));
    dialog.findChild<QLineEdit*>("profileHost")->setText(host);
    dialog.findChild<QLineEdit*>("profileName")->setText("Automatic TLS");
    dialog.findChild<QLineEdit*>("profileUser")->setText("operator");
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::profileSaved);
    save->click();
    QTRY_COMPARE(saved.count(), 1);
    const auto profile = qvariant_cast<choscordb::SavedProfile>(saved.at(0).at(1));
    QCOMPARE(profile.driver, driver);
    QCOMPARE(profile.tls, QString("prefer"));
    QCOMPARE(profile.host, host);
}

void WorkspaceTest::savedTlsClientIdentitySurvivesProfileEdit() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    choscordb::EngineAdapter adapter(nullptr, directory.filePath("metadata.sqlite"));
    choscordb::ProfileDialog dialog(&adapter);
    choscordb::SavedProfile legacy;
    legacy.name = "Saved client identity";
    legacy.driver = "postgres";
    legacy.host = "localhost";
    legacy.user = "operator";
    legacy.tls = "require";
    legacy.tlsClientIdentity = "/tmp/client.p12";
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::profileSaved);
    QTRY_VERIFY(dialog.findChild<QPushButton*>("profileSave")->isEnabled());
    dialog.saveDraft(legacy);
    QTRY_COMPARE(saved.count(), 1);
    dialog.show();
    QCoreApplication::processEvents();
    auto* secret = dialog.findChild<QLineEdit*>("profileTlsSecret");
    QVERIFY(secret->isVisibleTo(&dialog));
    secret->setText(QString(16384, QChar(0x00e9)));
    secret->setModified(true);
    dialog.findChild<QPushButton*>("profileTest")->click();
    auto* secretValidation =
        dynamic_cast<choscordb::design::FieldValidation*>(secret->parentWidget());
    QVERIFY(secretValidation->error().contains("Credential exceeds"));
    secret->clear();
    secret->setModified(false);
    dialog.findChild<QLineEdit*>("profileName")->setText("Renamed client identity");
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    save->click();
    QTRY_COMPARE(saved.count(), 2);
    const auto updated = qvariant_cast<choscordb::SavedProfile>(saved.at(1).at(1));
    QCOMPARE(updated.tls, QString("require"));
    QCOMPARE(updated.tlsClientIdentity, QString("/tmp/client.p12"));
}

void WorkspaceTest::connectionSshFormKeepsBasicFields() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog.findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    auto* host = dialog.findChild<QLineEdit*>("profileSshHost");
    auto* port = dialog.findChild<QSpinBox*>("profileSshPort");
    auto* user = dialog.findChild<QLineEdit*>("profileSshUser");
    auto* authentication = dialog.findChild<QComboBox*>("profileSshAuthentication");
    auto* source = dialog.findChild<QComboBox*>("profileSshIdentitySource");
    auto* file = dialog.findChild<QLineEdit*>("profileSshIdentityFile");
    auto* secret = dialog.findChild<QLineEdit*>("profileSshSecret");
    QCOMPARE(port->minimum(), 1);
    QCOMPARE(port->maximum(), 65535);
    authentication->setCurrentIndex(authentication->findData("public_key"));
    dialog.show();
    QCoreApplication::processEvents();
    int previousTop = -1;
    for (const auto& [text, field] :
         QList<QPair<QString, QWidget*>>{{"SSH host", host},
                                         {"SSH port", port},
                                         {"SSH username", user},
                                         {"Authentication", authentication},
                                         {"Private key source", source},
                                         {"Private key file", file},
                                         {"Key passphrase (optional)", secret}}) {
        QLabel* caption = nullptr;
        for (auto* label : dialog.findChildren<QLabel*>())
            if (label->buddy() && (label->buddy() == field || label->buddy()->isAncestorOf(field)))
                caption = label;
        QVERIFY2(caption, qPrintable(text));
        QCOMPARE(caption->text().remove(QLatin1Char('&')), text);
        QCOMPARE(field->mapTo(&dialog, QPoint()).x(), host->mapTo(&dialog, QPoint()).x());
        const int top = field->mapTo(&dialog, QPoint()).y();
        QVERIFY2(top > previousTop, qPrintable(text));
        previousTop = top;
    }
    QVERIFY(source->isVisibleTo(&dialog));
    QVERIFY(file->isVisibleTo(&dialog));
    QVERIFY(secret->isVisibleTo(&dialog));
    authentication->setCurrentIndex(authentication->findData("password"));
    QVERIFY(!source->isVisibleTo(&dialog));
    QVERIFY(!file->isVisibleTo(&dialog));
    QVERIFY(secret->isVisibleTo(&dialog));
    bool hasPasswordLabel = false;
    for (auto* label : dialog.findChildren<QLabel*>())
        hasPasswordLabel |= label->text().remove(QLatin1Char('&')) == "SSH password" &&
                            label->buddy() && label->buddy()->isAncestorOf(secret);
    QVERIFY(hasPasswordLabel);
    authentication->setCurrentIndex(authentication->findData("agent"));
    QVERIFY(!source->isVisibleTo(&dialog));
    QVERIFY(!file->isVisibleTo(&dialog));
    QVERIFY(!secret->isVisibleTo(&dialog));
    authentication->setCurrentIndex(authentication->findData("public_key"));
    source->setCurrentIndex(source->findData("inline"));
    QVERIFY(source->isVisibleTo(&dialog));
    QVERIFY(!file->isVisibleTo(&dialog));
    QVERIFY(dialog.findChild<QWidget*>("profileSshPrivateKey")->isVisibleTo(&dialog));
    QVERIFY(!dialog.findChild<QWidget*>("profileSshHopList"));
}

void WorkspaceTest::connectionTransportValidationRejectsIncompatibleSettings() {
    WorkspaceFixture f;
    QTRY_COMPARE(f.workspace.liveConnections().size(), 1);
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
    auto* save = dialog->findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    dialog->findChild<QLineEdit*>("profileName")->setText("Socket draft");
    auto* driver = dialog->findChild<QComboBox*>("profileDriver");
    driver->setCurrentIndex(1);
    auto* host = dialog->findChild<QLineEdit*>("profileHost");
    host->setText("/tmp/postgres socket");
    dialog->findChild<QLineEdit*>("profileUser")->setText("operator");
    const auto errorFor = [](QWidget* field) {
        return dynamic_cast<choscordb::design::FieldValidation*>(field->parentWidget())->error();
    };
    QSignalSpy saved(f.workspace.adapter(), &choscordb::EngineAdapter::profileSaved);
    QSignalSpy failed(f.workspace.adapter(), &choscordb::EngineAdapter::profileFailed);
    auto* sshEnabled = dialog->findChild<QCheckBox*>("profileSshEnabled");
    sshEnabled->setChecked(true);
    save->click();
    QVERIFY(errorFor(sshEnabled).contains("Unix"));
    QVERIFY(errorFor(sshEnabled).contains("SSH"));
    sshEnabled->setChecked(false);
    host->setText("localhost");
    driver->setCurrentIndex(2);
    driver->setCurrentIndex(1);
    dialog->findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    dialog->findChild<QLineEdit*>("profileSshHost")->setText("bastion.example");
    dialog->findChild<QLineEdit*>("profileSshUser")->setText("operator");
    QCOMPARE(saved.count(), 0);
    QCOMPARE(failed.count(), 0);
}

void WorkspaceTest::connectionFormRejectsMalformedHostsBeforeSubmission() {
    WorkspaceFixture f;
    QTRY_COMPARE(f.workspace.liveConnections().size(), 1);
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
    QVERIFY(dialog);
    auto* save = dialog->findChild<QPushButton*>("profileSave");
    const auto errorFor = [](QWidget* field) {
        return dynamic_cast<choscordb::design::FieldValidation*>(field->parentWidget())->error();
    };
    QTRY_VERIFY(save->isEnabled());
    dialog->findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog->findChild<QLineEdit*>("profileName")->setText("Invalid host draft");
    dialog->findChild<QLineEdit*>("profileDatabase")->setText("postgres");
    dialog->findChild<QLineEdit*>("profileUser")->setText("operator");
    auto* host = dialog->findChild<QLineEdit*>("profileHost");
    QSignalSpy saved(f.workspace.adapter(), &choscordb::EngineAdapter::profileSaved);
    QSignalSpy failed(f.workspace.adapter(), &choscordb::EngineAdapter::profileFailed);
    QSignalSpy submitted(dialog, &choscordb::ProfileDialog::connectionSubmitted);
    for (const auto& invalid : {"", "postgres://localhost", "user@localhost", "localhost:5432",
                                "host name", "[::1]:5432", "[invalid]"}) {
        host->setText(invalid);
        for (const auto* action :
             {"profileSave", "profileTest", "profileConnect", "profileSaveConnect"}) {
            dialog->findChild<QPushButton*>(action)->click();
            QVERIFY2(save->isEnabled(), action);
            QVERIFY2(errorFor(host).contains("Host"), qPrintable(errorFor(host)));
            QVERIFY(errorFor(host).contains("port"));
            QVERIFY(host->property("invalid").toBool());
        }
    }
    QCOMPARE(saved.count(), 0);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(submitted.count(), 0);
    host->setText("localhost");
    auto* user = dialog->findChild<QLineEdit*>("profileUser");
    user->clear();
    save->click();
    QVERIFY(errorFor(user).contains("database username"));
    user->setText("operator");
    auto* database = dialog->findChild<QLineEdit*>("profileDatabase");
    database->clear();
    database->setText("postgres");
    auto* sshEnabled = dialog->findChild<QCheckBox*>("profileSshEnabled");
    sshEnabled->setChecked(true);
    auto* sshHost = dialog->findChild<QLineEdit*>("profileSshHost");
    sshHost->setText("operator@bastion");
    save->click();
    QVERIFY(errorFor(sshHost).contains("SSH host"));
    sshHost->setText("bastion");
    save->click();
    QVERIFY(errorFor(dialog->findChild<QLineEdit*>("profileSshUser")).contains("SSH username"));
    QCOMPARE(saved.count(), 0);
    QCOMPARE(failed.count(), 0);
    sshEnabled->setChecked(false);
    host->setText("::1");
    dialog->findChild<QSpinBox*>("profilePort")->setValue(1);
    save->click();
    QTRY_COMPARE(saved.count(), 1);
    QTRY_VERIFY(save->isEnabled());
    QCOMPARE(submitted.count(), 0);
}

void WorkspaceTest::mysqlConnectionFormExplainsOptionalDatabaseAndPreservesLiterals() {
    WorkspaceFixture f;
    QTRY_COMPARE(f.workspace.liveConnections().size(), 1);
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
    QVERIFY(dialog);
    auto* save = dialog->findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    auto* driver = dialog->findChild<QComboBox*>("profileDriver");
    auto* database = dialog->findChild<QLineEdit*>("profileDatabase");
    auto* databaseHelp = dialog->findChild<QLabel*>("profileDatabaseHelp");
    QVERIFY(databaseHelp);
    driver->setCurrentIndex(1);
    QVERIFY(databaseHelp->isVisibleTo(dialog));
    QCOMPARE(databaseHelp->text(), QString("If empty, the database name is the username."));
    QVERIFY(database->placeholderText().isEmpty());
    driver->setCurrentIndex(2);
    QCOMPARE(databaseHelp->text(), QString("If empty, the connection opens no database."));
    QVERIFY(database->placeholderText().isEmpty());
    driver->setCurrentIndex(0);
    QVERIFY(!databaseHelp->isVisibleTo(dialog));
    driver->setCurrentIndex(2);
    dialog->findChild<QLineEdit*>("profileName")->setText("MySQL server");
    dialog->findChild<QLineEdit*>("profileHost")->setText("[::1]");
    dialog->findChild<QLineEdit*>("profileUser")->setText(" operator ");
    auto* password = dialog->findChild<QLineEdit*>("profilePassword");
    password->setText(" p@ss :/word ");
    password->setModified(true);
    QSignalSpy saved(f.workspace.adapter(), &choscordb::EngineAdapter::profileSaved);
    save->click();
    QTRY_COMPARE(saved.count(), 1);
    QTRY_VERIFY(save->isEnabled());
    auto profile = qvariant_cast<choscordb::SavedProfile>(saved.at(0).at(1));
    QCOMPARE(profile.driver, QString("mysql"));
    QCOMPARE(profile.tls, QString("prefer"));
    QVERIFY(profile.database.isEmpty());
    QCOMPARE(profile.host, QString("[::1]"));
    QCOMPARE(profile.user, QString(" operator "));
    QCOMPARE(password->text(), QString(" p@ss :/word "));
    database->setText(" literal database ");
    save->click();
    QTRY_COMPARE(saved.count(), 2);
    QTRY_VERIFY(save->isEnabled());
    QSignalSpy listed(f.workspace.adapter(), &choscordb::EngineAdapter::profilesReady);
    f.workspace.adapter()->listProfiles(991);
    QTRY_COMPARE(listed.count(), 1);
    const auto profiles = qvariant_cast<QList<choscordb::SavedProfile>>(listed.at(0).at(1));
    QCOMPARE(profiles.size(), 1);
    QCOMPARE(profiles[0].database, QString(" literal database "));
    QCOMPARE(profiles[0].user, QString(" operator "));
}

void WorkspaceTest::connectionManualPasswordIsOptionalAndUsesOneCredentialChoice() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    choscordb::EngineAdapter adapter(nullptr, directory.filePath("metadata.sqlite"));
    choscordb::ProfileDialog dialog(&adapter);
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    auto* credentials = dialog.findChild<QCheckBox*>("profileSaveCredentials");
    QVERIFY(credentials);
    QVERIFY(!credentials->isChecked());
    QVERIFY(!dialog.findChild<QComboBox*>("profileAuthentication"));
    for (const auto* name :
         {"profileRememberPassword", "profileRememberSshSecret", "profileRememberTlsSecret",
          "profileRememberProxySecret", "profileSshPrivateKeyRemember", "profileSshHopRemember"})
        QVERIFY2(!dialog.findChild<QCheckBox*>(name), name);
    for (const auto* name :
         {"profilePgPassFile", "profilePgPassHostname", "profilePasswordCommandDirectory"})
        QVERIFY2(!dialog.findChild<QLineEdit*>(name), name);
    QVERIFY(!dialog.findChild<QPlainTextEdit*>("profilePasswordCommand"));
    QVERIFY(!dialog.findChild<QSpinBox*>("profilePasswordCommandTimeout"));
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog.findChild<QLineEdit*>("profileName")->setText("Passwordless PostgreSQL");
    dialog.findChild<QLineEdit*>("profileUser")->setText("operator");
    auto* password = dialog.findChild<QLineEdit*>("profilePassword");
    QVERIFY(password->text().isEmpty());
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::profileSaved);
    save->click();
    QTRY_COMPARE(saved.count(), 1);
    QTRY_VERIFY(save->isEnabled());
    auto profile = qvariant_cast<choscordb::SavedProfile>(saved.at(0).at(1));
    QCOMPARE(profile.tls, QString("prefer"));
    QVERIFY(profile.credentialRef.isEmpty());
    password->setText("session-only-secret");
    password->setModified(true);
    save->click();
    QTRY_COMPARE(saved.count(), 2);
    QTRY_VERIFY(save->isEnabled());
    profile = qvariant_cast<choscordb::SavedProfile>(saved.at(1).at(1));
    QVERIFY(profile.credentialRef.isEmpty());
    QCOMPARE(password->text(), QString("session-only-secret"));
    QSignalSpy listed(&adapter, &choscordb::EngineAdapter::profilesReady);
    adapter.listProfiles(413);
    QList<QVariant> requestedReply;
    QTRY_VERIFY([&] {
        int matches = 0;
        for (const auto& reply : listed)
            if (reply.at(0).toULongLong() == 413) {
                requestedReply = reply;
                ++matches;
            }
        return matches == 1;
    }());
    const auto profiles = qvariant_cast<QList<choscordb::SavedProfile>>(requestedReply.at(1));
    QCOMPARE(profiles.size(), 1);
    QVERIFY(profiles.front().credentialRef.isEmpty());
    dialog.findChild<QSpinBox*>("profilePort")->setValue(1);
    QSignalSpy submitted(&dialog, &choscordb::ProfileDialog::connectionSubmitted);
    auto* saveConnect = dialog.findChild<QPushButton*>("profileSaveConnect");
    QTRY_VERIFY(saveConnect->isEnabled());
    saveConnect->click();
    QTRY_COMPARE(saved.count(), 3);
    QTRY_COMPARE(submitted.count(), 1);
    QCOMPARE(password->text(), QString("session-only-secret"));
}
void WorkspaceTest::connectionEditedEmptyPasswordSuppressesSavedReference() {
    WorkspaceFixture f;
    QTRY_COMPARE(f.workspace.liveConnections().size(), 1);
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
    QVERIFY(dialog);
    auto* save = dialog->findChild<QPushButton*>("profileSave");
    auto* password = dialog->findChild<QLineEdit*>("profilePassword");
    auto* credentials = dialog->findChild<QCheckBox*>("profileSaveCredentials");
    QTRY_VERIFY(save->isEnabled());
    choscordb::SavedProfile profile;
    profile.id = "edited-empty-password";
    profile.name = "Edited password draft";
    profile.driver = "postgres";
    profile.host = "localhost";
    profile.port = 1;
    profile.database = "app";
    profile.user = "operator";
    profile.tls = "disable";
    profile.credentialRef = "prior-secret-reference";
    credentials->setChecked(true);
    password->setText("replacement-secret");
    password->setModified(true);
    dialog->saveDraft(profile);
    QTRY_VERIFY(save->isEnabled());
    QVERIFY(dialog->findChild<QLabel*>("profileStatus")
                ->text()
                .contains("unavailable", Qt::CaseInsensitive));
    password->clear();
    password->setModified(true);
    QSignalSpy submitted(dialog, &choscordb::ProfileDialog::connectionSubmitted);
    dialog->findChild<QPushButton*>("profileConnect")->click();
    QTRY_COMPARE(submitted.count(), 1);
    const auto transient = qvariant_cast<choscordb::SavedProfile>(submitted.at(0).at(0));
    QVERIFY(transient.credentialRef.isEmpty());
    QVERIFY(password->text().isEmpty());
}
void WorkspaceTest::connectionProxyControlsAreHidden() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    QVERIFY(!dialog.findChild<QCheckBox*>("profileProxyEnabled"));
    QVERIFY(!dialog.findChild<QLineEdit*>("profileProxyHost"));
}

void WorkspaceTest::connectionProxyControlsStayHiddenForServerDriver() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    QVERIFY(!dialog.findChild<QCheckBox*>("profileProxyEnabled"));
}

void WorkspaceTest::connectionForwardingControlsAreHidden() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog.findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    for (const char* name : {"profileSshRemoteHost", "profileSshLocalHost"})
        QVERIFY2(!dialog.findChild<QLineEdit*>(name), name);
    QVERIFY(!dialog.findChild<QSpinBox*>("profileSshRemotePort"));
}

void WorkspaceTest::switchingToAServerDriverKeepsEveryFieldInsideTheScrolledForm() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    dialog.show();
    QTRY_VERIFY(dialog.findChild<QPushButton*>("profileDriverSqlite")->isEnabled());
    dialog.findChild<QPushButton*>("profileDriverSqlite")->click();
    QCoreApplication::processEvents();
    dialog.findChild<QPushButton*>("profileDriverPostgres")->click();
    QTRY_VERIFY(dialog.findChild<QWidget*>("profileHost")->isVisible());
    QCoreApplication::processEvents();
    auto* scroll = dialog.findChild<QScrollArea*>("profileFormScroll");
    QVERIFY(scroll);
    auto* form = scroll->widget();
    for (const auto* name : {"profileHost", "profileDatabase", "profileUser", "profilePassword"}) {
        auto* field = dialog.findChild<QWidget*>(name);
        QVERIFY2(field && field->isVisible(), name);
        const QRect inForm(field->mapTo(form, QPoint()), field->size());
        QVERIFY2(form->rect().contains(inForm), qPrintable(QString("%1 bottom %2 > form %3")
                                                               .arg(name)
                                                               .arg(inForm.bottom())
                                                               .arg(form->height())));
    }
}

void WorkspaceTest::scrolledConnectionFormEndsAtItsLastVisibleRow() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    dialog.resize(dialog.width(), 520);
    dialog.show();
    QTRY_VERIFY(dialog.findChild<QPushButton*>("profileDriverPostgres")->isEnabled());
    dialog.findChild<QPushButton*>("profileDriverPostgres")->click();
    QTRY_VERIFY(dialog.findChild<QWidget*>("profileHost")->isVisible());
    auto* ssh = dialog.findChild<QCheckBox*>("profileSshEnabled");
    ssh->click();
    QTRY_VERIFY(dialog.findChild<QWidget*>("profileSshHost")->isVisible());
    auto* scroll = dialog.findChild<QScrollArea*>("profileFormScroll");
    QVERIFY(scroll);
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    QCoreApplication::processEvents();
    ssh->click();
    QTRY_VERIFY(!dialog.findChild<QWidget*>("profileSshHost")->isVisible());
    QCoreApplication::processEvents();
    auto* form = scroll->widget();
    int lastBottom = 0;
    for (auto* child : form->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
        if (child->isVisibleTo(form))
            lastBottom = qMax(lastBottom, child->geometry().bottom() + 1);
    }
    QVERIFY2(scroll->verticalScrollBar()->maximum() > 0, "form must scroll at this height");
    QTRY_COMPARE(form->height(), lastBottom + form->layout()->contentsMargins().bottom());
    auto* body = scroll->parentWidget();
    QCOMPARE(scroll->y(), 0);
    QCOMPARE(scroll->geometry().bottom() + 1, body->height());
    const auto padding = form->layout()->contentsMargins();
    QVERIFY(padding.top() > 0 && padding.bottom() > 0);
}

void WorkspaceTest::connectionFormDrawsOnTheDialogSurface() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    dialog.findChild<QPushButton*>("profileDriverPostgres")->click();
    dialog.show();
    QCoreApplication::processEvents();
    auto* scroll = dialog.findChild<QScrollArea*>("profileFormScroll");
    QVERIFY(scroll);
    QVERIFY(!scroll->viewport()->autoFillBackground());
    QVERIFY(!scroll->widget()->testAttribute(Qt::WA_StyledBackground));
    QVERIFY(!scroll->widget()->autoFillBackground());
}

void WorkspaceTest::driverChoicesAreSquareLogoButtonsWithTheNameOnHover() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    dialog.show();
    QCoreApplication::processEvents();
    int previousRight = -1;
    for (const auto& [object, name] :
         {std::pair{"profileDriverPostgres", "PostgreSQL"},
          std::pair{"profileDriverMysql", "MySQL"}, std::pair{"profileDriverMariadb", "MariaDB"},
          std::pair{"profileDriverSqlite", "SQLite"}}) {
        auto* button = dialog.findChild<choscordb::design::Button*>(object);
        QVERIFY2(button && button->isVisible(), object);
        QVERIFY(button->text().isEmpty());
        QCOMPARE(button->toolTip(), QString(name));
        QCOMPARE(button->accessibleName(), QString(name));
        QCOMPARE(button->width(), button->height());
        QVERIFY(button->height() >
                choscordb::design::dimension(choscordb::design::Dimension::Logo));
        QVERIFY(button->geometry().left() > previousRight);
        previousRight = button->geometry().right();
    }
}
