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
    void shown();
    void hidden();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void center();
    QDialog& dialog_;
    QPointer<QWidget> owner_;
    QPointer<QWidget> backdrop_;
    QPointer<QWidget> previousFocus_;
};

void paintDialogSurface(QWidget& widget, bool drawBorder = true);

} // namespace choscordb::design
