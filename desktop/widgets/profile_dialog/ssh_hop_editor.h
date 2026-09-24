#pragma once
#include "bridge/engine_adapter.h"
#include "widgets/profile_dialog/ssh_private_key_editor.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>
class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QPushButton;
namespace choscordb {
struct SshHopSecretDraft {
    QString secret;
    bool modified = false;
    bool remember = false;
    SshPrivateKeyDraft privateKey;
    bool restoreSecret = true, restorePrivateKey = true;
};
using SshHopSecrets = QHash<QString, SshHopSecretDraft>;
class SshHopEditor final : public QWidget {
    Q_OBJECT
  public:
    explicit SshHopEditor(QWidget* parent = nullptr);
    void setDraft(const QJsonArray& hops, const QJsonObject& references,
                  const QJsonObject& privateKeyReferences = {});
    QJsonArray draft() const;
    QJsonObject references() const;
    QJsonObject privateKeyReferences() const;
    QList<SshHopCredential> credentials(bool saving) const;
    SshHopSecrets captureSecrets(bool sessionOnly) const;
    void restoreSecrets(const SshHopSecrets& secrets);
    void setInspectionEnabled(bool enabled);
    bool setKnownHosts(const SshHostKeyTarget& target, const QString& path);
  signals:
    void changed();
    void hostKeyInspectionRequested(const choscordb::SshHostKeyTarget& target);

  private:
    struct Hop {
        QString id, host, user, authentication = "configured", identity, agent, knownHosts;
        int port = 22;
        QString reference, privateKeyReference, identitySource = "file";
        SshHopSecretDraft credential;
    };
    void selectRow(int row);
    void updateRow();
    void refreshList(int selected);
    void updateAuthentication();
    static bool usesSecret(const Hop& hop);
    QList<Hop> hops_;
    int selected_ = -1;
    bool filling_ = false;
    bool inspectionEnabled_ = true;
    QPushButton* inspect_;
    QListWidget* list_;
    QWidget* fields_;
    QLineEdit *host_, *user_, *identity_, *agent_, *knownHosts_, *secret_;
    QComboBox *authentication_, *identitySource_;
    SshPrivateKeyEditor* privateKey_;
    QSpinBox* port_;
    QCheckBox* remember_;
};
} // namespace choscordb
