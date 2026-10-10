#include "query_settings_dialog.h"
#include "bridge/request_token.h"
#include "design_system/button/button.h"
#include "design_system/dialog_sections/dialog_sections.h"
#include "design_system/status_line/status_line.h"
#include "design_system/text/text.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
namespace choscordb {
QuerySettingsDialog::QuerySettingsDialog(EngineAdapter* adapter, QWidget* parent)
    : DialogShell(parent), adapter_(adapter),
      connectionTimeoutSeconds_(QueryPreferences().connectionTimeoutSeconds) {
    setObjectName("querySettingsDialog");
    setWindowTitle(tr("Query settings"));
    auto* root = new QVBoxLayout(this);
    auto* sections = new design::DialogSections(this);
    root->addWidget(sections);
    auto* heading = new design::Text(tr("Query settings"), sections);
    heading->setTypographyRole(design::TypographyRole::Title);
    sections->headerLayout()->addWidget(heading);
    auto* layout = sections->bodyLayout();
    layout->setSpacing(design::spacing(design::Spacing::Two));
    auto* form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(form);
    const auto limits = EngineAdapter::queryPreferenceLimits();
    pageSize_ = new QSpinBox(this);
    pageSize_->setObjectName("queryPageSize");
    pageSize_->setRange(limits.minPageSize, limits.maxPageSize);
    timeout_ = new QSpinBox(this);
    timeout_->setObjectName("queryTimeoutSeconds");
    timeout_->setRange(0, limits.maxTimeoutSeconds);
    timeout_->setSpecialValueText(tr("No timeout"));
    form->addRow(tr("Rows per page"), pageSize_);
    form->addRow(tr("Statement timeout (seconds)"), timeout_);
    auto* explanation = createDescription(
        tr("Applies to new queries. Existing results keep their page size and timeout."), this);
    layout->addWidget(explanation);
    apply_ = new design::Button(tr("Apply"), this);
    apply_->setObjectName("querySettingsApply");
    reset_ = new design::Button(tr("Restore defaults"), this);
    reset_->setObjectName("querySettingsReset");
    reset_->setVariant(design::ButtonVariant::Secondary);
    auto* cancel = new design::Button(tr("Cancel"), this);
    cancel->setObjectName("querySettingsCancel");
    cancel->setVariant(design::ButtonVariant::Outline);
    auto* buttons = sections->footerLayout();
    buttons->addWidget(reset_);
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(apply_);
    layout->addStretch();
    statusLine_ = new design::StatusLine(this);
    statusLine_->setObjectName("querySettingsStatusLine");
    status_ = statusLine_->findChild<QLabel*>("statusMessage");
    status_->setObjectName("querySettingsStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    layout->addWidget(statusLine_);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(apply_, &QPushButton::clicked, this, &QuerySettingsDialog::apply);
    connect(reset_, &QPushButton::clicked, this, [this] {
        if (token_)
            return;
        auto defaults = QueryPreferences{};
        defaults.connectionTimeoutSeconds = connectionTimeoutSeconds_;
        defaults.showSystemSchemas = showSystemSchemas_;
        fill(defaults);
        ready_ = true;
        setStatus({});
        statusLine_->setAvailable(true);
        statusLine_->setNeutral();
        updateControls();
    });
    connect(adapter, &EngineAdapter::queryPreferencesReady, this,
            [this](quint64 token, const QueryPreferences& value) {
                if (!token_ || token != token_) {
                    // Preferences can save this shared record while Query Settings is open.
                    showSystemSchemas_ = value.showSystemSchemas;
                    return;
                }
                token_ = 0;
                statusLine_->setBusy(false);
                // Rust validates stored settings and reports invalid ones as failures.
                fill(value);
                ready_ = true;
                statusLine_->setAvailable(true);
                statusLine_->setNeutral();
                setStatus(saving_ ? tr("Query settings saved.") : QString{});
                saving_ = false;
                updateControls();
                emit queryPreferencesConfirmed(value);
            });
    connect(adapter, &EngineAdapter::recoveryFailed, this,
            [this](quint64 token, const QString& error) {
                if (!token_ || token != token_)
                    return;
                token_ = 0;
                statusLine_->setBusy(false);
                saving_ = false;
                statusLine_->setAvailable(false);
                setStatus(error);
                updateControls();
            });
    fill(QueryPreferences{});
    token_ = nextRequestToken();
    updateControls();
    if (adapter_) {
        statusLine_->setAvailable(true);
        statusLine_->setNeutral();
        statusLine_->setBusy(true);
        setStatus(tr("Loading query settings…"));
        adapter_->getQueryPreferences(token_);
    } else {
        token_ = 0;
        statusLine_->setAvailable(false);
        setStatus(tr("Settings service is unavailable."));
        updateControls();
    }
}
void QuerySettingsDialog::fill(const QueryPreferences& value) {
    pageSize_->setValue(value.pageSize);
    timeout_->setValue(value.timeoutSeconds);
    connectionTimeoutSeconds_ = value.connectionTimeoutSeconds;
    showSystemSchemas_ = value.showSystemSchemas;
}
void QuerySettingsDialog::updateControls() {
    pageSize_->setEnabled(!token_);
    timeout_->setEnabled(!token_);
    reset_->setEnabled(!token_);
    apply_->setEnabled(!token_ && ready_ && adapter_);
}
void QuerySettingsDialog::apply() {
    if (token_ || !ready_ || !adapter_)
        return;
    QueryPreferences value;
    value.pageSize = quint32(pageSize_->value());
    value.timeoutSeconds = quint32(timeout_->value());
    value.connectionTimeoutSeconds = connectionTimeoutSeconds_;
    value.showSystemSchemas = showSystemSchemas_;
    saving_ = true;
    const auto token = nextRequestToken();
    token_ = token;
    statusLine_->setAvailable(true);
    statusLine_->setNeutral();
    statusLine_->setBusy(true);
    setStatus(tr("Saving query settings…"));
    updateControls();
    // The owner registers this token before even a synchronous submission failure.
    // Retain only locals afterward: a signal handler may close/delete this dialog.
    const QPointer<EngineAdapter> adapter = adapter_;
    emit queryPreferencesSaveSubmitted(token);
    if (adapter)
        adapter->setQueryPreferences(value, token);
}
void QuerySettingsDialog::showEvent(QShowEvent* event) {
    DialogShell::showEvent(event);
    layout()->setContentsMargins(0, 0, 0, 0);
    layout()->setSpacing(0);
}
void QuerySettingsDialog::setStatus(const QString& message) {
    statusLine_->setMessage(message);
}
} // namespace choscordb
