#include "design_system/theme.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
namespace choscordb {
void ProfileDialog::createConnectionControls(QFormLayout* security, QFormLayout* ssh) {
    createAuthenticationControls(qobject_cast<QFormLayout*>(postgresFields_->layout()));
    createProxyControls(qobject_cast<QFormLayout*>(postgresFields_->layout()));
    const auto changed = [this] {
        if (!filling_) {
            dirty_ = true;
            ++revision_;
        }
    };
    const auto line = [this, changed](const char* name) {
        auto* field = new QLineEdit(form_);
        field->setObjectName(name);
        field->setMaxLength(16384);
        connect(field, &QLineEdit::textEdited, this, changed);
        return field;
    };
    tlsClientIdentity_ = line("profileTlsClientIdentity");
    tlsClientIdentity_->setPlaceholderText(
        tr("Optional PKCS#12 client certificate (.p12 or .pfx)"));
    security->addRow(tr("Client identity"), validated(tlsClientIdentity_));
    tlsSecret_ = line("profileTlsSecret");
    tlsSecret_->setEchoMode(QLineEdit::Password);
    tlsSecret_->setPlaceholderText(tr("PKCS#12 password; leave blank for an unencrypted identity"));
    security->addRow(tr("Client identity password"), validated(tlsSecret_));
    rememberTlsSecret_ = new QCheckBox(tr("Save identity password in OS credential store"), form_);
    rememberTlsSecret_->setObjectName("profileRememberTlsSecret");
    connect(rememberTlsSecret_, &QCheckBox::toggled, this, changed);
    security->addRow(rememberTlsSecret_);
    const auto spin = [this, changed](const char* name, int minimum, int maximum, int value) {
        auto* field = new QSpinBox(form_);
        field->setObjectName(name);
        field->setRange(minimum, maximum);
        field->setValue(value);
        connect(field, &QSpinBox::valueChanged, this, changed);
        return field;
    };
    sshRemoteHost_ = line("profileSshRemoteHost");
    sshRemoteHost_->setPlaceholderText(tr("Optional — use the database host"));
    sshRemotePort_ = spin("profileSshRemotePort", 0, 65535, 0);
    sshRemotePort_->setSpecialValueText(tr("Use database port"));
    ssh->addRow(tr("Forwarding destination host"), sshRemoteHost_);
    ssh->addRow(tr("Forwarding destination port"), sshRemotePort_);
    sshRemoteHost_->setToolTip(
        tr("Destination reached from the SSH server. TLS still verifies the database hostname."));
    sshRemotePort_->setToolTip(sshRemoteHost_->toolTip());
    sshLocalBinding_ = new QCheckBox(tr("Use a local forwarding listener"), form_);
    sshLocalBinding_->setObjectName("profileSshLocalBinding");
    connect(sshLocalBinding_, &QCheckBox::toggled, this, changed);
    sshLocalHost_ = line("profileSshLocalHost");
    sshLocalHost_->setPlaceholderText(tr("127.0.0.1 (default); IPv4 or IPv6 address"));
    sshLocalPort_ = spin("profileSshLocalPort", 0, 65535, 0);
    sshLocalPort_->setSpecialValueText(tr("Choose automatically"));
    sshShareTunnels_ = new QCheckBox(tr("Share compatible SSH tunnels"), form_);
    sshShareTunnels_->setObjectName("profileSshShareTunnels");
    sshShareTunnels_->setToolTip(tr(
        "Reuse an SSH session only when its destination, credentials and trust settings match."));
    connect(sshShareTunnels_, &QCheckBox::toggled, this, changed);
    sshLocalBindingWarning_ = new QLabel(
        tr("This address exposes the forwarded database port on all network interfaces."), form_);
    sshLocalBindingWarning_->setObjectName("profileSshLocalBindingWarning");
    sshLocalBindingWarning_->setWordWrap(true);
    const auto updateBinding = [this] {
        const bool enabled = sshLocalBinding_->isChecked();
        sshLocalHost_->setEnabled(enabled);
        sshLocalPort_->setEnabled(enabled);
        const auto address = sshLocalHost_->text();
        auto zeroAddress = address;
        zeroAddress.remove(QRegularExpression("[\\[\\]:0.]"));
        const bool wildcard =
            address == "0.0.0.0" || (address.contains(':') && zeroAddress.isEmpty());
        sshLocalBindingWarning_->setVisible(enabled && wildcard);
    };
    connect(sshLocalBinding_, &QCheckBox::toggled, this, updateBinding);
    connect(sshLocalHost_, &QLineEdit::textChanged, this, updateBinding);
    ssh->addRow(sshLocalBinding_);
    ssh->addRow(tr("Local bind address"), sshLocalHost_);
    ssh->addRow(tr("Local port (0 chooses automatically)"), sshLocalPort_);
    ssh->addRow(sshLocalBindingWarning_);
    ssh->addRow(sshShareTunnels_);
    updateBinding();
    sshTimeout_ = spin("profileSshTimeout", 1, 300, 15);
    sshTimeout_->setSuffix(tr(" s"));
    sshKeepalive_ = spin("profileSshKeepalive", 0, 86400, 0);
    sshKeepalive_->setSuffix(tr(" s"));
    sshKeepaliveCount_ = spin("profileSshKeepaliveCount", 1, 100, 3);
    sshAgentSocket_ = line("profileSshAgentSocket");
    sshKnownHosts_ = line("profileSshKnownHosts");
    ssh->addRow(tr("SSH connection timeout"), sshTimeout_);
    ssh->addRow(tr("Keepalive interval (0 disables)"), sshKeepalive_);
    ssh->addRow(tr("Unanswered keepalives"), sshKeepaliveCount_);
    ssh->addRow(tr("Custom agent socket"), sshAgentSocket_);
    ssh->addRow(tr("Known hosts file"), sshKnownHosts_);
    createPrivateKeyControls(ssh);
    sshHopEditor_ = new SshHopEditor(form_);
    connect(sshHopEditor_, &SshHopEditor::changed, this, changed);
    ssh->addRow(tr("SSH jump hosts"), sshHopEditor_);
    createTrustControls(ssh);
    // Retain the internal widgets for older saved drafts and asynchronous state,
    // but keep the connection form limited to the four basic SSH settings.
    for (QWidget* control : QList<QWidget*>{
             sshRemoteHost_, sshRemotePort_, sshLocalBinding_, sshLocalHost_, sshLocalPort_,
             sshLocalBindingWarning_, sshShareTunnels_, sshTimeout_, sshKeepalive_,
             sshKeepaliveCount_, sshAgentSocket_, sshKnownHosts_, sshHopEditor_, inspectSshKeys_})
        ssh->setRowVisible(control, false);
}
QString ProfileDialog::sshOptionsDraft() const {
    return QStringLiteral("{}");
}
bool ProfileDialog::validateSecurityDraft(const SavedProfile&) {
    return true;
}
void ProfileDialog::writeSecurityDraft(SavedProfile& value) const {
    value.tlsClientIdentity = tlsClientIdentity_->text();
    if (value.tlsClientIdentity != current_.tlsClientIdentity)
        value.tlsCredentialRef.clear();
    value.sshIdentitySource = value.sshAuthentication == "public_key"
                                  ? sshIdentitySource_->currentData().toString()
                                  : QStringLiteral("file");
    if (value.sshIdentitySource != "file")
        value.sshIdentityFile.clear();
    if (!value.sshEnabled || value.sshAuthentication != "public_key" ||
        value.sshIdentitySource != "inline")
        value.sshPrivateKeyRef.clear();
    value.sshOptions = sshOptionsDraft();
    value.sshJumpPrivateKeyRefs = QStringLiteral("{}");
    value.sshJumpCredentialRefs = QStringLiteral("{}");
}
void ProfileDialog::setSecurityDraft(const SavedProfile& value) {
    tlsClientIdentity_->setText(value.tlsClientIdentity);
    tlsSecret_->clear();
    tlsSecret_->setModified(false);
    tlsSecret_->setPlaceholderText(
        value.tlsCredentialRef.isEmpty()
            ? tr("PKCS#12 password; leave blank for an unencrypted identity")
            : tr("Saved identity password — leave unchanged to keep"));
    rememberTlsSecret_->setChecked(!value.tlsCredentialRef.isEmpty());
    sshIdentitySource_->setCurrentIndex(sshIdentitySource_->findData(value.sshIdentitySource));
    sshPrivateKey_->setDraft({{}, false, !value.sshPrivateKeyRef.isEmpty()},
                             !value.sshPrivateKeyRef.isEmpty());
    updatePrivateKeyControls();
    const auto options = QJsonDocument::fromJson(value.sshOptions.toUtf8()).object();
    sshLocalHost_->setText(options["local_host"].toString());
    sshLocalPort_->setValue(options["local_port"].toInt(0));
    sshLocalBinding_->setChecked(
        (!options["local_host"].isNull() && !options["local_host"].isUndefined()) ||
        (!options["local_port"].isNull() && !options["local_port"].isUndefined()));
    sshShareTunnels_->setChecked(options["share_tunnels"].toBool(false));
    sshRemoteHost_->setText(options["remote_host"].toString());
    sshRemotePort_->setValue(options["remote_port"].toInt(0));
    sshTimeout_->setValue(options["connect_timeout_seconds"].toInt(15));
    sshKeepalive_->setValue(options["server_alive_interval_seconds"].toInt(0));
    sshKeepaliveCount_->setValue(options["server_alive_count_max"].toInt(3));
    sshAgentSocket_->setText(options["agent_socket"].toString());
    sshKnownHosts_->setText(options["known_hosts_file"].toString());
    sshHopEditor_->setDraft(options["jump_hosts"].toArray(),
                            QJsonDocument::fromJson(value.sshJumpCredentialRefs.toUtf8()).object(),
                            QJsonDocument::fromJson(value.sshJumpPrivateKeyRefs.toUtf8()).object());
}
} // namespace choscordb
