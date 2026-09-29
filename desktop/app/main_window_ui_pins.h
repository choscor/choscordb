#pragma once

class QTreeView;
class QVBoxLayout;
class QWidget;

namespace choscordb {
class SidebarSection;
namespace design {
class Text;
} // namespace design

struct PinnedSidebar {
    SidebarSection* section;
    QTreeView* list;
    design::Text* empty;
};

PinnedSidebar buildPinnedSidebar(QWidget* parent, QVBoxLayout* layout);
} // namespace choscordb
