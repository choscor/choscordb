#pragma once
#include "bridge/completion_service.h"
#include "design_system/icons.h"
#include <QObject>
#include <QPointer>
#include <functional>
#include <limits>
class QCompleter;
class QModelIndex;
class QStandardItemModel;
class QTimer;
namespace choscordb {
class SqlEditor;
class EditorCompletionController final : public QObject {
    Q_OBJECT
  public:
    using KindIcon = std::function<design::Icon(const QString&)>;
    explicit EditorCompletionController(KindIcon kindIcon, QObject* parent = nullptr);
    void setEditor(SqlEditor* editor);
    void setCatalog(CompletionService service);
    void requestCompletion(bool requested = true);
  signals:
    void partialCatalog(bool partial);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void dismiss();
    void launch();
    void accept(const QModelIndex& index);
    KindIcon kindIcon_;
    QPointer<SqlEditor> editor_;
    QCompleter* completer_;
    QStandardItemModel* model_;
    QTimer* timer_;
    CompletionService service_;
    QList<QMetaObject::Connection> editorConnections_;
    quint64 generation_ = 0, shownGeneration_ = 0, shownRevision_ = 0;
    quint64 shownStart_ = 0, shownEnd_ = 0;
    quint64 launchedCursor_ = std::numeric_limits<quint64>::max();
    bool busy_ = false, pending_ = false, requested_ = false, inserting_ = false;
};
}
