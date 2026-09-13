#pragma once
#include "bridge/completion_service.h"
#include <QObject>
#include <QPointer>
class QCompleter;
class QModelIndex;
class QStandardItemModel;
class QTimer;
namespace choscordb {
class SqlEditor;
class EditorCompletionController final : public QObject {
    Q_OBJECT
  public:
    explicit EditorCompletionController(QObject* parent = nullptr);
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
    QPointer<SqlEditor> editor_;
    QCompleter* completer_;
    QStandardItemModel* model_;
    QTimer* timer_;
    CompletionService service_;
    QList<QMetaObject::Connection> editorConnections_;
    quint64 generation_ = 0, shownGeneration_ = 0, shownRevision_ = 0;
    quint64 shownStart_ = 0, shownEnd_ = 0;
    bool busy_ = false, pending_ = false, requested_ = false, inserting_ = false;
};
} // namespace choscordb
