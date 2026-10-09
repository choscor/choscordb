#pragma once

#include "bridge/engine_adapter.h"
#include "bridge/rust_text.h"
#include "choscordb-bridge/src/lib.rs.h"
#include <QByteArray>
#include <QHash>
#include <QQueue>
#include <QSet>

namespace choscordb {
namespace engine_adapter_detail {
using bridge_detail::fromRust;
using bridge_detail::toRust;
using bridge_detail::utf8View;
inline ProfileDto profileDto(const SavedProfile& value) {
    ProfileDto dto;
    dto.group_id = toRust(value.groupId);
    dto.id = toRust(value.id);
    dto.name = toRust(value.name);
    dto.driver = toRust(value.driver);
    dto.path = toRust(value.path);
    dto.read_only = value.readOnly;
    dto.host = toRust(value.host);
    dto.port = value.port;
    dto.database = toRust(value.database);
    dto.user = toRust(value.user);
    dto.tls = toRust(value.tls);
    dto.root_certificate = toRust(value.rootCertificate);
    dto.tls_client_identity = toRust(value.tlsClientIdentity);
    dto.tls_credential_ref = toRust(value.tlsCredentialRef);
    dto.proxy_options = toRust(value.proxyOptions);
    dto.proxy_credential_ref = toRust(value.proxyCredentialRef);
    dto.ssh_jump_credential_refs = toRust(value.sshJumpCredentialRefs);
    dto.ssh_private_key_ref = toRust(value.sshPrivateKeyRef);
    dto.ssh_jump_private_key_refs = toRust(value.sshJumpPrivateKeyRefs);
    dto.ssh_options = toRust(value.sshOptions);
    dto.credential_ref = toRust(value.credentialRef);
    dto.ssh_credential_ref = toRust(value.sshCredentialRef);
    dto.ssh_enabled = value.sshEnabled;
    dto.ssh_host = toRust(value.sshHost);
    dto.ssh_port = value.sshPort;
    dto.ssh_user = toRust(value.sshUser);
    dto.ssh_authentication = toRust(value.sshAuthentication);
    dto.ssh_identity_source = toRust(value.sshIdentitySource);
    dto.ssh_identity_file = toRust(value.sshIdentityFile);
    return dto;
}
inline ProfileSecretDraftsDto secretDraftsDto(const ProfileSecretDrafts& value) {
    ProfileSecretDraftsDto dto;
    dto.save_credentials = value.saveCredentials;
    dto.database = toRust(value.database);
    dto.database_modified = value.databaseModified;
    dto.ssh = toRust(value.ssh);
    dto.ssh_modified = value.sshModified;
    dto.tls = toRust(value.tls);
    dto.tls_modified = value.tlsModified;
    dto.ssh_private_key = toRust(value.sshPrivateKey);
    dto.ssh_private_key_modified = value.sshPrivateKeyModified;
    return dto;
}
QList<SshHostKeyCandidate> hostKeyCandidates(const rust::Vec<SshHostKeyCandidateDto>& values);
ObjectGraph objectGraph(const ObjectGraphDto& dto);
} // namespace engine_adapter_detail
struct EngineAdapter::Private {
    explicit Private(const QString& path);
    rust::Box<BridgeEngine> engine;
    QSet<quint64> pendingHistoryClears;
    QSet<quint64> connections;
    quint32 connectionTimeoutSeconds;
    struct InspectionRequest {
        quint64 connection, token;
        QString object;
        ObjectInspectionPane pane;
    };
    QHash<quint64, InspectionRequest> inspections;
    struct GraphRequest {
        quint64 connection, token;
        QString object;
    };
    QHash<quint64, GraphRequest> graphs;
    quint64 nextInspectionToken = quint64(1) << 63;
    bool closing = false, stopping = false;
    std::optional<quint64> shutdownToken;
    QString shutdownHistoryError;
    struct Transfer {
        quint64 reserved;
        std::optional<quint64> retained;
        bool released = false;
    };
    QHash<quint64, Transfer> transfers;
};
} // namespace choscordb
