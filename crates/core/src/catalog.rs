//! The catalog session of a Connect. Catalog reads use a second session of the
//! Connect, so a long query on the first session does not hold the navigator.
use crate::{
    Event,
    actor::{CatalogRead, Command, EventSink, Request, catalog_read},
};
use choscordb_driver_api::*;
use std::{
    sync::{
        Arc, OnceLock,
        atomic::{AtomicBool, Ordering},
    },
    time::{Duration, Instant},
};

/// After the catalog session fails to open, the query session answers the reads
/// for this time. Then the next read tries to open the catalog session again.
pub(crate) const CATALOG_RETRY: Duration = Duration::from_secs(30);
use tokio::sync::{mpsc, watch};

/// Where the engine sends the catalog reads of one Connect.
#[derive(Default)]
pub(crate) struct CatalogRoute {
    /// The queue of the catalog session, when the driver can open one.
    reads: OnceLock<mpsc::Sender<CatalogRead>>,
    /// The query session has an open transaction. Its uncommitted catalog
    /// changes are visible only on that session.
    in_transaction: AtomicBool,
}
impl CatalogRoute {
    /// The catalog session queue, or `None` when the query session must answer.
    pub(crate) fn reads(&self) -> Option<&mpsc::Sender<CatalogRead>> {
        if self.in_transaction() {
            None
        } else {
            self.reads.get()
        }
    }
    /// Whether the query session can have uncommitted catalog changes. Its
    /// children are then not saved for the next connection.
    pub(crate) fn in_transaction(&self) -> bool {
        self.in_transaction.load(Ordering::Acquire)
    }
    /// Follows the transaction state of the query session. When the state is
    /// unknown or cannot be read, the query session answers, because only it can
    /// show its own uncommitted changes.
    pub(crate) fn follow(&self, state: Result<Option<bool>>) {
        let active = !matches!(state, Ok(Some(false)));
        self.in_transaction.store(active, Ordering::Release);
    }
    /// Starts the catalog session task of a connected query session.
    pub(crate) fn start(
        &self,
        connection: &dyn Connection,
        session: CatalogSession,
        capacity: usize,
    ) {
        let Some(opener) = connection.sibling_sessions() else {
            return;
        };
        let (sender, reads) = mpsc::channel(capacity);
        if self.reads.set(sender).is_ok() {
            tokio::spawn(run(opener, reads, session));
        }
    }
}

/// What the catalog session task shares with its query session.
pub(crate) struct CatalogSession {
    pub id: ConnectionId,
    /// The query session answers a read when the catalog session cannot open.
    pub fallback: mpsc::WeakSender<Request>,
    pub events: EventSink,
    pub shutdown: watch::Receiver<bool>,
    pub grace: Duration,
}

/// Opens the catalog session at the first read and closes it on disconnect.
async fn run(
    opener: Arc<dyn SessionOpener>,
    mut reads: mpsc::Receiver<CatalogRead>,
    mut task: CatalogSession,
) {
    let mut session: Option<Box<dyn Connection>> = None;
    let mut retry_at: Option<Instant> = None;
    loop {
        let read = tokio::select! {
            biased;
            _ = crate::operation::signalled(&mut task.shutdown) => break,
            read = reads.recv() => match read {
                Some(read) => read,
                None => break,
            },
        };
        let waiting = retry_at.is_some_and(|at| Instant::now() < at);
        let current = match &mut session {
            Some(current) => Some(current),
            None if waiting => None,
            None => {
                let opened = tokio::select! {
                    biased;
                    _ = crate::operation::signalled(&mut task.shutdown) => break,
                    opened = opener.open() => opened,
                };
                match opened {
                    Ok(opened) => {
                        retry_at = None;
                        Some(session.insert(opened))
                    }
                    Err(error) => {
                        // Show the failure once for this retry interval.
                        retry_at = Some(Instant::now() + CATALOG_RETRY);
                        task.events
                            .send(Event::OperationFailed {
                                connection: task.id,
                                error,
                            })
                            .await;
                        None
                    }
                }
            }
        };
        let current = match current {
            Some(current) => current,
            // The query session answers. A stopped query session reports its own disconnect.
            None => {
                if let Some(actor) = task.fallback.upgrade() {
                    let request = Request {
                        command: Command::Catalog(read),
                    };
                    if actor.send(request).await.is_err() {
                        break;
                    }
                }
                continue;
            }
        };
        let error = catalog_read(
            &mut **current,
            task.id,
            read,
            &mut task.shutdown,
            &task.events,
        )
        .await;
        // A lost catalog session opens again at the next read.
        if error.is_some_and(|error| {
            matches!(error.kind, ErrorKind::Disconnected | ErrorKind::Connection)
        }) && let Some(mut lost) = session.take()
        {
            let _ = tokio::time::timeout(task.grace, lost.close()).await;
        }
    }
    if let Some(mut current) = session {
        let _ = tokio::time::timeout(task.grace, current.close()).await;
    }
}
