#pragma once
#include "models/shortcut_catalog.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
class QAction;
class QMainWindow;
namespace choscordb {
class PreferencesDialog;
class SqlEditor;
class EditorPreferencesController final : public QObject {
    Q_OBJECT
  public:
    explicit EditorPreferencesController(QMainWindow* window);
    void addAction(const QString& id, QAction* action, bool configurable = true);
    void addEditor(SqlEditor* editor);
    void initialize(EngineAdapter* adapter);
    void open();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void apply(const EditorPreferences& value);
    void applyFont(SqlEditor* editor);
    QMainWindow* window_;
    QPointer<EngineAdapter> adapter_;
    QPointer<PreferencesDialog> dialog_;
    QList<ShortcutDescriptor> catalog_;
    QHash<QString, QAction*> actions_;
    QList<QPointer<SqlEditor>> editors_;
    EditorPreferences preferences_;
    QSet<quint64> pendingSaves_;
    bool loading_ = true;
};
} // namespace choscordb
