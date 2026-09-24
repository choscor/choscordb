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
    connect(proxyEnabled_, &QCheckBox::toggled, this, changed);
    connect(proxyProtocol_, &QComboBox::currentIndexChanged, this, changed);
    connect(proxyPort_, &QSpinBox::valueChanged, this, changed);
    connect(rememberProxySecret_, &QCheckBox::toggled, this, changed);
}
void ProfileDialog::updateProxyControls() {
    if (!proxySecret_ || !rememberProxySecret_)
        return;
    const bool enabled = driver_->currentData() != "sqlite" && proxyEnabled_->isChecked();
    proxyFields_->setVisible(enabled);
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
    if (profile.driver == "sqlite" || !proxyEnabled_->isChecked()) {
        profile.proxyOptions.clear();
        profile.proxyCredentialRef.clear();
        return;
    }
    const auto username = proxyUser_->text();
    const QJsonObject options{
        {"protocol", proxyProtocol_->currentData().toString()},
        {"host", proxyHost_->text()},
        {"port", proxyPort_->value()},
        {"username", username.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(username)}};
    const auto previous = QJsonDocument::fromJson(current_.proxyOptions.toUtf8()).object();
    profile.proxyOptions = QString::fromUtf8(QJsonDocument(options).toJson(QJsonDocument::Compact));
    if (!proxyNeedsPassword(profile) || previous["username"] != options["username"] ||
        previous["protocol"] != options["protocol"])
        profile.proxyCredentialRef.clear();
}
void ProfileDialog::setProxyDraft(const SavedProfile& profile) {
    const auto options = QJsonDocument::fromJson(profile.proxyOptions.toUtf8()).object();
    proxyEnabled_->setChecked(!options.isEmpty());
    proxyProtocol_->setCurrentIndex(options["protocol"] == "socks4" ? 1 : 0);
    proxyHost_->setText(options["host"].toString());
    proxyPort_->setValue(options["port"].toInt(1080));
    proxyUser_->setText(options["username"].toString());
    proxySecret_->clear();
    proxySecret_->setModified(false);
    proxySecret_->setPlaceholderText(profile.proxyCredentialRef.isEmpty()
                                         ? tr("SOCKS5 password")
                                         : tr("Saved proxy password — leave unchanged to keep"));
    rememberProxySecret_->setChecked(!profile.proxyCredentialRef.isEmpty());
    updateProxyControls();
}
} // namespace choscordb
