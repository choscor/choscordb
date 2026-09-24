#include "widgets/profile_dialog/profile_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
namespace choscordb {
void ProfileDialog::createProxyControls(QFormLayout* form) {
    proxyEnabled_ = new QCheckBox(tr("Connect through a SOCKS proxy"), form_);
    proxyEnabled_->setObjectName("profileProxyEnabled");
    form->addRow(proxyEnabled_);
    proxyFields_ = new QWidget(form_);
    auto* fields = new QFormLayout(proxyFields_);
    proxyProtocol_ = new QComboBox(proxyFields_);
    proxyProtocol_->setObjectName("profileProxyProtocol");
    proxyProtocol_->addItem(tr("SOCKS5"), "socks5");
    proxyProtocol_->addItem(tr("SOCKS4"), "socks4");
    fields->addRow(tr("Proxy protocol"), proxyProtocol_);
    const auto changed = [this] {
        if (!filling_) {
            dirty_ = true;
            ++revision_;
        }
        updateProxyControls();
    };
    const auto line = [this, changed](const char* name, int limit) {
        auto* field = new QLineEdit(proxyFields_);
        field->setObjectName(name);
        field->setMaxLength(limit);
        connect(field, &QLineEdit::textChanged, this, changed);
        return field;
    };
    proxyHost_ = line("profileProxyHost", 16384);
    fields->addRow(tr("Proxy host"), proxyHost_);
    proxyPort_ = new QSpinBox(proxyFields_);
    proxyPort_->setObjectName("profileProxyPort");
    proxyPort_->setRange(1, 65535);
    proxyPort_->setValue(1080);
    fields->addRow(tr("Proxy port"), proxyPort_);
    // One character beyond the wire limit makes oversized pastes fail validation.
    proxyUser_ = line("profileProxyUser", 256);
    proxyUser_->setPlaceholderText(tr("Optional — blank means no authentication"));
    fields->addRow(tr("Proxy username / SOCKS4 user ID"), proxyUser_);
    proxySecret_ = line("profileProxySecret", 256);
    proxySecret_->setEchoMode(QLineEdit::Password);
    fields->addRow(tr("Proxy password"), proxySecret_);
    rememberProxySecret_ =
        new QCheckBox(tr("Save proxy password in OS credential store"), proxyFields_);
    rememberProxySecret_->setObjectName("profileRememberProxySecret");
    fields->addRow(rememberProxySecret_);
    auto* help =
        new QLabel(tr("SOCKS5 can use a username and password. SOCKS4 uses only a user ID. "
                      "Passwords are never stored in the profile."),
                   proxyFields_);
    help->setWordWrap(true);
    fields->addRow(help);
    form->addRow(proxyFields_);
    form->setRowVisible(proxyEnabled_, false);
    form->setRowVisible(proxyFields_, false);
    connect(proxyEnabled_, &QCheckBox::toggled, this, changed);
    connect(proxyProtocol_, &QComboBox::currentIndexChanged, this, changed);
    connect(proxyPort_, &QSpinBox::valueChanged, this, changed);
    connect(rememberProxySecret_, &QCheckBox::toggled, this, changed);
}
void ProfileDialog::updateProxyControls() {
    if (!proxySecret_ || !rememberProxySecret_)
        return;
    const bool enabled = driver_->currentData() != "sqlite" && proxyEnabled_->isChecked();
    proxyFields_->setVisible(false);
    const bool password =
        enabled && proxyProtocol_->currentData() == "socks5" && !proxyUser_->text().isEmpty();
    proxySecret_->setEnabled(password);
    rememberProxySecret_->setEnabled(password);
}
bool ProfileDialog::proxyNeedsPassword(const SavedProfile& profile) {
    const auto options = QJsonDocument::fromJson(profile.proxyOptions.toUtf8()).object();
    return profile.driver != "sqlite" && options["protocol"] == "socks5" &&
           !options["username"].toString().isEmpty();
}
void ProfileDialog::writeProxyDraft(SavedProfile& profile) const {
    profile.proxyOptions.clear();
    profile.proxyCredentialRef.clear();
}
void ProfileDialog::setProxyDraft(const SavedProfile& profile) {
    Q_UNUSED(profile);
    proxyEnabled_->setChecked(false);
    proxyHost_->clear();
    proxyUser_->clear();
    proxySecret_->clear();
    proxySecret_->setModified(false);
    rememberProxySecret_->setChecked(false);
    updateProxyControls();
}
} // namespace choscordb
