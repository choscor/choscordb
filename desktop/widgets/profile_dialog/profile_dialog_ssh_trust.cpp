#include "design_system/button/button.h"
#include "design_system/status_line/status_line.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/profile_dialog/ssh_host_key_dialog.h"
#include "widgets/screen_state/screen_state.h"
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
                updateTrustControls();
                if (revision_ != trustRevision_) {
                    stepTrust(ProfileTestEvent::TestFailed,
                              EngineAdapter::sshTrustNote(SshTrustNote::SettingsChanged));
                    return;
                }
                if (candidates.isEmpty()) {
                    stepTrust(ProfileTestEvent::TestFailed,
                              EngineAdapter::sshTrustNote(SshTrustNote::NoHostKeys));
                    return;
                }
                stepTrust(ProfileTestEvent::HostKeyUnknown,
                          EngineAdapter::sshTrustNote(SshTrustNote::KeysInspected));
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
                            if (!EngineAdapter::profileTestTransition(trustState_,
                                                                      ProfileTestEvent::Approve)
                                     .allowed) {
                                review->invalidate();
                                return;
                            }
                            stepTrust(ProfileTestEvent::Approve);
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
                    stepTrust(ProfileTestEvent::ApprovalFailed,
                              EngineAdapter::sshTrustNote(SshTrustNote::ApprovalStale));
                    return;
                }
                if (outcome == "approved")
                    stepTrust(ProfileTestEvent::Approved);
                else
                    stepTrust(ProfileTestEvent::ApprovalFailed,
                              EngineAdapter::sshTrustNote(SshTrustNote::ApprovalUnknown));
                if (trustDialog_)
                    trustDialog_->finishApproval(outcome);
            });
    connect(adapter_, &EngineAdapter::sshHostKeyOperationFailed, this,
            [this](quint64 token, const QString& error) {
                if (token != trustToken_)
                    return;
                trustToken_ = 0;
                updateTrustControls();
                stepTrust(trustState_ == ProfileTestState::Trusting
                              ? ProfileTestEvent::ApprovalFailed
                              : ProfileTestEvent::TestFailed,
                          error);
                if (trustDialog_)
                    trustDialog_->showFailure(error);
            });
    updateTrustControls();
}
bool ProfileDialog::stepTrust(ProfileTestEvent event, const QString& detail) {
    const auto next = EngineAdapter::profileTestTransition(trustState_, event);
    if (next.allowed)
        trustState_ = next.state;
    const auto shown =
        next.allowed ? EngineAdapter::profileTestPresentation(trustState_, detail)
                     : EngineAdapter::profileTestPresentation(ProfileTestState::Failed, next.error);
    statusLine_->setMessage(shown.message);
    statusLine_->setBusy(shown.busy);
    statusLine_->setState(statusLineState(shown.tone));
    statusLine_->setVisible(!shown.message.isEmpty());
    return next.allowed;
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
    if (trustState_ == ProfileTestState::AwaitingTrust)
        stepTrust(ProfileTestEvent::Reject);
    if (!stepTrust(ProfileTestEvent::Test, EngineAdapter::sshTrustNote(SshTrustNote::Inspecting)))
        return;
    trustRevision_ = revision_;
    trustToken_ = ++token_;
    updateTrustControls();
    adapter_->inspectSshHostKeys(profile, trustToken_);
}
}
