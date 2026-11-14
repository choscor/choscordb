#include "widgets/profile_dialog/ssh_host_key_dialog.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/field/field.h"
#include "design_system/metrics/metrics.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
#include "design_system/toast_region/toast_region.h"
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
namespace choscordb {
SshHostKeyDialog::SshHostKeyDialog(const QList<SshHostKeyCandidate>& candidates,
                                   const QString& knownHostsPath, QWidget* parent)
    : DialogShell(parent), candidates_(candidates) {
    setObjectName("sshHostKeyDialog");
    resize(design::dialogInitialSize(design::DialogSize::Form));
    setWindowTitle(tr("Verify SSH host key"));
    auto* sections = createSections();
    auto* heading = new design::Text(windowTitle(), sections);
    heading->setTextStyle(design::TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* dismiss = sections->addDismissButton(tr("Close host key verification"));
    dismiss->setObjectName("sshHostKeyDismiss");
    connect(dismiss, &QPushButton::clicked, this, &QDialog::reject);
    auto* layout = sections->bodyLayout();
    layout->addWidget(createDescription(
        tr("Compare the SHA256 fingerprint with a trusted source before approving a key. "
           "Inspection does not change SSH trust."),
        this));
    list_ = new QListWidget(this);
    list_->setObjectName("sshHostKeyCandidates");
    list_->setAccessibleName(tr("Inspected SSH host keys"));
    list_->setMaximumHeight(design::dimension(design::Dimension::Row) * 4);
    for (const auto& candidate : candidates_)
        list_->addItem(candidate.keyType + " · " + candidate.sha256);
    list_->setCurrentRow(-1);
    layout->addWidget(list_);
    auto* form = sections->addForm();
    form_ = form;
    const auto field = [this, form, sections](const char* name, const QString& title) {
        auto* value = new QLineEdit(this);
        value->setObjectName(name);
        value->setReadOnly(true);
        sections->addFormRow(form, title, value);
        return value;
    };
    original_ = field("sshHostKeyOriginalHost", tr("Configured host"));
    hostname_ = field("sshHostKeyHostname", tr("Resolved host"));
    port_ = field("sshHostKeyPort", tr("Port"));
    alias_ = field("sshHostKeyAlias", tr("Host key alias"));
    form->setRowVisible(alias_, false);
    algorithm_ = field("sshHostKeyAlgorithm", tr("Key algorithm"));
    fingerprint_ = field("sshHostKeyFingerprint", tr("SHA256 fingerprint"));
    path_ = new QLineEdit(this);
    path_->setObjectName("sshHostKeyKnownHosts");
    path_->setMaxLength(16385);
    path_->setText(knownHostsPath);
    auto* pathRow = new QWidget(this);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(design::spacing(design::Spacing::Two));
    pathValidation_ = new design::FieldValidation(path_, pathRow);
    pathLayout->addWidget(pathValidation_, 1);
    auto* browse = new design::Button(tr("Browse…"), pathRow);
    browse->setVariant(design::ButtonVariant::Outline);
    pathLayout->addWidget(browse);
    sections->addFormRow(form, tr("Known-hosts file"), pathRow);
    auto* pathHelp = createDescription(EngineAdapter::sshKnownHostsPathRule(), this);
    pathHelp->setObjectName("sshHostKeyKnownHostsHelp");
    form->addRow(QString(), pathHelp);
    statusLine_ = sections->addStatusLine();
    statusLine_->setObjectName("sshHostKeyStatusLine");
    status_ = statusLine_->findChild<QLabel*>("statusMessage");
    status_->setObjectName("sshHostKeyStatus");
    auto* cancel = sections->addAction(design::DialogActionRole::Dismiss, tr("Close"));
    cancel->setObjectName("sshHostKeyClose");
    approve_ = sections->addAction(design::DialogActionRole::Primary, tr("Approve selected key"));
    approve_->setObjectName("sshHostKeyApprove");
    approve_->setDefault(false);
    retry_ = sections->addAction(design::DialogActionRole::Primary, tr("Retry connection"));
    retry_->setObjectName("sshHostKeyRetry");
    retry_->setDefault(false);
    retry_->hide();
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(list_, &QListWidget::currentRowChanged, this, &SshHostKeyDialog::selectCandidate);
    connect(path_, &QLineEdit::textChanged, this, [this] { pathValidation_->setError({}); });
    connect(browse, &QPushButton::clicked, this, [this] {
        if (pending_ || approved_ || !valid_)
            return;
        const auto path =
            QFileDialog::getSaveFileName(this, tr("Select known-hosts file"), path_->text(), {},
                                         nullptr, QFileDialog::DontConfirmOverwrite);
        if (!path.isEmpty())
            path_->setText(path);
    });
    connect(approve_, &QPushButton::clicked, this, [this] {
        const auto row = list_->currentRow();
        if (!approve_->isEnabled() || row < 0 || row >= candidates_.size())
            return;
        if (const auto error = EngineAdapter::sshKnownHostsPathError(path_->text());
            !error.isEmpty()) {
            pathValidation_->setError(error);
            path_->setFocus();
            return;
        }
        pending_ = true;
        statusLine_->showProgress(tr("Approving selected key…"));
        updateActions();
        emit approvalRequested(candidates_[row], path_->text());
    });
    connect(retry_, &QPushButton::clicked, this, [this] {
        if (valid_ && approved_)
            emit retryRequested();
    });
    updateActions();
    path_->setFocus();
}
void SshHostKeyDialog::selectCandidate() {
    const auto row = list_->currentRow();
    const auto candidate =
        row >= 0 && row < candidates_.size() ? candidates_[row] : SshHostKeyCandidate{};
    original_->setText(candidate.originalHost);
    hostname_->setText(candidate.hostname);
    port_->setText(row < 0 ? QString() : QString::number(candidate.port));
    alias_->setText(candidate.hostKeyAlias);
    form_->setRowVisible(alias_, !candidate.hostKeyAlias.isEmpty());
    algorithm_->setText(candidate.keyType);
    fingerprint_->setText(candidate.sha256);
    updateActions();
}
void SshHostKeyDialog::updateActions() {
    const bool editable = valid_ && !pending_ && !approved_;
    list_->setEnabled(editable);
    path_->setEnabled(editable);
    approve_->setEnabled(editable && list_->currentRow() >= 0);
    retry_->setEnabled(valid_ && approved_);
}
void SshHostKeyDialog::finishApproval(const QString& outcome) {
    pending_ = false;
    approved_ = outcome == "approved";
    if (approved_) {
        if (auto* toast = windowToast(this))
            toast->showToast(tr("Key approved"), {}, ToastVariant::Success);
        statusLine_->showInfo(EngineAdapter::recoveryHint(RecoveryHint::UseApprovedKey));
    } else
        statusLine_->showInfo(tr("Approval outcome is unknown. %1")
                                  .arg(EngineAdapter::recoveryHint(RecoveryHint::RetryApproval)));
    retry_->setVisible(approved_);
    approve_->setVisible(!approved_);
    updateActions();
}
void SshHostKeyDialog::showFailure(const QString& error) {
    pending_ = false;
    statusLine_->showError(error);
    updateActions();
}
void SshHostKeyDialog::invalidate() {
    valid_ = false;
    statusLine_->showInfo(EngineAdapter::sshTrustNote(SshTrustNote::SettingsChanged));
    updateActions();
}
}
