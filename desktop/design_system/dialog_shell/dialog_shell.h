#pragma once

#include <QDialog>

class QLabel;
class QResizeEvent;
class QShowEvent;

namespace choscordb {
namespace design {
class DialogPresentation;
}

class DialogShell : public QDialog {
    Q_OBJECT

  public:
    explicit DialogShell(QWidget* parent = nullptr);
    void setAppModal();
    void open() override;
    [[nodiscard]] QLabel* createDescription(const QString& text, QWidget* parent);
    [[nodiscard]] QLabel* createInlineStatus(QWidget* parent);

  protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

  private:
    void applyLayoutMetrics();
    void ensureContentHeight();
    bool observingTheme_ = false;
    design::DialogPresentation* presentation_ = nullptr;
};

} // namespace choscordb
