//! OpenSSH owns host-key verification and agent/key authentication. Its stdio
//! forwarding keeps the remote database address off the local TCP interface.
use choscordb_driver_api::{
    DriverError, ErrorKind, Result, Secret, SshAskpass, SshTunnel, configure_ssh_authentication,
};
use std::{
    pin::Pin,
    process::Stdio,
    task::{Context, Poll},
};
use tokio::{
    io::{AsyncRead, AsyncWrite, ReadBuf},
    process::{Child, ChildStdin, ChildStdout, Command},
};

pub(crate) const TIMEOUT: std::time::Duration = std::time::Duration::from_secs(15);
pub(crate) fn timeout_error() -> DriverError {
    DriverError::new(ErrorKind::Connection, "SSH PostgreSQL connection timed out")
}

pub(crate) struct Stream {
    child: Option<Child>,
    input: ChildStdin,
    output: ChildStdout,
    _askpass: Option<SshAskpass>,
}
impl Stream {
    pub(crate) fn take_process(&mut self) -> Process {
        Process(self.child.take())
    }
    pub(crate) fn open(
        settings: &SshTunnel,
        secret: Option<&Secret>,
        host: &str,
        port: u16,
    ) -> Result<Self> {
        settings.validate()?;
        let destination = if host.contains(':') {
            format!("[{host}]:{port}")
        } else {
            format!("{host}:{port}")
        };
        let mut command = Command::new("ssh");
        command.args([
            "-T",
            "-o",
            "StrictHostKeyChecking=yes",
            "-o",
            "ConnectTimeout=15",
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
        let askpass = configure_ssh_authentication(&mut command, settings, secret)?;
        command
            .arg("-p")
            .arg(settings.port.to_string())
            .arg("-l")
            .arg(&settings.user);
        if let Some(identity) = &settings.identity_file {
            command.arg("-i").arg(identity);
        }
        command
            .arg("-W")
            .arg(destination)
            .arg("--")
            .arg(&settings.host)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .kill_on_drop(true);
        let mut child = command.spawn().map_err(|_| {
            DriverError::new(
                ErrorKind::Connection,
                "Cannot start OpenSSH; install ssh and configure key or agent authentication",
            )
        })?;
        let input = child.stdin.take().expect("piped SSH stdin");
        let output = child.stdout.take().expect("piped SSH stdout");
        Ok(Self {
            child: Some(child),
            input,
            output,
            _askpass: askpass,
        })
    }
}

pub(crate) struct Process(Option<Child>);
impl Process {
    pub(crate) async fn finish(&mut self) -> Result<()> {
        let status = self
            .0
            .as_mut()
            .expect("SSH process")
            .wait()
            .await
            .map_err(|_| {
                DriverError::new(ErrorKind::Connection, "SSH cancellation connection failed")
            })?;
        if status.success() {
            Ok(())
        } else {
            Err(DriverError::new(
                ErrorKind::Connection,
                "SSH cancellation connection failed",
            ))
        }
    }
}
impl Drop for Process {
    fn drop(&mut self) {
        if let Some(mut child) = self.0.take() {
            let _ = child.start_kill();
            if let Ok(runtime) = tokio::runtime::Handle::try_current() {
                runtime.spawn(async move {
                    let _ = child.wait().await;
                });
            }
        }
    }
}
impl Drop for Stream {
    fn drop(&mut self) {
        drop(Process(self.child.take()));
    }
}
impl AsyncRead for Stream {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buffer: &mut ReadBuf<'_>,
    ) -> Poll<std::io::Result<()>> {
        Pin::new(&mut self.output).poll_read(cx, buffer)
    }
}
impl AsyncWrite for Stream {
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
