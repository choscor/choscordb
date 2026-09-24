use choscordb_driver_api::{DriverError, ErrorKind, Result, Secret, TlsIdentity, TlsMode};
use postgres_native_tls::MakeTlsConnector;
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

pub async fn connector(
    mode: TlsMode,
    root: Option<PathBuf>,
    identity: Option<TlsIdentity>,
) -> Result<MakeTlsConnector> {
    tokio::task::spawn_blocking(move || {
        let mut builder = native_tls::TlsConnector::builder();
        builder.danger_accept_invalid_hostnames(matches!(
            mode,
            TlsMode::Prefer | TlsMode::Require | TlsMode::VerifyCa
        ));
        builder.danger_accept_invalid_certs(matches!(mode, TlsMode::Prefer | TlsMode::Require));
        if mode != TlsMode::Disable {
            if let Some(path) = root {
                let bytes = read_material(&path)?;
                let invalid = || DriverError::new(ErrorKind::Tls, "Invalid root certificate");
                const BEGIN: &[u8] = b"-----BEGIN CERTIFICATE-----";
                const END: &[u8] = b"-----END CERTIFICATE-----";
                let mut remaining = bytes.as_slice();
                let mut count = 0;
                while let Some(start) = remaining
                    .windows(BEGIN.len())
                    .position(|part| part == BEGIN)
                {
                    remaining = &remaining[start..];
                    let end = remaining
                        .windows(END.len())
                        .position(|part| part == END)
                        .ok_or_else(invalid)?
                        + END.len();
                    let certificate = native_tls::Certificate::from_pem(&remaining[..end])
                        .map_err(|_| invalid())?;
                    builder.add_root_certificate(certificate);
                    remaining = &remaining[end..];
                    count += 1;
                }
                if count == 0 {
                    builder.add_root_certificate(
                        native_tls::Certificate::from_der(&bytes).map_err(|_| invalid())?,
                    );
                }
            }
            if let Some(identity) = identity {
                let bytes = read_material(&identity.path)?;
                let identity = native_tls::Identity::from_pkcs12(
                    &bytes,
                    identity.password.as_ref().map_or("", Secret::expose),
                )
                .map_err(|_| {
                    DriverError::new(
                        ErrorKind::Tls,
                        "Cannot unlock TLS client identity; check PKCS#12 file and password",
                    )
                })?;
                builder.identity(identity);
            }
        }
        builder
            .build()
            .map(MakeTlsConnector::new)
            .map_err(|_| DriverError::new(ErrorKind::Tls, "Cannot initialize TLS"))
    })
    .await
    .map_err(|_| DriverError::new(ErrorKind::Internal, "TLS worker failed"))?
}
