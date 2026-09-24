//! Independent OpenSSH processes: each destination sees only its own credential.
use crate::{
    DriverError, ErrorKind, Result, Secret, SshAuthentication, SshJumpAuthentication, SshOptions,
    SshTunnel,
};
use crate::{ssh_process::OwnedChild, ssh_proxy::Relay};
use std::{
    collections::BTreeMap,
    pin::Pin,
    process::Stdio,
    sync::Arc,
    task::{Context, Poll},
    time::Duration,
};
use tokio::{
    io::{AsyncRead, AsyncWrite, ReadBuf},
    process::{ChildStdin, ChildStdout, Command},
};

pub fn validate_ssh_chain_authentication(
    settings: &SshTunnel,
    secret: Option<&Secret>,
    hops: &BTreeMap<String, Secret>,
) -> Result<()> {
    validate_ssh_chain_authentication_with_keys(settings, secret, hops, None, &BTreeMap::new())
}
pub fn validate_ssh_chain_authentication_with_keys(
    settings: &SshTunnel,
    secret: Option<&Secret>,
    hops: &BTreeMap<String, Secret>,
    private_key: Option<&Secret>,
    hop_keys: &BTreeMap<String, Secret>,
) -> Result<()> {
    settings.validate()?;
    if settings.options.share_tunnels && !cfg!(unix) {
        return Err(DriverError::new(
            ErrorKind::Unsupported,
            "SSH session sharing requires private Unix control sockets",
        ));
    }
    crate::ssh_identity::validate(settings.identity_source, private_key)?;
    if hop_keys.keys().any(|id| {
        !settings.options.jump_hosts.iter().any(|hop| {
            hop.id.as_ref() == Some(id) && hop.identity_source == crate::SshIdentitySource::Inline
        })
    }) {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "SSH private keys reference an unknown inline identity hop",
        ));
    }
    for hop in &settings.options.jump_hosts {
        crate::ssh_identity::validate(
            hop.identity_source,
            hop.id.as_ref().and_then(|id| hop_keys.get(id)),
        )?;
    }
    let mut target = settings.clone();
    target.options.jump_hosts.clear();
    crate::validate_ssh_secret(&target, secret)?;
    if hops.keys().any(|id| {
        !settings
            .options
            .jump_hosts
            .iter()
            .any(|hop| hop.id.as_ref() == Some(id) && hop.authentication.uses_secret())
    }) {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "SSH credentials reference an unknown credential hop",
        ));
    }
    for hop in &settings.options.jump_hosts {
        if !hop.authentication.uses_secret() {
            continue;
        }
        let destination = hop_settings(settings, hop);
        let secret = hop.id.as_ref().and_then(|id| hops.get(id));
        crate::validate_ssh_secret(&destination, secret)?;
    }
    if !cfg!(unix)
        && (settings.identity_source == crate::SshIdentitySource::Inline || !hop_keys.is_empty())
    {
        return Err(DriverError::new(
            ErrorKind::Unsupported,
            "Inline SSH identities require verified private file permissions on this platform",
        ));
    }
    Ok(())
}
pub(crate) fn hop_settings(target: &SshTunnel, hop: &crate::SshJumpHost) -> SshTunnel {
    SshTunnel {
        host: hop.host.clone(),
        port: hop.port,
        user: hop.user.clone(),
        identity_file: hop.identity_file.clone(),
        identity_source: hop.identity_source,
        authentication: match hop.authentication {
            SshJumpAuthentication::Configured | SshJumpAuthentication::Agent => {
                SshAuthentication::Agent
            }
            SshJumpAuthentication::Password => SshAuthentication::Password,
            SshJumpAuthentication::PublicKey => SshAuthentication::PublicKey,
        },
        options: Box::new(SshOptions {
            agent_socket: hop.agent_socket.clone(),
            known_hosts_file: hop.known_hosts_file.clone(),
            share_tunnels: target.options.share_tunnels,
            connect_timeout_seconds: target.options.connect_timeout_seconds,
            server_alive_interval_seconds: target.options.server_alive_interval_seconds,
            server_alive_count_max: target.options.server_alive_count_max,
            ..Default::default()
        }),
    }
}
fn legacy(settings: &SshTunnel, secret: Option<&Secret>, hops: &BTreeMap<String, Secret>) -> bool {
    !settings.options.share_tunnels
        && settings.identity_source == crate::SshIdentitySource::File
        && hops.is_empty()
        && settings.authentication != SshAuthentication::Password
        && secret.is_none_or(|secret| secret.expose().is_empty())
        && settings.options.jump_hosts.iter().all(|hop| {
            hop.authentication == SshJumpAuthentication::Configured
                && hop.identity_source == crate::SshIdentitySource::File
                && hop.identity_file.is_none()
                && hop.agent_socket.is_none()
                && hop.known_hosts_file.is_none()
        })
}
fn failure() -> DriverError {
    DriverError::new(
        ErrorKind::Connection,
        "Cannot establish isolated SSH forwarding",
    )
}
/// The host application must dispatch `run_ssh_askpass_if_requested` before its
/// normal entry point. This also handles the app-owned proxy byte relay.
pub struct SshForward {
    pub(crate) master: Option<Arc<crate::ssh_shared::Master>>,
    local: Option<Arc<crate::SshLocalForward>>,
    process: Option<OwnedChild>,
    input: ChildStdin,
    output: ChildStdout,
    broker: Option<Arc<crate::SshAskpass>>,
    route: Option<Arc<Relay>>,
    identity: Option<Arc<crate::ssh_identity::Identity>>,
}
impl SshForward {
    pub async fn open(
        settings: &SshTunnel,
        secret: Option<&Secret>,
        hops: &BTreeMap<String, Secret>,
        host: &str,
        port: u16,
    ) -> Result<Self> {
        Self::open_with_keys(settings, secret, hops, None, &BTreeMap::new(), host, port).await
    }
    pub async fn open_with_keys(
        settings: &SshTunnel,
        secret: Option<&Secret>,
        hops: &BTreeMap<String, Secret>,
        private_key: Option<&Secret>,
        hop_keys: &BTreeMap<String, Secret>,
        host: &str,
        port: u16,
    ) -> Result<Self> {
        let deadline = Duration::from_secs(u64::from(settings.options.connect_timeout_seconds));
        tokio::time::timeout(deadline, async {
            let local =
                if settings.options.local_host.is_some() || settings.options.local_port.is_some() {
                    Some(
                        crate::SshLocalForward::open(
                            settings,
                            secret,
                            hops,
                            private_key,
                            hop_keys,
                            host,
                            port,
                        )
                        .await?,
                    )
                } else {
                    None
                };
            let mut stream = Self::open_channel_with_keys(
                settings,
                secret,
                hops,
                private_key,
                hop_keys,
                host,
                port,
            )
            .await?;
            stream.local = local;
            Ok(stream)
        })
        .await
        .map_err(|_| DriverError::new(ErrorKind::Timeout, "SSH connection preparation timed out"))?
    }
    /// Open an additional channel on an already-owned listener without rebinding
    /// its local port (used by cancellation and the local relay itself).
    pub async fn open_channel_with_keys(
        settings: &SshTunnel,
        secret: Option<&Secret>,
        hops: &BTreeMap<String, Secret>,
        private_key: Option<&Secret>,
        hop_keys: &BTreeMap<String, Secret>,
        host: &str,
        port: u16,
    ) -> Result<Self> {
        validate_ssh_chain_authentication_with_keys(settings, secret, hops, private_key, hop_keys)?;
        let destination = settings.options.forwarding_destination(host, port)?;
        let secret = secret.filter(|secret| {
            settings.authentication != SshAuthentication::PublicKey || !secret.expose().is_empty()
        });
        let deadline = Duration::from_secs(u64::from(settings.options.connect_timeout_seconds));
        tokio::time::timeout(
            deadline,
            Self::prepare(settings, secret, hops, private_key, hop_keys, destination),
        )
        .await
        .map_err(|_| DriverError::new(ErrorKind::Timeout, "SSH connection preparation timed out"))?
    }
    async fn prepare(
        settings: &SshTunnel,
        secret: Option<&Secret>,
        hops: &BTreeMap<String, Secret>,
        private_key: Option<&Secret>,
        hop_keys: &BTreeMap<String, Secret>,
        destination: String,
    ) -> Result<Self> {
        if legacy(settings, secret, hops) {
            let mut command = Command::new("ssh");
            let broker =
                crate::configure_ssh_authentication(&mut command, settings, secret)?.map(Arc::new);
            command.args(["-p", &settings.port.to_string(), "-l", &settings.user]);
            if let Some(identity) = &settings.identity_file {
                command.arg("-i").arg(identity);
            }
            command
                .arg("-W")
                .arg(destination)
                .arg("--")
                .arg(crate::tcp_host(&settings.host)?);
            return Self::spawn(command, broker, None, None);
        }
        let destinations =
            Self::destinations(settings, secret, hops, private_key, hop_keys).await?;
        if settings.options.share_tunnels {
            let context = destinations
                .iter()
                .map(|node| node.context.clone())
                .collect::<Vec<_>>();
            let key = crate::ssh_context::key(
                settings,
                secret,
                hops,
                private_key,
                hop_keys,
                "session",
                &context,
            )?;
            return crate::ssh_shared::acquire(key, destinations)
                .await?
                .channel(&destination);
        }
        Self::forward_destinations(destinations, destination).await
    }
    async fn destinations(
        settings: &SshTunnel,
        secret: Option<&Secret>,
        hops: &BTreeMap<String, Secret>,
        private_key: Option<&Secret>,
        hop_keys: &BTreeMap<String, Secret>,
    ) -> Result<Vec<crate::ssh_command::Destination>> {
        let mut destinations = Vec::with_capacity(settings.options.jump_hosts.len() + 1);
        for (index, hop) in settings.options.jump_hosts.iter().enumerate() {
            let destination = hop_settings(settings, hop);
            let secret = hop
                .id
                .as_ref()
                .and_then(|id| hops.get(id))
                .filter(|secret| {
                    hop.authentication != SshJumpAuthentication::PublicKey
                        || !secret.expose().is_empty()
                });
            destinations.push(
                crate::ssh_command::resolve(
                    destination,
                    hop.id.as_ref().and_then(|id| hop_keys.get(id)),
                    secret,
                    hop.authentication == SshJumpAuthentication::Configured,
                    index > 0,
                )
                .await?,
            );
        }
        let mut target = settings.clone();
        target.options.jump_hosts.clear();
        destinations.push(
            crate::ssh_command::resolve(
                target,
                private_key,
                secret,
                false,
                !settings.options.jump_hosts.is_empty(),
            )
            .await?,
        );
        Ok(destinations)
    }
    /// Retire only the matching sharing registrations; existing healthy leases
    /// retain ownership of their master and listener until they close normally.
    pub async fn invalidate_shared_context(
        settings: &SshTunnel,
        secret: Option<&Secret>,
        hops: &BTreeMap<String, Secret>,
        private_key: Option<&Secret>,
        hop_keys: &BTreeMap<String, Secret>,
        host: &str,
        port: u16,
    ) -> Result<()> {
        if !settings.options.share_tunnels {
            return Ok(());
        }
        validate_ssh_chain_authentication_with_keys(settings, secret, hops, private_key, hop_keys)?;
        settings.options.forwarding_destination(host, port)?;
        let listener_key = crate::ssh_context::key(
            settings,
            secret,
            hops,
            private_key,
            hop_keys,
            "listener",
            &[host.as_bytes().to_vec(), port.to_le_bytes().to_vec()],
        )?;
        let secret = secret.filter(|secret| {
            settings.authentication != SshAuthentication::PublicKey || !secret.expose().is_empty()
        });
        tokio::time::timeout(
            Duration::from_secs(u64::from(settings.options.connect_timeout_seconds)),
            async {
                let destinations =
                    Self::destinations(settings, secret, hops, private_key, hop_keys).await?;
                let context = destinations
                    .iter()
                    .map(|node| node.context.clone())
                    .collect::<Vec<_>>();
                let key = crate::ssh_context::key(
                    settings,
                    secret,
                    hops,
                    private_key,
                    hop_keys,
                    "session",
                    &context,
                )?;
                crate::ssh_shared::invalidate(key)?;
                crate::ssh_local::invalidate(listener_key)
            },
        )
        .await
        .map_err(|_| DriverError::new(ErrorKind::Timeout, "SSH recovery preparation timed out"))?
    }
    pub(crate) async fn open_prefix(
        settings: &SshTunnel,
        hops: &BTreeMap<String, Secret>,
        hop_keys: &BTreeMap<String, Secret>,
        count: usize,
        destination: String,
    ) -> Result<Self> {
        let mut destinations = Vec::new();
        for (index, hop) in settings.options.jump_hosts.iter().take(count).enumerate() {
            let destination = hop_settings(settings, hop);
            let secret = hop.id.as_ref().and_then(|id| hops.get(id));
            let key = hop.id.as_ref().and_then(|id| hop_keys.get(id));
            destinations.push(
                crate::ssh_command::resolve(
                    destination,
                    key,
                    secret,
                    hop.authentication == SshJumpAuthentication::Configured,
                    index > 0,
                )
                .await?,
            );
        }
        Self::forward_destinations(destinations, destination).await
    }
    pub(crate) async fn forward_destinations(
        destinations: Vec<crate::ssh_command::Destination>,
        destination: String,
    ) -> Result<Self> {
        let endpoints: Vec<_> = destinations
            .iter()
            .map(|node| crate::ssh_command::endpoint(&node.hostname, node.port))
            .collect();
        let count = destinations.len();
        let mut route: Option<Arc<Relay>> = None;
        for (index, mut node) in destinations.into_iter().enumerate() {
            // Pin the resolved remote address, not a loopback replacement. Original
            // Host/Match originalhost and HostKeyAlias/certificate semantics survive.
            let proxy = match &route {
                Some(route) => format!("ProxyCommand={}", route.configure(&mut node.command)?),
                None => "ProxyCommand=none".into(),
            };
            node.command
                .arg("-o")
                .arg(format!("HostName={}", node.hostname.replace('%', "%%")))
                .arg("-p")
                .arg(node.port.to_string())
                .arg("-o")
                .arg(proxy)
                .args([
                    "-o",
                    "ProxyJump=none",
                    "-o",
                    "ProxyUseFdpass=no",
                    "-o",
                    "ClearAllForwardings=yes",
                ])
                .arg("-W")
                .arg(endpoints.get(index + 1).unwrap_or(&destination))
                .arg("--")
                .arg(crate::tcp_host(&node.settings.host)?);
            let stream = Self::spawn(node.command, node.broker, route, node.identity)?;
            if index + 1 == count {
                return Ok(stream);
            }
            route = Some(Relay::new(stream).await?);
        }
        Err(failure())
    }
    pub(crate) fn spawn(
        mut command: Command,
        broker: Option<Arc<crate::SshAskpass>>,
        route: Option<Arc<Relay>>,
        identity: Option<Arc<crate::ssh_identity::Identity>>,
    ) -> Result<Self> {
        command
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::null());
        let mut process = OwnedChild::spawn(&mut command).map_err(|_| failure())?;
        let input = process.child().stdin.take().ok_or_else(failure)?;
        let output = process.child().stdout.take().ok_or_else(failure)?;
        Ok(Self {
            master: None,
            local: None,
            process: Some(process),
            input,
            output,
            broker,
            route,
            identity,
        })
    }
    pub fn take_process(&mut self) -> SshProcess {
        SshProcess {
            process: self.process.take(),
            _master: self.master.clone(),
            _local: self.local.clone(),
            _broker: self.broker.clone(),
            _route: self.route.clone(),
            _identity: self.identity.clone(),
        }
    }
}
pub struct SshProcess {
    _local: Option<Arc<crate::SshLocalForward>>,
    _master: Option<Arc<crate::ssh_shared::Master>>,
    process: Option<OwnedChild>,
    _broker: Option<Arc<crate::SshAskpass>>,
    _route: Option<Arc<Relay>>,
    _identity: Option<Arc<crate::ssh_identity::Identity>>,
}
impl SshProcess {
    pub async fn finish(&mut self) -> Result<()> {
        let status = self
            .process
            .as_mut()
            .ok_or_else(failure)?
            .wait()
            .await
            .map_err(|_| failure())?;
        if status.success() {
            Ok(())
        } else {
            Err(failure())
        }
    }
}
impl AsyncRead for SshForward {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buffer: &mut ReadBuf<'_>,
    ) -> Poll<std::io::Result<()>> {
        Pin::new(&mut self.output).poll_read(cx, buffer)
    }
}
impl AsyncWrite for SshForward {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buffer: &[u8],
    ) -> Poll<std::io::Result<usize>> {
        Pin::new(&mut self.input).poll_write(cx, buffer)
    }
    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<std::io::Result<()>> {
        Pin::new(&mut self.input).poll_flush(cx)
    }
    fn poll_shutdown(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<std::io::Result<()>> {
        Pin::new(&mut self.input).poll_shutdown(cx)
    }
}
