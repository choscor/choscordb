#pragma once
#include <QWidget>
class QCheckBox;
class QLabel;
class QPlainTextEdit;
namespace choscordb {
struct SshPrivateKeyDraft {
    QString secret;
    bool modified = false;
    bool remember = false;
};
// The text document contains only a masked preview, never private-key material.
class SshPrivateKeyEditor final : public QWidget {
    Q_OBJECT
  public:
    explicit SshPrivateKeyEditor(const QString& prefix, QWidget* parent = nullptr);
    SshPrivateKeyDraft draft() const;
    void setDraft(const SshPrivateKeyDraft& value, bool saved = false);
  signals:
    void changed();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void paste();
    void refresh();
    SshPrivateKeyDraft value_;
    QPlainTextEdit* preview_;
    QCheckBox* remember_;
    QLabel* error_;
};
} // namespace choscordb
