use crate::{Result, SshHostKeyApproval, SshHostKeyCandidate};
use std::{
    path::Path,
    sync::{
        Arc,
        atomic::{AtomicU8, Ordering},
    },
};
const OPEN: u8 = 0;
const APPENDING: u8 = 1;
const COMPLETE: u8 = 2;
const CANCELLED: u8 = 3;
#[derive(Clone)]
pub(crate) struct ApprovalState(Arc<AtomicU8>);
impl ApprovalState {
    pub(crate) fn new() -> Self {
        Self(Arc::new(AtomicU8::new(OPEN)))
    }
    pub(crate) fn cancel(&self) -> Option<SshHostKeyApproval> {
        match self
            .0
            .compare_exchange(OPEN, CANCELLED, Ordering::AcqRel, Ordering::Acquire)
        {
            Err(APPENDING) => Some(SshHostKeyApproval::OutcomeUnknown),
            Err(COMPLETE) => Some(SshHostKeyApproval::Approved),
            _ => None,
        }
    }
    #[cfg(any(unix, test))]
    fn ensure_open(&self) -> Result<()> {
        if self.0.load(Ordering::Acquire) != OPEN {
            Err(crate::DriverError::new(
                crate::ErrorKind::Cancelled,
                "SSH host key approval cancelled",
            ))
        } else {
            Ok(())
        }
    }
    #[cfg(any(unix, test))]
    fn begin_append(&self) -> Result<()> {
        self.0
            .compare_exchange(OPEN, APPENDING, Ordering::AcqRel, Ordering::Acquire)
            .map(|_| ())
            .map_err(|_| {
                crate::DriverError::new(
                    crate::ErrorKind::Cancelled,
                    "SSH host key approval cancelled",
                )
            })
    }
}
pub(crate) struct ApprovalGuard(pub ApprovalState);
impl Drop for ApprovalGuard {
    fn drop(&mut self) {
        self.0.cancel();
    }
}
#[cfg(any(unix, test))]
async fn blocking<T: Send + 'static>(
    state: ApprovalState,
    action: impl FnOnce(&ApprovalState) -> Result<T> + Send + 'static,
) -> Result<T> {
    tokio::task::spawn_blocking(move || {
        state.ensure_open()?;
        action(&state)
    })
    .await
    .map_err(|_| super::ssh_trust::failure("SSH known hosts worker stopped"))?
}
#[cfg(not(unix))]
pub(crate) async fn append(
    _: &SshHostKeyCandidate,
    _: &Path,
    _: &ApprovalState,
) -> Result<SshHostKeyApproval> {
    Err(crate::DriverError::new(
        crate::ErrorKind::Unsupported,
        "SSH host key approval requires verified private file permissions on this platform",
    ))
}
#[cfg(unix)]
pub(crate) async fn append(
    candidate: &SshHostKeyCandidate,
    path: &Path,
    state: &ApprovalState,
) -> Result<SshHostKeyApproval> {
    use super::ssh_trust::{failure, output};
    use std::io::{Read, Seek, Write};
    let path = path.to_owned();
    let prepared = blocking(state.clone(), move |state| prepare(&path, state)).await?;
    let host = candidate.record_host()?;
    let mut command = tokio::process::Command::new("ssh-keygen");
    command
        .arg("-F")
        .arg(&host)
        .arg("-f")
        .arg(prepared.snapshot.path());
    let (status, found) = output(&mut command, None, LIMIT).await?;
    let fail = || failure("Cannot update selected SSH known hosts file");
    if !matches!(status.code(), Some(0 | 1)) {
        return Err(fail());
    }
    let found = std::str::from_utf8(&found).map_err(|_| fail())?;
    let mut same = false;
    for line in found
        .lines()
        .filter(|line| !line.starts_with('#') && !line.trim().is_empty())
    {
        let fields: Vec<_> = line.split_whitespace().collect();
        if fields.len() < 3
            || fields[0].starts_with('@')
            || fields[1] != candidate.key_type
            || fields[2] != candidate.public_key
        {
            return Err(failure(
                "Existing SSH host key cannot be replaced by approval",
            ));
        }
        same = true;
    }
    if same {
        return Ok(SshHostKeyApproval::Approved);
    }
    let newline = if !prepared.original.is_empty() && !prepared.original.ends_with(b"\n") {
        "\n"
    } else {
        ""
    };
    let record = format!(
        "{newline}{host} {} {}\n",
        candidate.key_type, candidate.public_key
    );
    if prepared.original.len() + record.len() > LIMIT as usize {
        return Err(fail());
    }
    blocking(state.clone(), move |state| {
        let mut prepared = prepared;
        prepared.file.rewind().map_err(|_| fail())?;
        let mut current = Vec::new();
        (&mut prepared.file)
            .take(LIMIT + 1)
            .read_to_end(&mut current)
            .map_err(|_| fail())?;
        if current != prepared.original {
            return Err(failure("SSH known hosts file changed during approval"));
        }
        // This atomic transition races cancellation. A cancelled approval cannot
        // begin a delayed append after a blocked read or lock finally completes.
        state.begin_append()?;
        if prepared
            .file
            .write_all(record.as_bytes())
            .and_then(|()| prepared.file.sync_data())
            .is_err()
        {
            return Ok(SshHostKeyApproval::OutcomeUnknown);
        }
        state.0.store(COMPLETE, Ordering::Release);
        Ok(SshHostKeyApproval::Approved)
    })
    .await
}
#[cfg(unix)]
const LIMIT: u64 = 4 * 1024 * 1024;
#[cfg(unix)]
struct Prepared {
    file: std::fs::File,
    original: Vec<u8>,
    snapshot: tempfile::NamedTempFile,
}
#[cfg(unix)]
fn prepare(path: &Path, state: &ApprovalState) -> Result<Prepared> {
    use std::{
        io::{Read, Write},
        os::unix::fs::{OpenOptionsExt, PermissionsExt},
    };
    let fail = || super::ssh_trust::failure("Cannot update selected SSH known hosts file");
    let mut file = std::fs::OpenOptions::new()
        .read(true)
        .append(true)
        .create(true)
        .mode(0o600)
        .custom_flags(nix::libc::O_NOFOLLOW | nix::libc::O_NONBLOCK)
        .open(path)
        .map_err(|_| fail())?;
    state.ensure_open()?;
    let metadata = file.metadata().map_err(|_| fail())?;
    if !metadata.is_file() || metadata.permissions().mode() & 0o022 != 0 || metadata.len() > LIMIT {
        return Err(fail());
    }
    loop {
        state.ensure_open()?;
        match fs2::FileExt::try_lock_exclusive(&file) {
            Ok(()) => break,
            Err(error) if error.kind() == std::io::ErrorKind::WouldBlock => {
                std::thread::sleep(std::time::Duration::from_millis(20))
            }
            Err(_) => return Err(fail()),
        }
    }
    let mut original = Vec::new();
    (&mut file)
        .take(LIMIT + 1)
        .read_to_end(&mut original)
        .map_err(|_| fail())?;
    state.ensure_open()?;
    if original.len() as u64 > LIMIT {
        return Err(fail());
    }
    let mut snapshot = tempfile::NamedTempFile::new().map_err(|_| fail())?;
    snapshot.write_all(&original).map_err(|_| fail())?;
    state.ensure_open()?;
    Ok(Prepared {
        file,
        original,
        snapshot,
    })
}
#[cfg(test)]
mod tests {
    use super::*;
    #[tokio::test]
    async fn blocking_storage_does_not_block_timers_or_append_after_cancellation() {
        let state = ApprovalState::new();
        let copy = state.clone();
        let (started_tx, started_rx) = tokio::sync::oneshot::channel();
        let (release_tx, release_rx) = std::sync::mpsc::channel();
        let (completed_tx, completed_rx) = tokio::sync::oneshot::channel();
        let task = tokio::spawn(async move {
            let _guard = ApprovalGuard(copy.clone());
            blocking(copy, move |state| {
                let _ = started_tx.send(());
                release_rx.recv().unwrap(); // Controlled stalled filesystem operation.
                let permitted = state.begin_append().is_ok();
                let _ = completed_tx.send(permitted);
                Ok(())
            })
            .await
        });
        started_rx.await.unwrap();
        tokio::time::timeout(
            std::time::Duration::from_millis(100),
            tokio::time::sleep(std::time::Duration::from_millis(10)),
        )
        .await
        .expect("filesystem stalled Tokio timer");
        task.abort();
        assert!(task.await.unwrap_err().is_cancelled());
        release_tx.send(()).unwrap();
        assert!(
            !completed_rx.await.unwrap(),
            "cancelled filesystem worker began a delayed append"
        );
    }
    #[test]
    fn cancellation_of_an_issued_append_is_explicitly_unknown() {
        let state = ApprovalState::new();
        state.begin_append().unwrap();
        assert_eq!(state.cancel(), Some(SshHostKeyApproval::OutcomeUnknown));
    }
}
