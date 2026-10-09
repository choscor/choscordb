//! Typed transport for the profile form's draft policy in `choscordb_core::profile_draft`.
use crate::ffi;
use choscordb_core::profile_draft::{self, DraftAction, DraftSecrets, ProfileDraft, SecretDraft};

fn draft(dto: &ffi::ProfileDto) -> ProfileDraft {
    ProfileDraft {
        id: dto.id.clone(),
        name: dto.name.clone(),
        driver: dto.driver.clone(),
        path: dto.path.clone(),
        read_only: dto.read_only,
        host: dto.host.clone(),
        port: dto.port,
        user: dto.user.clone(),
        tls: dto.tls.clone(),
        tls_client_identity: dto.tls_client_identity.clone(),
        credential_ref: dto.credential_ref.clone(),
        tls_credential_ref: dto.tls_credential_ref.clone(),
        proxy_options: dto.proxy_options.clone(),
        proxy_credential_ref: dto.proxy_credential_ref.clone(),
        ssh_enabled: dto.ssh_enabled,
        ssh_host: dto.ssh_host.clone(),
        ssh_port: dto.ssh_port,
        ssh_user: dto.ssh_user.clone(),
        ssh_authentication: dto.ssh_authentication.clone(),
        ssh_identity_source: dto.ssh_identity_source.clone(),
        ssh_identity_file: dto.ssh_identity_file.clone(),
        ssh_credential_ref: dto.ssh_credential_ref.clone(),
        ssh_private_key_ref: dto.ssh_private_key_ref.clone(),
        has_ssh_hop_credentials: has_references(&dto.ssh_jump_credential_refs)
            || has_references(&dto.ssh_jump_private_key_refs),
    }
}

fn has_references(json: &str) -> bool {
    serde_json::from_str::<std::collections::BTreeMap<String, String>>(json)
        .is_ok_and(|references| !references.is_empty())
}

fn apply(mut dto: ffi::ProfileDto, value: ProfileDraft) -> ffi::ProfileDto {
    dto.id = value.id;
    dto.name = value.name;
    dto.driver = value.driver;
    dto.path = value.path;
    dto.read_only = value.read_only;
    dto.host = value.host;
    dto.port = value.port;
    dto.user = value.user;
    dto.tls = value.tls;
    dto.tls_client_identity = value.tls_client_identity;
    dto.credential_ref = value.credential_ref;
    dto.tls_credential_ref = value.tls_credential_ref;
    dto.proxy_options = value.proxy_options;
    dto.proxy_credential_ref = value.proxy_credential_ref;
    dto.ssh_enabled = value.ssh_enabled;
    dto.ssh_host = value.ssh_host;
    dto.ssh_port = value.ssh_port;
    dto.ssh_user = value.ssh_user;
    dto.ssh_authentication = value.ssh_authentication;
    dto.ssh_identity_source = value.ssh_identity_source;
    dto.ssh_identity_file = value.ssh_identity_file;
    dto.ssh_credential_ref = value.ssh_credential_ref;
    dto.ssh_private_key_ref = value.ssh_private_key_ref;
    dto
}

fn secrets(dto: &ffi::ProfileSecretDraftsDto) -> DraftSecrets<'_> {
    DraftSecrets {
        database: SecretDraft {
            text: &dto.database,
            modified: dto.database_modified,
        },
        ssh: SecretDraft {
            text: &dto.ssh,
            modified: dto.ssh_modified,
        },
        tls: SecretDraft {
            text: &dto.tls,
            modified: dto.tls_modified,
        },
        ssh_private_key: SecretDraft {
            text: &dto.ssh_private_key,
            modified: dto.ssh_private_key_modified,
        },
    }
}

pub fn profile_draft_defaults(profile: ffi::ProfileDto) -> ffi::ProfileDto {
    let value = profile_draft::with_defaults(draft(&profile));
    apply(profile, value)
}

pub fn profile_draft_normalize(
    original: ffi::ProfileDto,
    edited: ffi::ProfileDto,
    drafts: &ffi::ProfileSecretDraftsDto,
) -> ffi::ProfileDto {
    let value = profile_draft::normalize(&draft(&original), draft(&edited), &secrets(drafts));
    apply(edited, value)
}

pub fn profile_draft_validate(
    profile: ffi::ProfileDto,
    drafts: &ffi::ProfileSecretDraftsDto,
) -> ffi::ProfileFieldErrorDto {
    if let Some(error) = profile_draft::field_error(&draft(&profile), &secrets(drafts)) {
        return ffi::ProfileFieldErrorDto {
            field: error.field.key().into(),
            message: error.message,
        };
    }
    match crate::profile(profile) {
        Ok(_) => ffi::ProfileFieldErrorDto::default(),
        Err(error) => ffi::ProfileFieldErrorDto {
            field: String::new(),
            message: format!("Invalid connection options. {error}"),
        },
    }
}

fn action(value: DraftAction) -> String {
    match value {
        DraftAction::Keep => "keep",
        DraftAction::Replace => "replace",
        DraftAction::Clear => "clear",
    }
    .into()
}

/// Credentials for saving (`saving`) or for a test/connection with the draft.
pub fn profile_draft_credentials(
    profile: &ffi::ProfileDto,
    drafts: &ffi::ProfileSecretDraftsDto,
    saving: bool,
) -> ffi::ProfileCredentialsDto {
    let value = draft(profile);
    let used = profile_draft::secret_use(&value);
    let typed = secrets(drafts);
    let mut result = ffi::ProfileCredentialsDto {
        save_credentials: drafts.save_credentials,
        ..Default::default()
    };
    let text = |applies: bool, secret: SecretDraft<'_>| {
        if applies {
            secret.text.to_owned()
        } else {
            String::new()
        }
    };
    if saving {
        let save = drafts.save_credentials;
        let stored = |applies: bool, secret: SecretDraft<'_>, saved: &str| {
            let update = profile_draft::saved_action(applies, save, secret, !saved.is_empty());
            let secret = if update == DraftAction::Replace {
                secret.text.to_owned()
            } else {
                String::new()
            };
            (secret, action(update))
        };
        (result.database, result.database_action) =
            stored(used.database, typed.database, &value.credential_ref);
        (result.ssh, result.ssh_action) = stored(used.ssh, typed.ssh, &value.ssh_credential_ref);
        (result.tls, result.tls_action) = stored(used.tls, typed.tls, &value.tls_credential_ref);
        (result.ssh_private_key, result.ssh_private_key_action) = stored(
            used.ssh_private_key,
            typed.ssh_private_key,
            &value.ssh_private_key_ref,
        );
        result.proxy_action = action(DraftAction::Clear);
    } else {
        result.has_database = profile_draft::database_secret_present(used.database, typed.database);
        result.has_ssh = profile_draft::secret_present(used.ssh, typed.ssh);
        result.has_tls = profile_draft::secret_present(used.tls, typed.tls);
        result.has_ssh_private_key =
            profile_draft::secret_present(used.ssh_private_key, typed.ssh_private_key);
        result.database = text(result.has_database, typed.database);
        result.ssh = text(result.has_ssh, typed.ssh);
        result.tls = text(result.has_tls, typed.tls);
        result.ssh_private_key = text(result.has_ssh_private_key, typed.ssh_private_key);
    }
    result
}

pub fn profile_port_for_driver(port: u16, driver: &str) -> u16 {
    profile_draft::port_for_driver(port, driver)
}

pub fn profile_driver_form(driver: &str) -> ffi::ProfileDriverFormDto {
    let form = profile_draft::driver_form(driver);
    ffi::ProfileDriverFormDto {
        server: form.server,
        user_optional: form.user_optional,
        database_selects_server: form.database_selects_server,
    }
}

pub fn profile_has_saved_credentials(profile: &ffi::ProfileDto) -> bool {
    profile_draft::has_saved_credentials(&draft(profile))
}
