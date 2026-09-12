#include "query_settings_dialog.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <atomic>
namespace choscordb {
namespace {
quint64 nextToken() {
    static std::atomic<quint64> token{quint64(1) << 54};
    return token.fetch_add(1);
}
} // namespace
QuerySettingsDialog::QuerySettingsDialog(EngineAdapter* adapter, QWidget* parent)
    : QDialog(parent), adapter_(adapter) {
    setObjectName("querySettingsDialog");
    setWindowTitle(tr("Query settings"));
    auto* layout = new QFormLayout(this);
    const auto limits = EngineAdapter::queryPreferenceLimits();
    pageSize_ = new QSpinBox(this);
    pageSize_->setObjectName("queryPageSize");
    pageSize_->setRange(limits.minPageSize, limits.maxPageSize);
    timeout_ = new QSpinBox(this);
    timeout_->setObjectName("queryTimeoutSeconds");
    timeout_->setRange(0, limits.maxTimeoutSeconds);
    timeout_->setSpecialValueText(tr("No timeout"));
    layout->addRow(tr("Rows per page"), pageSize_);
    layout->addRow(tr("Statement timeout (seconds)"), timeout_);
    auto* explanation = new QLabel(
        tr("Applies to new queries. Existing results keep their page size and timeout."), this);
    explanation->setWordWrap(true);
    explanation->setTextFormat(Qt::PlainText);
    layout->addRow(explanation);
    status_ = new QLabel(this);
    status_->setObjectName("querySettingsStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addRow(status_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel |
                                             QDialogButtonBox::RestoreDefaults,
                                         this);
    apply_ = buttons->button(QDialogButtonBox::Apply);
    apply_->setObjectName("querySettingsApply");
    reset_ = buttons->button(QDialogButtonBox::RestoreDefaults);
    reset_->setObjectName("querySettingsReset");
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(apply_, &QPushButton::clicked, this, &QuerySettingsDialog::apply);
    connect(reset_, &QPushButton::clicked, this, [this] {
        if (token_)
            return;
        fill(QueryPreferences{});
        ready_ = true;
        status_->clear();
        updateControls();
    });
    connect(
        adapter, &EngineAdapter::queryPreferencesReady, this,
        [this](quint64 token, const QueryPreferences& value) {
            if (!token_ || token != token_)
                return;
            token_ = 0;
            const auto limits = EngineAdapter::queryPreferenceLimits();
            if (value.version != limits.version || value.pageSize < limits.minPageSize ||
                value.pageSize > limits.maxPageSize ||
                value.timeoutSeconds > limits.maxTimeoutSeconds) {
                status_->setText(
                    tr("Stored query settings are invalid. Reset to defaults to replace them."));
                updateControls();
                return;
            }
            fill(value);
            ready_ = true;
            status_->setText(saving_ ? tr("Query settings saved.") : tr("Query settings loaded."));
            saving_ = false;
            updateControls();
            emit queryPreferencesConfirmed(value);
        });
    connect(adapter, &EngineAdapter::recoveryFailed, this,
            [this](quint64 token, const QString& error) {
                if (!token_ || token != token_)
                    return;
                token_ = 0;
                saving_ = false;
                status_->setText(error);
                updateControls();
            });
    fill(QueryPreferences{});
    token_ = nextToken();
    updateControls();
    if (adapter_)
        adapter_->getQueryPreferences(token_);
    else {
        token_ = 0;
        status_->setText(tr("Settings service is unavailable."));
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
        status_->setText(tr("Query settings are outside the supported range."));
        return;
    }
    saving_ = true;
    const auto token = nextToken();
    token_ = token;
    status_->setText(tr("Saving query settings…"));
    updateControls();
    // The owner registers this token before even a synchronous submission failure.
    // Retain only locals afterward: a signal handler may close/delete this dialog.
    const QPointer<EngineAdapter> adapter = adapter_;
    emit queryPreferencesSaveSubmitted(token);
    if (adapter)
        adapter->setQueryPreferences(value, token);
}
} // namespace choscordb
