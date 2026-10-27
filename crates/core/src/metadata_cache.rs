//! Saved navigator children. The profile storage worker reads and writes them.
use choscordb_driver_api::{DriverError, ErrorKind, ObjectId, SchemaObject};
use choscordb_storage::{CachedChild, Storage};
use tokio::sync::{mpsc, oneshot};

type Reply<T> = oneshot::Sender<Result<T, DriverError>>;

pub(crate) enum Command {
    Read {
        profile: String,
        parent: Option<ObjectId>,
        reply: Reply<Option<Vec<CachedChild>>>,
    },
    Write {
        profile: String,
        parent: Option<ObjectId>,
        children: Vec<CachedChild>,
        reply: Reply<()>,
    },
}
impl Command {
    /// Runs on the storage worker. A caller that stopped waiting gets no reply.
    pub(crate) fn execute(self, storage: Option<&mut Storage>) {
        fn answer<T>(
            reply: Reply<T>,
            storage: Option<&mut Storage>,
            failure: &str,
            work: impl FnOnce(&mut Storage) -> choscordb_storage::Result<T>,
        ) {
            let failed = |message: &str| DriverError::new(ErrorKind::Io, message);
            let result = match storage {
                Some(storage) => work(storage).map_err(|_| failed(failure)),
                None => Err(failed("Could not open local metadata storage")),
            };
            let _ = reply.send(result);
        }
        let now = crate::query_history::now();
        match self {
            Self::Read {
                profile,
                parent,
                reply,
            } => answer(
                reply,
                storage,
                "Could not read the saved navigator children",
                |storage| storage.cached_children(&profile, parent.as_ref(), now),
            ),
            Self::Write {
                profile,
                parent,
                children,
                reply,
            } => answer(
                reply,
                storage,
                "Could not save the navigator children",
                |storage| storage.cache_children(&profile, parent.as_ref(), &children, now),
            ),
        }
    }
}

/// The names and the kinds of navigator children, without details.
pub(crate) fn saved_children(children: &[SchemaObject]) -> Vec<CachedChild> {
    children
        .iter()
        .map(|child| CachedChild {
            id: child.id.clone(),
            name: child.name.clone(),
            qualified_name: child.qualified_name.clone(),
            kind: child.kind.clone(),
            has_children: child.has_children,
        })
        .collect()
}

/// The saved children of one profile.
#[derive(Clone)]
pub(crate) struct CacheTarget {
    pub profile: String,
    pub store: mpsc::Sender<crate::profiles::Command>,
}
impl CacheTarget {
    async fn request<T>(
        &self,
        command: impl FnOnce(Reply<T>) -> Command,
    ) -> Result<T, DriverError> {
        let stopped = || DriverError::new(ErrorKind::Io, "Local metadata storage stopped");
        let (reply, answer) = oneshot::channel();
        self.store
            .send(crate::profiles::Command::MetadataCache(command(reply)))
            .await
            .map_err(|_| stopped())?;
        answer.await.map_err(|_| stopped())?
    }
    /// The saved children of `parent` as navigator rows, without details.
    pub(crate) async fn read(
        &self,
        parent: Option<ObjectId>,
    ) -> Result<Option<Vec<SchemaObject>>, DriverError> {
        let profile = self.profile.clone();
        let saved = self
            .request(|reply| Command::Read {
                profile,
                parent: parent.clone(),
                reply,
            })
            .await?;
        Ok(saved.map(|children| {
            children
                .into_iter()
                .map(|child| SchemaObject {
                    id: child.id,
                    parent: parent.clone(),
                    name: child.name,
                    qualified_name: child.qualified_name,
                    kind: child.kind,
                    has_children: child.has_children,
                    column: None,
                    properties: Vec::new(),
                })
                .collect()
        }))
    }
    /// Saves a complete child list.
    pub(crate) async fn write(
        &self,
        parent: Option<ObjectId>,
        children: Vec<CachedChild>,
    ) -> Result<(), DriverError> {
        let profile = self.profile.clone();
        self.request(|reply| Command::Write {
            profile,
            parent,
            children,
            reply,
        })
        .await
    }
}
