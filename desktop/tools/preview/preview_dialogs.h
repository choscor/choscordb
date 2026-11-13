#pragma once

class QWidget;
class QVBoxLayout;

namespace choscordb::design::preview_detail {
void applyHostTheme(QWidget& surface, const QWidget& host);
void populateDialogSections(QWidget* host, QVBoxLayout* layout);
}
