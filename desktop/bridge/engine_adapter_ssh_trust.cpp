#include "bridge/engine_adapter_p.h"

namespace choscordb {
namespace engine_adapter_detail {
QList<SshHostKeyCandidate> hostKeyCandidates(const rust::Vec<SshHostKeyCandidateDto>& values) {
    QList<SshHostKeyCandidate> candidates;
    candidates.reserve(static_cast<qsizetype>(values.size()));
    for (const auto& value : values) {
        SshHostKeyCandidate candidate;
        const auto kind = string(value.target_kind);
        candidate.target.kind = kind == "jump"         ? SshHostKeyTarget::Kind::JumpId
                                : kind == "jump_index" ? SshHostKeyTarget::Kind::JumpIndex
                                                       : SshHostKeyTarget::Kind::Target;
        candidate.target.id = string(value.target_id);
        candidate.target.index = static_cast<int>(value.target_index);
        candidate.originalHost = string(value.original_host);
        candidate.hostname = string(value.hostname);
        candidate.port = value.port;
        candidate.hostKeyAlias = string(value.host_key_alias);
        candidate.keyType = string(value.key_type);
        candidate.publicKey = string(value.public_key);
        candidate.sha256 = string(value.sha256);
        candidate.opaqueJson = string(value.opaque_json);
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}
} // namespace engine_adapter_detail

void EngineAdapter::inspectSshHostKeys(const SavedProfile& profile, const SshHostKeyTarget& target,
                                       const QList<SshHopCredential>& precedingHopCredentials,
                                       quint64 token) {
    if (d_->closing || d_->stopping) {
        emit sshHostKeyOperationFailed(token, tr("Workspace is closing."));
        return;
    }
    const char* kind = target.kind == SshHostKeyTarget::Kind::Target   ? "target"
                       : target.kind == SshHostKeyTarget::Kind::JumpId ? "jump"
                                                                       : "jump_index";
    if (target.kind == SshHostKeyTarget::Kind::JumpIndex && target.index < 0) {
        emit sshHostKeyOperationFailed(token, tr("Invalid SSH hop index."));
        return;
    }
    ProfileCredentialsDto credentials;
    engine_adapter_detail::addHopCredentials(credentials, precedingHopCredentials);
    const auto idBytes = target.id.toUtf8();
    const auto submitted = profile_inspect_ssh_host_keys(
        *d_->engine, engine_adapter_detail::profileDto(profile), std::move(credentials), kind,
        engine_adapter_detail::utf8View(idBytes),
        target.kind == SshHostKeyTarget::Kind::JumpIndex ? static_cast<quint32>(target.index) : 0,
        token);
    if (!submitted.accepted)
        emit sshHostKeyOperationFailed(token, engine_adapter_detail::string(submitted.error));
}

void EngineAdapter::approveSshHostKey(const SshHostKeyCandidate& candidate,
                                      const QString& knownHostsPath, quint64 token) {
    if (d_->closing || d_->stopping) {
        emit sshHostKeyOperationFailed(token, tr("Workspace is closing."));
        return;
    }
    const auto candidateBytes = candidate.opaqueJson.toUtf8();
    const auto fingerprintBytes = candidate.sha256.toUtf8();
    const auto pathBytes = knownHostsPath.toUtf8();
    const auto submitted =
        approve_ssh_host_key(*d_->engine, engine_adapter_detail::utf8View(candidateBytes),
                             engine_adapter_detail::utf8View(fingerprintBytes),
                             engine_adapter_detail::utf8View(pathBytes), token);
    if (!submitted.accepted)
        emit sshHostKeyOperationFailed(token, engine_adapter_detail::string(submitted.error));
}
} // namespace choscordb
