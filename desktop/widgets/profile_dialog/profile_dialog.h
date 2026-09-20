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
namespace design {
class FieldValidation;
}
class ProfileDialog final : public DialogShell {
    Q_OBJECT
  public:
    explicit ProfileDialog(EngineAdapter* adapter, QWidget* parent = nullptr);
    void newProfile();
    void selectProfile(const QString& id);
    void manageProfile(const QString& id, const QString& action);
    void saveDraft(const SavedProfile& profile);
    void testDraft(const SavedProfile& profile);
  signals:
    void connectionSubmitted(const choscordb::SavedProfile& profile, quint64 id, bool savedProfile);
    void openQueryRequested(quint64 connection);

  protected:
    void showEvent(QShowEvent* event) override;

  private:
    SavedProfile draft() const;
    void setDraft(const SavedProfile& profile);
    void refresh();
    void setBusy(bool busy, const QString& message = {});
    void updateDriver();
    bool discardChanges();
    void connectDraft(bool openQuery);
    void dispatchProfileAction();
    QPointer<EngineAdapter> adapter_;
    QList<SavedProfile> profiles_;
    SavedProfile current_;
    QString pendingSelection_;
    QString managedProfile_, managedAction_;
    QString refreshNotice_;
    bool savingDraft_ = false;
    bool connectAfterSave_ = false;
    bool openQueryAfterConnect_ = false;
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
    QWidget* sshFields_;
    QLineEdit *name_, *path_, *host_, *database_, *user_, *rootCertificate_, *password_;
    QComboBox *driver_, *tls_;
    QCheckBox *readOnly_, *rememberPassword_;
    QSpinBox* port_;
    QCheckBox* sshEnabled_;
    QLineEdit *sshHost_, *sshUser_, *sshIdentityFile_;
    QSpinBox* sshPort_;
    QLabel* status_;
    design::FieldValidation* nameValidation_;
    QList<QPushButton*> actions_;
    QPushButton *sqliteChoice_, *postgresChoice_, *mysqlChoice_;
};
} // namespace choscordb
