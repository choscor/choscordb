//! Loopback relay to OpenSSH stdio forwarding. The MySQL hostname remains the
//! original endpoint for TLS verification; only its resolved address is changed.
use choscordb_driver_api::{
    DriverError, ErrorKind, Result, Secret, SshAuthentication, SshTunnel,
    configure_ssh_authentication,
};
use std::{process::Stdio, sync::Arc};
use tokio::{
    net::TcpListener,
    process::Command,
    task::{JoinHandle, JoinSet},
};

pub(crate) struct Tunnel {
    port: u16,
    worker: JoinHandle<()>,
}
impl Tunnel {
    pub(crate) fn port(&self) -> u16 {
        self.port
    }
    pub(crate) async fn open(
        settings: &SshTunnel,
        secret: Option<Secret>,
        host: &str,
        port: u16,
    ) -> Result<Arc<Self>> {
        settings.validate()?;
        if settings.authentication == SshAuthentication::Password && secret.is_none() {
            return Err(DriverError::new(
                ErrorKind::Authentication,
                "SSH password is required",
            ));
        }
        if host.is_empty() || host.chars().any(|c| c.is_whitespace() || c.is_control()) || port == 0
        {
            return Err(DriverError::new(
                ErrorKind::InvalidInput,
                "Invalid SSH database endpoint",
            ));
        }
        let listener = TcpListener::bind((std::net::Ipv4Addr::LOCALHOST, 0))
            .await
            .map_err(|_| DriverError::new(ErrorKind::Connection, "Cannot bind MySQL SSH relay"))?;
        let local_port = listener
            .local_addr()
            .map_err(|_| DriverError::new(ErrorKind::Connection, "Cannot inspect MySQL SSH relay"))?
            .port();
        let settings = settings.clone();
        let secret = secret.map(Arc::new);
        let destination = if host.contains(':') {
            format!("[{host}]:{port}")
        } else {
            format!("{host}:{port}")
        };
        let worker = tokio::spawn(async move {
            let mut clients = JoinSet::new();
            loop {
                tokio::select! {
                    result = listener.accept() => {
                        let Ok((socket, _)) = result else { break; };
                        // SQL, object reads, and cancellation each use independent transports.
                        if clients.len() >= 32 { drop(socket); continue; }
                        let settings = settings.clone();
                        let destination = destination.clone();
                        let secret = secret.clone();
                        clients.spawn(async move {
                            let mut command = Command::new("ssh");
                            command.args(["-T", "-o", "StrictHostKeyChecking=yes",
                                "-o", "ConnectTimeout=15", "-o", "ConnectionAttempts=1", "-o", "ExitOnForwardFailure=yes",
                                "-o", "ControlMaster=no", "-o", "ControlPath=none", "-o", "PermitLocalCommand=no"])
                                .arg("-p").arg(settings.port.to_string()).arg("-l").arg(&settings.user);
                            let Ok(_askpass) = configure_ssh_authentication(&mut command, &settings, secret.as_deref()) else { return; };
                            if let Some(identity) = &settings.identity_file { command.arg("-i").arg(identity); }
                            command.arg("-W").arg(destination).arg("--").arg(&settings.host)
                                .stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::null()).kill_on_drop(true);
                            let Ok(mut child) = command.spawn() else { return; };
                            let Some(mut input) = child.stdin.take() else { return; };
                            let Some(mut output) = child.stdout.take() else { return; };
                            let (mut read, mut write) = socket.into_split();
                            tokio::select! {
                                _ = tokio::io::copy(&mut read, &mut input) => {},
                                _ = tokio::io::copy(&mut output, &mut write) => {},
                            }
                            let _ = child.kill().await;
                            let _ = child.wait().await;
                        });
                    },
                    _ = clients.join_next(), if !clients.is_empty() => {},
                }
            }
        });
        Ok(Arc::new(Self {
            port: local_port,
            worker,
        }))
    }
}
impl Drop for Tunnel {
    fn drop(&mut self) {
        self.worker.abort();
    }
}
