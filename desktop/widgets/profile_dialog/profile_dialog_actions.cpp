#include "design_system/field/field.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QUrl>
#include <QUuid>
namespace choscordb {
bool ProfileDialog::validServerHost(const QString& host) {
    if (host.isEmpty() || host.startsWith('-'))
        return false;
    for (const auto character : host)
        if (character.isSpace() || character.category() == QChar::Other_Control ||
            QStringLiteral("/@?#\\").contains(character))
            return false;
    // QUrl validates IPv6 without requiring QtNetwork. Only the host is accepted;
    // credentials and the port have separate fields in this form.
    const auto authority =
        host.contains(':') && !host.startsWith('[') ? QStringLiteral("[%1]").arg(host) : host;
    const QUrl url(QStringLiteral("tcp://") + authority, QUrl::StrictMode);
    return url.isValid() && !url.host().isEmpty() && url.port() == -1;
}
void ProfileDialog::connectDraft(bool openQuery) {
    if (busy_ || !adapter_)
        return;
    auto value = draft();
    if (value.name.trimmed().isEmpty()) {
        nameValidation_->setError(tr("Enter a profile name."));
        name_->setFocus();
        return;
    }
    if (value.id.isEmpty())
        value.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!validateConnectionDraft(value))
        return;
    ++token_;
    setBusy(true, tr("Connecting…"));
    connectionSubmissionError_.clear();
    connecting_ = true;
    const bool hasPassword = (value.driver == "postgres" || value.driver == "mysql") &&
                             authenticationMethod(value) == "password" &&
                             (password_->isModified() || !password_->text().isEmpty());
    const bool hasSshSecret = value.sshEnabled && value.sshAuthentication != "agent" &&
                              (sshSecret_->isModified() || !sshSecret_->text().isEmpty());
    const bool hasTlsSecret = value.driver != "sqlite" && value.tls != "disable" &&
                              !value.tlsClientIdentity.isEmpty() &&
                              (tlsSecret_->isModified() || !tlsSecret_->text().isEmpty());
    const bool hasProxySecret = proxyNeedsPassword(value) &&
                                (proxySecret_->isModified() || !proxySecret_->text().isEmpty());
    const auto id = adapter_->connectProfileWithSecrets(
        value, hasPassword ? password_->text() : QString(), hasPassword,
        hasSshSecret ? sshSecret_->text() : QString(), hasSshSecret,
        hasTlsSecret ? tlsSecret_->text() : QString(), hasTlsSecret,
        hasProxySecret ? proxySecret_->text() : QString(), hasProxySecret,
        value.sshEnabled ? sshHopEditor_->credentials(false) : QList<SshHopCredential>{},
        privateKeyCredential(value, false));
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
    value.tls = tls_->currentData().toString();
    value.rootCertificate = rootCertificate_->text();
    value.sshEnabled =
        (value.driver == "postgres" || value.driver == "mysql") && sshEnabled_->isChecked();
    value.sshHost = sshHost_->text();
    value.sshPort = static_cast<quint16>(sshPort_->value());
    value.sshUser = sshUser_->text();
    value.sshAuthentication = sshAuthentication_->currentData().toString();
    if (value.sshAuthentication != current_.sshAuthentication ||
        (value.sshAuthentication == "public_key" &&
         sshIdentityFile_->text() != current_.sshIdentityFile))
        value.sshCredentialRef.clear();
    value.sshIdentityFile =
        value.sshAuthentication == "public_key" ? sshIdentityFile_->text() : QString();
    writeAuthenticationDraft(value);
    writeSecurityDraft(value);
    writeProxyDraft(value);
    return value;
}

void ProfileDialog::setDraft(const SavedProfile& value) {
    nameValidation_->setError({});
    for (auto* validation : validations_)
        validation->setError({});
    filling_ = true;
    ++revision_;
    current_ = value;
    name_->setText(value.name);
    driver_->setCurrentIndex(driver_->findData(value.driver));
    path_->setText(value.path);
    readOnly_->setChecked(value.readOnly);
    host_->setText(value.host.isEmpty() ? "localhost" : value.host);
    port_->setValue(value.port ? value.port : value.driver == "mysql" ? 3306 : 5432);
    database_->setText(value.database);
    user_->setText(value.user);
    const auto tlsIndex = tls_->findData(value.tls);
    tls_->setCurrentIndex(tlsIndex < 0 ? tls_->findData("disable") : tlsIndex);
    rootCertificate_->setText(value.rootCertificate);
    sshEnabled_->setChecked(value.sshEnabled);
    sshHost_->setText(value.sshHost);
    sshPort_->setValue(value.sshPort ? value.sshPort : 22);
    sshUser_->setText(value.sshUser);
    auto sshAuthentication = value.sshAuthentication;
    if (sshAuthentication.isEmpty())
        sshAuthentication = value.sshIdentityFile.isEmpty() ? "agent" : "public_key";
    const auto sshAuthenticationIndex = sshAuthentication_->findData(sshAuthentication);
    sshAuthentication_->setCurrentIndex(sshAuthenticationIndex < 0 ? 0 : sshAuthenticationIndex);
    sshIdentityFile_->setText(value.sshIdentityFile);
    sshSecret_->clear();
    sshSecret_->setModified(false);
    sshSecret_->setPlaceholderText(value.sshCredentialRef.isEmpty()
                                       ? (sshAuthentication == "public_key"
                                              ? tr("Optional for an unencrypted key")
                                              : tr("SSH password"))
                                       : tr("Saved SSH credential — leave unchanged to keep"));
    rememberSshSecret_->setChecked(!value.sshCredentialRef.isEmpty());
    password_->clear();
    password_->setModified(false);
    password_->setPlaceholderText(value.credentialRef.isEmpty()
                                      ? tr("Optional — leave blank for passwordless authentication")
                                      : tr("Saved password — leave unchanged to keep"));
    rememberPassword_->setChecked(!value.credentialRef.isEmpty());
    setAuthenticationDraft(value);
    setSecurityDraft(value);
    setProxyDraft(value);
    filling_ = false;
    dirty_ = false;
    updateDriver();
}

void ProfileDialog::updateDriver() {
    updateAuthenticationControls();
    updateProxyControls();
    updateTrustControls();
    const bool sqlite = driver_->currentData().toString() == "sqlite";
    sqliteChoice_->setChecked(sqlite);
    const bool mysql = driver_->currentData().toString() == "mysql";
    database_->setPlaceholderText(mysql ? tr("Optional — connect to the server")
                                        : tr("Optional — defaults to the username"));
    if (auto* model = qobject_cast<QStandardItemModel*>(tls_->model()))
        if (auto* prefer = model->item(tls_->findData("prefer")))
            prefer->setEnabled(!mysql);
    postgresChoice_->setChecked(!sqlite && !mysql);
    mysqlChoice_->setChecked(mysql);
    sshEnabled_->setVisible(!sqlite);
    sshFields_->setVisible(!sqlite && sshEnabled_->isChecked());

    sqliteFields_->setVisible(sqlite);
    postgresFields_->setVisible(!sqlite);
}

void ProfileDialog::saveDraft(const SavedProfile& profile) {
    if (busy_ || !adapter_)
        return;
    auto value = profile;
    if (value.name.trimmed().isEmpty()) {
        nameValidation_->setError(tr("Enter a profile name."));
        name_->setFocus();
        return;
    }
    if (value.id.isEmpty())
        value.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!validateConnectionDraft(value))
        return;
    const auto password = password_->text();
    const bool modified = password_->isModified();
    const bool remember = rememberPassword_->isChecked();
    const auto sshSecret = sshSecret_->text();
    const bool sshSecretModified = sshSecret_->isModified();
    const bool rememberSshSecret = rememberSshSecret_->isChecked();
    const auto tlsSecret = tlsSecret_->text();
    const bool tlsSecretModified = tlsSecret_->isModified();
    const bool rememberTlsSecret = rememberTlsSecret_->isChecked();
    const auto privateKey = sshPrivateKey_->draft();
    const auto hopSecrets = sshHopEditor_->captureSecrets(false);
    const auto proxySecret = proxySecret_->text();
    const bool proxySecretModified = proxySecret_->isModified();
    const bool rememberProxySecret = rememberProxySecret_->isChecked();
    setDraft(value);
    sshHopEditor_->restoreSecrets(hopSecrets);
    sshPrivateKey_->setDraft(privateKey, !value.sshPrivateKeyRef.isEmpty());
    proxySecret_->setText(proxySecret);
    proxySecret_->setModified(proxySecretModified);
    rememberProxySecret_->setChecked(rememberProxySecret);
    password_->setText(password);
    password_->setModified(modified);
    rememberPassword_->setChecked(remember);
    sshSecret_->setText(sshSecret);
    sshSecret_->setModified(sshSecretModified);
    rememberSshSecret_->setChecked(rememberSshSecret);
    tlsSecret_->setText(tlsSecret);
    tlsSecret_->setModified(tlsSecretModified);
    rememberTlsSecret_->setChecked(rememberTlsSecret);
    dirty_ = true;
    ++revision_;
    QString action = "clear";
    if ((value.driver == "postgres" || value.driver == "mysql") &&
        authenticationMethod(value) == "password" && remember)
        action =
            modified || !password.isEmpty() || value.credentialRef.isEmpty() ? "replace" : "keep";
    QString sshAction = "clear";
    if (value.sshEnabled && value.sshAuthentication != "agent" && rememberSshSecret) {
        if (sshSecretModified)
            sshAction = sshSecret.isEmpty() ? "clear" : "replace";
        else if (!sshSecret.isEmpty())
            sshAction = "replace";
        else if (!value.sshCredentialRef.isEmpty())
            sshAction = "keep";
    }
    QString tlsAction = "clear";
    if (value.driver != "sqlite" && value.tls != "disable" && !value.tlsClientIdentity.isEmpty() &&
        rememberTlsSecret)
        tlsAction = tlsSecretModified || !tlsSecret.isEmpty() || value.tlsCredentialRef.isEmpty()
                        ? "replace"
                        : "keep";
    QString proxyAction = "clear";
    if (proxyNeedsPassword(value) && rememberProxySecret) {
        if (proxySecretModified || !proxySecret.isEmpty())
            proxyAction = proxySecret.isEmpty() ? "clear" : "replace";
        else if (!value.proxyCredentialRef.isEmpty())
            proxyAction = "keep";
    }
    savingDraft_ = true;
    setBusy(true, tr("Saving profile…"));
    adapter_->saveProfileWithSecrets(
        value, action == "replace" ? password : QString(), action,
        sshAction == "replace" ? sshSecret : QString(), sshAction, ++token_,
        tlsAction == "replace" ? tlsSecret : QString(), tlsAction,
        proxyAction == "replace" ? proxySecret : QString(), proxyAction,
        value.sshEnabled ? sshHopEditor_->credentials(true) : QList<SshHopCredential>{},
        privateKeyCredential(value, true));
}

void ProfileDialog::testDraft(const SavedProfile& profile) {
    if (busy_ || !adapter_)
        return;
    auto value = profile;
    if (value.name.trimmed().isEmpty()) {
        nameValidation_->setError(tr("Enter a profile name."));
        name_->setFocus();
        return;
    }
    if (value.id.isEmpty())
        value.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!validateConnectionDraft(value))
        return;
    setBusy(true, tr("Testing connection…"));
    const bool hasPassword = (value.driver == "postgres" || value.driver == "mysql") &&
                             authenticationMethod(value) == "password" &&
                             (password_->isModified() || !password_->text().isEmpty());
    const bool hasSshSecret = value.sshEnabled && value.sshAuthentication != "agent" &&
                              (sshSecret_->isModified() || !sshSecret_->text().isEmpty());
    const bool hasTlsSecret = value.driver != "sqlite" && value.tls != "disable" &&
                              !value.tlsClientIdentity.isEmpty() &&
                              (tlsSecret_->isModified() || !tlsSecret_->text().isEmpty());
    const bool hasProxySecret = proxyNeedsPassword(value) &&
                                (proxySecret_->isModified() || !proxySecret_->text().isEmpty());
    adapter_->testProfileWithSecrets(
        value, hasPassword ? password_->text() : QString(), hasPassword,
        hasSshSecret ? sshSecret_->text() : QString(), hasSshSecret, ++token_,
        hasTlsSecret ? tlsSecret_->text() : QString(), hasTlsSecret,
        hasProxySecret ? proxySecret_->text() : QString(), hasProxySecret,
        value.sshEnabled ? sshHopEditor_->credentials(false) : QList<SshHopCredential>{},
        privateKeyCredential(value, false));
}

bool ProfileDialog::validateConnectionDraft(const SavedProfile& profile) {
    if (profile.name.toUtf8().size() > 1024 || profile.name.contains(QChar::Null)) {
        setBusy(false);
        showFieldError(name_, tr("Connection name is invalid or too long."));
        return false;
    }
    if (profile.driver == "sqlite" && profile.path.trimmed().isEmpty()) {
        setBusy(false);
        showFieldError(path_, tr("Enter a database file path or :memory:."));
        return false;
    }
    if (profile.driver == "sqlite" &&
        (profile.path.toUtf8().size() > 16 * 1024 || profile.path.contains(QChar::Null))) {
        setBusy(false);
        showFieldError(path_, tr("Database path is invalid or too long."));
        return false;
    }
    const auto validate = [this, &profile] {
        const auto oversized = [this](QLineEdit* field) {
            if (field->text().toUtf8().size() <= 16 * 1024)
                return false;
            setBusy(false);
            showFieldError(field, tr("Credential exceeds the supported size."));
            return true;
        };
        if (((profile.driver == "postgres" || profile.driver == "mysql") &&
             authenticationMethod(profile) == "password" && oversized(password_)) ||
            (profile.sshEnabled && profile.sshAuthentication != "agent" && oversized(sshSecret_)) ||
            (profile.driver != "sqlite" && profile.tls != "disable" &&
             !profile.tlsClientIdentity.isEmpty() && oversized(tlsSecret_)) ||
            (proxyNeedsPassword(profile) && oversized(proxySecret_)))
            return false;
        QString error;
        if (adapter_->validateConnectionProperties(profile, error))
            return true;
        if (profile.driver == "sqlite" && profile.path.startsWith("file:") &&
            authenticationMethod(profile) == "password" &&
            error == "Invalid profile") {
            setBusy(false);
            showFieldError(path_, tr("Enter a valid SQLite file URI."));
            return false;
        }
        setBusy(false, tr("Invalid connection options. %1").arg(error));
        return false;
    };
    if (profile.driver == "sqlite") {
        return validate();
    }
    const auto invalid = [this](QWidget* field, const QString& message) {
        setBusy(false);
        showFieldError(field, message);
        return false;
    };
    const bool socket = profile.host.startsWith('/');
    if (socket && profile.tls != "disable")
        return invalid(tls_, tr("Unix sockets require TLS disabled."));
    if (socket && profile.sshEnabled)
        return invalid(sshEnabled_, tr("Unix sockets cannot use an SSH tunnel."));
    if (profile.driver == "mysql" && profile.tls == "prefer")
        return invalid(
            tls_, tr("MySQL does not support Prefer TLS. Choose Require or a verification mode."));
    if (!socket && !validServerHost(profile.host))
        return invalid(host_, tr("Host must be a hostname or IPv4/IPv6 address. "
                                 "Enter the port separately; omit URLs and usernames."));
    if (profile.driver == "postgres" && profile.user.isEmpty() &&
        authenticationMethod(profile) != "pg_pass")
        return invalid(user_, tr("Enter the database username."));
    if (!profile.sshEnabled)
        return validate();
    if (!validServerHost(profile.sshHost))
        return invalid(sshHost_, tr("SSH host must be a hostname or IPv4/IPv6 address. "
                                    "Enter the SSH port separately; omit URLs and usernames."));
    if (profile.sshUser.isEmpty())
        return invalid(sshUser_, tr("Enter the SSH username."));
    if (!validateSecurityDraft(profile))
        return false;
    if (profile.sshAuthentication == "public_key" && profile.sshIdentitySource == "file" &&
        profile.sshIdentityFile.trimmed().isEmpty()) {
        return invalid(sshIdentityFile_, tr("Choose an SSH private key file."));
    }
    if (profile.sshAuthentication == "password" && sshSecret_->text().isEmpty() &&
        profile.sshCredentialRef.isEmpty()) {
        return invalid(sshSecret_, tr("Enter the SSH password."));
    }
    return validate();
}
} // namespace choscordb
