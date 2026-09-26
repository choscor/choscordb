#include "query_settings_dialog.h"
#include "design_system/button/button.h"
#include "design_system/field/field.h"
#include "design_system/text/text.h"
#include "design_system/toast_region/toast_region.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <atomic>
namespace choscordb {
namespace {
quint64 nextToken() {
    static std::atomic<quint64> token{quint64(1) << 54};
    return token.fetch_add(1);
}
} // namespace
QuerySettingsDialog::QuerySettingsDialog(EngineAdapter* adapter, QWidget* parent)
    : DialogShell(parent), adapter_(adapter) {
    setObjectName("querySettingsDialog");
    setWindowTitle(tr("Query settings"));
    auto* layout = new QVBoxLayout(this);
    auto* heading = new design::Text(tr("Query settings"), this);
    heading->setTypographyRole(design::TypographyRole::Heading);
    layout->addWidget(heading);
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
    pageSizeValidation_ = new design::FieldValidation(pageSize_, this);
    timeoutValidation_ = new design::FieldValidation(timeout_, this);
    form->addRow(tr("Rows per page"), pageSizeValidation_);
    form->addRow(tr("Statement timeout (seconds)"), timeoutValidation_);
    connect(pageSize_, &QSpinBox::valueChanged, this,
            [this] { pageSizeValidation_->setError({}); });
    connect(timeout_, &QSpinBox::valueChanged, this, [this] { timeoutValidation_->setError({}); });
    auto* explanation = createDescription(
        tr("Applies to new queries. Existing results keep their page size and timeout."), this);
    layout->addWidget(explanation);
    auto* buttons = new QDialogButtonBox(this);
    apply_ = new design::Button(tr("Apply"), this);
    apply_->setObjectName("querySettingsApply");
    reset_ = new design::Button(tr("Restore defaults"), this);
    reset_->setObjectName("querySettingsReset");
    reset_->setVariant(design::ButtonVariant::Secondary);
    auto* cancel = new design::Button(tr("Cancel"), this);
    cancel->setObjectName("querySettingsCancel");
    cancel->setVariant(design::ButtonVariant::Outline);
    buttons->addButton(apply_, QDialogButtonBox::ApplyRole);
    buttons->addButton(reset_, QDialogButtonBox::ResetRole);
    buttons->addButton(cancel, QDialogButtonBox::RejectRole);
    layout->addStretch();
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(apply_, &QPushButton::clicked, this, &QuerySettingsDialog::apply);
    connect(reset_, &QPushButton::clicked, this, [this] {
        if (token_)
            return;
        fill(QueryPreferences{});
        ready_ = true;
        windowToast(this)->clearNotice();
        updateControls();
    });
    connect(adapter, &EngineAdapter::queryPreferencesReady, this,
            [this](quint64 token, const QueryPreferences& value) {
                if (!token_ || token != token_)
                    return;
                token_ = 0;
                clearProgressToast(this);
                const auto limits = EngineAdapter::queryPreferenceLimits();
                if (value.version != limits.version || value.pageSize < limits.minPageSize ||
                    value.pageSize > limits.maxPageSize ||
                    value.timeoutSeconds > limits.maxTimeoutSeconds) {
                    pageSizeValidation_->setError(
                        value.pageSize < limits.minPageSize || value.pageSize > limits.maxPageSize
                            ? tr("Stored page size is outside the supported range.")
                            : QString{});
                    timeoutValidation_->setError(
                        value.timeoutSeconds > limits.maxTimeoutSeconds
                            ? tr("Stored timeout is outside the supported range.")
                            : QString{});
                    if (value.version != limits.version)
                        windowToast(this)->showToast(
                            tr("Error"),
                            tr("Stored settings version is unsupported. Restore defaults."),
                            ToastVariant::Danger, 0);
                    updateControls();
                    return;
                }
                fill(value);
                ready_ = true;
                windowToast(this)->showToast(
                    tr("Success"), saving_ ? tr("Query settings saved.")
                                            : tr("Query settings loaded."),
                    ToastVariant::Success);
                saving_ = false;
                updateControls();
                emit queryPreferencesConfirmed(value);
            });
    connect(adapter, &EngineAdapter::recoveryFailed, this,
            [this](quint64 token, const QString& error) {
                if (!token_ || token != token_)
                    return;
                token_ = 0;
                clearProgressToast(this);
                saving_ = false;
                windowToast(this)->showToast(tr("Error"), error, ToastVariant::Danger, 0);
                updateControls();
            });
    fill(QueryPreferences{});
    token_ = nextToken();
    updateControls();
    if (adapter_) {
        progressToast(this)->showProgress(tr("Query settings"), tr("Loading query settings…"));
        adapter_->getQueryPreferences(token_);
    } else {
        token_ = 0;
        windowToast(this)->showToast(tr("Error"), tr("Settings service is unavailable."),
                                     ToastVariant::Danger, 0);
        updateControls();
    }
}
void QuerySettingsDialog::fill(const QueryPreferences& value) {
    pageSize_->setValue(value.pageSize);
    timeout_->setValue(value.timeoutSeconds);
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
    const auto limits = EngineAdapter::queryPreferenceLimits();
    if (value.version != limits.version || value.pageSize < limits.minPageSize ||
        value.pageSize > limits.maxPageSize || value.timeoutSeconds > limits.maxTimeoutSeconds) {
        pageSizeValidation_->setError(value.pageSize < limits.minPageSize ||
                                              value.pageSize > limits.maxPageSize
                                          ? tr("Page size is outside the supported range.")
                                          : QString{});
        timeoutValidation_->setError(value.timeoutSeconds > limits.maxTimeoutSeconds
                                         ? tr("Timeout is outside the supported range.")
                                         : QString{});
        return;
    }
    saving_ = true;
    const auto token = nextToken();
    token_ = token;
    windowToast(this)->clearNotice();
    progressToast(this)->showProgress(tr("Query settings"), tr("Saving query settings…"));
    updateControls();
    // The owner registers this token before even a synchronous submission failure.
    // Retain only locals afterward: a signal handler may close/delete this dialog.
    const QPointer<EngineAdapter> adapter = adapter_;
    emit queryPreferencesSaveSubmitted(token);
    if (adapter)
        adapter->setQueryPreferences(value, token);
}
} // namespace choscordb
