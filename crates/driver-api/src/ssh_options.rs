use crate::{DriverError, ErrorKind, Result, tcp_host};
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum SshAuthentication {
    Agent,
    #[default]
    PublicKey,
    Password,
}
/// Location of a public-key identity. Inline bytes are supplied separately as secrets.
#[derive(Clone, Copy, Debug, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum SshIdentitySource {
    #[default]
    File,
    Inline,
}
impl SshIdentitySource {
    pub fn is_file(&self) -> bool {
        *self == Self::File
    }
    fn invalid(&self, public_key: bool, path: &Option<String>) -> bool {
        if public_key {
            match self {
                Self::File => path.is_none(),
                Self::Inline => path.is_some(),
            }
        } else {
            !self.is_file() || path.is_some()
        }
    }
}
/// SSH authentication uses OpenSSH with an explicit authentication method.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct SshTunnel {
    pub host: String,
    pub port: u16,
    pub user: String,
    pub authentication: SshAuthentication,
    pub identity_file: Option<String>,
    #[serde(default, skip_serializing_if = "SshIdentitySource::is_file")]
    pub identity_source: SshIdentitySource,
    #[serde(skip_serializing_if = "SshOptions::is_default")]
    pub options: Box<SshOptions>,
}
impl SshTunnel {
    pub fn validate(&self) -> Result<()> {
        tcp_host(&self.host)?;
        self.options.validate()?;
        if self.port == 0
            || [&self.host, &self.user].iter().any(|value| {
                value.is_empty()
                    || value.len() > 16 * 1024
                    || value.starts_with('-')
                    || value.chars().any(|c| c.is_whitespace() || c.is_control())
            })
            || self.identity_file.as_ref().is_some_and(|path| {
                path.trim().is_empty() || path.len() > 16 * 1024 || path.contains('\0')
            })
            || self.identity_source.invalid(
                self.authentication == SshAuthentication::PublicKey,
                &self.identity_file,
            )
        {
            return Err(DriverError::new(
                ErrorKind::InvalidInput,
                "Invalid SSH tunnel settings",
            ));
        }
        Ok(())
    }
}
impl<'de> Deserialize<'de> for SshTunnel {
    fn deserialize<D: serde::Deserializer<'de>>(
        deserializer: D,
    ) -> std::result::Result<Self, D::Error> {
        #[derive(Deserialize)]
        #[serde(deny_unknown_fields)]
        struct Stored {
            host: String,
            port: u16,
            user: String,
            authentication: Option<SshAuthentication>,
            identity_file: Option<String>,
            #[serde(default)]
            identity_source: SshIdentitySource,
            #[serde(default)]
            options: Box<SshOptions>,
        }
        let stored = Stored::deserialize(deserializer)?;
        let authentication = stored.authentication.unwrap_or_else(|| {
            if stored.identity_file.is_some() || stored.identity_source == SshIdentitySource::Inline
            {
                SshAuthentication::PublicKey
            } else {
                SshAuthentication::Agent
            }
        });
        Ok(Self {
            host: stored.host,
            port: stored.port,
            user: stored.user,
            authentication,
            identity_file: stored.identity_file,
            identity_source: stored.identity_source,
            options: stored.options,
        })
    }
}

/// Advanced OpenSSH transport settings. Jump hosts authenticate independently;
/// the target's password or private key is never forwarded to them.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(default, deny_unknown_fields)]
pub struct SshOptions {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub local_host: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub local_port: Option<u16>,
    #[serde(skip_serializing_if = "is_false")]
    pub share_tunnels: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub remote_host: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub remote_port: Option<u16>,
    pub connect_timeout_seconds: u32,
    pub server_alive_interval_seconds: u32,
    pub server_alive_count_max: u32,
    pub agent_socket: Option<String>,
    pub known_hosts_file: Option<String>,
    pub jump_hosts: Vec<SshJumpHost>,
}
impl Default for SshOptions {
    fn default() -> Self {
        Self {
            local_host: None,
            local_port: None,
            share_tunnels: false,
            remote_host: None,
            remote_port: None,
            connect_timeout_seconds: 15,
            server_alive_interval_seconds: 0,
            server_alive_count_max: 3,
            agent_socket: None,
            known_hosts_file: None,
            jump_hosts: Vec::new(),
        }
    }
}
#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum SshJumpAuthentication {
    #[default]
    Configured,
    Agent,
    Password,
    PublicKey,
}
impl SshJumpAuthentication {
    pub fn is_configured(&self) -> bool {
        matches!(self, Self::Configured)
    }
    pub fn uses_secret(&self) -> bool {
        matches!(self, Self::Password | Self::PublicKey)
    }
}
#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct SshJumpHost {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub id: Option<String>,
    #[serde(default, skip_serializing_if = "SshJumpAuthentication::is_configured")]
    pub authentication: SshJumpAuthentication,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub identity_file: Option<String>,
    #[serde(default, skip_serializing_if = "SshIdentitySource::is_file")]
    pub identity_source: SshIdentitySource,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub agent_socket: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub known_hosts_file: Option<String>,
    pub host: String,
    pub port: u16,
    pub user: String,
}

fn is_false(value: &bool) -> bool {
    !value
}
impl SshOptions {
    pub fn local_address(&self) -> Result<std::net::SocketAddr> {
        let address = match self.local_host.as_deref() {
            None => std::net::IpAddr::V4(std::net::Ipv4Addr::LOCALHOST),
            Some(host) => crate::tcp_host(host)?.parse().map_err(|_| {
                DriverError::new(
                    ErrorKind::InvalidInput,
                    "SSH local bind address must be a literal IPv4 or IPv6 address",
                )
            })?,
        };
        Ok(std::net::SocketAddr::new(
            address,
            self.local_port.unwrap_or(0),
        ))
    }
    /// Address resolved by the SSH server. The original database host remains
    /// the native TLS verification name and is never replaced by this value.
    pub fn forwarding_destination(&self, host: &str, port: u16) -> Result<String> {
        self.validate()?;
        let host = tcp_host(self.remote_host.as_deref().unwrap_or(host))?;
        let port = self.remote_port.unwrap_or(port);
        if port == 0 {
            return Err(DriverError::new(
                ErrorKind::InvalidInput,
                "Invalid SSH forwarding port",
            ));
        }
        Ok(if host.contains(':') {
            format!("[{host}]:{port}")
        } else {
            format!("{host}:{port}")
        })
    }
    pub fn is_default(&self) -> bool {
        self == &Self::default()
    }
    pub fn validate(&self) -> Result<()> {
        let invalid = || DriverError::new(ErrorKind::InvalidInput, "Invalid advanced SSH settings");
        self.local_address()?;
        if !(1..=300).contains(&self.connect_timeout_seconds)
            || self.server_alive_interval_seconds > 86_400
            || !(1..=100).contains(&self.server_alive_count_max)
            || self.jump_hosts.len() > 5
            || [&self.agent_socket, &self.known_hosts_file]
                .into_iter()
                .flatten()
                .any(|path| {
                    path.trim().is_empty()
                        || path.len() > 16 * 1024
                        || path.chars().any(char::is_control)
                        || path.contains(['"', '\\'])
                })
        {
            return Err(invalid());
        }
        if let Some(host) = &self.remote_host {
            tcp_host(host)?;
        }
        if self.remote_port == Some(0) {
            return Err(invalid());
        }
        let mut ids = std::collections::BTreeSet::new();
        for jump in &self.jump_hosts {
            tcp_host(&jump.host)?;
            if jump.id.as_ref().is_some_and(|id| {
                id.is_empty()
                    || id.len() > 64
                    || !id
                        .bytes()
                        .all(|byte| byte.is_ascii_alphanumeric() || b"_-".contains(&byte))
                    || !ids.insert(id)
            }) || (jump.authentication.uses_secret() && jump.id.is_none())
                || jump.identity_source.invalid(
                    jump.authentication == SshJumpAuthentication::PublicKey,
                    &jump.identity_file,
                )
                || [
                    &jump.identity_file,
                    &jump.agent_socket,
                    &jump.known_hosts_file,
                ]
                .into_iter()
                .flatten()
                .any(|path| {
                    path.trim().is_empty()
                        || path.len() > 16 * 1024
                        || path.chars().any(char::is_control)
                        || path.contains(['"', '\\'])
                })
            {
                return Err(invalid());
            }
            if jump.port == 0
                || jump
                    .host
                    .chars()
                    .any(|c| !c.is_ascii_alphanumeric() && !".:-%_[]".contains(c))
                || jump.user.is_empty()
                || jump.user.len() > 16 * 1024
                || jump.user.starts_with('-')
                || jump
                    .user
                    .chars()
                    .any(|c| !c.is_ascii_alphanumeric() && !"_.-".contains(c))
            {
                return Err(invalid());
            }
        }
        Ok(())
    }
}

/// Apply options common to PostgreSQL stdio forwarding and the MySQL relay.
/// `-J` sessions intentionally obtain authentication and trust policy from the
/// user's OpenSSH configuration. These target options are not jump-host options.
pub(crate) fn configure_ssh_transport(
    command: &mut tokio::process::Command,
    settings: &SshTunnel,
) -> Result<()> {
    settings.validate()?;
    let options = &settings.options;
    if options.jump_hosts.iter().any(|hop| {
        hop.authentication != SshJumpAuthentication::Configured
            || hop.identity_file.is_some()
            || hop.agent_socket.is_some()
            || hop.known_hosts_file.is_some()
    }) {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "Independent SSH hop settings require isolated forwarding",
        ));
    }
    command.args([
        "-T",
        "-o",
        "StrictHostKeyChecking=yes",
        "-o",
        "NoHostAuthenticationForLocalhost=no",
        "-o",
        "ConnectionAttempts=1",
        "-o",
        "ExitOnForwardFailure=yes",
        "-o",
        "ControlMaster=no",
        "-o",
        "ControlPath=none",
        "-o",
        "PermitLocalCommand=no",
    ]);
    for setting in [
        format!("ConnectTimeout={}", options.connect_timeout_seconds),
        format!(
            "ServerAliveInterval={}",
            options.server_alive_interval_seconds
        ),
        format!("ServerAliveCountMax={}", options.server_alive_count_max),
    ] {
        command.arg("-o").arg(setting);
    }
    if let Some(path) = &options.agent_socket {
        command.arg("-o").arg(format!("IdentityAgent=\"{path}\""));
    }
    if let Some(path) = &options.known_hosts_file {
        command
            .arg("-o")
            .arg(format!("UserKnownHostsFile=\"{path}\""));
    }
    if !options.jump_hosts.is_empty() {
        let jumps = options
            .jump_hosts
            .iter()
            .map(|jump| {
                let host = tcp_host(&jump.host).expect("validated jump host");
                let host = if host.contains(':') {
                    format!("[{host}]")
                } else {
                    host.to_owned()
                };
                format!("{}@{}:{}", jump.user, host, jump.port)
            })
            .collect::<Vec<_>>()
            .join(",");
        command.arg("-J").arg(jumps);
    }
    Ok(())
}
