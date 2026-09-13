#include "widgets/editor_completion/editor_completion.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QAbstractItemView>
#include <QCompleter>
#include <QFutureWatcher>
#include <QLineEdit>
#include <QPointer>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <QtTest>

namespace {
bool idle(choscordb::EditorCompletionController* controller) {
    if (!controller->findChildren<QFutureWatcherBase*>().isEmpty())
        return false;
    for (auto* timer : controller->findChildren<QTimer*>())
        if (timer->isActive())
            return false;
    return true;
}
} // namespace
class EditorCompletionLifecycleTest : public QObject {
    Q_OBJECT
  private slots:
    void keywordControlShowsSuggestionAndEscapeDoesNotEdit() {
        choscordb::SqlEditor editor;
        choscordb::EditorCompletionController controller;
        editor.setText("sel");
        editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, 3L);
        controller.setEditor(&editor);
        editor.show();
        editor.activateWindow();
        editor.setFocus();
        QTRY_VERIFY(editor.hasFocus());
        auto* popup = controller.findChild<QCompleter*>()->popup();
        controller.requestCompletion();
        QTRY_VERIFY(popup->isVisible());
        QVERIFY(popup->model()->index(0, 0).data().toString().contains("SELECT"));
        QTest::keyClick(popup, Qt::Key_Escape);
        QTRY_VERIFY(!popup->isVisible());
        QCOMPARE(editor.text(), QString("sel"));
    }
    void delayedSuggestionCannotSurviveInvalidation_data() {
        QTest::addColumn<int>("change");
        QTest::newRow("tab away and back") << 0;
        QTest::newRow("text edit") << 1;
        QTest::newRow("cursor move") << 2;
        QTest::newRow("selection changed") << 3;
        QTest::newRow("catalog refreshed") << 4;
        QTest::newRow("editor destroyed") << 5;
        QTest::newRow("controller destroyed") << 6;
        QTest::newRow("Escape before popup") << 7;
    }
    void delayedSuggestionCannotSurviveInvalidation() {
        QFETCH(int, change);
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
        QWidget window;
        auto* layout = new QVBoxLayout(&window);
        auto editor = std::make_unique<choscordb::SqlEditor>(&window);
        choscordb::SqlEditor other(&window);
        layout->addWidget(editor.get());
        layout->addWidget(&other);
        auto controller = std::make_unique<choscordb::EditorCompletionController>();
        editor->setText("sel");
        editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 3L);
        controller->setEditor(editor.get());
        window.show();
        window.activateWindow();
        editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());
        QPointer<QAbstractItemView> popup = controller->findChild<QCompleter*>()->popup();
        QCOMPARE(popup->objectName(), QString("sqlCompletionPopup"));
        controller->requestCompletion();
        QTRY_COMPARE(controller->findChildren<QFutureWatcherBase*>().size(), 1);
        QVERIFY(!popup->isVisible()); // Worker is queued behind the semaphore holder.
        switch (change) {
        case 0:
            controller->setEditor(&other);
            controller->setEditor(editor.get());
            editor->setFocus();
            break;
        case 1:
            editor->setText("zzzz_no_matching_identifier");
            editor->SendScintilla(QsciScintilla::SCI_GOTOPOS,
                                  editor->SendScintilla(QsciScintilla::SCI_GETLENGTH));
            break;
        case 2:
            editor->SendScintilla(QsciScintilla::SCI_GOTOPOS, 1L);
            break;
        case 3:
            editor->SendScintilla(QsciScintilla::SCI_SETSEL, 0UL, 3L);
            break;
        case 4:
            controller->setCatalog(choscordb::CompletionService({}, true));
            break;
        case 5:
            editor.reset();
            break;
        case 6:
            controller.reset();
            break;
        case 7:
            QTest::keyClick(editor.get(), Qt::Key_Escape);
            break;
        }
        release.release();
        blocker.waitForFinished();
        pool->waitForDone();
        if (controller)
            QTRY_VERIFY(idle(controller.get()));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        QVERIFY(!popup || !popup->isVisible());
        if (editor && change != 1)
            QCOMPARE(editor->text(), QString("sel"));
    }
    void emptyAndQuotedContextsSuppressAutomaticSuggestions_data() {
        QTest::addColumn<QString>("sql");
        QTest::newRow("empty") << QString{};
        QTest::newRow("string literal") << QString("SELECT 'sel");
        QTest::newRow("comment") << QString("-- sel");
    }
    void emptyAndQuotedContextsSuppressAutomaticSuggestions() {
        QFETCH(QString, sql);
        choscordb::SqlEditor editor;
        choscordb::EditorCompletionController controller;
        editor.setText(sql);
        editor.SendScintilla(QsciScintilla::SCI_GOTOPOS, long(sql.toUtf8().size()));
        controller.setEditor(&editor);
        editor.show();
        editor.activateWindow();
        editor.setFocus();
        QTRY_VERIFY(editor.hasFocus());
        controller.requestCompletion(false);
        QTRY_VERIFY(idle(&controller));
        QVERIFY(!controller.findChild<QCompleter*>()->popup()->isVisible());
        QCOMPARE(editor.text(), sql);
    }
};
QTEST_MAIN(EditorCompletionLifecycleTest)
#include "editor_completion_lifecycle_test.moc"
