#pragma once

#include <QPointer>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QToolButton;
class QDialog;
class QPlainTextEdit;

namespace choscordb::design {

// The shared bottom line for workspace results and object panes.
class StatusLine final : public QWidget {
    Q_OBJECT
  public:
    explicit StatusLine(QWidget* parent = nullptr);
    QHBoxLayout* contentLayout() const;
    void setAvailable(bool available);
    void setNeutral(bool neutral = true);
    void setMessage(const QString& message);
    void setBusy(bool busy);

  protected:
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void refreshAppearance();
    void openDetails();
    void refreshDetailsVisibility();
    QHBoxLayout* content_ = nullptr;
    QLabel* message_ = nullptr;
    QLabel* loading_ = nullptr;
    QToolButton* details_ = nullptr;
    QPointer<QDialog> detailsDialog_;
    QPointer<QPlainTextEdit> detailsText_;
    QString fullMessage_;
    bool available_ = false;
    bool neutral_ = false;
    bool refreshing_ = false;
};

} // namespace choscordb::design
