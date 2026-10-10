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
        const auto ui = resolveTypography(TypographyRole::Body);
        QCOMPARE(QFontInfo(ui).family(),
                 QFontInfo(QFontDatabase::systemFont(QFontDatabase::GeneralFont)).family());
        QFont savedSqlFont("Geist");
        savedSqlFont.setPointSize(16);
        QCOMPARE(QFontInfo(savedSqlFont).family(), QString("Geist"));
        QCOMPARE(savedSqlFont.pointSize(), 16);
    }
    void rolesFollowTheCompactTypeScale() {
        using namespace choscordb::design;
        // Exactly seven roles, Caption through Metadata.
        QCOMPARE(static_cast<int>(TypographyRole::Caption), 0);
        QCOMPARE(static_cast<int>(TypographyRole::Metadata), 6);
        struct Expected {
            TypographyRole role;
            int size;
            int lineHeight;
            QFont::Weight weight;
            double letterSpacing;
            bool fixed;
        };
        for (const auto& expected :
             {Expected{TypographyRole::Caption, 10, 14, QFont::DemiBold, 1.3, false},
              Expected{TypographyRole::Small, 11, 16, QFont::Normal, 0, false},
              Expected{TypographyRole::Dense, 12, 16, QFont::Normal, 0, false},
              Expected{TypographyRole::Body, 13, 18, QFont::Normal, -0.12, false},
              Expected{TypographyRole::Title, 14, 20, QFont::DemiBold, -0.12, false},
              Expected{TypographyRole::Mono, 13, 20, QFont::Normal, 0, true},
              Expected{TypographyRole::Metadata, 12, 16, QFont::Normal, 0, true}}) {
            const auto spec = typographySpec(expected.role);
            QCOMPARE(spec.pixelSize, expected.size);
            QCOMPARE(spec.lineHeight, expected.lineHeight);
            QCOMPARE(spec.weight, expected.weight);
            QCOMPARE(spec.family,
                     QFontDatabase::systemFont(expected.fixed ? QFontDatabase::FixedFont
                                                              : QFontDatabase::GeneralFont)
                         .family());
            const auto font = resolveTypography(expected.role);
            QCOMPARE(font.pixelSize(), expected.size);
            // Qt stores letter spacing in 1/64 px steps.
            if (!expected.fixed)
                QVERIFY2(qAbs(font.letterSpacing() - expected.letterSpacing) <= 1.0 / 64,
                         qPrintable(QString::number(font.letterSpacing())));
        }
    }
    void defaultMonospaceUsesControlledReferenceSize() {
        using namespace choscordb::design;
        Text text("SELECT 1;");
        text.setTypographyRole(TypographyRole::Mono);
        QCOMPARE(text.font().pixelSize(), 13);
        QCOMPARE(QFontInfo(text.font()).family(),
                 QFontInfo(QFontDatabase::systemFont(QFontDatabase::FixedFont)).family());
    }
    void renderedBaselinesFollowTheReferenceLineHeight() {
        using namespace choscordb::design;
        for (const auto& [role, expected] :
             {std::pair{TypographyRole::Body, 18}, std::pair{TypographyRole::Small, 16},
              std::pair{TypographyRole::Title, 20}}) {
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
        QCOMPARE(text.heightForWidth(100), 18);
        QCOMPARE(text.heightForWidth(40), 36);
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

    void textUsesReferenceLineBoxesForEveryRole() {
        using namespace choscordb::design;
        Text text("Hello\nHello");
        QCOMPARE(text.sizeHint().height(), 36);
        text.setTypographyRole(TypographyRole::Small);
        QCOMPARE(text.sizeHint().height(), 32);
        text.setTypographyRole(TypographyRole::Title);
        QCOMPARE(text.sizeHint().height(), 40);
        text.setTypographyRole(TypographyRole::Dense);
        QCOMPARE(text.sizeHint().height(), 32);
        text.setTypographyRole(TypographyRole::Caption);
        QCOMPARE(text.sizeHint().height(), 28);
        text.setTypographyRole(TypographyRole::Mono);
        QCOMPARE(text.sizeHint().height(), 40);
        text.setTypographyRole(TypographyRole::Title);
        text.setText("Hello");
        QCOMPARE(text.sizeHint().height(), 20);
    }
};

QTEST_MAIN(TypographyTest)
#include "typography_test.moc"
