#pragma once

#include <QDialog>

class QLabel;
class QShowEvent;

namespace choscordb {

class DialogShell : public QDialog {
    Q_OBJECT

  public:
    explicit DialogShell(QWidget* parent = nullptr);
    [[nodiscard]] QLabel* createDescription(const QString& text, QWidget* parent);
    [[nodiscard]] QLabel* createInlineStatus(QWidget* parent);

  protected:
    void showEvent(QShowEvent* event) override;

  private:
    void applyLayoutMetrics();
    bool observingTheme_ = false;
};

} // namespace choscordb
