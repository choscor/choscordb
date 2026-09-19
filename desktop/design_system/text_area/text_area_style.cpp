#include "design_system/text_area/text_area_style.h"

#include <QTextDocument>
#include <QTextEdit>

namespace choscordb::design {
QString textAreaStyleSheet() {
    return QStringLiteral(
        R"(QPlainTextEdit, QTextEdit { border: 1px solid @input; border-radius: @controlRadius; padding: 8px; background: @field; color: @foreground; }
)");
}

void configureRichTextArea(QTextEdit& editor) {
    editor.document()->setDefaultStyleSheet(
        QStringLiteral("p { margin-top: 0; margin-bottom: 0; }"));
}
} // namespace choscordb::design
