#include "design_system/field/field.h"
#include "widgets/search_panel/search_panel.h"
#include "widgets/sql_editor/sql_editor.h"
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <QtTest>
class SearchTest : public QObject {
    Q_OBJECT
  private slots:
    void replaceAllShowsEmptySearchErrorBelowFindField() {
        QWidget parent;
        choscordb::SqlEditor editor(&parent);
        choscordb::SearchPanel panel([&] { return &editor; }, &parent);
        parent.show();
        panel.showReplace();
        editor.setText("cat");
        panel.findChild<QPushButton*>("searchReplaceAll")->click();
        auto* needle = panel.findChild<QLineEdit*>("searchNeedle");
        auto* validation =
            dynamic_cast<choscordb::design::FieldValidation*>(needle->parentWidget());
        QVERIFY(validation);
        QVERIFY(validation->error().contains("text to find"));
    }
    void replaceOneRejectsMalformedUnicode() {
        QWidget parent;
        choscordb::SqlEditor editor(&parent);
        choscordb::SearchPanel panel([&] { return &editor; }, &parent);
        parent.show();
        panel.showReplace();
        editor.setText("cat");
        panel.findChild<QLineEdit*>("searchNeedle")->setText("cat");
        panel.findNext();
        QTRY_COMPARE(editor.selectedText(), QString("cat"));
        panel.findChild<QLineEdit*>("searchReplacement")->setText(QString(QChar(0xd800)));
        panel.findChild<QPushButton*>("searchReplace")->click();
        QCOMPARE(editor.text(), QString("cat"));
        auto* replacement = panel.findChild<QLineEdit*>("searchReplacement");
        auto* validation =
            dynamic_cast<choscordb::design::FieldValidation*>(replacement->parentWidget());
        QVERIFY(validation);
        QVERIFY(validation->error().contains("Unicode"));
    }
    void pendingSearchRejectsTabRoundTripDestructionOptionsAndHide_data() {
        QTest::addColumn<int>("change");
        QTest::addColumn<bool>("replace");
        for (bool replace : {false, true}) {
            const QString prefix = replace ? "replace " : "find ";
            QTest::newRow((prefix + "tab away and back").toUtf8().constData()) << 0 << replace;
            QTest::newRow((prefix + "target destroyed").toUtf8().constData()) << 1 << replace;
            QTest::newRow((prefix + "options changed").toUtf8().constData()) << 2 << replace;
            QTest::newRow((prefix + "hidden without editing").toUtf8().constData()) << 3 << replace;
        }
    }
    void pendingSearchRejectsTabRoundTripDestructionOptionsAndHide() {
        QFETCH(int, change);
        QFETCH(bool, replace);
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
        QWidget parent;
        auto target = std::make_unique<choscordb::SqlEditor>(&parent);
        choscordb::SqlEditor other(&parent);
        choscordb::SqlEditor* current = target.get();
        choscordb::SearchPanel panel([&] { return current; }, &parent);
        parent.show();
        panel.showReplace();
        target->setText("cat cat");
        other.setText("other");
        panel.findChild<QLineEdit*>("searchNeedle")->setText("cat");
        panel.findChild<QLineEdit*>("searchReplacement")->setText("dog");
        auto* button = panel.findChild<QPushButton*>(replace ? "searchReplaceAll" : "searchNext");
        button->click();
        QVERIFY(!button->isEnabled());
        switch (change) {
        case 0:
            current = &other;
            panel.editorChanged();
            current = target.get();
            panel.editorChanged();
            break;
        case 1:
            current = &other;
            target.reset();
            break;
        case 2:
            panel.findChild<QCheckBox*>("searchCase")->setChecked(true);
            break;
        case 3:
            panel.hide();
            break;
        }
        release.release();
        QTRY_VERIFY(button->isEnabled());
        if (target) {
            QCOMPARE(target->text(), QString("cat cat"));
            QVERIFY(target->selectedText().isEmpty());
        }
        QCOMPARE(other.text(), QString("other"));
        QVERIFY(panel.findChild<QLabel*>("searchStatus")->text().contains("discarded"));
    }
    void staleFindAndReadOnlyEditorDoNotChangeSelectionOrText() {
        QWidget parent;
        choscordb::SqlEditor editor(&parent);
        choscordb::SearchPanel panel([&] { return &editor; }, &parent);
        parent.show();
        panel.showReplace();
        editor.setText("cat cat");
        panel.findChild<QLineEdit*>("searchNeedle")->setText("cat");
        auto* next = panel.findChild<QPushButton*>("searchNext");
        panel.findNext();
        editor.SendScintilla(QsciScintilla::SCI_SETSEL, 1, 1);
        QTRY_VERIFY(next->isEnabled());
        QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_GETCURRENTPOS), 1L);
        QVERIFY(editor.selectedText().isEmpty());
        editor.setReadOnly(true);
        editor.SendScintilla(QsciScintilla::SCI_SETSEL, 0UL, 0L);
        panel.findNext();
        QTRY_COMPARE(editor.selectedText(), QString("cat"));
        panel.findChild<QLineEdit*>("searchReplacement")->setText("dog");
        panel.findChild<QPushButton*>("searchReplaceAll")->click();
        QCOMPARE(editor.text(), QString("cat cat"));
    }

    void findAndReplaceRespectUnicodeAndUndo() {
        QWidget parent;
        choscordb::SqlEditor editor(&parent);
        choscordb::SearchPanel panel([&] { return &editor; }, &parent);
        parent.show();
        panel.showReplace();
        editor.setText(QString::fromUtf8("é cat cat"));
        editor.SendScintilla(QsciScintilla::SCI_SETSEL, 0UL, 0L);
        panel.findChild<QLineEdit*>("searchNeedle")->setText("cat");
        panel.findNext();
        QTRY_COMPARE(editor.selectedText(), QString("cat"));
        QCOMPARE(editor.SendScintilla(QsciScintilla::SCI_GETSELECTIONSTART), 3L);
        panel.findChild<QLineEdit*>("searchReplacement")->setText(QString::fromUtf8("犬"));
        panel.findChild<QPushButton*>("searchReplace")->click();
        QCOMPARE(editor.text(), QString::fromUtf8("é 犬 cat"));
        editor.undo();
        QCOMPARE(editor.text(), QString::fromUtf8("é cat cat"));
    }
    void replaceAllIsOneUndoAndStaleSnapshotCannotApply() {
        QWidget parent;
        choscordb::SqlEditor editor(&parent);
        choscordb::SearchPanel panel([&] { return &editor; }, &parent);
        parent.show();
        panel.showReplace();
        editor.setText("cat cat cat");
        panel.findChild<QLineEdit*>("searchNeedle")->setText("cat");
        panel.findChild<QLineEdit*>("searchReplacement")->setText("dog");
        auto* replace = panel.findChild<QPushButton*>("searchReplaceAll");
        replace->click();
        QTRY_COMPARE(editor.text(), QString("dog dog dog"));
        editor.undo();
        QCOMPARE(editor.text(), QString("cat cat cat"));
        replace->click();
        editor.setText("new edit");
        QTRY_VERIFY(replace->isEnabled());
        QCOMPARE(editor.text(), QString("new edit"));
        replace->click();
        panel.hide();
        editor.setText("hidden edit");
        QTRY_VERIFY(replace->isEnabled());
        QCOMPARE(editor.text(), QString("hidden edit"));
    }
};
QTEST_MAIN(SearchTest)
#include "search_test.moc"
