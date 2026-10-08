#pragma once

#include "bridge/request_token.h"
#include "bridge/rust_text.h"

namespace choscordb::query_workspace_detail {
// Short local names for the shared bridge helpers used across query_workspace*.cpp.
inline constexpr auto nextEditRequestToken = &nextRequestToken;
inline constexpr auto text = &bridge_detail::fromRust;
} // namespace choscordb::query_workspace_detail
