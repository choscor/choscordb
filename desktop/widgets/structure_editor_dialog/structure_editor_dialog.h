#pragma once
#include "bridge/engine_adapter.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include <QPointer>
#include <QTimer>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QVBoxLayout;
class QWidget;
namespace choscordb {
namespace design {
class Button;
class StatusLine;
class Text;
}
struct BridgeEvent;
class StructureEditorDialog final : public DialogShell {
    Q_OBJECT
  public:
    StructureEditorDialog(EngineAdapter* adapter, quint64 connection, StructureTable table,
                          const QString& objectId, const QString& targetName,
                          QWidget* parent = nullptr);
    bool isApplying() const { return applyToken_ != 0; }

  signals:
    void applied(quint64 connection);

  protected:
    void reject() override;

  private:
    struct FieldInput {
        QString key;
        QWidget* widget = nullptr;
    };
    struct ChangeEditor {
        QString kind;
        QWidget* frame = nullptr;
        design::Text* title = nullptr;
        QList<FieldInput> fields;
    };
    StructureTable table() const;
    QList<StructureChange> changes() const;
    QWidget* fieldWidget(const StructureField& field, QWidget* parent);
    void addChange(const StructureChangeKindInfo& kind);
    void removeChange(QWidget* frame);
    void updateReview();
    void updateActions();
    void apply();
    void handleEvent(const BridgeEvent& event);
    void handleInspection(quint64 connection, quint64 token, const ObjectInspection& inspection);
    void failLoad(const QString& error);
    void showStatus(const QString& message, bool error, bool busy);
    QPointer<EngineAdapter> adapter_;
    quint64 connection_;
    StructureTable base_;
    QList<StructureChangeKindInfo> kinds_;
    QStringList types_;
    QList<ChangeEditor> editors_;
    quint64 targetToken_ = 0, indexesToken_ = 0, keysToken_ = 0, applyToken_ = 0;
    bool loadFailed_ = false;
    bool planValid_ = false;
    QTimer reviewTimer_;
    QLineEdit* tableName_ = nullptr;
    design::Button* add_ = nullptr;
    QWidget* changeList_ = nullptr;
    QVBoxLayout* changeLayout_ = nullptr;
    QLabel* empty_ = nullptr;
    QLabel* error_ = nullptr;
    QPlainTextEdit* sqlPreview_ = nullptr;
    design::StatusLine* statusLine_ = nullptr;
    design::Button* cancel_ = nullptr;
    design::Button* apply_ = nullptr;
};
}
