//! Dedicated metadata worker. All SQLite initialization, operations and disposal
//! stay on this thread; the engine only submits bounded, nonblocking commands.
use crate::{ConnectionProfile, Engine, Event, ProfileConfiguration, SubmitError};
use choscordb_credentials::{CredentialStore, MAX_SECRET_BYTES};
use choscordb_driver_api::{
    Connection, ConnectionId, ConnectionOptions, DatabaseDriver, DriverCapabilities,
};
use choscordb_driver_api::{DriverError, ErrorKind, Secret};
use choscordb_storage::{Storage, StorageError};
use std::{path::PathBuf, sync::Arc, time::Duration};
use tokio::sync::oneshot;
pub enum CredentialUpdate {
    Keep,
    Replace(Secret),
    Clear,
}
use tokio::{
    runtime::Handle,
    sync::{mpsc, watch},
};
pub(crate) enum Command {
    HistoryWrite(crate::query_history::Write),
    Recovery(crate::recovery::Command),
    List(u64),
    Save(ConnectionProfile, CredentialUpdate, u64),
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
        .filter_map(|p| p.credential_ref)
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
        Command::Save(mut profile, update, request_token) => {
            let previous = storage
                .profile(&profile.id)
                .map_err(storage_error)?
                .and_then(|p| p.credential_ref);
            let new_reference = match update {
                CredentialUpdate::Keep => {
                    profile.credential_ref = previous;
                    None
                }
                CredentialUpdate::Clear => {
                    profile.credential_ref = None;
                    None
                }
                CredentialUpdate::Replace(secret) => {
                    let reference = uuid::Uuid::new_v4().to_string();
                    // Persist recovery intent before OS publication, including process crashes.
                    storage
                        .queue_credential_cleanup(&reference)
                        .map_err(storage_error)?;
                    credentials
                        .put(&reference, &secret)
                        .map_err(credential_error)?;
                    profile.credential_ref = Some(reference.clone());
                    Some(reference)
                }
            };
            if let Err(error) = storage.save_profile(&profile) {
                if let Some(reference) = new_reference
                    && credentials.delete(&reference).is_ok()
                {
                    let _ = storage.acknowledge_credential_cleanup(&reference);
                }
                return Err(storage_error(error));
            }
            if let Some(reference) = new_reference {
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
            profile: storage
                .duplicate_profile(&source, &id, &name)
                .map_err(storage_error)?,
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
        self.submit_profile(Command::Save(profile, update, token))
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
        self.ensure_running()?;
        validate(&profile)?;
        if let Some(secret) = &password {
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
        let reference = profile.credential_ref.clone();
        self.runtime.as_ref().ok_or(SubmitError::ShuttingDown)?.spawn(async move {
            let _permit = permit;
            let result = tokio::select! { biased;
                _ = shutdown.wait_for(|s| *s) => return,
                result = tokio::time::timeout(Duration::from_secs(10), async {
                    let password = resolve(&commands, reference, password).await?;
                    let mut connection = driver.connect(profile.configuration.connection_options(password)).await?;
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
    if secret.expose().len() > MAX_SECRET_BYTES {
        Err(SubmitError::ResourceLimit)
    } else {
        Ok(())
    }
}
async fn resolve(
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
struct ProfileDriver {
    inner: Arc<dyn DatabaseDriver>,
    commands: mpsc::Sender<Command>,
    reference: Option<String>,
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
        mut options: ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn Connection>> {
        if let ConnectionOptions::Postgres { password, .. }
        | ConnectionOptions::Mysql { password, .. } = &mut options
        {
            *password = resolve(&self.commands, self.reference.clone(), password.take()).await?;
        }
        self.inner.connect(options).await
    }
}
impl Engine {
    pub fn connect_profile(
        &mut self,
        profile: ConnectionProfile,
        password: Option<Secret>,
    ) -> Result<ConnectionId, SubmitError> {
        self.ensure_running()?;
        validate(&profile)?;
        if let Some(secret) = &password {
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
            inner,
            commands: self.profiles.clone(),
            reference: profile.credential_ref.clone(),
        });
        self.connect_driver(driver, profile.configuration.connection_options(password))
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
