#pragma once

#include "bridge/request_token.h"
#include "bridge/rust_text.h"

namespace choscordb::query_workspace_detail {
inline constexpr auto nextEditRequestToken = &nextRequestToken;
inline constexpr auto text = &bridge_detail::fromRust;
}
