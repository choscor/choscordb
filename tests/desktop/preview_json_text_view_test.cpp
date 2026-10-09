#include "design_system/fonts/fonts.h"
#include "design_system/right_sheet/right_sheet.h"
#include "design_system/theme.h"
#include "preview_test.h"
#include "preview_test_helpers.h"
#include "tools/preview/preview_window.h"
#include <QApplication>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QTextLayout>
#include <QtTest>

void PreviewTest::jsonTextViewSpecimenColorsSyntaxInBothThemes() {
    using choscordb::design::contrastRatio;
    using choscordb::design::resolvedThemeForWidget;
    choscordb::design::PreviewWindow window;
    QVERIFY(window.selectSpecimen("right-sheet"));
    window.show();
    QCoreApplication::processEvents();
    const QString json = QStringLiteral(
        "{\n  \"key\": \"value\",\n  \"count\": 42,\n  \"active\": true,\n  \"missing\": null\n}");
    QColor priorKey;
    for (const auto* name : {"previewLight", "previewDark"}) {
        auto* host = window.findChild<QWidget*>(name);
        QVERIFY(host);
        auto* open = host->findChild<QPushButton*>("previewOpenRightSheet");
        QVERIFY(open);
        open->click();
        auto* sheet = previewSurface<choscordb::design::RightSheet>(host, "previewOpenRightSheet");
        QVERIFY(sheet);
        QTRY_VERIFY(sheet->isVisible());
        auto* editor = sheet->findChild<QPlainTextEdit*>("previewRightSheetContent");
        QVERIFY(editor);
        QVERIFY(editor->isVisible());
        QVERIFY(editor->inherits("choscordb::design::JsonTextView"));
        QVERIFY(editor->isReadOnly());
        QCOMPARE(editor->toPlainText(), json);
        QCOMPARE(editor->font().family(),
                 choscordb::design::resolveTypography(choscordb::design::TypographyRole::Monospace)
                     .family());
        const auto colors = resolvedThemeForWidget(*editor).colors;
        const auto block = editor->document()->firstBlock().next();
        QVERIFY(block.isValid());
        const auto inkAt = [&](const QTextBlock& target, int position) {
            for (const auto& range : target.layout()->formats())
                if (position >= range.start && position < range.start + range.length)
                    return range.format.foreground().color();
            return colors.fg;
        };
        const auto key = inkAt(block, 3);
        const auto punctuation = inkAt(block, 7);
        const auto stringValue = inkAt(block, 10);
        QVERIFY(key != stringValue);
        QVERIFY(key != punctuation);
        QVERIFY(stringValue != punctuation);
        QVERIFY(contrastRatio(key, colors.surface) >= 4.5);
        QVERIFY(contrastRatio(stringValue, colors.surface) >= 4.5);
        QVERIFY(contrastRatio(punctuation, colors.surface) >= 4.5);
        const auto numberBlock = block.next();
        const auto literalBlock = numberBlock.next();
        QVERIFY(numberBlock.isValid() && literalBlock.isValid());
        const auto number = inkAt(numberBlock, 11);
        const auto literal = inkAt(literalBlock, 12);
        QVERIFY(number != key && number != stringValue && number != punctuation);
        QVERIFY(literal != key && literal != stringValue && literal != number &&
                literal != punctuation);
        QVERIFY(contrastRatio(number, colors.surface) >= 4.5);
        QVERIFY(contrastRatio(literal, colors.surface) >= 4.5);
        const auto nullBlock = literalBlock.next();
        QVERIFY(nullBlock.isValid());
        QCOMPARE(inkAt(nullBlock, 13), literal);
        if (priorKey.isValid())
            QVERIFY(key != priorKey);
        priorKey = key;
        sheet->reject();
    }
}
