use crate::{DriverError, ErrorKind, Result, Secret, SshAuthentication, SshTunnel};
use std::io::{Read, Write};
use std::net::SocketAddr;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

const ADDRESS: &str = "CHOSCORDB_SSH_ASKPASS_ADDRESS";
const TOKEN: &str = "CHOSCORDB_SSH_ASKPASS_TOKEN";
const MODE: &str = "CHOSCORDB_SSH_ASKPASS_MODE";
const REQUEST_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(2);

pub struct SshAskpass {
    address: SocketAddr,
    token: String,
    worker: tokio::task::JoinHandle<()>,
}

impl SshAskpass {
    pub fn new(secret: Secret) -> std::io::Result<Self> {
        Self::with_lifetime(secret, std::time::Duration::from_secs(30))
    }

    fn with_lifetime(secret: Secret, lifetime: std::time::Duration) -> std::io::Result<Self> {
        let listener = std::net::TcpListener::bind((std::net::Ipv4Addr::LOCALHOST, 0))?;
        listener.set_nonblocking(true)?;
        let listener = tokio::net::TcpListener::from_std(listener)?;
        let address = listener.local_addr()?;
        let token = uuid::Uuid::new_v4().to_string();
        let expected = std::sync::Arc::new(token.clone());
        let secret = std::sync::Arc::new(secret);
        let worker = tokio::spawn(async move {
            let deadline = tokio::time::sleep(lifetime);
            tokio::pin!(deadline);
            // Keep unauthenticated local clients from creating unbounded tasks.
            let mut requests = tokio::task::JoinSet::new();
            loop {
                let accepted = tokio::select! {
                    _ = &mut deadline => break,
                    _ = requests.join_next(), if !requests.is_empty() => continue,
                    accepted = listener.accept(), if requests.len() < 16 => accepted,
                };
                let Ok((socket, peer)) = accepted else {
                    break;
                };
                if !peer.ip().is_loopback() {
                    continue;
                }
                let expected = expected.clone();
                let secret = secret.clone();
                requests.spawn(async move {
                    let _ = tokio::time::timeout(
                        REQUEST_TIMEOUT,
                        serve_request(socket, &expected, &secret),
                    )
                    .await;
                });
            }
            // JoinSet aborts outstanding requests on expiry and when the broker
            // is dropped, so no detached task retains a credential or socket.
        });
        Ok(Self {
            address,
            token,
            worker,
        })
    }

    pub fn address(&self) -> SocketAddr {
        self.address
    }

    pub fn token(&self) -> &str {
        &self.token
    }

    pub fn configure(&self, command: &mut tokio::process::Command) -> std::io::Result<()> {
        let executable = std::env::current_exe()?;
        command
            .env("SSH_ASKPASS", executable)
            .env("SSH_ASKPASS_REQUIRE", "force")
            .env("DISPLAY", "choscordb-askpass")
            .env(MODE, "1")
            .env(ADDRESS, self.address.to_string())
            .env(TOKEN, &self.token);
        Ok(())
    }
}

impl Drop for SshAskpass {
    fn drop(&mut self) {
        self.worker.abort();
    }
}

async fn serve_request(
    mut socket: tokio::net::TcpStream,
    expected: &str,
    secret: &Secret,
) -> std::io::Result<()> {
    let length = socket.read_u16().await? as usize;
    if length == 0 || length > 128 {
        return Ok(());
    }
    let mut supplied = vec![0; length];
    socket.read_exact(&mut supplied).await?;
    if supplied.as_slice() != expected.as_bytes() {
        return Ok(());
    }
    let bytes = secret.expose().as_bytes();
    let length =
        u32::try_from(bytes.len()).map_err(|_| std::io::Error::other("invalid SSH secret"))?;
    socket.write_all(&length.to_be_bytes()).await?;
    socket.write_all(bytes).await
}

/// Validate credentials before creating a tunnel or starting detached relay work.
pub fn validate_ssh_secret(settings: &SshTunnel, secret: Option<&Secret>) -> Result<()> {
    if settings.authentication == SshAuthentication::Agent {
        return Ok(());
    }
    // This helper serves the legacy ProxyJump path. Its subprocesses inherit
    // askpass environment, so credential-bearing chains must use isolated
    // forwarding through validate_ssh_chain_authentication instead.
    if !settings.options.jump_hosts.is_empty()
        && (settings.authentication == SshAuthentication::Password
            || secret.is_some_and(|secret| !secret.expose().is_empty()))
    {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "SSH jump hosts require agent authentication or an unencrypted target key",
        ));
    }
    if settings.authentication == SshAuthentication::Password
        && secret.is_none_or(|secret| secret.expose().is_empty())
    {
        return Err(DriverError::new(
            ErrorKind::Authentication,
            "SSH password is required",
        ));
    }
    if secret.is_some_and(|secret| secret.expose().contains(['\0', '\r', '\n'])) {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "Invalid SSH credential",
        ));
    }
    Ok(())
}

pub(crate) fn clear_ssh_askpass_environment(command: &mut tokio::process::Command) {
    for key in [
        "SSH_ASKPASS",
        "SSH_ASKPASS_REQUIRE",
        ADDRESS,
        TOKEN,
        MODE,
        crate::ssh_proxy::TOKEN,
    ] {
        command.env_remove(key);
    }
}

pub fn configure_ssh_authentication(
    command: &mut tokio::process::Command,
    settings: &SshTunnel,
    secret: Option<&Secret>,
) -> Result<Option<SshAskpass>> {
    clear_ssh_askpass_environment(command);
    if settings.identity_source == crate::SshIdentitySource::Inline {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "Inline SSH identities require isolated forwarding",
        ));
    }
    // Clearing a saved passphrase supplies an explicit empty secret. For jump
    // chains this represents an unencrypted target key and needs no broker.
    let secret = if settings.authentication == SshAuthentication::PublicKey
        && !settings.options.jump_hosts.is_empty()
    {
        secret.filter(|secret| !secret.expose().is_empty())
    } else {
        secret
    };
    validate_ssh_secret(settings, secret)?;
    crate::ssh_options::configure_ssh_transport(command, settings)?;
    let (batch, preferred) = match settings.authentication {
        SshAuthentication::Agent => {
            command.args([
                "-o",
                "IdentityFile=none",
                "-o",
                "IdentitiesOnly=no",
                "-o",
                "PubkeyAuthentication=yes",
            ]);
            (true, "publickey")
        }
        SshAuthentication::PublicKey => {
            command.args(["-o", "PubkeyAuthentication=yes"]);
            (secret.is_none(), "publickey")
        }
        SshAuthentication::Password => {
            command.args([
                "-o",
                "PubkeyAuthentication=no",
                "-o",
                "PasswordAuthentication=yes",
            ]);
            (false, "password")
        }
    };
    command
        .arg("-o")
        .arg(format!("BatchMode={}", if batch { "yes" } else { "no" }))
        .arg("-o")
        .arg(format!("PreferredAuthentications={preferred}"))
        .args([
            "-o",
            "KbdInteractiveAuthentication=no",
            "-o",
            "NumberOfPasswordPrompts=1",
        ]);
    if settings.authentication == SshAuthentication::PublicKey && settings.identity_file.is_some() {
        command.args(["-o", "IdentitiesOnly=yes"]);
    }
    if settings.authentication == SshAuthentication::Agent {
        return Ok(None);
    }
    let Some(secret) = secret else {
        return Ok(None);
    };
    let lifetime =
        std::time::Duration::from_secs(u64::from(settings.options.connect_timeout_seconds) + 5);
    let askpass =
        SshAskpass::with_lifetime(Secret::new(secret.expose()), lifetime).map_err(|_| {
            DriverError::new(ErrorKind::Connection, "Cannot prepare SSH authentication")
        })?;
    askpass.configure(command).map_err(|_| {
        DriverError::new(ErrorKind::Connection, "Cannot prepare SSH authentication")
    })?;
    Ok(Some(askpass))
}

pub async fn request_ssh_secret(address: SocketAddr, token: &str) -> std::io::Result<Secret> {
    tokio::time::timeout(REQUEST_TIMEOUT, request_secret(address, token))
        .await
        .map_err(|_| std::io::Error::new(std::io::ErrorKind::TimedOut, "SSH askpass timed out"))?
}

async fn request_secret(address: SocketAddr, token: &str) -> std::io::Result<Secret> {
    let mut socket = tokio::net::TcpStream::connect(address).await?;
    let length = u16::try_from(token.len())
        .map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidInput, "invalid token"))?;
    socket.write_all(&length.to_be_bytes()).await?;
    socket.write_all(token.as_bytes()).await?;
    let length = socket.read_u32().await? as usize;
    if length > 16 * 1024 {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            "invalid SSH secret",
        ));
    }
    let mut bytes = zeroize::Zeroizing::new(vec![0; length]);
    socket.read_exact(&mut bytes).await?;
    let value = String::from_utf8(std::mem::take(&mut *bytes))
        .map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidData, "invalid SSH secret"))?;
    Ok(Secret::new(value))
}

/// Returns `Some` only when OpenSSH launched this process as its askpass helper.
pub fn run_ssh_askpass_if_requested() -> Option<i32> {
    if let Some(code) = crate::ssh_proxy::run_if_requested() {
        return Some(code);
    }
    if std::env::var_os(MODE).as_deref() != Some(std::ffi::OsStr::new("1")) {
        return None;
    }
    Some(run_ssh_askpass().map_or(1, |()| 0))
}

fn run_ssh_askpass() -> std::io::Result<()> {
    let address: SocketAddr = std::env::var(ADDRESS)
        .map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidInput, "missing address"))?
        .parse()
        .map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidInput, "invalid address"))?;
    let token = std::env::var(TOKEN)
        .map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidInput, "missing token"))?;
    let mut socket = std::net::TcpStream::connect_timeout(&address, REQUEST_TIMEOUT)?;
    socket.set_read_timeout(Some(REQUEST_TIMEOUT))?;
    socket.set_write_timeout(Some(REQUEST_TIMEOUT))?;
    let length = u16::try_from(token.len())
        .map_err(|_| std::io::Error::new(std::io::ErrorKind::InvalidInput, "invalid token"))?;
    socket.write_all(&length.to_be_bytes())?;
    socket.write_all(token.as_bytes())?;
    let mut length = [0; 4];
    socket.read_exact(&mut length)?;
    let length = u32::from_be_bytes(length) as usize;
    if length > 16 * 1024 {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            "invalid SSH secret",
        ));
    }
    let mut bytes = zeroize::Zeroizing::new(vec![0; length]);
    socket.read_exact(&mut bytes)?;
    let value =
        zeroize::Zeroizing::new(String::from_utf8(std::mem::take(&mut *bytes)).map_err(|_| {
            std::io::Error::new(std::io::ErrorKind::InvalidData, "invalid SSH secret")
        })?);
    let sanitized = sanitize_askpass_output(&value)?;
    let mut stdout = std::io::stdout().lock();
    stdout.write_all(sanitized.as_bytes())?;
    stdout.write_all(b"\n")?;
    stdout.flush()
}

fn sanitize_askpass_output(value: &str) -> std::io::Result<zeroize::Zeroizing<String>> {
    let sanitized = zeroize::Zeroizing::new(value.replace(['\r', '\n'], ""));
    if sanitized.len() != value.len() {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            "invalid SSH secret",
        ));
    }
    Ok(sanitized)
}

#[cfg(test)]
mod tests {
    use super::sanitize_askpass_output;

    #[test]
    fn askpass_output_is_one_unchanged_line() {
        assert_eq!(
            sanitize_askpass_output("correct horse battery staple")
                .unwrap()
                .as_str(),
            "correct horse battery staple"
        );
        assert!(sanitize_askpass_output("forged\nentry").is_err());
        assert!(sanitize_askpass_output("forged\rentry").is_err());
    }
}
