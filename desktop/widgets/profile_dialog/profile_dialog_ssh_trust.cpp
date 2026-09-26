#include "design_system/button/button.h"
#include "design_system/toast_region/toast_region.h"
#include "widgets/profile_dialog/profile_dialog.h"
#include "widgets/profile_dialog/ssh_host_key_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QTimer>
#include <QUuid>
namespace choscordb {
namespace {
bool sameTarget(const SshHostKeyTarget& left, const SshHostKeyTarget& right) {
    if (left.kind != right.kind)
        return false;
    switch (left.kind) {
    case SshHostKeyTarget::Kind::Target:
        return true;
    case SshHostKeyTarget::Kind::JumpId:
        return left.id == right.id;
    case SshHostKeyTarget::Kind::JumpIndex:
        return left.index == right.index;
    }
    return false;
}
int targetIndex(const QJsonArray& hops, const SshHostKeyTarget& target) {
    if (target.kind == SshHostKeyTarget::Kind::Target)
        return hops.size();
    if (target.kind == SshHostKeyTarget::Kind::JumpIndex)
        return target.index >= 0 && target.index < hops.size() ? target.index : -1;
    for (int row = 0; row < hops.size(); ++row)
        if (hops[row].toObject()["id"].toString() == target.id)
            return row;
    return -1;
}
} // namespace
void ProfileDialog::createTrustControls(QFormLayout* form) {
    inspectSshKeys_ = new design::Button(tr("Inspect host keys…"), form_);
    inspectSshKeys_->setObjectName("profileSshInspectHostKeys");
    static_cast<design::Button*>(inspectSshKeys_)->setVariant(design::ButtonVariant::Outline);
    form->addRow(tr("Final SSH server trust"), inspectSshKeys_);
    connect(inspectSshKeys_, &QPushButton::clicked, this, [this] { inspectHostKeys({}); });
    connect(sshHopEditor_, &SshHopEditor::hostKeyInspectionRequested, this,
            &ProfileDialog::inspectHostKeys);
    connect(sshEnabled_, &QCheckBox::toggled, this, &ProfileDialog::updateTrustControls);
    connect(adapter_, &EngineAdapter::sshHostKeysInspected, this,
            [this](quint64 token, const QList<SshHostKeyCandidate>& candidates) {
                if (token != trustToken_)
                    return;
                trustToken_ = 0;
                if (feedbackToast_)
                    feedbackToast_->clearNotice();
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
                for (const auto& candidate : candidates)
                    if (!sameTarget(candidate.target, trustTarget_)) {
                        showTrustStatus(
                            tr("Inspected host keys do not match the selected SSH server."));
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
                            if (!adapter_ || trustToken_ || revision_ != trustRevision_ ||
                                !sameTarget(candidate.target, trustTarget_)) {
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
                    if (trustTarget_.kind == SshHostKeyTarget::Kind::Target)
                        sshKnownHosts_->setText(trustPath_);
                    else if (!sshHopEditor_->setKnownHosts(trustTarget_, trustPath_)) {
                        if (trustDialog_)
                            trustDialog_->invalidate();
                        return;
                    }
                    dirty_ = true;
                    ++revision_;
                    trustRevision_ = revision_;
                }
                if (trustDialog_)
                    trustDialog_->finishApproval(outcome);
                else if (outcome == "approved") {
                    feedbackToast_ = windowToast(this);
                    if (feedbackToast_)
                        feedbackToast_->showToast(tr("Success"), tr("SSH host key approved."),
                                                  ToastVariant::Success);
                } else {
                    showTrustStatus(tr("SSH host key approval outcome is unknown."));
                }
            });
    connect(adapter_, &EngineAdapter::sshHostKeyOperationFailed, this,
            [this](quint64 token, const QString& error) {
                if (token != trustToken_)
                    return;
                trustToken_ = 0;
                updateTrustControls();
                if (trustDialog_)
                    trustDialog_->showFailure(error);
                else
                    showTrustStatus(error);
            });
    updateTrustControls();
}
void ProfileDialog::showTrustStatus(const QString& message) {
    status_->setText(message);
    status_->hide();
    if (isVisible()) {
        feedbackToast_ = windowToast(this);
        feedbackToast_->showToast(tr("Error"), message, ToastVariant::Danger);
    }
}
void ProfileDialog::updateTrustControls() {
    if (!inspectSshKeys_)
        return;
    const bool enabled = adapter_ && !busy_ && !trustToken_ && sshEnabled_->isChecked() &&
                         driver_->currentData() != "sqlite";
    inspectSshKeys_->setEnabled(enabled);
    sshHopEditor_->setInspectionEnabled(enabled);
}
void ProfileDialog::inspectHostKeys(const SshHostKeyTarget& target) {
    if (!adapter_ || busy_ || trustToken_ || !sshEnabled_->isChecked())
        return;
    auto profile = draft();
    if (!profile.sshEnabled)
        return;
    if (profile.id.isEmpty())
        profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (profile.name.isEmpty())
        profile.name = tr("SSH host inspection");
    const auto hops =
        QJsonDocument::fromJson(profile.sshOptions.toUtf8()).object()["jump_hosts"].toArray();
    const auto prefix = targetIndex(hops, target);
    if (prefix < 0)
        return;
    QSet<QString> preceding;
    for (int row = 0; row < prefix; ++row)
        preceding.insert(hops[row].toObject()["id"].toString());
    auto credentials = sshHopEditor_->credentials(false);
    credentials.removeIf(
        [&](const SshHopCredential& value) { return !preceding.contains(value.id); });
    trustTarget_ = target;
    trustPath_ = target.kind == SshHostKeyTarget::Kind::Target
                     ? sshKnownHosts_->text()
                     : hops[prefix].toObject()["known_hosts_file"].toString();
    trustRevision_ = revision_;
    trustToken_ = ++token_;
    updateTrustControls();
    feedbackToast_ = windowToast(this);
    if (feedbackToast_)
        feedbackToast_->showProgress(tr("Profiles"), tr("Inspecting SSH host keys…"));
    adapter_->inspectSshHostKeys(profile, target, credentials, trustToken_);
}
} // namespace choscordb
