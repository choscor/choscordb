use base64::Engine as _;
use choscordb_update::{
    UpdateError, UpdateRecord, parse_signed_update_metadata, verify_update_file,
    verify_update_file_with_cancel,
};
use ed25519_dalek::{Signer as _, SigningKey};
use serde_json::json;
use std::fs;

const KEY: &str = "ebVWLo/mVPlAeLES6KmLp5AfhTrmlb7X4OORC60ElmQ=";
const LINUX_PAYLOAD: &str = concat!(
    "eyJ2ZXJzaW9uIjoiMS4yLjQiLCJwbGF0Zm9ybSI6ImxpbnV4IiwiYXJjaCI6Ing4Nl82NCIsInVybC",
    "I6Imh0dHBzOi8vZ2l0aHViLmNvbS9jaG9zY29yL2Nob3Njb3JkYi9yZWxlYXNlcy9kb3dubG9hZC92",
    "MS4yLjQvQ2hvc2NvckRCLTEuMi40LWxpbnV4LXg4Nl82NC5BcHBJbWFnZSIsInNpemUiOjQsInNoYT",
    "I1NiI6Ijg4ZDQyNjZmZDRlNjMzOGQxM2I4NDVmY2YyODk1NzlkMjA5Yzg5NzgyM2I5MjE3ZGEzZTE2",
    "MTkzNmYwMzE1ODkiLCJub3RlcyI6IlN0YWJsZSBpbXByb3ZlbWVudHMifQ=="
);
const LINUX_SIGNATURE: &str = concat!(
    "t0FQGcniz1p3Qk5S/",
    "2YzFX5NmMDH0Nzf53qMPwvGvjOi5lSgBg1wYzSZcF+Wxo7rqoMGZqcrNCwDv1JjfcUtDQ=="
);
const WINDOWS_PAYLOAD: &str = concat!(
    "eyJ2ZXJzaW9uIjoiMS4yLjQiLCJwbGF0Zm9ybSI6IndpbmRvd3MiLCJhcmNoIjoieDY0IiwidXJsIj",
    "oiaHR0cHM6Ly9naXRodWIuY29tL2Nob3Njb3IvY2hvc2NvcmRiL3JlbGVhc2VzL2Rvd25sb2FkL3Yx",
    "LjIuNC9DaG9zY29yREItMS4yLjQtd2luZG93cy14NjQtc2V0dXAuZXhlIiwic2l6ZSI6NCwic2hhMj",
    "U2IjoiODhkNDI2NmZkNGU2MzM4ZDEzYjg0NWZjZjI4OTU3OWQyMDljODk3ODIzYjkyMTdkYTNlMTYx",
    "OTM2ZjAzMTU4OSIsIm5vdGVzIjoiV2luZG93cyBzdGFibGUifQ=="
);
const WINDOWS_SIGNATURE: &str = concat!(
    "dVZ+",
    "bRkBSbgxm05VsMTRTULH1PzNgQw4Ck4wMsbdBB65VGRNNhZe6MMCSWabJ9PwFsFj42PmFpbPujdszRHqDg=="
);

fn key() -> Vec<u8> {
    base64::engine::general_purpose::STANDARD
        .decode(KEY)
        .unwrap()
}

fn linux_envelope() -> Vec<u8> {
    serde_json::to_vec(&json!({
        "key_id": "windows-linux-v1",
        "payload": LINUX_PAYLOAD,
        "signature": LINUX_SIGNATURE
    }))
    .unwrap()
}

fn windows_envelope() -> Vec<u8> {
    serde_json::to_vec(&json!({
        "key_id": "windows-linux-v1",
        "payload": WINDOWS_PAYLOAD,
        "signature": WINDOWS_SIGNATURE
    }))
    .unwrap()
}

fn signed_record(payload: serde_json::Value) -> (Vec<u8>, [u8; 32]) {
    let signing = SigningKey::from_bytes(&[7u8; 32]);
    let bytes = serde_json::to_vec(&payload).unwrap();
    let envelope = serde_json::to_vec(&json!({
        "key_id": "windows-linux-v1",
        "payload": base64::engine::general_purpose::STANDARD.encode(&bytes),
        "signature": base64::engine::general_purpose::STANDARD.encode(signing.sign(&bytes).to_bytes()),
    })).unwrap();
    (envelope, signing.verifying_key().to_bytes())
}

fn valid_payload() -> serde_json::Value {
    json!({
        "version": "1.2.4",
        "platform": "linux",
        "arch": "x86_64",
        "url": "https://github.com/choscor/choscordb/releases/download/v1.2.4/ChoscorDB-1.2.4-linux-x86_64.AppImage",
        "size": 4,
        "sha256": "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589",
        "notes": "Stable improvements"
    })
}

fn parse_test_record(
    payload: serde_json::Value,
) -> Result<Option<choscordb_update::UpdateRecord>, UpdateError> {
    let (envelope, public_key) = signed_record(payload);
    parse_signed_update_metadata(
        &envelope,
        &public_key,
        "1.2.3",
        "linux",
        "x86_64",
        "choscor/choscordb",
    )
}

#[test]
fn accepts_signed_linux_release_and_verifies_exact_package_bytes() {
    let record = parse_signed_update_metadata(
        &linux_envelope(),
        &key(),
        "1.2.3",
        "linux",
        "x86_64",
        "choscor/choscordb",
    )
    .unwrap()
    .unwrap();
    assert_eq!(record.version, "1.2.4");
    assert_eq!(record.notes, "Stable improvements");
    assert_eq!(
        record.url,
        "https://github.com/choscor/choscordb/releases/download/v1.2.4/ChoscorDB-1.2.4-linux-x86_64.AppImage"
    );
    assert_eq!(record.size, 4);

    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("package");
    fs::write(&path, b"abcd").unwrap();
    assert_eq!(verify_update_file(&path, &record), Ok(()));
    fs::write(&path, b"abce").unwrap();
    assert_eq!(
        verify_update_file(&path, &record),
        Err(UpdateError::PackageIntegrityFailed)
    );
    fs::write(&path, b"abcde").unwrap();
    assert_eq!(
        verify_update_file(&path, &record),
        Err(UpdateError::WrongPackageSize)
    );
}

#[test]
fn accepts_signed_windows_installer_and_rejects_tampered_or_mismatched_feeds() {
    let record = parse_signed_update_metadata(
        &windows_envelope(),
        &key(),
        "1.2.3",
        "windows",
        "x64",
        "choscor/choscordb",
    )
    .unwrap()
    .unwrap();
    assert_eq!(
        record.url,
        "https://github.com/choscor/choscordb/releases/download/v1.2.4/ChoscorDB-1.2.4-windows-x64-setup.exe"
    );

    let mut tampered = serde_json::from_slice::<serde_json::Value>(&linux_envelope()).unwrap();
    let payload = base64::engine::general_purpose::STANDARD
        .decode(LINUX_PAYLOAD)
        .unwrap();
    let modified = String::from_utf8(payload)
        .unwrap()
        .replace("\"size\":4", "\"size\":5");
    tampered["payload"] = json!(base64::engine::general_purpose::STANDARD.encode(modified));
    assert_eq!(
        parse_signed_update_metadata(
            &serde_json::to_vec(&tampered).unwrap(),
            &key(),
            "1.2.3",
            "linux",
            "x86_64",
            "choscor/choscordb"
        ),
        Err(UpdateError::InvalidSignature)
    );

    let mut wrong_key = serde_json::from_slice::<serde_json::Value>(&linux_envelope()).unwrap();
    wrong_key["key_id"] = json!("unknown-key");
    assert_eq!(
        parse_signed_update_metadata(
            &serde_json::to_vec(&wrong_key).unwrap(),
            &key(),
            "1.2.3",
            "linux",
            "x86_64",
            "choscor/choscordb"
        ),
        Err(UpdateError::UnsupportedSigningKey)
    );
    assert_eq!(
        parse_signed_update_metadata(
            &linux_envelope(),
            &key(),
            "1.2.3",
            "windows",
            "x64",
            "choscor/choscordb"
        ),
        Err(UpdateError::VersionOrPlatformMismatch)
    );
    assert_eq!(
        parse_signed_update_metadata(
            &linux_envelope(),
            &key(),
            "1.2.3",
            "linux",
            "arm64",
            "choscor/choscordb"
        ),
        Err(UpdateError::VersionOrPlatformMismatch)
    );
    assert_eq!(
        parse_signed_update_metadata(
            &linux_envelope(),
            &key(),
            "1.2.3",
            "linux",
            "x86_64",
            "fork/project"
        ),
        Err(UpdateError::UnexpectedPackageUrl)
    );
}

#[test]
fn current_or_newer_installed_version_has_no_update() {
    for current in [
        "1.2.4",
        "1.2.5",
        "1.2.1000000000000000000000000000000000000",
    ] {
        assert_eq!(
            parse_signed_update_metadata(
                &linux_envelope(),
                &key(),
                current,
                "linux",
                "x86_64",
                "choscor/choscordb"
            ),
            Ok(None)
        );
    }
    assert_eq!(
        parse_signed_update_metadata(
            &linux_envelope(),
            &key(),
            "01.2.3",
            "linux",
            "x86_64",
            "choscor/choscordb"
        ),
        Err(UpdateError::VersionOrPlatformMismatch)
    );
}

#[test]
fn rejects_invalid_signed_package_details_and_unexpected_urls() {
    for size in [json!(0), json!(4.5), json!(9007199254740992u64)] {
        let mut payload = valid_payload();
        payload["size"] = size;
        assert_eq!(
            parse_test_record(payload),
            Err(UpdateError::InvalidPackageDetails)
        );
    }
    let mut payload = valid_payload();
    payload["sha256"] = json!("88D4266FD4E6338D13B845FCF289579D209C897823B9217DA3E161936F031589");
    assert_eq!(
        parse_test_record(payload),
        Err(UpdateError::InvalidPackageDetails)
    );
    let mut payload = valid_payload();
    payload["notes"] = json!("🙂".repeat(4_097));
    assert_eq!(
        parse_test_record(payload),
        Err(UpdateError::InvalidPackageDetails)
    );
    let mut payload = valid_payload();
    payload["url"] = json!("https://attacker.invalid/package");
    assert_eq!(
        parse_test_record(payload),
        Err(UpdateError::UnexpectedPackageUrl)
    );
    let mut payload = valid_payload();
    payload["version"] = json!("01.2.4");
    assert_eq!(
        parse_test_record(payload),
        Err(UpdateError::VersionOrPlatformMismatch)
    );
}

#[test]
fn rejects_oversized_or_malformed_envelope_before_update_decision() {
    assert_eq!(
        parse_signed_update_metadata(
            &vec![b'x'; 32_769],
            &key(),
            "1.2.3",
            "linux",
            "x86_64",
            "choscor/choscordb"
        ),
        Err(UpdateError::FeedTooLarge)
    );
    assert_eq!(
        parse_signed_update_metadata(
            b"[]",
            &key(),
            "1.2.3",
            "linux",
            "x86_64",
            "choscor/choscordb"
        ),
        Err(UpdateError::InvalidFeed)
    );
    let mut envelope = serde_json::from_slice::<serde_json::Value>(&linux_envelope()).unwrap();
    envelope["payload"] = json!("not valid base64");
    assert_eq!(
        parse_signed_update_metadata(
            &serde_json::to_vec(&envelope).unwrap(),
            &key(),
            "1.2.3",
            "linux",
            "x86_64",
            "choscor/choscordb"
        ),
        Err(UpdateError::InvalidSignature)
    );
}

#[test]
fn package_hashing_can_be_cancelled_between_chunks() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("large-package");
    fs::write(&path, vec![b'x'; 3 * 1024 * 1024]).unwrap();
    let record = UpdateRecord {
        version: "1.2.4".into(),
        url: String::new(),
        size: 3 * 1024 * 1024,
        sha256: [0; 32],
        notes: String::new(),
    };
    let mut checks = 0;
    assert_eq!(
        verify_update_file_with_cancel(&path, &record, || {
            checks += 1;
            checks == 3
        }),
        Err(UpdateError::VerificationCancelled)
    );
    assert_eq!(checks, 3);
}

#[test]
fn verifies_package_on_a_small_stack_thread() {
    let record = parse_signed_update_metadata(
        &linux_envelope(),
        &key(),
        "1.2.3",
        "linux",
        "x86_64",
        "choscor/choscordb",
    )
    .unwrap()
    .unwrap();
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("package");
    fs::write(&path, b"abcd").unwrap();
    let verified = std::thread::Builder::new()
        .stack_size(512 * 1024)
        .spawn(move || verify_update_file(&path, &record))
        .unwrap()
        .join()
        .unwrap();
    assert_eq!(verified, Ok(()));
}
