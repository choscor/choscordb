use choscordb_driver_api::{DriverError, ErrorKind, Result, TlsIdentity, TlsMode};
use mysql_async::{ClientIdentity, SslOpts};
use std::{
    io::Read,
    path::{Path, PathBuf},
};

fn read_material(path: &Path) -> Result<Vec<u8>> {
    const LIMIT: usize = 1024 * 1024;
    let io = |_| DriverError::new(ErrorKind::Tls, "Cannot read TLS certificate or identity");
    let metadata = std::fs::metadata(path).map_err(io)?;
    if !metadata.is_file() || metadata.len() > LIMIT as u64 {
        return Err(DriverError::new(
            ErrorKind::ResourceLimit,
            "TLS material must be a regular file no larger than 1 MiB",
        ));
    }
    let mut bytes = Vec::new();
    std::fs::File::open(path)
        .map_err(io)?
        .take((LIMIT + 1) as u64)
        .read_to_end(&mut bytes)
        .map_err(io)?;
    if bytes.len() > LIMIT {
        return Err(DriverError::new(
            ErrorKind::ResourceLimit,
            "TLS material exceeds 1 MiB limit",
        ));
    }
    Ok(bytes)
}

pub async fn options(
    mode: TlsMode,
    root: Option<PathBuf>,
    identity: Option<TlsIdentity>,
) -> Result<Option<SslOpts>> {
    if mode == TlsMode::Prefer {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "MySQL does not support TLS Prefer; select Disable, Require, Verify CA, or Verify Full",
        ));
    }
    if mode == TlsMode::Disable {
        return Ok(None);
    }
    tokio::task::spawn_blocking(move || {
        let mut options = SslOpts::default()
            .with_disable_built_in_roots(false)
            .with_danger_accept_invalid_certs(mode == TlsMode::Require)
            .with_danger_skip_domain_validation(matches!(
                mode,
                TlsMode::Require | TlsMode::VerifyCa
            ));
        if let Some(path) = root {
            options = options.with_root_certs(vec![read_material(&path)?.into()]);
        }
        if let Some(identity) = identity {
            let bytes = read_material(&identity.path)?;
            let mut client = ClientIdentity::new(bytes.into());
            if let Some(password) = identity.password {
                client = client.with_password(password.expose().to_owned());
            }
            options = options.with_client_identity(Some(client));
        }
        Ok(Some(options))
    })
    .await
    .map_err(|_| DriverError::new(ErrorKind::Internal, "MySQL TLS worker failed"))?
}
