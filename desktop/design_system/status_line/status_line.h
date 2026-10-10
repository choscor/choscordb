#pragma once

#include <QList>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;

class QHBoxLayout;
class QToolButton;
class QDialog;
class QPlainTextEdit;

namespace choscordb::design {

// The shared bottom line for workspace results and object panes.
class StatusLine final : public QWidget {
    Q_OBJECT
  public:
    explicit StatusLine(QWidget* parent = nullptr);
    enum class State { Neutral, Success, Error };
    void setState(State state);
    struct Fields {
        QLabel* source = nullptr;
        QLabel* outcome = nullptr;
        QLabel* duration = nullptr;
        QLabel* memory = nullptr;
        QLabel* page = nullptr;
        QLabel* rows = nullptr;
        QPushButton* previous = nullptr;
        QPushButton* next = nullptr;
    };
    struct Content {
        QString source, outcome, duration, memory, page, rows;
    };
    void configure(const Fields& fields, bool centered = false);
    void setContent(const Content& content, State state);
    QWidget* pagingWidget() const { return paging_; }
    void setAvailable(bool available);
    void setNeutral(bool neutral = true);
    void setMessage(const QString& message);
    void setBusy(bool busy);

  protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

  private:
    void refreshAppearance();
    void fitContent(bool reserveDetails = false);
    void refreshContentDescription();
    void refreshDetailsVisibility();
    void openDetails();
    Fields fields_;
    Content values_;
    bool centered_ = false;
    bool configured_ = false;
    QList<QPoint> separators_;
    QWidget* paging_ = nullptr;
    QHBoxLayout* content_ = nullptr;
    QLabel* message_ = nullptr;
    QLabel* loading_ = nullptr;
    QToolButton* details_ = nullptr;
    QPointer<QDialog> detailsDialog_;
    QPointer<QPlainTextEdit> detailsText_;
    QString fullMessage_;
    QString feedbackMessage_;
    State state_ = State::Neutral;
    bool refreshing_ = false;
};

} // namespace choscordb::design
