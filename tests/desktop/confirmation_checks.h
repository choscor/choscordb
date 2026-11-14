#pragma once

#include <QAbstractButton>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <memory>

inline QAbstractButton* confirmationAction(const QMessageBox* box) {
    for (auto* button : box->buttons())
        if (button->property("confirmationConsequence").isValid())
            return button;
    return nullptr;
}

inline bool destructiveConfirmation(const QAbstractButton* button) {
    return button->property("confirmationConsequence").toString() == QLatin1String("destructive");
}

class ConfirmedPrompt {
  public:
    ConfirmedPrompt()
        : poll_(std::make_unique<QTimer>()), title_(std::make_shared<QString>()),
          text_(std::make_shared<QString>()) {
        poll_->setInterval(5);
        QObject::connect(poll_.get(), &QTimer::timeout, poll_.get(),
                         [poll = poll_.get(), title = title_, text = text_] {
                             for (auto* widget : QApplication::allWidgets()) {
                                 auto* box = qobject_cast<QMessageBox*>(widget);
                                 auto* action =
                                     box && box->isVisible() ? confirmationAction(box) : nullptr;
                                 if (!action)
                                     continue;
                                 *title = box->windowTitle();
                                 *text = box->text();
                                 poll->stop();
                                 action->click();
                                 return;
                             }
                         });
        poll_->start();
    }
    const QString& operator*() const { return *title_; }
    const QString& text() const { return *text_; }

  private:
    std::unique_ptr<QTimer> poll_;
    std::shared_ptr<QString> title_;
    std::shared_ptr<QString> text_;
};

[[nodiscard]] inline ConfirmedPrompt confirmNextPrompt() {
    return {};
}
