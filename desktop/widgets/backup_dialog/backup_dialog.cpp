#include "widgets/backup_dialog/backup_dialog.h"
#include "bridge/rust_text.h"
#include "choscordb-bridge/src/lib.rs.h"
#include "design_system/button/button.h"
#include "design_system/confirmation_dialog/confirmation_dialog.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/field/field.h"
#include "design_system/fonts/fonts.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include "widgets/screen_state/screen_state.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace choscordb {
using bridge_detail::fromRust;
BackupDialog::BackupDialog(EngineAdapter* adapter, BackupTarget target, QWidget* parent)
    : DialogShell(parent), adapter_(adapter), target_(std::move(target)),
      form_(EngineAdapter::backupForm(target_.driver, target_.restore)), path_(new QLineEdit(this)),
      toolFolder_(new QLineEdit(this)), browse_(new design::Button(tr("Browse…"), this)),
      browseTools_(new design::Button(tr("Browse…"), this)), output_(new QPlainTextEdit(this)) {
    const auto title = target_.restore ? tr("Restore database") : tr("Back up database");
    setObjectName("backupDialog");
    setWindowTitle(title);
    setAppModal();
    resize(design::dialogInitialSize(design::DialogSize::Detail));
    path_->setObjectName("backupPath");
    toolFolder_->setObjectName("backupToolFolder");
    browse_->setObjectName("backupBrowse");
    browseTools_->setObjectName("backupBrowseTools");

    auto* sections = createSections();
    auto* heading = new design::Text(title, sections->headerLayout()->parentWidget());
    heading->setTextStyle(design::TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* dismiss =
        sections->addDismissButton(target_.restore ? tr("Close restore") : tr("Close backup"));
    connect(dismiss, &QPushButton::clicked, this, &BackupDialog::reject);
    auto* bodyLayout = sections->bodyLayout();
    auto* body = bodyLayout->parentWidget();
    body->setProperty("designSurface", "panel");
    body->setAttribute(Qt::WA_StyledBackground);
    auto* scope =
        createDescription(target_.restore ? tr("Restore a backup file into %1.").arg(target_.name)
                                          : tr("Back up %1 into a file.").arg(target_.name),
                          body);
    scope->setObjectName("backupScope");
    scope->setWordWrap(true);
    bodyLayout->addWidget(scope);

    auto* form = sections->addForm();
    sections->addFormRow(form, tr("File"), pathRow(path_, &validation_, browse_));
    if (form_.usesTool) {
        sections->addFormRow(form, tr("Tool folder"),
                             pathRow(toolFolder_, &toolValidation_, browseTools_),
                             design::FieldNeed::Optional);
        form->addRow(QString(), createDescription(tr("Leave the folder empty to find the "
                                                     "client tools on PATH."),
                                                  body));
    } else {
        for (QWidget* field :
             {static_cast<QWidget*>(toolFolder_), static_cast<QWidget*>(browseTools_)})
            field->hide();
    }
    for (const auto& option : form_.options) {
        auto* box = new QCheckBox(option.label, this);
        box->setObjectName(QStringLiteral("backupOption_%1").arg(option.code));
        box->setChecked(option.defaultOn);
        form->addRow(QString(), box);
        options_.append({option.code, box});
    }

    output_->setObjectName("backupOutput");
    output_->setAccessibleName(tr("Tool output"));
    output_->setReadOnly(true);
    output_->setProperty("designRole", "codePreview");
    output_->setFont(
        design::resolveTypography(design::TextStyle::Body, design::FontDesign::Monospaced));
    output_->setFrameShape(QFrame::NoFrame);
    output_->hide();
    bodyLayout->addWidget(output_, 1);
    bodyLayout->addStretch();

    statusLine_ = sections->addStatusLine();
    statusLine_->setObjectName("backupStatusLine");
    cancel_ = sections->addAction(design::DialogActionRole::Dismiss, tr("Cancel"));
    cancel_->setObjectName("backupCancel");
    start_ = sections->addAction(design::DialogActionRole::Primary,
                                 target_.restore ? tr("Restore") : tr("Back up"));
    start_->setObjectName("backupStart");

    if (!form_.unavailable.isEmpty())
        statusLine_->showError(form_.unavailable);
    connect(path_, &QLineEdit::textChanged, this, [this] { validation_->setError({}); });
    connect(start_, &QPushButton::clicked, this, &BackupDialog::start);
    connect(cancel_, &QPushButton::clicked, this, &BackupDialog::reject);
    connect(browse_, &QPushButton::clicked, this, [this, title] {
        const auto path = target_.restore
                              ? QFileDialog::getOpenFileName(this, title, path_->text())
                              : QFileDialog::getSaveFileName(this, title, path_->text());
        if (!path.isEmpty())
            path_->setText(path);
    });
    connect(browseTools_, &QPushButton::clicked, this, [this] {
        const auto folder =
            QFileDialog::getExistingDirectory(this, tr("Tool folder"), toolFolder_->text());
        if (!folder.isEmpty())
            toolFolder_->setText(folder);
    });
    connect(adapter, &EngineAdapter::eventReady, this, &BackupDialog::handleEvent);
    updateActions();
}
BackupDialog::~BackupDialog() {
    if (run_ && adapter_)
        adapter_->cancelExport(*run_);
}
QWidget* BackupDialog::pathRow(QLineEdit* field, design::FieldValidation** validation,
                               design::Button* browse) {
    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(design::spacing(design::Spacing::Two));
    *validation = new design::FieldValidation(field, row);
    layout->addWidget(*validation, 1);
    browse->setVariant(design::ButtonVariant::Outline);
    browse->setParent(row);
    layout->addWidget(browse, 0, Qt::AlignTop);
    return row;
}
bool BackupDialog::isRunning() const {
    return EngineAdapter::backupPresentation(state_, target_.restore).busy;
}
void BackupDialog::updateActions() {
    const bool running = isRunning();
    for (QWidget* field : {static_cast<QWidget*>(path_), static_cast<QWidget*>(toolFolder_),
                           static_cast<QWidget*>(browse_), static_cast<QWidget*>(browseTools_)})
        field->setEnabled(!running);
    for (const auto& option : options_)
        option.second->setEnabled(!running);
    start_->setEnabled(adapter_ && !running && form_.unavailable.isEmpty());
    cancel_->setEnabled(state_ != ExportState::Cancelling);
}
void BackupDialog::start() {
    if (!adapter_ || isRunning())
        return;
    const auto gate = EngineAdapter::executionGate(target_.safety,
                                                   {true, 0, 0, target_.restore, !target_.restore});
    if (gate.decision == SqlGateDecision::Reject) {
        validation_->setError(gate.message);
        return;
    }
    if (gate.decision == SqlGateDecision::Confirm &&
        !ConfirmationDialog::confirm(this, gate.title, gate.message, gate.accept,
                                     ConfirmationDialog::Consequence::Destructive))
        return;
    if (!apply(ExportEvent::Submit))
        return;
    output_->clear();
    output_->show();
    BackupRequest request{target_.driver, target_.restore, path_->text(), toolFolder_->text(), {}};
    for (const auto& option : options_)
        if (option.second->isChecked())
            request.options.append(option.first);
    QString submitError;
    run_ = adapter_->startBackup(target_.connection, request, &submitError);
    if (!run_) {
        settle(ExportEvent::Fail, submitError);
        return;
    }
    apply(ExportEvent::Start);
    updateActions();
}
void BackupDialog::cancel() {
    if (!run_ || state_ == ExportState::Cancelling || !adapter_)
        return;
    apply(ExportEvent::Cancel);
    updateActions();
    adapter_->cancelExport(*run_);
}
void BackupDialog::handleEvent(const BridgeEvent& value) {
    if (!run_ || *run_ != value.id)
        return;
    const auto kind = fromRust(value.kind);
    if (kind == "backup_output") {
        output_->appendPlainText(fromRust(value.object));
    } else if (kind == "backup_finished") {
        settle(ExportEvent::Finish);
    } else if (kind == "backup_failed") {
        const auto detail = fromRust(value.error);
        if (fromRust(value.error_kind) == "Cancelled") {
            if (state_ != ExportState::Cancelling)
                apply(ExportEvent::Cancel);
            settle(ExportEvent::CancelSettled, detail);
        } else {
            settle(ExportEvent::Fail, detail);
        }
    }
}
bool BackupDialog::apply(ExportEvent event, const QString& detail) {
    const auto next = EngineAdapter::exportTransition(state_, event);
    if (!next.allowed) {
        statusLine_->setState(design::StatusLine::State::Error);
        statusLine_->setMessage(next.error);
        statusLine_->show();
        return false;
    }
    state_ = next.state;
    showState(detail);
    return true;
}
void BackupDialog::showState(const QString& detail) {
    const auto shown = EngineAdapter::backupPresentation(state_, target_.restore, detail);
    statusLine_->setBusy(shown.busy);
    statusLine_->setState(statusLineState(shown.tone));
    statusLine_->setMessage(shown.message);
    statusLine_->setVisible(!shown.message.isEmpty());
}
void BackupDialog::settle(ExportEvent event, const QString& detail) {
    run_.reset();
    if (!apply(event, detail))
        apply(ExportEvent::Fail, EngineAdapter::exportTransition(state_, event).error);
    start_->setText(state_ == ExportState::Failed
                        ? tr("Retry")
                        : (target_.restore ? tr("Restore") : tr("Back up")));
    updateActions();
    if (state_ == ExportState::Succeeded) {
        closeAfter_ = false;
        closeWithSuccess(windowTitle(),
                         EngineAdapter::backupPresentation(state_, target_.restore).message);
        return;
    }
    if (closeAfter_) {
        closeAfter_ = false;
        if (state_ != ExportState::Failed)
            hide();
    }
}
void BackupDialog::closeEvent(QCloseEvent* event) {
    if (isRunning()) {
        closeAfter_ = true;
        cancel();
        event->ignore();
    } else {
        QDialog::closeEvent(event);
    }
}
void BackupDialog::reject() {
    if (isRunning()) {
        closeAfter_ = true;
        cancel();
    } else {
        DialogShell::reject();
    }
}
}
