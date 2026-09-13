#include "design_system/theme.h"
#include "widgets/sql_editor.h"
#include <QFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThreadPool>
#include <Qsci/qsciabstractapis.h>
#include <Qsci/qscilexersql.h>
#include <QtConcurrentRun>
#include <QtTest>
class EditorTest : public QObject {
    Q_OBJECT
  private slots:
    void darkPaletteAlsoColorsTheFoldMargin() {
        using namespace choscordb::design;
        choscordb::SqlEditor editor;
        editor.resize(400, 200);
        editor.setPalette(applicationPalette(
            {ResolvedAppearance::Dark, resolveColors(ResolvedAppearance::Dark, {}), false}));
        editor.show();
        QCoreApplication::processEvents();
        const int x = editor.marginWidth(0) + editor.marginWidth(1) + editor.marginWidth(2) / 2;
        QCOMPARE(editor.grab().toImage().pixelColor(x, 160), QColor("#171717"));
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
