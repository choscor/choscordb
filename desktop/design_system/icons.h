#pragma once
#include <QColor>
#include <QIcon>
namespace choscordb::design {
enum class Icon { AppMark, Run, Cancel, Add };
[[nodiscard]] QString iconResourcePath(Icon icon);
[[nodiscard]] bool iconResourceDecodes(Icon icon);
[[nodiscard]] QIcon themedIcon(Icon icon, const QColor& color, int size);
} // namespace choscordb::design
