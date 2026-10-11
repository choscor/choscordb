#include "design_system/text/text.h"

#include <QAccessible>
#include <QFontInfo>
#include <QtTest>

class TypographyTest final : public QObject {
    Q_OBJECT
  private slots:
    void uiResolvesTheBundledGeistFamilies() {
        using namespace choscordb::design;
        // Run first (or by this slot name in a fresh process). No test/helper
        // registers bundled fonts: resolve the UI exactly as startup does.
        const auto ui = resolveTypography(TextStyle::Body);
        QVERIFY(bundledFontsAvailable());
        QCOMPARE(QFontInfo(ui).family(), QString("Geist"));
        QCOMPARE(QFontInfo(resolveTypography(TextStyle::Body, FontDesign::Monospaced)).family(),
                 QString("Geist Mono"));
        QVERIFY(QFontInfo(resolveTypography(TextStyle::Body, FontDesign::Monospaced)).fixedPitch());
        QFont savedSqlFont("Geist");
        savedSqlFont.setPointSize(16);
        QCOMPARE(QFontInfo(savedSqlFont).family(), QString("Geist"));
        QCOMPARE(savedSqlFont.pointSize(), 16);
    }
    void stylesUseTheGeistScale() {
        using namespace choscordb::design;
        // Eight styles, Large Title through Caption, with a 12px minimum.
        QCOMPARE(static_cast<int>(TextStyle::LargeTitle), 0);
        QCOMPARE(static_cast<int>(TextStyle::Caption), 7);
        struct Expected {
            TextStyle style;
            int size;
            int lineHeight;
            QFont::Weight weight;
        };
        for (const auto& expected : {Expected{TextStyle::LargeTitle, 28, 36, QFont::DemiBold},
                                     Expected{TextStyle::Title1, 22, 28, QFont::DemiBold},
                                     Expected{TextStyle::Title2, 18, 24, QFont::DemiBold},
                                     Expected{TextStyle::Title3, 16, 24, QFont::DemiBold},
                                     Expected{TextStyle::Headline, 14, 20, QFont::DemiBold},
                                     Expected{TextStyle::Body, 14, 20, QFont::Normal},
                                     Expected{TextStyle::Callout, 13, 18, QFont::Normal},
                                     Expected{TextStyle::Caption, 12, 16, QFont::Medium}}) {
            for (const auto design : {FontDesign::Default, FontDesign::Monospaced}) {
                const auto spec = typographySpec(expected.style, design);
                QCOMPARE(spec.pixelSize, expected.size);
                QCOMPARE(spec.lineHeight, expected.lineHeight);
                QCOMPARE(spec.weight, expected.weight);
                const auto family =
                    design == FontDesign::Monospaced ? QString("Geist Mono") : QString("Geist");
                QCOMPARE(spec.family, family);
                const auto font = resolveTypography(expected.style, design);
                QCOMPARE(font.family(), family);
                QCOMPARE(font.pixelSize(), expected.size);
                QCOMPARE(font.weight(), expected.weight);
                QCOMPARE(font.letterSpacing(), 0.0);
                QCOMPARE(font.hintingPreference(), QFont::PreferVerticalHinting);
            }
        }
    }
    void defaultMonospaceUsesControlledReferenceSize() {
        using namespace choscordb::design;
        Text text("SELECT 1;");
        text.setTextStyle(TextStyle::Body, FontDesign::Monospaced);
        QCOMPARE(text.font().pixelSize(), 14);
        QCOMPARE(QFontInfo(text.font()).family(), QString("Geist Mono"));
    }
    void renderedBaselinesFollowTheReferenceLineHeight() {
        using namespace choscordb::design;
        for (const auto& [style, expected] :
             {std::pair{TextStyle::Body, 20}, std::pair{TextStyle::Callout, 18},
              std::pair{TextStyle::Title3, 24}}) {
            Text text("H\nH");
            text.setTextStyle(style);
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
        text.setTextStyle(TextStyle::Callout);
        QCOMPARE(text.heightForWidth(35), 36);
        text.setText(QString::fromUtf8("Tiếng Việt — 日本語 — العربية"));
        auto* accessible = QAccessible::queryAccessibleInterface(&text);
        QVERIFY(accessible != nullptr);
        QCOMPARE(accessible->role(), QAccessible::StaticText);
        QCOMPARE(accessible->text(QAccessible::Name), text.text());
        QCOMPARE(text.textInteractionFlags(), Qt::NoTextInteraction);
        QVERIFY(text.heightForWidth(70) > text.heightForWidth(400));
        QCOMPARE(text.heightForWidth(70) % 18, 0);
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
        text.show();
        QCoreApplication::processEvents();
        QImage normal;
        for (const auto weight : {QFont::Normal, QFont::Medium, QFont::DemiBold, QFont::Bold}) {
            text.setWeight(weight);
            QCOMPARE(QFontInfo(text.font()).family(), QString("Geist"));
            QCOMPARE(QFontInfo(text.font()).weight(), weight);
            QCoreApplication::processEvents();
            if (weight == QFont::Normal)
                normal = text.grab().toImage();
        }
        QVERIFY(text.grab().toImage() != normal);
    }

    void textUsesReferenceLineBoxesForEveryStyle() {
        using namespace choscordb::design;
        Text text("Hello\nHello");
        QCOMPARE(text.sizeHint().height(), 40);
        text.setTextStyle(TextStyle::Callout);
        QCOMPARE(text.sizeHint().height(), 36);
        text.setTextStyle(TextStyle::Headline);
        QCOMPARE(text.sizeHint().height(), 40);
        text.setTextStyle(TextStyle::Caption);
        QCOMPARE(text.sizeHint().height(), 32);
        text.setTextStyle(TextStyle::Body, FontDesign::Monospaced);
        QCOMPARE(text.sizeHint().height(), 40);
        text.setTextStyle(TextStyle::LargeTitle);
        QCOMPARE(text.sizeHint().height(), 72);
        text.setText("Hello");
        QCOMPARE(text.sizeHint().height(), 36);
    }
};

QTEST_MAIN(TypographyTest)
#include "typography_test.moc"
