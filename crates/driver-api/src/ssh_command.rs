//! Construct destination-scoped OpenSSH commands and snapshot their effective address.
use crate::{DriverError, ErrorKind, Result, Secret, SshAskpass, SshTunnel};
use std::{process::Stdio, sync::Arc};
use tokio::{io::AsyncReadExt, process::Command};

pub(crate) struct Destination {
    pub settings: SshTunnel,
    pub context: Vec<u8>,
    pub identity: Option<Arc<crate::ssh_identity::Identity>>,
    pub hostname: String,
    pub port: u16,
    pub command: Command,
    pub broker: Option<Arc<SshAskpass>>,
}
fn failure() -> DriverError {
    DriverError::new(
        ErrorKind::Connection,
        "Cannot resolve OpenSSH destination configuration",
    )
}
pub(crate) async fn resolve(
    mut settings: SshTunnel,
    key: Option<&Secret>,
    secret: Option<&Secret>,
    configured: bool,
    routed: bool,
) -> Result<Destination> {
    let inline = settings.identity_source == crate::SshIdentitySource::Inline;
    let identity = crate::ssh_identity::materialize(&mut settings, key)?.map(Arc::new);
    let mut command = Command::new("ssh");
    crate::ssh_askpass::clear_ssh_askpass_environment(&mut command);
    let broker = if configured {
        crate::ssh_options::configure_ssh_transport(&mut command, &settings)?;
        command.args(["-o", "BatchMode=yes"]);
        None
    } else {
        crate::configure_ssh_authentication(&mut command, &settings, secret)?.map(Arc::new)
    };
    command.args(["-p", &settings.port.to_string(), "-l", &settings.user]);
    if let Some(identity) = &settings.identity_file {
        command.arg("-i").arg(identity);
    }
    let address = snapshot(&settings, &command, routed).await?;
    let context = if settings.options.share_tunnels {
        crate::ssh_context::configuration(&settings, &address.configuration, inline).await
    } else {
        Vec::new()
    };
    Ok(Destination {
        context,
        settings,
        identity,
        hostname: address.hostname,
        port: address.port,
        command,
        broker,
    })
}
pub(crate) struct Address {
    pub hostname: String,
    pub port: u16,
    pub host_key_alias: Option<String>,
    pub configuration: String,
}
pub(crate) async fn inspect_address(settings: &SshTunnel, routed: bool) -> Result<Address> {
    let mut settings = settings.clone();
    settings.options.jump_hosts.clear();
    let mut command = Command::new("ssh");
    crate::ssh_options::configure_ssh_transport(&mut command, &settings)?;
    command.args(["-p", &settings.port.to_string(), "-l", &settings.user]);
    if let Some(identity) = &settings.identity_file {
        command.arg("-i").arg(identity);
    }
    snapshot(&settings, &command, routed).await
}
async fn snapshot(settings: &SshTunnel, command: &Command, routed: bool) -> Result<Address> {
    // -G evaluates Host/Match before connecting. No authentication broker reaches
    // that child; a placeholder preserves the presence of a proxy for canonicalization.
    let mut preflight = Command::new("ssh");
    preflight
        .arg("-G")
        .arg("-o")
        .arg(if routed {
            "ProxyCommand=choscordb-owned-preflight"
        } else {
            "ProxyCommand=none"
        })
        .args(["-o", "ProxyJump=none", "-o", "ProxyUseFdpass=no"])
        .args(command.as_std().get_args())
        .arg("--")
        .arg(crate::tcp_host(&settings.host)?)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .kill_on_drop(true);
    crate::ssh_askpass::clear_ssh_askpass_environment(&mut preflight);
    let mut child = crate::ssh_process::OwnedChild::spawn(&mut preflight).map_err(|_| failure())?;
    let mut output = Vec::new();
    child
        .child()
        .stdout
        .take()
        .ok_or_else(failure)?
        .take(128 * 1024 + 1)
        .read_to_end(&mut output)
        .await
        .map_err(|_| failure())?;
    if output.len() > 128 * 1024 {
        return Err(DriverError::new(
            ErrorKind::ResourceLimit,
            "OpenSSH configuration exceeds limit",
        ));
    }
    if !child.wait().await.map_err(|_| failure())?.success() {
        return Err(failure());
    }
    let output = std::str::from_utf8(&output).map_err(|_| failure())?;
    let hostname = output
        .lines()
        .find_map(|line| line.strip_prefix("hostname "))
        .ok_or_else(failure)?;
    let hostname = crate::tcp_host(hostname).map_err(|_| failure())?;
    if hostname
        .chars()
        .any(|c| !c.is_ascii_alphanumeric() && !".:-%_[]".contains(c))
    {
        return Err(failure());
    }
    let port = output
        .lines()
        .find_map(|line| line.strip_prefix("port "))
        .and_then(|port| port.parse::<u16>().ok())
        .filter(|port| *port != 0)
        .ok_or_else(failure)?;
    let host_key_alias = output
        .lines()
        .find_map(|line| line.strip_prefix("hostkeyalias "))
        .filter(|alias| *alias != "none")
        .map(str::to_owned);
    Ok(Address {
        configuration: output.to_owned(),
        hostname: hostname.to_owned(),
        port,
        host_key_alias,
    })
}

pub(crate) fn endpoint(host: &str, port: u16) -> String {
    if host.contains(':') {
        format!("[{host}]:{port}")
    } else {
        format!("{host}:{port}")
    }
}
