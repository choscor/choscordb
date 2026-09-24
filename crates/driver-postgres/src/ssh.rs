//! Shared isolated OpenSSH forwarding preserves the database TLS hostname.
pub(crate) use choscordb_driver_api::SshForward as Stream;
pub(crate) fn timeout_error() -> choscordb_driver_api::DriverError {
    choscordb_driver_api::DriverError::new(
        choscordb_driver_api::ErrorKind::Timeout,
        "SSH PostgreSQL connection timed out",
    )
}
