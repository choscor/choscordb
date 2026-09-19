#pragma once
#include "models/shortcut_catalog.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
class QAction;
namespace choscordb {
class MainWindow;
class PreferencesDialog;
class AppearanceController;
class SqlEditor;
class EditorPreferencesController final : public QObject {
    Q_OBJECT
  public:
    explicit EditorPreferencesController(MainWindow* window);
    void addAction(const QString& id, QAction* action, bool configurable = true);
    void addEditor(SqlEditor* editor);
    void initialize(EngineAdapter* adapter);
    void setAppearanceController(AppearanceController* appearance);
    void open();

  signals:
    void queryPreferencesSaveSubmitted(quint64 token);
    void queryPreferencesConfirmed(const choscordb::QueryPreferences& preferences);
    void historyPolicyConfirmed(const choscordb::HistoryPolicy& policy);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void apply(const EditorPreferences& value);
    void applyFont(SqlEditor* editor);
    MainWindow* window_;
    QPointer<EngineAdapter> adapter_;
    QPointer<PreferencesDialog> dialog_;
    QPointer<AppearanceController> appearance_;
    QList<ShortcutDescriptor> catalog_;
    QHash<QString, QAction*> actions_;
    QList<QPointer<SqlEditor>> editors_;
    EditorPreferences preferences_;
    QSet<quint64> pendingSaves_;
    bool loading_ = true;
};
} // namespace choscordb
