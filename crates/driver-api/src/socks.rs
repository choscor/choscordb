//! Native SOCKS CONNECT transport; database TLS runs above the returned stream.
use crate::{DriverError, ErrorKind, Result, Secret, tcp_host};
use serde::{Deserialize, Serialize};
use std::{net::IpAddr, time::Duration};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::TcpStream,
};
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SocksProtocol {
    Socks4,
    #[default]
    Socks5,
}
fn default_port() -> u16 {
    1080
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SocksProxy {
    #[serde(default)]
    pub protocol: SocksProtocol,
    pub host: String,
    #[serde(default = "default_port")]
    pub port: u16,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub username: Option<String>,
}
fn invalid() -> DriverError {
    DriverError::new(
        ErrorKind::InvalidInput,
        "Invalid SOCKS proxy settings or destination",
    )
}
impl SocksProxy {
    pub fn needs_password(&self) -> bool {
        self.protocol == SocksProtocol::Socks5 && self.username.is_some()
    }
    pub fn validate(&self) -> Result<()> {
        tcp_host(&self.host)?;
        if self.port == 0
            || self
                .username
                .as_ref()
                .is_some_and(|user| user.is_empty() || user.len() > 255 || user.contains('\0'))
        {
            return Err(invalid());
        }
        Ok(())
    }
    pub fn validate_transport(&self, host: &str, port: u16, ssh: bool) -> Result<()> {
        self.validate_target(host, port)?;
        if ssh || host.starts_with('/') {
            return Err(DriverError::new(
                ErrorKind::InvalidInput,
                "SOCKS proxies cannot be combined with SSH or Unix sockets",
            ));
        }
        Ok(())
    }
    pub fn validate_target(&self, host: &str, port: u16) -> Result<()> {
        self.validate()?;
        let host = tcp_host(host)?;
        if port == 0 {
            return Err(invalid());
        }
        match host.parse::<IpAddr>() {
            Ok(IpAddr::V6(_)) if self.protocol == SocksProtocol::Socks4 => {
                return Err(DriverError::new(
                    ErrorKind::InvalidInput,
                    "SOCKS4 does not support IPv6 destinations; use SOCKS5",
                ));
            }
            Ok(_) => (),
            Err(_) if host.len() > 255 || !host.is_ascii() || host.contains([':', '%']) => {
                return Err(invalid());
            }
            Err(_) => (),
        }
        Ok(())
    }
}
pub fn validate_socks_secret(proxy: &SocksProxy, secret: Option<&Secret>) -> Result<()> {
    if proxy.needs_password()
        && secret.is_none_or(|value| value.expose().is_empty() || value.expose().len() > 255)
    {
        return Err(DriverError::new(
            ErrorKind::Authentication,
            "SOCKS5 requires a password of 1–255 bytes",
        ));
    }
    Ok(())
}
pub async fn connect_socks(
    proxy: &SocksProxy,
    secret: Option<&Secret>,
    host: &str,
    port: u16,
    timeout: Duration,
) -> Result<TcpStream> {
    proxy.validate_target(host, port)?;
    validate_socks_secret(proxy, secret)?;
    if timeout.is_zero() {
        return Err(invalid());
    }
    tokio::time::timeout(timeout, async {
        let mut stream = TcpStream::connect((tcp_host(&proxy.host)?, proxy.port))
            .await
            .map_err(connection_error)?;
        let host = tcp_host(host)?;
        match proxy.protocol {
            SocksProtocol::Socks5 => socks5(&mut stream, proxy, secret, host, port).await?,
            SocksProtocol::Socks4 => socks4(&mut stream, proxy, host, port).await?,
        }
        Ok(stream)
    })
    .await
    .map_err(|_| DriverError::new(ErrorKind::Timeout, "SOCKS connection timed out"))?
}

fn connection_error(_: std::io::Error) -> DriverError {
    DriverError::new(ErrorKind::Connection, "SOCKS proxy connection failed")
}
async fn socks5(
    stream: &mut TcpStream,
    proxy: &SocksProxy,
    secret: Option<&Secret>,
    host: &str,
    port: u16,
) -> Result<()> {
    let method = if proxy.needs_password() { 2 } else { 0 };
    stream
        .write_all(&[5, 1, method])
        .await
        .map_err(connection_error)?;
    let mut selected = [0; 2];
    stream
        .read_exact(&mut selected)
        .await
        .map_err(connection_error)?;
    if selected != [5, method] {
        return Err(DriverError::new(
            ErrorKind::Authentication,
            "SOCKS5 proxy rejected the configured authentication method",
        ));
    }
    if method == 2 {
        let user = proxy
            .username
            .as_ref()
            .expect("validated proxy username")
            .as_bytes();
        let password = secret
            .expect("validated proxy password")
            .expose()
            .as_bytes();
        let mut credentials = zeroize::Zeroizing::new(vec![1, user.len() as u8]);
        credentials.extend_from_slice(user);
        credentials.push(password.len() as u8);
        credentials.extend_from_slice(password);
        stream
            .write_all(&credentials)
            .await
            .map_err(connection_error)?;
        let mut reply = [0; 2];
        stream
            .read_exact(&mut reply)
            .await
            .map_err(connection_error)?;
        if reply != [1, 0] {
            return Err(DriverError::new(
                ErrorKind::Authentication,
                "SOCKS5 password authentication failed",
            ));
        }
    }
    let mut request = vec![5, 1, 0];
    match host.parse::<IpAddr>() {
        Ok(IpAddr::V4(address)) => {
            request.push(1);
            request.extend_from_slice(&address.octets());
        }
        Ok(IpAddr::V6(address)) => {
            request.push(4);
            request.extend_from_slice(&address.octets());
        }
        Err(_) => {
            request.extend_from_slice(&[3, host.len() as u8]);
            request.extend_from_slice(host.as_bytes());
        }
    }
    request.extend_from_slice(&port.to_be_bytes());
    stream.write_all(&request).await.map_err(connection_error)?;
    let mut reply = [0; 4];
    stream
        .read_exact(&mut reply)
        .await
        .map_err(connection_error)?;
    if reply[0] != 5 || reply[1] != 0 || reply[2] != 0 {
        return Err(DriverError::new(
            ErrorKind::Connection,
            "SOCKS5 proxy rejected the connection",
        ));
    }
    let length = match reply[3] {
        1 => 4,
        4 => 16,
        3 => {
            let size = stream.read_u8().await.map_err(connection_error)?;
            if size == 0 {
                return Err(DriverError::new(
                    ErrorKind::Connection,
                    "Malformed SOCKS5 proxy reply",
                ));
            }
            usize::from(size)
        }
        _ => {
            return Err(DriverError::new(
                ErrorKind::Connection,
                "Malformed SOCKS5 proxy reply",
            ));
        }
    };
    let mut bound = [0; 255];
    stream
        .read_exact(&mut bound[..length])
        .await
        .map_err(connection_error)?;
    stream.read_u16().await.map_err(connection_error)?;
    Ok(())
}

async fn socks4(stream: &mut TcpStream, proxy: &SocksProxy, host: &str, port: u16) -> Result<()> {
    let address = host.parse::<std::net::Ipv4Addr>().ok();
    let mut request = vec![4, 1];
    request.extend_from_slice(&port.to_be_bytes());
    request.extend_from_slice(&address.map_or([0, 0, 0, 1], |address| address.octets()));
    request.extend_from_slice(proxy.username.as_deref().unwrap_or("").as_bytes());
    request.push(0);
    if address.is_none() {
        request.extend_from_slice(host.as_bytes());
        request.push(0);
    }
    stream.write_all(&request).await.map_err(connection_error)?;
    let mut reply = [0; 8];
    stream
        .read_exact(&mut reply)
        .await
        .map_err(connection_error)?;
    if reply[0] != 0 || reply[1] != 90 {
        return Err(DriverError::new(
            ErrorKind::Connection,
            "SOCKS4 proxy rejected the connection",
        ));
    }
    Ok(())
}
