#include "design_system/menu/menu.h"
#include "design_system/theme.h"
#include "design_system/theme_manager.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QContextMenuEvent>
#include <QFile>
#include <QMenu>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <Qsci/qsciabstractapis.h>
#include <Qsci/qscilexersql.h>
#include <QtConcurrentRun>
#include <QtTest>
class EditorTest : public QObject {
    Q_OBJECT
  private slots:
    void contextMenuOpensAtPointer() {
        choscordb::SqlEditor editor;
        editor.resize(500, 300);
        editor.move(300, 250);
        editor.show();
        QCoreApplication::processEvents();
        const QPoint local(180, 90);
        const QPoint global = editor.mapToGlobal(local);
        QPoint actual;
        bool found = false;
        QTimer::singleShot(0, &editor, [&] {
            QMenu* menu = nullptr;
            for (auto* widget : QApplication::topLevelWidgets())
                if (auto* candidate = qobject_cast<QMenu*>(widget);
                    candidate && candidate->isVisible())
                    menu = candidate;
            if (menu) {
                actual = menu->pos();
                found = menu->isVisible() && !menu->actions().isEmpty();
                menu->close();
            }
        });
        QContextMenuEvent event(QContextMenuEvent::Mouse, local, global);
        QApplication::sendEvent(editor.viewport(), &event);
        QVERIFY(found);
        QCOMPARE(actual, choscordb::design::detail::contextMenuPosition(global));
    }
    void lineNumberGutterFitsContentAndFont() {
        choscordb::SqlEditor editor;
        editor.setText("SELECT 1;\nSELECT 2;");
        editor.resize(400, 160);
        editor.show();
        QCoreApplication::processEvents();
        const auto digitWidth = editor.SendScintilla(QsciScintilla::SCI_TEXTWIDTH,
                                                     QsciScintilla::STYLE_LINENUMBER, "0");
        QVERIFY(editor.marginWidth(0) >= digitWidth * 3);
        QVERIFY(editor.marginWidth(0) < digitWidth * 5);
        const auto image = editor.grab().toImage();
        int leftmostInk = editor.marginWidth(0);
        for (int y = 3; y < 24; ++y)
            for (int x = 1; x < editor.marginWidth(0); ++x)
                if (image.pixelColor(x, y).lightness() < 100)
                    leftmostInk = qMin(leftmostInk, x);
        QVERIFY(leftmostInk > 2);
        const auto originalWidth = editor.marginWidth(0);
        auto font = editor.font();
        font.setPointSize(24);
        editor.setEditorFont(font);
        QVERIFY(editor.marginWidth(0) > originalWidth);
    }
    void defaultPixelFontRendersLegiblyAndCustomPointFontPreservesEditing() {
        using namespace choscordb::design;
        choscordb::SqlEditor editor;
        editor.setPalette(applicationPalette(
            {ResolvedAppearance::Light, resolveColors(ResolvedAppearance::Light, {}), false}));
        editor.setText("MMMM\nMMMM");
        editor.resize(500, 220);
        editor.show();
        editor.clearFocus();
        QCoreApplication::processEvents();
        const auto capHeight = [&editor] {
            const auto capture = editor.grab();
            const auto image = capture.toImage();
            const auto scale = capture.devicePixelRatio();
            const auto left = editor.SendScintilla(QsciScintilla::SCI_POINTXFROMPOSITION, 0UL, 0L);
            const auto right = editor.SendScintilla(QsciScintilla::SCI_POINTXFROMPOSITION, 0UL, 4L);
            const auto lineHeight = editor.SendScintilla(QsciScintilla::SCI_TEXTHEIGHT, 0UL);
            int first = image.height(), last = -1;
            for (int y = 1; y < qRound(lineHeight * scale); ++y)
                for (int x = qRound((left + 2) * scale); x < qRound(right * scale); ++x)
                    if (image.pixelColor(x, y).lightness() < 120) {
                        first = qMin(first, y);
                        last = qMax(last, y);
                    }
            return last < first ? 0 : qRound((last - first + 1) / scale);
        };
        // A 13px monospace capital must be visibly legible, not the 1pt glyph
        // produced when QScintilla receives a pixel QFont as pointSizeF == -1.
        const auto defaultCaps = capHeight();
        QVERIFY2(defaultCaps >= 8, qPrintable(QString::number(defaultCaps)));
        QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_TEXTHEIGHT, 0UL), 24L);
        const auto defaultWidth =
            editor.SendScintilla(QsciScintilla::SCI_TEXTWIDTH, QsciLexerSQL::Default, "MMMM");
        // Platform fixed-font fallback differs under Qt's offscreen plugin.
        // Its absolute advance is not a reference constant; compare actual
        // glyph growth after the user's larger point-size preference instead.
        auto custom = resolveTypography(TypographyRole::Monospace);
        custom.setPointSize(22);
        editor.setEditorFont(custom);
        QCoreApplication::processEvents();
        QVERIFY(capHeight() >= 16);
        QVERIFY(editor.SendScintilla(QsciScintilla::SCI_TEXTHEIGHT, 0UL) > 24L);
        QVERIFY(editor.SendScintilla(QsciScintilla::SCI_TEXTWIDTH, QsciLexerSQL::Default, "MMMM") >
                defaultWidth * 1.5);
        editor.insertAt(";", 1, 4);
        editor.setSelection(0, 1, 0, 3);
        const auto revision = editor.revision();
        editor.setEditorFont(resolveTypography(TypographyRole::Monospace));
        QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_TEXTHEIGHT, 0UL), 24L);
        QCOMPARE(editor.selectedText(), QString("MM"));
        QCOMPARE(editor.revision(), revision);
        QCOMPARE(editor.text(), QString("MMMM\nMMMM;"));
        editor.undo();
        QCOMPARE(editor.text(), QString("MMMM\nMMMM"));
    }
    void persistedPointFontUsesReferenceMinimumLineBox() {
        choscordb::SqlEditor editor;
        auto font = editor.font();
        font.setPointSize(13);
        editor.setEditorFont(font);
        QCOMPARE(editor.lexer()->font(QsciLexerSQL::Default).pointSize(), 13);
        QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_TEXTHEIGHT, 0UL), 24L);
        font.setPointSize(32);
        editor.setEditorFont(font);
        QVERIFY(editor.SendScintilla(QsciScintilla::SCI_TEXTHEIGHT, 0UL) > 24L);
        QCOMPARE(editor.extraAscent(), 0);
        QCOMPARE(editor.extraDescent(), 0);
    }
    void syntaxColorsFollowLiveThemeAndForcedContrast() {
        using namespace choscordb::design;
        choscordb::SqlEditor editor;
        ThemeManager manager;
        manager.setMode(ThemeMode::Light);
        manager.applyTo(editor);
        editor.setText("SELECT 'sample', 42; -- comment");
        editor.recolor();
        QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_GETSTYLEAT, 0UL),
                 static_cast<long>(QsciLexerSQL::Keyword));
        QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_GETSTYLEAT, 7UL),
                 static_cast<long>(QsciLexerSQL::SingleQuotedString));
        QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_GETSTYLEAT, 17UL),
                 static_cast<long>(QsciLexerSQL::Number));
        QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_GETSTYLEAT, 21UL),
                 static_cast<long>(QsciLexerSQL::CommentLine));
        QCOMPARE(editor.lexer()->color(QsciLexerSQL::Keyword), QColor("#885da7"));
        QVERIFY(editor.lexer()->color(QsciLexerSQL::Keyword) !=
                editor.palette().color(QPalette::Link));
        for (const auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            manager.setMode(mode);
            manager.applyTo(editor);
            for (const auto style : {QsciLexerSQL::Keyword, QsciLexerSQL::SingleQuotedString,
                                     QsciLexerSQL::Number, QsciLexerSQL::CommentLine})
                QVERIFY(contrastRatio(editor.lexer()->color(style),
                                      editor.palette().color(QPalette::Base)) >= 4.5);
        }
        QPalette forced;
        forced.setColor(QPalette::Window, Qt::black);
        forced.setColor(QPalette::Base, Qt::black);
        forced.setColor(QPalette::WindowText, Qt::yellow);
        forced.setColor(QPalette::Text, Qt::yellow);
        manager.setSystemPalette(forced);
        manager.setForcedContrast(true);
        manager.applyTo(editor);
        for (const auto style : {QsciLexerSQL::Keyword, QsciLexerSQL::SingleQuotedString,
                                 QsciLexerSQL::Number, QsciLexerSQL::CommentLine})
            QCOMPARE(editor.lexer()->color(style), QColor(Qt::yellow));
    }
    void darkPaletteAlsoColorsTheFoldMargin() {
        using namespace choscordb::design;
        choscordb::SqlEditor editor;
        editor.resize(400, 200);
        editor.setPalette(applicationPalette(
            {ResolvedAppearance::Dark, resolveColors(ResolvedAppearance::Dark, {}), false}));
        editor.show();
        QCoreApplication::processEvents();
        const int x = editor.marginWidth(0) + editor.marginWidth(1) + editor.marginWidth(2) / 2;
        QCOMPARE(editor.grab().toImage().pixelColor(x, 160), QColor("#20272b"));
    }
    void restoresBufferAndSelectionWithoutReadingFile() {
        choscordb::SqlEditor editor;
        const QByteArray sql = QString::fromUtf8("SELECT '🦀é';\n").toUtf8();
        QSignalSpy opened(&editor, &choscordb::SqlEditor::fileOpened);
        QSignalSpy saved(&editor, &choscordb::SqlEditor::fileSaved);
        QVERIFY(editor.restoreDocument(sql, "/missing/recovered.sql", 14, 8, true));
        QCOMPARE(editor.text().toUtf8(), sql);
        QCOMPARE(editor.filePath(), QString("/missing/recovered.sql"));
        QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_GETCURRENTPOS), 14L);
        QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_GETANCHOR), 8L);
        QCOMPARE(editor.selectedText(), QString::fromUtf8("🦀é"));
        QVERIFY(editor.isModified());
        QCOMPARE(opened.count(), 0);
        QCOMPARE(saved.count(), 0);
        QVERIFY(!editor.restoreDocument(sql, "bad.sql", 9, 8, false));
        QVERIFY(!editor.restoreDocument(QByteArray::fromHex("ff"), "bad.sql", 0, 0, false));
        QCOMPARE(editor.filePath(), QString("/missing/recovered.sql"));
        QCOMPARE(editor.text().toUtf8(), sql);
        QVERIFY(editor.isModified());
    }
    void recoveryPreservesEmbeddedNullAndRejectsPendingFileIo() {
        choscordb::SqlEditor editor;
        const QByteArray sql("a\0b", 3);
        QVERIFY(editor.restoreDocument(sql, {}, 3, 0, false));
        QCOMPARE(editor.text().toUtf8(), sql);
        QVERIFY(!editor.isModified());
        QTemporaryDir dir;
        QSignalSpy saved(&editor, &choscordb::SqlEditor::fileSaved);
        editor.saveFile(dir.filePath("query.sql"));
        QVERIFY(!editor.restoreDocument("replacement", {}, 0, 0, true));
        QCOMPARE(editor.text().toUtf8(), sql);
        QTRY_COMPARE(saved.count(), 1);
        QVERIFY(saved.at(0).at(1).toString().isEmpty());
    }
    void rapidEditorReplacementKeepsCompletionUsable() {
        for (int i = 0; i < 250; ++i) {
            auto editor = std::make_unique<choscordb::SqlEditor>();
            editor->setText("SEL");
            QStringList completions;
            editor->lexer()->apis()->updateAutoCompletionList({"sel"}, completions);
            QCOMPARE(completions, QStringList{"SELECT"});
            editor.reset();
        }
        QCoreApplication::processEvents();
    }
    void fontChangePreservesDocumentUndoAndSyntaxEmphasis() {
        choscordb::SqlEditor editor;
        editor.setText("SELECT 1");
        editor.insertAt(";", 0, 8);
        const auto before = editor.text();
        const auto revision = editor.revision();
        const bool keywordBold = editor.lexer()->font(QsciLexerSQL::Keyword).bold();
        auto font = editor.font();
        font.setPointSize(24);
        editor.setEditorFont(font);
        QCOMPARE(editor.lexer()->font(QsciLexerSQL::Default).pointSize(), 24);
        QCOMPARE(editor.lexer()->font(QsciLexerSQL::Keyword).pointSize(), 24);
        QCOMPARE(editor.lexer()->font(QsciLexerSQL::Keyword).bold(), keywordBold);
        QCOMPARE(
            editor.SendScintilla(QsciScintilla::SCI_STYLEGETSIZE, QsciScintilla::STYLE_LINENUMBER),
            24L);
        QCOMPARE(editor.text(), before);
        QCOMPARE(editor.revision(), revision);
        editor.undo();
        QCOMPARE(editor.text(), QString("SELECT 1"));
    }
    void roundTripUtf8() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("query.sql");
        choscordb::SqlEditor editor;
        editor.setText(QString::fromUtf8("SELECT 'Tiếng Việt', '🦀';\n"));
        QSignalSpy saved(&editor, &choscordb::SqlEditor::fileSaved);
        editor.saveFile(path);
        QCOMPARE(saved.count(), 0); // Completion must be delivered by the event loop.
        QTRY_COMPARE(saved.count(), 1);
        QVERIFY(saved.at(0).at(1).toString().isEmpty());
        choscordb::SqlEditor reopened;
        QSignalSpy opened(&reopened, &choscordb::SqlEditor::fileOpened);
        reopened.openFile(path);
        QCOMPARE(opened.count(), 0);
        QTRY_COMPARE(opened.count(), 1);
        QVERIFY(opened.at(0).at(1).toString().isEmpty());
        QCOMPARE(reopened.text(), editor.text());
        QVERIFY(!reopened.isModified());
    }
    void staleLoadAndSavePreserveNewEdits() {
        QTemporaryDir dir;
        const auto path = dir.filePath("query.sql");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("SELECT 1;");
        file.close();
        choscordb::SqlEditor editor;
        QSignalSpy opened(&editor, &choscordb::SqlEditor::fileOpened);
        editor.openFile(path);
        editor.setText("SELECT 2;");
        editor.setModified(true);
        QTRY_COMPARE(opened.count(), 1);
        QVERIFY(!opened.at(0).at(1).toString().isEmpty());
        QCOMPARE(editor.text(), QString("SELECT 2;"));
        QVERIFY(editor.isModified());
        QSignalSpy saved(&editor, &choscordb::SqlEditor::fileSaved);
        editor.saveFile(path);
        editor.setText("SELECT 3;");
        editor.setModified(true);
        QTRY_COMPARE(saved.count(), 1);
        QVERIFY(saved.at(0).at(1).toString().isEmpty());
        QVERIFY(editor.isModified());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("SELECT 2;"));
    }
    void failedSaveLeavesDirty() {
        QTemporaryDir dir;
        choscordb::SqlEditor editor;
        editor.setText("SELECT 1;");
        editor.setModified(true);
        QSignalSpy saved(&editor, &choscordb::SqlEditor::fileSaved);
        editor.saveFile(dir.filePath("missing/query.sql"));
        QTRY_COMPARE(saved.count(), 1);
        QVERIFY(!saved.at(0).at(1).toString().isEmpty());
        QVERIFY(editor.isModified());
        QVERIFY(editor.filePath().isEmpty());
    }
    void oversizedLoadLeavesDocumentUnchanged() {
        QTemporaryDir dir;
        QFile file(dir.filePath("large.sql"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(choscordb::DocumentIo::MaximumBytes + 1));
        file.close();
        choscordb::SqlEditor editor;
        editor.setText("SELECT 42;");
        QSignalSpy opened(&editor, &choscordb::SqlEditor::fileOpened);
        editor.openFile(file.fileName());
        QTRY_COMPARE(opened.count(), 1);
        QVERIFY(!opened.at(0).at(1).toString().isEmpty());
        QCOMPARE(editor.text(), QString("SELECT 42;"));
    }
    void queuedLargeReadDoesNotBlockUiOrEditorDestruction() {
        QTemporaryDir dir;
        QFile file(dir.filePath("large.sql"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(4 * 1024 * 1024, ' '));
        file.close();
        auto* pool = QThreadPool::globalInstance();
        pool->waitForDone();
        const int previous = pool->maxThreadCount();
        pool->setMaxThreadCount(1);
        QSemaphore started, release;
        auto blocker = QtConcurrent::run([&] {
            started.release();
            release.acquire();
        });
        auto cleanup = qScopeGuard([&] {
            release.release();
            blocker.waitForFinished();
            pool->waitForDone();
            pool->setMaxThreadCount(previous);
        });
        QVERIFY(started.tryAcquire(1, 5000));
        auto editor = std::make_unique<choscordb::SqlEditor>();
        QSignalSpy opened(editor.get(), &choscordb::SqlEditor::fileOpened);
        editor->openFile(file.fileName());
        bool heartbeat = false;
        QTimer::singleShot(0, [&] { heartbeat = true; });
        QTRY_VERIFY(heartbeat);
        QCOMPARE(opened.count(), 0); // Disk worker remains queued behind blocker.
        editor.reset();              // Must not join the queued I/O worker.
    }
    void paletteKeepsTextReadable() {
        choscordb::SqlEditor editor;
        auto colors = editor.palette();
        colors.setColor(QPalette::Base, QColor("#181818"));
        colors.setColor(QPalette::Text, QColor("#eeeeee"));
        editor.setPalette(colors);
        QCOMPARE(editor.lexer()->paper(QsciLexerSQL::Default), QColor("#181818"));
        QCOMPARE(editor.lexer()->color(QsciLexerSQL::Default), QColor("#eeeeee"));
        QVERIFY(editor.lexer()->color(QsciLexerSQL::Keyword).lightness() > 128);
    }
    void invalidUtf8DoesNotReplaceDocument_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("invalid") << QByteArray::fromHex("fffe80");
        QTest::newRow("truncated") << QByteArray::fromHex("53454c45435420c3");
    }
    void invalidUtf8DoesNotReplaceDocument() {
        QFETCH(QByteArray, bytes);
        QTemporaryDir dir;
        QFile file(dir.filePath("bad.sql"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bytes);
        file.close();
        choscordb::SqlEditor editor;
        editor.setText("SELECT 42;");
        QSignalSpy opened(&editor, &choscordb::SqlEditor::fileOpened);
        editor.openFile(file.fileName());
        QTRY_COMPARE(opened.count(), 1);
        QCOMPARE(editor.text(), QString("SELECT 42;"));
        QVERIFY(!opened.at(0).at(1).toString().isEmpty());
    }
};
QTEST_MAIN(EditorTest)
#include "editor_test.moc"
