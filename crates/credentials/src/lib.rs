//! Blocking operating-system credential access. Call from a serialized worker,
//! never the UI thread. No plaintext persistence or fallback credential backend.
pub use choscordb_driver_api::Secret;

pub const MAX_SECRET_BYTES: usize = 16 * 1024;
const SERVICE: &str = "org.choscordb.desktop";
pub type Result<T> = std::result::Result<T, CredentialError>;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CredentialError {
    Missing,
    Unavailable,
    InvalidReference,
    SecretTooLarge,
    InvalidNamespace,
}
impl std::fmt::Display for CredentialError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Self::Missing => "Saved credential was not found",
            Self::Unavailable => "Operating-system credential store is unavailable",
            Self::InvalidReference => "Invalid credential reference",
            Self::SecretTooLarge => "Credential exceeds the supported size",
            Self::InvalidNamespace => "Invalid credential test namespace",
        })
    }
}
impl std::error::Error for CredentialError {}

pub trait CredentialStore: Send + Sync {
    fn get(&self, reference: &str) -> Result<Secret>;
    fn put(&self, reference: &str, secret: &Secret) -> Result<()>;
    /// Deletion is idempotent: an absent credential is already deleted.
    fn delete(&self, reference: &str) -> Result<()>;
}

/// Explicit backend for headless tests or builds without a credential service.
/// It never silently stores secrets in memory or on disk.
pub struct UnavailableStore;
impl CredentialStore for UnavailableStore {
    fn get(&self, _: &str) -> Result<Secret> {
        Err(CredentialError::Unavailable)
    }
    fn put(&self, _: &str, _: &Secret) -> Result<()> {
        Err(CredentialError::Unavailable)
    }
    fn delete(&self, _: &str) -> Result<()> {
        Err(CredentialError::Unavailable)
    }
}

pub struct NativeCredentialStore {
    service: String,
}
impl Default for NativeCredentialStore {
    fn default() -> Self {
        Self::new()
    }
}
impl NativeCredentialStore {
    pub fn new() -> Self {
        Self {
            service: SERVICE.into(),
        }
    }
    /// Isolated native tests must never share the production namespace.
    pub fn for_test_namespace(namespace: &str) -> Result<Self> {
        if !namespace.starts_with("org.choscordb.tests.")
            || namespace.len() <= "org.choscordb.tests.".len()
            || namespace.len() > 256
            || namespace.contains('\0')
        {
            return Err(CredentialError::InvalidNamespace);
        }
        Ok(Self {
            service: namespace.into(),
        })
    }
}
fn validate_reference(reference: &str) -> Result<()> {
    if reference.is_empty() || reference.len() > 256 || reference.contains('\0') {
        Err(CredentialError::InvalidReference)
    } else {
        Ok(())
    }
}

// Process-wide serialization also orders calls from independent adapter instances.
static NATIVE_ACCESS: std::sync::Mutex<()> = std::sync::Mutex::new(());

#[cfg(any(target_os = "macos", target_os = "windows", target_os = "linux"))]
fn normalize(error: keyring::Error) -> CredentialError {
    match error {
        keyring::Error::NoEntry => CredentialError::Missing,
        _ => CredentialError::Unavailable,
    }
}
impl CredentialStore for NativeCredentialStore {
    fn get(&self, reference: &str) -> Result<Secret> {
        validate_reference(reference)?;
        let _guard = NATIVE_ACCESS
            .lock()
            .map_err(|_| CredentialError::Unavailable)?;
        #[cfg(any(target_os = "macos", target_os = "windows", target_os = "linux"))]
        {
            let value = keyring::Entry::new(&self.service, reference)
                .map_err(normalize)?
                .get_password()
                .map_err(normalize)?;
            if value.len() > MAX_SECRET_BYTES {
                return Err(CredentialError::SecretTooLarge);
            }
            Ok(Secret::new(value))
        }
        #[cfg(not(any(target_os = "macos", target_os = "windows", target_os = "linux")))]
        {
            let _ = &self.service;
            Err(CredentialError::Unavailable)
        }
    }
    fn put(&self, reference: &str, secret: &Secret) -> Result<()> {
        validate_reference(reference)?;
        if secret.expose().len() > MAX_SECRET_BYTES {
            return Err(CredentialError::SecretTooLarge);
        }
        let _guard = NATIVE_ACCESS
            .lock()
            .map_err(|_| CredentialError::Unavailable)?;
        #[cfg(any(target_os = "macos", target_os = "windows", target_os = "linux"))]
        {
            keyring::Entry::new(&self.service, reference)
                .map_err(normalize)?
                .set_password(secret.expose())
                .map_err(normalize)
        }
        #[cfg(not(any(target_os = "macos", target_os = "windows", target_os = "linux")))]
        {
            Err(CredentialError::Unavailable)
        }
    }
    fn delete(&self, reference: &str) -> Result<()> {
        validate_reference(reference)?;
        let _guard = NATIVE_ACCESS
            .lock()
            .map_err(|_| CredentialError::Unavailable)?;
        #[cfg(any(target_os = "macos", target_os = "windows", target_os = "linux"))]
        {
            match keyring::Entry::new(&self.service, reference)
                .map_err(normalize)?
                .delete_credential()
                .map_err(normalize)
            {
                Err(CredentialError::Missing) => Ok(()),
                result => result,
            }
        }
        #[cfg(not(any(target_os = "macos", target_os = "windows", target_os = "linux")))]
        {
            Err(CredentialError::Unavailable)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn unavailable_backend_never_accepts_a_secret() {
        assert!(matches!(
            UnavailableStore.get("reference"),
            Err(CredentialError::Unavailable)
        ));
        assert_eq!(
            UnavailableStore.put("reference", &Secret::new("sensitive")),
            Err(CredentialError::Unavailable)
        );
        assert_eq!(
            UnavailableStore.delete("reference"),
            Err(CredentialError::Unavailable)
        );
    }
    #[cfg(any(target_os = "macos", target_os = "windows", target_os = "linux"))]
    #[test]
    fn native_diagnostics_are_normalized_without_backend_payloads() {
        assert_eq!(normalize(keyring::Error::NoEntry), CredentialError::Missing);
        let error = normalize(keyring::Error::Invalid(
            "sensitive".into(),
            "private".into(),
        ));
        assert_eq!(error, CredentialError::Unavailable);
        assert!(!format!("{error:?} {error}").contains("private"));
    }
    #[test]
    fn rejects_invalid_references_without_os_access() {
        let store = NativeCredentialStore::new();
        for reference in ["", "a\0b", &"x".repeat(257)] {
            assert!(matches!(
                store.get(reference),
                Err(CredentialError::InvalidReference)
            ));
            assert_eq!(
                store.put(reference, &Secret::new("sensitive")),
                Err(CredentialError::InvalidReference)
            );
            assert_eq!(
                store.delete(reference),
                Err(CredentialError::InvalidReference)
            );
        }
    }
    #[test]
    fn oversized_secret_rejected_without_os_access() {
        assert_eq!(
            NativeCredentialStore::new()
                .put("test", &Secret::new("x".repeat(MAX_SECRET_BYTES + 1))),
            Err(CredentialError::SecretTooLarge)
        );
    }
    #[test]
    fn namespaces_and_errors_are_safe() {
        for invalid in [
            "",
            SERVICE,
            "org.choscordb.tests.",
            "org.choscordb.tests.a\0b",
        ] {
            assert!(matches!(
                NativeCredentialStore::for_test_namespace(invalid),
                Err(CredentialError::InvalidNamespace)
            ));
        }
        assert!(
            NativeCredentialStore::for_test_namespace("org.choscordb.tests.credentials").is_ok()
        );
        for error in [
            CredentialError::Missing,
            CredentialError::Unavailable,
            CredentialError::InvalidReference,
            CredentialError::SecretTooLarge,
            CredentialError::InvalidNamespace,
        ] {
            assert!(!error.to_string().contains("sensitive"));
        }
    }
}
