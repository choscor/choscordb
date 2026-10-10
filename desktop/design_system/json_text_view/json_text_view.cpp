#include "design_system/json_text_view/json_text_view.h"

#include "design_system/fonts/fonts.h"
#include "design_system/theme.h"

#include <QEvent>
#include <QRegularExpression>
#include <QShowEvent>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>
#include <array>

namespace choscordb::design {
namespace {

// Syntax colors are presentation only. Very large documents stay plain so that a large page
// view does not spend its layout time in per-token regular-expression formatting.
constexpr int MaxHighlightedCharacters = 1024 * 1024;

class JsonSyntaxHighlighter final : public QSyntaxHighlighter {
  public:
    enum Token { Key, String, Literal, Number };

    explicit JsonSyntaxHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}

    // Returns whether the token colors changed.
    bool setColors(const Colors& colors) {
        const std::array<QColor, 4> next{colors.codeKeyword, colors.codeString, colors.codeComment,
                                         colors.codeNumber};
        if (resolved_ && next == colors_)
            return false;
        colors_ = next;
        for (std::size_t token = 0; token < formats_.size(); ++token)
            formats_[token].setForeground(colors_[token]);
        resolved_ = true;
        return true;
    }

  protected:
    void highlightBlock(const QString& line) override {
        if (!resolved_ || document()->characterCount() > MaxHighlightedCharacters)
            return;
        // JSON strings cannot contain a raw line break. Escapes stay within one
        // block, so no cross-block parser state is needed for valid documents.
        static const QRegularExpression tokens(QStringLiteral(
            R"json("(?:\\.|[^"\\])*"|-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?|\b(?:true|false|null)\b)json"));
        auto matches = tokens.globalMatch(line);
        while (matches.hasNext()) {
            const auto match = matches.next();
            const int start = match.capturedStart();
            const int end = match.capturedEnd();
            const QStringView token(line.constData() + start, end - start);
            Token kind = Number;
            if (token.startsWith(QLatin1Char('"'))) {
                int next = end;
                while (next < line.size() && line.at(next).isSpace())
                    ++next;
                kind = next < line.size() && line.at(next) == QLatin1Char(':') ? Key : String;
            } else if (token.startsWith(QLatin1Char('t')) || token.startsWith(QLatin1Char('f')) ||
                       token.startsWith(QLatin1Char('n'))) {
                kind = Literal;
            }
            setFormat(start, end - start, formats_[kind]);
        }
    }

  private:
    std::array<QColor, 4> colors_;
    std::array<QTextCharFormat, 4> formats_;
    bool resolved_ = false;
};

} // namespace

JsonTextView::JsonTextView(QWidget* parent) : QPlainTextEdit(parent) {
    setReadOnly(true);
    setProperty("designRole", "codePreview");
    setFrameShape(QFrame::NoFrame);
    setFont(resolveTypography(TypographyRole::Mono));
    auto* highlighter = new JsonSyntaxHighlighter(document());
    highlighter->setColors(resolvedThemeForWidget(*this).colors);
    highlighter_ = highlighter;
}

void JsonTextView::refreshHighlighting() {
    // Text changes are highlighted incrementally by QSyntaxHighlighter. Rehighlight the
    // whole document only when the resolved theme changed its token colors.
    if (highlighter_ && static_cast<JsonSyntaxHighlighter*>(highlighter_)
                            ->setColors(resolvedThemeForWidget(*this).colors))
        highlighter_->rehighlight();
}

void JsonTextView::changeEvent(QEvent* event) {
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange ||
        event->type() == QEvent::ParentChange)
        refreshHighlighting();
}

void JsonTextView::showEvent(QShowEvent* event) {
    QPlainTextEdit::showEvent(event);
    refreshHighlighting();
}

} // namespace choscordb::design
