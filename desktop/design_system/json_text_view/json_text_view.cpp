#include "design_system/json_text_view/json_text_view.h"

#include "design_system/fonts/fonts.h"
#include "design_system/theme.h"

#include <QEvent>
#include <QRegularExpression>
#include <QShowEvent>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>

namespace choscordb::design {
namespace {

class JsonSyntaxHighlighter final : public QSyntaxHighlighter {
  public:
    JsonSyntaxHighlighter(QTextDocument* document, const QWidget& view)
        : QSyntaxHighlighter(document), view_(view) {}

  protected:
    void highlightBlock(const QString& line) override {
        // JSON strings cannot contain a raw line break. Escapes stay within one
        // block, so no cross-block parser state is needed for valid documents.
        static const QRegularExpression tokens(QStringLiteral(
            R"json("(?:\\.|[^"\\])*"|-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?|\b(?:true|false|null)\b)json"));
        const auto colors = resolvedThemeForWidget(view_).colors;
        auto matches = tokens.globalMatch(line);
        while (matches.hasNext()) {
            const auto match = matches.next();
            const int start = match.capturedStart();
            const int end = match.capturedEnd();
            const QStringView token(line.constData() + start, end - start);
            QColor color;
            if (token.startsWith(QLatin1Char('"'))) {
                int next = end;
                while (next < line.size() && line.at(next).isSpace())
                    ++next;
                color = next < line.size() && line.at(next) == QLatin1Char(':') ? colors.jsonKey
                                                                                : colors.jsonString;
            } else if (token.startsWith(QLatin1Char('t')) || token.startsWith(QLatin1Char('f')) ||
                       token.startsWith(QLatin1Char('n'))) {
                color = colors.jsonLiteral;
            } else {
                color = colors.jsonNumber;
            }
            QTextCharFormat format;
            format.setForeground(color);
            setFormat(start, end - start, format);
        }
    }

  private:
    const QWidget& view_;
};

} // namespace

JsonTextView::JsonTextView(QWidget* parent) : QPlainTextEdit(parent) {
    setReadOnly(true);
    setProperty("designRole", "codePreview");
    setFrameShape(QFrame::NoFrame);
    setFont(resolveTypography(TypographyRole::Monospace));
    highlighter_ = new JsonSyntaxHighlighter(document(), *this);
}

void JsonTextView::changeEvent(QEvent* event) {
    QPlainTextEdit::changeEvent(event);
    if (highlighter_ &&
        (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange ||
         event->type() == QEvent::ParentChange))
        highlighter_->rehighlight();
}

void JsonTextView::showEvent(QShowEvent* event) {
    QPlainTextEdit::showEvent(event);
    if (highlighter_)
        highlighter_->rehighlight();
}

} // namespace choscordb::design
