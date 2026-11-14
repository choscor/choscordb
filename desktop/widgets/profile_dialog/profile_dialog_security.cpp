#include "widgets/profile_dialog/profile_dialog.h"
#include <QComboBox>
#include <QLineEdit>
namespace choscordb {
void ProfileDialog::setSecurityDraft(const SavedProfile& value) {
    tlsSecret_->clear();
    tlsSecret_->setModified(false);
    tlsSecret_->setPlaceholderText(value.tlsCredentialRef.isEmpty() ? QString()
                                                                    : tr("Saved password"));
    sshIdentitySource_->setCurrentIndex(sshIdentitySource_->findData(value.sshIdentitySource));
    sshPrivateKey_->setDraft({{}, false}, !value.sshPrivateKeyRef.isEmpty());
    updatePrivateKeyControls();
}
}
