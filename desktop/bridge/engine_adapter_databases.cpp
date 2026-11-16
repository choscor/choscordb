#include "bridge/engine_adapter_p.h"

namespace choscordb {
using engine_adapter_detail::fromRust;
using engine_adapter_detail::profileDto;
using engine_adapter_detail::secretDraftsDto;
using engine_adapter_detail::withConnectionTimeout;
bool EngineAdapter::listDatabases(quint64 connection, quint64 token) {
    auto reply = list_databases(*d_->engine, connection, token);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
    return reply.accepted;
}
bool EngineAdapter::switchDatabase(quint64 connection, const SavedProfile& profile, quint64 token) {
    auto credentials = profile_draft_credentials(profileDto(profile), secretDraftsDto({}), false);
    auto reply = switch_database(*d_->engine, connection,
                                 withConnectionTimeout(profile, d_->connectionTimeoutSeconds),
                                 std::move(credentials), token);
    if (!reply.accepted)
        emit commandFailed(fromRust(reply.error));
    return reply.accepted;
}
}
