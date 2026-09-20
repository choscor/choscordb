use crate::{DriverError, ErrorKind, Result};
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
/// SSH authentication uses OpenSSH's agent or an optional private-key file.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SshTunnel {
    pub host: String,
    pub port: u16,
    pub user: String,
    pub identity_file: Option<String>,
}
impl SshTunnel {
    pub fn validate(&self) -> Result<()> {
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
        {
            return Err(DriverError::new(
                ErrorKind::InvalidInput,
                "Invalid SSH tunnel settings",
            ));
        }
        Ok(())
    }
}
#[derive(Debug)]
pub enum ConnectionOptions {
    Sqlite {
        path: std::path::PathBuf,
        read_only: bool,
    },
    Mysql {
        host: String,
        port: u16,
        database: String,
        user: String,
        password: Option<Secret>,
        tls: TlsMode,
        root_certificate: Option<std::path::PathBuf>,
        ssh: Option<SshTunnel>,
    },
    Postgres {
        host: String,
        port: u16,
        database: String,
        user: String,
        password: Option<Secret>,
        tls: TlsMode,
        root_certificate: Option<std::path::PathBuf>,
        ssh: Option<SshTunnel>,
    },
}
