#include "widgets/sql_file_dialog/sql_file_dialog.h"
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
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace choscordb {
using bridge_detail::fromRust;
SqlFileDialog::SqlFileDialog(EngineAdapter* adapter, SqlFileTarget target, QWidget* parent)
    : DialogShell(parent), adapter_(adapter), target_(std::move(target)),
      path_(new QLineEdit(this)), browse_(new design::Button(tr("Browse…"), this)),
      mode_(codeLabelChoices(EngineAdapter::sqlFileModes(), this)),
      errors_(new QPlainTextEdit(this)) {
    setObjectName("sqlFileDialog");
    setWindowTitle(tr("Run SQL file"));
    setAppModal();
    resize(design::dialogInitialSize(design::DialogSize::Detail));
    path_->setObjectName("sqlFilePath");
    browse_->setObjectName("sqlFileBrowse");
    browse_->setVariant(design::ButtonVariant::Outline);
    mode_->setObjectName("sqlFileMode");

    auto* sections = createSections();
    auto* heading = new design::Text(tr("Run SQL file"), sections->headerLayout()->parentWidget());
    heading->setTextStyle(design::TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* dismiss = sections->addDismissButton(tr("Close SQL file run"));
    connect(dismiss, &QPushButton::clicked, this, &SqlFileDialog::reject);
    auto* bodyLayout = sections->bodyLayout();
    auto* body = bodyLayout->parentWidget();
    body->setProperty("designSurface", "panel");
    body->setAttribute(Qt::WA_StyledBackground);
    auto* scope =
        createDescription(tr("Run each statement of a SQL file on %1.").arg(target_.name), body);
    scope->setObjectName("sqlFileScope");
    scope->setWordWrap(true);
    bodyLayout->addWidget(scope);

    auto* form = sections->addForm();
    auto* pathRow = new QWidget(this);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(design::spacing(design::Spacing::Two));
    validation_ = new design::FieldValidation(path_, pathRow);
    pathLayout->addWidget(validation_, 1);
    pathLayout->addWidget(browse_, 0, Qt::AlignTop);
    browse_->setParent(pathRow);
    sections->addFormRow(form, tr("File"), pathRow);
    sections->addFormRow(form, tr("On error"), mode_);

    errors_->setObjectName("sqlFileErrors");
    errors_->setAccessibleName(tr("Statement errors"));
    errors_->setReadOnly(true);
    errors_->setProperty("designRole", "codePreview");
    errors_->setFont(
        design::resolveTypography(design::TextStyle::Body, design::FontDesign::Monospaced));
    errors_->setFrameShape(QFrame::NoFrame);
    errors_->hide();
    bodyLayout->addWidget(errors_, 1);
    bodyLayout->addStretch();

    statusLine_ = sections->addStatusLine();
    statusLine_->setObjectName("sqlFileStatusLine");
    cancel_ = sections->addAction(design::DialogActionRole::Dismiss, tr("Cancel"));
    cancel_->setObjectName("sqlFileCancel");
    start_ = sections->addAction(design::DialogActionRole::Primary, tr("Run"));
    start_->setObjectName("sqlFileStart");

    connect(path_, &QLineEdit::textChanged, this, [this] { validation_->setError({}); });
    connect(start_, &QPushButton::clicked, this, &SqlFileDialog::start);
    connect(cancel_, &QPushButton::clicked, this, &SqlFileDialog::reject);
    connect(browse_, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Run SQL file"), path_->text());
        if (!path.isEmpty())
            path_->setText(path);
    });
    connect(adapter, &EngineAdapter::eventReady, this, &SqlFileDialog::handleEvent);
    updateActions();
}
SqlFileDialog::~SqlFileDialog() {
    if (run_ && adapter_)
        adapter_->cancelExport(*run_);
}
bool SqlFileDialog::isRunning() const {
    return EngineAdapter::sqlFilePresentation(state_).busy;
}
void SqlFileDialog::updateActions() {
    const bool running = isRunning();
    for (QWidget* field : {static_cast<QWidget*>(path_), static_cast<QWidget*>(browse_),
                           static_cast<QWidget*>(mode_)})
        field->setEnabled(!running);
    start_->setEnabled(adapter_ && !running);
    cancel_->setEnabled(state_ != ExportState::Cancelling);
}
void SqlFileDialog::start() {
    if (!adapter_ || isRunning())
        return;
    const auto gate = EngineAdapter::executionGate(target_.safety, {true, 0, 0, true, false});
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
    errors_->clear();
    errors_->hide();
    QString submitError;
    run_ = adapter_->runSqlFile(target_.connection, path_->text(), target_.driver, target_.sqlMode,
                                mode_->currentData().toString(), &submitError);
    if (!run_) {
        settle(ExportEvent::Fail, submitError);
        return;
    }
    apply(ExportEvent::Start);
    updateActions();
}
void SqlFileDialog::cancel() {
    if (!run_ || state_ == ExportState::Cancelling || !adapter_)
        return;
    apply(ExportEvent::Cancel);
    updateActions();
    adapter_->cancelExport(*run_);
}
void SqlFileDialog::handleEvent(const BridgeEvent& value) {
    if (!run_ || *run_ != value.id)
        return;
    const auto kind = fromRust(value.kind);
    const auto summary = tr("Ran %n statement(s).", nullptr, int(value.exported_rows));
    if (kind == "sql_file_progress") {
        if (state_ != ExportState::Running)
            return;
        const auto percent =
            value.total_bytes == 0 ? 100 : int(value.exported_bytes * 100 / value.total_bytes);
        showState(tr("%1 Read %2% of the file.").arg(summary).arg(percent));
    } else if (kind == "sql_file_finished") {
        if (value.exported_tables == 0) {
            settle(ExportEvent::Finish, summary);
            return;
        }
        errors_->setPlainText(bridge_detail::fromRustStrings(value.warnings).join('\n'));
        errors_->show();
        settle(ExportEvent::Fail,
               tr("%1 %n statement(s) failed.", nullptr, int(value.exported_tables)).arg(summary));
    } else if (kind == "sql_file_failed") {
        const auto detail = QStringLiteral("%1 %2").arg(fromRust(value.error), summary);
        if (fromRust(value.error_kind) == "Cancelled") {
            if (state_ != ExportState::Cancelling)
                apply(ExportEvent::Cancel);
            settle(ExportEvent::CancelSettled, detail);
        } else {
            settle(ExportEvent::Fail, detail);
        }
    }
}
bool SqlFileDialog::apply(ExportEvent event, const QString& detail) {
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
void SqlFileDialog::showState(const QString& detail) {
    const auto shown = EngineAdapter::sqlFilePresentation(state_, detail);
    statusLine_->setBusy(shown.busy);
    statusLine_->setState(statusLineState(shown.tone));
    statusLine_->setMessage(shown.message);
    statusLine_->setVisible(!shown.message.isEmpty());
}
void SqlFileDialog::settle(ExportEvent event, const QString& detail) {
    run_.reset();
    if (!apply(event, detail))
        apply(ExportEvent::Fail, EngineAdapter::exportTransition(state_, event).error);
    start_->setText(state_ == ExportState::Failed ? tr("Retry") : tr("Run"));
    updateActions();
    if (state_ == ExportState::Succeeded) {
        closeAfter_ = false;
        closeWithSuccess(tr("SQL file complete"), detail);
        return;
    }
    if (closeAfter_) {
        closeAfter_ = false;
        if (state_ != ExportState::Failed)
            hide();
    }
}
void SqlFileDialog::closeEvent(QCloseEvent* event) {
    if (isRunning()) {
        closeAfter_ = true;
        cancel();
        event->ignore();
    } else {
        QDialog::closeEvent(event);
    }
}
void SqlFileDialog::reject() {
    if (isRunning()) {
        closeAfter_ = true;
        cancel();
    } else {
        DialogShell::reject();
    }
}
}
