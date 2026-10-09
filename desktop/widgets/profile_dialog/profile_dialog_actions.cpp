#include "design_system/field/field.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
namespace choscordb {
ProfileSecretDrafts ProfileDialog::secretDrafts() const {
    ProfileSecretDrafts secrets;
    secrets.saveCredentials = saveCredentials_->isChecked();
    secrets.database = password_->text();
    secrets.databaseModified = password_->isModified();
    secrets.ssh = sshSecret_->text();
    secrets.sshModified = sshSecret_->isModified();
    secrets.tls = tlsSecret_->text();
    secrets.tlsModified = tlsSecret_->isModified();
    const auto privateKey = sshPrivateKey_->draft();
    secrets.sshPrivateKey = privateKey.secret;
    secrets.sshPrivateKeyModified = privateKey.modified;
    return secrets;
}

QWidget* ProfileDialog::fieldWidget(const QString& field) const {
    const QHash<QString, QWidget*> fields{
        {"name", name_},
        {"path", path_},
        {"host", host_},
        {"user", user_},
        {"password", password_},
        {"ssh_enabled", sshEnabled_},
        {"ssh_host", sshHost_},
        {"ssh_user", sshUser_},
        {"ssh_identity_file", sshIdentityFile_},
        {"ssh_secret", sshSecret_},
        {"tls_secret", tlsSecret_},
        {"ssh_private_key", sshPrivateKey_},
    };
    return fields.value(field, nullptr);
}

void ProfileDialog::connectDraft(bool openQuery) {
    if (busy_ || !adapter_)
        return;
    const auto value = draft();
    if (!validateConnectionDraft(value))
        return;
    ++token_;
    setBusy(true, tr("Connecting…"));
    connectionSubmissionError_.clear();
    connecting_ = true;
    const auto id = adapter_->connectProfileDraft(value, secretDrafts());
    connecting_ = false;
    if (!id) {
        setBusy(false, connectionSubmissionError_.isEmpty()
                           ? tr("Connection could not be submitted.")
                           : connectionSubmissionError_);
        return;
    }
    pendingConnection_ = id;
    openQueryAfterConnect_ = openQuery;
    bool savedProfile = false;
    for (const auto& profile : profiles_)
        savedProfile = savedProfile || profile.id == value.id;
    emit connectionSubmitted(value, *id, savedProfile);
}

SavedProfile ProfileDialog::draft() const {
    auto value = current_;
    value.name = name_->text();
    value.driver = driver_->currentData().toString();
    value.path = path_->text();
    value.readOnly = readOnly_->isChecked();
    value.host = host_->text();
    value.port = static_cast<quint16>(port_->value());
    value.database = database_->text();
    value.user = user_->text();
    value.sshEnabled = sshEnabled_->isChecked();
    value.sshHost = sshHost_->text();
    value.sshPort = static_cast<quint16>(sshPort_->value());
    value.sshUser = sshUser_->text();
    value.sshAuthentication = sshAuthentication_->currentData().toString();
    value.sshIdentitySource = sshIdentitySource_->currentData().toString();
    value.sshIdentityFile = sshIdentityFile_->text();
    return EngineAdapter::normalizeProfileDraft(current_, value, secretDrafts());
}

void ProfileDialog::setDraft(const SavedProfile& profile) {
    nameValidation_->setError({});
    for (auto* validation : validations_)
        validation->setError({});
    filling_ = true;
    ++revision_;
    const auto value = EngineAdapter::profileDraftDefaults(profile);
    current_ = value;
    name_->setText(value.name);
    driver_->setCurrentIndex(driver_->findData(value.driver));
    path_->setText(value.path);
    readOnly_->setChecked(value.readOnly);
    host_->setText(value.host);
    port_->setValue(value.port);
    database_->setText(value.database);
    user_->setText(value.user);
    sshEnabled_->setChecked(value.sshEnabled);
    sshHost_->setText(value.sshHost);
    sshPort_->setValue(value.sshPort);
    sshUser_->setText(value.sshUser);
    const auto sshAuthenticationIndex = sshAuthentication_->findData(value.sshAuthentication);
    sshAuthentication_->setCurrentIndex(sshAuthenticationIndex < 0 ? 0 : sshAuthenticationIndex);
    sshIdentityFile_->setText(value.sshIdentityFile);
    sshSecret_->clear();
    sshSecret_->setModified(false);
    sshSecret_->setPlaceholderText(value.sshCredentialRef.isEmpty()
                                       ? (value.sshAuthentication == "public_key"
                                              ? tr("Optional for an unencrypted key")
                                              : tr("SSH password"))
                                       : tr("Saved SSH credential — leave unchanged to keep"));
    password_->clear();
    password_->setModified(false);
    password_->setPlaceholderText(value.credentialRef.isEmpty()
                                      ? tr("Optional — leave blank for passwordless authentication")
                                      : tr("Saved password — leave unchanged to keep"));
    saveCredentials_->setChecked(EngineAdapter::profileHasSavedCredentials(value));
    setSecurityDraft(value);
    filling_ = false;
    dirty_ = false;
    updateDriver();
}

void ProfileDialog::updateDriver() {
    const auto form = EngineAdapter::profileDriverForm(driver_->currentData().toString());
    user_->setPlaceholderText(form.userOptional ? tr("Optional — anonymous authentication")
                                                : tr("Database username"));
    updateTrustControls();
    if (auto* choice = driverChoices_->button(driver_->currentIndex()))
        choice->setChecked(true);
    database_->setPlaceholderText(form.databaseSelectsServer
                                      ? tr("Optional — connect to the server")
                                      : tr("Optional — defaults to the username"));
    sshEnabled_->setVisible(form.server);
    qobject_cast<QFormLayout*>(postgresFields_->layout())
        ->setRowVisible(tlsSecretField_, form.server && current_.tls != "disable" &&
                                             !current_.tlsClientIdentity.isEmpty());
    sshFields_->setVisible(form.server && sshEnabled_->isChecked());

    sqliteFields_->setVisible(!form.server);
    postgresFields_->setVisible(form.server);
}

void ProfileDialog::saveDraft(const SavedProfile& profile) {
    if (busy_ || !adapter_)
        return;
    if (!validateConnectionDraft(profile))
        return;
    const auto secrets = secretDrafts();
    const auto privateKey = sshPrivateKey_->draft();
    setDraft(profile);
    // setDraft resets the form's secret fields; keep what the user typed.
    sshPrivateKey_->setDraft(privateKey, !profile.sshPrivateKeyRef.isEmpty());
    password_->setText(secrets.database);
    password_->setModified(secrets.databaseModified);
    saveCredentials_->setChecked(secrets.saveCredentials);
    sshSecret_->setText(secrets.ssh);
    sshSecret_->setModified(secrets.sshModified);
    tlsSecret_->setText(secrets.tls);
    tlsSecret_->setModified(secrets.tlsModified);
    dirty_ = true;
    ++revision_;
    savingDraft_ = true;
    setBusy(true, tr("Saving profile…"));
    adapter_->saveProfileDraft(profile, secrets, ++token_);
}

void ProfileDialog::testDraft(const SavedProfile& profile) {
    if (busy_ || !adapter_)
        return;
    if (!validateConnectionDraft(profile))
        return;
    setBusy(true, tr("Testing connection…"));
    adapter_->testProfileDraft(profile, secretDrafts(), ++token_);
}

bool ProfileDialog::validateConnectionDraft(const SavedProfile& profile) {
    const auto error = EngineAdapter::validateProfileDraft(profile, secretDrafts());
    if (error.message.isEmpty())
        return true;
    auto* field = fieldWidget(error.field);
    setBusy(false, field ? QString() : error.message);
    if (field)
        showFieldError(field, error.message);
    return false;
}
} // namespace choscordb
