#include "design_system/field/field.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
namespace choscordb {
void ProfileDialog::createPrivateKeyControls(QFormLayout* form, QGridLayout* grid) {
    sshIdentitySource_ = new QComboBox(form_);
    sshIdentitySource_->setObjectName("profileSshIdentitySource");
    sshIdentitySource_->addItem(tr("Key file"), "file");
    sshIdentitySource_->addItem(tr("Paste private key"), "inline");
    sshIdentitySourceLabel_ = new QLabel(tr("Private key source"), sshFields_);
    sshIdentitySourceLabel_->setBuddy(sshIdentitySource_);
    grid->addWidget(sshIdentitySourceLabel_, 4, 0, 1, 2);
    grid->addWidget(sshIdentitySource_, 5, 0, 1, 2);
    sshPrivateKey_ = new SshPrivateKeyEditor("profileSshPrivateKey", form_);
    form->addRow(tr("Private key contents"), sshPrivateKey_);
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
    sshIdentitySourceLabel_->setVisible(key);
    sshIdentitySource_->setVisible(key);
    sshIdentityFileLabel_->setVisible(key && !inlineKey);
    validationFor(sshIdentityFile_)->setVisible(key && !inlineKey);
    if (auto* form = qobject_cast<QFormLayout*>(sshPrivateKey_->parentWidget()->layout())) {
        form->setRowVisible(sshPrivateKey_, inlineKey);
    }
}
} // namespace choscordb
