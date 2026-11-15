#pragma once
#include "bridge/value_preview.h"
#include "design_system/dialog_shell/dialog_shell.h"
#include <QByteArray>

class QLabel;
class QPlainTextEdit;
class QPushButton;
namespace choscordb {
class ValuePreviewDialog final : public DialogShell {
    Q_OBJECT
  public:
    ValuePreviewDialog(const ValuePreview& preview, const QByteArray& bytes,
                       QWidget* parent = nullptr);

  private:
    void showPdfPage(int number);
    void updatePdfPageStatus(const QString& error);
    QPlainTextEdit* text_ = nullptr;
    QByteArray bytes_;
    QLabel* picture_ = nullptr;
    QLabel* pageStatus_ = nullptr;
    QPushButton* previousPage_ = nullptr;
    QPushButton* nextPage_ = nullptr;
    int pdfPage_ = 1;
    int pdfPages_ = 0;
};
}
