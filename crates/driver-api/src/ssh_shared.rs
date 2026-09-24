//! Optional multiplexing uses only application-owned, private control sockets.
use crate::{DriverError, ErrorKind, Result};
use crate::{ssh_command::Destination, ssh_process::OwnedChild, ssh_proxy::Relay};
use std::{
    collections::HashMap,
    process::Stdio,
    sync::{Arc, Mutex, OnceLock, Weak},
};
use tokio::process::Command;
type Slot = tokio::sync::Mutex<Weak<Master>>;
static POOL: OnceLock<Mutex<HashMap<[u8; 32], Weak<Slot>>>> = OnceLock::new();
static MASTERS: tokio::sync::Semaphore = tokio::sync::Semaphore::const_new(128);
pub(crate) fn invalidate(key: [u8; 32]) -> Result<()> {
    if let Some(pool) = POOL.get() {
        pool.lock().map_err(|_| failure())?.remove(&key);
    }
    Ok(())
}
pub(crate) struct Master {
    _process: OwnedChild,
    _admission: tokio::sync::SemaphorePermit<'static>,
    directory: tempfile::TempDir,
    _broker: Option<Arc<crate::SshAskpass>>,
    _identity: Option<Arc<crate::ssh_identity::Identity>>,
    _route: Option<Arc<Relay>>,
    _slot: Arc<Slot>,
    host: String,
    port: u16,
    user: String,
}
fn failure() -> DriverError {
    DriverError::new(ErrorKind::Connection, "Cannot establish shared SSH session")
}
impl Master {
    fn path(&self) -> std::path::PathBuf {
        self.directory.path().join("c")
    }
    #[cfg(unix)]
    async fn alive(&self) -> bool {
        tokio::net::UnixStream::connect(self.path()).await.is_ok()
    }
    #[cfg(not(unix))]
    async fn alive(&self) -> bool {
        false
    }
    pub(crate) fn channel(self: &Arc<Self>, destination: &str) -> Result<crate::SshForward> {
        let mut command = Command::new("ssh");
        crate::ssh_askpass::clear_ssh_askpass_environment(&mut command);
        command.args(["-T", "-S"]).arg(self.path()).args([
            "-o",
            "ControlMaster=no",
            "-o",
            "ControlPersist=no",
            "-o",
            "ClearAllForwardings=yes",
            // A missing/dead master must never fall back to a new direct login.
            // This fixed shell command contains no user-controlled text.
            "-o",
            "ProxyCommand=exit 1",
            "-o",
            "ProxyJump=none",
            "-o",
            "BatchMode=yes",
            "-o",
            "PermitLocalCommand=no",
            "-p",
            &self.port.to_string(),
            "-l",
            &self.user,
            "-W",
            destination,
            "--",
            &self.host,
        ]);
        let mut channel = crate::SshForward::spawn(command, None, None, None)?;
        channel.master = Some(self.clone());
        Ok(channel)
    }
}
pub(crate) async fn acquire(
    key: [u8; 32],
    mut destinations: Vec<Destination>,
) -> Result<Arc<Master>> {
    if !cfg!(unix) {
        return Err(DriverError::new(
            ErrorKind::Unsupported,
            "SSH session sharing requires private Unix control sockets",
        ));
    }
    let slot = {
        let mut pool = POOL
            .get_or_init(Default::default)
            .lock()
            .map_err(|_| failure())?;
        pool.retain(|_, slot| slot.strong_count() > 0);
        if let Some(slot) = pool.get(&key).and_then(Weak::upgrade) {
            slot
        } else {
            if pool.len() >= 128 {
                return Err(DriverError::new(
                    ErrorKind::ResourceLimit,
                    "SSH shared session limit reached",
                ));
            }
            let slot = Arc::new(tokio::sync::Mutex::new(Weak::new()));
            pool.insert(key, Arc::downgrade(&slot));
            slot
        }
    };
    let mut current = slot.lock().await;
    if let Some(master) = current.upgrade()
        && master.alive().await
    {
        return Ok(master);
    }
    // Retired masters can still serve healthy leases; count them too.
    let admission = MASTERS.try_acquire().map_err(|_| {
        DriverError::new(ErrorKind::ResourceLimit, "SSH shared session limit reached")
    })?;
    let mut target = destinations.pop().ok_or_else(failure)?;
    let route = if destinations.is_empty() {
        None
    } else {
        let stream = crate::SshForward::forward_destinations(
            destinations,
            crate::ssh_command::endpoint(&target.hostname, target.port),
        )
        .await?;
        Some(Relay::new(stream).await?)
    };
    let directory = tokio::task::spawn_blocking(|| {
        let mut builder = tempfile::Builder::new();
        builder.prefix("cdb-ssh-");
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            builder.permissions(std::fs::Permissions::from_mode(0o700));
        }
        // Control sockets have short platform path limits; do not inherit a long TMPDIR.
        builder.tempdir_in("/tmp")
    })
    .await
    .map_err(|_| failure())?
    .map_err(|_| failure())?;
    let mut command = Command::new(target.command.as_std().get_program());
    command
        .args([
            "-M",
            "-N",
            "-o",
            "ControlMaster=yes",
            "-o",
            "ControlPersist=no",
            "-S",
        ])
        .arg(directory.path().join("c"));
    command.args(target.command.as_std().get_args());
    for (name, value) in target.command.as_std().get_envs() {
        if let Some(value) = value {
            command.env(name, value);
        } else {
            command.env_remove(name);
        }
    }
    let proxy = match &route {
        Some(route) => format!("ProxyCommand={}", route.configure(&mut command)?),
        None => "ProxyCommand=none".into(),
    };
    command
        .arg("-o")
        .arg(format!("HostName={}", target.hostname.replace('%', "%%")))
        .args(["-p", &target.port.to_string(), "-o"])
        .arg(proxy)
        .args([
            "-o",
            "ProxyJump=none",
            "-o",
            "ProxyUseFdpass=no",
            "-o",
            "ClearAllForwardings=yes",
            "--",
            &target.settings.host,
        ])
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    let process = OwnedChild::spawn(&mut command).map_err(|_| failure())?;
    #[cfg(unix)]
    let mut process = process;
    #[cfg(unix)]
    loop {
        if process.try_wait().map_err(|_| failure())?.is_some() {
            return Err(failure());
        }
        if tokio::net::UnixStream::connect(directory.path().join("c"))
            .await
            .is_ok()
        {
            break;
        }
        tokio::time::sleep(std::time::Duration::from_millis(20)).await;
    }
    let master = Arc::new(Master {
        _process: process,
        _admission: admission,
        directory,
        _broker: target.broker.take(),
        _identity: target.identity.take(),
        _route: route,
        _slot: slot.clone(),
        host: target.settings.host,
        port: target.port,
        user: target.settings.user,
    });
    *current = Arc::downgrade(&master);
    Ok(master)
}
