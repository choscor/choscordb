#pragma once
#include "bridge/engine_adapter.h"
#include <QObject>
#include <QPointer>
#include <QSet>
namespace choscordb {
class QuerySettingsController final : public QObject {
    Q_OBJECT
  public:
    QuerySettingsController(EngineAdapter* adapter, QObject* parent = nullptr);
    const QueryPreferences& preferences() const { return preferences_; }
    bool isReady() const { return ready_; }
    void trackSave(quint64 token) { saves_.insert(token); }
    void applyConfirmed(const QueryPreferences& preferences) { apply(preferences); }
  signals:
    void preferencesChanged(const choscordb::QueryPreferences& preferences);
    void readyChanged(bool ready);
    void failed(const QString& error);

  private:
    void apply(const QueryPreferences& preferences);
    QPointer<EngineAdapter> adapter_;
    QueryPreferences preferences_;
    QSet<quint64> saves_;
    quint64 initialToken_ = 0;
    bool ready_ = false;
};
}
