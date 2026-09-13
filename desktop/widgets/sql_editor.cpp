#include "widgets/sql_editor.h"
#include "bridge/engine_adapter.h"
#include "design_system/theme.h"
#include <QEvent>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QTimer>
#include <Qsci/qsciabstractapis.h>
#include <Qsci/qscilexersql.h>

#include <QStringDecoder>
namespace choscordb {
namespace {
class KeywordApis final : public QsciAbstractAPIs {
  public:
    explicit KeywordApis(QsciLexer* lexer) : QsciAbstractAPIs(lexer) {}
    void updateAutoCompletionList(const QStringList& context, QStringList& list) override {
        if (context.size() == 1)
            list.append(EngineAdapter::keywordCompletions(context.last()));
    }
    QStringList callTips(const QStringList&, int, QsciScintilla::CallTipsStyle,
                         QList<int>&) override {
        return {};
    }
};
} // namespace
SqlEditor::SqlEditor(QWidget* parent) : QsciScintilla(parent) {
    setUtf8(true);
    setAccessibleName(tr("SQL editor"));
    const auto font = design::resolveTypography(design::TypographyRole::Monospace);
    setFont(font);
    setMarginsFont(font);
    setMarginType(0, QsciScintilla::NumberMargin);
    updateLineNumberMargin();
    setBraceMatching(QsciScintilla::SloppyBraceMatch);
    setAutoIndent(true);
    setIndentationWidth(4);
    setTabWidth(4);
    setIndentationsUseTabs(false);
    setFolding(QsciScintilla::BoxedTreeFoldStyle);
    auto* lexer = new QsciLexerSQL(this);
    lexer->setDefaultFont(font);
    setLexer(lexer);
    // Immutable keywords do not require QScintilla's asynchronous API preparation,
    // whose worker can access a lexer while its QObject children are destroyed.
    new KeywordApis(lexer);
    setAutoCompletionSource(QsciScintilla::AcsAll);
    setAutoCompletionCaseSensitivity(false);
    setAutoCompletionThreshold(2);
    connect(this, &QsciScintilla::textChanged, this, [this] { ++revision_; });
    connect(this, &QsciScintilla::linesChanged, this, &SqlEditor::updateLineNumberMargin);
    applyPalette();
}
bool SqlEditor::restoreDocument(const QByteArray& sql, const QString& path, quint64 cursor,
                                quint64 anchor, bool modified) {
    if (ioBusy_ || sql.size() > DocumentIo::MaximumBytes)
        return false;
    const auto boundary = [&sql](quint64 offset) {
        return offset <= static_cast<quint64>(sql.size()) &&
               (offset == static_cast<quint64>(sql.size()) ||
                (static_cast<unsigned char>(sql.at(static_cast<qsizetype>(offset))) & 0xc0) !=
                    0x80);
    };
    if (!sql.isValidUtf8() || !boundary(cursor) || !boundary(anchor))
        return false;
    SendScintilla(SCI_CLEARALL);
    SendScintilla(SCI_ADDTEXT, static_cast<uintptr_t>(sql.size()), sql.constData());
    SendScintilla(SCI_EMPTYUNDOBUFFER);
    path_ = path;
    setModified(modified);
    SendScintilla(SCI_SETSEL, static_cast<unsigned long>(anchor), static_cast<long>(cursor));
    return true;
}
void SqlEditor::setEditorFont(const QFont& font) {
    setFont(font);
    setMarginsFont(font);
    if (lexer()) {
        for (int style = 0; style < 128; ++style) {
            if (lexer()->description(style).isEmpty())
                continue;
            const auto previous = lexer()->font(style);
            auto styled = font;
            styled.setBold(previous.bold());
            styled.setItalic(previous.italic());
            styled.setUnderline(previous.underline());
            lexer()->setFont(styled, style);
        }
        lexer()->setDefaultFont(font);
    }
    updateLineNumberMargin();
}
void SqlEditor::updateLineNumberMargin() {
    const auto digits = qMax<qsizetype>(5, QString::number(lines()).size());
    setMarginWidth(0, QString(digits, QChar('0')));
}
void SqlEditor::setProfileId(const QString& id) {
    if (property("profileId").toString() == id)
        return;
    setProperty("profileId", id);
    emit profileAssociationChanged();
}
void SqlEditor::openFile(const QString& path) {
    if (ioBusy_) {
        QTimer::singleShot(0, this, [this, path] {
            emit fileOpened(path, tr("Another file operation is active."));
        });
        return;
    }
    ioBusy_ = true;
    const auto revision = revision_;
    auto* watcher = new QFutureWatcher<DocumentIoResult>(this);
    connect(watcher, &QFutureWatcher<DocumentIoResult>::finished, this,
            [this, watcher, path, revision] {
                const auto result = watcher->result();
                watcher->deleteLater();
                ioBusy_ = false;
                QString error = result.error;
                if (error.isEmpty() && revision != revision_)
                    error = tr("Document changed while loading; loaded text was discarded.");
                if (error.isEmpty()) {
                    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
                    const QString decoded = decoder.decode(result.bytes);
                    if (decoder.hasError())
                        error = tr("File is not valid UTF-8.");
                    else {
                        setText(decoded);
                        setModified(false);
                        path_ = path;
                    }
                }
                emit fileOpened(path, error);
            });
    watcher->setFuture(io_.read(path));
}
void SqlEditor::saveFile(const QString& path) {
    if (ioBusy_) {
        QTimer::singleShot(0, this, [this, path] {
            emit fileSaved(path, tr("Another file operation is active."));
        });
        return;
    }
    ioBusy_ = true;
    const auto revision = revision_;
    const auto bytes = text().toUtf8();
    auto* watcher = new QFutureWatcher<DocumentIoResult>(this);
    connect(watcher, &QFutureWatcher<DocumentIoResult>::finished, this,
            [this, watcher, path, revision] {
                const auto result = watcher->result();
                watcher->deleteLater();
                ioBusy_ = false;
                if (result.error.isEmpty()) {
                    path_ = path;
                    if (revision == revision_)
                        setModified(false);
                }
                emit fileSaved(path, result.error);
            });
    watcher->setFuture(io_.write(path, bytes));
}
void SqlEditor::applyPalette() {
    const auto base = palette().color(QPalette::Base);
    const auto foreground = palette().color(QPalette::Text);
    setPaper(base);
    setColor(foreground);
    lexer()->setDefaultPaper(base);
    lexer()->setDefaultColor(foreground);
    lexer()->setPaper(base, -1);
    lexer()->setColor(foreground, -1);
    auto keyword = palette().color(QPalette::Link);
    auto string = palette().color(QPalette::LinkVisited);
    if (base.lightness() < 128) {
        if (keyword.lightness() <= 128)
            keyword = foreground;
        if (string.lightness() <= 128)
            string = foreground;
    }
    lexer()->setColor(keyword, QsciLexerSQL::Keyword);
    lexer()->setColor(string, QsciLexerSQL::SingleQuotedString);
    for (const auto style :
         {QsciLexerSQL::Comment, QsciLexerSQL::CommentLine, QsciLexerSQL::CommentDoc})
        lexer()->setColor(palette().color(QPalette::PlaceholderText), style);
    setMarginsBackgroundColor(palette().color(QPalette::AlternateBase));
    setMarginsForegroundColor(foreground);
    setFoldMarginColors(base, base);
    setCaretForegroundColor(foreground);
    setSelectionBackgroundColor(palette().color(QPalette::Highlight));
    setSelectionForegroundColor(palette().color(QPalette::HighlightedText));
    setMatchedBraceBackgroundColor(palette().color(QPalette::Highlight));
    setMatchedBraceForegroundColor(palette().color(QPalette::HighlightedText));
}
void SqlEditor::changeEvent(QEvent* event) {
    QsciScintilla::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && lexer())
        applyPalette();
}
} // namespace choscordb
