#pragma once

#include <QPlainTextEdit>

class QEvent;
class QShowEvent;
class QSyntaxHighlighter;

namespace choscordb::design {

// Selectable, read-only JSON presentation. The document remains plain text;
// syntax colors live only in the layout formats.
class JsonTextView final : public QPlainTextEdit {
    Q_OBJECT

  public:
    explicit JsonTextView(QWidget* parent = nullptr);

  protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private:
    QSyntaxHighlighter* highlighter_ = nullptr;
};

} // namespace choscordb::design
