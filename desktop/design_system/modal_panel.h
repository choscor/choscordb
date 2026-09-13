#pragma once
#include <QDialog>
#include <QPointer>

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

void paintDialogSurface(QWidget& widget);

// Application modal surface; standard QDialog rejection never accepts an action.
class ModalPanel : public QDialog {
    Q_OBJECT
  public:
    explicit ModalPanel(QWidget* parent);
    ~ModalPanel() override;
    [[nodiscard]] QSize sizeHint() const override;

  protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    DialogPresentation presentation_;
};
} // namespace choscordb::design
