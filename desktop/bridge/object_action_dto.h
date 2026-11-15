#pragma once

#include "bridge/object_action.h"
#include "bridge/rust_text.h"

namespace choscordb {

inline ObjectActionRequestDto objectActionDto(const ObjectActionRequest& request) {
    using bridge_detail::toRust;
    ObjectActionRequestDto dto;
    dto.action = toRust(request.action);
    dto.driver = toRust(request.driver);
    dto.kind = toRust(request.kind);
    dto.object_id = toRust(request.objectId);
    dto.qualified_name = toRust(request.qualifiedName);
    dto.new_name = toRust(request.newName);
    dto.relation_subtype = toRust(request.relationSubtype);
    dto.cascade = request.cascade;
    dto.restart_identity = request.restartIdentity;
    dto.with_data = request.withData;
    return dto;
}

}
