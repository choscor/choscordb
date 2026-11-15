#include "widgets/database_action_dialog/database_action_dialog.h"
#include "bridge/request_token.h"
#include "bridge/rust_text.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace choscordb {
namespace {
using bridge_detail::fromRust;
}
DatabaseActionDialog::DatabaseActionDialog(EngineAdapter* adapter, quint64 connection,
                                           const QString& driver, const DatabaseList& databases,
                                           QWidget* parent)
    : DialogShell(parent), adapter_(adapter), connection_(connection), driver_(driver),
      current_(databases.current), kinds_(EngineAdapter::databaseActionKinds()) {
    setObjectName("databaseActionDialog");
    setWindowTitle(tr("Manage databases"));
    setAppModal();
    resize(design::dialogInitialSize(design::DialogSize::Form));
    auto* sections = createSections();
    auto* heading = new design::Text(windowTitle(), sections->headerLayout()->parentWidget());
    heading->setTextStyle(design::TextStyle::Headline);
    sections->headerLayout()->addWidget(heading);
    auto* dismiss = sections->addDismissButton(tr("Close database actions"));
    connect(dismiss, &QPushButton::clicked, this, &DatabaseActionDialog::reject);
    auto* bodyLayout = sections->bodyLayout();
    auto* body = bodyLayout->parentWidget();
    body->setProperty("designSurface", "panel");
    body->setAttribute(Qt::WA_StyledBackground);

    kind_ = new QComboBox(this);
    kind_->setObjectName("databaseActionKind");
    for (const auto& kind : kinds_)
        kind_->addItem(kind.label, kind.code);
    target_ = new QComboBox(this);
    target_->setObjectName("databaseActionTarget");
    for (const auto& choice : databases.choices)
        if (choice.code != databases.current)
            target_->addItem(choice.label, choice.code);
    const auto lineEdit = [this](const char* name) {
        auto* edit = new QLineEdit(this);
        edit->setObjectName(name);
        connect(edit, &QLineEdit::textChanged, this, &DatabaseActionDialog::updateReview);
        return edit;
    };
    name_ = lineEdit("databaseActionName");
    newName_ = lineEdit("databaseActionNewName");
    encoding_ = lineEdit("databaseActionEncoding");
    collation_ = lineEdit("databaseActionCollation");
    confirmation_ = lineEdit("databaseActionConfirm");
    form_ = sections->addForm();
    sections->addFormRow(form_, tr("Action"), kind_);
    sections->addFormRow(form_, tr("Database"), target_);
    sections->addFormRow(form_, tr("Name"), name_);
    sections->addFormRow(form_, tr("New name"), newName_);
    sections->addFormRow(form_, tr("Encoding"), encoding_, design::FieldNeed::Optional);
    sections->addFormRow(form_, tr("Collation"), collation_, design::FieldNeed::Optional);
    sections->addFormRow(form_, tr("Type the name"), confirmation_);

    error_ = new QLabel(body);
    error_->setObjectName("databaseActionError");
    error_->setProperty("designRole", "fieldError");
    error_->setWordWrap(true);
    error_->hide();
    bodyLayout->addWidget(error_);
    bodyLayout->addWidget(createDescription(tr("SQL to execute"), body));
    sqlPreview_ = new QPlainTextEdit(body);
    sqlPreview_->setObjectName("databaseActionSql");
    sqlPreview_->setAccessibleName(tr("SQL preview"));
    sqlPreview_->setReadOnly(true);
    sqlPreview_->setProperty("designRole", "codePreview");
    sqlPreview_->setFont(
        design::resolveTypography(design::TextStyle::Body, design::FontDesign::Monospaced));
    sqlPreview_->setFrameShape(QFrame::NoFrame);
    bodyLayout->addWidget(sqlPreview_);

    statusLine_ = sections->addStatusLine();
    statusLine_->setObjectName("databaseActionStatusLine");
    cancel_ = sections->addAction(design::DialogActionRole::Dismiss, tr("Cancel"));
    cancel_->setObjectName("databaseActionCancel");
    apply_ = sections->addAction(design::DialogActionRole::Primary, tr("Apply"));
    apply_->setObjectName("databaseActionApply");
    connect(cancel_, &QPushButton::clicked, this, &DatabaseActionDialog::reject);
    connect(apply_, &QPushButton::clicked, this, &DatabaseActionDialog::apply);
    connect(kind_, &QComboBox::currentIndexChanged, this, &DatabaseActionDialog::updateFields);
    connect(target_, &QComboBox::currentIndexChanged, this, &DatabaseActionDialog::updateReview);
    connect(adapter, &EngineAdapter::eventReady, this, &DatabaseActionDialog::handleEvent);
    updateFields();
}
DatabaseActionKindInfo DatabaseActionDialog::kind() const {
    const auto index = kind_->currentIndex();
    return index >= 0 && index < kinds_.size() ? kinds_.at(index) : DatabaseActionKindInfo{};
}
DatabaseAction DatabaseActionDialog::action() const {
    const auto selected = kind();
    return {driver_,
            selected.code,
            current_,
            selected.existing ? target_->currentData().toString() : name_->text(),
            newName_->text(),
            encoding_->text(),
            collation_->text(),
            confirmation_->text()};
}
void DatabaseActionDialog::updateFields() {
    const auto selected = kind();
    form_->setRowVisible(target_, selected.existing);
    form_->setRowVisible(name_, !selected.existing);
    form_->setRowVisible(newName_, selected.newName);
    form_->setRowVisible(encoding_, selected.settings);
    form_->setRowVisible(collation_, selected.settings);
    form_->setRowVisible(confirmation_, selected.confirmation);
    updateReview();
}
void DatabaseActionDialog::updateReview() {
    const auto plan = EngineAdapter::databaseActionPlan(action());
    sqlPreview_->setPlainText(plan.sql);
    error_->setText(plan.error);
    error_->setVisible(!plan.error.isEmpty());
    apply_->setEnabled(!isApplying() && !plan.sql.isEmpty());
    cancel_->setEnabled(!isApplying());
}
void DatabaseActionDialog::apply() {
    if (!adapter_ || isApplying())
        return;
    applyToken_ = nextRequestToken();
    QString error;
    if (!adapter_->applyDatabaseAction(connection_, action(), applyToken_, &error)) {
        applyToken_ = 0;
        showStatus(error, true, false);
        updateReview();
        return;
    }
    showStatus(tr("Applying the database change…"), false, true);
    updateReview();
}
void DatabaseActionDialog::handleEvent(const BridgeEvent& value) {
    if (value.id != connection_ || !isApplying())
        return;
    const auto kindName = fromRust(value.kind);
    if (kindName == "disconnected") {
        applyToken_ = 0;
        showStatus(tr("The connection closed before the change completed. Its outcome is "
                      "unknown."),
                   true, false);
        updateReview();
        return;
    }
    if (value.request_token != applyToken_ ||
        (kindName != "edit_applied" && kindName != "edit_failed"))
        return;
    applyToken_ = 0;
    if (kindName == "edit_failed") {
        showStatus(fromRust(value.error), true, false);
        updateReview();
        return;
    }
    const auto label = kind().label;
    emit applied(connection_, label);
    closeWithSuccess(label, sqlPreview_->toPlainText());
}
void DatabaseActionDialog::showStatus(const QString& message, bool error, bool busy) {
    statusLine_->setBusy(busy);
    statusLine_->setState(error ? design::StatusLine::State::Error
                                : design::StatusLine::State::Neutral);
    statusLine_->setMessage(message);
    statusLine_->setVisible(!message.isEmpty());
}
void DatabaseActionDialog::reject() {
    if (!isApplying())
        DialogShell::reject();
}
}
