//! Signed update metadata and downloaded package verification.
mod install;
mod network;
mod preferences;
mod readiness;
use base64::Engine as _;
use ed25519_dalek::{Signature, Verifier, VerifyingKey};
pub use install::{
    InstallCommand, InstallError, launch_prepared_install, prepare_windows_install,
    take_windows_install_failure_marker,
};
#[cfg(target_os = "linux")]
pub use install::{
    LinuxHelperRequest, prepare_linux_helper, prepare_linux_install, run_linux_helper,
};
pub use network::{
    CheckedUpdate, NetworkError, StagedUpdate, StagingLocation, UpdateNetworkService,
};
pub use preferences::{UpdatePreferenceError, UpdatePreferenceStore};
pub use readiness::{wait_for_update_readiness, write_update_readiness_file};
use serde_json::Value;
use sha2::{Digest, Sha256};
use std::{fs::File, io::Read, path::Path};

const MAX_FEED_BYTES: usize = 32_768;
const MAX_NOTES_UTF16_UNITS: usize = 8_192;
const MAX_EXACT_JSON_INTEGER: f64 = 9_007_199_254_740_991.0;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct UpdateRecord {
    pub version: String,
    pub url: String,
    pub size: u64,
    pub sha256: [u8; 32],
    pub notes: String,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, thiserror::Error)]
pub enum UpdateError {
    #[error("Update feed is too large")]
    FeedTooLarge,
    #[error("Invalid update feed")]
    InvalidFeed,
    #[error("Unsupported update signing key")]
    UnsupportedSigningKey,
    #[error("Update signature is invalid")]
    InvalidSignature,
    #[error("Invalid signed update record")]
    InvalidSignedRecord,
    #[error("Update version or platform does not match")]
    VersionOrPlatformMismatch,
    #[error("Invalid signed package details")]
    InvalidPackageDetails,
    #[error("Unexpected update package URL")]
    UnexpectedPackageUrl,
    #[error("Downloaded package has the wrong size")]
    WrongPackageSize,
    #[error("Could not verify downloaded package")]
    PackageReadFailed,
    #[error("Downloaded package failed integrity verification")]
    PackageIntegrityFailed,
    #[error("Downloaded package verification was cancelled")]
    VerificationCancelled,
}

pub fn parse_signed_update_metadata(
    envelope: &[u8],
    public_key: &[u8],
    current_version: &str,
    expected_platform: &str,
    expected_arch: &str,
    repository: &str,
) -> Result<Option<UpdateRecord>, UpdateError> {
    if envelope.len() > MAX_FEED_BYTES {
        return Err(UpdateError::FeedTooLarge);
    }
    let outer: Value = serde_json::from_slice(envelope).map_err(|_| UpdateError::InvalidFeed)?;
    let wrapper = outer.as_object().ok_or(UpdateError::InvalidFeed)?;
    if wrapper.get("key_id").and_then(Value::as_str) != Some("windows-linux-v1") {
        return Err(UpdateError::UnsupportedSigningKey);
    }
    let payload = canonical_base64(wrapper.get("payload").and_then(Value::as_str))
        .ok_or(UpdateError::InvalidSignature)?;
    let signature = canonical_base64(wrapper.get("signature").and_then(Value::as_str))
        .ok_or(UpdateError::InvalidSignature)?;
    let key: &[u8; 32] = public_key
        .try_into()
        .map_err(|_| UpdateError::InvalidSignature)?;
    let signature: &[u8; 64] = signature
        .as_slice()
        .try_into()
        .map_err(|_| UpdateError::InvalidSignature)?;
    let key = VerifyingKey::from_bytes(key).map_err(|_| UpdateError::InvalidSignature)?;
    key.verify(&payload, &Signature::from_bytes(signature))
        .map_err(|_| UpdateError::InvalidSignature)?;

    let inner: Value =
        serde_json::from_slice(&payload).map_err(|_| UpdateError::InvalidSignedRecord)?;
    let record = inner.as_object().ok_or(UpdateError::InvalidSignedRecord)?;
    let version = record
        .get("version")
        .and_then(Value::as_str)
        .unwrap_or_default();
    let candidate = version_parts(version).ok_or(UpdateError::VersionOrPlatformMismatch)?;
    let current = version_parts(current_version).ok_or(UpdateError::VersionOrPlatformMismatch)?;
    if record.get("platform").and_then(Value::as_str) != Some(expected_platform)
        || record.get("arch").and_then(Value::as_str) != Some(expected_arch)
    {
        return Err(UpdateError::VersionOrPlatformMismatch);
    }

    let size = record
        .get("size")
        .and_then(Value::as_f64)
        .ok_or(UpdateError::InvalidPackageDetails)?;
    let digest = record
        .get("sha256")
        .and_then(Value::as_str)
        .unwrap_or_default();
    let notes = record
        .get("notes")
        .and_then(Value::as_str)
        .ok_or(UpdateError::InvalidPackageDetails)?;
    if !(1.0..=MAX_EXACT_JSON_INTEGER).contains(&size)
        || size.fract() != 0.0
        || notes.encode_utf16().count() > MAX_NOTES_UTF16_UNITS
    {
        return Err(UpdateError::InvalidPackageDetails);
    }
    let sha256 = digest_bytes(digest).ok_or(UpdateError::InvalidPackageDetails)?;

    let asset = if expected_platform == "windows" {
        format!("ChoscorDB-{version}-windows-x64-setup.exe")
    } else {
        format!("ChoscorDB-{version}-linux-x86_64.AppImage")
    };
    let expected_url =
        format!("https://github.com/{repository}/releases/download/v{version}/{asset}");
    let url = record
        .get("url")
        .and_then(Value::as_str)
        .unwrap_or_default();
    if url != expected_url || url::Url::parse(url).is_err() {
        return Err(UpdateError::UnexpectedPackageUrl);
    }
    if !newer_version(candidate, current) {
        return Ok(None);
    }
    Ok(Some(UpdateRecord {
        version: version.to_owned(),
        url: url.to_owned(),
        size: size as u64,
        sha256,
        notes: notes.to_owned(),
    }))
}

pub fn verify_update_file(path: &Path, record: &UpdateRecord) -> Result<(), UpdateError> {
    verify_update_file_with_cancel(path, record, || false)
}

pub fn verify_update_file_with_cancel(
    path: &Path,
    record: &UpdateRecord,
    mut cancelled: impl FnMut() -> bool,
) -> Result<(), UpdateError> {
    let mut file = File::open(path).map_err(|_| UpdateError::WrongPackageSize)?;
    if file
        .metadata()
        .map_err(|_| UpdateError::WrongPackageSize)?
        .len()
        != record.size
    {
        return Err(UpdateError::WrongPackageSize);
    }
    let mut hash = Sha256::new();
    let mut buffer = [0u8; 1024 * 1024];
    loop {
        if cancelled() {
            return Err(UpdateError::VerificationCancelled);
        }
        match file.read(&mut buffer) {
            Ok(0) => break,
            Ok(size) => hash.update(&buffer[..size]),
            Err(_) => return Err(UpdateError::PackageReadFailed),
        }
    }
    if hash.finalize().as_slice() != record.sha256 {
        return Err(UpdateError::PackageIntegrityFailed);
    }
    Ok(())
}

fn canonical_base64(value: Option<&str>) -> Option<Vec<u8>> {
    let encoded = value?;
    let decoded = base64::engine::general_purpose::STANDARD
        .decode(encoded)
        .ok()?;
    if decoded.is_empty() || base64::engine::general_purpose::STANDARD.encode(&decoded) != encoded {
        return None;
    }
    Some(decoded)
}

fn version_parts(value: &str) -> Option<[&str; 3]> {
    let mut parts = value.split('.');
    let result = [parts.next()?, parts.next()?, parts.next()?];
    if parts.next().is_some()
        || result.iter().any(|part| {
            part.is_empty()
                || (part.len() > 1 && part.starts_with('0'))
                || !part.bytes().all(|byte| byte.is_ascii_digit())
        })
    {
        return None;
    }
    Some(result)
}

fn newer_version(candidate: [&str; 3], current: [&str; 3]) -> bool {
    for (left, right) in candidate.into_iter().zip(current) {
        if left.len() != right.len() {
            return left.len() > right.len();
        }
        if left != right {
            return left > right;
        }
    }
    false
}

fn digest_bytes(value: &str) -> Option<[u8; 32]> {
    if value.len() != 64
        || !value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        return None;
    }
    let mut bytes = [0u8; 32];
    for (index, chunk) in value.as_bytes().chunks_exact(2).enumerate() {
        bytes[index] = u8::from_str_radix(std::str::from_utf8(chunk).ok()?, 16).ok()?;
    }
    Some(bytes)
}
