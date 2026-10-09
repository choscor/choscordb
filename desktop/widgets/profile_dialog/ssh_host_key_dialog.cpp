#include "widgets/profile_dialog/ssh_host_key_dialog.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include "design_system/theme.h"
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
    setWindowTitle(tr("Verify SSH host key"));
    auto* root = new QVBoxLayout(this);
    auto* sections = new design::DialogSections(this);
    root->addWidget(sections);
    auto* heading = new design::Text(windowTitle(), sections);
    heading->setTypographyRole(design::TypographyRole::DialogTitle);
    sections->headerLayout()->addWidget(heading);
    auto* layout = sections->bodyLayout();
    layout->setSpacing(design::spacing(design::Spacing::Two));
    layout->addWidget(createDescription(
        tr("Compare the SHA256 fingerprint with a trusted source before approving a key. "
           "Inspection does not change SSH trust."),
        this));
    list_ = new QListWidget(this);
    list_->setObjectName("sshHostKeyCandidates");
    list_->setAccessibleName(tr("Inspected SSH host keys"));
    list_->setMaximumHeight(design::dimension(design::Dimension::TableRow) * 4);
    for (const auto& candidate : candidates_)
        list_->addItem(candidate.keyType + " — " + candidate.sha256);
    list_->setCurrentRow(-1);
    layout->addWidget(list_);
    auto* form = new QFormLayout;
    const auto field = [this, form](const char* name, const QString& title) {
        auto* value = new QLineEdit(this);
        value->setObjectName(name);
        value->setReadOnly(true);
        form->addRow(title, value);
        return value;
    };
    original_ = field("sshHostKeyOriginalHost", tr("Configured host"));
    hostname_ = field("sshHostKeyHostname", tr("Resolved host"));
    port_ = field("sshHostKeyPort", tr("Port"));
    alias_ = field("sshHostKeyAlias", tr("Host key alias"));
    alias_->setPlaceholderText(tr("No alias"));
    algorithm_ = field("sshHostKeyAlgorithm", tr("Key algorithm"));
    fingerprint_ = field("sshHostKeyFingerprint", tr("SHA256 fingerprint"));
    path_ = new QLineEdit(this);
    path_->setObjectName("sshHostKeyKnownHosts");
    path_->setMaxLength(16385);
    path_->setText(knownHostsPath);
    path_->setPlaceholderText(tr("Choose the known-hosts file to update"));
    path_->setToolTip(tr("Use an absolute file path without variable substitutions."));
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(path_);
    auto* browse = new design::Button(tr("Choose file…"), this);
    browse->setVariant(design::ButtonVariant::Outline);
    pathRow->addWidget(browse);
    form->addRow(tr("Known-hosts file"), pathRow);
    layout->addLayout(form);
    statusLine_ = new design::StatusLine(this);
    statusLine_->setObjectName("sshHostKeyStatusLine");
    status_ = statusLine_->findChild<QLabel*>("statusMessage");
    status_->setObjectName("sshHostKeyStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    statusLine_->hide();
    layout->addWidget(statusLine_);
    auto* actions = sections->footerLayout();
    auto* cancel = new design::Button(tr("Close"), this);
    cancel->setObjectName("sshHostKeyClose");
    cancel->setVariant(design::ButtonVariant::Outline);
    approve_ = new design::Button(tr("Approve selected key"), this);
    approve_->setObjectName("sshHostKeyApprove");
    approve_->setAutoDefault(false);
    approve_->setDefault(false);
    retry_ = new design::Button(tr("Retry connection"), this);
    retry_->setObjectName("sshHostKeyRetry");
    retry_->setAutoDefault(false);
    retry_->hide();
    actions->addWidget(cancel);
    actions->addStretch();
    actions->addWidget(retry_);
    actions->addWidget(approve_);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(list_, &QListWidget::currentRowChanged, this, &SshHostKeyDialog::selectCandidate);
    connect(path_, &QLineEdit::textChanged, this, &SshHostKeyDialog::updateActions);
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
        pending_ = true;
        statusLine_->setMessage(tr("Approving selected key…"));
        statusLine_->setAvailable(true);
        statusLine_->setBusy(true);
        statusLine_->show();
        updateActions();
        emit approvalRequested(candidates_[row], path_->text());
    });
    connect(retry_, &QPushButton::clicked, this, [this] {
        if (valid_ && approved_)
            emit retryRequested();
    });
    updateActions();
    // Focusing the list can implicitly select its first row on some platforms.
    path_->setFocus();
}
void SshHostKeyDialog::showEvent(QShowEvent* event) {
    DialogShell::showEvent(event);
    layout()->setContentsMargins(0, 0, 0, 0);
    layout()->setSpacing(0);
}
void SshHostKeyDialog::selectCandidate() {
    const auto row = list_->currentRow();
    const auto candidate =
        row >= 0 && row < candidates_.size() ? candidates_[row] : SshHostKeyCandidate{};
    original_->setText(candidate.originalHost);
    hostname_->setText(candidate.hostname);
    port_->setText(row < 0 ? QString() : QString::number(candidate.port));
    alias_->setText(candidate.hostKeyAlias);
    algorithm_->setText(candidate.keyType);
    fingerprint_->setText(candidate.sha256);
    updateActions();
}
void SshHostKeyDialog::updateActions() {
    const bool editable = valid_ && !pending_ && !approved_;
    list_->setEnabled(editable);
    path_->setEnabled(editable);
    const bool validPath = EngineAdapter::sshKnownHostsPathValid(path_->text());
    approve_->setEnabled(editable && list_->currentRow() >= 0 && validPath);
    retry_->setEnabled(valid_ && approved_);
}
void SshHostKeyDialog::finishApproval(const QString& outcome) {
    pending_ = false;
    approved_ = outcome == "approved";
    statusLine_->setBusy(false);
    statusLine_->setAvailable(approved_);
    if (!approved_)
        statusLine_->setNeutral();
    statusLine_->setMessage(approved_
                                ? tr("Key approved. Retry the connection to use it.")
                                : tr("Approval outcome is unknown. Check the selected file or "
                                     "retry approval."));
    statusLine_->show();
    retry_->setVisible(approved_);
    updateActions();
}
void SshHostKeyDialog::showFailure(const QString& error) {
    pending_ = false;
    statusLine_->setBusy(false);
    statusLine_->setAvailable(false);
    statusLine_->setMessage(error);
    statusLine_->show();
    updateActions();
}
void SshHostKeyDialog::invalidate() {
    valid_ = false;
    statusLine_->setBusy(false);
    statusLine_->setNeutral();
    statusLine_->setMessage(tr("Connection settings changed. Inspect the host keys again."));
    statusLine_->show();
    updateActions();
}
} // namespace choscordb
