#include "widgets/profile_dialog/ssh_hop_editor.h"
#include "design_system/button/button.h"
#include "design_system/theme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QUuid>
#include <QVBoxLayout>
namespace choscordb {
SshHopEditor::SshHopEditor(QWidget* parent) : QWidget(parent) {
    const auto metrics = design::resolveMetrics(design::Density::Compact, true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* help = new QLabel(tr("Connect through these hosts in order before the final SSH server. "
                               "Each host has its own authentication and password."),
                            this);
    help->setWordWrap(true);
    layout->addWidget(help);
    list_ = new QListWidget(this);
    list_->setObjectName("profileSshHopList");
    list_->setAccessibleName(tr("SSH jump hosts in connection order"));
    list_->setMaximumHeight(metrics.dataRowHeight * 5);
    layout->addWidget(list_);
    auto* buttons = new QHBoxLayout;
    const auto button = [this, buttons](const char* name, const QString& title) {
        auto* result = new design::Button(title, this);
        result->setObjectName(name);
        result->setVariant(design::ButtonVariant::Outline);
        buttons->addWidget(result);
        return result;
    };
    auto* add = button("profileSshHopAdd", tr("Add host"));
    auto* remove = button("profileSshHopRemove", tr("Remove"));
    auto* up = button("profileSshHopUp", tr("Move up"));
    auto* down = button("profileSshHopDown", tr("Move down"));
    buttons->addStretch();
    layout->addLayout(buttons);
    fields_ = new QWidget(this);
    auto* form = new QFormLayout(fields_);
    form->setContentsMargins(0, 0, 0, 0);
    const auto line = [this](const char* name) {
        auto* result = new QLineEdit(fields_);
        result->setObjectName(name);
        result->setMaxLength(16385);
        connect(result, &QLineEdit::textChanged, this, &SshHopEditor::updateRow);
        return result;
    };
    host_ = line("profileSshHopHost");
    user_ = line("profileSshHopUser");
    port_ = new QSpinBox(fields_);
    port_->setObjectName("profileSshHopPort");
    port_->setRange(1, 65535);
    port_->setValue(22);
    form->addRow(tr("Jump host"), host_);
    form->addRow(tr("Port"), port_);
    form->addRow(tr("SSH username"), user_);
    authentication_ = new QComboBox(fields_);
    authentication_->setObjectName("profileSshHopAuthentication");
    authentication_->addItem(tr("Use SSH configuration"), "configured");
    authentication_->addItem(tr("SSH agent"), "agent");
    authentication_->addItem(tr("Password"), "password");
    authentication_->addItem(tr("Private key"), "public_key");
    form->addRow(tr("Authentication"), authentication_);
    identitySource_ = new QComboBox(fields_);
    identitySource_->setObjectName("profileSshHopIdentitySource");
    identitySource_->addItem(tr("Key file"), "file");
    identitySource_->addItem(tr("Paste private key"), "inline");
    form->addRow(tr("Private key source"), identitySource_);
    privateKey_ = new SshPrivateKeyEditor("profileSshHopPrivateKey", fields_);
    identity_ = line("profileSshHopIdentityFile");
    secret_ = line("profileSshHopSecret");
    secret_->setEchoMode(QLineEdit::Password);
    remember_ = new QCheckBox(tr("Save this host's password in OS credential store"), fields_);
    remember_->setObjectName("profileSshHopRemember");
    agent_ = line("profileSshHopAgentSocket");
    knownHosts_ = line("profileSshHopKnownHosts");
    form->addRow(tr("Private key file"), identity_);
    form->addRow(tr("Private key contents"), privateKey_);
    form->addRow(tr("Password / key passphrase"), secret_);
    form->addRow(remember_);
    form->addRow(tr("SSH agent socket (optional)"), agent_);
    form->addRow(tr("Known hosts file (optional)"), knownHosts_);
    inspect_ = new design::Button(tr("Inspect host keys…"), fields_);
    inspect_->setObjectName("profileSshHopInspectHostKeys");
    static_cast<design::Button*>(inspect_)->setVariant(design::ButtonVariant::Outline);
    form->addRow(inspect_);
    connect(inspect_, &QPushButton::clicked, this, [this] {
        if (selected_ < 0 || selected_ >= hops_.size() || !inspectionEnabled_)
            return;
        SshHostKeyTarget target;
        target.kind = hops_[selected_].id.isEmpty() ? SshHostKeyTarget::Kind::JumpIndex
                                                    : SshHostKeyTarget::Kind::JumpId;
        target.id = hops_[selected_].id;
        target.index = selected_;
        emit hostKeyInspectionRequested(target);
    });
    layout->addWidget(fields_);
    connect(list_, &QListWidget::currentRowChanged, this, &SshHopEditor::selectRow);
    connect(port_, &QSpinBox::valueChanged, this, &SshHopEditor::updateRow);
    connect(remember_, &QCheckBox::toggled, this, &SshHopEditor::updateRow);
    connect(privateKey_, &SshPrivateKeyEditor::changed, this, &SshHopEditor::updateRow);
    connect(identitySource_, &QComboBox::currentIndexChanged, this, [this] {
        if (filling_ || selected_ < 0)
            return;
        hops_[selected_].privateKeyReference.clear();
        privateKey_->setDraft({});
        updateRow();
        updateAuthentication();
    });
    connect(authentication_, &QComboBox::currentIndexChanged, this, [this] {
        if (filling_ || selected_ < 0)
            return;
        auto& hop = hops_[selected_];
        hop.reference.clear();
        hop.credential = {};
        hop.privateKeyReference.clear();
        privateKey_->setDraft({});
        if (authentication_->currentData() != "public_key")
            hop.identity.clear();
        {
            const QSignalBlocker secretBlocker(secret_), rememberBlocker(remember_),
                identityBlocker(identity_);
            secret_->clear();
            remember_->setChecked(false);
            identity_->setText(hop.identity);
        }
        updateRow();
        updateAuthentication();
    });
    connect(secret_, &QLineEdit::textEdited, this, [this] {
        if (!filling_ && selected_ >= 0)
            hops_[selected_].credential.modified = true;
    });
    connect(add, &QPushButton::clicked, this, [this] {
        if (hops_.size() >= 5)
            return;
        Hop hop;
        hop.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        hops_.append(hop);
        refreshList(hops_.size() - 1);
        host_->setFocus();
        emit changed();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (selected_ < 0)
            return;
        const auto row = selected_;
        hops_.removeAt(row);
        refreshList(qMin(row, int(hops_.size()) - 1));
        emit changed();
    });
    const auto move = [this](int delta) {
        const auto next = selected_ + delta;
        if (selected_ < 0 || next < 0 || next >= hops_.size())
            return;
        hops_.swapItemsAt(selected_, next);
        refreshList(next);
        emit changed();
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(down, &QPushButton::clicked, this, [move] { move(1); });
    selectRow(-1);
}
bool SshHopEditor::usesSecret(const Hop& hop) {
    return hop.authentication == "password" || hop.authentication == "public_key";
}
void SshHopEditor::updateRow() {
    if (filling_ || selected_ < 0 || selected_ >= hops_.size())
        return;
    auto& hop = hops_[selected_];
    hop.host = host_->text();
    hop.port = port_->value();
    hop.user = user_->text();
    hop.authentication = authentication_->currentData().toString();
    if (usesSecret(hop) && hop.id.isEmpty())
        hop.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (hop.identity != identity_->text())
        hop.reference.clear();
    hop.identity = identity_->text();
    hop.identitySource = identitySource_->currentData().toString();
    hop.credential.privateKey = privateKey_->draft();
    hop.agent = agent_->text();
    hop.knownHosts = knownHosts_->text();
    if (hop.credential.secret != secret_->text())
        hop.credential.modified = true;
    hop.credential.secret = secret_->text();
    hop.credential.remember = remember_->isChecked();
    if (auto* item = list_->item(selected_))
        item->setText(tr("%1. %2@%3:%4").arg(selected_ + 1).arg(hop.user, hop.host).arg(hop.port));
    emit changed();
}
void SshHopEditor::updateAuthentication() {
    const bool key = authentication_->currentData() == "public_key";
    const bool secret = key || authentication_->currentData() == "password";
    identitySource_->setEnabled(key);
    const bool inlineKey = key && identitySource_->currentData() == "inline";
    identity_->setEnabled(key && !inlineKey);
    privateKey_->setEnabled(inlineKey);
    auto* form = qobject_cast<QFormLayout*>(fields_->layout());
    form->setRowVisible(identitySource_, key);
    form->setRowVisible(privateKey_, inlineKey);
    form->setRowVisible(identity_, key && !inlineKey);
    secret_->setEnabled(secret);
    remember_->setEnabled(secret);
}
void SshHopEditor::selectRow(int row) {
    filling_ = true;
    selected_ = row;
    const Hop hop = row >= 0 && row < hops_.size() ? hops_[row] : Hop{};
    host_->setText(hop.host);
    port_->setValue(hop.port);
    user_->setText(hop.user);
    authentication_->setCurrentIndex(authentication_->findData(hop.authentication));
    identity_->setText(hop.identity);
    identitySource_->setCurrentIndex(identitySource_->findData(hop.identitySource));
    privateKey_->setDraft(hop.credential.privateKey, !hop.privateKeyReference.isEmpty());
    agent_->setText(hop.agent);
    knownHosts_->setText(hop.knownHosts);
    secret_->setText(hop.credential.secret);
    secret_->setModified(hop.credential.modified);
    secret_->setPlaceholderText(hop.reference.isEmpty()
                                    ? tr("This host's password or optional key passphrase")
                                    : tr("Saved credential — leave unchanged to keep"));
    remember_->setChecked(hop.credential.remember);
    fields_->setEnabled(row >= 0);
    inspect_->setEnabled(inspectionEnabled_ && row >= 0);
    updateAuthentication();
    filling_ = false;
}
void SshHopEditor::refreshList(int selected) {
    const QSignalBlocker blocker(list_);
    list_->clear();
    for (int row = 0; row < hops_.size(); ++row) {
        const auto& hop = hops_[row];
        list_->addItem(tr("%1. %2@%3:%4").arg(row + 1).arg(hop.user, hop.host).arg(hop.port));
    }
    list_->setCurrentRow(selected);
    selectRow(selected);
    findChild<QPushButton*>("profileSshHopAdd")->setEnabled(hops_.size() < 5);
}
void SshHopEditor::setDraft(const QJsonArray& values, const QJsonObject& references,
                            const QJsonObject& privateKeyReferences) {
    const auto selectedId =
        selected_ >= 0 && selected_ < hops_.size() ? hops_[selected_].id : QString();
    hops_.clear();
    for (const auto& value : values) {
        const auto object = value.toObject();
        Hop hop;
        hop.id = object["id"].toString();
        hop.host = object["host"].toString();
        hop.port = object["port"].toInt(22);
        hop.user = object["user"].toString();
        hop.authentication = object["authentication"].toString("configured");
        hop.identity = object["identity_file"].toString();
        hop.identitySource = object["identity_source"].toString("file");
        hop.privateKeyReference = privateKeyReferences[hop.id].toString();
        hop.credential.privateKey.remember = !hop.privateKeyReference.isEmpty();
        hop.agent = object["agent_socket"].toString();
        hop.knownHosts = object["known_hosts_file"].toString();
        hop.reference = references[hop.id].toString();
        hop.credential.remember = !hop.reference.isEmpty() && usesSecret(hop);
        hops_.append(hop);
    }
    int selected = hops_.isEmpty() ? -1 : 0;
    if (!selectedId.isEmpty())
        for (int row = 0; row < hops_.size(); ++row)
            if (hops_[row].id == selectedId)
                selected = row;
    refreshList(selected);
}
QJsonArray SshHopEditor::draft() const {
    QJsonArray result;
    for (const auto& hop : hops_) {
        QJsonObject value{{"host", hop.host},
                          {"port", hop.port},
                          {"user", hop.user},
                          {"authentication", hop.authentication}};
        if (!hop.id.isEmpty())
            value["id"] = hop.id;
        if (hop.authentication == "public_key")
            value["identity_source"] = hop.identitySource;
        if (hop.authentication == "public_key" && hop.identitySource == "file" &&
            !hop.identity.isEmpty())
            value["identity_file"] = hop.identity;
        if (!hop.agent.isEmpty())
            value["agent_socket"] = hop.agent;
        if (!hop.knownHosts.isEmpty())
            value["known_hosts_file"] = hop.knownHosts;
        result.append(value);
    }
    return result;
}
QJsonObject SshHopEditor::references() const {
    QJsonObject result;
    for (const auto& hop : hops_)
        if (usesSecret(hop) && !hop.id.isEmpty() && !hop.reference.isEmpty())
            result[hop.id] = hop.reference;
    return result;
}
QJsonObject SshHopEditor::privateKeyReferences() const {
    QJsonObject result;
    for (const auto& hop : hops_)
        if (hop.authentication == "public_key" && hop.identitySource == "inline" &&
            !hop.id.isEmpty() && !hop.privateKeyReference.isEmpty())
            result[hop.id] = hop.privateKeyReference;
    return result;
}
QList<SshHopCredential> SshHopEditor::credentials(bool saving) const {
    QList<SshHopCredential> result;
    for (const auto& hop : hops_) {
        if (!usesSecret(hop) || hop.id.isEmpty())
            continue;
        SshHopCredential credential;
        credential.id = hop.id;
        if (saving) {
            credential.action = "clear";
            if (hop.credential.remember) {
                if (hop.credential.modified || !hop.credential.secret.isEmpty()) {
                    credential.action = hop.credential.secret.isEmpty() ? "clear" : "replace";
                    if (credential.action == "replace")
                        credential.secret = hop.credential.secret;
                } else if (!hop.reference.isEmpty())
                    credential.action = "keep";
            }
        } else {
            credential.hasSecret = hop.credential.modified || !hop.credential.secret.isEmpty();
            if (credential.hasSecret)
                credential.secret = hop.credential.secret;
        }
        credential.privateKeyAction = "clear";
        if (hop.authentication == "public_key" && hop.identitySource == "inline") {
            const auto& key = hop.credential.privateKey;
            if (saving) {
                if (key.remember) {
                    if (key.modified || !key.secret.isEmpty()) {
                        credential.privateKeyAction = key.secret.isEmpty() ? "clear" : "replace";
                        if (credential.privateKeyAction == "replace")
                            credential.privateKey = key.secret;
                    } else if (!hop.privateKeyReference.isEmpty())
                        credential.privateKeyAction = "keep";
                }
            } else {
                credential.hasPrivateKey = key.modified || !key.secret.isEmpty();
                if (credential.hasPrivateKey)
                    credential.privateKey = key.secret;
            }
        }
        result.append(credential);
    }
    return result;
}
SshHopSecrets SshHopEditor::captureSecrets(bool sessionOnly) const {
    SshHopSecrets result;
    for (const auto& hop : hops_) {
        if (!usesSecret(hop) || hop.id.isEmpty())
            continue;
        auto value = hop.credential;
        value.restoreSecret = !sessionOnly || !value.remember;
        value.restorePrivateKey = !sessionOnly || !value.privateKey.remember;
        if (!value.restoreSecret)
            value.secret.clear();
        if (!value.restorePrivateKey)
            value.privateKey.secret.clear();
        result.insert(hop.id, value);
    }
    return result;
}
void SshHopEditor::restoreSecrets(const SshHopSecrets& values) {
    for (auto& hop : hops_)
        if (usesSecret(hop) && values.contains(hop.id)) {
            const auto& value = values[hop.id];
            if (value.restoreSecret) {
                hop.credential.secret = value.secret;
                hop.credential.modified = value.modified;
                hop.credential.remember = value.remember;
            }
            if (value.restorePrivateKey)
                hop.credential.privateKey = value.privateKey;
        }
    selectRow(selected_);
}
void SshHopEditor::setInspectionEnabled(bool enabled) {
    inspectionEnabled_ = enabled;
    inspect_->setEnabled(enabled && selected_ >= 0);
}
bool SshHopEditor::setKnownHosts(const SshHostKeyTarget& target, const QString& path) {
    int row = target.index;
    if (target.kind == SshHostKeyTarget::Kind::JumpId) {
        row = -1;
        for (int i = 0; i < hops_.size(); ++i)
            if (hops_[i].id == target.id)
                row = i;
    }
    if (row < 0 || row >= hops_.size())
        return false;
    hops_[row].knownHosts = path;
    if (row == selected_)
        knownHosts_->setText(path);
    emit changed();
    return true;
}
} // namespace choscordb
