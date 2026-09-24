use crate::{DriverError, ErrorKind, Result, SshTunnel};
use serde::{Deserialize, Serialize};
use std::time::Duration;

pub const MIN_PAGE_SIZE: u32 = 100;
pub const MAX_PAGE_SIZE: u32 = 10_000;
pub const DEFAULT_PAGE_SIZE: u32 = 1_000;

pub fn validate_page_size(size: u32) -> bool {
    (MIN_PAGE_SIZE..=MAX_PAGE_SIZE).contains(&size)
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
pub struct PageSize(u32);
impl PageSize {
    pub fn new(size: u32) -> Result<Self> {
        if validate_page_size(size) {
            Ok(Self(size))
        } else {
            Err(DriverError::new(
                ErrorKind::InvalidInput,
                format!("Page size must be between {MIN_PAGE_SIZE} and {MAX_PAGE_SIZE}"),
            ))
        }
    }
    pub fn get(self) -> u32 {
        self.0
    }
}
impl Default for PageSize {
    fn default() -> Self {
        Self(DEFAULT_PAGE_SIZE)
    }
}
impl<'de> Deserialize<'de> for PageSize {
    fn deserialize<D: serde::Deserializer<'de>>(
        deserializer: D,
    ) -> std::result::Result<Self, D::Error> {
        Self::new(u32::deserialize(deserializer)?).map_err(serde::de::Error::custom)
    }
}
#[derive(Clone, Debug)]
pub struct QueryOptions {
    pub page_size: PageSize,
    pub timeout: Option<Duration>,
    pub auto_commit: bool,
}
impl Default for QueryOptions {
    fn default() -> Self {
        Self {
            page_size: PageSize::default(),
            timeout: None,
            auto_commit: true,
        }
    }
}
#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq, Eq)]
pub enum TlsMode {
    Disable,
    Prefer,
    Require,
    VerifyCa,
    #[default]
    VerifyFull,
}
/// Secret bytes are deliberately excluded from Debug and serialization.
pub struct Secret(zeroize::Zeroizing<String>);
impl Secret {
    pub fn new(value: impl Into<String>) -> Self {
        Self(zeroize::Zeroizing::new(value.into()))
    }
    pub fn expose(&self) -> &str {
        &self.0
    }
}
impl std::fmt::Debug for Secret {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("[REDACTED]")
    }
}
/// Validate a TCP hostname or IP literal, accepting brackets around IPv6 input.
/// Host and port are separate fields; URLs, userinfo and socket paths are invalid.
pub fn tcp_host(host: &str) -> Result<&str> {
    let invalid = || {
        DriverError::new(
            ErrorKind::InvalidInput,
            "Enter a hostname or IP address without a URL, username, or port",
        )
    };
    if host.is_empty()
        || host.len() > 16 * 1024
        || host.starts_with('-')
        || host.chars().any(|c| c.is_whitespace() || c.is_control())
        || host.contains(['/', '\\', '@', '?', '#'])
    {
        return Err(invalid());
    }
    let address = if host.starts_with('[') && host.ends_with(']') {
        &host[1..host.len() - 1]
    } else {
        host
    };
    if host.contains(['[', ']']) || address.contains(':') {
        // A scope identifier is meaningful for link-local IPv6 endpoints.
        let (ip, scope) = address
            .split_once('%')
            .map_or((address, None), |(ip, scope)| (ip, Some(scope)));
        if ip.parse::<std::net::Ipv6Addr>().is_err()
            || scope.is_some_and(|scope| {
                scope.is_empty()
                    || !scope
                        .chars()
                        .all(|c| c.is_ascii_alphanumeric() || "_.-".contains(c))
            })
        {
            return Err(invalid());
        }
    }
    Ok(address)
}

#[derive(Debug)]
pub struct TlsIdentity {
    pub path: std::path::PathBuf,
    pub password: Option<Secret>,
}

#[derive(Debug)]
pub enum ConnectionOptions {
    Sqlite {
        path: std::path::PathBuf,
        read_only: bool,
    },
    Mysql {
        proxy: Option<crate::SocksProxy>,
        proxy_secret: Option<Secret>,
        host: String,
        port: u16,
        database: String,
        user: String,
        password: Option<Secret>,
        ssh_secret: Option<Secret>,
        ssh_jump_secrets: std::collections::BTreeMap<String, Secret>,
        ssh_private_key: Option<Secret>,
        ssh_jump_private_keys: std::collections::BTreeMap<String, Secret>,
        tls: TlsMode,
        root_certificate: Option<std::path::PathBuf>,
        tls_identity: Option<TlsIdentity>,
        ssh: Option<SshTunnel>,
    },
    Postgres {
        proxy: Option<crate::SocksProxy>,
        proxy_secret: Option<Secret>,
        host: String,
        port: u16,
        database: String,
        user: String,
        password: Option<Secret>,
        ssh_secret: Option<Secret>,
        ssh_jump_secrets: std::collections::BTreeMap<String, Secret>,
        ssh_private_key: Option<Secret>,
        ssh_jump_private_keys: std::collections::BTreeMap<String, Secret>,
        tls: TlsMode,
        root_certificate: Option<std::path::PathBuf>,
        tls_identity: Option<TlsIdentity>,
        ssh: Option<SshTunnel>,
    },
}
