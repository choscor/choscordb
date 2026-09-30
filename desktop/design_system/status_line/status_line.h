#pragma once

#include <QList>
#include <QPoint>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;

class QHBoxLayout;

namespace choscordb::design {

struct ResolvedTheme;
QString statusLineStyleSheet(const ResolvedTheme& theme);

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

  protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

  private:
    void refreshAppearance();
    void fitContent();
    Fields fields_;
    Content values_;
    bool centered_ = false;
    QList<QPoint> separators_;
    QWidget* paging_ = nullptr;
    QHBoxLayout* content_ = nullptr;
    State state_ = State::Neutral;
    bool refreshing_ = false;
};

} // namespace choscordb::design
