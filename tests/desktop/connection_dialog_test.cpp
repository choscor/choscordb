#include "bridge/engine_adapter.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "workspace_test.h"
#include "workspace_test_fixture.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>


void WorkspaceTest::connectionSshFormShowsOnlyBasicSettings() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    QCOMPARE(dialog.size(), QSize(560, 440));
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog.findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    for (const char* name : {"profileSshHost", "profileSshUser"})
        QVERIFY(dialog.findChild<QLineEdit*>(name)->isVisibleTo(&dialog));
    for (const char* name : {"profileSshRemoteHost", "profileSshLocalHost",
                             "profileSshAgentSocket", "profileSshKnownHosts",
                             "profileProxyHost"})
        QVERIFY(!dialog.findChild<QLineEdit*>(name)->isVisibleTo(&dialog));
    QVERIFY(!dialog.findChild<QSpinBox*>("profileSshLocalPort")->isVisibleTo(&dialog));
    QVERIFY(!dialog.findChild<QCheckBox*>("profileProxyEnabled")->isVisibleTo(&dialog));
}

void WorkspaceTest::connectionIdentityCredentialErrorsPreserveDraft() {
    WorkspaceFixture f;
    QTRY_COMPARE(f.connections.count(), 1);
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
    auto* save = dialog->findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    dialog->findChild<QLineEdit*>("profileName")->setText("Identity draft");
    dialog->findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog->findChild<QLineEdit*>("profileUser")->setText("operator");
    auto* identity = dialog->findChild<QLineEdit*>("profileTlsClientIdentity");
    identity->setText("/missing/client.p12");
    auto* secret = dialog->findChild<QLineEdit*>("profileTlsSecret");
    const auto oversized = QString(16384, QChar(0x00e9));
    secret->setText(oversized);
    secret->setModified(true);
    dialog->findChild<QCheckBox*>("profileRememberTlsSecret")->setChecked(true);
    auto* status = dialog->findChild<QLabel*>("profileStatus");
    QSignalSpy saved(f.workspace.adapter(), &choscordb::EngineAdapter::profileSaved);
    QSignalSpy submitted(dialog, &choscordb::ProfileDialog::connectionSubmitted);
    for (const auto* action : {"profileSave", "profileTest", "profileConnect"}) {
        dialog->findChild<QPushButton*>(action)->click();
        QTRY_VERIFY(save->isEnabled());
        QVERIFY2(status->text().contains("Credential exceeds"), qPrintable(status->text()));
        QCOMPARE(secret->text(), oversized);
        QVERIFY(secret->isModified());
        QCOMPARE(identity->text(), QString("/missing/client.p12"));
    }
    QCOMPARE(saved.count(), 0);
    QCOMPARE(submitted.count(), 0);
}

void WorkspaceTest::connectionSshFormKeepsBasicFields() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog.findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    QCOMPARE(dialog.findChild<QSpinBox*>("profileSshPort")->minimum(), 1);
    QCOMPARE(dialog.findChild<QSpinBox*>("profileSshPort")->maximum(), 65535);
    QVERIFY(dialog.findChild<QComboBox*>("profileSshAuthentication")->isVisibleTo(&dialog));
    QVERIFY(!dialog.findChild<QWidget*>("profileSshHopList")->isVisibleTo(&dialog));
}

void WorkspaceTest::connectionTransportValidationRejectsIncompatibleSettings() {
    WorkspaceFixture f;
    QTRY_COMPARE(f.connections.count(), 1);
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
    auto* tls = dialog->findChild<QComboBox*>("profileTls");
    auto* status = dialog->findChild<QLabel*>("profileStatus");
    QSignalSpy saved(f.workspace.adapter(), &choscordb::EngineAdapter::profileSaved);
    QSignalSpy failed(f.workspace.adapter(), &choscordb::EngineAdapter::profileFailed);
    save->click();
    QVERIFY(status->text().contains("Unix"));
    QVERIFY(status->text().contains("TLS"));
    tls->setCurrentIndex(tls->findData("disable"));
    dialog->findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    save->click();
    QVERIFY(status->text().contains("Unix"));
    QVERIFY(status->text().contains("SSH"));
    dialog->findChild<QCheckBox*>("profileSshEnabled")->setChecked(false);
    host->setText("localhost");
    driver->setCurrentIndex(2);
    tls->setCurrentIndex(tls->findData("prefer"));
    save->click();
    QVERIFY(status->text().contains("MySQL"));
    QVERIFY(status->text().contains("Prefer"));
    driver->setCurrentIndex(1);
    tls->setCurrentIndex(tls->findData("verify_full"));
    dialog->findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    dialog->findChild<QLineEdit*>("profileSshHost")->setText("bastion.example");
    dialog->findChild<QLineEdit*>("profileSshUser")->setText("operator");
    QCOMPARE(saved.count(), 0);
    QCOMPARE(failed.count(), 0);
}

void WorkspaceTest::connectionFormRejectsMalformedHostsBeforeSubmission() {
    WorkspaceFixture f;
    QTRY_COMPARE(f.connections.count(), 1);
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
    QVERIFY(dialog);
    auto* save = dialog->findChild<QPushButton*>("profileSave");
    auto* status = dialog->findChild<QLabel*>("profileStatus");
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
            QVERIFY2(status->text().contains("Host"), qPrintable(status->text()));
            QVERIFY(status->text().contains("port"));
            QVERIFY(status->isVisible());
        }
    }
    QCOMPARE(saved.count(), 0);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(submitted.count(), 0);
    host->setText("localhost");
    auto* user = dialog->findChild<QLineEdit*>("profileUser");
    user->clear();
    save->click();
    QVERIFY(status->text().contains("database username"));
    user->setText("operator");
    auto* database = dialog->findChild<QLineEdit*>("profileDatabase");
    database->clear();
    // An omitted PostgreSQL database now defaults to the explicit username.
    database->setText("postgres");
    auto* sshEnabled = dialog->findChild<QCheckBox*>("profileSshEnabled");
    sshEnabled->setChecked(true);
    auto* sshHost = dialog->findChild<QLineEdit*>("profileSshHost");
    sshHost->setText("operator@bastion");
    save->click();
    QVERIFY(status->text().contains("SSH host"));
    sshHost->setText("bastion");
    save->click();
    QVERIFY(status->text().contains("SSH username"));
    QCOMPARE(saved.count(), 0);
    QCOMPARE(failed.count(), 0);
    sshEnabled->setChecked(false);
    // A rejected Save & connect must not turn a later Save into a connection.
    host->setText("::1");
    dialog->findChild<QSpinBox*>("profilePort")->setValue(1);
    save->click();
    QTRY_COMPARE(saved.count(), 1);
    QTRY_VERIFY(save->isEnabled());
    QCOMPARE(submitted.count(), 0);
}

void WorkspaceTest::mysqlConnectionFormExplainsOptionalDatabaseAndPreservesLiterals() {
    WorkspaceFixture f;
    QTRY_COMPARE(f.connections.count(), 1);
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
    QVERIFY(dialog);
    auto* save = dialog->findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    auto* driver = dialog->findChild<QComboBox*>("profileDriver");
    auto* database = dialog->findChild<QLineEdit*>("profileDatabase");
    driver->setCurrentIndex(1);
    QVERIFY(database->placeholderText().contains("username"));
    driver->setCurrentIndex(2);
    QVERIFY(database->placeholderText().contains("Optional"));
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

void WorkspaceTest::connectionAuthenticationProvidersRoundTripAndIgnoreManualPasswords() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    choscordb::EngineAdapter adapter(nullptr, directory.filePath("metadata.sqlite"));
    choscordb::ProfileDialog dialog(&adapter);
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    auto* authentication = dialog.findChild<QComboBox*>("profileAuthentication");
    QVERIFY(authentication);
    auto* driver = dialog.findChild<QComboBox*>("profileDriver");
    driver->setCurrentIndex(1);
    dialog.findChild<QLineEdit*>("profileName")->setText("Provider profile");
    auto* user = dialog.findChild<QLineEdit*>("profileUser");
    user->setText("operator");
    dialog.findChild<QSpinBox*>("profilePort")->setValue(1);
    auto* password = dialog.findChild<QLineEdit*>("profilePassword");
    auto* remember = dialog.findChild<QCheckBox*>("profileRememberPassword");
    password->setText("manual credential");
    password->setModified(true);
    remember->setChecked(true);
    authentication->setCurrentIndex(authentication->findData("command"));
    QVERIFY(password->text().isEmpty());
    QVERIFY(!password->isEnabled());
    QVERIFY(!remember->isEnabled());
    auto* command = dialog.findChild<QPlainTextEdit*>("profilePasswordCommand");
    const auto commandText = QStringLiteral("printf '%s%s' private marker; exit 9");
    command->setPlainText(commandText);
    dialog.findChild<QLineEdit*>("profilePasswordCommandDirectory")->setText(directory.path());
    dialog.findChild<QSpinBox*>("profilePasswordCommandTimeout")->setValue(7);
    // Even stale values in disabled controls must never override the selected provider.
    const auto stale = QString(16384, QChar(0x00e9));
    password->setText(stale);
    password->setModified(true);
    remember->setChecked(true);
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::profileSaved);
    save->click();
    QTRY_COMPARE(saved.count(), 1);
    QTRY_VERIFY(save->isEnabled());
    auto profile = qvariant_cast<choscordb::SavedProfile>(saved.at(0).at(1));
    auto settings = QJsonDocument::fromJson(profile.authentication.toUtf8()).object();
    QCOMPARE(settings["method"].toString(), QString("command"));
    QCOMPARE(settings["command"].toString(), commandText);
    QCOMPARE(settings["working_directory"].toString(), directory.path());
    QCOMPARE(settings["timeout_seconds"].toInt(), 7);
    QVERIFY(profile.credentialRef.isEmpty());
    auto* status = dialog.findChild<QLabel*>("profileStatus");
    for (const auto* action : {"profileTest", "profileConnect"}) {
        password->setText(stale);
        password->setModified(true);
        dialog.findChild<QPushButton*>(action)->click();
        QTRY_VERIFY(save->isEnabled());
        QVERIFY2(status->text().contains("Password command failed"), qPrintable(status->text()));
        QVERIFY(!status->text().contains("privatemarker"));
        QCOMPARE(command->toPlainText(), commandText);
    }
    for (const auto& name : QDir(directory.path()).entryList(QDir::Files)) {
        QFile file(directory.filePath(name));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("privatemarker"));
    }
    authentication->setCurrentIndex(authentication->findData("pg_pass"));
    user->clear();
    auto* passfileHost = dialog.findChild<QLineEdit*>("profilePgPassHostname");
    QVERIFY(passfileHost);
    passfileHost->setText("password-file.example");
    const auto passfile = directory.filePath("missing.pass");
    dialog.findChild<QLineEdit*>("profilePgPassFile")->setText(passfile);
    save->click();
    QTRY_COMPARE(saved.count(), 2);
    QTRY_VERIFY(save->isEnabled());
    profile = qvariant_cast<choscordb::SavedProfile>(saved.at(1).at(1));
    QVERIFY(profile.user.isEmpty());
    settings = QJsonDocument::fromJson(profile.authentication.toUtf8()).object();
    QCOMPARE(settings["method"].toString(), QString("pg_pass"));
    QCOMPARE(settings["path"].toString(), passfile);
    QCOMPARE(settings["hostname"].toString(), QString("password-file.example"));
    dialog.findChild<QPushButton*>("profileTest")->click();
    QTRY_VERIFY(save->isEnabled());
    QVERIFY2(status->text().contains("passfile"), qPrintable(status->text()));
    QCOMPARE(dialog.findChild<QLineEdit*>("profilePgPassFile")->text(), passfile);
    driver->setCurrentIndex(2);
    save->click();
    QVERIFY(save->isEnabled());
    QVERIFY(status->text().contains("connection options"));
    QCOMPARE(saved.count(), 2);
    authentication->setCurrentIndex(authentication->findData("password"));
    QCOMPARE(authentication->currentData().toString(), QString("password"));
    QVERIFY(password->isEnabled());
    QVERIFY(remember->isEnabled());
    authentication->setCurrentIndex(authentication->findData("command"));
    driver->setCurrentIndex(0);
    dialog.findChild<QLineEdit*>("profilePath")->setText(":memory:");
    save->click();
    QTRY_COMPARE(saved.count(), 3);
    QTRY_VERIFY(save->isEnabled());
    profile = qvariant_cast<choscordb::SavedProfile>(saved.at(2).at(1));
    QCOMPARE(profile.driver, QString("sqlite"));
    QCOMPARE(QJsonDocument::fromJson(profile.authentication.toUtf8()).object()["method"].toString(),
             QString("password"));
}
void WorkspaceTest::connectionProxyControlsAreHidden() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    QVERIFY(!dialog.findChild<QCheckBox*>("profileProxyEnabled")->isVisibleTo(&dialog));
    QVERIFY(!dialog.findChild<QLineEdit*>("profileProxyHost")->isVisibleTo(&dialog));
}

void WorkspaceTest::connectionProxyControlsStayHiddenForServerDriver() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    QVERIFY(!dialog.findChild<QCheckBox*>("profileProxyEnabled")->isVisibleTo(&dialog));
}

void WorkspaceTest::connectionForwardingControlsAreHidden() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog.findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    for (const char* name : {"profileSshRemoteHost", "profileSshLocalHost"})
        QVERIFY(!dialog.findChild<QLineEdit*>(name)->isVisibleTo(&dialog));
    QVERIFY(!dialog.findChild<QSpinBox*>("profileSshRemotePort")->isVisibleTo(&dialog));
}
