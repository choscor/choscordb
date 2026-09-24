//! Explicit inspection and approval of public SSH host keys. Inspection is not trust.
use crate::{DriverError, ErrorKind, Result};
use serde::{Deserialize, Serialize};
use std::{path::Path, process::Stdio, time::Duration};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    process::Command,
};

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "kind", content = "id", rename_all = "snake_case")]
pub enum SshHostKeyTarget {
    Target,
    Jump(String),
    JumpIndex(usize),
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SshHostKeyCandidate {
    pub target: SshHostKeyTarget,
    pub original_host: String,
    pub hostname: String,
    pub port: u16,
    pub host_key_alias: Option<String>,
    pub key_type: String,
    pub public_key: String,
    pub sha256: String,
}
pub(crate) fn failure(message: &'static str) -> DriverError {
    DriverError::new(ErrorKind::Connection, message)
}
fn invalid() -> DriverError {
    DriverError::new(ErrorKind::InvalidInput, "Invalid SSH host key approval")
}
impl SshHostKeyCandidate {
    pub(crate) fn record_host(&self) -> Result<String> {
        crate::tcp_host(&self.original_host)?;
        let host = crate::tcp_host(&self.hostname)?;
        if self.port == 0 {
            return Err(invalid());
        }
        if let SshHostKeyTarget::Jump(id) = &self.target
            && (id.is_empty()
                || id.len() > 64
                || !id
                    .bytes()
                    .all(|b| b.is_ascii_alphanumeric() || b"_-".contains(&b)))
        {
            return Err(invalid());
        }
        if matches!(self.target,SshHostKeyTarget::JumpIndex(index) if index>=5) {
            return Err(invalid());
        }
        let literal = |value: &str| {
            !value.is_empty()
                && value.len() <= 16 * 1024
                && value
                    .bytes()
                    .all(|b| b.is_ascii_alphanumeric() || b"._-:%".contains(&b))
        };
        if !literal(host) {
            return Err(invalid());
        }
        if let Some(alias) = &self.host_key_alias {
            // OpenSSH stores HostKeyAlias verbatim, without non-default port suffix.
            if alias.is_empty()
                || alias.len() > 16 * 1024
                || !alias
                    .bytes()
                    .all(|b| b.is_ascii_alphanumeric() || b"._-:%[]".contains(&b))
            {
                return Err(invalid());
            }
            return Ok(alias.clone());
        }
        Ok(if self.port == 22 {
            host.to_owned()
        } else {
            format!("[{host}]:{}", self.port)
        })
    }
    pub(crate) async fn validate(&self) -> Result<()> {
        self.record_host()?;
        if !matches!(
            self.key_type.as_str(),
            "ssh-ed25519"
                | "ssh-rsa"
                | "ecdsa-sha2-nistp256"
                | "ecdsa-sha2-nistp384"
                | "ecdsa-sha2-nistp521"
        ) || self.public_key.is_empty()
            || self.public_key.len() > 16 * 1024
            || !self
                .public_key
                .bytes()
                .all(|b| b.is_ascii_alphanumeric() || b"+/=".contains(&b))
        {
            return Err(invalid());
        }
        if fingerprint(&self.key_type, &self.public_key).await? != self.sha256 {
            return Err(invalid());
        }
        Ok(())
    }
}
pub(crate) async fn output(
    command: &mut Command,
    input: Option<&[u8]>,
    limit: u64,
) -> Result<(std::process::ExitStatus, Vec<u8>)> {
    crate::ssh_askpass::clear_ssh_askpass_environment(command);
    command
        .stdin(if input.is_some() {
            Stdio::piped()
        } else {
            Stdio::null()
        })
        .stdout(Stdio::piped())
        .stderr(Stdio::null());
    let mut child = crate::ssh_process::OwnedChild::spawn(command)
        .map_err(|_| failure("Cannot start OpenSSH host key operation"))?;
    if let Some(input) = input {
        let mut stdin = child
            .child()
            .stdin
            .take()
            .ok_or_else(|| failure("Cannot inspect SSH host key"))?;
        stdin
            .write_all(input)
            .await
            .map_err(|_| failure("Cannot inspect SSH host key"))?;
        drop(stdin);
    }
    let mut bytes = Vec::new();
    child
        .child()
        .stdout
        .take()
        .ok_or_else(|| failure("Cannot inspect SSH host key"))?
        .take(limit + 1)
        .read_to_end(&mut bytes)
        .await
        .map_err(|_| failure("Cannot inspect SSH host key"))?;
    if bytes.len() as u64 > limit {
        return Err(DriverError::new(
            ErrorKind::ResourceLimit,
            "SSH host key output exceeds limit",
        ));
    }
    let status = child
        .wait()
        .await
        .map_err(|_| failure("Cannot inspect SSH host key"))?;
    Ok((status, bytes))
}
pub(crate) async fn fingerprint(kind: &str, key: &str) -> Result<String> {
    let mut command = Command::new("ssh-keygen");
    command.args(["-l", "-E", "sha256", "-f", "-"]);
    let (success, bytes) = output(
        &mut command,
        Some(format!("{kind} {key}\n").as_bytes()),
        4096,
    )
    .await?;
    if !success.success() {
        return Err(invalid());
    }
    let text = std::str::from_utf8(&bytes).map_err(|_| invalid())?;
    if text.lines().count() != 1 {
        return Err(invalid());
    }
    let hash = text
        .split_whitespace()
        .nth(1)
        .filter(|value| value.starts_with("SHA256:") && value.len() == 50)
        .ok_or_else(invalid)?;
    Ok(hash.to_owned())
}
/// Result of an explicitly requested trust-file update.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SshHostKeyApproval {
    Approved,
    /// The OS append/sync began but its outcome could not be established in
    /// time. Retrying acquires the same file lock and reconciles the exact key.
    OutcomeUnknown,
}
/// Append exactly the explicitly approved public key. Existing host records are
/// never replaced. The caller selects the destination file; no default is written.
/// Cancellation prevents appends that have not started. Already issued OS I/O
/// cannot be recalled: a timeout/error then returns OutcomeUnknown, not rejection.
pub async fn approve_ssh_host_key(
    candidate: &SshHostKeyCandidate,
    expected_sha256: &str,
    path: &Path,
) -> Result<SshHostKeyApproval> {
    let literal = path.to_str().ok_or_else(invalid)?;
    let normalized: std::path::PathBuf = path.components().collect();
    if !path.is_absolute()
        || path.file_name().is_none()
        || normalized.as_os_str() != path.as_os_str()
        || path.components().any(|part| {
            matches!(
                part,
                std::path::Component::CurDir | std::path::Component::ParentDir
            )
        })
        || literal.chars().any(char::is_control)
        || literal.contains(['%', '$', '"', '\\'])
    {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "Select an absolute normalized SSH known hosts path without expansion tokens or quote characters",
        ));
    }
    if expected_sha256 != candidate.sha256 {
        return Err(invalid());
    }
    let state = crate::ssh_trust_store::ApprovalState::new();
    let _guard = crate::ssh_trust_store::ApprovalGuard(state.clone());
    match tokio::time::timeout(Duration::from_secs(5), async {
        candidate.validate().await?;
        crate::ssh_trust_store::append(candidate, path, &state).await
    })
    .await
    {
        Ok(result) => result,
        Err(_) => state.cancel().map(Ok).unwrap_or_else(|| {
            Err(DriverError::new(
                ErrorKind::Timeout,
                "SSH host key approval timed out before appending",
            ))
        }),
    }
}

/// Inspect public keys without authenticating to the inspected destination.
/// Only credentials for preceding, already trusted hops are used. No trust file
/// is changed. The caller must independently approve a displayed fingerprint.
pub async fn inspect_ssh_host_keys(
    settings: &crate::SshTunnel,
    target: SshHostKeyTarget,
    mut hop_secrets: std::collections::BTreeMap<String, crate::Secret>,
    mut hop_private_keys: std::collections::BTreeMap<String, crate::Secret>,
) -> Result<Vec<SshHostKeyCandidate>> {
    settings.validate()?;
    let count = match &target {
        SshHostKeyTarget::Target => settings.options.jump_hosts.len(),
        SshHostKeyTarget::Jump(id) => settings
            .options
            .jump_hosts
            .iter()
            .position(|hop| hop.id.as_ref() == Some(id))
            .ok_or_else(invalid)?,
        SshHostKeyTarget::JumpIndex(index) if *index < settings.options.jump_hosts.len() => *index,
        SshHostKeyTarget::JumpIndex(_) => return Err(invalid()),
    };
    if hop_secrets.len() > 5 || hop_private_keys.len() > 5 {
        return Err(invalid());
    }
    let prefix: std::collections::BTreeSet<_> = settings
        .options
        .jump_hosts
        .iter()
        .take(count)
        .filter_map(|hop| hop.id.as_ref())
        .collect();
    hop_secrets.retain(|id, _| prefix.contains(id));
    hop_private_keys.retain(|id, _| prefix.contains(id));
    tokio::time::timeout(
        Duration::from_secs(u64::from(settings.options.connect_timeout_seconds)),
        async {
            let selected = match &target {
                SshHostKeyTarget::Target => settings.clone(),
                SshHostKeyTarget::Jump(_) | SshHostKeyTarget::JumpIndex(_) => {
                    crate::ssh_forward::hop_settings(settings, &settings.options.jump_hosts[count])
                }
            };
            let address = crate::ssh_command::inspect_address(&selected, count > 0).await?;
            let mut candidate = SshHostKeyCandidate {
                target,
                original_host: selected.host.clone(),
                hostname: address.hostname.clone(),
                port: address.port,
                host_key_alias: address.host_key_alias,
                key_type: String::new(),
                public_key: String::new(),
                sha256: String::new(),
            };
            candidate.record_host()?;
            let relay = if count > 0 {
                Some(
                    ScanRelay::new(
                        settings.clone(),
                        hop_secrets,
                        hop_private_keys,
                        count,
                        crate::ssh_command::endpoint(&address.hostname, address.port),
                    )
                    .await?,
                )
            } else {
                None
            };
            let (host, port) = relay
                .as_ref()
                .map(|relay| ("127.0.0.1", relay.port))
                .unwrap_or((&address.hostname, address.port));
            let mut command = Command::new("ssh-keyscan");
            command.args([
                "-T",
                &settings.options.connect_timeout_seconds.to_string(),
                "-p",
                &port.to_string(),
                "-t",
                "ed25519,ecdsa,rsa",
                "--",
                host,
            ]);
            let (status, bytes) = output(&mut command, None, 64 * 1024).await?;
            drop(relay);
            if !status.success() {
                return Err(failure("Cannot inspect SSH host keys through trusted hops"));
            }
            let text = std::str::from_utf8(&bytes)
                .map_err(|_| failure("Invalid SSH host key inspection output"))?;
            let mut candidates = Vec::new();
            for line in text
                .lines()
                .filter(|line| !line.starts_with('#') && !line.trim().is_empty())
            {
                let fields: Vec<_> = line.split_whitespace().collect();
                if fields.len() != 3 || candidates.len() >= 3 {
                    return Err(failure("Invalid SSH host key inspection output"));
                }
                candidate.key_type = fields[1].into();
                candidate.public_key = fields[2].into();
                candidate.sha256 = fingerprint(&candidate.key_type, &candidate.public_key).await?;
                candidate.validate().await?;
                if !candidates.contains(&candidate) {
                    candidates.push(candidate.clone());
                }
            }
            if candidates.is_empty() {
                return Err(failure("SSH server did not provide a supported host key"));
            }
            Ok(candidates)
        },
    )
    .await
    .map_err(|_| DriverError::new(ErrorKind::Timeout, "SSH host key inspection timed out"))?
}
struct ScanRelay {
    port: u16,
    worker: tokio::task::JoinHandle<()>,
}
impl Drop for ScanRelay {
    fn drop(&mut self) {
        self.worker.abort();
    }
}
impl ScanRelay {
    async fn new(
        settings: crate::SshTunnel,
        secrets: std::collections::BTreeMap<String, crate::Secret>,
        keys: std::collections::BTreeMap<String, crate::Secret>,
        count: usize,
        destination: String,
    ) -> Result<Self> {
        let listener = tokio::net::TcpListener::bind((std::net::Ipv4Addr::LOCALHOST, 0))
            .await
            .map_err(|_| failure("Cannot prepare SSH host inspection"))?;
        let port = listener
            .local_addr()
            .map_err(|_| failure("Cannot prepare SSH host inspection"))?
            .port();
        let settings = std::sync::Arc::new(settings);
        let secrets = std::sync::Arc::new(secrets);
        let keys = std::sync::Arc::new(keys);
        let worker = tokio::spawn(async move {
            let mut requests = tokio::task::JoinSet::new();
            for _ in 0..3 {
                let Ok((socket, _)) = listener.accept().await else {
                    break;
                };
                let settings = settings.clone();
                let secrets = secrets.clone();
                let keys = keys.clone();
                let destination = destination.clone();
                requests.spawn(async move {
                    let prepare=crate::SshForward::open_prefix(&settings,&secrets,&keys,count,destination);
                    tokio::pin!(prepare);let mut peek=[0];
                    let stream=tokio::select! {
                        result=&mut prepare=>result,
                        ready=socket.peek(&mut peek)=>{
                            if !matches!(ready,Ok(n) if n>0){return;}
                            prepare.await
                        }
                    };
                    let Ok(stream)=stream else{return;};
                    let (mut a,mut b)=socket.into_split();let(mut c,mut d)=tokio::io::split(stream);
                    tokio::select!{_=tokio::io::copy(&mut a,&mut d)=>{},_=tokio::io::copy(&mut c,&mut b)=>{}}
                });
            }
            while requests.join_next().await.is_some() {}
        });
        Ok(Self { port, worker })
    }
}
