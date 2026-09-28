#pragma once

class QListWidget;
class QVBoxLayout;
class QWidget;

namespace choscordb {
class SidebarSection;
namespace design {
class Text;
class ThemeManager;
} // namespace design

struct PinnedSidebar {
    SidebarSection* section;
    QListWidget* list;
    design::Text* empty;
};

PinnedSidebar buildPinnedSidebar(QWidget* parent, QVBoxLayout* layout, design::ThemeManager* theme);
} // namespace choscordb
