//! Connection-profile draft policy for the native profile form.
//!
//! The form collects raw widget values; these functions decide defaults, which
//! saved credential references a draft keeps, which secrets apply, how each
//! secret is stored or sent, and which field an invalid draft reports.

/// The profile values the form edits, plus the saved references it carries.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ProfileDraft {
    pub id: String,
    pub name: String,
    pub driver: String,
    pub path: String,
    pub read_only: bool,
    pub host: String,
    pub port: u16,
    pub user: String,
    pub tls: String,
    pub tls_client_identity: String,
    pub credential_ref: String,
    pub tls_credential_ref: String,
    pub proxy_options: String,
    pub proxy_credential_ref: String,
    pub ssh_enabled: bool,
    pub ssh_host: String,
    pub ssh_port: u16,
    pub ssh_user: String,
    pub ssh_authentication: String,
    pub ssh_identity_source: String,
    pub ssh_identity_file: String,
    pub ssh_credential_ref: String,
    pub ssh_private_key_ref: String,
    pub has_ssh_hop_credentials: bool,
}

/// A secret as typed in the form: its text and whether the user edited it.
#[derive(Clone, Copy, Debug, Default)]
pub struct SecretDraft<'a> {
    pub text: &'a str,
    pub modified: bool,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct DraftSecrets<'a> {
    pub database: SecretDraft<'a>,
    pub ssh: SecretDraft<'a>,
    pub tls: SecretDraft<'a>,
    pub ssh_private_key: SecretDraft<'a>,
}

/// Which secrets the draft's transport uses.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SecretUse {
    pub database: bool,
    pub ssh: bool,
    pub tls: bool,
    pub ssh_private_key: bool,
}

/// How a saved credential changes when the draft is saved.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DraftAction {
    Keep,
    Replace,
    Clear,
}

/// The form field an invalid draft reports, with its user-facing message.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DraftField {
    Name,
    Path,
    Host,
    User,
    Password,
    SshEnabled,
    SshHost,
    SshUser,
    SshIdentityFile,
    SshSecret,
    TlsSecret,
    SshPrivateKey,
}

impl DraftField {
    pub fn key(self) -> &'static str {
        match self {
            Self::Name => "name",
            Self::Path => "path",
            Self::Host => "host",
            Self::User => "user",
            Self::Password => "password",
            Self::SshEnabled => "ssh_enabled",
            Self::SshHost => "ssh_host",
            Self::SshUser => "ssh_user",
            Self::SshIdentityFile => "ssh_identity_file",
            Self::SshSecret => "ssh_secret",
            Self::TlsSecret => "tls_secret",
            Self::SshPrivateKey => "ssh_private_key",
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DraftFieldError {
    pub field: DraftField,
    pub message: String,
}

/// Bytes accepted for a password-like secret typed into the form.
pub const MAX_FORM_SECRET_BYTES: usize = 16 * 1024;
/// Why pasted private key text cannot be used; `None` when it can.
pub fn private_key_text_error(text: &str) -> Option<&'static str> {
    (text.len() > choscordb_credentials::MAX_SECRET_BYTES || text.contains('\0'))
        .then_some("Private keys must be at most 64 KiB and contain no NUL characters.")
}
const MAX_NAME_BYTES: usize = 1024;
const MAX_PATH_BYTES: usize = 16 * 1024;

fn server(driver: &str) -> bool {
    driver == "postgres" || driver == "mysql"
}

/// Which connection fields a driver uses and what their blank values mean.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DriverForm {
    /// Host, port, user, TLS and SSH apply; otherwise the profile is a file path.
    pub server: bool,
    /// A blank user name authenticates anonymously instead of being rejected.
    pub user_optional: bool,
    /// A blank database connects to the server without selecting a database;
    /// otherwise the driver defaults it to the user name.
    pub database_selects_server: bool,
}

pub fn driver_form(driver: &str) -> DriverForm {
    let mysql = driver == "mysql";
    DriverForm {
        server: server(driver),
        user_optional: mysql,
        database_selects_server: mysql,
    }
}

/// The standard port for a server driver; 0 for drivers without a port.
pub fn default_port(driver: &str) -> u16 {
    match driver {
        "postgres" => 5432,
        "mysql" => 3306,
        _ => 0,
    }
}

/// The port after the user picks another driver: a port still at another
/// server's standard value follows the new driver, a custom port is kept.
pub fn port_for_driver(port: u16, driver: &str) -> u16 {
    let standard = default_port(driver);
    if standard != 0 && (port == 5432 || port == 3306) {
        standard
    } else {
        port
    }
}

/// Fill the values a new or legacy draft leaves empty, including a new id.
pub fn with_defaults(mut draft: ProfileDraft) -> ProfileDraft {
    if draft.id.is_empty() {
        draft.id = uuid::Uuid::new_v4().to_string();
    }
    if draft.driver.is_empty() {
        draft.driver = "sqlite".into();
    }
    if draft.host.is_empty() {
        draft.host = "localhost".into();
    }
    if draft.port == 0 {
        draft.port = match default_port(&draft.driver) {
            0 => default_port("postgres"),
            port => port,
        };
    }
    if draft.tls.is_empty() {
        draft.tls = "prefer".into();
    }
    if draft.ssh_port == 0 {
        draft.ssh_port = 22;
    }
    if draft.ssh_authentication.is_empty() {
        // Legacy profiles stored only a key file; without one they used the agent.
        draft.ssh_authentication = if draft.ssh_identity_file.is_empty() {
            "agent".into()
        } else {
            "public_key".into()
        };
    }
    if draft.ssh_identity_source.is_empty() {
        draft.ssh_identity_source = "file".into();
    }
    draft
}

/// Apply the form's edits to the profile it started from. Saved references that
/// no longer match the edited authentication are dropped so they are neither
/// used for a connection nor kept on save.
pub fn normalize(
    original: &ProfileDraft,
    mut edited: ProfileDraft,
    secrets: &DraftSecrets<'_>,
) -> ProfileDraft {
    if edited.id.is_empty() {
        edited.id = uuid::Uuid::new_v4().to_string();
    }
    edited.ssh_enabled = server(&edited.driver) && edited.ssh_enabled;
    if secrets.database.modified && secrets.database.text.is_empty() {
        edited.credential_ref.clear();
    }
    let public_key = edited.ssh_authentication == "public_key";
    if edited.ssh_authentication != original.ssh_authentication
        || (public_key && edited.ssh_identity_file != original.ssh_identity_file)
    {
        edited.ssh_credential_ref.clear();
    }
    if !public_key {
        edited.ssh_identity_source = "file".into();
    }
    if !public_key || edited.ssh_identity_source != "file" {
        edited.ssh_identity_file.clear();
    }
    if !edited.ssh_enabled
        || !public_key
        || edited.ssh_identity_source != "inline"
        || edited.ssh_authentication != original.ssh_authentication
        || edited.ssh_identity_source != original.ssh_identity_source
    {
        edited.ssh_private_key_ref.clear();
    }
    // SOCKS proxies are no longer supported; the form never carries them forward.
    edited.proxy_options.clear();
    edited.proxy_credential_ref.clear();
    edited
}

pub fn secret_use(draft: &ProfileDraft) -> SecretUse {
    let ssh = server(&draft.driver) && draft.ssh_enabled;
    let public_key = draft.ssh_authentication == "public_key";
    SecretUse {
        database: server(&draft.driver),
        ssh: ssh && draft.ssh_authentication != "agent",
        tls: server(&draft.driver) && !draft.tls_client_identity.is_empty(),
        ssh_private_key: ssh && public_key && draft.ssh_identity_source == "inline",
    }
}

/// How saving the draft changes one saved credential.
pub fn saved_action(
    applies: bool,
    save_credentials: bool,
    secret: SecretDraft<'_>,
    has_saved: bool,
) -> DraftAction {
    if !applies || !save_credentials {
        DraftAction::Clear
    } else if secret.modified || !secret.text.is_empty() {
        if secret.text.is_empty() {
            DraftAction::Clear
        } else {
            DraftAction::Replace
        }
    } else if has_saved {
        DraftAction::Keep
    } else {
        DraftAction::Clear
    }
}

/// Whether a test or connection sends the typed database password. An emptied
/// password field means passwordless authentication (see `normalize`).
pub fn database_secret_present(applies: bool, secret: SecretDraft<'_>) -> bool {
    applies && !secret.text.is_empty()
}

/// Whether a test or connection sends this typed secret instead of a saved one.
pub fn secret_present(applies: bool, secret: SecretDraft<'_>) -> bool {
    applies && (secret.modified || !secret.text.is_empty())
}

/// The id and name for a copy of a profile named `name`.
pub fn duplicate_identity(name: &str) -> (String, String) {
    (uuid::Uuid::new_v4().to_string(), format!("{name} copy"))
}

pub fn has_saved_credentials(draft: &ProfileDraft) -> bool {
    !draft.credential_ref.is_empty()
        || !draft.ssh_credential_ref.is_empty()
        || !draft.ssh_private_key_ref.is_empty()
        || !draft.tls_credential_ref.is_empty()
        || !draft.proxy_credential_ref.is_empty()
        || draft.has_ssh_hop_credentials
}

fn error(field: DraftField, message: &str) -> Option<DraftFieldError> {
    Some(DraftFieldError {
        field,
        message: message.into(),
    })
}

/// The first form field an invalid draft must fix, in form order. Settings the
/// form does not show are validated with the full profile afterwards.
pub fn field_error(draft: &ProfileDraft, secrets: &DraftSecrets<'_>) -> Option<DraftFieldError> {
    if draft.name.trim().is_empty() {
        return error(DraftField::Name, "Enter a profile name.");
    }
    if draft.name.len() > MAX_NAME_BYTES || draft.name.contains('\0') {
        return error(DraftField::Name, "Connection name is invalid or too long.");
    }
    if draft.driver == "sqlite" {
        if draft.path.trim().is_empty() {
            return error(DraftField::Path, "Enter a database file path or :memory:.");
        }
        if draft.path.len() > MAX_PATH_BYTES || draft.path.contains('\0') {
            return error(DraftField::Path, "Database path is invalid or too long.");
        }
        if draft.path.starts_with("file:")
            && choscordb_driver_api::validate_sqlite_uri(&draft.path, draft.read_only).is_err()
        {
            return error(DraftField::Path, "Enter a valid SQLite file URI.");
        }
        return None;
    }
    let used = secret_use(draft);
    for (applies, secret, field) in [
        (used.database, secrets.database, DraftField::Password),
        (used.ssh, secrets.ssh, DraftField::SshSecret),
        (used.tls, secrets.tls, DraftField::TlsSecret),
    ] {
        if applies && secret.text.len() > MAX_FORM_SECRET_BYTES {
            return error(field, "Credential exceeds the supported size.");
        }
    }
    let socket = draft.host.starts_with('/');
    if socket && draft.tls != "disable" && draft.tls != "prefer" {
        return error(
            DraftField::Host,
            "Unix sockets do not support required or verified TLS.",
        );
    }
    if socket && draft.ssh_enabled {
        return error(
            DraftField::SshEnabled,
            "Unix sockets cannot use an SSH tunnel.",
        );
    }
    if !socket && choscordb_driver_api::tcp_host(&draft.host).is_err() {
        return error(
            DraftField::Host,
            "Host must be a hostname or IPv4/IPv6 address. Enter the port separately; omit URLs and usernames.",
        );
    }
    let form = driver_form(&draft.driver);
    if form.server && !form.user_optional && draft.user.is_empty() {
        return error(DraftField::User, "Enter the database username.");
    }
    if !draft.ssh_enabled {
        return None;
    }
    if choscordb_driver_api::tcp_host(&draft.ssh_host).is_err() {
        return error(
            DraftField::SshHost,
            "SSH host must be a hostname or IPv4/IPv6 address. Enter the SSH port separately; omit URLs and usernames.",
        );
    }
    if draft.ssh_user.is_empty() {
        return error(DraftField::SshUser, "Enter the SSH username.");
    }
    if draft.ssh_authentication == "public_key"
        && draft.ssh_identity_source == "file"
        && draft.ssh_identity_file.trim().is_empty()
    {
        return error(
            DraftField::SshIdentityFile,
            "Choose an SSH private key file.",
        );
    }
    if draft.ssh_authentication == "password"
        && secrets.ssh.text.is_empty()
        && draft.ssh_credential_ref.is_empty()
    {
        return error(DraftField::SshSecret, "Enter the SSH password.");
    }
    if used.ssh_private_key
        && let Some(message) = private_key_text_error(secrets.ssh_private_key.text)
    {
        return error(DraftField::SshPrivateKey, message);
    }
    None
}
