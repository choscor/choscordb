#include "widgets/profile_dialog/profile_dialog.h"
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLineEdit>
namespace choscordb {
void ProfileDialog::createConnectionControls(QFormLayout* ssh, QGridLayout* grid) {
    createPrivateKeyControls(ssh, grid);
    createTrustControls(ssh);
}
void ProfileDialog::setSecurityDraft(const SavedProfile& value) {
    tlsSecret_->clear();
    tlsSecret_->setModified(false);
    tlsSecret_->setPlaceholderText(value.tlsCredentialRef.isEmpty()
                                       ? tr("Client identity password")
                                       : tr("Saved identity password — leave unchanged to keep"));
    sshIdentitySource_->setCurrentIndex(sshIdentitySource_->findData(value.sshIdentitySource));
    sshPrivateKey_->setDraft({{}, false}, !value.sshPrivateKeyRef.isEmpty());
    updatePrivateKeyControls();
}
} // namespace choscordb
