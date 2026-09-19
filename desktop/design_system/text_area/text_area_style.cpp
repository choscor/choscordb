#include "design_system/text_area/text_area_style.h"
#include "design_system/style/style_resource.h"

#include <QTextDocument>
#include <QTextEdit>

namespace choscordb::design {
QString textAreaStyleSheet() {
    return loadStyleSheet(QStringLiteral("text_area/text_area_style_sheet.qss"));
}

void configureRichTextArea(QTextEdit& editor) {
    editor.document()->setDefaultStyleSheet(
        loadStyleSheet(QStringLiteral("text_area/configure_rich_text_area.qss")));
}
} // namespace choscordb::design
