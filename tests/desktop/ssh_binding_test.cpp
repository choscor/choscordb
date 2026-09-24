#include "choscordb-bridge/src/lib.rs.h"
#include "workspace_test.h"
#include "workspace_test_fixture.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSpinBox>

namespace {
void prepareBindingProfile(choscordb::ProfileDialog& dialog) {
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog.findChild<QLineEdit*>("profileName")->setText("Local SSH binding");
    dialog.findChild<QLineEdit*>("profileUser")->setText("operator");
    auto* tls = dialog.findChild<QComboBox*>("profileTls");
    tls->setCurrentIndex(tls->findData("disable"));
    dialog.findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    dialog.findChild<QLineEdit*>("profileSshHost")->setText("127.0.0.1");
    dialog.findChild<QLineEdit*>("profileSshUser")->setText("operator");
    dialog.findChild<QSpinBox*>("profileSshTimeout")->setValue(1);
}
} // namespace
void WorkspaceTest::sshLocalBindingSettingsRoundTripAndWarnForWildcard() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    auto* binding = dialog.findChild<QCheckBox*>("profileSshLocalBinding");
    auto* host = dialog.findChild<QLineEdit*>("profileSshLocalHost");
    auto* port = dialog.findChild<QSpinBox*>("profileSshLocalPort");
    auto* sharing = dialog.findChild<QCheckBox*>("profileSshShareTunnels");
    auto* warning = dialog.findChild<QLabel*>("profileSshLocalBindingWarning");
    QVERIFY(binding && host && port && sharing && warning);
    QVERIFY(!binding->isChecked());
    QVERIFY(!sharing->isChecked());
    QCOMPARE(port->value(), 0);
    prepareBindingProfile(dialog);
    binding->setChecked(true);
    host->setText("::");
    QVERIFY(!warning->isHidden());
    QVERIFY(warning->text().contains("all network interfaces"));
    host->setText("::0.0.0.0");
    QVERIFY(!warning->isHidden());
    host->setText("::1");
    QVERIFY(warning->isHidden());
    sharing->setChecked(true);
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::profileSaved);
    save->click();
    QTRY_COMPARE(saved.count(), 1);
    auto profile = qvariant_cast<choscordb::SavedProfile>(saved.first().at(1));
    auto options = QJsonDocument::fromJson(profile.sshOptions.toUtf8()).object();
    QCOMPARE(options["local_host"].toString(), QString("::1"));
    QCOMPARE(options["local_port"].toInt(-1), 0);
    QVERIFY(options["share_tunnels"].toBool());
    QTRY_VERIFY(save->isEnabled());
    QVERIFY(binding->isChecked());
    QCOMPARE(host->text(), QString("::1"));
    binding->setChecked(false);
    save->click();
    QTRY_COMPARE(saved.count(), 2);
    profile = qvariant_cast<choscordb::SavedProfile>(saved.last().at(1));
    options = QJsonDocument::fromJson(profile.sshOptions.toUtf8()).object();
    QVERIFY(options["local_host"].isNull() || options["local_host"].isUndefined());
    QVERIFY(options["local_port"].isNull() || options["local_port"].isUndefined());
    QVERIFY(options["share_tunnels"].toBool());
}

void WorkspaceTest::sshLocalBindConflictReachesTestAndConnectAndKeepsDraft() {
#ifndef Q_OS_UNIX
    QSKIP("Requires the owned Python local socket fixture");
#endif
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    auto* binding = dialog.findChild<QCheckBox*>("profileSshLocalBinding");
    auto* host = dialog.findChild<QLineEdit*>("profileSshLocalHost");
    auto* port = dialog.findChild<QSpinBox*>("profileSshLocalPort");
    QVERIFY(binding && host && port);
    prepareBindingProfile(dialog);
    binding->setChecked(true);
    host->setText("127.0.0.1");
    QProcess listener;
    listener.start("python3",
                   {"-u", "-c",
                    "import socket,time; s=socket.socket(); s.bind(('127.0.0.1',0)); s.listen(1); "
                    "print(s.getsockname()[1],flush=True); time.sleep(30)"});
    QVERIFY(listener.waitForStarted());
    QVERIFY(listener.waitForReadyRead());
    bool validPort = false;
    const auto reservedPort = QString::fromUtf8(listener.readLine()).trimmed().toUShort(&validPort);
    QVERIFY(validPort && reservedPort);
    port->setValue(reservedPort);
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::profileSaved);
    save->click();
    QTRY_COMPARE(saved.count(), 1);
    QTRY_VERIFY(save->isEnabled());
    QSignalSpy failed(&adapter, &choscordb::EngineAdapter::profileFailed);
    dialog.findChild<QPushButton*>("profileTest")->click();
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(failed.first().last().toString().contains("bind selected SSH local address"));
    QTRY_VERIFY(save->isEnabled());
    bool connectionFailed = false;
    connect(&adapter, &choscordb::EngineAdapter::eventReady, &dialog,
            [&](const choscordb::BridgeEvent& event) {
                if (event.kind == "connection_failed")
                    connectionFailed = true;
            });
    dialog.findChild<QPushButton*>("profileConnect")->click();
    QTRY_VERIFY(connectionFailed);
    QTRY_VERIFY(save->isEnabled());
    QCOMPARE(port->value(), int(reservedPort));
    QCOMPARE(host->text(), QString("127.0.0.1"));
    host->setText("not-an-ip");
    QSignalSpy submitted(&dialog, &choscordb::ProfileDialog::connectionSubmitted);
    for (const auto* action : {"profileSave", "profileTest", "profileConnect"}) {
        dialog.findChild<QPushButton*>(action)->click();
        QTRY_VERIFY(save->isEnabled());
        QCOMPARE(host->text(), QString("not-an-ip"));
    }
    QCOMPARE(saved.count(), 1);
    QCOMPARE(failed.count(), 1);
    QVERIFY(submitted.isEmpty());
    listener.kill();
    QVERIFY(listener.waitForFinished());
}
