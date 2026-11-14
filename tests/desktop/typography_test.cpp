#include "design_system/text/text.h"

#include <QAccessible>
#include <QFontDatabase>
#include <QFontInfo>
#include <QtTest>

class TypographyTest final : public QObject {
    Q_OBJECT
  private slots:
    void uiResolvesInterWithTheKumoSpacingAndFeatures() {
        using namespace choscordb::design;
        QVERIFY(bundledFontsAvailable());
        for (const auto style :
             {TextStyle::Headline, TextStyle::Body, TextStyle::Callout, TextStyle::Caption}) {
            const auto ui = resolveTypography(style);
            QCOMPARE(QFontInfo(ui).family(), QString("Inter"));
            QCOMPARE(ui.letterSpacingType(), QFont::AbsoluteSpacing);
            constexpr qreal qtLetterSpacingStep = 1.0 / 64;
            QVERIFY(ui.letterSpacing() < 0);
            QVERIFY(qAbs(ui.letterSpacing() + 0.01 * ui.pixelSize()) < qtLetterSpacingStep);
            for (const auto tag :
                 {QFont::Tag("cv02"), QFont::Tag("cv03"), QFont::Tag("cv04"), QFont::Tag("calt")}) {
                QVERIFY2(ui.isFeatureSet(tag), qPrintable(tag.toString()));
                QCOMPARE(ui.featureValue(tag), 1U);
            }
        }
        QFont savedSqlFont("Courier New");
        savedSqlFont.setPointSize(16);
        QCOMPARE(savedSqlFont.family(), QString("Courier New"));
        QCOMPARE(savedSqlFont.pointSize(), 16);
    }
    void monospaceResolvesTheFirstInstalledFamilyOfTheKumoStack() {
        using namespace choscordb::design;
        const QStringList kumoStack{"SF Mono",  "SFMono-Regular",  "Menlo",      "Monaco",
                                    "Consolas", "Liberation Mono", "Courier New"};
        QString expected = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
        for (const auto& family : kumoStack) {
            if (QFontDatabase::hasFamily(family)) {
                expected = family;
                break;
            }
        }
        const auto mono = resolveTypography(TextStyle::Body, FontDesign::Monospaced);
        QCOMPARE(mono.family(), expected);
        QCOMPARE(typographySpec(TextStyle::Body, FontDesign::Monospaced).family, expected);
        QVERIFY(QFontInfo(mono).fixedPitch());
        QCOMPARE(mono.letterSpacing(), 0.0);
        QVERIFY(!mono.isFeatureSet(QFont::Tag("cv02")));
        QVERIFY(!QFontDatabase::hasFamily("Geist") && !QFontDatabase::hasFamily("Geist Mono"));
    }
    void stylesUseTheKumoScale() {
        using namespace choscordb::design;
        QCOMPARE(static_cast<int>(TextStyle::Headline), 0);
        QCOMPARE(static_cast<int>(TextStyle::Caption), 3);
        struct Expected {
            TextStyle style;
            int size;
            int lineHeight;
            QFont::Weight weight;
        };
        for (const auto& expected : {Expected{TextStyle::Headline, 14, 21, QFont::DemiBold},
                                     Expected{TextStyle::Body, 14, 21, QFont::Normal},
                                     Expected{TextStyle::Callout, 13, 15, QFont::Normal},
                                     Expected{TextStyle::Caption, 12, 16, QFont::Medium}}) {
            for (const auto design : {FontDesign::Default, FontDesign::Monospaced}) {
                const auto spec = typographySpec(expected.style, design);
                QCOMPARE(spec.pixelSize, expected.size);
                QCOMPARE(spec.lineHeight, expected.lineHeight);
                QCOMPARE(spec.weight, expected.weight);
                const auto font = resolveTypography(expected.style, design);
                QCOMPARE(font.family(), spec.family);
                QCOMPARE(font.pixelSize(), expected.size);
                QCOMPARE(font.weight(), expected.weight);
                QCOMPARE(font.hintingPreference(), QFont::PreferVerticalHinting);
            }
            QCOMPARE(typographySpec(expected.style).family, QString("Inter"));
        }
    }
    void defaultMonospaceUsesControlledReferenceSize() {
        using namespace choscordb::design;
        Text text("SELECT 1;");
        text.setTextStyle(TextStyle::Body, FontDesign::Monospaced);
        QCOMPARE(text.font().pixelSize(), 14);
        QVERIFY(QFontInfo(text.font()).fixedPitch());
    }
    void renderedBaselinesFollowTheReferenceLineHeight() {
        using namespace choscordb::design;
        for (const auto& [style, expected] :
             {std::pair{TextStyle::Body, 21}, std::pair{TextStyle::Callout, 15},
              std::pair{TextStyle::Caption, 16}}) {
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
        QCOMPARE(text.heightForWidth(100), 21);
        QCOMPARE(text.heightForWidth(40), 42);
        text.setTextStyle(TextStyle::Callout);
        QCOMPARE(text.heightForWidth(35), 30);
        text.setText(QString::fromUtf8("Tiếng Việt — 日本語 — العربية"));
        auto* accessible = QAccessible::queryAccessibleInterface(&text);
        QVERIFY(accessible != nullptr);
        QCOMPARE(accessible->role(), QAccessible::StaticText);
        QCOMPARE(accessible->text(QAccessible::Name), text.text());
        QCOMPARE(text.textInteractionFlags(), Qt::NoTextInteraction);
        QVERIFY(text.heightForWidth(70) > text.heightForWidth(400));
        QCOMPARE(text.heightForWidth(70) % 15, 0);
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
            QCOMPARE(QFontInfo(text.font()).family(), QString("Inter"));
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
        QCOMPARE(text.sizeHint().height(), 42);
        text.setTextStyle(TextStyle::Callout);
        QCOMPARE(text.sizeHint().height(), 30);
        text.setTextStyle(TextStyle::Headline);
        QCOMPARE(text.sizeHint().height(), 42);
        text.setTextStyle(TextStyle::Caption);
        QCOMPARE(text.sizeHint().height(), 32);
        text.setTextStyle(TextStyle::Body, FontDesign::Monospaced);
        QCOMPARE(text.sizeHint().height(), 42);
        text.setText("Hello");
        QCOMPARE(text.sizeHint().height(), 21);
    }
};

QTEST_MAIN(TypographyTest)
#include "typography_test.moc"
