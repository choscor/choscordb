#pragma once

#include <QPlainTextEdit>

class QEvent;
class QShowEvent;
class QSyntaxHighlighter;

namespace choscordb::design {

class JsonTextView final : public QPlainTextEdit {
    Q_OBJECT

  public:
    explicit JsonTextView(QWidget* parent = nullptr);

  protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private:
    void refreshHighlighting();
    QSyntaxHighlighter* highlighter_ = nullptr;
};

}
