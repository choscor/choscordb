#pragma once

#include "bridge/engine_adapter.h"
#include "choscordb-bridge/src/lib.rs.h"
#include <QByteArray>
#include <QHash>
#include <QQueue>
#include <QSet>

namespace choscordb {
namespace engine_adapter_detail {
inline rust::Str utf8View(const QByteArray& bytes) {
    return rust::Str(bytes.constData(), static_cast<size_t>(bytes.size()));
}
inline QString string(const rust::String& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
inline rust::String rustString(const QString& value) {
    const auto bytes = value.toUtf8();
    return rust::String(bytes.constData(), static_cast<size_t>(bytes.size()));
}
inline ProfileDto profileDto(const SavedProfile& value) {
    ProfileDto dto;
    dto.group_id = rustString(value.groupId);
    dto.id = rustString(value.id);
    dto.name = rustString(value.name);
    dto.driver = rustString(value.driver);
    dto.path = rustString(value.path);
    dto.read_only = value.readOnly;
    dto.host = rustString(value.host);
    dto.port = value.port;
    dto.database = rustString(value.database);
    dto.user = rustString(value.user);
    dto.tls = rustString(value.tls);
    dto.root_certificate = rustString(value.rootCertificate);
    dto.tls_client_identity = rustString(value.tlsClientIdentity);
    dto.tls_credential_ref = rustString(value.tlsCredentialRef);
    dto.proxy_options = rustString(value.proxyOptions);
    dto.proxy_credential_ref = rustString(value.proxyCredentialRef);
    dto.ssh_jump_credential_refs = rustString(value.sshJumpCredentialRefs);
    dto.ssh_private_key_ref = rustString(value.sshPrivateKeyRef);
    dto.ssh_jump_private_key_refs = rustString(value.sshJumpPrivateKeyRefs);
    dto.ssh_options = rustString(value.sshOptions);
    dto.authentication = rustString(value.authentication);
    dto.credential_ref = rustString(value.credentialRef);
    dto.ssh_credential_ref = rustString(value.sshCredentialRef);
    dto.ssh_enabled = value.sshEnabled;
    dto.ssh_host = rustString(value.sshHost);
    dto.ssh_port = value.sshPort;
    dto.ssh_user = rustString(value.sshUser);
    dto.ssh_authentication = rustString(value.sshAuthentication);
    dto.ssh_identity_source = rustString(value.sshIdentitySource);
    dto.ssh_identity_file = rustString(value.sshIdentityFile);
    return dto;
}
inline void addHopCredentials(ProfileCredentialsDto& credentials,
                              const QList<SshHopCredential>& sshHops) {
    for (const auto& hop : sshHops) {
        SshHopCredentialDto value;
        value.id = rustString(hop.id);
        value.secret = rustString(hop.secret);
        value.action = rustString(hop.action);
        value.has_secret = hop.hasSecret;
        value.private_key = rustString(hop.privateKey);
        value.private_key_action = rustString(hop.privateKeyAction);
        value.has_private_key = hop.hasPrivateKey;
        credentials.ssh_hops.push_back(std::move(value));
    }
}
QList<SshHostKeyCandidate> hostKeyCandidates(const rust::Vec<SshHostKeyCandidateDto>& values);
} // namespace engine_adapter_detail
struct EngineAdapter::Private {
    explicit Private(const QString& path);
    rust::Box<BridgeEngine> engine;
    struct RecoveryRequest {
        quint64 token;
        std::function<Submit()> command;
        quint64 bytes;
    };
    QQueue<RecoveryRequest> recoveryQueue;
    std::optional<quint64> activeRecovery;
    quint64 queuedRecoveryBytes = 0;
    QSet<quint64> connections;
    struct QueryPaging {
        quint64 connection;
        quint32 pageSize;
    };
    QHash<quint64, QueryPaging> queryPaging;
    struct InspectionRequest {
        quint64 connection, token;
        QString object;
        ObjectInspectionPane pane;
    };
    QHash<quint64, InspectionRequest> inspections;
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
