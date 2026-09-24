#![cfg(unix)]
use choscordb_core::{ConnectionProfile, Engine, Event, ProfileSecrets, SubmitError};
use choscordb_credentials::Secret;
use choscordb_driver_api::{SshHostKeyCandidate, SshHostKeyTarget};
use std::time::{Duration, Instant};

fn event(engine: &mut Engine) -> Event {
    let until = Instant::now() + Duration::from_secs(5);
    loop {
        if let Some(event) = engine.try_event() {
            return event;
        }
        assert!(Instant::now() < until, "missing SSH trust event");
        std::thread::sleep(Duration::from_millis(2));
    }
}

#[test]
fn approval_is_nonblocking_and_rejects_a_mismatched_fingerprint_without_writing() {
    let mut engine = Engine::new(Default::default(), vec![]).unwrap();
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("known_hosts");
    let candidate = SshHostKeyCandidate {
        target: SshHostKeyTarget::Target,
        original_host: "ssh.example".into(),
        hostname: "ssh.example".into(),
        port: 22,
        host_key_alias: None,
        key_type: "ssh-ed25519".into(),
        public_key: "invalid".into(),
        sha256: "SHA256:not-a-real-fingerprint".into(),
    };
    engine
        .approve_ssh_host_key(candidate, "SHA256:different".into(), path.clone(), 41)
        .unwrap();
    assert!(matches!(
        event(&mut engine),
        Event::SshHostKeyFailed {
            request_token: 41,
            ..
        }
    ));
    assert!(!path.exists());
}

#[test]
fn inspection_rejects_target_secrets_and_nonprefix_hop_credentials() {
    let mut engine = Engine::new(Default::default(), vec![]).unwrap();
    let profile: ConnectionProfile = serde_json::from_value(serde_json::json!({
        "id":"ssh", "name":"SSH", "group_id":null, "credential_ref":null,
        "configuration":{"driver":"postgres","host":"db.example","port":5432,
            "user":"alice","database":"db","tls":{"mode":"Disable","root_certificate_path":null},
            "ssh":{"host":"target.example","port":22,"user":"target","authentication":"agent",
                "options":{"jump_hosts":[
                    {"id":"first","host":"first.example","port":22,"user":"first","authentication":"password"},
                    {"id":"second","host":"second.example","port":22,"user":"second","authentication":"password"}
                ]}}}
    })).unwrap();
    let mut target_secret = ProfileSecrets {
        ssh: Some(Secret::new("never-send-target")),
        ..Default::default()
    };
    assert!(matches!(
        engine.inspect_profile_ssh_host_keys(
            profile.clone(),
            std::mem::take(&mut target_secret),
            SshHostKeyTarget::Jump("first".into()),
            1
        ),
        Err(SubmitError::InvalidInput)
    ));
    let mut nonprefix = ProfileSecrets::default();
    nonprefix
        .ssh_jumps
        .insert("second".into(), Secret::new("never-send-second"));
    assert!(matches!(
        engine.inspect_profile_ssh_host_keys(
            profile,
            nonprefix,
            SshHostKeyTarget::Jump("first".into()),
            2
        ),
        Err(SubmitError::InvalidInput)
    ));
}
