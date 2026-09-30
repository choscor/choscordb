#pragma once
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QVariant>
#include <QWidget>
#include <optional>
class QAction;
class QPushButton;
class QTabBar;
class QTableView;
class QStandardItemModel;
class QStackedWidget;
class QPlainTextEdit;
class QHBoxLayout;
namespace choscordb {
namespace design {
class Text;
class StatusLine;
} // namespace design
class EngineAdapter;
class ObjectErdWidget;
struct ObjectGraph;
struct ObjectInspection;
class ObjectExplorer final : public QWidget {
    Q_OBJECT
  public:
    explicit ObjectExplorer(EngineAdapter* adapter, QWidget* parent = nullptr);
    void openObject(quint64 connection, const QString& object, const QString& label,
                    const QString& kind = QString(), const QVariantList& properties = {});
    void restoreObject(std::optional<quint64> connection, const QString& object,
                       const QString& label, const QString& kind = QString(),
                       const QVariantList& properties = {});
    void activateRestoredObject();
    int paneIndex() const { return activePane_; }
    bool needsConnection() const { return !connection_.has_value(); }
    bool operationInFlight() const { return requestToken_ != 0 || operationBusy_; }
    void selectPane(int index);
    void setDisconnected();
    void installDataWidget(QWidget* widget);
    void setOperationBusy(bool busy);
  signals:
    void dataRequested(quint64 connection, const QString& object, const QString& label,
                       const QString& kind);
    void objectChanged();
    void sqlGenerated(quint64 connection, const QString& sql);
    void reconnectRequested();
    void paneChanged(int index);
    void relatedTableActivated(quint64 connection, const QString& objectId, const QString& label);

  private:
    void requestPane();
    void generateSql(const QString& kind);
    void updateActions();
    void updateFooter();
    void render(const ObjectInspection& inspection);
    void renderGraph(const ObjectGraph& graph);
    void setStatus(const QString& state, const QString& text);
    EngineAdapter* adapter_;
    QTabBar* tabs_;
    QTableView* table_;
    QStandardItemModel* model_;
    QStackedWidget* pages_;
    QPlainTextEdit* ddl_;
    ObjectErdWidget* erd_;
    design::Text* status_;
    QAction* retry_;
    QAction* reconnect_;
    QPushButton* refresh_;
    QPushButton* open_;
    QPushButton* generate_;
    design::StatusLine* footer_;
    QPointer<QWidget> dataFooter_;
    QList<QPointer<QWidget>> dataHeaderActions_;
    QHash<QString, QAction*> generationActions_;
    QStringList columns_;
    bool columnsLoaded_ = false;
    bool graphLoaded_ = false;
    QString graphState_, graphMessage_;
    std::optional<quint64> connection_;
    QString object_, label_, kind_;
    QVariantList properties_;
    quint64 requestToken_ = 0;
    bool operationBusy_ = false;
    bool restoredInert_ = false;
    int activePane_ = 0;
};
} // namespace choscordb
