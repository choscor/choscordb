use choscordb_storage::{ConnectionProfile, Storage};

#[test]
fn tls_identity_profile_preserves_policy_and_queues_replaced_credential() {
    let json = serde_json::json!({"id":"tls", "name":"TLS", "group_id":null,
        "credential_ref":null,"tls_credential_ref":"identity-passphrase",
        "configuration":{"driver":"postgres","host":"localhost","port":5432,"database":"app","user":"reader",
        "tls":{"mode":"VerifyCa","root_certificate_path":"/certs/ca.pem","client_identity_path":"/certs/client.p12"}}});
    let mut profile: ConnectionProfile = serde_json::from_value(json.clone()).expect("TLS profile");
    let mut storage = Storage::in_memory().unwrap();
    storage.save_profile(&profile).unwrap();
    assert_eq!(
        serde_json::to_value(storage.profile("tls").unwrap().unwrap()).unwrap(),
        json
    );
    assert!(storage.pending_credential_cleanup().unwrap().is_empty());
    let duplicate = storage.duplicate_profile("tls", "copy", "Copy").unwrap();
    assert!(duplicate.tls_credential_ref.is_none());
    profile.tls_credential_ref = Some("replacement".into());
    storage.save_profile(&profile).unwrap();
    assert_eq!(
        storage.pending_credential_cleanup().unwrap(),
        ["identity-passphrase"]
    );
    storage.delete_profile("tls").unwrap();
    assert_eq!(
        storage.pending_credential_cleanup().unwrap(),
        ["identity-passphrase", "replacement"]
    );
}
