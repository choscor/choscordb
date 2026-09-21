#pragma once

#include <QObject>
#include <QPointer>

class QDialog;
class QWidget;

namespace choscordb::design {

// Shared modal behavior for QDialog and QMessageBox without replacing their APIs.
class DialogPresentation final : public QObject {
  public:
    explicit DialogPresentation(QDialog& dialog);
    ~DialogPresentation() override;
    void makeModal();
    static QDialog* activeDialog(QWidget* owner = nullptr);
    void shown();
    void hidden();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void center();
    void scheduleCenter();
    QDialog& dialog_;
    QPointer<QWidget> owner_;
    QPointer<QWidget> anchor_;
    QPointer<QWidget> backdrop_;
    QPointer<QWidget> previousFocus_;
    bool centerPending_ = false;
    bool active_ = false;
};

void paintDialogSurface(QWidget& widget, bool drawBorder = true);

} // namespace choscordb::design
