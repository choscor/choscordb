#include "widgets/profile_dialog/profile_dialog.h"
#include "design_system/field/field.h"
#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
namespace choscordb {
void ProfileDialog::createPrivateKeyControls(QFormLayout* form) {
    sshIdentitySource_ = new QComboBox(form_);
    sshIdentitySource_->setObjectName("profileSshIdentitySource");
    sshIdentitySource_->addItem(tr("Key file"), "file");
    sshIdentitySource_->addItem(tr("Paste private key"), "inline");
    int identityRow = 0;
    QFormLayout::ItemRole identityRole;
    form->getWidgetPosition(validationFor(sshIdentityFile_), &identityRow, &identityRole);
    form->insertRow(identityRow, tr("Private key source"), sshIdentitySource_);
    sshPrivateKey_ = new SshPrivateKeyEditor("profileSshPrivateKey", form_);
    form->insertRow(identityRow + 2, tr("Private key contents"), sshPrivateKey_);
    connect(sshIdentitySource_, &QComboBox::currentIndexChanged, this, [this] {
        if (!filling_) {
            sshPrivateKey_->setDraft({});
            current_.sshPrivateKeyRef.clear();
            dirty_ = true;
            ++revision_;
        }
        updatePrivateKeyControls();
    });
    connect(sshPrivateKey_, &SshPrivateKeyEditor::changed, this, [this] {
        if (!filling_) {
            dirty_ = true;
            ++revision_;
        }
    });
}
void ProfileDialog::updatePrivateKeyControls() {
    if (!sshIdentitySource_)
        return;
    const bool key = sshAuthentication_->currentData() == "public_key";
    const bool inlineKey = key && sshIdentitySource_->currentData() == "inline";
    sshIdentitySource_->setEnabled(key);
    sshIdentityFile_->setEnabled(key && !inlineKey);
    sshPrivateKey_->setEnabled(inlineKey);
    if (auto* form = qobject_cast<QFormLayout*>(sshPrivateKey_->parentWidget()->layout())) {
        form->setRowVisible(sshIdentitySource_, key);
        form->setRowVisible(sshPrivateKey_, inlineKey);
        form->setRowVisible(validationFor(sshIdentityFile_), key && !inlineKey);
    }
}
SshPrivateKeyCredential ProfileDialog::privateKeyCredential(const SavedProfile& profile,
                                                            bool saving) const {
    SshPrivateKeyCredential result;
    result.action = "clear";
    if (!profile.sshEnabled || profile.sshAuthentication != "public_key" ||
        profile.sshIdentitySource != "inline")
        return result;
    const auto value = sshPrivateKey_->draft();
    if (saving) {
        if (value.remember) {
            if (value.modified || !value.secret.isEmpty()) {
                result.action = value.secret.isEmpty() ? "clear" : "replace";
                if (result.action == "replace")
                    result.secret = value.secret;
            } else if (!profile.sshPrivateKeyRef.isEmpty())
                result.action = "keep";
        }
    } else {
        result.hasSecret = value.modified || !value.secret.isEmpty();
        if (result.hasSecret)
            result.secret = value.secret;
    }
    return result;
}
} // namespace choscordb
