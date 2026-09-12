#pragma once
#include <cstdint>
#include <optional>
std::optional<std::uint64_t> currentResidentBytes();
std::optional<std::uint64_t> currentPhysicalFootprintBytes();
