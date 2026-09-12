#include "query_settings.h"
#include "widgets/query_settings_dialog.h"
#include <QTimer>
#include <atomic>
namespace choscordb {
namespace {
quint64 nextToken() {
    static std::atomic<quint64> token{quint64(1) << 53};
    return token.fetch_add(1);
}
} // namespace
QuerySettingsController::QuerySettingsController(EngineAdapter* adapter, QWidget* dialogParent,
                                                 QObject* parent)
    : QObject(parent), adapter_(adapter), dialogParent_(dialogParent) {
    initialToken_ = nextToken();
    connect(adapter, &EngineAdapter::queryPreferencesReady, this,
            [this](quint64 token, const QueryPreferences& value) {
                const bool initial = initialToken_ && token == initialToken_;
                if (!initial && !saves_.remove(token))
                    return;
                if (initial)
                    initialToken_ = 0;
                apply(value);
                if (initial && !ready_) {
                    ready_ = true;
                    emit readyChanged(true);
                }
            });
    connect(adapter, &EngineAdapter::recoveryFailed, this,
            [this](quint64 token, const QString& error) {
                const bool initial = initialToken_ && token == initialToken_;
                if (!initial && !saves_.remove(token))
                    return;
                if (initial) {
                    initialToken_ = 0;
                    ready_ = true;
                }
                emit failed(error);
                if (initial)
                    emit readyChanged(true);
            });
    // Defer submission so owners can connect ready/error signals before an
    // immediate queue or shutdown failure. Defaults are not ready until then.
    QTimer::singleShot(0, this, [this] {
        if (adapter_)
            adapter_->getQueryPreferences(initialToken_);
        else {
            initialToken_ = 0;
            ready_ = true;
            emit failed(tr("Query settings service is unavailable."));
            emit readyChanged(true);
        }
    });
}
void QuerySettingsController::open() {
    if (!adapter_) {
        emit failed(tr("Query settings service is unavailable."));
        return;
    }
    if (!dialog_) {
        dialog_ = new QuerySettingsDialog(adapter_, dialogParent_);
        dialog_->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog_, &QuerySettingsDialog::queryPreferencesSaveSubmitted, this,
                [this](quint64 token) { saves_.insert(token); });
        connect(dialog_, &QuerySettingsDialog::queryPreferencesConfirmed, this,
                &QuerySettingsController::apply);
    }
    dialog_->show();
    dialog_->raise();
    dialog_->activateWindow();
}
void QuerySettingsController::apply(const QueryPreferences& value) {
    const auto limits = EngineAdapter::queryPreferenceLimits();
    if (value.version != limits.version || value.pageSize < limits.minPageSize ||
        value.pageSize > limits.maxPageSize || value.timeoutSeconds > limits.maxTimeoutSeconds) {
        emit failed(tr("Invalid query settings received; current settings remain unchanged."));
        return;
    }
    if (value.version == preferences_.version && value.pageSize == preferences_.pageSize &&
        value.timeoutSeconds == preferences_.timeoutSeconds)
        return;
    preferences_ = value;
    emit preferencesChanged(preferences_);
}
} // namespace choscordb
