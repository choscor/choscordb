use choscordb_storage::{ConnectionProfile, Storage};

#[test]
fn hop_references_roundtrip_and_duplicate_and_delete_preserve_ownership() {
    let value = serde_json::json!({
        "id":"hops", "name":"Hops", "group_id":null, "credential_ref":null,
        "ssh_jump_credential_refs":{"first":"opaque-first","second":"opaque-second"},
        "configuration":{
            "driver":"postgres", "host":"localhost", "port":5432,
            "user":"alice", "database":"example",
            "tls":{"mode":"VerifyFull","root_certificate_path":null},
            "ssh":{"host":"target.example","port":22,"user":"target",
                "authentication":"agent", "options":{"jump_hosts":[
                    {"id":"first","host":"first.example","port":22,"user":"one","authentication":"password"},
                    {"id":"second","host":"second.example","port":2222,"user":"two","authentication":"password"}
                ]}}
        }
    });
    let profile: ConnectionProfile = serde_json::from_value(value).unwrap();
    let mut storage = Storage::in_memory().unwrap();
    storage.save_profile(&profile).unwrap();
    assert_eq!(storage.profile("hops").unwrap().unwrap(), profile);
    let duplicate = storage.duplicate_profile("hops", "copy", "Copy").unwrap();
    let copied = serde_json::to_value(&duplicate).unwrap();
    assert!(copied.get("ssh_jump_credential_refs").is_none());
    assert_eq!(duplicate.configuration, profile.configuration);
    assert!(storage.credential_is_referenced("opaque-first").unwrap());
    assert!(storage.credential_is_referenced("opaque-second").unwrap());
    storage.delete_profile("hops").unwrap();
    let cleanup = storage.pending_credential_cleanup().unwrap();
    assert!(cleanup.contains(&"opaque-first".to_owned()));
    assert!(cleanup.contains(&"opaque-second".to_owned()));
    assert!(!storage.credential_is_referenced("opaque-first").unwrap());
}

#[test]
fn storage_rejects_references_without_a_matching_credential_hop() {
    let mut profile: ConnectionProfile = serde_json::from_value(serde_json::json!({
        "id":"hops", "name":"Hops", "group_id":null, "credential_ref":null,
        "ssh_jump_credential_refs":{"ghost":"opaque-secret"},
        "configuration":{"driver":"sqlite","path":":memory:","read_only":false}
    }))
    .unwrap();
    assert!(profile.validate().is_err());
    profile.ssh_jump_credential_refs.clear();
    assert!(profile.validate().is_ok());
}
