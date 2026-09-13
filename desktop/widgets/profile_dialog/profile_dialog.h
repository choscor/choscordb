#pragma once
#include "bridge/engine_adapter.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include <QPointer>
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QCheckBox;
class QSpinBox;
namespace choscordb {
class ProfileDialog final : public DialogShell {
    Q_OBJECT
  public:
    explicit ProfileDialog(EngineAdapter* adapter, QWidget* parent = nullptr);
    void selectProfile(const QString& id);
    void saveDraft(const SavedProfile& profile);
    void testDraft(const SavedProfile& profile);
  signals:
    void connectionSubmitted(const choscordb::SavedProfile& profile, quint64 id, bool savedProfile);

  private:
    SavedProfile draft() const;
    void setDraft(const SavedProfile& profile);
    void refresh();
    void setBusy(bool busy, const QString& message = {});
    void updateDriver();
    bool discardChanges();
    QPointer<EngineAdapter> adapter_;
    QList<SavedProfile> profiles_;
    SavedProfile current_;
    QString pendingSelection_;
    QString refreshNotice_;
    bool savingDraft_ = false;
    bool preservePasswordOnRefresh_ = false;
    quint64 token_ = 0;
    quint64 revision_ = 0;
    bool busy_ = false;
    bool connecting_ = false;
    QString connectionSubmissionError_;
    std::optional<quint64> pendingConnection_;
    bool dirty_ = false;
    bool filling_ = false;
    QListWidget* list_;
    QWidget* form_;
    QWidget* sqliteFields_;
    QWidget* postgresFields_;
    QLineEdit *name_, *path_, *host_, *database_, *user_, *rootCertificate_, *password_;
    QComboBox *driver_, *tls_;
    QCheckBox *readOnly_, *rememberPassword_;
    QSpinBox* port_;
    QLabel* status_;
    QList<QPushButton*> actions_;
};
} // namespace choscordb
