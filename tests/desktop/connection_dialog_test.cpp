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

namespace {
void addJumpHost(choscordb::ProfileDialog* dialog, const QString& host, int port,
                 const QString& user) {
    dialog->findChild<QPushButton*>("profileSshHopAdd")->click();
    dialog->findChild<QLineEdit*>("profileSshHopHost")->setText(host);
    dialog->findChild<QSpinBox*>("profileSshHopPort")->setValue(port);
    dialog->findChild<QLineEdit*>("profileSshHopUser")->setText(user);
}
} // namespace

void WorkspaceTest::connectionSecurityControlsExposeSupportedVariants() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog controls(&adapter);
    auto* dialog = &controls;
    dialog->findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    QVERIFY(
        dialog->findChild<QLineEdit*>("profileDatabase")->placeholderText().contains("username"));
    auto* tls = dialog->findChild<QComboBox*>("profileTls");
    for (const auto* mode : {"verify_full", "verify_ca", "require", "prefer", "disable"})
        QVERIFY(tls->findData(mode) >= 0);
    auto* tlsSecret = dialog->findChild<QLineEdit*>("profileTlsSecret");
    QVERIFY(tlsSecret);
    QCOMPARE(tlsSecret->echoMode(), QLineEdit::Password);
    QVERIFY(!dialog->findChild<QLineEdit*>("profileConnectionUrl"));
    QVERIFY(!dialog->findChild<QPushButton*>("profileImportUrl"));
    for (const auto* driver : {"sqlite", "postgres", "mysql"}) {
        auto* selector = dialog->findChild<QComboBox*>("profileDriver");
        selector->setCurrentIndex(selector->findData(driver));
        for (const auto* section : {"profileBootstrapToggle", "profileHooksToggle",
                                    "profileTemplatesToggle", "profilePropertiesToggle"})
            QVERIFY2(!dialog->findChild<QPushButton*>(section), section);
    }
    QVERIFY(dialog->findChild<QLineEdit*>("profileTlsClientIdentity"));
    QCOMPARE(dialog->findChild<QSpinBox*>("profileSshTimeout")->value(), 15);
    QCOMPARE(dialog->findChild<QSpinBox*>("profileSshKeepalive")->value(), 0);
    QCOMPARE(dialog->findChild<QSpinBox*>("profileSshKeepaliveCount")->value(), 3);
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

void WorkspaceTest::connectionSecurityDraftRoundTripsWithoutPersistingSecrets() {
    WorkspaceFixture f;
    QTRY_COMPARE(f.connections.count(), 1);
    f.newConnection.trigger();
    auto* dialog = f.parent.findChild<choscordb::ProfileDialog*>("profileDialog");
    auto* save = dialog->findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    dialog->findChild<QLineEdit*>("profileName")->setText("Advanced profile");
    dialog->findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog->findChild<QLineEdit*>("profileUser")->setText("operator");
    auto* tls = dialog->findChild<QComboBox*>("profileTls");
    tls->setCurrentIndex(tls->findData("verify_ca"));
    dialog->findChild<QLineEdit*>("profileTlsClientIdentity")->setText("/tmp/client identity.p12");
    auto* secret = dialog->findChild<QLineEdit*>("profileTlsSecret");
    secret->setText(" distinct identity secret ");
    secret->setModified(true);
    dialog->findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    dialog->findChild<QLineEdit*>("profileSshHost")->setText("bastion.example");
    dialog->findChild<QLineEdit*>("profileSshUser")->setText("operator");
    dialog->findChild<QSpinBox*>("profileSshTimeout")->setValue(47);
    dialog->findChild<QSpinBox*>("profileSshKeepalive")->setValue(19);
    dialog->findChild<QSpinBox*>("profileSshKeepaliveCount")->setValue(7);
    dialog->findChild<QLineEdit*>("profileSshAgentSocket")->setText("/tmp/agent socket");
    dialog->findChild<QLineEdit*>("profileSshKnownHosts")->setText("/tmp/known hosts");
    addJumpHost(dialog, "first.example", 2201, "jump");
    addJumpHost(dialog, "::1", 22, "second");
    QSignalSpy saved(f.workspace.adapter(), &choscordb::EngineAdapter::profileSaved);
    save->click();
    QTRY_COMPARE(saved.count(), 1);
    QTRY_VERIFY(save->isEnabled());
    const auto profile = qvariant_cast<choscordb::SavedProfile>(saved.at(0).at(1));
    QVERIFY(profile.database.isEmpty());
    QCOMPARE(profile.tls, QString("verify_ca"));
    QCOMPARE(profile.tlsClientIdentity, QString("/tmp/client identity.p12"));
    QVERIFY(profile.tlsCredentialRef.isEmpty());
    const auto options = QJsonDocument::fromJson(profile.sshOptions.toUtf8()).object();
    QCOMPARE(options["connect_timeout_seconds"].toInt(), 47);
    QCOMPARE(options["server_alive_interval_seconds"].toInt(), 19);
    QCOMPARE(options["server_alive_count_max"].toInt(), 7);
    QCOMPARE(options["agent_socket"].toString(), QString("/tmp/agent socket"));
    QCOMPARE(options["known_hosts_file"].toString(), QString("/tmp/known hosts"));
    const auto jumps = options["jump_hosts"].toArray();
    QCOMPARE(jumps.size(), 2);
    QCOMPARE(jumps[0].toObject()["host"].toString(), QString("first.example"));
    QCOMPARE(jumps[0].toObject()["port"].toInt(), 2201);
    QCOMPARE(jumps[1].toObject()["host"].toString(), QString("::1"));
    QCOMPARE(secret->text(), QString(" distinct identity secret "));
    QVERIFY(secret->isModified());
    QVERIFY(!profile.sshOptions.contains("distinct identity secret"));
    dialog->findChild<QSpinBox*>("profileSshTimeout")->setValue(15);
    dialog->selectProfile(profile.id);
    QCOMPARE(dialog->findChild<QSpinBox*>("profileSshTimeout")->value(), 47);
    QCOMPARE(dialog->findChild<QListWidget*>("profileSshHopList")->count(), 2);
    dialog->findChild<QListWidget*>("profileSshHopList")->setCurrentRow(1);
    QCOMPARE(dialog->findChild<QLineEdit*>("profileSshHopHost")->text(), QString("::1"));
    QCOMPARE(dialog->findChild<QLineEdit*>("profileSshHopUser")->text(), QString("second"));
    QVERIFY(secret->text().isEmpty());
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
    auto* jumps = dialog->findChild<QListWidget*>("profileSshHopList");
    addJumpHost(dialog, "valid.example", 22, "operator");
    auto* jumpHost = dialog->findChild<QLineEdit*>("profileSshHopHost");
    for (const auto* value : {"missing endpoint", "host:22", "[not-ip]", "https://host"}) {
        jumpHost->setText(value);
        for (const auto* action : {"profileSave", "profileTest", "profileConnect"}) {
            dialog->findChild<QPushButton*>(action)->click();
            QVERIFY2(save->isEnabled(), value);
            QVERIFY2(status->text().contains("Invalid", Qt::CaseInsensitive), value);
        }
    }
    jumpHost->setText("valid.example");
    dialog->findChild<QLineEdit*>("profileSshHopUser")->setText("bad user");
    save->click();
    QVERIFY(status->text().contains("Invalid", Qt::CaseInsensitive));
    dialog->findChild<QLineEdit*>("profileSshHopUser")->setText("operator");
    auto* jumpPort = dialog->findChild<QSpinBox*>("profileSshHopPort");
    QCOMPARE(jumpPort->minimum(), 1);
    QCOMPARE(jumpPort->maximum(), 65535);
    for (int i = 1; i < 5; ++i)
        addJumpHost(dialog, "hop.example", 22, "operator");
    QVERIFY(!dialog->findChild<QPushButton*>("profileSshHopAdd")->isEnabled());
    QCOMPARE(jumps->count(), 5);
    while (jumps->count())
        dialog->findChild<QPushButton*>("profileSshHopRemove")->click();
    auto* knownHosts = dialog->findChild<QLineEdit*>("profileSshKnownHosts");
    knownHosts->setText("/tmp/invalid\"hosts");
    save->click();
    QVERIFY(status->text().contains("SSH paths"));
    knownHosts->clear();
    addJumpHost(dialog, "host", 22, "u");
    dialog->findChild<QComboBox*>("profileSshAuthentication")->setCurrentIndex(2);
    save->click();
    QVERIFY(status->text().contains("password"));
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
void WorkspaceTest::connectionJumpHostsAllowExplicitPassphraseClearing() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    choscordb::SavedProfile profile;
    profile.id = "clear-jump-key";
    profile.name = "Unencrypted jump key";
    profile.driver = "postgres";
    profile.host = "localhost";
    profile.user = "operator";
    profile.sshEnabled = true;
    profile.sshHost = "bastion";
    profile.sshUser = "operator";
    profile.sshAuthentication = "public_key";
    profile.sshIdentityFile = "/unused/private-key";
    profile.sshCredentialRef = "stale-saved-passphrase";
    profile.sshOptions = R"({"jump_hosts":[{"host":"jump","port":22,"user":"operator"}]})";
    addJumpHost(&dialog, "jump", 22, "operator");
    auto* secret = dialog.findChild<QLineEdit*>("profileSshSecret");
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::profileSaved);
    QSignalSpy failed(&adapter, &choscordb::EngineAdapter::profileFailed);
    secret->setText("new nonempty passphrase");
    secret->setModified(true);
    dialog.saveDraft(profile);
    QTRY_COMPARE(saved.count(), 1);
    QTRY_VERIFY(save->isEnabled());
    QCOMPARE(secret->text(), QString("new nonempty passphrase"));
    secret->clear();
    secret->setModified(true);
    dialog.findChild<QCheckBox*>("profileRememberSshSecret")->setChecked(true);
    dialog.saveDraft(profile);
    QTRY_COMPARE(saved.count(), 2);
    QTRY_VERIFY(save->isEnabled());
    QVERIFY(qvariant_cast<choscordb::SavedProfile>(saved.at(1).at(1)).sshCredentialRef.isEmpty());
    QCOMPARE(failed.count(), 0);
}
void WorkspaceTest::connectionProxySettingsRoundTripWithoutPersistingSecrets() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    auto* enabled = dialog.findChild<QCheckBox*>("profileProxyEnabled");
    QVERIFY(enabled);
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog.findChild<QLineEdit*>("profileName")->setText("Proxy profile");
    dialog.findChild<QLineEdit*>("profileUser")->setText("operator");
    enabled->setChecked(true);
    dialog.findChild<QLineEdit*>("profileProxyHost")->setText("proxy.example");
    dialog.findChild<QLineEdit*>("profileProxyUser")->setText("literal@user");
    auto* secret = dialog.findChild<QLineEdit*>("profileProxySecret");
    QCOMPARE(secret->echoMode(), QLineEdit::Password);
    secret->setText("session-only proxy secret");
    secret->setModified(true);
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::profileSaved);
    save->click();
    QTRY_COMPARE(saved.count(), 1);
    QTRY_VERIFY(save->isEnabled());
    const auto profile = qvariant_cast<choscordb::SavedProfile>(saved.at(0).at(1));
    const auto options = QJsonDocument::fromJson(profile.proxyOptions.toUtf8()).object();
    QCOMPARE(options["protocol"].toString(), QString("socks5"));
    QCOMPARE(options["host"].toString(), QString("proxy.example"));
    QCOMPARE(options["username"].toString(), QString("literal@user"));
    QCOMPARE(options["port"].toInt(), 1080);
    QVERIFY(profile.proxyCredentialRef.isEmpty());
    QVERIFY(!profile.proxyOptions.contains(secret->text()));
    QCOMPARE(secret->text(), QString("session-only proxy secret"));
    QVERIFY(secret->isModified());
    dialog.findChild<QComboBox*>("profileProxyProtocol")->setCurrentIndex(1);
    QVERIFY(!secret->isEnabled());
    save->click();
    QTRY_COMPARE(saved.count(), 2);
    QTRY_VERIFY(save->isEnabled());
    const auto socks4 = qvariant_cast<choscordb::SavedProfile>(saved.at(1).at(1));
    QCOMPARE(QJsonDocument::fromJson(socks4.proxyOptions.toUtf8()).object()["protocol"].toString(),
             QString("socks4"));
    QVERIFY(socks4.proxyCredentialRef.isEmpty());
}

void WorkspaceTest::connectionProxyConflictsPreserveEditableDraft() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog.findChild<QLineEdit*>("profileName")->setText("Invalid proxy combination");
    dialog.findChild<QLineEdit*>("profileUser")->setText("operator");
    auto* enabled = dialog.findChild<QCheckBox*>("profileProxyEnabled");
    QVERIFY(enabled);
    enabled->setChecked(true);
    auto* host = dialog.findChild<QLineEdit*>("profileProxyHost");
    host->setText("https://invalid-proxy");
    auto* secret = dialog.findChild<QLineEdit*>("profileProxySecret");
    dialog.findChild<QLineEdit*>("profileProxyUser")->setText("operator");
    secret->setText("retain-on-error");
    secret->setModified(true);
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::profileSaved);
    save->click();
    QVERIFY(save->isEnabled());
    QCOMPARE(saved.count(), 0);
    QCOMPARE(host->text(), QString("https://invalid-proxy"));
    QCOMPARE(secret->text(), QString("retain-on-error"));
    host->setText("proxy.example");
    dialog.findChild<QLineEdit*>("profileHost")->setText("/tmp");
    auto* tls = dialog.findChild<QComboBox*>("profileTls");
    tls->setCurrentIndex(tls->findData("disable"));
    save->click();
    QVERIFY(save->isEnabled());
    QCOMPARE(saved.count(), 0);
    QVERIFY(dialog.findChild<QLabel*>("profileStatus")->text().contains("Invalid"));
    QCOMPARE(secret->text(), QString("retain-on-error"));
}

void WorkspaceTest::connectionSshForwardingOverridesRoundTrip() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    auto* remoteHost = dialog.findChild<QLineEdit*>("profileSshRemoteHost");
    auto* remotePort = dialog.findChild<QSpinBox*>("profileSshRemotePort");
    QVERIFY(remoteHost);
    QVERIFY(remotePort);
    QVERIFY(remoteHost->text().isEmpty());
    QCOMPARE(remotePort->value(), 0);
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog.findChild<QLineEdit*>("profileName")->setText("Forwarding override");
    dialog.findChild<QLineEdit*>("profileUser")->setText("operator");
    dialog.findChild<QLineEdit*>("profileHost")->setText("certificate.example");
    dialog.findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    dialog.findChild<QLineEdit*>("profileSshHost")->setText("bastion.example");
    dialog.findChild<QLineEdit*>("profileSshUser")->setText("operator");
    remoteHost->setText("::1");
    remotePort->setValue(15432);
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::profileSaved);
    save->click();
    QTRY_COMPARE(saved.count(), 1);
    QTRY_VERIFY(save->isEnabled());
    const auto profile = qvariant_cast<choscordb::SavedProfile>(saved.at(0).at(1));
    const auto options = QJsonDocument::fromJson(profile.sshOptions.toUtf8()).object();
    QCOMPARE(options["remote_host"].toString(), QString("::1"));
    QCOMPARE(options["remote_port"].toInt(), 15432);
    QCOMPARE(profile.host, QString("certificate.example"));
    QCOMPARE(remoteHost->text(), QString("::1"));
    QCOMPARE(remotePort->value(), 15432);
    remoteHost->setText("https://invalid");
    save->click();
    QVERIFY(save->isEnabled());
    QCOMPARE(saved.count(), 1);
    QCOMPARE(remoteHost->text(), QString("https://invalid"));
    remoteHost->clear();
    remotePort->setValue(0);
    save->click();
    QTRY_COMPARE(saved.count(), 2);
    const auto defaults =
        QJsonDocument::fromJson(
            qvariant_cast<choscordb::SavedProfile>(saved.at(1).at(1)).sshOptions.toUtf8())
            .object();
    QVERIFY(defaults["remote_host"].isNull() || defaults["remote_host"].isUndefined());
    QVERIFY(defaults["remote_port"].isNull() || defaults["remote_port"].isUndefined());
}
