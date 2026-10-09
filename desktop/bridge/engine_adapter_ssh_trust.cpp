#include "bridge/engine_adapter_p.h"

namespace choscordb {
namespace engine_adapter_detail {
QList<SshHostKeyCandidate> hostKeyCandidates(const rust::Vec<SshHostKeyCandidateDto>& values) {
    QList<SshHostKeyCandidate> candidates;
    candidates.reserve(static_cast<qsizetype>(values.size()));
    for (const auto& value : values) {
        SshHostKeyCandidate candidate;
        candidate.originalHost = fromRust(value.original_host);
        candidate.hostname = fromRust(value.hostname);
        candidate.port = value.port;
        candidate.hostKeyAlias = fromRust(value.host_key_alias);
        candidate.keyType = fromRust(value.key_type);
        candidate.publicKey = fromRust(value.public_key);
        candidate.sha256 = fromRust(value.sha256);
        candidate.opaqueJson = fromRust(value.opaque_json);
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}
} // namespace engine_adapter_detail

void EngineAdapter::inspectSshHostKeys(const SavedProfile& profile, quint64 token) {
    if (d_->closing || d_->stopping) {
        emit sshHostKeyOperationFailed(token, tr("Workspace is closing."));
        return;
    }
    // The form inspects the final SSH server; jump hosts are no longer configurable.
    const auto submitted =
        profile_inspect_ssh_host_keys(*d_->engine, engine_adapter_detail::profileDto(profile),
                                      ProfileCredentialsDto{}, "target", "", 0, token);
    if (!submitted.accepted)
        emit sshHostKeyOperationFailed(token, engine_adapter_detail::fromRust(submitted.error));
}

int EngineAdapter::profileSecretMaxBytes() {
    return static_cast<int>(profile_secret_max_bytes());
}
QString EngineAdapter::sshPrivateKeyTextError(const QString& text) {
    const auto bytes = text.toUtf8();
    return engine_adapter_detail::fromRust(
        ssh_private_key_text_error(engine_adapter_detail::utf8View(bytes)));
}
bool EngineAdapter::sshKnownHostsPathValid(const QString& path) {
    const auto bytes = path.toUtf8();
    return ssh_known_hosts_path_valid(engine_adapter_detail::utf8View(bytes));
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
        emit sshHostKeyOperationFailed(token, engine_adapter_detail::fromRust(submitted.error));
}
} // namespace choscordb
