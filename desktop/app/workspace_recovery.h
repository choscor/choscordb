#pragma once
#include "bridge/engine_adapter.h"
#include <QObject>
#include <QTimer>
#include <functional>
class QTabWidget;
class QWidget;
namespace choscordb {
class SqlEditor;
// Recovery values 4 (Data) and 5 (ERD) intentionally differ from their tab positions.
quint32 savedObjectPane(int paneIndex);
int restoredObjectPane(quint32 savedPane);
// Recovery transports inert editor data only. The controller never opens a
// connection or reads a restored file, and owns at most one pending snapshot.
class WorkspaceRecoveryController final : public QObject {
    Q_OBJECT
  public:
    WorkspaceRecoveryController(QTabWidget* tabs, std::function<SqlEditor*()> addEditor,
                                QObject* parent = nullptr);
    void start();
    void watchEditor(SqlEditor* editor);
    QList<SavedWorkspaceTab> snapshotTabs() const;
    void setObjectFactory(std::function<QWidget*(const SavedWorkspaceTab&)> factory);
    bool isReady() const { return ready_; }
    bool isClosing() const { return closing_; }
  public slots:
    void changed();
    void flush();
    void retry();
    void startEmpty();
    void requestClose();
    void cancelClose();
    void closeWithoutRecovery();
    void restoredTabs(quint64 token, const QList<choscordb::SavedWorkspaceTab>& tabs,
                      quint32 activeIndex);
    void saved(quint64 token);
    void failed(quint64 token, const QString& error);
  signals:
    void saveTabsRequested(const QList<choscordb::SavedWorkspaceTab>& tabs, quint32 activeIndex,
                           quint64 token);
    void restoreTabsRequested(quint64 token);
    void mutationEnabled(bool enabled);
    void errorOccurred(const QString& error, bool duringClose);
    void persistenceSucceeded();
    void restoreCompleted(bool hasDocuments);
    void closeReady();

  private:
    void beginRestore();
    void setEnabled(bool enabled);
    void clearTabs();
    void applyTabs(const QList<SavedWorkspaceTab>& tabs, quint32 activeIndex);
    QTabWidget* tabs_;
    std::function<SqlEditor*()> addEditor_;
    std::function<QWidget*(const SavedWorkspaceTab&)> objectFactory_;
    QTimer debounce_;
    quint64 pending_ = 0;
    quint64 revision_ = 0;
    quint64 sentRevision_ = 0;
    bool ready_ = false;
    bool restoring_ = false;
    bool applying_ = false;
    bool closing_ = false;
    bool dirty_ = false;
    bool failed_ = false;
};
} // namespace choscordb
