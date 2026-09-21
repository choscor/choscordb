use crate::{DriverError, ErrorKind, Result, Secret, SshAuthentication, SshTunnel};
use std::io::{Read, Write};
use std::net::SocketAddr;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

const ADDRESS: &str = "CHOSCORDB_SSH_ASKPASS_ADDRESS";
const TOKEN: &str = "CHOSCORDB_SSH_ASKPASS_TOKEN";
const MODE: &str = "CHOSCORDB_SSH_ASKPASS_MODE";

pub struct SshAskpass {
    address: SocketAddr,
    token: String,
    worker: tokio::task::JoinHandle<()>,
}

impl SshAskpass {
    pub fn new(secret: Secret) -> std::io::Result<Self> {
        let listener = std::net::TcpListener::bind((std::net::Ipv4Addr::LOCALHOST, 0))?;
        listener.set_nonblocking(true)?;
        let listener = tokio::net::TcpListener::from_std(listener)?;
        let address = listener.local_addr()?;
        let token = uuid::Uuid::new_v4().to_string();
        let expected = token.clone();
        let worker = tokio::spawn(async move {
            let deadline = tokio::time::sleep(std::time::Duration::from_secs(30));
            tokio::pin!(deadline);
            loop {
                let accepted = tokio::select! {
                    _ = &mut deadline => break,
                    accepted = listener.accept() => accepted,
                };
                let Ok((mut socket, peer)) = accepted else {
                    break;
                };
                if !peer.ip().is_loopback() {
                    continue;
                }
                let mut length = [0_u8; 2];
                if socket.read_exact(&mut length).await.is_err() {
                    continue;
                }
                let length = u16::from_be_bytes(length) as usize;
                if length == 0 || length > 128 {
                    continue;
                }
                let mut supplied = vec![0; length];
                if socket.read_exact(&mut supplied).await.is_err()
                    || supplied.as_slice() != expected.as_bytes()
                {
                    continue;
                }
                let bytes = secret.expose().as_bytes();
                let Ok(length) = u32::try_from(bytes.len()) else {
                    continue;
                };
                if socket.write_all(&length.to_be_bytes()).await.is_ok() {
                    let _ = socket.write_all(bytes).await;
                }
            }
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

pub fn configure_ssh_authentication(
    command: &mut tokio::process::Command,
    settings: &SshTunnel,
    secret: Option<&Secret>,
) -> Result<Option<SshAskpass>> {
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
            if secret.is_none_or(|secret| secret.expose().is_empty()) {
                return Err(DriverError::new(
                    ErrorKind::Authentication,
                    "SSH password is required",
                ));
            }
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
    if secret.expose().contains(['\0', '\r', '\n']) {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "Invalid SSH credential",
        ));
    }
    let askpass = SshAskpass::new(Secret::new(secret.expose())).map_err(|_| {
        DriverError::new(ErrorKind::Connection, "Cannot prepare SSH authentication")
    })?;
    askpass.configure(command).map_err(|_| {
        DriverError::new(ErrorKind::Connection, "Cannot prepare SSH authentication")
    })?;
    Ok(Some(askpass))
}

pub async fn request_ssh_secret(address: SocketAddr, token: &str) -> std::io::Result<Secret> {
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
    let mut socket = std::net::TcpStream::connect(address)?;
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
