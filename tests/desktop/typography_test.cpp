#include "design_system/text/text.h"

#include <QAccessible>
#include <QFontDatabase>
#include <QFontInfo>
#include <QtTest>

class TypographyTest final : public QObject {
    Q_OBJECT
  private slots:
    void savedLegacySqlFontResolvesWithoutChangingPlatformUi() {
        using namespace choscordb::design;
        // Run first (or by this slot name in a fresh process). No test/helper
        // registers bundled fonts: resolve the UI exactly as startup does.
        const auto ui = resolveTypography(TextStyle::Body);
        QCOMPARE(QFontInfo(ui).family(),
                 QFontInfo(QFontDatabase::systemFont(QFontDatabase::GeneralFont)).family());
        QFont savedSqlFont("Geist");
        savedSqlFont.setPointSize(16);
        QCOMPARE(QFontInfo(savedSqlFont).family(), QString("Geist"));
        QCOMPARE(savedSqlFont.pointSize(), 16);
    }
    void stylesFollowTheMacosBuiltInTextStyles() {
        using namespace choscordb::design;
        // Apple's eleven macOS built-in text styles, Large Title through Caption 2.
        QCOMPARE(static_cast<int>(TextStyle::LargeTitle), 0);
        QCOMPARE(static_cast<int>(TextStyle::Caption2), 10);
        struct Expected {
            TextStyle style;
            int size;
            int lineHeight;
            QFont::Weight weight;
        };
        for (const auto& expected : {Expected{TextStyle::LargeTitle, 26, 32, QFont::Normal},
                                     Expected{TextStyle::Title1, 22, 26, QFont::Normal},
                                     Expected{TextStyle::Title2, 17, 22, QFont::Normal},
                                     Expected{TextStyle::Title3, 15, 20, QFont::Normal},
                                     Expected{TextStyle::Headline, 13, 16, QFont::Bold},
                                     Expected{TextStyle::Body, 13, 16, QFont::Normal},
                                     Expected{TextStyle::Callout, 12, 15, QFont::Normal},
                                     Expected{TextStyle::Subheadline, 11, 14, QFont::Normal},
                                     Expected{TextStyle::Footnote, 10, 13, QFont::Normal},
                                     Expected{TextStyle::Caption1, 10, 13, QFont::Normal},
                                     Expected{TextStyle::Caption2, 10, 13, QFont::Medium}}) {
            for (const auto design : {FontDesign::Default, FontDesign::Monospaced}) {
                const auto spec = typographySpec(expected.style, design);
                QCOMPARE(spec.pixelSize, expected.size);
                QCOMPARE(spec.lineHeight, expected.lineHeight);
                QCOMPARE(spec.weight, expected.weight);
                const auto platform = QFontDatabase::systemFont(
                    design == FontDesign::Monospaced ? QFontDatabase::FixedFont
                                                     : QFontDatabase::GeneralFont);
                QCOMPARE(spec.family, platform.family());
                const auto font = resolveTypography(expected.style, design);
                QCOMPARE(font.family(), platform.family());
                QCOMPARE(font.pixelSize(), expected.size);
                QCOMPARE(font.weight(), expected.weight);
                // The platform font tracks itself per size.
                QCOMPARE(font.letterSpacing(), 0.0);
            }
        }
    }
    void defaultMonospaceUsesControlledReferenceSize() {
        using namespace choscordb::design;
        Text text("SELECT 1;");
        text.setTextStyle(TextStyle::Body, FontDesign::Monospaced);
        QCOMPARE(text.font().pixelSize(), 13);
        QCOMPARE(QFontInfo(text.font()).family(),
                 QFontInfo(QFontDatabase::systemFont(QFontDatabase::FixedFont)).family());
    }
    void renderedBaselinesFollowTheReferenceLineHeight() {
        using namespace choscordb::design;
        for (const auto& [style, expected] :
             {std::pair{TextStyle::Body, 16}, std::pair{TextStyle::Subheadline, 14},
              std::pair{TextStyle::Title3, 20}}) {
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
        QCOMPARE(text.heightForWidth(100), 16);
        QCOMPARE(text.heightForWidth(40), 32);
        text.setTextStyle(TextStyle::Subheadline);
        QCOMPARE(text.heightForWidth(35), 28);
        text.setText(QString::fromUtf8("Tiếng Việt — 日本語 — العربية"));
        auto* accessible = QAccessible::queryAccessibleInterface(&text);
        QVERIFY(accessible != nullptr);
        QCOMPARE(accessible->role(), QAccessible::StaticText);
        QCOMPARE(accessible->text(QAccessible::Name), text.text());
        QCOMPARE(text.textInteractionFlags(), Qt::NoTextInteraction);
        QVERIFY(text.heightForWidth(70) > text.heightForWidth(400));
        QCOMPARE(text.heightForWidth(70) % 14, 0);
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
            QCOMPARE(QFontInfo(text.font()).family(),
                     QFontInfo(QFontDatabase::systemFont(QFontDatabase::GeneralFont)).family());
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
        QCOMPARE(text.sizeHint().height(), 32);
        text.setTextStyle(TextStyle::Subheadline);
        QCOMPARE(text.sizeHint().height(), 28);
        text.setTextStyle(TextStyle::Headline);
        QCOMPARE(text.sizeHint().height(), 32);
        text.setTextStyle(TextStyle::Callout);
        QCOMPARE(text.sizeHint().height(), 30);
        text.setTextStyle(TextStyle::Caption2);
        QCOMPARE(text.sizeHint().height(), 26);
        text.setTextStyle(TextStyle::Body, FontDesign::Monospaced);
        QCOMPARE(text.sizeHint().height(), 32);
        text.setTextStyle(TextStyle::LargeTitle);
        QCOMPARE(text.sizeHint().height(), 64);
        text.setText("Hello");
        QCOMPARE(text.sizeHint().height(), 32);
    }
};

QTEST_MAIN(TypographyTest)
#include "typography_test.moc"
