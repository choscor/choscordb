#pragma once
#include <QtGlobal>
#include <optional>
namespace choscordb {
std::optional<quint64> residentBytes();
std::optional<quint64> footprintBytes();
std::optional<quint64> peakBytes();
} // namespace choscordb
