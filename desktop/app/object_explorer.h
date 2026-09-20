#pragma once
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QVariant>
#include <QWidget>
#include <optional>
class QAction;
class QLabel;
class QPushButton;
class QTabBar;
class QTableView;
class QStandardItemModel;
class QStackedWidget;
class QPlainTextEdit;
class QHBoxLayout;
namespace choscordb {
class EngineAdapter;
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

  private:
    void requestPane();
    void generateSql(const QString& kind);
    void updateActions();
    void updateFooter();
    void render(const ObjectInspection& inspection);
    void setStatus(const QString& state, const QString& text);
    EngineAdapter* adapter_;
    QTabBar* tabs_;
    QTableView* table_;
    QStandardItemModel* model_;
    QStackedWidget* pages_;
    QPlainTextEdit* ddl_;
    QLabel* status_;
    QAction* retry_;
    QAction* reconnect_;
    QPushButton* refresh_;
    QPushButton* open_;
    QPushButton* generate_;
    QHBoxLayout* footer_;
    QPointer<QWidget> dataFooter_;
    QList<QPointer<QWidget>> dataHeaderActions_;
    QHash<QString, QAction*> generationActions_;
    QStringList columns_;
    bool columnsLoaded_ = false;
    std::optional<quint64> connection_;
    QString object_, label_, kind_;
    QVariantList properties_;
    quint64 requestToken_ = 0;
    bool operationBusy_ = false;
    bool restoredInert_ = false;
    int activePane_ = 0;
};
} // namespace choscordb
