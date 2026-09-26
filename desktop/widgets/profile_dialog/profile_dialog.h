#pragma once
#include "bridge/engine_adapter.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include "widgets/profile_dialog/ssh_hop_editor.h"
#include "widgets/profile_dialog/ssh_private_key_editor.h"
#include <QPointer>
#include <QHash>
class QComboBox;
class QFormLayout;
class QPlainTextEdit;
class QLabel;
class QDialog;
class QHideEvent;
class QLineEdit;
class QListWidget;
class QPushButton;
class QCheckBox;
class QSpinBox;
namespace choscordb {
class SshHostKeyDialog;
class ToastRegion;
namespace design {
class FieldValidation;
}
class ProfileDialog final : public DialogShell {
    Q_OBJECT
  public:
    explicit ProfileDialog(EngineAdapter* adapter, QWidget* parent = nullptr);
    void newProfile();
    void selectProfile(const QString& id);
    void manageProfile(const QString& id, const QString& action);
    void saveDraft(const SavedProfile& profile);
    void testDraft(const SavedProfile& profile);
  signals:
    void connectionSubmitted(const choscordb::SavedProfile& profile, quint64 id, bool savedProfile);
    void openQueryRequested(quint64 connection);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    void createTrustControls(QFormLayout* form);
    void inspectHostKeys(const SshHostKeyTarget& target);
    void updateTrustControls();
    void showTrustStatus(const QString& message);
    QWidget* validated(QWidget* field);
    design::FieldValidation* validationFor(QWidget* field) const;
    void showFieldError(QWidget* field, const QString& message);
    void createPrivateKeyControls(QFormLayout* form);
    void updatePrivateKeyControls();
    SshPrivateKeyCredential privateKeyCredential(const SavedProfile& profile, bool saving) const;
    void createProxyControls(QFormLayout* form);
    void updateProxyControls();
    void writeProxyDraft(SavedProfile& profile) const;
    void setProxyDraft(const SavedProfile& profile);
    static bool proxyNeedsPassword(const SavedProfile& profile);
    void createAuthenticationControls(QFormLayout* form);
    void updateAuthenticationControls();
    void writeAuthenticationDraft(SavedProfile& profile) const;
    void setAuthenticationDraft(const SavedProfile& profile);
    static QString authenticationMethod(const SavedProfile& profile);
    void createConnectionControls(QFormLayout* security, QFormLayout* ssh);
    static bool validServerHost(const QString& host);
    bool validateSecurityDraft(const SavedProfile& profile);
    QString sshOptionsDraft() const;
    void setSecurityDraft(const SavedProfile& profile);
    void writeSecurityDraft(SavedProfile& profile) const;
    SavedProfile draft() const;
    void setDraft(const SavedProfile& profile);
    void refresh();
    void setBusy(bool busy, const QString& message = {}, bool success = false);
    void updateDriver();
    bool discardChanges();
    bool validateConnectionDraft(const SavedProfile& profile);
    void connectDraft(bool openQuery);
    void dispatchProfileAction();
    QPointer<EngineAdapter> adapter_;
    QList<SavedProfile> profiles_;
    SavedProfile current_;
    QString pendingSelection_;
    QString managedProfile_, managedAction_;
    QString refreshNotice_;
    bool savingDraft_ = false;
    bool connectAfterSave_ = false;
    bool openQueryAfterConnect_ = false;
    bool preservePasswordOnRefresh_ = false;
    bool preserveSshSecretOnRefresh_ = false;
    bool preserveTlsSecretOnRefresh_ = false;
    bool preserveProxySecretOnRefresh_ = false;
    quint64 token_ = 0;
    quint64 revision_ = 0;
    bool busy_ = false;
    bool connecting_ = false;
    QString connectionSubmissionError_;
    std::optional<quint64> pendingConnection_;
    bool dirty_ = false;
    bool filling_ = false;
    QListWidget* list_;
    QWidget* form_;
    QWidget* sqliteFields_;
    QWidget* postgresFields_;
    QWidget* sshFields_;
    QLineEdit *name_, *path_, *host_, *database_, *user_, *rootCertificate_, *password_;
    QComboBox *driver_, *tls_, *sshAuthentication_;
    QCheckBox *readOnly_, *rememberPassword_, *rememberSshSecret_;
    QSpinBox* port_;
    QCheckBox* sshEnabled_;
    QLineEdit *sshHost_, *sshUser_, *sshIdentityFile_, *sshSecret_;
    QSpinBox* sshPort_;
    QLineEdit *tlsClientIdentity_, *tlsSecret_, *sshAgentSocket_, *sshKnownHosts_;
    QCheckBox* rememberTlsSecret_;
    QSpinBox *sshTimeout_, *sshKeepalive_, *sshKeepaliveCount_, *sshRemotePort_;
    QLineEdit* sshRemoteHost_;
    QCheckBox *sshLocalBinding_, *sshShareTunnels_;
    QLineEdit* sshLocalHost_;
    QSpinBox* sshLocalPort_;
    QLabel* sshLocalBindingWarning_;
    QComboBox* sshIdentitySource_ = nullptr;
    SshPrivateKeyEditor* sshPrivateKey_ = nullptr;
    std::optional<SshPrivateKeyDraft> pendingPrivateKey_;
    QPushButton* inspectSshKeys_ = nullptr;
    quint64 trustToken_ = 0, trustRevision_ = 0;
    SshHostKeyTarget trustTarget_;
    QString trustPath_;
    QPointer<SshHostKeyDialog> trustDialog_;
    SshHopEditor* sshHopEditor_;
    SshHopSecrets pendingHopSecrets_;
    QComboBox* authentication_;
    QWidget* authenticationFields_;
    QLineEdit *pgPassFile_, *pgPassHostname_, *passwordCommandDirectory_;
    QPlainTextEdit* passwordCommand_;
    QSpinBox* passwordCommandTimeout_;
    QCheckBox *proxyEnabled_ = nullptr, *rememberProxySecret_ = nullptr;
    QWidget* proxyFields_ = nullptr;
    QComboBox* proxyProtocol_ = nullptr;
    QLineEdit *proxyHost_ = nullptr, *proxyUser_ = nullptr, *proxySecret_ = nullptr;
    QSpinBox* proxyPort_ = nullptr;
    QLabel* status_;
    QPointer<QDialog> progressDialog_;
    QLabel* progressMessage_ = nullptr;
    QPointer<ToastRegion> feedbackToast_;
    QHash<QWidget*, design::FieldValidation*> validations_;
    design::FieldValidation* nameValidation_;
    QList<QPushButton*> actions_;
    QPushButton *sqliteChoice_, *postgresChoice_, *mysqlChoice_;
};
} // namespace choscordb
