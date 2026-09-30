#pragma once
#include "bridge/engine_adapter.h"
#include "design_system/dialog_shell/dialog_shell.h"
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
namespace choscordb {
namespace design {
class StatusLine;
}
class SshHostKeyDialog final : public DialogShell {
    Q_OBJECT
  public:
    SshHostKeyDialog(const QList<SshHostKeyCandidate>& candidates, const QString& knownHostsPath,
                     QWidget* parent = nullptr);
    void finishApproval(const QString& outcome);
    void showFailure(const QString& error);
    void invalidate();
  signals:
    void approvalRequested(const choscordb::SshHostKeyCandidate& candidate, const QString& path);
    void retryRequested();

  private:
    void selectCandidate();
    void updateActions();
    QList<SshHostKeyCandidate> candidates_;
    QListWidget* list_;
    QLineEdit *original_, *hostname_, *port_, *alias_, *algorithm_, *fingerprint_, *path_;
    QLabel* status_;
    design::StatusLine* statusLine_;
    QPushButton *approve_, *retry_;
    bool pending_ = false, valid_ = true, approved_ = false;
};
} // namespace choscordb
