#include "workspace_test.h"
#include "workspace_test_fixture.h"
#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>

void WorkspaceTest::inlineSshKeysStayMaskedAndOutsideProfileMetadata() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    auto* source = dialog.findChild<QComboBox*>("profileSshIdentitySource");
    auto* preview = dialog.findChild<QPlainTextEdit*>("profileSshPrivateKeyPreview");
    auto* paste = dialog.findChild<QPushButton*>("profileSshPrivateKeyPaste");
    QVERIFY(source && preview && paste);
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog.findChild<QLineEdit*>("profileName")->setText("Inline private key");
    dialog.findChild<QLineEdit*>("profileUser")->setText("operator");
    dialog.findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    dialog.findChild<QLineEdit*>("profileSshHost")->setText("target.example");
    dialog.findChild<QLineEdit*>("profileSshUser")->setText("ssh-user");
    auto* auth = dialog.findChild<QComboBox*>("profileSshAuthentication");
    auth->setCurrentIndex(auth->findData("public_key"));
    source->setCurrentIndex(source->findData("inline"));
    const QString key = "-----BEGIN OPENSSH PRIVATE KEY-----\nprivate-key-material\n-----END "
                        "OPENSSH PRIVATE KEY-----\n";
    QApplication::clipboard()->setText(key);
    paste->click();
    QVERIFY(preview->isReadOnly());
    QVERIFY(!preview->toPlainText().contains("private-key-material"));
    QCOMPARE(preview->toPlainText().count('\n'), key.count('\n'));
    auto* keyEditor = dialog.findChild<choscordb::SshPrivateKeyEditor*>("profileSshPrivateKey");
    QVERIFY(keyEditor);
    QCOMPARE(keyEditor->draft().secret, key);
    QApplication::clipboard()->setText(QString(65537, 'x'));
    paste->click();
    QCOMPARE(keyEditor->draft().secret, key);
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::profileSaved);
    QSignalSpy failed(&adapter, &choscordb::EngineAdapter::profileFailed);
    save->click();
    QTRY_VERIFY(saved.count() == 1 || failed.count() == 1);
    QVERIFY2(failed.isEmpty(),
             failed.isEmpty() ? "" : qPrintable(failed.first().last().toString()));
    const auto profile = qvariant_cast<choscordb::SavedProfile>(saved.first().at(1));
    QVERIFY(!profile.sshOptions.contains("private-key-material"));
    QVERIFY(profile.sshIdentityFile.isEmpty());
    QCOMPARE(profile.sshIdentitySource, QString("inline"));
    QVERIFY(profile.sshPrivateKeyRef.isEmpty());
    QTRY_VERIFY(save->isEnabled());
    QCOMPARE(source->currentData().toString(), QString("inline"));
    QCOMPARE(preview->toPlainText().count('\n'), key.count('\n'));
    dialog.findChild<QCheckBox*>("profileSshPrivateKeyRemember")->setChecked(true);
    save->click();
    QTRY_COMPARE(failed.count(), 1);
    QTRY_VERIFY(save->isEnabled());
    QVERIFY(!failed.first().last().toString().contains("private-key-material"));
    QCOMPARE(preview->toPlainText().count('\n'), key.count('\n'));
    QCOMPARE(keyEditor->draft().secret, key);
    source->setCurrentIndex(source->findData("file"));
    dialog.findChild<QLineEdit*>("profileSshIdentityFile")->setText("/tmp/identity");
    save->click();
    QTRY_COMPARE(saved.count(), 2);
    const auto fileProfile = qvariant_cast<choscordb::SavedProfile>(saved.last().at(1));
    QCOMPARE(fileProfile.sshIdentitySource, QString("file"));
    QVERIFY(fileProfile.sshPrivateKeyRef.isEmpty());
    QTRY_VERIFY(save->isEnabled());
    QApplication::clipboard()->clear();
}
