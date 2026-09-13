#include "design_system/typography.h"

#include <QAccessible>
#include <QFontInfo>
#include <QtTest>

class TypographyTest final : public QObject {
    Q_OBJECT
  private slots:
    void renderedBaselinesFollowTheReferenceLineHeight() {
        using namespace choscordb::design;
        for (const auto& [role, expected] :
             {std::pair{TypographyRole::Ui, 20}, std::pair{TypographyRole::Small, 16},
              std::pair{TypographyRole::Base, 24}}) {
            Text text("H\nH");
            text.setTypographyRole(role);
            auto palette = text.palette();
            palette.setColor(QPalette::Window, Qt::white);
            palette.setColor(QPalette::WindowText, Qt::black);
            text.setPalette(palette);
            text.setAutoFillBackground(true);
            text.resize(100, text.sizeHint().height());
            const auto pixmap = text.grab();
            const auto image = pixmap.toImage();
            QList<int> starts;
            bool previous = false;
            for (int y = 0; y < image.height(); ++y) {
                bool ink = false;
                for (int x = 0; x < image.width(); ++x) {
                    ink = ink || image.pixelColor(x, y).red() < 100;
                }
                if (ink && !previous) {
                    starts.append(y);
                }
                previous = ink;
            }
            QCOMPARE(starts.size(), 2);
            QCOMPARE(qRound((starts.at(1) - starts.at(0)) / pixmap.devicePixelRatio()), expected);
        }
    }

    void narrowWrappingPreservesPlainUnicodeAndAccessibility() {
        using namespace choscordb::design;
        Text text("Hello Hello");
        text.setWordWrap(true);
        QVERIFY(text.hasHeightForWidth());
        QCOMPARE(text.heightForWidth(100), 20);
        QCOMPARE(text.heightForWidth(40), 40);
        text.setTypographyRole(TypographyRole::Small);
        QCOMPARE(text.heightForWidth(35), 32);
        text.setText(QString::fromUtf8("Tiếng Việt — 日本語 — العربية"));
        auto* accessible = QAccessible::queryAccessibleInterface(&text);
        QVERIFY(accessible != nullptr);
        QCOMPARE(accessible->role(), QAccessible::StaticText);
        QCOMPARE(accessible->text(QAccessible::Name), text.text());
        QCOMPARE(text.textInteractionFlags(), Qt::NoTextInteraction);
        QVERIFY(text.heightForWidth(70) > text.heightForWidth(400));
        QCOMPARE(text.heightForWidth(70) % 16, 0);
        text.resize(70, text.heightForWidth(70));
    }

    void wrappedMinimumWidthFitsAnUnbreakableGlyph() {
        using namespace choscordb::design;
        Text text(QString::fromUtf8("界界界"));
        text.setWordWrap(true);
        QVERIFY(text.minimumSizeHint().width() >=
                QFontMetrics(text.font()).horizontalAdvance(QString::fromUtf8("界")));
    }

    void referenceWeightsResolveRealFontsAndChangeRenderedText() {
        using namespace choscordb::design;
        Text text("Database");
        text.resize(200, 40);
        QImage previous;
        for (const auto weight : {QFont::Normal, QFont::Medium, QFont::DemiBold, QFont::Bold}) {
            text.setWeight(weight);
            QCOMPARE(QFontInfo(text.font()).family(), QString("Geist"));
            QCOMPARE(QFontInfo(text.font()).weight(), weight);
            const auto rendered = text.grab().toImage();
            QVERIFY(rendered != previous);
            previous = rendered;
        }
    }

    void textUsesReferenceLineBoxesForEveryRole() {
        using namespace choscordb::design;
        Text text("Hello\nHello");
        QCOMPARE(text.sizeHint().height(), 40);
        text.setTypographyRole(TypographyRole::Small);
        QCOMPARE(text.sizeHint().height(), 32);
        text.setTypographyRole(TypographyRole::Heading);
        QCOMPARE(text.sizeHint().height(), 44);
        text.setTypographyRole(TypographyRole::Base);
        QCOMPARE(text.sizeHint().height(), 48);
        text.setTypographyRole(TypographyRole::DialogTitle);
        QCOMPARE(text.sizeHint().height(), 32);
        text.setTypographyRole(TypographyRole::Heading);
        text.setText("Hello");
        QCOMPARE(text.sizeHint().height(), 22);
    }
};

QTEST_MAIN(TypographyTest)
#include "typography_test.moc"
