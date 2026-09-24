//! A capability-authenticated byte relay; it never handles SSH authentication.
use crate::{DriverError, ErrorKind, Result};
use std::{
    io::{Read, Write},
    net::SocketAddr,
    path::Path,
    sync::Arc,
    time::Duration,
};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::TcpListener,
    task::JoinHandle,
};
const MODE: &str = "--choscordb-ssh-proxy";
const CLEAN_MODE: &str = "--choscordb-ssh-proxy-clean";
const STREAM_MODE: &str = "--choscordb-ssh-proxy-stream";
pub(crate) const TOKEN: &str = "CHOSCORDB_SSH_PROXY_TOKEN";
const ENVIRONMENT: [&str; 5] = [
    "SSH_ASKPASS",
    "SSH_ASKPASS_REQUIRE",
    "CHOSCORDB_SSH_ASKPASS_ADDRESS",
    "CHOSCORDB_SSH_ASKPASS_TOKEN",
    "CHOSCORDB_SSH_ASKPASS_MODE",
];

pub(crate) struct Relay {
    address: SocketAddr,
    token: String,
    worker: JoinHandle<()>,
}
impl Relay {
    pub(crate) async fn new(mut stream: crate::SshForward) -> Result<Arc<Self>> {
        let listener = TcpListener::bind((std::net::Ipv4Addr::LOCALHOST, 0))
            .await
            .map_err(|_| failure())?;
        let address = listener.local_addr().map_err(|_| failure())?;
        let token = uuid::Uuid::new_v4().to_string();
        let expected = token.clone();
        let worker = tokio::spawn(async move {
            // Wrong capabilities never consume the single authorized byte stream.
            let mut attempts = tokio::task::JoinSet::new();
            let mut socket = loop {
                tokio::select! {
                    accepted=listener.accept(),if attempts.len()<16=>{
                        let Ok((mut socket,peer))=accepted else {return;};
                        if !peer.ip().is_loopback(){continue;}
                        let expected=expected.clone();
                        attempts.spawn(async move {
                            tokio::time::timeout(Duration::from_secs(2),async {
                                let length=socket.read_u16().await.ok()? as usize;
                                if length!=expected.len(){return None;}
                                let mut token=vec![0;length];socket.read_exact(&mut token).await.ok()?;
                                if token!=expected.as_bytes(){return None;}
                                socket.write_u8(1).await.ok()?;Some(socket)
                            }).await.ok().flatten()
                        });
                    }
                    result=attempts.join_next(),if !attempts.is_empty()=>{
                        if let Some(Ok(Some(socket)))=result {break socket;}
                    }
                }
            };
            drop(listener);
            drop(attempts);
            let _ = tokio::io::copy_bidirectional(&mut socket, &mut stream).await;
        });
        Ok(Arc::new(Self {
            address,
            token,
            worker,
        }))
    }
    pub(crate) fn configure(&self, command: &mut tokio::process::Command) -> Result<String> {
        command.env(TOKEN, &self.token);
        proxy_command(
            &std::env::current_exe().map_err(|_| failure())?,
            self.address,
        )
    }
}
impl Drop for Relay {
    fn drop(&mut self) {
        self.worker.abort();
    }
}
fn failure() -> DriverError {
    DriverError::new(
        ErrorKind::Connection,
        "Cannot prepare isolated SSH hop relay",
    )
}
fn proxy_command(executable: &Path, address: SocketAddr) -> Result<String> {
    let executable = executable
        .to_str()
        .filter(|value| !value.chars().any(char::is_control))
        .ok_or_else(failure)?;
    // OpenSSH expands percent tokens before invoking the user's shell.
    #[cfg(unix)]
    let quoted = format!("'{}'", executable.replace('%', "%%").replace('\'', "'\\''"));
    #[cfg(not(unix))]
    let quoted = {
        if executable.contains(['"', '%']) {
            return Err(failure());
        }
        format!("\"{executable}\"")
    };
    Ok(format!("{quoted} {MODE} {address}"))
}
/// Dispatch before askpass mode: the first helper only re-execs with broker
/// environment removed. No application or SSH bytes are read before that step.
pub(crate) fn run_if_requested() -> Option<i32> {
    let args: Vec<_> = std::env::args_os().collect();
    let mode = args.get(1)?.to_str()?;
    if ![MODE, CLEAN_MODE, STREAM_MODE].contains(&mode) {
        return None;
    }
    if mode == STREAM_MODE {
        if args.len() != 2
            || ENVIRONMENT
                .iter()
                .chain([&TOKEN])
                .any(|key| std::env::var_os(key).is_some())
        {
            return Some(1);
        }
        #[cfg(unix)]
        {
            use std::os::fd::AsFd;
            return Some(
                std::io::stderr()
                    .as_fd()
                    .try_clone_to_owned()
                    .and_then(|fd| relay(std::net::TcpStream::from(fd)))
                    .map_or(1, |()| 0),
            );
        }
        #[cfg(not(unix))]
        return Some(1);
    }
    if args.len() != 3 {
        return Some(1);
    }
    if mode == MODE {
        let Ok(executable) = std::env::current_exe() else {
            return Some(1);
        };
        let mut command = std::process::Command::new(executable);
        command.arg(CLEAN_MODE).args(&args[2..]);
        for key in ENVIRONMENT {
            command.env_remove(key);
        }
        #[cfg(unix)]
        {
            use std::os::unix::process::CommandExt;
            let _ = command.exec();
            Some(1)
        }
        #[cfg(not(unix))]
        {
            Some(
                command
                    .status()
                    .ok()
                    .and_then(|status| status.code())
                    .unwrap_or(1),
            )
        }
    } else {
        if ENVIRONMENT
            .iter()
            .any(|key| std::env::var_os(key).is_some())
        {
            return Some(1);
        }
        let Ok(socket) = authenticate(&args[2]) else {
            return Some(1);
        };
        #[cfg(unix)]
        {
            use std::os::{fd::OwnedFd, unix::process::CommandExt};
            let Ok(executable) = std::env::current_exe() else {
                return Some(1);
            };
            // Socket ownership crosses exec through a standard descriptor; no raw-fd
            // ownership reconstruction or unsafe environment mutation is needed.
            let mut command = std::process::Command::new(executable);
            command
                .arg(STREAM_MODE)
                .env_remove(TOKEN)
                .stderr(std::process::Stdio::from(OwnedFd::from(socket)));
            let _ = command.exec();
            Some(1)
        }
        #[cfg(not(unix))]
        {
            Some(relay(socket).map_or(1, |()| 0))
        }
    }
}
fn authenticate(address: &std::ffi::OsStr) -> std::io::Result<std::net::TcpStream> {
    let invalid = || {
        std::io::Error::new(
            std::io::ErrorKind::InvalidInput,
            "invalid SSH relay request",
        )
    };
    let address: SocketAddr = address
        .to_str()
        .ok_or_else(invalid)?
        .parse()
        .map_err(|_| invalid())?;
    let token = zeroize::Zeroizing::new(std::env::var(TOKEN).map_err(|_| invalid())?);
    if !address.ip().is_loopback()
        || address.port() == 0
        || token.len() != 36
        || !token
            .bytes()
            .all(|byte| byte.is_ascii_hexdigit() || byte == b'-')
    {
        return Err(invalid());
    }
    let mut socket = std::net::TcpStream::connect_timeout(&address, Duration::from_secs(2))?;
    socket.set_read_timeout(Some(Duration::from_secs(2)))?;
    socket.set_write_timeout(Some(Duration::from_secs(2)))?;
    socket.write_all(&(token.len() as u16).to_be_bytes())?;
    socket.write_all(token.as_bytes())?;
    let mut ack = [0];
    socket.read_exact(&mut ack)?;
    if ack != [1] {
        return Err(invalid());
    }
    socket.set_read_timeout(None)?;
    socket.set_write_timeout(None)?;
    Ok(socket)
}
fn relay(mut socket: std::net::TcpStream) -> std::io::Result<()> {
    if !socket.peer_addr()?.ip().is_loopback() {
        return Err(std::io::Error::other("invalid SSH relay peer"));
    }
    let mut writer = socket.try_clone()?;
    std::thread::spawn(move || {
        let _ = std::io::copy(&mut std::io::stdin().lock(), &mut writer);
        let _ = writer.shutdown(std::net::Shutdown::Write);
    });
    // SSH packets need not contain a newline: flush every write to line-buffered stdout.
    let mut output = std::io::stdout().lock();
    let mut bytes = [0; 16 * 1024];
    loop {
        let count = socket.read(&mut bytes)?;
        if count == 0 {
            return output.flush();
        }
        output.write_all(&bytes[..count])?;
        output.flush()?;
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[tokio::test]
    async fn relay_capability_is_only_in_the_owning_child_environment() {
        let relay = Relay {
            address: "127.0.0.1:4242".parse().unwrap(),
            token: "private-relay-capability".into(),
            worker: tokio::spawn(std::future::pending()),
        };
        let mut command = tokio::process::Command::new("ssh");
        let proxy = relay.configure(&mut command).unwrap();
        command.arg("-o").arg(&proxy);
        assert!(!proxy.contains(&relay.token));
        assert!(
            !command
                .as_std()
                .get_args()
                .any(|arg| arg.to_string_lossy().contains(&relay.token))
        );
        assert!(command.as_std().get_envs().any(
            |(name, value)| name == TOKEN && value == Some(std::ffi::OsStr::new(&relay.token))
        ));
        crate::ssh_askpass::clear_ssh_askpass_environment(&mut command);
        assert!(
            command
                .as_std()
                .get_envs()
                .any(|(name, value)| name == TOKEN && value.is_none())
        );
    }
    #[test]
    #[cfg(unix)]
    fn executable_is_shell_quoted_and_ssh_percent_escaped() {
        let command = proxy_command(
            Path::new("/tmp/helper ' with%space/app"),
            "127.0.0.1:42".parse().unwrap(),
        )
        .unwrap();
        assert_eq!(
            command,
            "'/tmp/helper '\\'' with%%space/app' --choscordb-ssh-proxy 127.0.0.1:42"
        );
    }
}
