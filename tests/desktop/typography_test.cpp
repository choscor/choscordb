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
        const auto ui = resolveTypography(TypographyRole::Ui);
        QCOMPARE(QFontInfo(ui).family(),
                 QFontInfo(QFontDatabase::systemFont(QFontDatabase::GeneralFont)).family());
        QVERIFY(QFontInfo(ui).family() != QString("Geist"));
        QFont savedSqlFont("Geist");
        savedSqlFont.setPointSize(16);
        QCOMPARE(QFontInfo(savedSqlFont).family(), QString("Geist"));
        QCOMPARE(savedSqlFont.pointSize(), 16);
    }
    void defaultMonospaceUsesControlledReferenceSize() {
        using namespace choscordb::design;
        Text text("SELECT 1;");
        text.setTypographyRole(TypographyRole::Monospace);
        QCOMPARE(text.font().pixelSize(), 13);
        QCOMPARE(QFontInfo(text.font()).family(),
                 QFontInfo(QFontDatabase::systemFont(QFontDatabase::FixedFont)).family());
    }
    void renderedBaselinesFollowTheReferenceLineHeight() {
        using namespace choscordb::design;
        for (const auto& [role, expected] :
             {std::pair{TypographyRole::Ui, 18}, std::pair{TypographyRole::Small, 16},
              std::pair{TypographyRole::Base, 22}}) {
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
        QImage previous;
        for (const auto weight : {QFont::Normal, QFont::Medium, QFont::DemiBold, QFont::Bold}) {
            text.setWeight(weight);
            QCOMPARE(QFontInfo(text.font()).family(),
                     QFontInfo(QFontDatabase::systemFont(QFontDatabase::GeneralFont)).family());
            QCOMPARE(QFontInfo(text.font()).weight(), weight);
            const auto rendered = text.grab().toImage();
            QVERIFY(rendered != previous);
            previous = rendered;
        }
    }

    void textUsesReferenceLineBoxesForEveryRole() {
        using namespace choscordb::design;
        Text text("Hello\nHello");
        QCOMPARE(text.sizeHint().height(), 36);
        text.setTypographyRole(TypographyRole::Small);
        QCOMPARE(text.sizeHint().height(), 32);
        text.setTypographyRole(TypographyRole::Heading);
        QCOMPARE(text.sizeHint().height(), 40);
        text.setTypographyRole(TypographyRole::Base);
        QCOMPARE(text.sizeHint().height(), 44);
        text.setTypographyRole(TypographyRole::DialogTitle);
        QCOMPARE(text.sizeHint().height(), 40);
        text.setTypographyRole(TypographyRole::Heading);
        text.setText("Hello");
        QCOMPARE(text.sizeHint().height(), 20);
    }
};

QTEST_MAIN(TypographyTest)
#include "typography_test.moc"
