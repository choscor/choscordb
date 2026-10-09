use choscordb_core::profile_draft::{
    DraftAction, DraftField, DraftSecrets, ProfileDraft, SecretDraft, SecretUse, default_port,
    duplicate_identity, field_error, has_saved_credentials, normalize, port_for_driver,
    private_key_text_error, saved_action, secret_use, with_defaults,
};

fn server(driver: &str) -> ProfileDraft {
    ProfileDraft {
        id: "id".into(),
        name: "Server".into(),
        driver: driver.into(),
        host: "db.example".into(),
        port: 5432,
        user: "app".into(),
        tls: "prefer".into(),
        ssh_port: 22,
        ssh_authentication: "agent".into(),
        ssh_identity_source: "file".into(),
        ..Default::default()
    }
}

fn ssh(mut draft: ProfileDraft, authentication: &str) -> ProfileDraft {
    draft.ssh_enabled = true;
    draft.ssh_host = "bastion".into();
    draft.ssh_user = "operator".into();
    draft.ssh_authentication = authentication.into();
    draft
}

fn secret(text: &str, modified: bool) -> SecretDraft<'_> {
    SecretDraft { text, modified }
}

fn field(draft: &ProfileDraft, secrets: &DraftSecrets<'_>) -> Option<DraftField> {
    field_error(draft, secrets).map(|error| error.field)
}

#[test]
fn server_drivers_use_their_standard_ports() {
    assert_eq!(default_port("postgres"), 5432);
    assert_eq!(default_port("mysql"), 3306);
}

#[test]
fn switching_driver_moves_only_a_default_port() {
    assert_eq!(port_for_driver(5432, "mysql"), 3306);
    assert_eq!(port_for_driver(3306, "postgres"), 5432);
    assert_eq!(port_for_driver(6543, "mysql"), 6543);
    assert_eq!(port_for_driver(5432, "sqlite"), 5432);
}

#[test]
fn new_and_legacy_drafts_get_form_defaults() {
    let filled = with_defaults(ProfileDraft::default());
    assert_eq!(filled.driver, "sqlite");
    assert_eq!(filled.host, "localhost");
    assert_eq!(filled.port, 5432);
    assert_eq!(filled.tls, "prefer");
    assert_eq!(filled.ssh_port, 22);
    assert_eq!(filled.ssh_authentication, "agent");
    assert_eq!(filled.ssh_identity_source, "file");

    let mysql = with_defaults(ProfileDraft {
        driver: "mysql".into(),
        ..Default::default()
    });
    assert_eq!(mysql.port, 3306);

    let legacy_key = with_defaults(ProfileDraft {
        driver: "postgres".into(),
        host: "saved.example".into(),
        port: 6000,
        tls: "require".into(),
        ssh_identity_file: "/keys/id".into(),
        ..Default::default()
    });
    assert_eq!(legacy_key.ssh_authentication, "public_key");
    assert_eq!(legacy_key.host, "saved.example");
    assert_eq!(legacy_key.port, 6000);
    assert_eq!(legacy_key.tls, "require");
}

#[test]
fn normalizing_assigns_an_id_and_drops_removed_settings() {
    let original = server("postgres");
    let mut edited = original.clone();
    edited.id.clear();
    edited.proxy_options = r#"{"protocol":"socks5"}"#.into();
    edited.proxy_credential_ref = "proxy-ref".into();
    let normalized = normalize(&original, edited, &DraftSecrets::default());
    assert!(!normalized.id.is_empty());
    assert_ne!(normalized.id, "id");
    assert!(normalized.proxy_options.is_empty());
    assert!(normalized.proxy_credential_ref.is_empty());

    let kept = normalize(&original, original.clone(), &DraftSecrets::default());
    assert_eq!(kept.id, "id");
}

#[test]
fn sqlite_never_keeps_an_ssh_tunnel() {
    let mut original = ssh(server("postgres"), "agent");
    original.driver = "sqlite".into();
    let normalized = normalize(&original, original.clone(), &DraftSecrets::default());
    assert!(!normalized.ssh_enabled);
}

#[test]
fn clearing_the_password_field_forgets_the_saved_password() {
    let mut original = server("postgres");
    original.credential_ref = "saved".into();
    let cleared = normalize(
        &original,
        original.clone(),
        &DraftSecrets {
            database: secret("", true),
            ..Default::default()
        },
    );
    assert!(cleared.credential_ref.is_empty());
    let untouched = normalize(&original, original.clone(), &DraftSecrets::default());
    assert_eq!(untouched.credential_ref, "saved");
}

#[test]
fn changing_ssh_authentication_or_key_file_forgets_the_saved_ssh_secret() {
    let mut original = ssh(server("postgres"), "public_key");
    original.ssh_identity_file = "/keys/a".into();
    original.ssh_credential_ref = "ssh-saved".into();

    let same = normalize(&original, original.clone(), &DraftSecrets::default());
    assert_eq!(same.ssh_credential_ref, "ssh-saved");

    let mut other_file = original.clone();
    other_file.ssh_identity_file = "/keys/b".into();
    assert!(
        normalize(&original, other_file, &DraftSecrets::default())
            .ssh_credential_ref
            .is_empty()
    );

    let password = ssh(original.clone(), "password");
    let normalized = normalize(&original, password, &DraftSecrets::default());
    assert!(normalized.ssh_credential_ref.is_empty());
    assert!(normalized.ssh_identity_file.is_empty());
}

#[test]
fn inline_keys_keep_their_reference_only_while_inline_public_key_is_selected() {
    let mut original = ssh(server("postgres"), "public_key");
    original.ssh_identity_source = "inline".into();
    original.ssh_private_key_ref = "key-saved".into();

    let same = normalize(&original, original.clone(), &DraftSecrets::default());
    assert_eq!(same.ssh_private_key_ref, "key-saved");
    assert!(same.ssh_identity_file.is_empty());

    let mut file = original.clone();
    file.ssh_identity_source = "file".into();
    file.ssh_identity_file = "/keys/a".into();
    let normalized = normalize(&original, file, &DraftSecrets::default());
    assert!(normalized.ssh_private_key_ref.is_empty());
    assert_eq!(normalized.ssh_identity_file, "/keys/a");

    let agent = ssh(original.clone(), "agent");
    let normalized = normalize(&original, agent, &DraftSecrets::default());
    assert!(normalized.ssh_private_key_ref.is_empty());
    assert_eq!(normalized.ssh_identity_source, "file");
}

#[test]
fn secrets_apply_only_to_the_transport_that_uses_them() {
    assert_eq!(
        secret_use(&ProfileDraft {
            driver: "sqlite".into(),
            ..Default::default()
        }),
        SecretUse::default()
    );
    let mut draft = ssh(server("mysql"), "agent");
    draft.tls_client_identity = "/certs/client.p12".into();
    assert_eq!(
        secret_use(&draft),
        SecretUse {
            database: true,
            ssh: false,
            tls: true,
            ssh_private_key: false
        }
    );
    draft.ssh_authentication = "public_key".into();
    draft.ssh_identity_source = "inline".into();
    let used = secret_use(&draft);
    assert!(used.ssh && used.ssh_private_key);
}

#[test]
fn saving_replaces_typed_secrets_and_keeps_untouched_saved_ones() {
    use DraftAction::{Clear, Keep, Replace};
    assert_eq!(
        saved_action(true, true, secret("pw", false), false),
        Replace
    );
    assert_eq!(saved_action(true, true, secret("pw", true), true), Replace);
    assert_eq!(saved_action(true, true, secret("", true), true), Clear);
    assert_eq!(saved_action(true, true, secret("", false), true), Keep);
    assert_eq!(saved_action(true, true, secret("", false), false), Clear);
    // Unchecked "save credentials" and unused secrets are never stored.
    assert_eq!(saved_action(true, false, secret("pw", true), true), Clear);
    assert_eq!(saved_action(false, true, secret("pw", true), true), Clear);
}

#[test]
fn saved_references_report_saved_credentials() {
    assert!(!has_saved_credentials(&server("postgres")));
    for draft in [
        ProfileDraft {
            credential_ref: "a".into(),
            ..Default::default()
        },
        ProfileDraft {
            ssh_credential_ref: "a".into(),
            ..Default::default()
        },
        ProfileDraft {
            ssh_private_key_ref: "a".into(),
            ..Default::default()
        },
        ProfileDraft {
            tls_credential_ref: "a".into(),
            ..Default::default()
        },
        ProfileDraft {
            has_ssh_hop_credentials: true,
            ..Default::default()
        },
    ] {
        assert!(has_saved_credentials(&draft));
    }
}

#[test]
fn invalid_drafts_report_the_field_to_fix() {
    let none = DraftSecrets::default();
    let mut unnamed = server("postgres");
    unnamed.name = "  ".into();
    assert_eq!(field(&unnamed, &none), Some(DraftField::Name));

    let mut long_name = server("postgres");
    long_name.name = "n".repeat(1025);
    assert_eq!(field(&long_name, &none), Some(DraftField::Name));

    let sqlite = |path: &str| ProfileDraft {
        name: "Local".into(),
        driver: "sqlite".into(),
        path: path.into(),
        ..Default::default()
    };
    assert_eq!(field(&sqlite(" "), &none), Some(DraftField::Path));
    assert_eq!(
        field(&sqlite("file:db?mode=bogus"), &none),
        Some(DraftField::Path)
    );
    assert_eq!(field(&sqlite(":memory:"), &none), None);

    for host in [
        "",
        "postgres://",
        "user@",
        "host:port",
        "a b",
        "[::1]:5432",
        "[invalid]",
    ] {
        let mut draft = server("postgres");
        draft.host = host.into();
        assert_eq!(field(&draft, &none), Some(DraftField::Host), "{host}");
    }
    let mut ipv6 = server("postgres");
    ipv6.host = "::1".into();
    assert_eq!(field(&ipv6, &none), None);

    let mut socket = ssh(server("postgres"), "agent");
    socket.host = "/var/run/postgresql".into();
    assert_eq!(field(&socket, &none), Some(DraftField::SshEnabled));
    let mut verified_socket = server("postgres");
    verified_socket.host = "/var/run/postgresql".into();
    verified_socket.tls = "verify_full".into();
    assert_eq!(field(&verified_socket, &none), Some(DraftField::Host));

    let mut no_user = server("postgres");
    no_user.user.clear();
    assert_eq!(field(&no_user, &none), Some(DraftField::User));
    let mut anonymous_mysql = server("mysql");
    anonymous_mysql.user.clear();
    assert_eq!(field(&anonymous_mysql, &none), None);

    let mut ssh_host = ssh(server("postgres"), "agent");
    ssh_host.ssh_host = "operator@bastion".into();
    assert_eq!(field(&ssh_host, &none), Some(DraftField::SshHost));
    let mut ssh_user = ssh(server("postgres"), "agent");
    ssh_user.ssh_user.clear();
    assert_eq!(field(&ssh_user, &none), Some(DraftField::SshUser));
    let key_file = ssh(server("postgres"), "public_key");
    assert_eq!(field(&key_file, &none), Some(DraftField::SshIdentityFile));

    let password = ssh(server("postgres"), "password");
    assert_eq!(field(&password, &none), Some(DraftField::SshSecret));
    let typed = DraftSecrets {
        ssh: secret("pw", true),
        ..Default::default()
    };
    assert_eq!(field(&password, &typed), None);
    let mut saved = password.clone();
    saved.ssh_credential_ref = "saved".into();
    assert_eq!(field(&saved, &none), None);
}

#[test]
fn oversized_secrets_report_their_own_field() {
    let large = "x".repeat(16 * 1024 + 1);
    let draft = server("postgres");
    let secrets = DraftSecrets {
        database: secret(&large, true),
        ..Default::default()
    };
    let error = field_error(&draft, &secrets).expect("oversized password");
    assert_eq!(error.field, DraftField::Password);
    assert!(error.message.contains("Credential exceeds"));

    // A secret the transport does not use is not checked.
    let sqlite = ProfileDraft {
        name: "Local".into(),
        driver: "sqlite".into(),
        path: ":memory:".into(),
        ..Default::default()
    };
    assert_eq!(field_error(&sqlite, &secrets), None);

    let mut identity = server("postgres");
    identity.tls_client_identity = "/certs/client.p12".into();
    let tls = DraftSecrets {
        tls: secret(&large, true),
        ..Default::default()
    };
    assert_eq!(field(&identity, &tls), Some(DraftField::TlsSecret));
}

#[test]
fn a_duplicate_gets_a_fresh_id_and_a_copy_name() {
    let (first, name) = duplicate_identity("Reporting");
    let (second, _) = duplicate_identity("Reporting");
    assert_eq!(name, "Reporting copy");
    assert!(!first.is_empty());
    assert_ne!(first, second);
}

#[test]
fn defaults_give_a_new_profile_an_id_and_keep_an_existing_one() {
    let first = with_defaults(ProfileDraft::default());
    let second = with_defaults(ProfileDraft::default());
    assert_eq!(first.id.len(), 36);
    assert_ne!(first.id, second.id);
    assert_eq!(with_defaults(server("mysql")).id, "id");
}

#[test]
fn pasted_private_keys_must_be_bounded_text_without_nul() {
    let key = "-----BEGIN OPENSSH PRIVATE KEY-----\nmaterial\n-----END OPENSSH PRIVATE KEY-----\n";
    assert_eq!(private_key_text_error(key), None);
    assert!(private_key_text_error(&"x".repeat(64 * 1024)).is_none());
    assert!(private_key_text_error(&"x".repeat(64 * 1024 + 1)).is_some());
    assert!(private_key_text_error("key\0material").is_some());
}

#[test]
fn driver_forms_describe_the_fields_each_driver_uses() {
    use choscordb_core::profile_draft::{DriverForm, driver_form};
    assert_eq!(driver_form("sqlite"), DriverForm::default());
    assert_eq!(
        driver_form("postgres"),
        DriverForm {
            server: true,
            user_optional: false,
            database_selects_server: false,
        }
    );
    assert_eq!(
        driver_form("mysql"),
        DriverForm {
            server: true,
            user_optional: true,
            database_selects_server: true,
        }
    );
}
