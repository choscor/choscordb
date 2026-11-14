#include "design_system/dialog_sections/dialog_sections.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
namespace choscordb {
void ProfileDialog::createPrivateKeyControls() {
    sshIdentitySource_ = new QComboBox(form_);
    sshIdentitySource_->setObjectName("profileSshIdentitySource");
    sshIdentitySource_->addItem(tr("Key file"), "file");
    sshIdentitySource_->addItem(tr("Paste private key"), "inline");
    sections_->addFormRow(formLayout_, tr("Private key source"), sshIdentitySource_);
    sections_->addFormRow(formLayout_, tr("Private key file"), validated(sshIdentityFile_));
    sshPrivateKey_ = new SshPrivateKeyEditor("profileSshPrivateKey", form_);
    sections_->addFormRow(formLayout_, tr("Private key contents"), sshPrivateKey_);
    sshPrivateKeyHelp_ =
        createDescription(tr("Paste a complete private key. Its contents stay hidden."), form_);
    sshPrivateKeyHelp_->setObjectName("profileSshPrivateKeyHelp");
    formLayout_->addRow(QString(), sshPrivateKeyHelp_);
    QWidget::setTabOrder(sshAuthentication_, sshIdentitySource_);
    QWidget::setTabOrder(sshIdentitySource_, sshIdentityFile_);
    QWidget::setTabOrder(sshIdentityFile_, sshSecret_);
    connect(sshIdentitySource_, &QComboBox::currentIndexChanged, this, [this] {
        if (!filling_) {
            sshPrivateKey_->setDraft({});
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
    updateRowVisibility();
}
}
