#include "design_system/json_text_view/json_text_view.h"
#include "design_system/theme_manager.h"

#include <QApplication>
#include <QClipboard>
#include <QTextBlock>
#include <QTextLayout>
#include <QVBoxLayout>
#include <QtTest>

using namespace choscordb::design;

class JsonTextViewTest final : public QObject {
    Q_OBJECT

  private slots:
    void syntaxColorsAndCopySurviveThemeChanges();
};

void JsonTextViewTest::syntaxColorsAndCopySurviveThemeChanges() {
    ThemeManager theme;
    theme.setMode(ThemeMode::Light);
    QWidget root;
    theme.applyTo(root);
    auto* layout = new QVBoxLayout(&root);
    auto* editor = new JsonTextView(&root);
    layout->addWidget(editor);
    const QString json = QStringLiteral(
        R"json({"key":"true \" 99","count":42,"active":true,"missing":null,"off":false})json");
    editor->setPlainText(json);
    root.show();
    QCoreApplication::processEvents();

    const auto inkAt = [&](int position) {
        const auto block = editor->document()->firstBlock();
        for (const auto& range : block.layout()->formats())
            if (position >= range.start && position < range.start + range.length)
                return range.format.foreground().color();
        return resolvedThemeForWidget(*editor).colors.text;
    };
    const int key = json.indexOf(QStringLiteral("key"));
    const int string = json.indexOf(QStringLiteral("true"));
    const int stringNumber = json.indexOf(QStringLiteral("99"));
    const int number = json.indexOf(QStringLiteral("42"));
    const int literal = json.lastIndexOf(QStringLiteral("true"));
    const int nullValue = json.indexOf(QStringLiteral("null"));
    const int falseValue = json.indexOf(QStringLiteral("false"));
    const int punctuation = json.indexOf(QLatin1Char(':'));
    QVERIFY(key >= 0 && string >= 0 && stringNumber >= 0 && number >= 0 && literal >= 0 &&
            nullValue >= 0 && falseValue >= 0 && punctuation >= 0);

    const auto checkColors = [&] {
        const auto colors = resolvedThemeForWidget(*editor).colors;
        QCOMPARE(inkAt(key), colors.jsonKey);
        QCOMPARE(inkAt(string), colors.jsonString);
        QCOMPARE(inkAt(stringNumber), colors.jsonString);
        QCOMPARE(inkAt(number), colors.jsonNumber);
        QCOMPARE(inkAt(literal), colors.jsonLiteral);
        QCOMPARE(inkAt(nullValue), colors.jsonLiteral);
        QCOMPARE(inkAt(falseValue), colors.jsonLiteral);
        QCOMPARE(inkAt(punctuation), colors.text);
        QCOMPARE(editor->toPlainText(), json);
    };
    checkColors();
    editor->selectAll();
    editor->copy();
    QCOMPARE(QApplication::clipboard()->text(), json);

    theme.setMode(ThemeMode::Dark);
    theme.applyTo(root);
    QCoreApplication::processEvents();
    checkColors();

    editor->setPlainText(QStringLiteral("true"));
    QCOMPARE(inkAt(0), resolvedThemeForWidget(*editor).colors.jsonLiteral);
    QCOMPARE(editor->toPlainText(), QStringLiteral("true"));
}

QTEST_MAIN(JsonTextViewTest)
#include "json_text_view_test.moc"
