//! Private, process-scoped keys for optional SSH sharing. No credential is stored
//! in a registry key, filesystem name, log, or externally visible digest.
use crate::{Result, Secret, SshTunnel};
use hmac::{Hmac, Mac};
use sha2::Sha256;
use std::{collections::BTreeMap, sync::OnceLock};
type Digest = Hmac<Sha256>;
fn digest() -> Digest {
    static SALT: OnceLock<[u8; 32]> = OnceLock::new();
    let salt = SALT.get_or_init(|| {
        let mut value = [0; 32];
        value[..16].copy_from_slice(uuid::Uuid::new_v4().as_bytes());
        value[16..].copy_from_slice(uuid::Uuid::new_v4().as_bytes());
        value
    });
    Digest::new_from_slice(salt).expect("fixed HMAC key length")
}
fn field(hash: &mut Digest, value: &[u8]) {
    hash.update(&(value.len() as u64).to_le_bytes());
    hash.update(value);
}
pub(crate) fn key(
    settings: &SshTunnel,
    secret: Option<&Secret>,
    hops: &BTreeMap<String, Secret>,
    private_key: Option<&Secret>,
    hop_keys: &BTreeMap<String, Secret>,
    purpose: &str,
    extra: &[Vec<u8>],
) -> Result<[u8; 32]> {
    let mut hash = digest();
    field(&mut hash, purpose.as_bytes());
    field(
        &mut hash,
        tokio::runtime::Handle::current()
            .id()
            .to_string()
            .as_bytes(),
    );
    let mut settings = settings.clone();
    if purpose == "session" {
        settings.options.local_host = None;
        settings.options.local_port = None;
        settings.options.remote_host = None;
        settings.options.remote_port = None;
    }
    field(
        &mut hash,
        &serde_json::to_vec(&settings).map_err(|_| {
            crate::DriverError::new(
                crate::ErrorKind::InvalidInput,
                "Invalid SSH sharing settings",
            )
        })?,
    );
    for secret in [secret, private_key] {
        field(&mut hash, &[u8::from(secret.is_some())]);
        if let Some(secret) = secret {
            field(&mut hash, secret.expose().as_bytes());
        }
    }
    for map in [hops, hop_keys] {
        field(&mut hash, &map.len().to_le_bytes());
        for (id, secret) in map {
            field(&mut hash, id.as_bytes());
            field(&mut hash, secret.expose().as_bytes());
        }
    }
    for value in extra {
        field(&mut hash, value);
    }
    Ok(hash.finalize().into_bytes().into())
}
pub(crate) async fn configuration(
    settings: &SshTunnel,
    configuration: &str,
    inline: bool,
) -> Vec<u8> {
    let mut value = configuration.to_owned();
    let inline_path = inline.then(|| settings.identity_file.clone()).flatten();
    if let Some(path) = &inline_path {
        value = value.replace(path, "<inline-identity>");
    }
    let mut hash = digest();
    field(&mut hash, value.as_bytes());
    let mut paths = Vec::new();
    let mut identities = Vec::new();
    let mut uncertain = false;
    let public_key = configuration
        .lines()
        .any(|line| line == "pubkeyauthentication true" || line == "pubkeyauthentication yes");
    for line in configuration.lines() {
        let Some((name, values)) = line.split_once(' ') else {
            continue;
        };
        if matches!(name, "knownhostscommand" | "pkcs11provider") && values != "none" {
            uncertain = true;
        }
        if name == "verifyhostkeydns" && values != "false" && values != "no" {
            uncertain = true;
        }
        if !public_key && matches!(name, "identityfile" | "certificatefile") {
            continue;
        }
        if matches!(
            name,
            "identityfile"
                | "certificatefile"
                | "userknownhostsfile"
                | "globalknownhostsfile"
                | "revokedhostkeys"
        ) {
            // Also consider the whole value: older OpenSSH dumps may omit quotes
            // around a single path containing spaces. More ambiguous lists never
            // reuse a session rather than guessing which trust/key file is used.
            let words = shlex::split(values).unwrap_or_default();
            if words.len() > 2 || words.is_empty() {
                uncertain = true;
            }
            if name == "identityfile" {
                identities.extend(words.iter().cloned());
                identities.push(values.to_owned());
            }
            paths.extend(words);
            paths.push(values.to_owned());
        }
    }
    paths.extend(settings.options.known_hosts_file.iter().cloned());
    if public_key {
        paths.extend(settings.identity_file.iter().cloned());
        identities.extend(settings.identity_file.iter().cloned());
    }
    for identity in identities {
        if inline_path.as_ref() != Some(&identity) && identity != "none" {
            // OpenSSH may load a public identity and its implicit user
            // certificate even when ssh -G contains no CertificateFile.
            paths.push(format!("{identity}.pub"));
            paths.push(format!("{identity}-cert.pub"));
        }
    }
    paths.sort();
    paths.dedup();
    let metadata = tokio::task::spawn_blocking(move || {
        let mut hash = digest();
        for path in paths {
            if path == "none" || inline_path.as_ref() == Some(&path) {
                continue;
            }
            if path.contains(['%', '$']) || (path.starts_with('~') && !path.starts_with("~/")) {
                uncertain = true;
                continue;
            }
            let path = if let Some(suffix) = path.strip_prefix("~/") {
                let Some(home) = std::env::var_os("HOME") else {
                    uncertain = true;
                    continue;
                };
                std::path::PathBuf::from(home).join(suffix)
            } else {
                std::path::PathBuf::from(&path)
            };
            field(&mut hash, path.as_os_str().as_encoded_bytes());
            match std::fs::metadata(&path) {
                Ok(metadata) => {
                    #[cfg(unix)]
                    {
                        use std::os::unix::fs::{FileTypeExt, MetadataExt};
                        if path == std::path::Path::new("/dev/null")
                            && metadata.file_type().is_char_device()
                        {
                            // macOS changes the null device's timestamps on writes.
                            // Its trust contents remain empty; scope reuse to the
                            // device identity and ownership, never mutable times.
                            for value in [
                                metadata.dev(),
                                metadata.ino(),
                                metadata.rdev(),
                                u64::from(metadata.mode()),
                                u64::from(metadata.uid()),
                                u64::from(metadata.gid()),
                            ] {
                                field(&mut hash, &value.to_le_bytes());
                            }
                            continue;
                        }
                    }
                    if !metadata.is_file() {
                        uncertain = true;
                        continue;
                    }
                    #[cfg(unix)]
                    {
                        use std::os::unix::fs::MetadataExt;
                        for value in [
                            metadata.dev(),
                            metadata.ino(),
                            u64::from(metadata.mode()),
                            u64::from(metadata.uid()),
                            u64::from(metadata.gid()),
                            metadata.len(),
                            metadata.mtime() as u64,
                            metadata.mtime_nsec() as u64,
                            metadata.ctime() as u64,
                            metadata.ctime_nsec() as u64,
                        ] {
                            field(&mut hash, &value.to_le_bytes());
                        }
                    }
                    #[cfg(not(unix))]
                    {
                        let _ = metadata;
                        uncertain = true;
                    }
                }
                Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
                    field(&mut hash, b"missing")
                }
                Err(_) => uncertain = true,
            }
        }
        if uncertain {
            field(&mut hash, uuid::Uuid::new_v4().as_bytes());
        }
        hash.finalize().into_bytes().to_vec()
    })
    .await;
    match metadata {
        Ok(metadata) => field(&mut hash, &metadata),
        Err(_) => field(&mut hash, uuid::Uuid::new_v4().as_bytes()),
    };
    // Agent identities can change without changing the socket inode. Snapshot
    // public identities as well when public-key authentication is enabled.
    if public_key {
        let configured = configuration
            .lines()
            .find_map(|line| line.strip_prefix("identityagent "));
        let socket = settings.options.agent_socket.as_deref().or(configured);
        if socket != Some("none") {
            field(
                &mut hash,
                socket
                    .map(str::to_owned)
                    .or_else(|| std::env::var("SSH_AUTH_SOCK").ok())
                    .unwrap_or_default()
                    .as_bytes(),
            );
            let mut command = tokio::process::Command::new("ssh-add");
            command.arg("-L");
            if let Some(socket) = socket.filter(|socket| *socket != "SSH_AUTH_SOCK") {
                command.env("SSH_AUTH_SOCK", socket);
            }
            match crate::ssh_trust::output(&mut command, None, 128 * 1024).await {
                Ok((status, keys)) if status.success() => field(&mut hash, &keys),
                _ => field(&mut hash, b"agent unavailable"),
            }
        }
    }
    hash.finalize().into_bytes().to_vec()
}
