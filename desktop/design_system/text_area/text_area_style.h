#pragma once

#include <QString>

class QTextEdit;

namespace choscordb::design {
// Stock-widget rules use theme placeholders resolved by controlStyleSheet().
QString textAreaStyleSheet();
void configureRichTextArea(QTextEdit& editor);
} // namespace choscordb::design
