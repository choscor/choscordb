#include "design_system/toast_region/toast_region.h"
#include "widgets/profile_dialog/ssh_host_key_dialog.h"
#include "workspace_test.h"
#include "workspace_test_fixture.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSpinBox>

namespace {
void prepareInspection(choscordb::ProfileDialog& dialog) {
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog.findChild<QLineEdit*>("profileName")->setText("Trust review fixture");
    dialog.findChild<QLineEdit*>("profileUser")->setText("database-user");
    dialog.findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    dialog.findChild<QLineEdit*>("profileSshHost")->setText("192.0.2.30");
    dialog.findChild<QLineEdit*>("profileSshUser")->setText("target-user");
}
} // namespace

void WorkspaceTest::sshHostKeyInspectionRequiresExplicitActionAndKeepsUiResponsive() {
#ifndef Q_OS_UNIX
    QSKIP("Requires Python TCP fixture");
#endif
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    auto* inspect = dialog.findChild<QPushButton*>("profileSshInspectHostKeys");
    auto* inspectHop = dialog.findChild<QPushButton*>("profileSshHopInspectHostKeys");
    QVERIFY(inspect && inspectHop);
    QVERIFY(!inspect->isEnabled());
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog.findChild<QLineEdit*>("profileName")->setText("Inspect SSH trust");
    dialog.findChild<QLineEdit*>("profileUser")->setText("operator");
    dialog.findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    dialog.findChild<QLineEdit*>("profileSshHost")->setText("127.0.0.1");
    dialog.findChild<QLineEdit*>("profileSshUser")->setText("operator");
    QProcess peer;
    peer.start("python3",
               {"-u", "-c",
                "import socket,time; s=socket.socket(); s.bind(('127.0.0.1',0)); s.listen(8); "
                "print(s.getsockname()[1],flush=True); c,_=s.accept(); "
                "print('accepted',flush=True); time.sleep(30)"});
    QVERIFY(peer.waitForStarted());
    QVERIFY(peer.waitForReadyRead());
    bool portOk = false;
    const auto port = QString::fromUtf8(peer.readLine()).trimmed().toUShort(&portOk);
    QVERIFY(portOk && port);
    dialog.findChild<QSpinBox*>("profileSshPort")->setValue(port);
    dialog.findChild<QSpinBox*>("profileSshTimeout")->setValue(1);
    QVERIFY(inspect->isEnabled());
    QVERIFY(!inspectHop->isEnabled());
    QSignalSpy connections(&dialog, &choscordb::ProfileDialog::connectionSubmitted);
    QSignalSpy failures(&adapter, &choscordb::EngineAdapter::sshHostKeyOperationFailed);
    bool responsive = false;
    QTimer::singleShot(0, &dialog, [&] { responsive = true; });
    QElapsedTimer callTime;
    callTime.start();
    inspect->click();
    QVERIFY(callTime.elapsed() < 500);
    QTRY_VERIFY_WITH_TIMEOUT(responsive, 1000);
    QTRY_VERIFY(peer.canReadLine());
    QCOMPARE(peer.readLine().trimmed(), QByteArray("accepted"));
    auto* username = dialog.findChild<QLineEdit*>("profileSshUser");
    username->selectAll();
    QTest::keyClicks(username, "changed-user");
    QVERIFY(connections.isEmpty());
    auto* approve = dialog.findChild<QPushButton*>("sshHostKeyApprove");
    QVERIFY(!approve || !approve->isEnabled());
    peer.kill();
    QVERIFY(peer.waitForFinished());
    QTRY_COMPARE(failures.count(), 1);
    QCOMPARE(username->text(), QString("changed-user"));
    QVERIFY(connections.isEmpty());
}

void WorkspaceTest::sshHostKeyStaleInspectionSuccessCannotOpenApproval() {
    if (!qEnvironmentVariableIsSet("CHOSCORDB_QT_TRUST_FIXTURE"))
        QSKIP("Run tests/desktop/ssh_trust_fixture.py for deterministic inspection callbacks");
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    QTRY_VERIFY(dialog.findChild<QPushButton*>("profileSave")->isEnabled());
    prepareInspection(dialog);
    QSignalSpy inspected(&adapter, &choscordb::EngineAdapter::sshHostKeysInspected);
    QSignalSpy failed(&adapter, &choscordb::EngineAdapter::sshHostKeyOperationFailed);
    QSignalSpy approved(&adapter, &choscordb::EngineAdapter::sshHostKeyApproved);
    dialog.findChild<QPushButton*>("profileSshInspectHostKeys")->click();
    auto* username = dialog.findChild<QLineEdit*>("profileSshUser");
    username->selectAll();
    QTest::keyClicks(username, "different-user");
    QTRY_VERIFY_WITH_TIMEOUT(inspected.count() == 1 || !failed.isEmpty(), 10000);
    QVERIFY2(failed.isEmpty(),
             failed.isEmpty() ? "" : qPrintable(failed.first().last().toString()));
    QCOMPARE(inspected.count(), 1);
    QVERIFY(!dialog.findChild<choscordb::SshHostKeyDialog*>());
    QVERIFY(approved.isEmpty());
    QCOMPARE(username->text(), QString("different-user"));
    QVERIFY(dialog.findChild<QLineEdit*>("profileSshKnownHosts")->text().isEmpty());
    QVERIFY(dialog.findChild<QLabel*>("profileStatus")->text().contains("changed"));
}

void WorkspaceTest::sshHostKeyReviewRequiresSelectionAndShowsExactFingerprint() {
    choscordb::SshHostKeyCandidate candidate;
    candidate.target.kind = choscordb::SshHostKeyTarget::Kind::JumpId;
    candidate.target.id = "second-hop";
    candidate.originalHost = "jump-alias";
    candidate.hostname = "2001:db8::7";
    candidate.port = 2207;
    candidate.hostKeyAlias = "trusted-server-alias";
    candidate.keyType = "ssh-ed25519";
    candidate.sha256 = "SHA256:abcdefghijklmnopqrstuvwxyz0123456789ABCDE01";
    candidate.publicKey = "public-key-payload";
    candidate.opaqueJson = "opaque-candidate-payload";
    QWidget host;
    host.resize(900, 600);
    host.show();
    choscordb::SshHostKeyDialog review({candidate}, {}, &host);
    QSignalSpy approved(&review, &choscordb::SshHostKeyDialog::approvalRequested);
    QSignalSpy retried(&review, &choscordb::SshHostKeyDialog::retryRequested);
    review.show();
    QApplication::processEvents();
    auto* candidates = review.findChild<QListWidget*>("sshHostKeyCandidates");
    auto* approve = review.findChild<QPushButton*>("sshHostKeyApprove");
    auto* retry = review.findChild<QPushButton*>("sshHostKeyRetry");
    auto* path = review.findChild<QLineEdit*>("sshHostKeyKnownHosts");
    QVERIFY(candidates && approve && retry && path);
    QCOMPARE(candidates->currentRow(), -1);
    QVERIFY(!approve->isEnabled());
    candidates->setCurrentRow(0);
    QVERIFY(!approve->isEnabled());
    QCOMPARE(review.findChild<QLineEdit*>("sshHostKeyOriginalHost")->text(), QString("jump-alias"));
    QCOMPARE(review.findChild<QLineEdit*>("sshHostKeyHostname")->text(), QString("2001:db8::7"));
    QCOMPARE(review.findChild<QLineEdit*>("sshHostKeyPort")->text(), QString("2207"));
    QCOMPARE(review.findChild<QLineEdit*>("sshHostKeyAlias")->text(),
             QString("trusted-server-alias"));
    QCOMPARE(review.findChild<QLineEdit*>("sshHostKeyFingerprint")->text(), candidate.sha256);
    QCOMPARE(review.findChild<QLineEdit*>("sshHostKeyAlgorithm")->text(), QString("ssh-ed25519"));
    for (const auto& invalid :
         QStringList{"/tmp/invalid\"known-hosts", "relative-known-hosts", ":/known-hosts",
                     "~/.ssh/known_hosts", "/tmp/known-%h", "/tmp/${HOME}-known-hosts",
                     "/tmp/invalid\\hosts", "/tmp/invalid\nhosts"}) {
        path->setText(invalid);
        QVERIFY2(!approve->isEnabled(), qPrintable(invalid));
    }
    path->setText("/tmp/explicit known hosts");
    QVERIFY(approve->isEnabled());
    QVERIFY(approved.isEmpty());
    approve->click();
    QCOMPARE(approved.count(), 1);
    QCOMPARE(approved.first().at(1).toString(), QString("/tmp/explicit known hosts"));
    const auto selected = qvariant_cast<choscordb::SshHostKeyCandidate>(approved.first().at(0));
    QCOMPARE(selected.opaqueJson, candidate.opaqueJson);
    QCOMPARE(selected.sha256, candidate.sha256);
    QCOMPARE(selected.target.id, QString("second-hop"));
    QVERIFY(!approve->isEnabled());
    review.finishApproval("outcome_unknown");
    auto* status = review.findChild<QLabel*>("sshHostKeyStatus");
    auto* toast =
        host.findChild<choscordb::ToastRegion*>("toastRegion", Qt::FindDirectChildrenOnly);
    QVERIFY(status && toast);
    QVERIFY(!review.findChild<choscordb::ToastRegion*>("toastRegion", Qt::FindDirectChildrenOnly));
    QVERIFY(toast->x() >= host.width() / 2);
    QVERIFY(toast->y() >= host.height() / 2);
    QVERIFY(status->text().isEmpty());
    QVERIFY(toast->text().contains("unknown"));
    QCOMPARE(toast->property("variant").toString(), QString("warning"));
    review.showFailure("Approval failed for the selected key.");
    QVERIFY(status->text().isEmpty());
    QVERIFY(toast->text().contains("Approval failed"));
    QCOMPARE(toast->property("variant").toString(), QString("danger"));
    QVERIFY(!retry->isEnabled());
    QVERIFY(approve->isEnabled());
    QCOMPARE(approved.count(), 1);
    approve->click();
    QCOMPARE(approved.count(), 2);
    review.finishApproval("approved");
    QVERIFY(status->text().isEmpty());
    QVERIFY(toast->text().contains("Key approved"));
    QCOMPARE(toast->property("variant").toString(), QString("success"));
    QVERIFY(retried.isEmpty());
    QVERIFY(retry->isEnabled());
    retry->click();
    QCOMPARE(retried.count(), 1);
    review.invalidate();
    QVERIFY(status->text().isEmpty());
    QVERIFY(toast->text().contains("Connection settings changed"));
    QCOMPARE(toast->property("variant").toString(), QString("warning"));
    QVERIFY(!retry->isEnabled());
    QVERIFY(!approve->isEnabled());
    choscordb::SshHostKeyDialog cancelled({candidate}, "/tmp/unused known hosts");
    QSignalSpy cancelledApproval(&cancelled, &choscordb::SshHostKeyDialog::approvalRequested);
    cancelled.findChild<QListWidget*>("sshHostKeyCandidates")->setCurrentRow(0);
    cancelled.findChild<QPushButton*>("sshHostKeyClose")->click();
    QVERIFY(cancelledApproval.isEmpty());
}
