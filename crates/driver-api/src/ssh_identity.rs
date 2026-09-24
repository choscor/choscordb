//! Destination-scoped temporary identities. Never persist inline key text in profiles.
use crate::{DriverError, ErrorKind, Result, Secret, SshIdentitySource, SshTunnel};

pub(crate) struct Identity {
    _directory: tempfile::TempDir,
}
fn invalid() -> DriverError {
    DriverError::new(ErrorKind::InvalidInput, "Invalid inline SSH private key")
}
pub(crate) fn validate(source: SshIdentitySource, key: Option<&Secret>) -> Result<()> {
    match (source, key) {
        (SshIdentitySource::File, None) => Ok(()),
        (SshIdentitySource::Inline, Some(key))
            if !key.expose().trim().is_empty()
                && key.expose().len() <= 64 * 1024
                && !key.expose().contains('\0') =>
        {
            Ok(())
        }
        _ => Err(invalid()),
    }
}
pub(crate) fn materialize(
    settings: &mut SshTunnel,
    key: Option<&Secret>,
) -> Result<Option<Identity>> {
    validate(settings.identity_source, key)?;
    if settings.identity_source == SshIdentitySource::File {
        return Ok(None);
    }
    #[cfg(not(unix))]
    return Err(DriverError::new(
        ErrorKind::Unsupported,
        "Inline SSH identities require verified private file permissions on this platform",
    ));
    #[cfg(unix)]
    {
        use std::{
            io::Write,
            os::unix::fs::{OpenOptionsExt, PermissionsExt},
        };
        let failure =
            || DriverError::new(ErrorKind::Connection, "Cannot prepare private SSH identity");
        let directory = tempfile::Builder::new()
            .prefix("choscordb-ssh-")
            .permissions(std::fs::Permissions::from_mode(0o700))
            .tempdir()
            .map_err(|_| failure())?;
        let path = directory.path().join("identity");
        let mut file = std::fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .mode(0o600)
            .open(&path)
            .map_err(|_| failure())?;
        let key = key.ok_or_else(invalid)?;
        // DBeaver trims pasted key lines; preserve that behavior without a second
        // plaintext allocation. OpenSSH accepts the canonical trailing newline.
        for line in key.expose().lines() {
            file.write_all(line.trim().as_bytes())
                .map_err(|_| failure())?;
            file.write_all(b"\n").map_err(|_| failure())?;
        }
        file.flush().map_err(|_| failure())?;
        settings.identity_file = Some(path.to_str().ok_or_else(failure)?.to_owned());
        settings.identity_source = SshIdentitySource::File;
        Ok(Some(Identity {
            _directory: directory,
        }))
    }
}
#[cfg(all(test, unix))]
mod tests {
    use super::*;
    use std::os::unix::fs::PermissionsExt;
    #[test]
    fn private_identity_is_private_and_removed_on_drop() {
        let mut settings: SshTunnel = serde_json::from_value(serde_json::json!({"host":"localhost","port":22,"user":"alice","authentication":"public_key","identity_source":"inline"})).unwrap();
        let key = Secret::new("  private fixture bytes  \r\n");
        let identity = materialize(&mut settings, Some(&key)).unwrap();
        let path = std::path::PathBuf::from(settings.identity_file.unwrap());
        assert_eq!(
            std::fs::metadata(&path).unwrap().permissions().mode() & 0o777,
            0o600
        );
        assert_eq!(
            std::fs::metadata(path.parent().unwrap())
                .unwrap()
                .permissions()
                .mode()
                & 0o777,
            0o700
        );
        assert_eq!(std::fs::read(&path).unwrap(), b"private fixture bytes\n");
        drop(identity);
        assert!(!path.exists());
        assert!(!path.parent().unwrap().exists());
    }
}
