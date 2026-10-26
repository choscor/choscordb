#pragma once

class QWidget;
class QVBoxLayout;

namespace choscordb::design::preview_detail {
// Dialogs open in the window overlay, outside the themed host, so each one
// takes the host's theme, stylesheet and palette itself.
void applyHostTheme(QWidget& surface, const QWidget& host);
void populateDialogSections(QWidget* host, QVBoxLayout* layout);
} // namespace choscordb::design::preview_detail
