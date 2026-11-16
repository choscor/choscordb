#include "query_settings.h"
#include "bridge/request_token.h"
#include <QTimer>
namespace choscordb {
QuerySettingsController::QuerySettingsController(EngineAdapter* adapter, QObject* parent)
    : QObject(parent), adapter_(adapter) {
    initialToken_ = nextRequestToken();
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
    QTimer::singleShot(0, this, [this] {
        if (adapter_)
            adapter_->getQueryPreferences(initialToken_);
        else {
            initialToken_ = 0;
            ready_ = true;
            emit failed(tr("Query preferences are not available."));
            emit readyChanged(true);
        }
    });
}
void QuerySettingsController::apply(const QueryPreferences& value) {
    if (value == preferences_)
        return;
    preferences_ = value;
    emit preferencesChanged(preferences_);
}
}
