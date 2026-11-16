#pragma once
#include "bridge/engine_adapter.h"
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <optional>

class QLabel;
class QPlainTextEdit;
class QVBoxLayout;
namespace choscordb {
namespace design {
class Button;
class Text;
}
struct BridgeEvent;
class StructureEditor final : public QWidget {
    Q_OBJECT
  public:
    StructureEditor(EngineAdapter* adapter, quint64 connection, StructureTable table,
                    const QString& objectId, QWidget* parent = nullptr);
    void load();
    void setTableName(const QString& name);
    void setPane(std::optional<ObjectInspectionPane> pane);
    [[nodiscard]] bool hasChanges() const { return !editors_.isEmpty(); }
    [[nodiscard]] qsizetype changeCount() const { return editors_.size(); }
    [[nodiscard]] bool isApplying() const { return applyToken_ != 0; }
    [[nodiscard]] bool loading() const {
        return targetToken_ != 0 || indexesToken_ != 0 || keysToken_ != 0;
    }
    [[nodiscard]] bool canApply() const;
    [[nodiscard]] QString shownSql() const;
    [[nodiscard]] QString plannedSql();
    bool apply(const QString& reviewedSql);

  signals:
    void stateChanged();
    void statusChanged(const QString& message, bool error, bool busy);
    void applied(quint64 connection);
    void applyFailed(quint64 connection);

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
    void handleEvent(const BridgeEvent& event);
    void load(bool announce);
    void handleInspection(quint64 connection, quint64 token, const ObjectInspection& inspection);
    void finishLoadStep();
    void failLoad(const QString& error);
    void fail(const QString& error);
    QPointer<EngineAdapter> adapter_;
    quint64 connection_;
    StructureTable base_;
    QString objectId_;
    QString tableName_;
    QList<StructureChangeKindInfo> kinds_;
    QStringList types_;
    QList<ChangeEditor> editors_;
    quint64 targetToken_ = 0, indexesToken_ = 0, keysToken_ = 0, applyToken_ = 0;
    bool loadFailed_ = false;
    bool loadAnnounced_ = false;
    bool disconnected_ = false;
    QString failure_;
    void clearApplyFailure();
    void editChanges();
    bool planValid_ = false;
    QTimer reviewTimer_;
    design::Button* add_ = nullptr;
    QWidget* changeList_ = nullptr;
    QVBoxLayout* changeLayout_ = nullptr;
    QLabel* empty_ = nullptr;
    QLabel* error_ = nullptr;
    QPlainTextEdit* sqlPreview_ = nullptr;
};
}
