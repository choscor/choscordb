#include "bridge/completion_service.h"
#include <QtTest>
#include <future>
class CompletionServiceTest : public QObject {
    Q_OBJECT
  private slots:
    void rejectedMetadataRemainsPartialAndDoesNotSuppressKeywords() {
        choscordb::CompletionService service({{"invalid", QString(QChar(0xd800)), "table"}});
        const auto page = service.complete("sel", 3, true);
        QVERIFY(page.valid);
        QVERIFY(page.partial);
        QVERIFY(!page.items.isEmpty());
        QCOMPARE(page.items.first().insertText, QString("SELECT"));
    }
    void quotedUnicodePrefixUsesUtf8RangeAndImmutableCatalog() {
        QList<choscordb::CompletionCandidate> candidates = {
            {QString::fromUtf8("列"), QString::fromUtf8("\"模式\".\"列\""), "column"}};
        choscordb::CompletionService service(candidates);
        auto copy = service;
        candidates[0].insertText = "changed";
        service = choscordb::CompletionService();
        const auto source = QString::fromUtf8("SELECT \"模式\".列");
        const auto page = copy.complete(source, source.toUtf8().size(), true);
        QVERIFY(page.valid);
        QCOMPARE(page.start, quint64(7));
        QCOMPARE(page.end, quint64(source.toUtf8().size()));
        QCOMPARE(page.items.size(), 1);
        QCOMPARE(page.items[0].insertText, QString::fromUtf8("\"模式\".\"列\""));
        auto worker = std::async(std::launch::async, [copy, source] {
            return copy.complete(source, source.toUtf8().size(), true);
        });
        const auto concurrent = copy.complete("sel", 3, true);
        QVERIFY(concurrent.valid);
        QCOMPARE(worker.get().items.first().insertText, page.items.first().insertText);
    }
    void malformedAndOversizedInputCannotReachCompletion() {
        choscordb::CompletionService service;
        QVERIFY(!service.complete(QString(QChar(0xd800)), 0, true).valid);
        const auto bound = choscordb::CompletionService::limits().maxSourceBytes;
        QVERIFY(!service.complete(QString(qsizetype(bound + 1), QChar('a')), 0, true).valid);
        QVERIFY(!service.complete(QString::fromUtf8("é"), 1, true).valid);
    }
};
QTEST_MAIN(CompletionServiceTest)
#include "completion_service_test.moc"
