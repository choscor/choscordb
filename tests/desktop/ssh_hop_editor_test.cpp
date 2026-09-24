#include "workspace_test.h"
#include "workspace_test_fixture.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSpinBox>

void WorkspaceTest::sshHopEditorPreservesIndependentSecretsWhenReordered() {
    choscordb::EngineAdapter adapter;
    choscordb::ProfileDialog dialog(&adapter);
    auto* save = dialog.findChild<QPushButton*>("profileSave");
    QTRY_VERIFY(save->isEnabled());
    auto* add = dialog.findChild<QPushButton*>("profileSshHopAdd");
    auto* list = dialog.findChild<QListWidget*>("profileSshHopList");
    QVERIFY(add && list);
    dialog.findChild<QComboBox*>("profileDriver")->setCurrentIndex(1);
    dialog.findChild<QLineEdit*>("profileName")->setText("Independent hop credentials");
    dialog.findChild<QLineEdit*>("profileUser")->setText("operator");
    dialog.findChild<QCheckBox*>("profileSshEnabled")->setChecked(true);
    dialog.findChild<QLineEdit*>("profileSshHost")->setText("target.example");
    dialog.findChild<QLineEdit*>("profileSshUser")->setText("target-user");
    auto* auth = dialog.findChild<QComboBox*>("profileSshHopAuthentication");
    auto* host = dialog.findChild<QLineEdit*>("profileSshHopHost");
    auto* user = dialog.findChild<QLineEdit*>("profileSshHopUser");
    auto* secret = dialog.findChild<QLineEdit*>("profileSshHopSecret");
    QVERIFY(auth && host && user && secret);
    QCOMPARE(secret->echoMode(), QLineEdit::Password);
    add->click();
    host->setText("first.example");
    user->setText("first-user");
    auth->setCurrentIndex(auth->findData("password"));
    secret->setText("first transient secret");
    secret->setModified(true);
    add->click();
    host->setText("::1");
    user->setText("second-user");
    dialog.findChild<QSpinBox*>("profileSshHopPort")->setValue(2202);
    auth->setCurrentIndex(auth->findData("public_key"));
    dialog.findChild<QLineEdit*>("profileSshHopIdentityFile")->setText("/tmp/second key");
    dialog.findChild<QLineEdit*>("profileSshHopKnownHosts")->setText("/tmp/known hosts");
    secret->setText("second transient passphrase");
    secret->setModified(true);
    QSignalSpy saved(&adapter, &choscordb::EngineAdapter::profileSaved);
    save->click();
    QTRY_COMPARE(saved.count(), 1);
    QTRY_VERIFY(save->isEnabled());
    auto profile = qvariant_cast<choscordb::SavedProfile>(saved.at(0).at(1));
    const auto original =
        QJsonDocument::fromJson(profile.sshOptions.toUtf8()).object()["jump_hosts"].toArray();
    QCOMPARE(original.size(), 2);
    const auto firstId = original[0].toObject()["id"].toString();
    const auto secondId = original[1].toObject()["id"].toString();
    QVERIFY(!firstId.isEmpty() && !secondId.isEmpty() && firstId != secondId);
    QVERIFY(!profile.sshOptions.contains("transient"));
    QVERIFY(QJsonDocument::fromJson(profile.sshJumpCredentialRefs.toUtf8()).object().isEmpty());
    list->setCurrentRow(1);
    QCOMPARE(secret->text(), QString("second transient passphrase"));
    dialog.findChild<QPushButton*>("profileSshHopUp")->click();
    QCOMPARE(list->currentRow(), 0);
    QCOMPARE(secret->text(), QString("second transient passphrase"));
    list->setCurrentRow(1);
    QCOMPARE(secret->text(), QString("first transient secret"));
    save->click();
    QTRY_COMPARE(saved.count(), 2);
    QTRY_VERIFY(save->isEnabled());
    profile = qvariant_cast<choscordb::SavedProfile>(saved.at(1).at(1));
    const auto reordered =
        QJsonDocument::fromJson(profile.sshOptions.toUtf8()).object()["jump_hosts"].toArray();
    QCOMPARE(reordered[0].toObject()["id"].toString(), secondId);
    QCOMPARE(reordered[1].toObject()["id"].toString(), firstId);
    list->setCurrentRow(1);
    auto* remember = dialog.findChild<QCheckBox*>("profileSshHopRemember");
    remember->setChecked(true);
    QSignalSpy failed(&adapter, &choscordb::EngineAdapter::profileFailed);
    save->click();
    QTRY_COMPARE(failed.count(), 1);
    QTRY_VERIFY(save->isEnabled());
    QCOMPARE(secret->text(), QString("first transient secret"));
    QVERIFY(!dialog.findChild<QLabel*>("profileStatus")->text().contains("first transient secret"));
    remember->setChecked(false);
    host->setText("https://invalid-hop");
    save->click();
    QVERIFY(save->isEnabled());
    QCOMPARE(saved.count(), 2);
    QCOMPARE(host->text(), QString("https://invalid-hop"));
    QCOMPARE(secret->text(), QString("first transient secret"));
    host->setText("first.example");
    auth->setCurrentIndex(auth->findData("agent"));
    QVERIFY(!secret->isEnabled());
}

void WorkspaceTest::sshHopEditorKeepsAndClearsCredentialsByStableId() {
    choscordb::SshHopEditor editor;
    editor.setDraft(QJsonArray{QJsonObject{{"id", "first"},
                                           {"host", "first.example"},
                                           {"port", 22},
                                           {"user", "one"},
                                           {"authentication", "password"}},
                               QJsonObject{{"id", "second"},
                                           {"host", "second.example"},
                                           {"port", 22},
                                           {"user", "two"},
                                           {"authentication", "public_key"},
                                           {"identity_file", "/tmp/key"}}},
                    QJsonObject{{"first", "saved-first"}, {"second", "saved-second"}});
    const auto credential = [&editor](const QString& id, bool saving) {
        for (const auto& value : editor.credentials(saving))
            if (value.id == id)
                return value;
        return choscordb::SshHopCredential{};
    };
    QCOMPARE(credential("first", true).action, QString("keep"));
    QCOMPARE(credential("second", true).action, QString("keep"));
    QVERIFY(!credential("first", false).hasSecret);
    auto* list = editor.findChild<QListWidget*>("profileSshHopList");
    auto* secret = editor.findChild<QLineEdit*>("profileSshHopSecret");
    auto* remember = editor.findChild<QCheckBox*>("profileSshHopRemember");
    list->setCurrentRow(1);
    remember->setChecked(false);
    secret->setText("remaining-hop-session-key");
    QCOMPARE(credential("second", true).action, QString("clear"));
    QCOMPARE(credential("first", true).action, QString("keep"));
    list->setCurrentRow(0);
    secret->setText("replacement-first");
    QCOMPARE(credential("first", true).action, QString("replace"));
    QCOMPARE(credential("first", true).secret, QString("replacement-first"));
    editor.findChild<QPushButton*>("profileSshHopDown")->click();
    QCOMPARE(credential("first", false).secret, QString("replacement-first"));
    secret->clear();
    QVERIFY(credential("first", false).hasSecret);
    QVERIFY(credential("first", false).secret.isEmpty());
    QCOMPARE(credential("first", true).action, QString("clear"));
    auto* auth = editor.findChild<QComboBox*>("profileSshHopAuthentication");
    auth->setCurrentIndex(auth->findData("agent"));
    QVERIFY(credential("first", false).id.isEmpty());
    QVERIFY(!editor.references().contains("first"));
    QVERIFY(!secret->isEnabled());
    editor.findChild<QPushButton*>("profileSshHopRemove")->click();
    QCOMPARE(editor.draft().size(), 1);
    QCOMPARE(editor.draft()[0].toObject()["id"].toString(), QString("second"));
    QCOMPARE(credential("second", false).secret, QString("remaining-hop-session-key"));
    QCOMPARE(editor.references().value("second").toString(), QString("saved-second"));
}
