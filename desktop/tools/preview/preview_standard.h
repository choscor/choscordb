#pragma once

class QString;
class QWidget;
class QVBoxLayout;

namespace choscordb::design::preview_detail {
void populateStandard(const QString& id, QWidget* host, QVBoxLayout* layout);
} // namespace choscordb::design::preview_detail
