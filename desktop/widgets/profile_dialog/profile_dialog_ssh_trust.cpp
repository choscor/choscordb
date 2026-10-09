#include "design_system/button/button.h"
#include "design_system/status_line/status_line.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/profile_dialog/ssh_host_key_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QTimer>
namespace choscordb {
void ProfileDialog::createTrustControls(QFormLayout* form) {
    inspectSshKeys_ = new design::Button(tr("Inspect host keys…"), form_);
    inspectSshKeys_->setObjectName("profileSshInspectHostKeys");
    static_cast<design::Button*>(inspectSshKeys_)->setVariant(design::ButtonVariant::Outline);
    form->addRow(tr("Final SSH server trust"), inspectSshKeys_);
    connect(inspectSshKeys_, &QPushButton::clicked, this, &ProfileDialog::inspectHostKeys);
    connect(sshEnabled_, &QCheckBox::toggled, this, &ProfileDialog::updateTrustControls);
    connect(adapter_, &EngineAdapter::sshHostKeysInspected, this,
            [this](quint64 token, const QList<SshHostKeyCandidate>& candidates) {
                if (token != trustToken_)
                    return;
                trustToken_ = 0;
                statusLine_->setBusy(false);
                statusLine_->setMessage(
                    tr("SSH host keys inspected. Review the keys before approving."));
                statusLine_->setAvailable(true);
                statusLine_->show();
                updateTrustControls();
                if (revision_ != trustRevision_) {
                    showTrustStatus(
                        tr("Connection settings changed. Inspect the host keys again."));
                    return;
                }
                if (candidates.isEmpty()) {
                    showTrustStatus(tr("No SSH host keys were returned."));
                    return;
                }
                if (trustDialog_)
                    trustDialog_->close();
                auto* review = new SshHostKeyDialog(candidates, trustPath_, this);
                review->setAttribute(Qt::WA_DeleteOnClose);
                trustDialog_ = review;
                auto* validity = new QTimer(review);
                validity->setInterval(50);
                connect(validity, &QTimer::timeout, review, [this, review, validity] {
                    if (revision_ != trustRevision_) {
                        review->invalidate();
                        validity->stop();
                    }
                });
                validity->start();
                connect(review, &SshHostKeyDialog::approvalRequested, this,
                        [this, review](const SshHostKeyCandidate& candidate, const QString& path) {
                            if (!adapter_ || trustToken_ || revision_ != trustRevision_) {
                                review->invalidate();
                                return;
                            }
                            trustPath_ = path;
                            trustToken_ = ++token_;
                            updateTrustControls();
                            adapter_->approveSshHostKey(candidate, path, trustToken_);
                        });
                connect(review, &SshHostKeyDialog::retryRequested, this, [this, review] {
                    if (revision_ != trustRevision_) {
                        review->invalidate();
                        return;
                    }
                    review->close();
                    connectDraft(true);
                });
                review->open();
            });
    connect(adapter_, &EngineAdapter::sshHostKeyApproved, this,
            [this](quint64 token, const QString& outcome) {
                if (token != trustToken_)
                    return;
                trustToken_ = 0;
                updateTrustControls();
                if (revision_ != trustRevision_) {
                    if (trustDialog_)
                        trustDialog_->invalidate();
                    showTrustStatus(tr("The approval request finished for the previous settings. "
                                       "Inspect the current SSH server before retrying."));
                    return;
                }
                if (outcome == "approved") {
                    statusLine_->setBusy(false);
                    statusLine_->setAvailable(true);
                    statusLine_->setMessage(
                        tr("SSH host key approved. Retry the connection to use it."));
                    statusLine_->show();
                } else {
                    showTrustStatus(tr("SSH host key approval outcome is unknown."));
                }
                if (trustDialog_)
                    trustDialog_->finishApproval(outcome);
            });
    connect(adapter_, &EngineAdapter::sshHostKeyOperationFailed, this,
            [this](quint64 token, const QString& error) {
                if (token != trustToken_)
                    return;
                trustToken_ = 0;
                updateTrustControls();
                showTrustStatus(error, true);
                if (trustDialog_)
                    trustDialog_->showFailure(error);
            });
    updateTrustControls();
}
void ProfileDialog::showTrustStatus(const QString& message, bool failure) {
    statusLine_->setMessage(message);
    statusLine_->setBusy(false);
    statusLine_->setAvailable(!failure);
    if (!failure)
        statusLine_->setNeutral();
    statusLine_->show();
}
void ProfileDialog::updateTrustControls() {
    if (!inspectSshKeys_)
        return;
    const bool enabled = adapter_ && !busy_ && !trustToken_ && sshEnabled_->isChecked() &&
                         EngineAdapter::profileDriverForm(driver_->currentData().toString()).server;
    inspectSshKeys_->setEnabled(enabled);
}
void ProfileDialog::inspectHostKeys() {
    if (!adapter_ || busy_ || trustToken_ || !sshEnabled_->isChecked())
        return;
    auto profile = draft();
    if (!profile.sshEnabled)
        return;
    if (profile.name.isEmpty())
        profile.name = tr("SSH host inspection");
    trustRevision_ = revision_;
    trustToken_ = ++token_;
    updateTrustControls();
    statusLine_->setMessage(tr("Inspecting SSH host keys…"));
    statusLine_->setAvailable(true);
    statusLine_->setBusy(true);
    statusLine_->show();
    adapter_->inspectSshHostKeys(profile, trustToken_);
}
} // namespace choscordb
