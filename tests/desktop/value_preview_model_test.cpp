#include "models/value_preview_model.h"
#include <QtTest>
using choscordb::ValuePreviewModel;
class ValuePreviewModelTest : public QObject {
    Q_OBJECT
  private slots:
    void binaryWindow() {
        ValuePreviewModel model;
        QVERIFY(model.setChunk(QByteArray::fromHex("0001feff"), 32, 36, true));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0, 0)).toULongLong(), 32ULL);
        QCOMPARE(model.data(model.index(0, 1)).toString(), QString("00 01 fe ff"));
        QCOMPARE(model.nextOffset(), 36ULL);
    }
    void utf8RowsAndChunkOverlap() {
        ValuePreviewModel model;
        const QByteArray euro = QByteArray::fromHex("e282ac");
        QByteArray bytes(255, 'a');
        bytes += euro;
        bytes += "z";
        bytes += euro.left(2);
        QVERIFY(model.setChunk(bytes, 0, 262, false));
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0, 1)).toString(), QString(255, 'a'));
        QCOMPARE(model.data(model.index(1, 0)).toULongLong(), 255ULL);
        QCOMPARE(model.data(model.index(1, 1)).toString(), QString::fromUtf8(euro) + "z");
        QCOMPARE(model.nextOffset(), 259ULL);
        QVERIFY(model.setChunk(euro, 259, 262, false));
        QCOMPARE(model.data(model.index(0, 1)).toString(), QString::fromUtf8(euro));
        QCOMPARE(model.nextOffset(), 262ULL);
        QVERIFY(model.setChunk(QByteArray::fromHex("82ac") + "x", 1, 4, false));
        QCOMPARE(model.data(model.index(0, 0)).toULongLong(), 1ULL);
        QCOMPARE(model.data(model.index(0, 1)).toString(),
                 QString("[Invalid UTF-8; hex] 82 ac 78"));
    }
    void boundedOwnershipAndInvalidRanges() {
        ValuePreviewModel model;
        QByteArray bytes(65536, 'x');
        bytes.reserve(1024 * 1024);
        QVERIFY(model.setChunk(bytes, 0, 65536, false));
        QCOMPARE(model.rowCount(), 256);
        QVERIFY(model.residentBytes() >= 65536U);
        QVERIFY(model.residentBytes() < 70000U);
        QVERIFY(!model.setChunk(QByteArray(65537, 'x'), 0, 65537, true));
        QVERIFY(!model.setChunk("x", 8, 8, false));
        QVERIFY(!model.setChunk({}, 9, 8, false));
        QCOMPARE(model.rowCount(), 256);
        QCOMPARE(model.rowCount(model.index(0, 0)), 0);
        model.clear();
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.residentBytes(), 0U);
        QVERIFY(model.setChunk({}, 8, 8, false));
        QCOMPARE(model.nextOffset(), 8ULL);
    }
    void controlsRemainVisible() {
        ValuePreviewModel model;
        const QByteArray bytes("first\nsecond\r\t\\\0\x01", 17);
        QVERIFY(model.setChunk(bytes, 0, bytes.size(), false));
        QCOMPARE(model.data(model.index(0, 1)).toString(),
                 QStringLiteral("first\\nsecond\\r\\t\\\\\\0\\u0001"));
        QCOMPARE(model.headerData(1, Qt::Horizontal).toString(),
                 QStringLiteral("Text (escaped UTF-8)"));
    }
    void invalidUtf8IsVisible() {
        ValuePreviewModel model;
        QVERIFY(model.setChunk(QByteArray::fromHex("61ff62e282"), 0, 5, false));
        const auto text = model.data(model.index(0, 1)).toString();
        QCOMPARE(text, QString("[Invalid UTF-8; hex] 61 ff 62 e2 82"));
        QCOMPARE(model.nextOffset(), 5ULL);
        QVERIFY(model.setChunk(QByteArray(65536, '\x80'), 1, 65537, false));
        QCOMPARE(model.rowCount(), 256);
        QCOMPARE(model.data(model.index(0, 0)).toULongLong(), 1ULL);
        const auto invalid = model.data(model.index(0, 1)).toString();
        QVERIFY(invalid.startsWith("[Invalid UTF-8; hex] 80 80"));
        QVERIFY(invalid.size() < 800);
        QVERIFY(model.residentBytes() < 70000U);
    }
};
QTEST_GUILESS_MAIN(ValuePreviewModelTest)
#include "value_preview_model_test.moc"
