//! Dedicated metadata worker. All SQLite initialization, operations and disposal
//! stay on this thread; the engine only submits bounded, nonblocking commands.
use crate::{ConnectionProfile, Engine, Event, ProfileConfiguration, SubmitError};
use choscordb_credentials::{CredentialStore, MAX_SECRET_BYTES};
use choscordb_driver_api::{
    Connection, ConnectionId, ConnectionOptions, DatabaseDriver, DriverCapabilities,
};
use choscordb_driver_api::{
    DatabaseAuthentication, DriverError, ErrorKind, Secret, resolve_database_authentication,
};
use choscordb_storage::{Storage, StorageError};
use std::{path::PathBuf, sync::Arc, time::Duration};
use tokio::sync::oneshot;
pub enum CredentialUpdate {
    Keep,
    Replace(Secret),
    Clear,
}
pub struct CredentialUpdates {
    pub database: CredentialUpdate,
    pub ssh: CredentialUpdate,
    pub ssh_private_key: CredentialUpdate,
    pub tls: CredentialUpdate,
    pub proxy: CredentialUpdate,
    pub ssh_jumps: std::collections::BTreeMap<String, CredentialUpdate>,
    pub ssh_jump_private_keys: std::collections::BTreeMap<String, CredentialUpdate>,
}
impl CredentialUpdates {
    fn database(update: CredentialUpdate) -> Self {
        Self {
            database: update,
            ssh: CredentialUpdate::Keep,
            ssh_private_key: CredentialUpdate::Keep,
            tls: CredentialUpdate::Keep,
            proxy: CredentialUpdate::Keep,
            ssh_jumps: Default::default(),
            ssh_jump_private_keys: Default::default(),
        }
    }
}
#[derive(Default)]
pub struct ProfileSecrets {
    pub database: Option<Secret>,
    pub ssh: Option<Secret>,
    pub ssh_private_key: Option<Secret>,
    pub tls: Option<Secret>,
    pub proxy: Option<Secret>,
    pub ssh_jumps: std::collections::BTreeMap<String, Secret>,
    pub ssh_jump_private_keys: std::collections::BTreeMap<String, Secret>,
}
use tokio::{
    runtime::Handle,
    sync::{mpsc, watch},
};
pub(crate) enum Command {
    HistoryWrite(crate::query_history::Write),
    Recovery(crate::recovery::Command),
    List(u64),
    Save(Box<ConnectionProfile>, CredentialUpdates, u64),
    Resolve(String, oneshot::Sender<Result<Secret, DriverError>>),
    Duplicate(String, String, String, u64),
    Delete(String, u64),
}
impl Command {
    fn token(&self) -> u64 {
        match self {
            Self::List(t)
            | Self::Save(_, _, t)
            | Self::Duplicate(_, _, _, t)
            | Self::Delete(_, t) => *t,
            Self::Resolve(..) | Self::HistoryWrite(_) => 0,
            Self::Recovery(command) => command.token(),
        }
    }
}
pub(crate) fn start(
    path: Option<PathBuf>,
    capacity: usize,
    events: mpsc::Sender<Event>,
    mut shutdown: watch::Receiver<bool>,
    runtime: Handle,
    credentials: Arc<dyn CredentialStore>,
) -> std::io::Result<mpsc::Sender<Command>> {
    let (sender, mut commands) = mpsc::channel::<Command>(capacity);
    std::thread::Builder::new()
        .name("choscordb-profiles".into())
        .spawn(move || {
            let mut storage = open_storage(path.clone());
            if let Ok((storage, lock)) = storage.as_mut()
                && let Ok(_guard) = acquire_lock(lock.as_ref(), &shutdown)
            {
                let _ = cleanup(storage, credentials.as_ref());
            }
            loop {
                let command = runtime.block_on(async {
                    tokio::select! { biased;
                        _ = shutdown.wait_for(|s| *s) => None,
                        command = commands.recv() => command,
                    }
                });
                let Some(command) = command else { break };
                // A failed open is recoverable (e.g. permissions/path repaired).
                // Retry only in response to an explicit queued operation.
                if storage.is_err() {
                    storage = open_storage(path.clone());
                    if let Ok((storage, lock)) = storage.as_mut()
                        && let Ok(_guard) = acquire_lock(lock.as_ref(), &shutdown)
                    {
                        let _ = cleanup(storage, credentials.as_ref());
                    }
                }
                if let Command::HistoryWrite(job) = command {
                    let query = job.query;
                    let result = match storage.as_mut() {
                        Ok((storage, _)) => job.persist(storage),
                        Err(_) => Err(DriverError::new(
                            ErrorKind::Io,
                            "Could not open local history storage",
                        )),
                    };
                    if let Err(error) = result {
                        runtime.block_on(async {
                            tokio::select! {
                                _ = shutdown.wait_for(|s| *s) => {},
                                _ = events.send(Event::HistoryWriteFailed { query, error }) => {},
                            }
                        });
                    }
                    continue;
                }
                let request_token = command.token();
                let recovery = matches!(&command, Command::Recovery(_));
                let event = match storage.as_mut() {
                    Ok((storage, _)) if recovery => {
                        let Command::Recovery(command) = command else {
                            unreachable!()
                        };
                        crate::recovery::execute(storage, command)
                    }
                    Ok((storage, lock)) => {
                        let guard = acquire_lock(lock.as_ref(), &shutdown);
                        if let Command::Resolve(reference, reply) = command {
                            if !reply.is_closed() {
                                let result = guard.and_then(|_guard| {
                                    credentials.get(&reference).map_err(credential_error)
                                });
                                let _ = reply.send(result);
                            }
                            continue;
                        }
                        guard.and_then(|_guard| execute(storage, credentials.as_ref(), command))
                    }
                    Err(_) => {
                        let error = DriverError::new(
                            ErrorKind::Io,
                            if recovery {
                                "Could not open local workspace storage"
                            } else {
                                "Could not open local profile storage"
                            },
                        );
                        if let Command::Resolve(_, reply) = command {
                            let _ = reply.send(Err(error));
                            continue;
                        }
                        Err(error)
                    }
                }
                .unwrap_or_else(|error| {
                    if recovery {
                        Event::RecoveryFailed {
                            request_token,
                            error,
                        }
                    } else {
                        Event::ProfileFailed {
                            request_token,
                            error,
                        }
                    }
                });
                if !runtime.block_on(async {
                    tokio::select! { biased;
                        _ = shutdown.wait_for(|s| *s) => false,
                        result = events.send(event) => result.is_ok(),
                    }
                }) {
                    break;
                }
            }
        })?;
    Ok(sender)
}
fn credential_error(error: choscordb_credentials::CredentialError) -> DriverError {
    DriverError::new(ErrorKind::Io, error.to_string())
}
fn storage_error(error: StorageError) -> DriverError {
    let (kind, message) = match error {
        StorageError::InvalidProfile => (ErrorKind::InvalidInput, "Profile fields are invalid"),
        StorageError::ResourceLimit => (ErrorKind::ResourceLimit, "Profile storage limit reached"),
        StorageError::ProfileMissing => (ErrorKind::StaleHandle, "Profile no longer exists"),
        _ => (ErrorKind::Io, "Could not complete local profile operation"),
    };
    DriverError::new(kind, message)
}
fn cleanup(storage: &mut Storage, credentials: &dyn CredentialStore) -> Result<(), DriverError> {
    let mut failed = false;
    let referenced: std::collections::HashSet<_> = storage
        .profiles()
        .map_err(storage_error)?
        .into_iter()
        .flat_map(|p| {
            [
                p.credential_ref,
                p.ssh_credential_ref,
                p.ssh_private_key_ref,
                p.tls_credential_ref,
                p.proxy_credential_ref,
            ]
            .into_iter()
            .chain(p.ssh_jump_credential_refs.into_values().map(Some))
            .chain(p.ssh_jump_private_key_refs.into_values().map(Some))
        })
        .flatten()
        .collect();
    for reference in storage
        .pending_credential_cleanup()
        .map_err(storage_error)?
    {
        if referenced.contains(&reference) {
            continue;
        }
        if credentials.delete(&reference).is_ok() {
            storage
                .acknowledge_credential_cleanup(&reference)
                .map_err(storage_error)?;
        } else {
            failed = true;
        }
    }
    if failed {
        Err(DriverError::new(
            ErrorKind::Io,
            "Saved profile changes, but credential cleanup is pending",
        ))
    } else {
        Ok(())
    }
}
fn execute(
    storage: &mut Storage,
    credentials: &dyn CredentialStore,
    command: Command,
) -> Result<Event, DriverError> {
    match command {
        Command::List(request_token) => {
            let _ = cleanup(storage, credentials);
            Ok(Event::Profiles {
                request_token,
                profiles: storage.profiles().map_err(storage_error)?,
            })
        }
        Command::Save(mut profile, mut updates, request_token) => {
            let previous = storage.profile(&profile.id).map_err(storage_error)?;
            let mut created = Vec::new();
            let database = prepare_credential_update(
                storage,
                credentials,
                previous.as_ref().and_then(|p| p.credential_ref.clone()),
                updates.database,
            );
            let database = match database {
                Ok((reference, new_reference)) => {
                    created.extend(new_reference);
                    reference
                }
                Err(error) => return Err(error),
            };
            let ssh = prepare_credential_update(
                storage,
                credentials,
                previous.as_ref().and_then(|p| p.ssh_credential_ref.clone()),
                updates.ssh,
            );
            let ssh = match ssh {
                Ok((reference, new_reference)) => {
                    created.extend(new_reference);
                    reference
                }
                Err(error) => {
                    discard_created_credentials(storage, credentials, &created);
                    return Err(error);
                }
            };
            let ssh_private_key = if inline_target(&profile) {
                match prepare_credential_update(
                    storage,
                    credentials,
                    previous
                        .as_ref()
                        .and_then(|p| p.ssh_private_key_ref.clone()),
                    updates.ssh_private_key,
                ) {
                    Ok((reference, new_reference)) => {
                        created.extend(new_reference);
                        reference
                    }
                    Err(error) => {
                        discard_created_credentials(storage, credentials, &created);
                        return Err(error);
                    }
                }
            } else {
                None
            };
            let tls = prepare_credential_update(
                storage,
                credentials,
                previous.as_ref().and_then(|p| p.tls_credential_ref.clone()),
                updates.tls,
            );
            let tls = match tls {
                Ok((reference, new_reference)) => {
                    created.extend(new_reference);
                    reference
                }
                Err(error) => {
                    discard_created_credentials(storage, credentials, &created);
                    return Err(error);
                }
            };
            let proxy = prepare_credential_update(
                storage,
                credentials,
                previous
                    .as_ref()
                    .and_then(|p| p.proxy_credential_ref.clone()),
                updates.proxy,
            );
            let proxy = match proxy {
                Ok((reference, new_reference)) => {
                    created.extend(new_reference);
                    reference
                }
                Err(error) => {
                    discard_created_credentials(storage, credentials, &created);
                    return Err(error);
                }
            };
            let mut hop_references = std::collections::BTreeMap::new();
            for id in credential_hop_ids(&profile) {
                let old = previous
                    .as_ref()
                    .and_then(|p| p.ssh_jump_credential_refs.get(id))
                    .cloned();
                let update = updates
                    .ssh_jumps
                    .remove(id)
                    .unwrap_or(CredentialUpdate::Keep);
                match prepare_credential_update(storage, credentials, old, update) {
                    Ok((reference, new_reference)) => {
                        created.extend(new_reference);
                        if let Some(reference) = reference {
                            hop_references.insert(id.to_owned(), reference);
                        }
                    }
                    Err(error) => {
                        discard_created_credentials(storage, credentials, &created);
                        return Err(error);
                    }
                }
            }
            profile.ssh_jump_credential_refs = hop_references;
            let mut key_references = std::collections::BTreeMap::new();
            for id in inline_hop_ids(&profile) {
                let old = previous
                    .as_ref()
                    .and_then(|p| p.ssh_jump_private_key_refs.get(id))
                    .cloned();
                let update = updates
                    .ssh_jump_private_keys
                    .remove(id)
                    .unwrap_or(CredentialUpdate::Keep);
                match prepare_credential_update(storage, credentials, old, update) {
                    Ok((reference, new_reference)) => {
                        created.extend(new_reference);
                        if let Some(reference) = reference {
                            key_references.insert(id.to_owned(), reference);
                        }
                    }
                    Err(error) => {
                        discard_created_credentials(storage, credentials, &created);
                        return Err(error);
                    }
                }
            }
            profile.ssh_jump_private_key_refs = key_references;
            profile.ssh_private_key_ref = ssh_private_key;
            profile.proxy_credential_ref = proxy;
            profile.credential_ref = database;
            profile.ssh_credential_ref = ssh;
            profile.tls_credential_ref = tls;
            if let Err(error) = storage.save_profile(&profile) {
                discard_created_credentials(storage, credentials, &created);
                return Err(storage_error(error));
            }
            for reference in created {
                let _ = storage.acknowledge_credential_cleanup(&reference);
            }
            let warning = cleanup(storage, credentials)
                .err()
                .map(|_| "Profile saved; credential cleanup will be retried".into());
            Ok(Event::ProfileSaved {
                request_token,
                profile,
                warning,
            })
        }
        Command::Duplicate(source, id, name, request_token) => Ok(Event::ProfileSaved {
            request_token,
            profile: Box::new(
                storage
                    .duplicate_profile(&source, &id, &name)
                    .map_err(storage_error)?,
            ),
            warning: None,
        }),
        Command::Delete(id, request_token) => {
            storage.delete_profile(&id).map_err(storage_error)?;
            let warning = cleanup(storage, credentials)
                .err()
                .map(|_| "Profile deleted; credential cleanup will be retried".into());
            Ok(Event::ProfileDeleted {
                request_token,
                id,
                warning,
            })
        }
        Command::Resolve(..) | Command::Recovery(_) | Command::HistoryWrite(_) => unreachable!(),
    }
}

fn prepare_credential_update(
    storage: &mut Storage,
    credentials: &dyn CredentialStore,
    previous: Option<String>,
    update: CredentialUpdate,
) -> Result<(Option<String>, Option<String>), DriverError> {
    match update {
        CredentialUpdate::Keep => Ok((previous, None)),
        CredentialUpdate::Clear => Ok((None, None)),
        CredentialUpdate::Replace(secret) => {
            let reference = uuid::Uuid::new_v4().to_string();
            storage
                .queue_credential_cleanup(&reference)
                .map_err(storage_error)?;
            if let Err(error) = credentials.put(&reference, &secret) {
                let _ = storage.acknowledge_credential_cleanup(&reference);
                return Err(credential_error(error));
            }
            Ok((Some(reference.clone()), Some(reference)))
        }
    }
}

fn discard_created_credentials(
    storage: &mut Storage,
    credentials: &dyn CredentialStore,
    references: &[String],
) {
    for reference in references {
        if credentials.delete(reference).is_ok() {
            let _ = storage.acknowledge_credential_cleanup(reference);
        }
    }
}
impl Engine {
    pub fn profile_list(&self, token: u64) -> Result<(), SubmitError> {
        self.submit_profile(Command::List(token))
    }
    pub fn profile_save(&self, profile: ConnectionProfile, token: u64) -> Result<(), SubmitError> {
        self.profile_save_with_secret(profile, CredentialUpdate::Keep, token)
    }
    pub fn profile_save_with_secret(
        &self,
        profile: ConnectionProfile,
        update: CredentialUpdate,
        token: u64,
    ) -> Result<(), SubmitError> {
        if let CredentialUpdate::Replace(secret) = &update {
            validate_secret(secret)?;
        }
        self.profile_save_with_secrets(profile, CredentialUpdates::database(update), token)
    }
    pub fn profile_save_with_secrets(
        &self,
        mut profile: ConnectionProfile,
        updates: CredentialUpdates,
        token: u64,
    ) -> Result<(), SubmitError> {
        // Submitted references are draft metadata. Keep only references owned by
        // the previously saved profile inside the metadata worker.
        profile.ssh_jump_credential_refs.clear();
        profile.ssh_private_key_ref = None;
        profile.ssh_jump_private_key_refs.clear();
        if !inline_target(&profile)
            && matches!(updates.ssh_private_key, CredentialUpdate::Replace(_))
        {
            return Err(SubmitError::InvalidInput);
        }
        if let CredentialUpdate::Replace(secret) = &updates.ssh_private_key {
            validate_private_key(secret)?;
        }
        let inline_ids = inline_hop_ids(&profile);
        let all_hop_ids: Vec<&str> = match &profile.configuration {
            ProfileConfiguration::Postgres { ssh: Some(ssh), .. }
            | ProfileConfiguration::Mysql { ssh: Some(ssh), .. } => ssh
                .options
                .jump_hosts
                .iter()
                .filter_map(|hop| hop.id.as_deref())
                .collect(),
            _ => Vec::new(),
        };
        if updates.ssh_jump_private_keys.len() > 5
            || updates.ssh_jump_private_keys.iter().any(|(id, update)| {
                !all_hop_ids.contains(&id.as_str())
                    || (matches!(update, CredentialUpdate::Replace(_))
                        && !inline_ids.contains(&id.as_str()))
            })
        {
            return Err(SubmitError::InvalidInput);
        }
        for update in updates.ssh_jump_private_keys.values() {
            if let CredentialUpdate::Replace(secret) = update {
                validate_private_key(secret)?;
            }
        }
        let hop_ids = credential_hop_ids(&profile);
        if updates.ssh_jumps.len() > 5
            || updates
                .ssh_jumps
                .keys()
                .any(|id| !hop_ids.contains(&id.as_str()))
        {
            return Err(SubmitError::InvalidInput);
        }
        for update in updates.ssh_jumps.values() {
            if let CredentialUpdate::Replace(secret) = update {
                validate_secret(secret)?;
                if secret.expose().contains(['\0', '\r', '\n']) {
                    return Err(SubmitError::InvalidInput);
                }
            }
        }
        for update in [
            &updates.database,
            &updates.ssh,
            &updates.tls,
            &updates.proxy,
        ] {
            if let CredentialUpdate::Replace(secret) = update {
                validate_secret(secret)?;
            }
        }
        if let CredentialUpdate::Replace(secret) = &updates.proxy
            && let ProfileConfiguration::Postgres {
                proxy: Some(proxy), ..
            }
            | ProfileConfiguration::Mysql {
                proxy: Some(proxy), ..
            } = &profile.configuration
            && proxy.needs_password()
        {
            choscordb_driver_api::validate_socks_secret(proxy, Some(secret))
                .map_err(|_| SubmitError::InvalidInput)?;
        }
        self.submit_profile(Command::Save(Box::new(profile), updates, token))
    }
    pub fn profile_duplicate(
        &self,
        source: String,
        id: String,
        name: String,
        token: u64,
    ) -> Result<(), SubmitError> {
        self.submit_profile(Command::Duplicate(source, id, name, token))
    }
    pub fn profile_delete(&self, id: String, token: u64) -> Result<(), SubmitError> {
        self.submit_profile(Command::Delete(id, token))
    }
    fn submit_profile(&self, command: Command) -> Result<(), SubmitError> {
        self.ensure_running()?;
        match &command {
            Command::Save(profile, _, _) => validate(profile)?,
            Command::Duplicate(source, id, name, _) => {
                bounded(source)?;
                bounded(id)?;
                bounded(name)?;
            }
            Command::Delete(id, _) => bounded(id)?,
            Command::List(_)
            | Command::Resolve(..)
            | Command::Recovery(_)
            | Command::HistoryWrite(_) => {}
        }
        self.profiles.try_send(command).map_err(crate::map_send)
    }
    pub fn test_profile(
        &self,
        profile: ConnectionProfile,
        password: Option<Secret>,
        request_token: u64,
    ) -> Result<(), SubmitError> {
        self.test_profile_with_secrets(
            profile,
            ProfileSecrets {
                ssh_private_key: None,
                ssh_jump_private_keys: Default::default(),
                ssh_jumps: Default::default(),
                database: password,
                ssh: None,
                tls: None,
                proxy: None,
            },
            request_token,
        )
    }
    pub fn test_profile_with_secrets(
        &self,
        mut profile: ConnectionProfile,
        secrets: ProfileSecrets,
        request_token: u64,
    ) -> Result<(), SubmitError> {
        self.ensure_running()?;
        prune_draft_hop_references(&mut profile);
        validate(&profile)?;
        validate_hop_secrets(&profile, &secrets.ssh_jumps)?;
        validate_inline_secrets(&profile, &secrets)?;
        for secret in [
            &secrets.database,
            &secrets.ssh,
            &secrets.tls,
            &secrets.proxy,
        ]
        .into_iter()
        .flatten()
        {
            validate_secret(secret)?;
        }
        let driver_id = match profile.configuration {
            ProfileConfiguration::Sqlite { .. } => "sqlite",
            ProfileConfiguration::Postgres { .. } => "postgres",
            ProfileConfiguration::Mysql { .. } => "mysql",
        };
        let driver = self
            .drivers
            .get(driver_id)
            .cloned()
            .ok_or(SubmitError::UnknownDriver)?;
        let permit = self
            .profile_tests
            .clone()
            .try_acquire_owned()
            .map_err(|_| SubmitError::ResourceLimit)?;
        let events = self.events_tx.clone();
        let mut shutdown = self.shutdown.subscribe();
        let commands = self.profiles.clone();
        let test_timeout = authentication_budget(
            &profile.configuration.connection_options(None, None),
            &profile.authentication,
        );
        let authentication = profile.authentication.clone();
        let references = CredentialReferences::from_profile(&profile);
        self.runtime.as_ref().ok_or(SubmitError::ShuttingDown)?.spawn(async move {
            let _permit = permit;
            let result = tokio::select! { biased;
                _ = shutdown.wait_for(|s| *s) => return,
                result = tokio::time::timeout(test_timeout, async {
                    let mut options = profile_options(&profile, secrets);
                    if let ConnectionOptions::Postgres { ssh: Some(ssh), .. }
                    | ConnectionOptions::Mysql { ssh: Some(ssh), .. } = &mut options {
                        ssh.options.share_tunnels = false;
                    }
                    resolve_options(&commands, references, &authentication, &mut options).await?;
                    let mut connection = driver.connect(options).await?;
                    connection.close().await
                }) => result.unwrap_or_else(|_| Err(DriverError::new(ErrorKind::Timeout, "Connection test timed out"))),
            };
            let event = match result { Ok(()) => Event::ProfileTested { request_token }, Err(error) => Event::ProfileFailed { request_token, error } };
            tokio::select! { biased;
                _ = shutdown.wait_for(|s| *s) => {},
                _ = events.send(event) => {},
            }
        });
        Ok(())
    }
}

fn credential_hop_ids(profile: &ConnectionProfile) -> Vec<&str> {
    match &profile.configuration {
        ProfileConfiguration::Postgres { ssh: Some(ssh), .. }
        | ProfileConfiguration::Mysql { ssh: Some(ssh), .. } => ssh
            .options
            .jump_hosts
            .iter()
            .filter(|hop| hop.authentication.uses_secret())
            .filter_map(|hop| hop.id.as_deref())
            .collect(),
        _ => Vec::new(),
    }
}
fn inline_target(profile: &ConnectionProfile) -> bool {
    match &profile.configuration {
        ProfileConfiguration::Postgres { ssh: Some(ssh), .. }
        | ProfileConfiguration::Mysql { ssh: Some(ssh), .. } => {
            ssh.authentication == choscordb_driver_api::SshAuthentication::PublicKey
                && ssh.identity_source == choscordb_driver_api::SshIdentitySource::Inline
        }
        _ => false,
    }
}
fn inline_hop_ids(profile: &ConnectionProfile) -> Vec<&str> {
    match &profile.configuration {
        ProfileConfiguration::Postgres { ssh: Some(ssh), .. }
        | ProfileConfiguration::Mysql { ssh: Some(ssh), .. } => ssh
            .options
            .jump_hosts
            .iter()
            .filter(|hop| {
                hop.authentication == choscordb_driver_api::SshJumpAuthentication::PublicKey
                    && hop.identity_source == choscordb_driver_api::SshIdentitySource::Inline
            })
            .filter_map(|hop| hop.id.as_deref())
            .collect(),
        _ => Vec::new(),
    }
}
fn validate_private_key(key: &Secret) -> Result<(), SubmitError> {
    let value = key.expose();
    if value.trim().is_empty() || value.len() > MAX_SECRET_BYTES || value.contains('\0') {
        Err(SubmitError::InvalidInput)
    } else {
        Ok(())
    }
}
fn prune_draft_hop_references(profile: &mut ConnectionProfile) {
    let active = credential_hop_ids(profile)
        .into_iter()
        .map(str::to_owned)
        .collect::<std::collections::BTreeSet<_>>();
    profile
        .ssh_jump_credential_refs
        .retain(|id, _| active.contains(id));
    let inline = inline_hop_ids(profile)
        .into_iter()
        .map(str::to_owned)
        .collect::<std::collections::BTreeSet<_>>();
    profile
        .ssh_jump_private_key_refs
        .retain(|id, _| inline.contains(id));
    if !inline_target(profile) {
        profile.ssh_private_key_ref = None;
    }
}

fn validate_hop_secrets(
    profile: &ConnectionProfile,
    secrets: &std::collections::BTreeMap<String, Secret>,
) -> Result<(), SubmitError> {
    if secrets.len() > 5 {
        return Err(SubmitError::InvalidInput);
    }
    let ssh = match &profile.configuration {
        ProfileConfiguration::Postgres { ssh: Some(ssh), .. }
        | ProfileConfiguration::Mysql { ssh: Some(ssh), .. } => ssh,
        _ => return Ok(()),
    };
    for (id, secret) in secrets {
        let hop = ssh
            .options
            .jump_hosts
            .iter()
            .find(|hop| hop.id.as_deref() == Some(id));
        let Some(hop) = hop else {
            return Err(SubmitError::InvalidInput);
        };
        if hop.authentication.uses_secret() {
            validate_secret(secret)?;
            if secret.expose().contains(['\0', '\r', '\n']) {
                return Err(SubmitError::InvalidInput);
            }
        }
    }
    Ok(())
}
fn validate_inline_secrets(
    profile: &ConnectionProfile,
    secrets: &ProfileSecrets,
) -> Result<(), SubmitError> {
    if secrets.ssh_jump_private_keys.len() > 5 {
        return Err(SubmitError::InvalidInput);
    }
    if inline_target(profile)
        && let Some(key) = &secrets.ssh_private_key
    {
        validate_private_key(key)?;
    }
    let ssh = match &profile.configuration {
        ProfileConfiguration::Postgres { ssh: Some(ssh), .. }
        | ProfileConfiguration::Mysql { ssh: Some(ssh), .. } => ssh,
        _ => return Ok(()),
    };
    for (id, key) in &secrets.ssh_jump_private_keys {
        let Some(hop) = ssh
            .options
            .jump_hosts
            .iter()
            .find(|hop| hop.id.as_deref() == Some(id))
        else {
            return Err(SubmitError::InvalidInput);
        };
        if hop.authentication == choscordb_driver_api::SshJumpAuthentication::PublicKey
            && hop.identity_source == choscordb_driver_api::SshIdentitySource::Inline
        {
            validate_private_key(key)?;
        }
    }
    Ok(())
}

fn validate(profile: &ConnectionProfile) -> Result<(), SubmitError> {
    profile.validate().map_err(|error| match error {
        StorageError::ResourceLimit => SubmitError::ResourceLimit,
        _ => SubmitError::InvalidInput,
    })
}
fn bounded(value: &str) -> Result<(), SubmitError> {
    if value.is_empty() || value.contains('\0') {
        return Err(SubmitError::InvalidInput);
    }
    if value.len() > choscordb_storage::MAX_PROFILE_BYTES {
        return Err(SubmitError::ResourceLimit);
    }
    Ok(())
}

fn validate_secret(secret: &Secret) -> Result<(), SubmitError> {
    if secret.expose().len() > 16 * 1024 {
        Err(SubmitError::ResourceLimit)
    } else {
        Ok(())
    }
}
pub(super) async fn resolve(
    commands: &mpsc::Sender<Command>,
    reference: Option<String>,
    password: Option<Secret>,
) -> Result<Option<Secret>, DriverError> {
    if password.is_some() || reference.is_none() {
        return Ok(password);
    }
    let (reply, response) = oneshot::channel();
    commands
        .try_send(Command::Resolve(reference.unwrap(), reply))
        .map_err(|_| {
            DriverError::new(
                ErrorKind::ResourceLimit,
                "Credential worker is busy or unavailable",
            )
        })?;
    response
        .await
        .map_err(|_| DriverError::new(ErrorKind::Io, "Credential worker stopped"))?
        .map(Some)
}
fn profile_options(profile: &ConnectionProfile, secrets: ProfileSecrets) -> ConnectionOptions {
    let mut options = profile
        .configuration
        .connection_options(secrets.database, secrets.ssh);
    if let ConnectionOptions::Postgres {
        tls_identity: Some(identity),
        ..
    }
    | ConnectionOptions::Mysql {
        tls_identity: Some(identity),
        ..
    } = &mut options
    {
        identity.password = secrets.tls;
    }
    if let ConnectionOptions::Postgres { proxy_secret, .. }
    | ConnectionOptions::Mysql { proxy_secret, .. } = &mut options
    {
        *proxy_secret = secrets.proxy;
    }
    if let ConnectionOptions::Postgres {
        ssh_jump_secrets, ..
    }
    | ConnectionOptions::Mysql {
        ssh_jump_secrets, ..
    } = &mut options
    {
        *ssh_jump_secrets = secrets.ssh_jumps;
    }
    if let ConnectionOptions::Postgres {
        ssh_private_key,
        ssh_jump_private_keys,
        ..
    }
    | ConnectionOptions::Mysql {
        ssh_private_key,
        ssh_jump_private_keys,
        ..
    } = &mut options
    {
        *ssh_private_key = secrets.ssh_private_key;
        *ssh_jump_private_keys = secrets.ssh_jump_private_keys;
    }
    options
}

#[derive(Clone)]
struct CredentialReferences {
    database: Option<String>,
    ssh: Option<String>,
    ssh_private_key: Option<String>,
    tls: Option<String>,
    proxy: Option<String>,
    ssh_jumps: std::collections::BTreeMap<String, String>,
    ssh_jump_private_keys: std::collections::BTreeMap<String, String>,
}
impl CredentialReferences {
    fn from_profile(profile: &ConnectionProfile) -> Self {
        Self {
            database: profile.credential_ref.clone(),
            ssh: profile.ssh_credential_ref.clone(),
            ssh_private_key: profile.ssh_private_key_ref.clone(),
            tls: profile.tls_credential_ref.clone(),
            proxy: profile.proxy_credential_ref.clone(),
            ssh_jumps: profile.ssh_jump_credential_refs.clone(),
            ssh_jump_private_keys: profile.ssh_jump_private_key_refs.clone(),
        }
    }
}
// Resolve only credentials consumed by the selected transport. A stale saved
// reference from a previous transport must not prompt or block the connection.
async fn resolve_options(
    commands: &mpsc::Sender<Command>,
    references: CredentialReferences,
    authentication: &DatabaseAuthentication,
    options: &mut ConnectionOptions,
) -> Result<(), DriverError> {
    resolve_database_authentication(authentication, options).await?;
    if let ConnectionOptions::Postgres {
        password,
        ssh_secret,
        ssh_private_key,
        ssh_jump_secrets,
        ssh_jump_private_keys,
        ssh,
        tls,
        tls_identity,
        proxy,
        proxy_secret,
        ..
    }
    | ConnectionOptions::Mysql {
        password,
        ssh_secret,
        ssh_private_key,
        ssh_jump_secrets,
        ssh_jump_private_keys,
        ssh,
        tls,
        tls_identity,
        proxy,
        proxy_secret,
        ..
    } = options
    {
        if authentication.is_password() {
            *password = resolve(commands, references.database, password.take()).await?;
        }
        *ssh_secret = if ssh.as_ref().is_some_and(|settings| {
            settings.authentication != choscordb_driver_api::SshAuthentication::Agent
        }) {
            resolve(commands, references.ssh, ssh_secret.take()).await?
        } else {
            None
        };
        *ssh_private_key = if ssh.as_ref().is_some_and(|settings| {
            settings.authentication == choscordb_driver_api::SshAuthentication::PublicKey
                && settings.identity_source == choscordb_driver_api::SshIdentitySource::Inline
        }) {
            resolve(commands, references.ssh_private_key, ssh_private_key.take()).await?
        } else {
            None
        };
        if let Some(settings) = ssh {
            let active: std::collections::BTreeSet<_> = settings
                .options
                .jump_hosts
                .iter()
                .filter(|hop| hop.authentication.uses_secret())
                .filter_map(|hop| hop.id.as_ref())
                .cloned()
                .collect();
            ssh_jump_secrets.retain(|id, _| active.contains(id));
            for id in active {
                let transient = ssh_jump_secrets.remove(&id);
                if let Some(secret) =
                    resolve(commands, references.ssh_jumps.get(&id).cloned(), transient).await?
                {
                    ssh_jump_secrets.insert(id, secret);
                }
            }
            let inline: std::collections::BTreeSet<_> = settings
                .options
                .jump_hosts
                .iter()
                .filter(|hop| {
                    hop.authentication == choscordb_driver_api::SshJumpAuthentication::PublicKey
                        && hop.identity_source == choscordb_driver_api::SshIdentitySource::Inline
                })
                .filter_map(|hop| hop.id.as_ref())
                .cloned()
                .collect();
            ssh_jump_private_keys.retain(|id, _| inline.contains(id));
            for id in inline {
                let transient = ssh_jump_private_keys.remove(&id);
                if let Some(key) = resolve(
                    commands,
                    references.ssh_jump_private_keys.get(&id).cloned(),
                    transient,
                )
                .await?
                {
                    ssh_jump_private_keys.insert(id, key);
                }
            }
        } else {
            ssh_jump_secrets.clear();
            ssh_jump_private_keys.clear();
        }
        *proxy_secret = if proxy
            .as_ref()
            .is_some_and(|settings| settings.needs_password())
        {
            resolve(commands, references.proxy, proxy_secret.take()).await?
        } else {
            None
        };
        if *tls != choscordb_driver_api::TlsMode::Disable {
            if let Some(identity) = tls_identity {
                identity.password =
                    resolve(commands, references.tls, identity.password.take()).await?;
            }
        } else {
            *tls_identity = None;
        }
    }
    Ok(())
}

fn authentication_budget(
    options: &ConnectionOptions,
    authentication: &DatabaseAuthentication,
) -> Duration {
    let connection = match options {
        ConnectionOptions::Postgres { ssh, .. } | ConnectionOptions::Mysql { ssh, .. } => {
            ssh.as_ref().map_or(Duration::from_secs(15), |settings| {
                Duration::from_secs(u64::from(settings.options.connect_timeout_seconds))
            })
        }
        _ => Duration::from_secs(15),
    };
    connection + Duration::from_secs(5) + authentication.command_timeout()
}

struct ProfileDriver {
    authentication: DatabaseAuthentication,
    inner: Arc<dyn DatabaseDriver>,
    commands: mpsc::Sender<Command>,
    references: CredentialReferences,
}
#[async_trait::async_trait]
impl DatabaseDriver for ProfileDriver {
    fn id(&self) -> &'static str {
        self.inner.id()
    }
    fn capabilities(&self) -> DriverCapabilities {
        self.inner.capabilities()
    }
    async fn connect(
        &self,
        options: ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn Connection>> {
        self.connect_options(options).await
    }
}
impl ProfileDriver {
    async fn connect_options(
        &self,
        mut options: ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn Connection>> {
        let budget = authentication_budget(&options, &self.authentication);
        tokio::time::timeout(budget, async {
            resolve_options(
                &self.commands,
                self.references.clone(),
                &self.authentication,
                &mut options,
            )
            .await?;
            self.inner.connect(options).await
        })
        .await
        .map_err(|_| {
            DriverError::new(
                ErrorKind::Timeout,
                "Connection timed out while preparing credentials or transport",
            )
        })?
    }
}
impl Engine {
    pub fn connect_profile(
        &mut self,
        profile: ConnectionProfile,
        password: Option<Secret>,
    ) -> Result<ConnectionId, SubmitError> {
        self.connect_profile_with_secrets(
            profile,
            ProfileSecrets {
                ssh_private_key: None,
                ssh_jump_private_keys: Default::default(),
                ssh_jumps: Default::default(),
                database: password,
                ssh: None,
                tls: None,
                proxy: None,
            },
        )
    }
    pub fn connect_profile_with_secrets(
        &mut self,
        mut profile: ConnectionProfile,
        secrets: ProfileSecrets,
    ) -> Result<ConnectionId, SubmitError> {
        self.ensure_running()?;
        prune_draft_hop_references(&mut profile);
        validate(&profile)?;
        validate_hop_secrets(&profile, &secrets.ssh_jumps)?;
        validate_inline_secrets(&profile, &secrets)?;
        for secret in [
            &secrets.database,
            &secrets.ssh,
            &secrets.tls,
            &secrets.proxy,
        ]
        .into_iter()
        .flatten()
        {
            validate_secret(secret)?;
        }
        let id = match profile.configuration {
            ProfileConfiguration::Sqlite { .. } => "sqlite",
            ProfileConfiguration::Postgres { .. } => "postgres",
            ProfileConfiguration::Mysql { .. } => "mysql",
        };
        let inner = self
            .drivers
            .get(id)
            .cloned()
            .ok_or(SubmitError::UnknownDriver)?;
        let driver = Arc::new(ProfileDriver {
            authentication: profile.authentication.clone(),
            inner,
            commands: self.profiles.clone(),
            references: CredentialReferences::from_profile(&profile),
        });
        self.connect_driver_with_timeout(
            driver,
            profile_options(&profile, secrets),
            Some(authentication_budget(
                &profile.configuration.connection_options(None, None),
                &profile.authentication,
            )),
        )
    }
}

// One sidecar lock orders publication and orphan cleanup across application instances.
// Its path is canonicalized so equivalent metadata paths share the same lock.
fn open_storage(path: Option<PathBuf>) -> Result<(Storage, Option<std::fs::File>), ()> {
    match path {
        None => Storage::in_memory().map(|s| (s, None)).map_err(|_| ()),
        Some(path) => {
            if let Some(parent) = path.parent().filter(|p| !p.as_os_str().is_empty()) {
                std::fs::create_dir_all(parent).map_err(|_| ())?;
            }
            let storage = Storage::open(&path).map_err(|_| ())?;
            let path = std::fs::canonicalize(path).map_err(|_| ())?;
            let mut name = path.file_name().ok_or(())?.to_os_string();
            name.push(".credentials.lock");
            let lock = std::fs::OpenOptions::new()
                .read(true)
                .write(true)
                .create(true)
                .truncate(false)
                .open(path.with_file_name(name))
                .map_err(|_| ())?;
            Ok((storage, Some(lock)))
        }
    }
}
struct LockGuard<'a>(&'a std::fs::File);
impl Drop for LockGuard<'_> {
    fn drop(&mut self) {
        let _ = fs2::FileExt::unlock(self.0);
    }
}
fn acquire_lock<'a>(
    file: Option<&'a std::fs::File>,
    shutdown: &watch::Receiver<bool>,
) -> Result<Option<LockGuard<'a>>, DriverError> {
    let Some(file) = file else {
        return Ok(None);
    };
    loop {
        if *shutdown.borrow() {
            return Err(DriverError::new(ErrorKind::Io, "Credential worker stopped"));
        }
        match fs2::FileExt::try_lock_exclusive(file) {
            Ok(()) => return Ok(Some(LockGuard(file))),
            Err(error) if error.raw_os_error() == fs2::lock_contended_error().raw_os_error() => {
                std::thread::sleep(Duration::from_millis(20))
            }
            Err(_) => {
                return Err(DriverError::new(
                    ErrorKind::Io,
                    "Could not lock profile credentials",
                ));
            }
        }
    }
}
